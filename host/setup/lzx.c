#include "lzx.h"

#include <stdlib.h>
#include <string.h>

#define LZX_NUM_CHARS 256
#define LZX_POSITION_SLOTS 30 /* for a 32 KiB window */
#define LZX_MAIN_SYMS (LZX_NUM_CHARS + LZX_POSITION_SLOTS * 8)
#define LZX_LEN_SYMS 249
#define LZX_ALIGNED_SYMS 8
#define LZX_PRE_SYMS 20
#define LZX_MAX_CODE_LEN 16
#define LZX_MIN_MATCH 2
#define LZX_E8_SIZE 12000000

enum {
    LZX_BLOCK_VERBATIM = 1,
    LZX_BLOCK_ALIGNED = 2,
    LZX_BLOCK_UNCOMPRESSED = 3,
};

#define FAST_BITS 11

typedef struct HuffTable {
    uint16_t fast[1 << FAST_BITS]; /* symbol << 5 | length, 0 for slow path */
    uint16_t count[LZX_MAX_CODE_LEN + 1];
    uint16_t offset[LZX_MAX_CODE_LEN + 1];
    uint32_t first[LZX_MAX_CODE_LEN + 1];
    uint16_t sorted[LZX_MAIN_SYMS];
    uint32_t num_syms;
} HuffTable;

typedef struct BitReader {
    const uint8_t *data;
    size_t size;
    size_t pos; /* next byte to load into the buffer */
    uint64_t buf; /* left aligned */
    uint32_t bits;
    bool overrun;
} BitReader;

struct MimeLzx {
    HuffTable main_table;
    HuffTable len_table;
    HuffTable aligned_table;
    HuffTable pre_table;
    uint8_t main_lens[LZX_MAIN_SYMS];
    uint8_t len_lens[LZX_LEN_SYMS];
    uint8_t aligned_lens[LZX_ALIGNED_SYMS];
};

static uint32_t g_extra_bits[LZX_POSITION_SLOTS];
static uint32_t g_position_base[LZX_POSITION_SLOTS];

static void init_slots(void)
{
    static bool done;
    if (done)
        return;
    uint32_t base = 0;
    for (int s = 0; s < LZX_POSITION_SLOTS; s++) {
        uint32_t extra = s < 4 ? 0 : (uint32_t)(s - 2) >> 1;
        if (extra > 17)
            extra = 17;
        g_extra_bits[s] = extra;
        g_position_base[s] = base;
        base += 1u << extra;
    }
    done = true;
}

MimeLzx *mime_lzx_create(void)
{
    init_slots();
    return mime_xcalloc(1, sizeof(MimeLzx));
}

void mime_lzx_destroy(MimeLzx *lzx)
{
    free(lzx);
}

static void br_init(BitReader *br, const uint8_t *data, size_t size)
{
    br->data = data;
    br->size = size;
    br->pos = 0;
    br->buf = 0;
    br->bits = 0;
    br->overrun = false;
}

static inline void br_refill(BitReader *br)
{
    while (br->bits <= 48) {
        uint32_t word = 0;
        if (br->pos + 1 < br->size)
            word = (uint32_t)br->data[br->pos] | (uint32_t)br->data[br->pos + 1] << 8;
        else if (br->pos >= br->size + 8)
            br->overrun = true;
        br->pos += 2;
        br->buf |= (uint64_t)word << (48 - br->bits);
        br->bits += 16;
    }
}

static inline uint32_t br_peek(BitReader *br, uint32_t n)
{
    return n ? (uint32_t)(br->buf >> (64 - n)) : 0;
}

static inline void br_skip(BitReader *br, uint32_t n)
{
    br->buf <<= n;
    br->bits -= n;
}

static inline uint32_t br_read(BitReader *br, uint32_t n)
{
    br_refill(br);
    uint32_t v = br_peek(br, n);
    br_skip(br, n);
    return v;
}

/* byte position of the first bit not consumed yet. */
static inline size_t br_consumed_bits(const BitReader *br)
{
    return br->pos * 8 - br->bits;
}

static bool huff_build(HuffTable *t, const uint8_t *lens, uint32_t num_syms)
{
    memset(t->count, 0, sizeof(t->count));
    memset(t->fast, 0, sizeof(t->fast));
    t->num_syms = num_syms;
    for (uint32_t i = 0; i < num_syms; i++)
        t->count[lens[i]]++;
    t->count[0] = 0;

    /* reject over-subscribed codes; incomplete ones are legal in lzx. */
    int32_t left = 1;
    for (int len = 1; len <= LZX_MAX_CODE_LEN; len++) {
        left = (left << 1) - t->count[len];
        if (left < 0)
            return false;
    }

    uint32_t code = 0;
    uint16_t off = 0;
    for (int len = 1; len <= LZX_MAX_CODE_LEN; len++) {
        t->first[len] = code;
        t->offset[len] = off;
        off += t->count[len];
        code = (code + t->count[len]) << 1;
    }
    uint16_t next[LZX_MAX_CODE_LEN + 1];
    memcpy(next, t->offset, sizeof(next));
    for (uint32_t sym = 0; sym < num_syms; sym++) {
        if (lens[sym])
            t->sorted[next[lens[sym]]++] = (uint16_t)sym;
    }

    for (int len = 1; len <= FAST_BITS; len++) {
        for (uint32_t i = 0; i < t->count[len]; i++) {
            uint32_t sym = t->sorted[t->offset[len] + i];
            uint32_t c = t->first[len] + i;
            uint32_t start = c << (FAST_BITS - len);
            uint32_t span = 1u << (FAST_BITS - len);
            for (uint32_t j = 0; j < span; j++)
                t->fast[start + j] = (uint16_t)(sym << 5 | len);
        }
    }
    return true;
}

static inline int huff_decode(BitReader *br, const HuffTable *t)
{
    br_refill(br);
    uint16_t e = t->fast[br_peek(br, FAST_BITS)];
    if (e) {
        br_skip(br, e & 31);
        return e >> 5;
    }
    uint32_t code = br_peek(br, LZX_MAX_CODE_LEN);
    for (int len = FAST_BITS + 1; len <= LZX_MAX_CODE_LEN; len++) {
        uint32_t c = code >> (LZX_MAX_CODE_LEN - len);
        uint32_t idx = c - t->first[len];
        if (idx < t->count[len]) {
            br_skip(br, len);
            return t->sorted[t->offset[len] + idx];
        }
    }
    return -1;
}

static bool read_lens(MimeLzx *lzx, BitReader *br, uint8_t *lens, uint32_t first, uint32_t last)
{
    uint8_t pre_lens[LZX_PRE_SYMS];
    for (int i = 0; i < LZX_PRE_SYMS; i++)
        pre_lens[i] = (uint8_t)br_read(br, 4);
    if (!huff_build(&lzx->pre_table, pre_lens, LZX_PRE_SYMS))
        return false;

    uint32_t i = first;
    while (i < last) {
        int sym = huff_decode(br, &lzx->pre_table);
        if (sym < 0)
            return false;
        if (sym == 17) {
            uint32_t n = 4 + br_read(br, 4);
            while (n-- && i < last)
                lens[i++] = 0;
        } else if (sym == 18) {
            uint32_t n = 20 + br_read(br, 5);
            while (n-- && i < last)
                lens[i++] = 0;
        } else if (sym == 19) {
            uint32_t n = 4 + br_read(br, 1);
            int s = huff_decode(br, &lzx->pre_table);
            if (s < 0 || s > 16)
                return false;
            uint8_t v = (uint8_t)((lens[i] + 17 - s) % 17);
            while (n-- && i < last)
                lens[i++] = v;
        } else {
            lens[i] = (uint8_t)((lens[i] + 17 - sym) % 17);
            i++;
        }
    }
    return true;
}

static inline uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void undo_e8(uint8_t *data, size_t size)
{
    if (size <= 10)
        return;
    for (size_t i = 0; i < size - 10; i++) {
        if (data[i] != 0xE8)
            continue;
        int32_t abs_off = (int32_t)rd_le32(data + i + 1);
        int32_t pos = (int32_t)i;
        if (abs_off >= -pos && abs_off < LZX_E8_SIZE) {
            int32_t rel = abs_off >= 0 ? abs_off - pos : abs_off + LZX_E8_SIZE;
            data[i + 1] = (uint8_t)rel;
            data[i + 2] = (uint8_t)(rel >> 8);
            data[i + 3] = (uint8_t)(rel >> 16);
            data[i + 4] = (uint8_t)(rel >> 24);
        }
        i += 4;
    }
}

bool mime_lzx_decompress(MimeLzx *lzx, const void *in, size_t in_size, void *out, size_t out_size)
{
    if (out_size > MIME_LZX_MAX_CHUNK)
        return false;
    uint8_t *dst = out;
    BitReader br;
    br_init(&br, in, in_size);
    memset(lzx->main_lens, 0, sizeof(lzx->main_lens));
    memset(lzx->len_lens, 0, sizeof(lzx->len_lens));
    uint32_t r0 = 1, r1 = 1, r2 = 1;
    size_t pos = 0;

    while (pos < out_size) {
        uint32_t type = br_read(&br, 3);
        uint32_t block_size = br_read(&br, 1) ? 32768 : br_read(&br, 16);
        if (block_size == 0)
            return false;
        size_t end = MIME_MIN(pos + block_size, out_size);

        if (type == LZX_BLOCK_UNCOMPRESSED) {
            /* align to 16 bits; an already aligned stream skips a whole word. */
            size_t consumed = br_consumed_bits(&br);
            size_t pad = 16 - (consumed & 15);
            size_t byte_pos = (consumed + pad) / 8;
            if (byte_pos + 12 > in_size)
                return false;
            r0 = rd_le32((const uint8_t *)in + byte_pos);
            r1 = rd_le32((const uint8_t *)in + byte_pos + 4);
            r2 = rd_le32((const uint8_t *)in + byte_pos + 8);
            byte_pos += 12;
            size_t n = end - pos;
            if (byte_pos + n > in_size)
                return false;
            memcpy(dst + pos, (const uint8_t *)in + byte_pos, n);
            byte_pos += block_size;
            if (block_size & 1)
                byte_pos++;
            pos = end;
            if (byte_pos > in_size)
                byte_pos = in_size;
            br_init(&br, (const uint8_t *)in + byte_pos, in_size - byte_pos);
            continue;
        }

        if (type != LZX_BLOCK_VERBATIM && type != LZX_BLOCK_ALIGNED)
            return false;
        if (type == LZX_BLOCK_ALIGNED) {
            for (int i = 0; i < LZX_ALIGNED_SYMS; i++)
                lzx->aligned_lens[i] = (uint8_t)br_read(&br, 3);
            if (!huff_build(&lzx->aligned_table, lzx->aligned_lens, LZX_ALIGNED_SYMS))
                return false;
        }
        if (!read_lens(lzx, &br, lzx->main_lens, 0, LZX_NUM_CHARS) ||
            !read_lens(lzx, &br, lzx->main_lens, LZX_NUM_CHARS, LZX_MAIN_SYMS) ||
            !huff_build(&lzx->main_table, lzx->main_lens, LZX_MAIN_SYMS))
            return false;
        if (!read_lens(lzx, &br, lzx->len_lens, 0, LZX_LEN_SYMS) ||
            !huff_build(&lzx->len_table, lzx->len_lens, LZX_LEN_SYMS))
            return false;

        while (pos < end) {
            int sym = huff_decode(&br, &lzx->main_table);
            if (sym < 0)
                return false;
            if (sym < LZX_NUM_CHARS) {
                dst[pos++] = (uint8_t)sym;
                continue;
            }
            sym -= LZX_NUM_CHARS;
            uint32_t len = (uint32_t)sym & 7;
            uint32_t slot = (uint32_t)sym >> 3;
            if (len == 7) {
                int extra = huff_decode(&br, &lzx->len_table);
                if (extra < 0)
                    return false;
                len += (uint32_t)extra;
            }
            len += LZX_MIN_MATCH;

            uint32_t offset;
            if (slot == 0) {
                offset = r0;
            } else if (slot == 1) {
                offset = r1;
                r1 = r0;
                r0 = offset;
            } else if (slot == 2) {
                offset = r2;
                r2 = r0;
                r0 = offset;
            } else {
                uint32_t extra = g_extra_bits[slot];
                uint32_t footer;
                if (type == LZX_BLOCK_ALIGNED && extra >= 3) {
                    footer = br_read(&br, extra - 3) << 3;
                    int a = huff_decode(&br, &lzx->aligned_table);
                    if (a < 0)
                        return false;
                    footer += (uint32_t)a;
                } else {
                    footer = br_read(&br, extra);
                }
                offset = g_position_base[slot] + footer - 2;
                r2 = r1;
                r1 = r0;
                r0 = offset;
            }

            if (offset == 0 || offset > pos || pos + len > out_size)
                return false;
            const uint8_t *src = dst + pos - offset;
            uint8_t *d = dst + pos;
            for (uint32_t i = 0; i < len; i++)
                d[i] = src[i];
            pos += len;
        }
        if (br.overrun)
            return false;
    }

    undo_e8(dst, out_size);
    return true;
}
