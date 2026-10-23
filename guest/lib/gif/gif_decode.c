#include "gif.h"
#include "gif_internal.h"

/*
 * lzw decoder. a table entry is not a prefix chain but the place of the string
 * in the output written so far: a new entry is the previous string followed by
 * the first byte of the current one, and those are adjacent in the output. a
 * code then costs one block copy and no chain walk. the output has to be one
 * linear buffer for that, rows are split up afterwards.
 */

typedef struct LzwTables {
    uint32_t off[4096];
    uint16_t len[4096];
} LzwTables;

#define COPY_SLACK 16u

static int lzw_run(LzwTables *t, const uint8_t *data, size_t size, unsigned mcs, uint8_t *out, size_t out_size,
                   uint8_t fill)
{
    const unsigned clear = 1u << mcs, eoi = clear + 1;
    unsigned next = eoi + 1, csize = mcs + 1;
    size_t pos = 0;
    bool have_prev = false;
    size_t prev_off = 0;
    unsigned prev_len = 0;
    uint64_t bits = 0;
    unsigned nbits = 0;
    const uint8_t *ip = data, *iend = data + size;
    int rc = MIME_GIF_OK;

    for (;;) {
        if (nbits < csize) {
            if ((size_t)(iend - ip) >= 8) {
                bits |= gif_load64(ip) << nbits;
                unsigned adv = (63 - nbits) >> 3;
                ip += adv;
                nbits += adv << 3;
            } else {
                while (nbits <= 56 && ip < iend) {
                    bits |= (uint64_t)*ip++ << nbits;
                    nbits += 8;
                }
                if (nbits < csize) {
                    rc = MIME_GIF_TRUNCATED;
                    break;
                }
            }
        }
        unsigned code = (unsigned)bits & ((1u << csize) - 1);
        bits >>= csize;
        nbits -= csize;

        size_t room = out_size - pos;
        size_t len;
        if (code < clear) {
            out[pos] = (uint8_t)code;
            len = 1;
        } else if (code <= eoi) {
            if (code == eoi) {
                rc = MIME_GIF_TRUNCATED;
                break;
            }
            next = eoi + 1;
            csize = mcs + 1;
            have_prev = false;
            continue;
        } else if (code < next) {
            len = t->len[code];
            const uint8_t *src = out + t->off[code];
            if (room >= len + COPY_SLACK) {
                uint8_t *dst = out + pos;
                size_t i = 0;
                do {
                    __builtin_memmove(dst + i, src + i, 16);
                    i += 16;
                } while (i < len);
            } else {
                if (len > room)
                    len = room;
                __builtin_memcpy(out + pos, src, len);
            }
        } else if (code == next && have_prev) {
            len = (size_t)prev_len + 1;
            const uint8_t *src = out + prev_off;
            if (room >= len + COPY_SLACK) {
                uint8_t *dst = out + pos;
                size_t i = 0;
                do {
                    __builtin_memmove(dst + i, src + i, 16);
                    i += 16;
                } while (i < prev_len);
                dst[prev_len] = *src;
            } else {
                if (len > room)
                    len = room;
                for (size_t i = 0; i < len; i++)
                    out[pos + i] = src[i];
            }
        } else {
            rc = MIME_GIF_DAMAGED;
            break;
        }

        if (have_prev && next < 4096) {
            t->off[next] = (uint32_t)prev_off;
            t->len[next] = (uint16_t)(prev_len + 1);
            next++;
            if (next == (1u << csize) && csize < 12)
                csize++;
        }
        have_prev = true;
        prev_off = pos;
        prev_len = (unsigned)len;
        pos += len;
        if (pos >= out_size)
            break;
    }
    if (pos < out_size)
        __builtin_memset(out + pos, fill, out_size - pos);
    return rc;
}

int mime_gif_lzw_decode(const uint8_t *data, size_t size, unsigned min_code_size, uint8_t *out, size_t out_size,
                        uint8_t fill, const MimeAllocator *alloc)
{
    if (min_code_size == 1)
        min_code_size = 2;
    if (min_code_size < 2 || min_code_size > 8)
        return MIME_GIF_ERR_DATA;
    if (out_size > MIME_GIF_MAX_PIXELS)
        return MIME_GIF_ERR_UNSUPPORTED;
    if (!out_size)
        return MIME_GIF_OK;
    if (!out || (!data && size) || !alloc)
        return MIME_GIF_ERR_PARAM;
    LzwTables *t = mime_alloc(alloc, sizeof(*t));
    if (!t)
        return MIME_GIF_ERR_NOMEM;
    int rc = lzw_run(t, data, size, min_code_size, out, out_size, fill);
    mime_free(alloc, t);
    return rc;
}

/* flattens a frame's sub-block payload into dst and returns its size. */
static size_t flatten(const MimeGif *g, const MimeGifFrame *f, uint8_t *dst)
{
    const uint8_t *p = f->data, *end = g->data + g->size;
    size_t done = 0;
    while (p < end) {
        size_t len = *p++;
        if (!len)
            break;
        if (len > (size_t)(end - p))
            len = (size_t)(end - p);
        __builtin_memcpy(dst + done, p, len);
        done += len;
        p += len;
    }
    return done;
}

static void deinterlace(const uint8_t *lin, uint8_t *out, size_t stride, unsigned w, unsigned h)
{
    static const uint8_t start[4] = {0, 4, 2, 1};
    static const uint8_t step[4] = {8, 8, 4, 2};
    for (unsigned pass = 0; pass < 4; pass++) {
        for (unsigned y = start[pass]; y < h; y += step[pass]) {
            __builtin_memcpy(out + (size_t)y * stride, lin, w);
            lin += w;
        }
    }
}

int mime_gif_decode_frame(const MimeGif *g, unsigned index, uint8_t *out, size_t stride)
{
    if (!g || index >= g->info.frame_count)
        return MIME_GIF_ERR_PARAM;
    const MimeGifFrame *f = &g->frames[index];
    unsigned w = f->width, h = f->height;
    if (!w || !h)
        return MIME_GIF_OK;
    if (!f->decodable)
        return MIME_GIF_ERR_DATA;
    if (!out || stride < w)
        return MIME_GIF_ERR_PARAM;

    size_t area = (size_t)w * h;
    bool direct = !f->interlaced && stride == w;
    size_t flat_off = (sizeof(LzwTables) + 15u) & ~(size_t)15;
    size_t lin_off = flat_off + (((size_t)f->data_bytes + 15u) & ~(size_t)15);
    size_t total = lin_off + (direct ? 0 : area);
    uint8_t *mem = mime_alloc(&g->alloc, total);
    if (!mem)
        return MIME_GIF_ERR_NOMEM;
    size_t flat_size = flatten(g, f, mem + flat_off);
    uint8_t *lin = direct ? out : mem + lin_off;
    unsigned mcs = f->min_code_size == 1 ? 2 : f->min_code_size;
    uint8_t fill = f->has_transparency ? f->transparent_index : 0;
    int rc = lzw_run((LzwTables *)mem, mem + flat_off, flat_size, mcs, lin, area, fill);
    if (!direct) {
        if (f->interlaced) {
            deinterlace(lin, out, stride, w, h);
        } else {
            for (unsigned y = 0; y < h; y++)
                __builtin_memcpy(out + (size_t)y * stride, lin + (size_t)y * w, w);
        }
    }
    MimeAllocator a = g->alloc;
    mime_free(&a, mem);
    return rc;
}

void mime_gif_palette_to_bgra(const uint8_t *rgb, unsigned count, int transparent, uint32_t out[256])
{
    if (count > 256)
        count = 256;
    unsigned i = 0;
    for (; i < count; i++)
        out[i] = 0xff000000u | (uint32_t)rgb[i * 3] << 16 | (uint32_t)rgb[i * 3 + 1] << 8 | rgb[i * 3 + 2];
    for (; i < 256; i++)
        out[i] = 0;
    if (transparent >= 0 && transparent < 256)
        out[transparent] = 0;
}

int mime_gif_decode_frame_bgra(const MimeGif *g, unsigned index, uint8_t *out, size_t out_stride)
{
    if (!g || index >= g->info.frame_count)
        return MIME_GIF_ERR_PARAM;
    const MimeGifFrame *f = &g->frames[index];
    unsigned w = f->width, h = f->height;
    if (!w || !h)
        return MIME_GIF_OK;
    if (!f->decodable)
        return MIME_GIF_ERR_DATA;
    if (!out || out_stride < (size_t)w * 4)
        return MIME_GIF_ERR_PARAM;
    uint8_t *idx = mime_alloc(&g->alloc, (size_t)w * h);
    if (!idx)
        return MIME_GIF_ERR_NOMEM;
    int rc = mime_gif_decode_frame(g, index, idx, w);
    if (rc >= 0) {
        uint32_t pal[256];
        mime_gif_palette_to_bgra(f->palette, f->palette_size, f->has_transparency ? f->transparent_index : -1, pal);
        for (unsigned y = 0; y < h; y++) {
            const uint8_t *s = idx + (size_t)y * w;
            uint8_t *d = out + (size_t)y * out_stride;
            for (unsigned x = 0; x < w; x++) {
                uint32_t c = pal[s[x]];
                __builtin_memcpy(d + x * 4, &c, 4);
            }
        }
    }
    MimeAllocator a = g->alloc;
    mime_free(&a, idx);
    return rc;
}
