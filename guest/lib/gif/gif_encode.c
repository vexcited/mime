#include "gif.h"
#include "gif_internal.h"

bool gif_buffer_reserve(MimeGifBuffer *buf, const MimeAllocator *alloc, size_t extra)
{
    if (extra > (size_t)-1 - buf->size)
        return false;
    size_t need = buf->size + extra;
    if (need <= buf->capacity)
        return true;
    size_t cap = buf->capacity ? buf->capacity : 4096;
    while (cap < need) {
        if (cap > (size_t)-1 / 2) {
            cap = need;
            break;
        }
        cap *= 2;
    }
    uint8_t *p = mime_alloc(alloc, cap);
    if (!p)
        return false;
    if (buf->size)
        __builtin_memcpy(p, buf->data, buf->size);
    mime_free(alloc, buf->data);
    buf->data = p;
    buf->capacity = cap;
    return true;
}

static inline void store64le(uint8_t *p, uint64_t v)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    __builtin_memcpy(p, &v, 8);
#else
    for (int i = 0; i < 8; i++)
        p[i] = (uint8_t)(v >> (8 * i));
#endif
}

#define HASH_BITS 13
#define HASH_SIZE (1u << HASH_BITS)

typedef struct RowSource {
    const uint8_t *base;
    size_t stride;
    size_t width;
    unsigned height;
    bool interlace;
} RowSource;

/*
 * codes are written bit packed and flat into the tail of buf, then spread into
 * sub-blocks in place. the dictionary maps prefix code and next byte to a code
 * in open addressing, an entry is key << 12 | code and 0 is empty, as no code
 * is ever 0 or 1 in use of a string.
 */
static int lzw_encode_rows(const RowSource *src, unsigned mcs, MimeGifBuffer *buf, const MimeAllocator *alloc)
{
    uint32_t *table = mime_alloc(alloc, HASH_SIZE * sizeof(uint32_t));
    if (!table)
        return MIME_GIF_ERR_NOMEM;
    __builtin_memset(table, 0, HASH_SIZE * sizeof(uint32_t));

    const unsigned clear = 1u << mcs, eoi = clear + 1, mask = clear - 1;
    unsigned next = eoi + 1, size = mcs + 1;
    const size_t start = buf->size;
    uint64_t bits = 0;
    unsigned nb = 0;
    uint8_t *op = buf->data + start;
    uint8_t *lim = buf->data + buf->capacity;
    int rc = MIME_GIF_OK;

#define EMIT(c, sz)                                                                                                   \
    do {                                                                                                              \
        bits |= (uint64_t)(c) << nb;                                                                                  \
        nb += (sz);                                                                                                   \
        if (__builtin_expect((size_t)(lim - op) < 8, 0)) {                                                            \
            buf->size = (size_t)(op - buf->data);                                                                     \
            if (!gif_buffer_reserve(buf, alloc, buf->capacity ? buf->capacity : 4096)) {                              \
                rc = MIME_GIF_ERR_NOMEM;                                                                              \
                goto done;                                                                                            \
            }                                                                                                         \
            op = buf->data + buf->size;                                                                               \
            lim = buf->data + buf->capacity;                                                                          \
        }                                                                                                             \
        store64le(op, bits);                                                                                          \
        unsigned nbytes_ = nb >> 3;                                                                                   \
        op += nbytes_;                                                                                                \
        bits >>= nbytes_ << 3;                                                                                        \
        nb &= 7;                                                                                                      \
    } while (0)

    if (!gif_buffer_reserve(buf, alloc, 4096)) {
        mime_free(alloc, table);
        return MIME_GIF_ERR_NOMEM;
    }
    op = buf->data + start;
    lim = buf->data + buf->capacity;

    EMIT(clear, size);

    bool first = true;
    unsigned cur = 0;
    for (unsigned row = 0; row < src->height; row++) {
        unsigned y = row;
        if (src->interlace) {
            /* the n-th row of the stream is this row of the image. */
            unsigned h = src->height, n = row;
            unsigned c0 = (h + 7) >> 3, c1 = (h + 3) >> 3, c2 = (h + 1) >> 2;
            if (n < c0)
                y = n * 8;
            else if ((n -= c0) < c1)
                y = n * 8 + 4;
            else if ((n -= c1) < c2)
                y = n * 4 + 2;
            else
                y = (n - c2) * 2 + 1;
        }
        const uint8_t *p = src->base + (size_t)y * src->stride;
        const uint8_t *pe = p + src->width;
        if (first) {
            if (p == pe)
                continue;
            cur = *p++ & mask;
            first = false;
        }
        while (p < pe) {
            unsigned px = *p++ & mask;
            uint32_t key = (cur << 8) | px;
            unsigned h = (key * 0x9e3779b1u) >> (32 - HASH_BITS);
            uint32_t e;
            while ((e = table[h]) != 0) {
                if ((e >> 12) == key)
                    break;
                h = (h + 1) & (HASH_SIZE - 1);
            }
            if (e) {
                cur = e & 0xfff;
                continue;
            }
            EMIT(cur, size);
            table[h] = (key << 12) | next;
            if (next == (1u << size) && size < 12)
                size++;
            next++;
            if (next >= 4095) {
                EMIT(clear, size);
                __builtin_memset(table, 0, HASH_SIZE * sizeof(uint32_t));
                next = eoi + 1;
                size = mcs + 1;
            }
            cur = px;
        }
    }
    if (!first) {
        EMIT(cur, size);
        if (next == (1u << size) && size < 12)
            size++;
    }
    EMIT(eoi, size);
    if (nb)
        *op++ = (uint8_t)bits;

    {
        size_t flat = (size_t)(op - (buf->data + start));
        size_t blocks = (flat + 254) / 255;
        buf->size = start + flat;
        if (!gif_buffer_reserve(buf, alloc, blocks + 1)) {
            rc = MIME_GIF_ERR_NOMEM;
            goto done;
        }
        uint8_t *d = buf->data + start;
        for (size_t b = blocks; b > 0; b--) {
            size_t off = (b - 1) * 255;
            size_t len = flat - off < 255 ? flat - off : 255;
            __builtin_memmove(d + off + b, d + off, len);
            d[off + b - 1] = (uint8_t)len;
        }
        d[flat + blocks] = 0;
        buf->size = start + flat + blocks + 1;
    }
done:
    mime_free(alloc, table);
    return rc;
#undef EMIT
}

int mime_gif_lzw_encode(const uint8_t *indices, size_t count, unsigned min_code_size, MimeGifBuffer *buf,
                        const MimeAllocator *alloc)
{
    if (min_code_size < 2 || min_code_size > 8 || (!indices && count) || !buf || !alloc)
        return MIME_GIF_ERR_PARAM;
    RowSource src = {indices, 0, count, 1, false};
    return lzw_encode_rows(&src, min_code_size, buf, alloc);
}

/* container writer. */

struct MimeGifEncoder {
    MimeAllocator alloc;
    MimeGifBuffer buf;
    unsigned width;
    unsigned height;
    bool has_global;
    unsigned global_size; /* padded entry count. */
    bool needs89;
    bool any_frame;
};

static unsigned palette_bits(unsigned count)
{
    unsigned bits = 1;
    while ((1u << bits) < count)
        bits++;
    return bits;
}

static bool put(MimeGifEncoder *e, const void *data, size_t n)
{
    if (!gif_buffer_reserve(&e->buf, &e->alloc, n))
        return false;
    __builtin_memcpy(e->buf.data + e->buf.size, data, n);
    e->buf.size += n;
    return true;
}

static bool put_byte(MimeGifEncoder *e, unsigned v)
{
    uint8_t b = (uint8_t)v;
    return put(e, &b, 1);
}


/* writes count entries and pads with black up to padded. */
static bool put_palette(MimeGifEncoder *e, const uint8_t *rgb, unsigned count, unsigned padded)
{
    if (!put(e, rgb, (size_t)count * 3))
        return false;
    if (padded > count) {
        size_t pad = (size_t)(padded - count) * 3;
        if (!gif_buffer_reserve(&e->buf, &e->alloc, pad))
            return false;
        __builtin_memset(e->buf.data + e->buf.size, 0, pad);
        e->buf.size += pad;
    }
    return true;
}

static bool put_subblocks(MimeGifEncoder *e, const uint8_t *data, size_t size)
{
    while (size) {
        size_t n = size < 255 ? size : 255;
        if (!put_byte(e, (unsigned)n) || !put(e, data, n))
            return false;
        data += n;
        size -= n;
    }
    return put_byte(e, 0);
}

int mime_gif_encoder_create(const MimeAllocator *alloc, unsigned width, unsigned height,
                            const uint8_t *global_palette, unsigned global_palette_size, uint8_t background_index,
                            MimeGifEncoder **out)
{
    if (!out)
        return MIME_GIF_ERR_PARAM;
    *out = NULL;
    if (!alloc || width < 1 || width > 65535 || height < 1 || height > 65535)
        return MIME_GIF_ERR_PARAM;
    if (global_palette && (global_palette_size < 1 || global_palette_size > 256))
        return MIME_GIF_ERR_PARAM;
    MimeGifEncoder *e = mime_alloc(alloc, sizeof(*e));
    if (!e)
        return MIME_GIF_ERR_NOMEM;
    __builtin_memset(e, 0, sizeof(*e));
    e->alloc = *alloc;
    e->width = width;
    e->height = height;
    unsigned packed = 0x70;
    if (global_palette) {
        unsigned bits = palette_bits(global_palette_size);
        e->has_global = true;
        e->global_size = 1u << bits;
        packed = 0x80 | ((bits - 1) << 4) | (bits - 1);
    }
    uint8_t head[13] = {'G', 'I', 'F', '8', '9', 'a', (uint8_t)width, (uint8_t)(width >> 8), (uint8_t)height,
                        (uint8_t)(height >> 8), (uint8_t)packed, background_index, 0};
    if (!put(e, head, sizeof(head)) ||
        (global_palette && !put_palette(e, global_palette, global_palette_size, e->global_size))) {
        mime_gif_encoder_free(e);
        return MIME_GIF_ERR_NOMEM;
    }
    *out = e;
    return MIME_GIF_OK;
}

void mime_gif_encoder_free(MimeGifEncoder *enc)
{
    if (enc) {
        MimeAllocator a = enc->alloc;
        mime_free(&a, enc->buf.data);
        mime_free(&a, enc);
    }
}

int mime_gif_encoder_add_application(MimeGifEncoder *enc, const uint8_t id[8], const uint8_t auth[3],
                                     const uint8_t *data, size_t size)
{
    if (!enc || !id || !auth || (!data && size))
        return MIME_GIF_ERR_PARAM;
    enc->needs89 = true;
    uint8_t head[3 + 11] = {0x21, 0xff, 11};
    __builtin_memcpy(head + 3, id, 8);
    __builtin_memcpy(head + 11, auth, 3);
    if (!put(enc, head, sizeof(head)) || !put_subblocks(enc, data, size))
        return MIME_GIF_ERR_NOMEM;
    return MIME_GIF_OK;
}

int mime_gif_encoder_set_loop(MimeGifEncoder *enc, unsigned loop_count)
{
    if (!enc || enc->any_frame || loop_count > 65535)
        return MIME_GIF_ERR_PARAM;
    uint8_t data[3] = {1, (uint8_t)loop_count, (uint8_t)(loop_count >> 8)};
    return mime_gif_encoder_add_application(enc, (const uint8_t *)"NETSCAPE", (const uint8_t *)"2.0", data,
                                            sizeof(data));
}

int mime_gif_encoder_add_comment(MimeGifEncoder *enc, const char *text, size_t length)
{
    if (!enc || (!text && length))
        return MIME_GIF_ERR_PARAM;
    enc->needs89 = true;
    if (!put_byte(enc, 0x21) || !put_byte(enc, 0xfe) || !put_subblocks(enc, (const uint8_t *)text, length))
        return MIME_GIF_ERR_NOMEM;
    return MIME_GIF_OK;
}

int mime_gif_encoder_add_frame(MimeGifEncoder *enc, const MimeGifEncFrame *f)
{
    if (!enc || !f || !f->indices || f->width < 1 || f->height < 1 || f->stride < f->width || f->disposal > 3)
        return MIME_GIF_ERR_PARAM;
    if ((unsigned)f->left + f->width > enc->width || (unsigned)f->top + f->height > enc->height)
        return MIME_GIF_ERR_PARAM;

    unsigned bits;
    if (f->palette) {
        if (f->palette_size < 1 || f->palette_size > 256)
            return MIME_GIF_ERR_PARAM;
        bits = palette_bits(f->palette_size);
    } else if (enc->has_global) {
        bits = palette_bits(enc->global_size);
    } else {
        return MIME_GIF_ERR_PARAM;
    }

    if (f->delay || f->disposal || f->user_input || f->has_transparency) {
        enc->needs89 = true;
        uint8_t gce[8] = {0x21,
                          0xf9,
                          4,
                          (uint8_t)(f->disposal << 2 | (f->user_input ? 2 : 0) | (f->has_transparency ? 1 : 0)),
                          (uint8_t)f->delay,
                          (uint8_t)(f->delay >> 8),
                          f->has_transparency ? f->transparent_index : (uint8_t)0,
                          0};
        if (!put(enc, gce, sizeof(gce)))
            return MIME_GIF_ERR_NOMEM;
    }
    uint8_t desc[10] = {0x2c,
                        (uint8_t)f->left,
                        (uint8_t)(f->left >> 8),
                        (uint8_t)f->top,
                        (uint8_t)(f->top >> 8),
                        (uint8_t)f->width,
                        (uint8_t)(f->width >> 8),
                        (uint8_t)f->height,
                        (uint8_t)(f->height >> 8),
                        (uint8_t)((f->palette ? 0x80 | (bits - 1) : 0) | (f->interlace ? 0x40 : 0))};
    if (!put(enc, desc, sizeof(desc)))
        return MIME_GIF_ERR_NOMEM;
    if (f->palette && !put_palette(enc, f->palette, f->palette_size, 1u << bits))
        return MIME_GIF_ERR_NOMEM;
    unsigned mcs = bits < 2 ? 2 : bits;
    if (!put_byte(enc, mcs))
        return MIME_GIF_ERR_NOMEM;
    enc->any_frame = true;
    RowSource src = {f->indices, f->stride, f->width, f->height, f->interlace};
    return lzw_encode_rows(&src, mcs, &enc->buf, &enc->alloc);
}

int mime_gif_encoder_finish(MimeGifEncoder *enc, uint8_t **data, size_t *size)
{
    if (!enc || !data || !size) {
        mime_gif_encoder_free(enc);
        return MIME_GIF_ERR_PARAM;
    }
    *data = NULL;
    *size = 0;
    if (!put_byte(enc, 0x3b)) {
        mime_gif_encoder_free(enc);
        return MIME_GIF_ERR_NOMEM;
    }
    if (!enc->needs89)
        enc->buf.data[4] = '7';
    *data = enc->buf.data;
    *size = enc->buf.size;
    MimeAllocator a = enc->alloc;
    mime_free(&a, enc);
    return MIME_GIF_OK;
}
