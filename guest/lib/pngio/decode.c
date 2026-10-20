#include <stdckdint.h>

#include "../png/inflate.h"
#include "pngio_internal.h"

#define CHUNK(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))
#define CHUNK_IHDR CHUNK('I', 'H', 'D', 'R')
#define CHUNK_PLTE CHUNK('P', 'L', 'T', 'E')
#define CHUNK_IDAT CHUNK('I', 'D', 'A', 'T')
#define CHUNK_IEND CHUNK('I', 'E', 'N', 'D')
#define CHUNK_TRNS CHUNK('t', 'R', 'N', 'S')
#define CHUNK_GAMA CHUNK('g', 'A', 'M', 'A')
#define CHUNK_CHRM CHUNK('c', 'H', 'R', 'M')
#define CHUNK_SRGB CHUNK('s', 'R', 'G', 'B')
#define CHUNK_ICCP CHUNK('i', 'C', 'C', 'P')
#define CHUNK_PHYS CHUNK('p', 'H', 'Y', 's')
#define CHUNK_BKGD CHUNK('b', 'K', 'G', 'D')
#define CHUNK_SBIT CHUNK('s', 'B', 'I', 'T')
#define CHUNK_TIME CHUNK('t', 'I', 'M', 'E')
#define CHUNK_EXIF CHUNK('e', 'X', 'I', 'f')
#define CHUNK_ACTL CHUNK('a', 'c', 'T', 'L')
#define CHUNK_TEXT CHUNK('t', 'E', 'X', 't')
#define CHUNK_ZTXT CHUNK('z', 'T', 'X', 't')
#define CHUNK_ITXT CHUNK('i', 'T', 'X', 't')

/* the most a metadata item may inflate to. */
#define MAX_METADATA (64u << 20)

/* cap on stored text items and extra chunks each, further ones are dropped. */
#define MAX_ITEMS 16384u

/* deflate cannot expand more than 1032 to 1, plus the headers. */
#define MAX_INFLATE_RATIO 1032u

struct MimePngioDecoder {
    MimeAllocator alloc;
    const uint8_t *data;
    size_t size;
    unsigned flags;
    MimePngioInfo info;
    int format;
    PngioConv conv;
    unsigned bits;  /* bits per pixel of the file. */
    unsigned bpp;   /* bytes per pixel for filtering, at least 1. */
    PngioPass pass[7];
    unsigned npass;
    size_t raw_size;      /* filtered image data, filter bytes included. */
    size_t max_rowbytes;

    size_t first_idat; /* offset of the first IDAT chunk. */
    unsigned idat_count;
    size_t idat_total;

    MimePngioText *texts;
    unsigned text_cap;
    MimePngioChunk *chunks;
    unsigned chunk_cap;
    const uint8_t *iccp;
    size_t iccp_size;

    uint8_t *raw;           /* inflated data followed by a zero row. */
    uint32_t filtered_rows; /* non interlaced: rows already unfiltered. */
    uint8_t *cache;         /* interlaced: the decoded image for the row api. */
    size_t cache_stride;
};

static const char empty_string[1] = "";

static bool chunk_letter(uint8_t c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static int grow(const MimeAllocator *a, void **array, unsigned *cap, size_t elem)
{
    unsigned ncap = *cap ? *cap * 2 : 8;
    void *n = mime_alloc(a, (size_t)ncap * elem);

    if (!n)
        return MIME_PNGIO_ERR_NOMEM;
    if (*array)
        __builtin_memcpy(n, *array, (size_t)*cap * elem);
    mime_free(a, *array);
    *array = n;
    *cap = ncap;
    return MIME_PNGIO_OK;
}

int mime_pngio_native_format(const MimePngioInfo *info)
{
    unsigned d = info->bit_depth;
    bool key = info->has_trns_key;

    switch (info->color_type) {
    case MIME_PNGIO_COLOR_GRAY:
        if (d == 16)
            return key ? MIME_PNGIO_FMT_RGBA64 : MIME_PNGIO_FMT_GRAY16;
        if (key)
            return MIME_PNGIO_FMT_BGRA32;
        return d == 8 ? MIME_PNGIO_FMT_GRAY8 : d == 4 ? MIME_PNGIO_FMT_GRAY4 : d == 2 ? MIME_PNGIO_FMT_GRAY2 : MIME_PNGIO_FMT_GRAY1;
    case MIME_PNGIO_COLOR_PALETTE:
        return d == 8 ? MIME_PNGIO_FMT_INDEXED8 : d == 4 ? MIME_PNGIO_FMT_INDEXED4 : d == 2 ? MIME_PNGIO_FMT_INDEXED2 : MIME_PNGIO_FMT_INDEXED1;
    case MIME_PNGIO_COLOR_RGB:
        if (d == 16)
            return key ? MIME_PNGIO_FMT_RGBA64 : MIME_PNGIO_FMT_RGB48;
        return key ? MIME_PNGIO_FMT_BGRA32 : MIME_PNGIO_FMT_BGR24;
    case MIME_PNGIO_COLOR_GRAY_ALPHA:
    case MIME_PNGIO_COLOR_RGBA:
        return d == 16 ? MIME_PNGIO_FMT_RGBA64 : MIME_PNGIO_FMT_BGRA32;
    default:
        return MIME_PNGIO_FMT_NONE;
    }
}

int mime_pngio_raw_format(const MimePngioInfo *info)
{
    unsigned d = info->bit_depth;

    switch (info->color_type) {
    case MIME_PNGIO_COLOR_GRAY:
        return d == 16 ? MIME_PNGIO_FMT_GRAY16 : d == 8 ? MIME_PNGIO_FMT_GRAY8 : d == 4 ? MIME_PNGIO_FMT_GRAY4 : d == 2 ? MIME_PNGIO_FMT_GRAY2 : MIME_PNGIO_FMT_GRAY1;
    case MIME_PNGIO_COLOR_PALETTE:
        return mime_pngio_native_format(info);
    case MIME_PNGIO_COLOR_RGB:
        return d == 16 ? MIME_PNGIO_FMT_RGB48 : MIME_PNGIO_FMT_RGB24;
    case MIME_PNGIO_COLOR_GRAY_ALPHA:
        return d == 16 ? MIME_PNGIO_FMT_GRAY_ALPHA32 : MIME_PNGIO_FMT_GRAY_ALPHA16;
    case MIME_PNGIO_COLOR_RGBA:
        return d == 16 ? MIME_PNGIO_FMT_RGBA64 : MIME_PNGIO_FMT_RGBA32;
    default:
        return MIME_PNGIO_FMT_NONE;
    }
}

static unsigned file_channels(unsigned color_type)
{
    switch (color_type) {
    case MIME_PNGIO_COLOR_RGB:
        return 3;
    case MIME_PNGIO_COLOR_GRAY_ALPHA:
        return 2;
    case MIME_PNGIO_COLOR_RGBA:
        return 4;
    default:
        return 1;
    }
}

static int parse_ihdr(MimePngioDecoder *dec, const uint8_t *b, uint64_t max_pixels)
{
    MimePngioInfo *info = &dec->info;
    uint32_t w = pngio_be32(b), h = pngio_be32(b + 4);
    unsigned depth = b[8], type = b[9];

    if (!w || !h || w > 0x7fffffffu || h > 0x7fffffffu)
        return MIME_PNGIO_ERR_FORMAT;
    if (b[10] != 0 || b[11] != 0 || b[12] > 1)
        return MIME_PNGIO_ERR_FORMAT;
    switch (type) {
    case MIME_PNGIO_COLOR_GRAY:
        if (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16)
            return MIME_PNGIO_ERR_FORMAT;
        break;
    case MIME_PNGIO_COLOR_PALETTE:
        if (depth != 1 && depth != 2 && depth != 4 && depth != 8)
            return MIME_PNGIO_ERR_FORMAT;
        break;
    case MIME_PNGIO_COLOR_RGB:
    case MIME_PNGIO_COLOR_GRAY_ALPHA:
    case MIME_PNGIO_COLOR_RGBA:
        if (depth != 8 && depth != 16)
            return MIME_PNGIO_ERR_FORMAT;
        break;
    default:
        return MIME_PNGIO_ERR_FORMAT;
    }
    if ((uint64_t)w * h > max_pixels)
        return MIME_PNGIO_ERR_UNSUPPORTED;
    info->width = w;
    info->height = h;
    info->bit_depth = (uint8_t)depth;
    info->color_type = (uint8_t)type;
    info->interlaced = b[12] == 1;
    info->has_alpha = type == MIME_PNGIO_COLOR_GRAY_ALPHA || type == MIME_PNGIO_COLOR_RGBA;
    dec->bits = file_channels(type) * depth;
    dec->bpp = dec->bits >= 8 ? dec->bits / 8 : 1;
    return MIME_PNGIO_OK;
}

/* length of the null terminated string at p, within n bytes, or n + 1 when it is not terminated. */
static size_t nul_len(const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (!p[i])
            return i;
    return n + 1;
}

static int add_text(MimePngioDecoder *dec, const MimePngioText *t)
{
    if (dec->info.text_count >= MAX_ITEMS)
        return MIME_PNGIO_OK;
    if (dec->info.text_count == dec->text_cap) {
        int r = grow(&dec->alloc, (void **)&dec->texts, &dec->text_cap, sizeof(*dec->texts));
        if (r)
            return r;
    }
    dec->texts[dec->info.text_count++] = *t;
    return MIME_PNGIO_OK;
}

static int add_chunk(MimePngioDecoder *dec, const uint8_t *type, const uint8_t *body, uint32_t len, unsigned position)
{
    MimePngioChunk *c;

    if (dec->info.chunk_count >= MAX_ITEMS)
        return MIME_PNGIO_OK;
    if (dec->info.chunk_count == dec->chunk_cap) {
        int r = grow(&dec->alloc, (void **)&dec->chunks, &dec->chunk_cap, sizeof(*dec->chunks));
        if (r)
            return r;
    }
    c = &dec->chunks[dec->info.chunk_count++];
    __builtin_memcpy(c->type, type, 4);
    c->data = body;
    c->size = len;
    c->position = position;
    return MIME_PNGIO_OK;
}

/* text chunks: a malformed one is dropped. */
static int parse_text(MimePngioDecoder *dec, uint32_t type, const uint8_t *b, uint32_t len)
{
    MimePngioText t;
    size_t kl = nul_len(b, len < 80 ? len : 80);

    if (kl == 0 || kl > 79 || kl >= len)
        return MIME_PNGIO_OK;
    __builtin_memset(&t, 0, sizeof(t));
    t.keyword = (const char *)b;
    t.language = empty_string;
    t.translated_keyword = empty_string;
    b += kl + 1;
    len -= (uint32_t)kl + 1;

    if (type == CHUNK_TEXT) {
        t.kind = MIME_PNGIO_TEXT_LATIN1;
    } else if (type == CHUNK_ZTXT) {
        if (len < 1 || b[0] != 0)
            return MIME_PNGIO_OK;
        t.kind = MIME_PNGIO_TEXT_ZTXT;
        t.compressed = true;
        b++;
        len--;
    } else {
        if (len < 2 || b[0] > 1 || (b[0] == 1 && b[1] != 0))
            return MIME_PNGIO_OK;
        t.kind = MIME_PNGIO_TEXT_ITXT;
        t.utf8 = true;
        t.compressed = b[0] == 1;
        b += 2;
        len -= 2;
        size_t ll = nul_len(b, len);
        if (ll >= len)
            return MIME_PNGIO_OK;
        t.language = (const char *)b;
        b += ll + 1;
        len -= (uint32_t)ll + 1;
        size_t tl = nul_len(b, len);
        if (tl >= len)
            return MIME_PNGIO_OK;
        t.translated_keyword = (const char *)b;
        b += tl + 1;
        len -= (uint32_t)tl + 1;
    }
    t.data = b;
    t.data_size = len;
    return add_text(dec, &t);
}

static bool valid_time(const MimePngioTime *t)
{
    return t->month >= 1 && t->month <= 12 && t->day >= 1 && t->day <= 31 && t->hour <= 23 && t->minute <= 59 &&
           t->second <= 60;
}

/*
 * ancillary chunks that describe the image, honoured before the image data only.
 * returns 1 when the chunk was consumed, 0 when it goes to the extra chunk list.
 */
static int parse_known(MimePngioDecoder *dec, uint32_t type, const uint8_t *b, uint32_t len)
{
    MimePngioInfo *info = &dec->info;
    unsigned ct = info->color_type;

    switch (type) {
    case CHUNK_GAMA:
        if (len == 4 && !info->has_gama) {
            info->gama = pngio_be32(b);
            info->has_gama = true;
        }
        return 1;
    case CHUNK_CHRM:
        if (len == 32 && !info->has_chrm) {
            for (unsigned i = 0; i < 8; i++)
                info->chrm[i] = pngio_be32(b + 4 * i);
            info->has_chrm = true;
        }
        return 1;
    case CHUNK_SRGB:
        if (len == 1 && !info->has_srgb) {
            info->srgb_intent = b[0];
            info->has_srgb = true;
        }
        return 1;
    case CHUNK_ICCP: {
        size_t nl = nul_len(b, len < 80 ? len : 80);
        if (info->has_iccp || nl == 0 || nl > 79 || len < nl + 3 || b[nl + 1] != 0)
            return 1;
        __builtin_memcpy(info->iccp_name, b, nl);
        info->iccp_name[nl] = 0;
        dec->iccp = b + nl + 2;
        dec->iccp_size = len - nl - 2;
        info->has_iccp = true;
        return 1;
    }
    case CHUNK_PHYS:
        if (len == 9 && !info->has_phys) {
            info->phys_x = pngio_be32(b);
            info->phys_y = pngio_be32(b + 4);
            info->phys_unit = b[8];
            info->has_phys = true;
        }
        return 1;
    case CHUNK_BKGD: {
        unsigned need = ct == MIME_PNGIO_COLOR_PALETTE ? 1 : (ct == MIME_PNGIO_COLOR_RGB || ct == MIME_PNGIO_COLOR_RGBA) ? 6 : 2;
        if (len != need || info->has_bkgd)
            return 1;
        if (need == 1) {
            info->bkgd[0] = b[0];
        } else {
            unsigned mask = info->bit_depth == 16 ? 0xffffu : (1u << info->bit_depth) - 1;
            for (unsigned i = 0; i < need / 2; i++)
                info->bkgd[i] = (uint16_t)(pngio_be16(b + 2 * i) & mask);
        }
        info->has_bkgd = true;
        return 1;
    }
    case CHUNK_SBIT: {
        unsigned need = ct == MIME_PNGIO_COLOR_GRAY ? 1 : ct == MIME_PNGIO_COLOR_GRAY_ALPHA ? 2 : ct == MIME_PNGIO_COLOR_RGBA ? 4 : 3;
        if (len != need || info->has_sbit)
            return 1;
        __builtin_memcpy(info->sbit, b, need);
        info->sbit_count = (uint8_t)need;
        info->has_sbit = true;
        return 1;
    }
    default:
        return 0;
    }
}

/* chunks that may sit anywhere. */
static int parse_anywhere(MimePngioDecoder *dec, uint32_t type, const uint8_t *b, uint32_t len)
{
    MimePngioInfo *info = &dec->info;

    switch (type) {
    case CHUNK_TIME:
        if (len == 7 && !info->has_time) {
            MimePngioTime t = {(uint16_t)pngio_be16(b), b[2], b[3], b[4], b[5], b[6]};
            if (valid_time(&t)) {
                info->time = t;
                info->has_time = true;
            }
        }
        return 1;
    case CHUNK_EXIF:
        if (len > 0 && !info->has_exif) {
            info->exif = b;
            info->exif_size = len;
            info->has_exif = true;
        }
        return 1;
    case CHUNK_ACTL:
        if (len == 8 && !info->has_actl) {
            info->actl_frames = pngio_be32(b);
            info->actl_plays = pngio_be32(b + 4);
            info->has_actl = true;
        }
        return 1;
    default:
        return 0;
    }
}

static void resolve_trns(MimePngioDecoder *dec, const uint8_t *b, uint32_t len)
{
    MimePngioInfo *info = &dec->info;
    unsigned mask = info->bit_depth == 16 ? 0xffffu : (1u << info->bit_depth) - 1;

    switch (info->color_type) {
    case MIME_PNGIO_COLOR_GRAY:
        if (len < 2)
            return;
        info->trns_key[0] = (uint16_t)(pngio_be16(b) & mask);
        info->has_trns_key = true;
        break;
    case MIME_PNGIO_COLOR_RGB:
        if (len < 6)
            return;
        for (unsigned i = 0; i < 3; i++)
            info->trns_key[i] = (uint16_t)(pngio_be16(b + 2 * i) & mask);
        info->has_trns_key = true;
        break;
    case MIME_PNGIO_COLOR_PALETTE:
        if (!len)
            return;
        for (uint32_t i = 0; i < len && i < info->palette_count; i++)
            info->palette_alpha[i] = b[i];
        info->has_palette_alpha = true;
        break;
    default:
        return;
    }
    info->has_alpha = true;
}

static void close_decoder(MimePngioDecoder *dec);

static int parse_chunks(MimePngioDecoder *dec, uint64_t max_pixels)
{
    const uint8_t *d = dec->data;
    size_t size = dec->size, pos = 8;
    bool seen_ihdr = false, seen_plte = false, idat_run = false, idat_done = false;
    const uint8_t *trns = nullptr;
    uint32_t trns_len = 0;
    bool skip_crc = (dec->flags & MIME_PNGIO_SKIP_CRC) != 0;
    MimePngioInfo *info = &dec->info;
    int r;

    __builtin_memset(info->palette_alpha, 255, sizeof(info->palette_alpha));

    for (;;) {
        if (size - pos < 8) {
            /* a file cut after complete image data is accepted. */
            if (dec->idat_count)
                break;
            return MIME_PNGIO_ERR_FORMAT;
        }
        uint32_t len = pngio_be32(d + pos);
        uint32_t type = pngio_be32(d + pos + 4);
        const uint8_t *tp = d + pos + 4;
        const uint8_t *body = d + pos + 8;
        bool bad_name = !chunk_letter(tp[0]) || !chunk_letter(tp[1]) || !chunk_letter(tp[2]) || !chunk_letter(tp[3]);

        if (bad_name || len > 0x7fffffffu || size - pos < 12 || len > size - pos - 12) {
            /* the last IDAT may be cut short, whatever it holds is used. */
            if (!bad_name && len <= 0x7fffffffu && type == CHUNK_IDAT && seen_ihdr && (idat_run || !dec->idat_count) &&
                (info->color_type != MIME_PNGIO_COLOR_PALETTE || info->has_plte)) {
                if (!dec->idat_count)
                    dec->first_idat = pos;
                dec->idat_count++;
                dec->idat_total += size - pos - 8;
                break;
            }
            /* a file cut after complete image data is accepted. */
            if (dec->idat_count)
                break;
            return MIME_PNGIO_ERR_FORMAT;
        }
        bool critical = !(tp[0] & 0x20);
        if (!skip_crc && mime_pngio_crc32(0, tp, (size_t)len + 4) != pngio_be32(body + len)) {
            if (critical && type != CHUNK_IEND)
                return MIME_PNGIO_ERR_CRC;
            if (!critical) {
                pos += (size_t)len + 12;
                continue;
            }
        }

        if (!seen_ihdr && type != CHUNK_IHDR)
            return MIME_PNGIO_ERR_FORMAT;
        if (idat_run && type != CHUNK_IDAT) {
            idat_run = false;
            idat_done = true;
        }
        unsigned position = dec->idat_count ? MIME_PNGIO_POS_AFTER_IDAT
                            : seen_plte    ? MIME_PNGIO_POS_BEFORE_IDAT
                                           : MIME_PNGIO_POS_BEFORE_PLTE;

        switch (type) {
        case CHUNK_IHDR:
            if (seen_ihdr || len != 13)
                return MIME_PNGIO_ERR_FORMAT;
            r = parse_ihdr(dec, body, max_pixels);
            if (r)
                return r;
            seen_ihdr = true;
            break;
        case CHUNK_PLTE:
            if (dec->idat_count || seen_plte || info->color_type == MIME_PNGIO_COLOR_GRAY ||
                info->color_type == MIME_PNGIO_COLOR_GRAY_ALPHA) {
                break;
            }
            if (len == 0 || len % 3 || len > 768) {
                if (info->color_type == MIME_PNGIO_COLOR_PALETTE)
                    return MIME_PNGIO_ERR_FORMAT;
                break;
            }
            seen_plte = true;
            info->has_plte = true;
            info->palette_count = (uint16_t)(len / 3);
            if (info->color_type == MIME_PNGIO_COLOR_PALETTE && info->palette_count > (1u << info->bit_depth))
                info->palette_count = (uint16_t)(1u << info->bit_depth);
            if (info->color_type != MIME_PNGIO_COLOR_PALETTE && info->palette_count > 256)
                info->palette_count = 256;
            __builtin_memcpy(info->palette, body, (size_t)info->palette_count * 3);
            break;
        case CHUNK_TRNS:
            if (!dec->idat_count && !trns) {
                trns = body;
                trns_len = len;
            }
            break;
        case CHUNK_IDAT:
            if (idat_done)
                return MIME_PNGIO_ERR_FORMAT;
            if (info->color_type == MIME_PNGIO_COLOR_PALETTE && !info->has_plte)
                return MIME_PNGIO_ERR_FORMAT;
            if (!dec->idat_count)
                dec->first_idat = pos;
            dec->idat_count++;
            dec->idat_total += len;
            idat_run = true;
            break;
        case CHUNK_IEND:
            if (!dec->idat_count)
                return MIME_PNGIO_ERR_FORMAT;
            goto done;
        case CHUNK_TEXT:
        case CHUNK_ZTXT:
        case CHUNK_ITXT:
            r = parse_text(dec, type, body, len);
            if (r)
                return r;
            break;
        default:
            if (critical)
                return MIME_PNGIO_ERR_UNSUPPORTED;
            if (parse_anywhere(dec, type, body, len))
                break;
            if (!dec->idat_count && parse_known(dec, type, body, len))
                break;
            r = add_chunk(dec, tp, body, len, position);
            if (r)
                return r;
            break;
        }
        pos += (size_t)len + 12;
    }
done:
    if (!dec->idat_count)
        return MIME_PNGIO_ERR_FORMAT;
    if (trns)
        resolve_trns(dec, trns, trns_len);
    return MIME_PNGIO_OK;
}

int mime_pngio_open_limit(const void *data, size_t size, const MimeAllocator *alloc, unsigned flags,
                          uint64_t max_pixels, MimePngioDecoder **out)
{
    MimePngioDecoder *dec;
    int r;

    if (!out)
        return MIME_PNGIO_ERR_PARAM;
    *out = nullptr;
    if (!data || !alloc || !alloc->alloc || !alloc->free)
        return MIME_PNGIO_ERR_PARAM;
    if (!mime_pngio_is_png(data, size))
        return MIME_PNGIO_ERR_FORMAT;
    dec = mime_alloc(alloc, sizeof(*dec));
    if (!dec)
        return MIME_PNGIO_ERR_NOMEM;
    __builtin_memset(dec, 0, sizeof(*dec));
    dec->alloc = *alloc;
    dec->data = data;
    dec->size = size;
    dec->flags = flags;

    r = parse_chunks(dec, max_pixels ? max_pixels : MIME_PNGIO_MAX_PIXELS);
    if (!r) {
        MimePngioInfo *info = &dec->info;
        size_t total = 0;

        dec->format = (flags & MIME_PNGIO_RAW) ? mime_pngio_raw_format(info) : mime_pngio_native_format(info);
        pngio_conv_init(&dec->conv, info, dec->format);
        dec->npass = pngio_pass_layout(info->width, info->height, info->interlaced, dec->bits, dec->pass);
        if (!dec->npass)
            r = MIME_PNGIO_ERR_UNSUPPORTED;
        for (unsigned i = 0; !r && i < dec->npass; i++) {
            const PngioPass *ps = &dec->pass[i];
            size_t rowsize;
            if (ckd_mul(&rowsize, ps->rowbytes + 1, (size_t)ps->h) || ckd_add(&total, total, rowsize))
                r = MIME_PNGIO_ERR_UNSUPPORTED;
            if (ps->rowbytes > dec->max_rowbytes)
                dec->max_rowbytes = ps->rowbytes;
        }
        dec->raw_size = total;
    }
    if (r) {
        close_decoder(dec);
        return r;
    }
    *out = dec;
    return MIME_PNGIO_OK;
}

int mime_pngio_open(const void *data, size_t size, const MimeAllocator *alloc, unsigned flags, MimePngioDecoder **out)
{
    return mime_pngio_open_limit(data, size, alloc, flags, 0, out);
}

void mime_pngio_release(MimePngioDecoder *dec)
{
    if (!dec)
        return;
    mime_free(&dec->alloc, dec->raw);
    mime_free(&dec->alloc, dec->cache);
    dec->raw = nullptr;
    dec->cache = nullptr;
    dec->filtered_rows = 0;
}

static void close_decoder(MimePngioDecoder *dec)
{
    MimeAllocator a = dec->alloc;

    mime_pngio_release(dec);
    mime_free(&a, dec->texts);
    mime_free(&a, dec->chunks);
    mime_free(&a, dec);
}

void mime_pngio_close(MimePngioDecoder *dec)
{
    if (dec)
        close_decoder(dec);
}

const MimePngioInfo *mime_pngio_info(const MimePngioDecoder *dec)
{
    return &dec->info;
}

int mime_pngio_format(const MimePngioDecoder *dec)
{
    return dec->format;
}

/* joins the IDAT payloads, or points into the file when there is one. */
static int gather_idat(const MimePngioDecoder *dec, const uint8_t **z, uint8_t **owned)
{
    const uint8_t *d = dec->data;
    size_t pos = dec->first_idat;

    *owned = nullptr;
    if (dec->idat_count == 1) {
        *z = d + pos + 8;
        return MIME_PNGIO_OK;
    }
    uint8_t *buf = mime_alloc(&dec->alloc, dec->idat_total ? dec->idat_total : 1);
    if (!buf)
        return MIME_PNGIO_ERR_NOMEM;
    size_t off = 0;
    while (off < dec->idat_total && dec->size - pos >= 8 && pngio_be32(d + pos + 4) == CHUNK_IDAT) {
        size_t len = pngio_be32(d + pos), avail = dec->size - pos - 8;
        if (len > avail)
            len = avail;
        /* never past what the chunk walk counted. */
        if (len > dec->idat_total - off)
            len = dec->idat_total - off;
        __builtin_memcpy(buf + off, d + pos + 8, len);
        off += len;
        pos += len + 12;
        if (len == avail)
            break;
    }
    *z = buf;
    *owned = buf;
    return MIME_PNGIO_OK;
}

/* inflates the image data into dec->raw. */
static int ensure_raw(MimePngioDecoder *dec)
{
    const uint8_t *z;
    uint8_t *owned;
    size_t got, alloc_size, state_off;
    int r;

    if (dec->raw)
        return MIME_PNGIO_OK;
    /* a deflate stream of this size cannot hold that many bytes, so refuse before allocating. */
    if (dec->raw_size > (uint64_t)dec->idat_total * MAX_INFLATE_RATIO + 4096)
        return MIME_PNGIO_ERR_DATA;
    /* the image, the zero row, then the decoder state, which is too big for a guest stack. */
    state_off = (dec->raw_size + dec->max_rowbytes + 15) & ~(size_t)15;
    if (state_off < dec->raw_size || ckd_add(&alloc_size, state_off, sizeof(MimeInflate)))
        return MIME_PNGIO_ERR_UNSUPPORTED;
    uint8_t *raw = mime_alloc(&dec->alloc, alloc_size);
    if (!raw)
        return MIME_PNGIO_ERR_NOMEM;
    __builtin_memset(raw + dec->raw_size, 0, dec->max_rowbytes);

    r = gather_idat(dec, &z, &owned);
    if (r) {
        mime_free(&dec->alloc, raw);
        return r;
    }
    unsigned zflags = MIME_INFLATE_ZLIB | ((dec->flags & MIME_PNGIO_SKIP_ADLER) ? MIME_INFLATE_NO_CHECKSUM : 0);
    r = mime_inflate_with((MimeInflate *)(raw + state_off), z, dec->idat_total, raw, dec->raw_size, &got, zflags);
    mime_free(&dec->alloc, owned);
    /* data after a complete image and a missing trailer are tolerated. */
    if (r == MIME_INFLATE_ERR_OUTPUT || r == MIME_INFLATE_ERR_TRUNCATED)
        r = got == dec->raw_size ? MIME_INFLATE_OK : r;
    if (r || got != dec->raw_size) {
        mime_free(&dec->alloc, raw);
        return MIME_PNGIO_ERR_DATA;
    }
    dec->raw = raw;
    dec->filtered_rows = 0;
    return MIME_PNGIO_OK;
}

static int unfilter_upto(MimePngioDecoder *dec, uint32_t upto)
{
    size_t rb = dec->pass[0].rowbytes, pitch = rb + 1;

    while (dec->filtered_rows < upto) {
        uint8_t *row = dec->raw + (size_t)dec->filtered_rows * pitch;
        const uint8_t *prev = dec->filtered_rows ? row - pitch + 1 : dec->raw + dec->raw_size;
        if (!pngio_unfilter_row(row + 1, prev, rb, dec->bpp, row[0]))
            return MIME_PNGIO_ERR_DATA;
        dec->filtered_rows++;
    }
    return MIME_PNGIO_OK;
}

/* non interlaced, n rows from row y0. */
static int decode_plain(MimePngioDecoder *dec, uint32_t y0, uint32_t n, uint8_t *out, size_t stride)
{
    size_t pitch = dec->pass[0].rowbytes + 1;
    int r = ensure_raw(dec);

    if (r)
        return r;
    if (dec->filtered_rows < y0) {
        r = unfilter_upto(dec, y0);
        if (r)
            return r;
    }
    for (uint32_t i = 0; i < n; i++) {
        uint32_t y = y0 + i;
        if (y >= dec->filtered_rows) {
            r = unfilter_upto(dec, y + 1);
            if (r)
                return r;
        }
        pngio_conv_row(&dec->conv, dec->raw + (size_t)y * pitch + 1, out + (size_t)i * stride, dec->info.width);
    }
    return MIME_PNGIO_OK;
}

static void scatter(const uint8_t *tmp, uint8_t *row, unsigned bits, uint32_t x0, uint32_t dx, uint32_t n)
{
    if (bits >= 8) {
        size_t sz = bits >> 3, step = (size_t)dx * sz;
        uint8_t *d = row + (size_t)x0 * sz;

        switch (sz) {
        case 1:
            for (uint32_t j = 0; j < n; j++, d += step)
                d[0] = tmp[j];
            break;
        case 3:
            for (uint32_t j = 0; j < n; j++, d += step, tmp += 3) {
                d[0] = tmp[0];
                d[1] = tmp[1];
                d[2] = tmp[2];
            }
            break;
        case 4:
            for (uint32_t j = 0; j < n; j++, d += step, tmp += 4)
                __builtin_memcpy(d, tmp, 4);
            break;
        case 8:
            for (uint32_t j = 0; j < n; j++, d += step, tmp += 8)
                __builtin_memcpy(d, tmp, 8);
            break;
        default:
            for (uint32_t j = 0; j < n; j++, d += step, tmp += sz)
                __builtin_memcpy(d, tmp, sz);
            break;
        }
        return;
    }
    unsigned mask = (1u << bits) - 1;
    for (uint32_t j = 0; j < n; j++) {
        size_t sb = (size_t)j * bits, db = ((size_t)x0 + (size_t)j * dx) * bits;
        unsigned v = (tmp[sb >> 3] >> (8 - bits - (sb & 7))) & mask;
        row[db >> 3] = (uint8_t)(row[db >> 3] | (v << (8 - bits - (db & 7))));
    }
}

/* the whole interlaced image, out being zero filled for packed formats by this function. */
static int decode_adam7(MimePngioDecoder *dec, uint8_t *out, size_t stride)
{
    const MimePngioInfo *info = &dec->info;
    unsigned bits = mime_pngio_bits_per_pixel(dec->format);
    size_t tmp_size = mime_pngio_row_bytes(dec->format, info->width);
    int r = ensure_raw(dec);

    if (r)
        return r;
    uint8_t *tmp = mime_alloc(&dec->alloc, tmp_size ? tmp_size : 1);
    if (!tmp)
        return MIME_PNGIO_ERR_NOMEM;
    if (bits < 8) {
        for (uint32_t y = 0; y < info->height; y++)
            __builtin_memset(out + (size_t)y * stride, 0, tmp_size);
    }

    uint8_t *row = dec->raw;
    for (unsigned p = 0; p < dec->npass; p++) {
        const PngioPass *ps = &dec->pass[p];
        const uint8_t *prev = dec->raw + dec->raw_size;
        for (uint32_t y = 0; y < ps->h; y++) {
            uint8_t *cur = row + 1;
            if (!pngio_unfilter_row(cur, prev, ps->rowbytes, dec->bpp, row[0])) {
                mime_free(&dec->alloc, tmp);
                return MIME_PNGIO_ERR_DATA;
            }
            pngio_conv_row(&dec->conv, cur, tmp, ps->w);
            scatter(tmp, out + (size_t)(ps->y0 + y * ps->dy) * stride, bits, ps->x0, ps->dx, ps->w);
            prev = cur;
            row = cur + ps->rowbytes;
        }
    }
    mime_free(&dec->alloc, tmp);
    return MIME_PNGIO_OK;
}

int mime_pngio_decode(MimePngioDecoder *dec, void *out, size_t stride)
{
    size_t rb;
    int r;

    if (!dec || !out)
        return MIME_PNGIO_ERR_PARAM;
    rb = mime_pngio_row_bytes(dec->format, dec->info.width);
    if (!rb || stride < rb)
        return MIME_PNGIO_ERR_PARAM;
    if (dec->info.interlaced) {
        mime_free(&dec->alloc, dec->raw);
        dec->raw = nullptr;
        r = decode_adam7(dec, out, stride);
    } else {
        r = decode_plain(dec, 0, dec->info.height, out, stride);
    }
    mime_free(&dec->alloc, dec->raw);
    dec->raw = nullptr;
    dec->filtered_rows = 0;
    return r;
}

int mime_pngio_decode_rows(MimePngioDecoder *dec, uint32_t y, uint32_t count, void *out, size_t stride)
{
    size_t rb;
    int r;

    if (!dec || (!out && count))
        return MIME_PNGIO_ERR_PARAM;
    if (y > dec->info.height || count > dec->info.height - y)
        return MIME_PNGIO_ERR_PARAM;
    rb = mime_pngio_row_bytes(dec->format, dec->info.width);
    if (!rb || (count && stride < rb))
        return MIME_PNGIO_ERR_PARAM;
    if (!count)
        return MIME_PNGIO_OK;
    if (!dec->info.interlaced)
        return decode_plain(dec, y, count, out, stride);

    if (!dec->cache) {
        size_t total;
        if (ckd_mul(&total, rb, (size_t)dec->info.height))
            return MIME_PNGIO_ERR_UNSUPPORTED;
        dec->cache = mime_alloc(&dec->alloc, total);
        if (!dec->cache)
            return MIME_PNGIO_ERR_NOMEM;
        r = decode_adam7(dec, dec->cache, rb);
        mime_free(&dec->alloc, dec->raw);
        dec->raw = nullptr;
        if (r) {
            mime_free(&dec->alloc, dec->cache);
            dec->cache = nullptr;
            return r;
        }
        dec->cache_stride = rb;
    }
    for (uint32_t i = 0; i < count; i++)
        __builtin_memcpy((uint8_t *)out + (size_t)i * stride, dec->cache + (size_t)(y + i) * dec->cache_stride, rb);
    return MIME_PNGIO_OK;
}

int mime_pngio_text(const MimePngioDecoder *dec, unsigned index, MimePngioText *text)
{
    if (!dec || !text || index >= dec->info.text_count)
        return MIME_PNGIO_ERR_PARAM;
    *text = dec->texts[index];
    return MIME_PNGIO_OK;
}

int mime_pngio_chunk(const MimePngioDecoder *dec, unsigned index, MimePngioChunk *chunk)
{
    if (!dec || !chunk || index >= dec->info.chunk_count)
        return MIME_PNGIO_ERR_PARAM;
    *chunk = dec->chunks[index];
    return MIME_PNGIO_OK;
}

/* copies or inflates into a buffer of size + 1 bytes with a null terminator. */
static int metadata_bytes(const MimePngioDecoder *dec, const uint8_t *src, size_t src_size, bool compressed,
                          void **data, size_t *size)
{
    uint8_t *buf;

    *data = nullptr;
    *size = 0;
    if (!compressed) {
        if (src_size > MAX_METADATA)
            return MIME_PNGIO_ERR_UNSUPPORTED;
        buf = mime_alloc(&dec->alloc, src_size + 1);
        if (!buf)
            return MIME_PNGIO_ERR_NOMEM;
        __builtin_memcpy(buf, src, src_size);
        buf[src_size] = 0;
        *data = buf;
        *size = src_size;
        return MIME_PNGIO_OK;
    }

    void *inflated;
    size_t n;
    unsigned zflags = MIME_INFLATE_ZLIB | ((dec->flags & MIME_PNGIO_SKIP_ADLER) ? MIME_INFLATE_NO_CHECKSUM : 0);
    int r = mime_inflate_alloc(src, src_size, &dec->alloc, 0, MAX_METADATA, &inflated, &n, zflags);
    if (r)
        return r == MIME_INFLATE_ERR_NOMEM ? MIME_PNGIO_ERR_NOMEM : MIME_PNGIO_ERR_DATA;
    buf = mime_alloc(&dec->alloc, n + 1);
    if (!buf) {
        mime_free(&dec->alloc, inflated);
        return MIME_PNGIO_ERR_NOMEM;
    }
    __builtin_memcpy(buf, inflated, n);
    buf[n] = 0;
    mime_free(&dec->alloc, inflated);
    *data = buf;
    *size = n;
    return MIME_PNGIO_OK;
}

int mime_pngio_text_data(const MimePngioDecoder *dec, unsigned index, void **data, size_t *size)
{
    if (!dec || !data || !size || index >= dec->info.text_count)
        return MIME_PNGIO_ERR_PARAM;
    const MimePngioText *t = &dec->texts[index];
    return metadata_bytes(dec, t->data, t->data_size, t->compressed, data, size);
}

int mime_pngio_iccp_data(const MimePngioDecoder *dec, void **data, size_t *size)
{
    if (!dec || !data || !size || !dec->info.has_iccp)
        return MIME_PNGIO_ERR_PARAM;
    return metadata_bytes(dec, dec->iccp, dec->iccp_size, true, data, size);
}

void mime_pngio_free(const MimePngioDecoder *dec, void *p)
{
    if (dec)
        mime_free(&dec->alloc, p);
}

int mime_pngio_decode_alloc(const void *data, size_t size, const MimeAllocator *alloc, unsigned flags,
                            void **pixels, size_t *stride, MimePngioInfo *info, int *format)
{
    MimePngioDecoder *dec;
    size_t rb, total;
    int r;

    if (!pixels)
        return MIME_PNGIO_ERR_PARAM;
    *pixels = nullptr;
    r = mime_pngio_open(data, size, alloc, flags, &dec);
    if (r)
        return r;
    rb = mime_pngio_row_bytes(dec->format, dec->info.width);
    if (!rb || ckd_mul(&total, rb, (size_t)dec->info.height)) {
        close_decoder(dec);
        return MIME_PNGIO_ERR_UNSUPPORTED;
    }
    void *buf = mime_alloc(alloc, total);
    if (!buf) {
        close_decoder(dec);
        return MIME_PNGIO_ERR_NOMEM;
    }
    r = mime_pngio_decode(dec, buf, rb);
    if (r) {
        mime_free(alloc, buf);
        close_decoder(dec);
        return r;
    }
    *pixels = buf;
    if (stride)
        *stride = rb;
    if (info)
        *info = dec->info;
    if (format)
        *format = dec->format;
    close_decoder(dec);
    return MIME_PNGIO_OK;
}
