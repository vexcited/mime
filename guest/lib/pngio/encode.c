#include <stdckdint.h>

#include "../png/deflate.h"
#include "pngio_internal.h"

#define MAX_CHUNK 0x7fffffffu

typedef struct Buf {
    const MimeAllocator *alloc;
    uint8_t *p;
    size_t len, cap;
    bool failed;
} Buf;

static void buf_reserve(Buf *b, size_t extra)
{
    size_t need, ncap;
    uint8_t *n;

    if (b->failed)
        return;
    if (ckd_add(&need, b->len, extra)) {
        b->failed = true;
        return;
    }
    if (need <= b->cap)
        return;
    ncap = b->cap < 1024 ? 1024 : b->cap;
    while (ncap < need) {
        if (ncap > SIZE_MAX / 2) {
            ncap = need;
            break;
        }
        ncap *= 2;
    }
    n = mime_alloc(b->alloc, ncap);
    if (!n) {
        b->failed = true;
        return;
    }
    if (b->len)
        __builtin_memcpy(n, b->p, b->len);
    mime_free(b->alloc, b->p);
    b->p = n;
    b->cap = ncap;
}

static void buf_put(Buf *b, const void *d, size_t n)
{
    buf_reserve(b, n);
    if (b->failed)
        return;
    if (n)
        __builtin_memcpy(b->p + b->len, d, n);
    b->len += n;
}

static void buf_u32(Buf *b, uint32_t v)
{
    uint8_t t[4];
    pngio_put_be32(t, v);
    buf_put(b, t, 4);
}

static void buf_u16(Buf *b, unsigned v)
{
    uint8_t t[2] = {(uint8_t)(v >> 8), (uint8_t)v};
    buf_put(b, t, 2);
}

static void buf_u8(Buf *b, unsigned v)
{
    uint8_t t = (uint8_t)v;
    buf_put(b, &t, 1);
}

static size_t chunk_begin(Buf *b, const char *type)
{
    size_t start = b->len;
    buf_u32(b, 0);
    buf_put(b, type, 4);
    return start;
}

static void chunk_end(Buf *b, size_t start)
{
    if (b->failed)
        return;
    size_t len = b->len - start - 8;
    pngio_put_be32(b->p + start, (uint32_t)len);
    buf_u32(b, mime_pngio_crc32(0, b->p + start + 4, len + 4));
}

static void chunk_simple(Buf *b, const char *type, const void *data, size_t n)
{
    size_t s = chunk_begin(b, type);
    buf_put(b, data, n);
    chunk_end(b, s);
}

typedef struct Layout {
    unsigned color_type;
    unsigned depth;
    unsigned channels;
    unsigned file_bits;
    bool indexed;
    bool identity; /* the native row is already the file row. */
} Layout;

static bool layout_of(int format, Layout *l)
{
    l->indexed = false;
    l->identity = false;
    switch (format) {
    case MIME_PNGIO_FMT_GRAY1:
    case MIME_PNGIO_FMT_GRAY2:
    case MIME_PNGIO_FMT_GRAY4:
    case MIME_PNGIO_FMT_GRAY8:
        l->color_type = MIME_PNGIO_COLOR_GRAY;
        l->depth = mime_pngio_bits_per_pixel(format);
        l->channels = 1;
        l->identity = format == MIME_PNGIO_FMT_GRAY8;
        break;
    case MIME_PNGIO_FMT_GRAY16:
        l->color_type = MIME_PNGIO_COLOR_GRAY;
        l->depth = 16;
        l->channels = 1;
        break;
    case MIME_PNGIO_FMT_INDEXED1:
    case MIME_PNGIO_FMT_INDEXED2:
    case MIME_PNGIO_FMT_INDEXED4:
    case MIME_PNGIO_FMT_INDEXED8:
        l->color_type = MIME_PNGIO_COLOR_PALETTE;
        l->depth = mime_pngio_bits_per_pixel(format);
        l->channels = 1;
        l->indexed = true;
        l->identity = format == MIME_PNGIO_FMT_INDEXED8;
        break;
    case MIME_PNGIO_FMT_BGR24:
    case MIME_PNGIO_FMT_BGRX32:
    case MIME_PNGIO_FMT_RGB24:
        l->color_type = MIME_PNGIO_COLOR_RGB;
        l->depth = 8;
        l->channels = 3;
        l->identity = format == MIME_PNGIO_FMT_RGB24;
        break;
    case MIME_PNGIO_FMT_RGB48:
        l->color_type = MIME_PNGIO_COLOR_RGB;
        l->depth = 16;
        l->channels = 3;
        break;
    case MIME_PNGIO_FMT_BGRA32:
    case MIME_PNGIO_FMT_RGBA32:
        l->color_type = MIME_PNGIO_COLOR_RGBA;
        l->depth = 8;
        l->channels = 4;
        l->identity = format == MIME_PNGIO_FMT_RGBA32;
        break;
    case MIME_PNGIO_FMT_RGBA64:
        l->color_type = MIME_PNGIO_COLOR_RGBA;
        l->depth = 16;
        l->channels = 4;
        break;
    case MIME_PNGIO_FMT_GRAY_ALPHA16:
        l->color_type = MIME_PNGIO_COLOR_GRAY_ALPHA;
        l->depth = 8;
        l->channels = 2;
        l->identity = true;
        break;
    case MIME_PNGIO_FMT_GRAY_ALPHA32:
        l->color_type = MIME_PNGIO_COLOR_GRAY_ALPHA;
        l->depth = 16;
        l->channels = 2;
        break;
    default:
        return false;
    }
    l->file_bits = l->depth * l->channels;
    return true;
}

/* picks the pixels x0, x0 + dx, ... of a native row, packed. dst must be zeroed for formats below 8 bits. */
static void gather(const uint8_t *row, uint8_t *dst, unsigned bits, uint32_t x0, uint32_t dx, uint32_t n)
{
    if (bits >= 8) {
        size_t sz = bits >> 3, step = (size_t)dx * sz;
        const uint8_t *s = row + (size_t)x0 * sz;

        switch (sz) {
        case 1:
            for (uint32_t j = 0; j < n; j++, s += step)
                dst[j] = s[0];
            break;
        case 3:
            for (uint32_t j = 0; j < n; j++, s += step, dst += 3) {
                dst[0] = s[0];
                dst[1] = s[1];
                dst[2] = s[2];
            }
            break;
        case 4:
            for (uint32_t j = 0; j < n; j++, s += step, dst += 4)
                __builtin_memcpy(dst, s, 4);
            break;
        case 8:
            for (uint32_t j = 0; j < n; j++, s += step, dst += 8)
                __builtin_memcpy(dst, s, 8);
            break;
        default:
            for (uint32_t j = 0; j < n; j++, s += step, dst += sz)
                __builtin_memcpy(dst, s, sz);
            break;
        }
        return;
    }
    unsigned mask = (1u << bits) - 1;
    for (uint32_t j = 0; j < n; j++) {
        size_t sb = ((size_t)x0 + (size_t)j * dx) * bits, db = (size_t)j * bits;
        unsigned v = (row[sb >> 3] >> (8 - bits - (sb & 7))) & mask;
        dst[db >> 3] = (uint8_t)(dst[db >> 3] | (v << (8 - bits - (db & 7))));
    }
}

static bool keyword_ok(const char *k)
{
    size_t n = 0;

    if (!k)
        return false;
    while (k[n]) {
        if ((uint8_t)k[n] < 32 || ((uint8_t)k[n] > 126 && (uint8_t)k[n] < 161))
            return false;
        n++;
    }
    return n >= 1 && n <= 79 && k[0] != ' ' && k[n - 1] != ' ';
}

static bool chunk_type_ok(const char *t)
{
    for (int i = 0; i < 4; i++) {
        char c = t[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')))
            return false;
    }
    return !(__builtin_memcmp(t, "IHDR", 4) == 0 || __builtin_memcmp(t, "PLTE", 4) == 0 ||
             __builtin_memcmp(t, "IDAT", 4) == 0 || __builtin_memcmp(t, "IEND", 4) == 0);
}

static bool time_ok(const MimePngioTime *t)
{
    return t->month >= 1 && t->month <= 12 && t->day >= 1 && t->day <= 31 && t->hour <= 23 && t->minute <= 59 &&
           t->second <= 60;
}

static int validate(const MimePngioEncode *e, const Layout *l)
{
    if (!e->alloc || !e->alloc->alloc || !e->alloc->free || !e->pixels)
        return MIME_PNGIO_ERR_PARAM;
    if (!e->width || !e->height || e->width > MAX_CHUNK || e->height > MAX_CHUNK)
        return MIME_PNGIO_ERR_PARAM;
    size_t rb = mime_pngio_row_bytes(e->format, e->width);
    if (!rb || (e->stride && e->stride < rb))
        return MIME_PNGIO_ERR_PARAM;
    if (l->indexed) {
        if (!e->palette || e->palette_count < 1 || e->palette_count > (1u << l->depth))
            return MIME_PNGIO_ERR_PARAM;
        if (e->palette_alpha_count && (!e->palette_alpha || e->palette_alpha_count > e->palette_count))
            return MIME_PNGIO_ERR_PARAM;
    }
    if (e->has_trns_key && (l->color_type != MIME_PNGIO_COLOR_GRAY && l->color_type != MIME_PNGIO_COLOR_RGB))
        return MIME_PNGIO_ERR_PARAM;
    if (e->filter < MIME_PNGIO_FILTER_ADAPTIVE || e->filter > MIME_PNGIO_FILTER_PAETH)
        return MIME_PNGIO_ERR_PARAM;
    if (e->has_time && !time_ok(&e->time))
        return MIME_PNGIO_ERR_PARAM;
    if (e->iccp_name && (!keyword_ok(e->iccp_name) || !e->iccp_data || !e->iccp_size || e->iccp_size > MAX_CHUNK))
        return MIME_PNGIO_ERR_PARAM;
    if ((e->exif_size && !e->exif) || e->exif_size > MAX_CHUNK)
        return MIME_PNGIO_ERR_PARAM;
    if (e->has_sbit) {
        for (unsigned i = 0; i < (l->indexed ? 3 : l->channels); i++)
            if (e->sbit[i] < 1 || e->sbit[i] > (l->indexed ? 8 : l->depth))
                return MIME_PNGIO_ERR_PARAM;
    }
    if (e->text_count && !e->texts)
        return MIME_PNGIO_ERR_PARAM;
    for (unsigned i = 0; i < e->text_count; i++) {
        const MimePngioTextItem *t = &e->texts[i];
        if (!keyword_ok(t->keyword) || t->kind > MIME_PNGIO_TEXT_ITXT || (t->text_size && !t->text) ||
            t->text_size > MAX_CHUNK)
            return MIME_PNGIO_ERR_PARAM;
        if (t->kind == MIME_PNGIO_TEXT_LATIN1) {
            for (size_t k = 0; k < t->text_size; k++)
                if (!((const uint8_t *)t->text)[k])
                    return MIME_PNGIO_ERR_PARAM;
        }
        if (t->kind == MIME_PNGIO_TEXT_ITXT) {
            if ((t->language && __builtin_strlen(t->language) > 79) ||
                (t->translated_keyword && __builtin_strlen(t->translated_keyword) > 4096))
                return MIME_PNGIO_ERR_PARAM;
        }
    }
    if (e->chunk_count && !e->chunks)
        return MIME_PNGIO_ERR_PARAM;
    for (unsigned i = 0; i < e->chunk_count; i++) {
        const MimePngioChunk *c = &e->chunks[i];
        if (!chunk_type_ok(c->type) || (c->size && !c->data) || c->size > MAX_CHUNK || c->position > MIME_PNGIO_POS_AFTER_IDAT)
            return MIME_PNGIO_ERR_PARAM;
    }
    return MIME_PNGIO_OK;
}

static void put_extras(Buf *b, const MimePngioEncode *e, unsigned position)
{
    for (unsigned i = 0; i < e->chunk_count; i++)
        if (e->chunks[i].position == position)
            chunk_simple(b, e->chunks[i].type, e->chunks[i].data, e->chunks[i].size);
}

static int put_text(Buf *b, const MimePngioTextItem *t, int level)
{
    size_t s;
    const char *lang = t->language ? t->language : "";
    const char *trans = t->translated_keyword ? t->translated_keyword : "";
    bool compress = t->kind == MIME_PNGIO_TEXT_ZTXT || (t->kind == MIME_PNGIO_TEXT_ITXT && t->compress);
    void *z = nullptr;
    size_t zlen = 0;
    const void *body = t->text;
    size_t body_size = t->text_size;

    if (compress) {
        static const uint8_t none = 0;
        if (mime_deflate_zlib(t->text ? t->text : &none, t->text_size, level, b->alloc, &z, &zlen))
            return MIME_PNGIO_ERR_NOMEM;
        body = z;
        body_size = zlen;
    }
    s = chunk_begin(b, t->kind == MIME_PNGIO_TEXT_LATIN1 ? "tEXt" : t->kind == MIME_PNGIO_TEXT_ZTXT ? "zTXt" : "iTXt");
    buf_put(b, t->keyword, __builtin_strlen(t->keyword) + 1);
    if (t->kind == MIME_PNGIO_TEXT_ZTXT) {
        buf_u8(b, 0);
    } else if (t->kind == MIME_PNGIO_TEXT_ITXT) {
        buf_u8(b, compress);
        buf_u8(b, 0);
        buf_put(b, lang, __builtin_strlen(lang) + 1);
        buf_put(b, trans, __builtin_strlen(trans) + 1);
    }
    buf_put(b, body, body_size);
    chunk_end(b, s);
    mime_free(b->alloc, z);
    return MIME_PNGIO_OK;
}

int mime_pngio_encode(const MimePngioEncode *e, void **out, size_t *out_size)
{
    Layout l;
    PngioPass pass[7];
    Buf b = {};
    uint8_t *filtered = nullptr, *work = nullptr;
    void *z = nullptr;
    size_t zlen = 0;
    int r;

    if (out)
        *out = nullptr;
    if (out_size)
        *out_size = 0;
    if (!e || !out || !out_size)
        return MIME_PNGIO_ERR_PARAM;
    if (!layout_of(e->format, &l))
        return MIME_PNGIO_ERR_PARAM;
    r = validate(e, &l);
    if (r)
        return r;

    const MimeAllocator *alloc = e->alloc;
    int level = e->level < 0 ? 6 : e->level > 9 ? 9 : e->level;
    bool interlaced = (e->flags & MIME_PNGIO_ENC_INTERLACE) != 0;
    unsigned native_bits = mime_pngio_bits_per_pixel(e->format);
    size_t native_rb = mime_pngio_row_bytes(e->format, e->width);
    size_t stride = e->stride ? e->stride : native_rb;
    unsigned bpp = l.file_bits >= 8 ? l.file_bits / 8 : 1;
    unsigned npass = pngio_pass_layout(e->width, e->height, interlaced, l.file_bits, pass);
    size_t total = 0, max_rb = 0;

    if (!npass)
        return MIME_PNGIO_ERR_PARAM;
    for (unsigned i = 0; i < npass; i++) {
        size_t sz;
        if (ckd_mul(&sz, pass[i].rowbytes + 1, (size_t)pass[i].h) || ckd_add(&total, total, sz))
            return MIME_PNGIO_ERR_NOMEM;
        if (pass[i].rowbytes > max_rb)
            max_rb = pass[i].rowbytes;
    }

    int filter = e->filter;
    if (filter == MIME_PNGIO_FILTER_ADAPTIVE && (l.indexed || l.depth < 8))
        filter = MIME_PNGIO_FILTER_NONE;

    /* work: two file rows, two filter candidates, a zero row and a gathered native row. */
    size_t stride_w = (max_rb + 31) & ~(size_t)15;
    size_t gat_size = interlaced ? native_rb : 0;
    size_t work_size;
    if (ckd_mul(&work_size, stride_w, (size_t)5) || ckd_add(&work_size, work_size, gat_size + 16))
        return MIME_PNGIO_ERR_NOMEM;
    filtered = mime_alloc(alloc, total);
    work = mime_alloc(alloc, work_size);
    if (!filtered || !work) {
        r = MIME_PNGIO_ERR_NOMEM;
        goto fail;
    }
    uint8_t *file_row[2] = {work, work + stride_w};
    uint8_t *cand[2] = {work + 2 * stride_w, work + 3 * stride_w};
    uint8_t *zero = work + 4 * stride_w;
    uint8_t *gat = work + 5 * stride_w;
    __builtin_memset(zero, 0, stride_w);

    const uint8_t *pixels = e->pixels;
    uint8_t *dst = filtered;
    for (unsigned p = 0; p < npass; p++) {
        const PngioPass *ps = &pass[p];
        const uint8_t *prev = zero;
        unsigned flip = 0;
        for (uint32_t y = 0; y < ps->h; y++) {
            const uint8_t *src = pixels + (size_t)(ps->y0 + y * ps->dy) * stride;
            const uint8_t *cur;
            size_t n = ps->rowbytes;

            if (!interlaced && l.identity) {
                cur = src;
            } else {
                uint8_t *fr = file_row[flip];
                flip ^= 1;
                if (interlaced) {
                    if (native_bits < 8)
                        __builtin_memset(gat, 0, native_rb);
                    gather(src, gat, native_bits, ps->x0, ps->dx, ps->w);
                    pngio_unconv_row(e->format, gat, fr, ps->w);
                } else {
                    pngio_unconv_row(e->format, src, fr, ps->w);
                }
                cur = fr;
            }

            unsigned type;
            if (filter == MIME_PNGIO_FILTER_ADAPTIVE) {
                uint64_t best = ~(uint64_t)0;
                unsigned best_buf = 0;
                type = 0;
                unsigned slot = 0;
                for (unsigned t = 0; t < 5; t++) {
                    const uint8_t *res;
                    if (t == 0) {
                        res = cur;
                    } else {
                        pngio_filter_row(cand[slot], cur, prev, n, bpp, t);
                        res = cand[slot];
                    }
                    uint64_t sum = pngio_abs_sum(res, n);
                    if (sum < best) {
                        best = sum;
                        type = t;
                        if (t) {
                            best_buf = slot;
                            slot ^= 1;
                        }
                    }
                }
                dst[0] = (uint8_t)type;
                __builtin_memcpy(dst + 1, type ? cand[best_buf] : cur, n);
            } else {
                type = (unsigned)(filter - MIME_PNGIO_FILTER_NONE);
                dst[0] = (uint8_t)type;
                pngio_filter_row(dst + 1, cur, prev, n, bpp, type);
            }
            dst += n + 1;
            prev = cur;
        }
    }
    mime_free(alloc, work);
    work = nullptr;

    r = mime_deflate_zlib(filtered, total, level, alloc, &z, &zlen);
    mime_free(alloc, filtered);
    filtered = nullptr;
    if (r) {
        r = MIME_PNGIO_ERR_NOMEM;
        goto fail;
    }

    {
        static const uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
        size_t idat_size = e->idat_size ? e->idat_size : 65536;
        if (idat_size > MAX_CHUNK)
            idat_size = MAX_CHUNK;
        size_t nidat = zlen ? (zlen + idat_size - 1) / idat_size : 1;
        size_t extra;

        b.alloc = alloc;
        if (ckd_mul(&extra, nidat, (size_t)12) || ckd_add(&extra, extra, zlen) || ckd_add(&extra, extra, (size_t)8 + 25 + 12))
            goto nomem;
        buf_put(&b, signature, 8);

        size_t s = chunk_begin(&b, "IHDR");
        buf_u32(&b, e->width);
        buf_u32(&b, e->height);
        buf_u8(&b, l.depth);
        buf_u8(&b, l.color_type);
        buf_u8(&b, 0);
        buf_u8(&b, 0);
        buf_u8(&b, interlaced);
        chunk_end(&b, s);

        if (e->has_srgb)
            chunk_simple(&b, "sRGB", &e->srgb_intent, 1);
        if (e->iccp_name) {
            void *pz;
            size_t pzlen;
            if (mime_deflate_zlib(e->iccp_data, e->iccp_size, level, alloc, &pz, &pzlen))
                goto nomem;
            s = chunk_begin(&b, "iCCP");
            buf_put(&b, e->iccp_name, __builtin_strlen(e->iccp_name) + 1);
            buf_u8(&b, 0);
            buf_put(&b, pz, pzlen);
            chunk_end(&b, s);
            mime_free(alloc, pz);
        }
        if (e->has_gama) {
            s = chunk_begin(&b, "gAMA");
            buf_u32(&b, e->gama);
            chunk_end(&b, s);
        }
        if (e->has_chrm) {
            s = chunk_begin(&b, "cHRM");
            for (unsigned i = 0; i < 8; i++)
                buf_u32(&b, e->chrm[i]);
            chunk_end(&b, s);
        }
        if (e->has_sbit)
            chunk_simple(&b, "sBIT", e->sbit, l.indexed ? 3 : l.channels);
        put_extras(&b, e, MIME_PNGIO_POS_BEFORE_PLTE);

        if (l.indexed) {
            chunk_simple(&b, "PLTE", e->palette, (size_t)e->palette_count * 3);
            unsigned last = 0;
            for (unsigned i = 0; i < e->palette_alpha_count; i++)
                if (e->palette_alpha[i] != 255)
                    last = i + 1;
            if (last)
                chunk_simple(&b, "tRNS", e->palette_alpha, last);
        } else if (e->has_trns_key) {
            s = chunk_begin(&b, "tRNS");
            for (unsigned i = 0; i < (l.color_type == MIME_PNGIO_COLOR_RGB ? 3u : 1u); i++)
                buf_u16(&b, e->trns_key[i]);
            chunk_end(&b, s);
        }
        if (e->has_bkgd) {
            s = chunk_begin(&b, "bKGD");
            if (l.indexed)
                buf_u8(&b, e->bkgd[0]);
            else
                for (unsigned i = 0; i < (l.channels >= 3 ? 3u : 1u); i++)
                    buf_u16(&b, e->bkgd[i]);
            chunk_end(&b, s);
        }
        if (e->has_phys) {
            s = chunk_begin(&b, "pHYs");
            buf_u32(&b, e->phys_x);
            buf_u32(&b, e->phys_y);
            buf_u8(&b, e->phys_unit);
            chunk_end(&b, s);
        }
        if (e->exif_size)
            chunk_simple(&b, "eXIf", e->exif, e->exif_size);
        if (e->has_time) {
            s = chunk_begin(&b, "tIME");
            buf_u16(&b, e->time.year);
            buf_u8(&b, e->time.month);
            buf_u8(&b, e->time.day);
            buf_u8(&b, e->time.hour);
            buf_u8(&b, e->time.minute);
            buf_u8(&b, e->time.second);
            chunk_end(&b, s);
        }
        for (unsigned i = 0; i < e->text_count; i++) {
            r = put_text(&b, &e->texts[i], level);
            if (r)
                goto fail;
        }
        put_extras(&b, e, MIME_PNGIO_POS_BEFORE_IDAT);

        buf_reserve(&b, extra);
        for (size_t off = 0; off < zlen || off == 0; off += idat_size) {
            size_t piece = zlen - off < idat_size ? zlen - off : idat_size;
            chunk_simple(&b, "IDAT", (const uint8_t *)z + off, piece);
            if (!zlen)
                break;
        }
        put_extras(&b, e, MIME_PNGIO_POS_AFTER_IDAT);
        chunk_simple(&b, "IEND", nullptr, 0);
    }
    mime_free(alloc, z);
    if (b.failed) {
        mime_free(alloc, b.p);
        return MIME_PNGIO_ERR_NOMEM;
    }
    *out = b.p;
    *out_size = b.len;
    return MIME_PNGIO_OK;

nomem:
    r = MIME_PNGIO_ERR_NOMEM;
fail:
    mime_free(alloc, filtered);
    mime_free(alloc, work);
    mime_free(alloc, z);
    mime_free(alloc, b.p);
    return r;
}
