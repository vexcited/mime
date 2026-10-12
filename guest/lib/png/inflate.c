#include "inflate.h"

#define LIT_BITS 10
#define DIST_BITS 8
#define CL_BITS 7
#define LIT_MASK ((1u << LIT_BITS) - 1)
#define DIST_MASK ((1u << DIST_BITS) - 1)
#define CL_MASK ((1u << CL_BITS) - 1)

/* room needed by the fast loop: the longest match plus the overshoot of 16 byte copies. */
#define FAST_OUT_MARGIN (258 + 32)

/* internal result of a huffman block: the end of block code was decoded. */
#define BLOCK_END 1

enum { M_ZHEADER, M_HEADER, M_STORED, M_HUFF, M_TRAILER, M_DONE };

/*
 * table entry layout: bits 0-3 code length, bits 4-7 extra bits, bits 8-9
 * kind, bits 16-31 value. K_SLOW entries send decoding to the canonical
 * decoder, either because the code is longer than the table index or because
 * the prefix is unused or maps to an invalid symbol.
 */
enum { K_LIT, K_BASE, K_EOB, K_SLOW };
#define E_LEN(e) ((e) & 15u)
#define E_EXTRA(e) (((e) >> 4) & 15u)
#define E_KIND(e) (((e) >> 8) & 3u)
#define E_VAL(e) ((e) >> 16)
#define ENTRY(val, kind, extra, len) \
    (((uint32_t)(val) << 16) | ((uint32_t)(kind) << 8) | ((uint32_t)(extra) << 4) | (uint32_t)(len))
#define SLOW_ENTRY ENTRY(0, K_SLOW, 0, 0)

enum { T_CL, T_LIT, T_DIST };

static const uint16_t len_base[29] = {
    3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258,
};
static const uint8_t len_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
};
static const uint16_t dist_base[30] = {
    1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577,
};
static const uint8_t dist_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13,
};
static const uint8_t cl_order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

static inline uint64_t load64(const uint8_t *p)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint64_t v;
    __builtin_memcpy(&v, p, 8);
    return v;
#else
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--)
        v = (v << 8) | p[i];
    return v;
#endif
}

static uint32_t sym_entry(int type, unsigned sym, unsigned len)
{
    if (type == T_CL)
        return ENTRY(sym, K_LIT, 0, len);
    if (type == T_LIT) {
        if (sym < 256)
            return ENTRY(sym, K_LIT, 0, len);
        if (sym == 256)
            return ENTRY(0, K_EOB, 0, len);
        if (sym < 286)
            return ENTRY(len_base[sym - 257], K_BASE, len_extra[sym - 257], len);
        return SLOW_ENTRY;
    }
    if (sym < 30)
        return ENTRY(dist_base[sym], K_BASE, dist_extra[sym], len);
    return SLOW_ENTRY;
}

/*
 * builds the lookup table and the canonical decoding data from code lengths.
 * fails on an over-subscribed code. incomplete codes are accepted; their
 * unused codes fail when decoded.
 */
static bool build_table(const uint8_t *lens, unsigned n, int type, uint32_t *table, unsigned tbits,
                        uint16_t *count, uint16_t *sym)
{
    uint16_t offs[16];
    unsigned size = 1u << tbits;

    for (unsigned i = 0; i < 16; i++)
        count[i] = 0;
    for (unsigned i = 0; i < n; i++)
        count[lens[i]]++;
    count[0] = 0;

    int left = 1;
    for (unsigned len = 1; len < 16; len++) {
        left = (left << 1) - count[len];
        if (left < 0)
            return false;
    }

    offs[1] = 0;
    for (unsigned len = 1; len < 15; len++)
        offs[len + 1] = (uint16_t)(offs[len] + count[len]);
    for (unsigned i = 0; i < n; i++)
        if (lens[i])
            sym[offs[lens[i]]++] = (uint16_t)i;

    for (unsigned i = 0; i < size; i++)
        table[i] = SLOW_ENTRY;

    unsigned code = 0, k = 0;
    for (unsigned len = 1; len < 16; len++) {
        for (unsigned j = 0; j < count[len]; j++, k++, code++) {
            if (len > tbits)
                continue;
            uint32_t e = sym_entry(type, sym[k], len);
            for (unsigned idx = __builtin_bitreverse32(code) >> (32 - len); idx < size; idx += 1u << len)
                table[idx] = e;
        }
        code <<= 1;
    }
    return true;
}

/* bit by bit canonical decoding, used for long and invalid codes. */
static int slow_decode(uint64_t bits, const uint16_t *count, const uint16_t *sym, unsigned *used)
{
    int code = 0, first = 0, index = 0;

    for (unsigned len = 1; len < 16; len++) {
        code |= (int)(bits & 1);
        bits >>= 1;
        int c = count[len];
        if (code - first < c) {
            *used = len;
            return sym[index + code - first];
        }
        index += c;
        first = (first + c) << 1;
        code <<= 1;
    }
    return -1;
}

static void refill_slow(MimeInflate *s)
{
    while (s->bitcnt <= 56) {
        uint64_t b = 0;
        if (s->in < s->in_end)
            b = *s->in++;
        else
            s->overrun++;
        s->bitbuf |= b << s->bitcnt;
        s->bitcnt += 8;
    }
}

/* true once bits past the end of the input, read as zeros, have been consumed. */
static inline bool past_end(const MimeInflate *s)
{
    return s->overrun * 8 > s->bitcnt;
}

static inline uint32_t take_bits(MimeInflate *s, unsigned n)
{
    uint32_t v = (uint32_t)(s->bitbuf & ((1u << n) - 1));
    s->bitbuf >>= n;
    s->bitcnt -= n;
    return v;
}

/* decodes one symbol, the caller has refilled the bit buffer. */
static int decode_sym(MimeInflate *s, const uint32_t *table, unsigned mask, const uint16_t *count,
                      const uint16_t *sym, int type, uint32_t *entry)
{
    uint32_t e = table[s->bitbuf & mask];

    if (E_KIND(e) == K_SLOW) {
        unsigned used = 15;
        int v = slow_decode(s->bitbuf, count, sym, &used);
        if (v >= 0)
            e = sym_entry(type, (unsigned)v, used);
        if (v < 0 || E_KIND(e) == K_SLOW) {
            /* a bad code made of bits past the end of the input means truncation. */
            return s->overrun * 8 + used > s->bitcnt ? MIME_INFLATE_ERR_TRUNCATED : MIME_INFLATE_ERR_DATA;
        }
    }
    s->bitbuf >>= E_LEN(e);
    s->bitcnt -= E_LEN(e);
    *entry = e;
    return MIME_INFLATE_OK;
}

/* drops bits up to a byte boundary and gives the whole bytes left in the bit buffer back to the input. */
static int byte_align_input(MimeInflate *s)
{
    unsigned drop = s->bitcnt & 7;
    s->bitbuf >>= drop;
    s->bitcnt -= drop;
    unsigned bytes = s->bitcnt >> 3;
    if (s->overrun > bytes)
        return MIME_INFLATE_ERR_TRUNCATED;
    s->in -= bytes - s->overrun;
    s->bitbuf = 0;
    s->bitcnt = 0;
    s->overrun = 0;
    return MIME_INFLATE_OK;
}

static void load_fixed(MimeInflate *s)
{
    uint8_t lens[288];
    unsigned i = 0;

    for (; i < 144; i++)
        lens[i] = 8;
    for (; i < 256; i++)
        lens[i] = 9;
    for (; i < 280; i++)
        lens[i] = 7;
    for (; i < 288; i++)
        lens[i] = 8;
    build_table(lens, 288, T_LIT, s->lit_table, LIT_BITS, s->lit_count, s->lit_sym);
    for (i = 0; i < 32; i++)
        lens[i] = 5;
    build_table(lens, 32, T_DIST, s->dist_table, DIST_BITS, s->dist_count, s->dist_sym);
    s->fixed_loaded = true;
}

static int read_dynamic(MimeInflate *s)
{
    uint8_t lens[288 + 32];
    uint8_t cl[19] = {0};
    int r;

    s->fixed_loaded = false;
    refill_slow(s);
    unsigned nlit = take_bits(s, 5) + 257;
    unsigned ndist = take_bits(s, 5) + 1;
    unsigned ncl = take_bits(s, 4) + 4;
    for (unsigned i = 0; i < ncl; i++) {
        if (s->bitcnt < 3)
            refill_slow(s);
        cl[cl_order[i]] = (uint8_t)take_bits(s, 3);
    }
    if (past_end(s))
        return MIME_INFLATE_ERR_TRUNCATED;
    /* the code length table borrows the literal table. */
    if (!build_table(cl, 19, T_CL, s->lit_table, CL_BITS, s->lit_count, s->lit_sym))
        return MIME_INFLATE_ERR_DATA;

    unsigned total = nlit + ndist, i = 0;
    while (i < total) {
        uint32_t e;
        refill_slow(s);
        r = decode_sym(s, s->lit_table, CL_MASK, s->lit_count, s->lit_sym, T_CL, &e);
        if (r)
            return r;
        unsigned sym = E_VAL(e), rep;
        uint8_t val = 0;
        if (sym < 16) {
            val = (uint8_t)sym;
            rep = 1;
        } else if (sym == 16) {
            if (i == 0)
                return MIME_INFLATE_ERR_DATA;
            val = lens[i - 1];
            rep = 3 + take_bits(s, 2);
        } else if (sym == 17) {
            rep = 3 + take_bits(s, 3);
        } else {
            rep = 11 + take_bits(s, 7);
        }
        if (past_end(s))
            return MIME_INFLATE_ERR_TRUNCATED;
        if (rep > total - i)
            return MIME_INFLATE_ERR_DATA;
        __builtin_memset(lens + i, val, rep);
        i += rep;
    }
    if (lens[256] == 0)
        return MIME_INFLATE_ERR_DATA;
    if (!build_table(lens, nlit, T_LIT, s->lit_table, LIT_BITS, s->lit_count, s->lit_sym))
        return MIME_INFLATE_ERR_DATA;
    if (!build_table(lens + nlit, ndist, T_DIST, s->dist_table, DIST_BITS, s->dist_count, s->dist_sym))
        return MIME_INFLATE_ERR_DATA;
    return MIME_INFLATE_OK;
}

static int read_block_header(MimeInflate *s)
{
    int r;

    refill_slow(s);
    s->last = take_bits(s, 1) != 0;
    unsigned type = take_bits(s, 2);
    if (past_end(s))
        return MIME_INFLATE_ERR_TRUNCATED;
    switch (type) {
    case 0: {
        r = byte_align_input(s);
        if (r)
            return r;
        if (s->in_end - s->in < 4)
            return MIME_INFLATE_ERR_TRUNCATED;
        unsigned len = s->in[0] | (unsigned)s->in[1] << 8;
        unsigned nlen = s->in[2] | (unsigned)s->in[3] << 8;
        if (len != (~nlen & 0xffffu))
            return MIME_INFLATE_ERR_DATA;
        s->in += 4;
        s->stored_left = len;
        s->mode = M_STORED;
        return MIME_INFLATE_OK;
    }
    case 1:
        if (!s->fixed_loaded)
            load_fixed(s);
        s->mode = M_HUFF;
        return MIME_INFLATE_OK;
    case 2:
        r = read_dynamic(s);
        if (r)
            return r;
        s->mode = M_HUFF;
        return MIME_INFLATE_OK;
    default:
        return MIME_INFLATE_ERR_DATA;
    }
}

static int stored_block(MimeInflate *s)
{
    while (s->stored_left) {
        size_t avail_in = (size_t)(s->in_end - s->in);
        size_t avail_out = (size_t)(s->out_end - s->out);
        if (!avail_in)
            return MIME_INFLATE_ERR_TRUNCATED;
        if (!avail_out)
            return MIME_INFLATE_ERR_OUTPUT;
        size_t n = s->stored_left;
        if (n > avail_in)
            n = avail_in;
        if (n > avail_out)
            n = avail_out;
        __builtin_memcpy(s->out, s->in, n);
        s->out += n;
        s->in += n;
        s->stored_left -= (uint32_t)n;
    }
    return MIME_INFLATE_OK;
}

/* copies a match byte by byte up to the end of the output, saving the rest for later. */
static int copy_match_slow(MimeInflate *s, uint32_t len, uint32_t dist)
{
    uint8_t *out = s->out;
    size_t room = (size_t)(s->out_end - out);
    uint32_t n = len <= room ? len : (uint32_t)room;
    const uint8_t *src = out - dist;

    for (uint32_t i = 0; i < n; i++)
        out[i] = src[i];
    s->out = out + n;
    if (n < len) {
        s->pend_len = len - n;
        s->pend_dist = dist;
        return MIME_INFLATE_ERR_OUTPUT;
    }
    s->pend_len = 0;
    return MIME_INFLATE_OK;
}

/*
 * decodes symbols while at least 8 input bytes and FAST_OUT_MARGIN output
 * bytes remain, so that one refill covers a whole length and distance pair
 * of at most 48 bits and no output bound check is needed per symbol.
 */
static int fast_loop(MimeInflate *s)
{
    const uint8_t *in = s->in;
    const uint8_t *const in_end = s->in_end;
    uint8_t *out = s->out;
    uint8_t *const out_start = s->out_start;
    uint8_t *const out_end = s->out_end;
    const uint32_t *const lt = s->lit_table;
    const uint32_t *const dt = s->dist_table;
    uint64_t bb = s->bitbuf;
    unsigned bc = s->bitcnt;
    int ret = MIME_INFLATE_OK;

    while (in_end - in >= 8 && out_end - out >= FAST_OUT_MARGIN) {
        bb |= load64(in) << bc;
        in += (63 - bc) >> 3;
        bc |= 56;

        uint32_t e = lt[bb & LIT_MASK];
        if (__builtin_expect(E_KIND(e) == K_SLOW, 0)) {
            unsigned used;
            int v = slow_decode(bb, s->lit_count, s->lit_sym, &used);
            if (v >= 0)
                e = sym_entry(T_LIT, (unsigned)v, used);
            if (v < 0 || E_KIND(e) == K_SLOW) {
                ret = MIME_INFLATE_ERR_DATA;
                break;
            }
        }
        bb >>= E_LEN(e);
        bc -= E_LEN(e);
        if (E_KIND(e) == K_LIT) {
            *out++ = (uint8_t)E_VAL(e);
            /* at least 41 bits remain, enough for two more table resolved literals. */
            e = lt[bb & LIT_MASK];
            if (E_KIND(e) != K_LIT || !E_LEN(e))
                continue;
            bb >>= E_LEN(e);
            bc -= E_LEN(e);
            *out++ = (uint8_t)E_VAL(e);
            e = lt[bb & LIT_MASK];
            if (E_KIND(e) != K_LIT || !E_LEN(e))
                continue;
            bb >>= E_LEN(e);
            bc -= E_LEN(e);
            *out++ = (uint8_t)E_VAL(e);
            continue;
        }
        if (E_KIND(e) == K_EOB) {
            ret = BLOCK_END;
            break;
        }

        unsigned extra = E_EXTRA(e);
        uint32_t len = E_VAL(e) + (uint32_t)(bb & ((1u << extra) - 1));
        bb >>= extra;
        bc -= extra;

        e = dt[bb & DIST_MASK];
        if (__builtin_expect(E_KIND(e) == K_SLOW, 0)) {
            unsigned used;
            int v = slow_decode(bb, s->dist_count, s->dist_sym, &used);
            if (v >= 0)
                e = sym_entry(T_DIST, (unsigned)v, used);
            if (v < 0 || E_KIND(e) == K_SLOW) {
                ret = MIME_INFLATE_ERR_DATA;
                break;
            }
        }
        bb >>= E_LEN(e);
        bc -= E_LEN(e);
        extra = E_EXTRA(e);
        uint32_t dist = E_VAL(e) + (uint32_t)(bb & ((1u << extra) - 1));
        bb >>= extra;
        bc -= extra;

        if (dist > (size_t)(out - out_start)) {
            ret = MIME_INFLATE_ERR_DATA;
            break;
        }
        const uint8_t *src = out - dist;
        uint8_t *end = out + len;
        if (dist >= 16) {
            uint8_t *d = out;
            do {
                __builtin_memcpy(d, src, 16);
                d += 16;
                src += 16;
            } while (d < end);
        } else if (dist >= 8) {
            uint8_t *d = out;
            do {
                __builtin_memcpy(d, src, 8);
                d += 8;
                src += 8;
            } while (d < end);
        } else if (dist == 1) {
            __builtin_memset(out, *src, len);
        } else {
            for (uint32_t i = 0; i < len; i++)
                out[i] = src[i];
        }
        out = end;
    }

    s->in = in;
    s->out = out;
    s->bitbuf = bb;
    s->bitcnt = bc;
    return ret;
}

/* decodes one symbol with full bound checks. */
static int slow_symbol(MimeInflate *s)
{
    uint32_t e;
    int r;

    refill_slow(s);
    r = decode_sym(s, s->lit_table, LIT_MASK, s->lit_count, s->lit_sym, T_LIT, &e);
    if (r)
        return r;
    if (past_end(s))
        return MIME_INFLATE_ERR_TRUNCATED;
    if (E_KIND(e) == K_LIT) {
        if (s->out == s->out_end) {
            s->pend_lit = (int)E_VAL(e);
            return MIME_INFLATE_ERR_OUTPUT;
        }
        *s->out++ = (uint8_t)E_VAL(e);
        return MIME_INFLATE_OK;
    }
    if (E_KIND(e) == K_EOB)
        return BLOCK_END;

    /* at least 57 bits were available and at most 20 are used so far. */
    uint32_t len = E_VAL(e) + take_bits(s, E_EXTRA(e));
    r = decode_sym(s, s->dist_table, DIST_MASK, s->dist_count, s->dist_sym, T_DIST, &e);
    if (r)
        return r;
    uint32_t dist = E_VAL(e) + take_bits(s, E_EXTRA(e));
    if (past_end(s))
        return MIME_INFLATE_ERR_TRUNCATED;
    if (dist > (size_t)(s->out - s->out_start))
        return MIME_INFLATE_ERR_DATA;
    return copy_match_slow(s, len, dist);
}

static int huff_block(MimeInflate *s)
{
    int r;

    if (s->pend_lit >= 0) {
        if (s->out == s->out_end)
            return MIME_INFLATE_ERR_OUTPUT;
        *s->out++ = (uint8_t)s->pend_lit;
        s->pend_lit = -1;
    }
    if (s->pend_len) {
        r = copy_match_slow(s, s->pend_len, s->pend_dist);
        if (r)
            return r;
    }
    for (;;) {
        r = fast_loop(s);
        if (r == BLOCK_END)
            return MIME_INFLATE_OK;
        if (r)
            return r;
        r = slow_symbol(s);
        if (r == BLOCK_END)
            return MIME_INFLATE_OK;
        if (r)
            return r;
    }
}

static int zlib_header(MimeInflate *s)
{
    if (s->in_end - s->in < 2)
        return MIME_INFLATE_ERR_TRUNCATED;
    unsigned cmf = s->in[0], flg = s->in[1];
    if ((cmf & 15) != 8 || (cmf >> 4) > 7 || ((cmf << 8) | flg) % 31 != 0)
        return MIME_INFLATE_ERR_DATA;
    /* preset dictionaries are not supported. */
    if (flg & 0x20)
        return MIME_INFLATE_ERR_DATA;
    s->in += 2;
    return MIME_INFLATE_OK;
}

static int trailer(MimeInflate *s, unsigned flags)
{
    if (!(flags & MIME_INFLATE_ZLIB))
        return past_end(s) ? MIME_INFLATE_ERR_TRUNCATED : MIME_INFLATE_OK;
    int r = byte_align_input(s);
    if (r)
        return r;
    if (s->in_end - s->in < 4)
        return MIME_INFLATE_ERR_TRUNCATED;
    uint32_t want = (uint32_t)s->in[0] << 24 | (uint32_t)s->in[1] << 16 | (uint32_t)s->in[2] << 8 | s->in[3];
    s->in += 4;
    if (!(flags & MIME_INFLATE_NO_CHECKSUM) &&
        mime_inflate_adler32(1, s->out_start, (size_t)(s->out - s->out_start)) != want)
        return MIME_INFLATE_ERR_CHECKSUM;
    return MIME_INFLATE_OK;
}

static int run(MimeInflate *s, unsigned flags)
{
    for (;;) {
        int r;
        switch (s->mode) {
        case M_ZHEADER:
            r = zlib_header(s);
            if (!r)
                s->mode = M_HEADER;
            break;
        case M_HEADER:
            r = read_block_header(s);
            break;
        case M_STORED:
            r = stored_block(s);
            if (!r)
                s->mode = s->last ? M_TRAILER : M_HEADER;
            break;
        case M_HUFF:
            r = huff_block(s);
            if (!r)
                s->mode = s->last ? M_TRAILER : M_HEADER;
            break;
        case M_TRAILER:
            r = trailer(s, flags);
            if (!r)
                s->mode = M_DONE;
            break;
        default:
            return MIME_INFLATE_OK;
        }
        if (r)
            return r;
    }
}

static uint8_t empty_buffer[1];

static void state_init(MimeInflate *s, const void *src, size_t src_len, void *dst, size_t dst_cap, unsigned flags)
{
    s->in = src ? (const uint8_t *)src : empty_buffer;
    s->in_end = s->in + src_len;
    s->out_start = dst ? (uint8_t *)dst : empty_buffer;
    s->out = s->out_start;
    s->out_end = s->out_start + dst_cap;
    s->bitbuf = 0;
    s->bitcnt = 0;
    s->overrun = 0;
    s->mode = (flags & MIME_INFLATE_ZLIB) ? M_ZHEADER : M_HEADER;
    s->last = false;
    s->fixed_loaded = false;
    s->pend_lit = -1;
    s->pend_len = 0;
    s->pend_dist = 0;
    s->stored_left = 0;
}

int mime_inflate_with(MimeInflate *s, const void *src, size_t src_len, void *dst, size_t dst_cap,
                      size_t *out_len, unsigned flags)
{
    if (out_len)
        *out_len = 0;
    if (!s || (!src && src_len) || (!dst && dst_cap))
        return MIME_INFLATE_ERR_PARAM;
    state_init(s, src, src_len, dst, dst_cap, flags);
    int r = run(s, flags);
    if (out_len)
        *out_len = (size_t)(s->out - s->out_start);
    return r;
}

int mime_inflate(const void *src, size_t src_len, void *dst, size_t dst_cap, size_t *out_len, unsigned flags)
{
    MimeInflate s;
    return mime_inflate_with(&s, src, src_len, dst, dst_cap, out_len, flags);
}

int mime_inflate_alloc(const void *src, size_t src_len, const MimeAllocator *alloc, size_t size_hint,
                       size_t max_size, void **out, size_t *out_len, unsigned flags)
{
    if (out)
        *out = NULL;
    if (out_len)
        *out_len = 0;
    if (!alloc || !out || (!src && src_len))
        return MIME_INFLATE_ERR_PARAM;

    MimeInflate *s = mime_alloc(alloc, sizeof(*s));
    if (!s)
        return MIME_INFLATE_ERR_NOMEM;

    size_t cap = size_hint;
    if (!cap)
        cap = src_len <= SIZE_MAX / 4 ? src_len * 4 : SIZE_MAX;
    if (cap < 256)
        cap = 256;
    if (max_size && cap > max_size)
        cap = max_size;
    uint8_t *buf = mime_alloc(alloc, cap);
    if (!buf) {
        mime_free(alloc, s);
        return MIME_INFLATE_ERR_NOMEM;
    }

    state_init(s, src, src_len, buf, cap, flags);
    int r;
    for (;;) {
        r = run(s, flags);
        if (r != MIME_INFLATE_ERR_OUTPUT || (max_size && cap >= max_size))
            break;
        size_t ncap = cap > SIZE_MAX / 2 ? SIZE_MAX : cap * 2;
        if (max_size && ncap > max_size)
            ncap = max_size;
        uint8_t *nbuf = mime_alloc(alloc, ncap);
        if (!nbuf) {
            r = MIME_INFLATE_ERR_NOMEM;
            break;
        }
        size_t used = (size_t)(s->out - s->out_start);
        __builtin_memcpy(nbuf, buf, used);
        mime_free(alloc, buf);
        buf = nbuf;
        cap = ncap;
        s->out_start = buf;
        s->out = buf + used;
        s->out_end = buf + cap;
    }

    size_t len = (size_t)(s->out - s->out_start);
    mime_free(alloc, s);
    if (r) {
        mime_free(alloc, buf);
        return r;
    }
    *out = buf;
    if (out_len)
        *out_len = len;
    return MIME_INFLATE_OK;
}

uint32_t mime_inflate_adler32(uint32_t adler, const void *data, size_t len)
{
    const uint8_t *p = data;
    uint32_t a = adler & 0xffff, b = adler >> 16;

    while (len) {
        /* 5552 is the longest run before the 32 bit sums can overflow. */
        size_t n = len < 5552 ? len : 5552;
        len -= n;
        /* per 16 bytes, b gains 16 times the old a plus the bytes weighted 16 down to 1. */
        while (n >= 16) {
            uint32_t sum = 0, weighted = 0;
            for (unsigned i = 0; i < 16; i++) {
                sum += p[i];
                weighted += (16 - i) * (uint32_t)p[i];
            }
            b += a * 16 + weighted;
            a += sum;
            p += 16;
            n -= 16;
        }
        while (n--) {
            a += *p++;
            b += a;
        }
        a %= 65521;
        b %= 65521;
    }
    return b << 16 | a;
}
