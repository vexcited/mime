#include "tiff_internal.h"

#define CODE_CLEAR 256
#define CODE_EOI 257
#define CODE_FIRST 258
#define CODE_MAX 4096

static void lzw_init(TiffLzwDec *st)
{
    for (unsigned i = 0; i < 256; i++) {
        st->ident[i] = (uint8_t)i;
        st->tab[i].p = &st->ident[i];
        st->tab[i].len = 1;
    }
    memset(st->ident + 256, 0, sizeof st->ident - 256);
}

/*
 * the msb first decoder of the tiff specification. every table entry points at an earlier occurrence of its
 * string in the output, so a code is a plain copy. a new entry is the previous string plus the first byte
 * of the current one, which is the previous string extended by one byte in the output.
 * early is 1 when the code width grows one code before the table is full, as in the specification, and 0 for
 * the encoders that grow it late.
 */
static size_t lzw_decode_msb(TiffLzwDec *st, const uint8_t *src, size_t n, uint8_t *dst, size_t cap, bool *error,
                             unsigned early)
{
    uint8_t *out = dst, *out_end = dst + cap;
    uint64_t bitpos = 0;
    uint64_t total_bits = (uint64_t)n * 8;
    unsigned nbits = 9, free_ent = CODE_FIRST;
    const uint8_t *prev_p = NULL;
    uint32_t prev_len = 0;
    *error = false;

    for (;;) {
        size_t bp = (size_t)(bitpos >> 3);
        unsigned code;
        if (TIFF_LIKELY(n >= 8 && bp <= n - 8)) {
            uint64_t v;
            memcpy(&v, src + bp, 8);
            v = __builtin_bswap64(v);
            code = (unsigned)(v >> (64 - nbits - (unsigned)(bitpos & 7))) & ((1u << nbits) - 1);
        } else {
            if (bitpos + nbits > total_bits)
                break;
            uint32_t v = (uint32_t)src[bp] << 16;
            if (bp + 1 < n)
                v |= (uint32_t)src[bp + 1] << 8;
            if (bp + 2 < n)
                v |= src[bp + 2];
            code = (v >> (24 - nbits - (unsigned)(bitpos & 7))) & ((1u << nbits) - 1);
        }
        bitpos += nbits;

        const uint8_t *s;
        uint32_t len;
        bool extra = false;
        uint8_t *o = out;
        if (TIFF_LIKELY(code < free_ent && code != CODE_CLEAR && code != CODE_EOI)) {
            s = st->tab[code].p;
            len = st->tab[code].len;
        } else if (code == CODE_EOI) {
            break;
        } else if (code == CODE_CLEAR) {
            nbits = 9;
            free_ent = CODE_FIRST;
            prev_p = NULL;
            continue;
        } else if (code == free_ent && prev_p) {
            s = prev_p;
            len = prev_len + 1;
            extra = true;
        } else {
            *error = true;
            break;
        }
        size_t room = (size_t)(out_end - out);
        if (TIFF_LIKELY(room >= (size_t)len + 32)) {
            if (TIFF_UNLIKELY(extra)) {
                memcpy(o, s, len - 1);
                o[len - 1] = s[0];
            } else {
                /* most strings are short, so one wide copy covers them and the loop is rarely entered. */
                memmove(o, s, 16);
                if (len > 16) {
                    uint8_t *d = o + 16, *de = o + len;
                    const uint8_t *q = s + 16;
                    do {
                        memcpy(d, q, 16);
                        d += 16;
                        q += 16;
                    } while (d < de);
                }
            }
            out += len;
        } else {
            size_t take = len < room ? len : room;
            if (extra) {
                for (size_t i = 0; i < take; i++)
                    o[i] = i + 1 == len ? s[0] : s[i];
            } else {
                for (size_t i = 0; i < take; i++)
                    o[i] = s[i];
            }
            out += take;
            if (take < len)
                break;
        }
        if (prev_p && free_ent < CODE_MAX) {
            st->tab[free_ent].p = prev_p;
            st->tab[free_ent].len = prev_len + 1;
            free_ent++;
            if (free_ent + early >= (1u << nbits) && nbits < 12)
                nbits++;
        }
        prev_p = o;
        prev_len = len;
    }
    return (size_t)(out - dst);
}

/* the lsb first variant of the first versions of the specification: widths grow late. */
static size_t lzw_decode_lsb(TiffLzwDec *st, const uint8_t *src, size_t n, uint8_t *dst, size_t cap, bool *error)
{
    const uint8_t *p = src, *end = src + n;
    uint8_t *out = dst, *out_end = dst + cap;
    uint32_t acc = 0;
    unsigned cnt = 0;
    unsigned nbits = 9, free_ent = CODE_FIRST;
    const uint8_t *prev_p = NULL;
    uint32_t prev_len = 0;
    *error = false;

    for (;;) {
        while (cnt < nbits) {
            if (p == end)
                goto done;
            acc |= (uint32_t)*p++ << cnt;
            cnt += 8;
        }
        unsigned code = acc & ((1u << nbits) - 1);
        acc >>= nbits;
        cnt -= nbits;
        if (code == CODE_EOI)
            break;
        if (code == CODE_CLEAR) {
            nbits = 9;
            free_ent = CODE_FIRST;
            prev_p = NULL;
            continue;
        }
        const uint8_t *s;
        uint32_t len;
        bool extra = false;
        if (code < free_ent) {
            s = st->tab[code].p;
            len = st->tab[code].len;
        } else if (code == free_ent && prev_p) {
            s = prev_p;
            len = prev_len + 1;
            extra = true;
        } else {
            *error = true;
            break;
        }
        size_t room = (size_t)(out_end - out);
        size_t take = len < room ? len : room;
        uint8_t *o = out;
        for (size_t i = 0; i < take; i++)
            o[i] = (extra && i + 1 == len) ? s[0] : s[i];
        out += take;
        if (take < len)
            break;
        if (prev_p && free_ent < CODE_MAX) {
            st->tab[free_ent].p = prev_p;
            st->tab[free_ent].len = prev_len + 1;
            free_ent++;
            if (free_ent >= (1u << nbits) && nbits < 12)
                nbits++;
        }
        prev_p = o;
        prev_len = len;
    }
done:
    return (size_t)(out - dst);
}

size_t tiff_lzw_decode(TiffLzwDec *st, const uint8_t *src, size_t n, uint8_t *dst, size_t cap, bool *error)
{
    lzw_init(st);
    if (n >= 2 && src[0] == 0 && (src[1] & 1))
        return lzw_decode_lsb(st, src, n, dst, cap, error);
    size_t got = lzw_decode_msb(st, src, n, dst, cap, error, 1);
    if (!*error)
        return got;
    /* some encoders widen the codes late, try that before giving up. */
    bool err2;
    size_t got2 = lzw_decode_msb(st, src, n, dst, cap, &err2, 0);
    if (!err2 || got2 > got) {
        *error = err2;
        return got2;
    }
    got = lzw_decode_msb(st, src, n, dst, cap, error, 1);
    return got;
}

typedef struct BitOut {
    uint8_t *o;
    uint64_t acc;
    unsigned cnt;
} BitOut;

static inline void put_code(BitOut *b, unsigned code, unsigned nbits)
{
    b->acc = (b->acc << nbits) | code;
    b->cnt += nbits;
    if (b->cnt >= 32) {
        uint32_t w = __builtin_bswap32((uint32_t)(b->acc >> (b->cnt - 32)));
        memcpy(b->o, &w, 4);
        b->o += 4;
        b->cnt -= 32;
    }
}

size_t tiff_lzw_encode(TiffLzwEnc *st, const uint8_t *src, size_t n, uint8_t *dst)
{
    BitOut b = {.o = dst};
    uint32_t *ht = st->hash;
    memset(ht, 0, sizeof st->hash);
    unsigned nbits = 9, free_ent = CODE_FIRST;
    put_code(&b, CODE_CLEAR, 9);
    if (n) {
        unsigned ent = src[0];
        for (size_t i = 1; i < n; i++) {
            unsigned c = src[i];
            uint32_t key = (ent << 8) | c;
            uint32_t h = (key * 2654435761u) >> 19;
            uint32_t e;
            while ((e = ht[h]) != 0) {
                if ((e >> 12) == key)
                    break;
                h = (h + 1) & 8191;
            }
            if (e) {
                ent = e & 0xfff;
                continue;
            }
            put_code(&b, ent, nbits);
            ht[h] = (key << 12) | free_ent;
            free_ent++;
            if (free_ent == 4094) {
                put_code(&b, CODE_CLEAR, nbits);
                memset(ht, 0, sizeof st->hash);
                free_ent = CODE_FIRST;
                nbits = 9;
            } else if (free_ent >= (1u << nbits)) {
                nbits++;
            }
            ent = c;
        }
        put_code(&b, ent, nbits);
        free_ent++;
        if (free_ent == 4094) {
            put_code(&b, CODE_CLEAR, nbits);
            nbits = 9;
        } else if (free_ent >= (1u << nbits)) {
            nbits++;
        }
    }
    put_code(&b, CODE_EOI, nbits);
    while (b.cnt >= 8) {
        *b.o++ = (uint8_t)(b.acc >> (b.cnt - 8));
        b.cnt -= 8;
    }
    if (b.cnt)
        *b.o++ = (uint8_t)(b.acc << (8 - b.cnt));
    return (size_t)(b.o - dst);
}
