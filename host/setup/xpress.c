#include "xpress.h"

#include <string.h>

#define XPRESS_SYMS 512
#define XPRESS_MAX_LEN 15
#define XPRESS_FAST_BITS 10
#define XPRESS_MIN_MATCH 3

typedef struct XpressTable {
    uint16_t fast[1 << XPRESS_FAST_BITS]; /* symbol << 4 | length, 0 for slow path */
    uint16_t count[XPRESS_MAX_LEN + 1];
    uint16_t offset[XPRESS_MAX_LEN + 1];
    uint32_t first[XPRESS_MAX_LEN + 1];
    uint16_t sorted[XPRESS_SYMS];
} XpressTable;

/*
 * the bit window keeps at least 16 valid bits on top; spare counts the
 * valid bits beyond those 16. length bytes are read from pos, which
 * already moved past every word loaded into the window.
 */
typedef struct XpressInput {
    const uint8_t *in;
    size_t size;
    size_t pos;
    uint32_t window;
    int32_t spare;
} XpressInput;

static uint32_t next_u16(XpressInput *x)
{
    uint32_t v = 0;
    if (x->pos + 2 <= x->size)
        v = (uint32_t)x->in[x->pos] | (uint32_t)x->in[x->pos + 1] << 8;
    x->pos += 2;
    return v;
}

static void consume(XpressInput *x, uint32_t n)
{
    if (n == 0)
        return;
    x->window <<= n;
    x->spare -= (int32_t)n;
    if (x->spare < 0) {
        x->window |= next_u16(x) << -x->spare;
        x->spare += 16;
    }
}

static bool build_table(XpressTable *t, const uint8_t *lens)
{
    memset(t->count, 0, sizeof(t->count));
    memset(t->fast, 0, sizeof(t->fast));
    for (int s = 0; s < XPRESS_SYMS; s++)
        t->count[lens[s]]++;
    t->count[0] = 0;
    int32_t left = 1;
    for (int len = 1; len <= XPRESS_MAX_LEN; len++) {
        left = (left << 1) - t->count[len];
        if (left < 0)
            return false;
    }
    uint32_t code = 0;
    uint16_t off = 0;
    for (int len = 1; len <= XPRESS_MAX_LEN; len++) {
        t->first[len] = code;
        t->offset[len] = off;
        off += t->count[len];
        code = (code + t->count[len]) << 1;
    }
    uint16_t next[XPRESS_MAX_LEN + 1];
    memcpy(next, t->offset, sizeof(next));
    for (int s = 0; s < XPRESS_SYMS; s++) {
        if (lens[s])
            t->sorted[next[lens[s]]++] = (uint16_t)s;
    }
    for (int len = 1; len <= XPRESS_FAST_BITS; len++) {
        uint32_t span = 1u << (XPRESS_FAST_BITS - len);
        for (uint32_t i = 0; i < t->count[len]; i++) {
            uint32_t sym = t->sorted[t->offset[len] + i];
            uint32_t start = (t->first[len] + i) << (XPRESS_FAST_BITS - len);
            for (uint32_t j = 0; j < span; j++)
                t->fast[start + j] = (uint16_t)(sym << 4 | (uint32_t)len);
        }
    }
    return true;
}

static int decode_symbol(const XpressTable *t, XpressInput *x)
{
    uint16_t e = t->fast[x->window >> (32 - XPRESS_FAST_BITS)];
    if (e) {
        consume(x, e & 15);
        return e >> 4;
    }
    uint32_t bits = x->window >> (32 - XPRESS_MAX_LEN);
    for (int len = XPRESS_FAST_BITS + 1; len <= XPRESS_MAX_LEN; len++) {
        uint32_t idx = (bits >> (XPRESS_MAX_LEN - len)) - t->first[len];
        if (idx < t->count[len]) {
            consume(x, (uint32_t)len);
            return t->sorted[t->offset[len] + idx];
        }
    }
    return -1;
}

bool mime_xpress_decompress(const void *in_ptr, size_t in_size, void *out_ptr, size_t out_size)
{
    const uint8_t *in = in_ptr;
    uint8_t *out = out_ptr;
    if (in_size < 256 + 4 || out_size > MIME_XPRESS_MAX_CHUNK)
        return false;

    uint8_t lens[XPRESS_SYMS];
    for (int i = 0; i < 256; i++) {
        lens[2 * i] = in[i] & 15;
        lens[2 * i + 1] = in[i] >> 4;
    }
    XpressTable table;
    if (!build_table(&table, lens))
        return false;

    XpressInput x = {.in = in, .size = in_size, .pos = 256};
    x.window = next_u16(&x) << 16;
    x.window |= next_u16(&x);
    x.spare = 16;

    size_t pos = 0;
    while (pos < out_size) {
        int sym = decode_symbol(&table, &x);
        if (sym < 0 || x.pos > in_size + 4)
            return false;
        if (sym < 256) {
            out[pos++] = (uint8_t)sym;
            continue;
        }
        sym -= 256;
        uint32_t length = (uint32_t)sym & 15;
        uint32_t offset_bits = (uint32_t)sym >> 4;
        if (length == 15) {
            if (x.pos >= in_size)
                return false;
            length = in[x.pos++];
            if (length == 255) {
                if (x.pos + 2 > in_size)
                    return false;
                length = (uint32_t)in[x.pos] | (uint32_t)in[x.pos + 1] << 8;
                x.pos += 2;
                if (length == 0) {
                    if (x.pos + 4 > in_size)
                        return false;
                    length = (uint32_t)in[x.pos] | (uint32_t)in[x.pos + 1] << 8 |
                             (uint32_t)in[x.pos + 2] << 16 | (uint32_t)in[x.pos + 3] << 24;
                    x.pos += 4;
                }
                if (length < 15)
                    return false;
                length -= 15;
            }
            length += 15;
        }
        length += XPRESS_MIN_MATCH;
        uint32_t offset = 1u << offset_bits;
        if (offset_bits)
            offset |= x.window >> (32 - offset_bits);
        consume(&x, offset_bits);
        if (offset > pos || length > out_size - pos)
            return false;
        for (uint32_t i = 0; i < length; i++, pos++)
            out[pos] = out[pos - offset];
    }
    return true;
}
