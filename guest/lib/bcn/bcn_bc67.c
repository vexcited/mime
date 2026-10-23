#include "bcn.h"

#include "bcn_tables.inc"

typedef struct BitReader {
    uint64_t lo, hi;
    unsigned pos;
} BitReader;

static inline uint64_t rd64(const uint8_t *p)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint64_t v;
    __builtin_memcpy(&v, p, 8);
    return v;
#else
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--)
        v = v << 8 | p[i];
    return v;
#endif
}

static void br_init(BitReader *br, const uint8_t *src)
{
    br->lo = rd64(src);
    br->hi = rd64(src + 8);
    br->pos = 0;
}

/* n is 0 to 32, least significant bit first. */
static inline uint32_t br_get(BitReader *br, unsigned n)
{
    unsigned pos = br->pos;
    uint64_t v;
    if (pos >= 64)
        v = br->hi >> (pos - 64);
    else if (pos + n <= 64)
        v = br->lo >> pos;
    else
        v = (br->lo >> pos) | (br->hi << (64 - pos));
    br->pos = pos + n;
    return (uint32_t)(v & (((uint64_t)1 << n) - 1));
}

static const uint8_t weights2[4] = {0, 21, 43, 64};
static const uint8_t weights3[8] = {0, 9, 18, 27, 37, 46, 55, 64};
static const uint8_t weights4[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};

static inline unsigned weight(unsigned bits, unsigned index)
{
    return bits == 2 ? weights2[index] : bits == 3 ? weights3[index] : weights4[index];
}

static inline unsigned subset_of(unsigned nsub, unsigned partition, unsigned pixel)
{
    if (nsub == 1)
        return 0;
    return ((nsub == 2 ? bptc_part2[partition] : bptc_part3[partition]) >> (2 * pixel)) & 3;
}

/* anchor pixels of the subsets, the first subset's is always pixel 0. */
static void anchors(unsigned nsub, unsigned partition, unsigned anchor[3])
{
    anchor[0] = 0;
    anchor[1] = anchor[2] = 0xff;
    if (nsub >= 2)
        anchor[1] = nsub == 2 ? bptc_anchor2[partition] : bptc_anchor3a[partition];
    if (nsub == 3)
        anchor[2] = bptc_anchor3b[partition];
}

/* bc7. */

typedef struct Bc7Mode {
    uint8_t subsets, partition_bits, rotation_bits, selector_bits, color_bits, alpha_bits, endpoint_pbits,
        shared_pbits, index_bits, index2_bits;
} Bc7Mode;

static const Bc7Mode bc7_modes[8] = {
    {3, 4, 0, 0, 4, 0, 1, 0, 3, 0}, {2, 6, 0, 0, 6, 0, 0, 1, 3, 0}, {3, 6, 0, 0, 5, 0, 0, 0, 2, 0},
    {2, 6, 0, 0, 7, 0, 1, 0, 2, 0}, {1, 0, 2, 1, 5, 6, 0, 0, 2, 3}, {1, 0, 2, 0, 7, 8, 0, 0, 2, 2},
    {1, 0, 0, 0, 7, 7, 1, 0, 4, 0}, {2, 6, 0, 0, 5, 5, 1, 0, 2, 0},
};

static inline unsigned expand_bits(unsigned v, unsigned prec)
{
    v <<= 8 - prec;
    return v | v >> prec;
}

static inline unsigned lerp(unsigned e0, unsigned e1, unsigned w)
{
    return ((64 - w) * e0 + w * e1 + 32) >> 6;
}

bool mime_bcn_decode_bc7_block(const uint8_t *src, uint32_t out[16], unsigned flags)
{
    if (src[0] == 0) {
        for (int i = 0; i < 16; i++)
            out[i] = 0;
        return false;
    }
    unsigned mode = (unsigned)__builtin_ctz(src[0]);
    const Bc7Mode *m = &bc7_modes[mode];
    BitReader br;
    br_init(&br, src);
    br.pos = mode + 1;

    unsigned partition = br_get(&br, m->partition_bits);
    unsigned rotation = br_get(&br, m->rotation_bits);
    unsigned selector = br_get(&br, m->selector_bits);
    unsigned nep = m->subsets * 2u;
    unsigned ep[6][4];
    for (unsigned c = 0; c < 3; c++)
        for (unsigned e = 0; e < nep; e++)
            ep[e][c] = br_get(&br, m->color_bits);
    for (unsigned e = 0; e < nep; e++)
        ep[e][3] = m->alpha_bits ? br_get(&br, m->alpha_bits) : 0;

    unsigned cprec = m->color_bits, aprec = m->alpha_bits;
    if (m->endpoint_pbits) {
        for (unsigned e = 0; e < nep; e++) {
            unsigned p = br_get(&br, 1);
            for (unsigned c = 0; c < 3; c++)
                ep[e][c] = ep[e][c] << 1 | p;
            if (m->alpha_bits)
                ep[e][3] = ep[e][3] << 1 | p;
        }
        cprec++;
        if (m->alpha_bits)
            aprec++;
    } else if (m->shared_pbits) {
        for (unsigned s = 0; s < m->subsets; s++) {
            unsigned p = br_get(&br, 1);
            for (unsigned e = 2 * s; e < 2 * s + 2; e++)
                for (unsigned c = 0; c < 3; c++)
                    ep[e][c] = ep[e][c] << 1 | p;
        }
        cprec++;
    }
    for (unsigned e = 0; e < nep; e++) {
        for (unsigned c = 0; c < 3; c++)
            ep[e][c] = expand_bits(ep[e][c], cprec);
        ep[e][3] = m->alpha_bits ? expand_bits(ep[e][3], aprec) : 255;
    }

    unsigned anchor[3];
    anchors(m->subsets, partition, anchor);
    unsigned idx[16], idx2[16];
    for (unsigned i = 0; i < 16; i++) {
        bool is_anchor = i == anchor[0] || i == anchor[1] || i == anchor[2];
        idx[i] = br_get(&br, m->index_bits - (is_anchor ? 1 : 0));
    }
    if (m->index2_bits) {
        for (unsigned i = 0; i < 16; i++) {
            bool is_anchor = i == anchor[0] || i == anchor[1] || i == anchor[2];
            idx2[i] = br_get(&br, m->index2_bits - (is_anchor ? 1 : 0));
        }
    }

    for (unsigned i = 0; i < 16; i++) {
        unsigned s = subset_of(m->subsets, partition, i);
        const unsigned *e0 = ep[2 * s], *e1 = ep[2 * s + 1];
        unsigned color_bits = m->index_bits, alpha_bits = m->index_bits;
        unsigned ci = idx[i], ai = idx[i];
        if (m->index2_bits) {
            if (selector) {
                color_bits = m->index2_bits;
                ci = idx2[i];
                alpha_bits = m->index_bits;
                ai = idx[i];
            } else {
                color_bits = m->index_bits;
                ci = idx[i];
                alpha_bits = m->index2_bits;
                ai = idx2[i];
            }
        }
        unsigned cw = weight(color_bits, ci), aw = weight(alpha_bits, ai);
        unsigned px[4] = {lerp(e0[0], e1[0], cw), lerp(e0[1], e1[1], cw), lerp(e0[2], e1[2], cw),
                          lerp(e0[3], e1[3], aw)};
        if (rotation) {
            unsigned t = px[3];
            px[3] = px[rotation - 1];
            px[rotation - 1] = t;
        }
        out[i] = px[3] << 24 | px[0] << 16 | px[1] << 8 | px[2];
    }
    if (flags & MIME_BCN_PREMULTIPLY) {
        for (int i = 0; i < 16; i++) {
            uint32_t p = out[i], a = p >> 24;
            if (a == 255)
                continue;
            uint32_t r = ((p >> 16) & 255) * a + 128, g = ((p >> 8) & 255) * a + 128, b = (p & 255) * a + 128;
            out[i] = a << 24 | ((r + (r >> 8)) >> 8) << 16 | ((g + (g >> 8)) >> 8) << 8 | ((b + (b >> 8)) >> 8);
        }
    }
    return true;
}

/* bc6h. */

static inline int32_t sext(int32_t v, unsigned bits)
{
    uint32_t m = 1u << (bits - 1);
    return (int32_t)(((uint32_t)v ^ m) - m);
}

static int32_t unquantize(int32_t x, unsigned bits, bool is_signed)
{
    if (!is_signed) {
        if (bits >= 15)
            return x;
        if (x == 0)
            return 0;
        if (x == (1 << bits) - 1)
            return 0xffff;
        return (int32_t)((((uint32_t)x << 15) + 0x4000) >> (bits - 1));
    }
    if (bits >= 16)
        return x;
    bool neg = x < 0;
    if (neg)
        x = -x;
    int32_t unq;
    if (x == 0)
        unq = 0;
    else if (x >= (1 << (bits - 1)) - 1)
        unq = 0x7fff;
    else
        unq = (int32_t)((((uint32_t)x << 15) + 0x4000) >> (bits - 1));
    return neg ? -unq : unq;
}

bool mime_bcn_decode_bc6h_block(const uint8_t *src, bool is_signed, uint16_t out_rgb[48])
{
    unsigned b0 = src[0];
    unsigned mode = (b0 & 3) < 2 ? (b0 & 3) : (b0 & 31);
    const Bc6Mode *m = NULL;
    for (unsigned i = 0; i < 14; i++)
        if (bc6_modes[i].mode == mode)
            m = &bc6_modes[i];
    if (!m) {
        for (unsigned i = 0; i < 48; i++)
            out_rgb[i] = 0;
        return false;
    }

    BitReader br;
    br_init(&br, src);
    uint32_t v[13] = {0};
    for (unsigned f = 0; f < m->nfields; f++) {
        const Bc6Field *fl = &m->fields[f];
        uint32_t bits = br_get(&br, fl->count);
        if (fl->var == 12)
            continue;
        for (unsigned i = 0; i < fl->count; i++)
            v[fl->var] |= ((bits >> i) & 1u) << (fl->first + fl->dir * (int)i);
    }
    unsigned partition = br_get(&br, m->partition_bits);
    unsigned nsub = m->partition_bits ? 2 : 1;

    int32_t e[4][3];
    unsigned epb = m->endpoint_bits;
    uint32_t mask = epb >= 32 ? 0xffffffffu : (1u << epb) - 1;
    for (unsigned ch = 0; ch < 3; ch++) {
        int32_t base = (int32_t)v[ch];
        if (is_signed)
            base = sext(base, epb);
        e[0][ch] = base;
        for (unsigned p = 1; p < nsub * 2; p++) {
            int32_t x = (int32_t)v[p * 3 + ch];
            if (m->transformed) {
                x = sext(x, m->delta[ch]);
                x = (int32_t)(((uint32_t)base + (uint32_t)x) & mask);
                if (is_signed)
                    x = sext(x, epb);
            } else if (is_signed) {
                x = sext(x, epb);
            }
            e[p][ch] = x;
        }
    }
    for (unsigned p = 0; p < nsub * 2; p++)
        for (unsigned ch = 0; ch < 3; ch++)
            e[p][ch] = unquantize(e[p][ch], epb, is_signed);

    unsigned index_bits = nsub == 2 ? 3 : 4;
    unsigned anchor1 = nsub == 2 ? bptc_anchor2[partition] : 0xff;
    for (unsigned i = 0; i < 16; i++) {
        unsigned s = nsub == 2 ? ((bptc_part2[partition] >> (2 * i)) & 3) : 0;
        bool is_anchor = i == 0 || i == anchor1;
        unsigned idx = br_get(&br, index_bits - (is_anchor ? 1 : 0));
        unsigned w = weight(index_bits, idx);
        for (unsigned ch = 0; ch < 3; ch++) {
            int32_t val = ((int32_t)(64 - w) * e[2 * s][ch] + (int32_t)w * e[2 * s + 1][ch] + 32) >> 6;
            uint32_t h;
            if (!is_signed) {
                h = ((uint32_t)val * 31) >> 6;
            } else if (val < 0) {
                h = ((((uint32_t)-val) * 31) >> 5) | 0x8000u;
            } else {
                h = ((uint32_t)val * 31) >> 5;
            }
            out_rgb[i * 3 + ch] = (uint16_t)h;
        }
    }
    return true;
}

uint8_t mime_bcn_half_to_u8(uint16_t h)
{
    if (h & 0x8000)
        return 0;
    unsigned e = (h >> 10) & 31, m = h & 1023;
    if (e == 31)
        return m ? 0 : 255;
    if (e >= 15)
        return 255;
    if (e == 0)
        return 0;
    unsigned sig = 1024 + m, shift = 25 - e;
    return (uint8_t)((sig * 255 + (1u << (shift - 1))) >> shift);
}

int mime_bcn_decode_bc6h_half(const void *src, size_t src_size, unsigned width, unsigned height, bool is_signed,
                              uint16_t *dst, size_t dst_stride)
{
    if (!width || !height || !dst || (!src && src_size) || dst_stride < (size_t)width * 8)
        return MIME_BCN_ERR_PARAM;
    unsigned fmt = is_signed ? MIME_BCN_BC6H_SF16 : MIME_BCN_BC6H_UF16;
    size_t need = mime_bcn_size(fmt, width, height);
    if (!need)
        return MIME_BCN_ERR_UNSUPPORTED;
    if (src_size < need)
        return MIME_BCN_ERR_DATA;
    const uint8_t *s = src;
    unsigned bw = (width + 3) / 4, bh = (height + 3) / 4;
    for (unsigned by = 0; by < bh; by++) {
        for (unsigned bx = 0; bx < bw; bx++, s += 16) {
            uint16_t px[48];
            mime_bcn_decode_bc6h_block(s, is_signed, px);
            for (unsigned y = 0; y < 4 && by * 4 + y < height; y++) {
                uint8_t *row = (uint8_t *)dst + (size_t)(by * 4 + y) * dst_stride;
                for (unsigned x = 0; x < 4 && bx * 4 + x < width; x++) {
                    uint16_t *d = (uint16_t *)(row + (size_t)(bx * 4 + x) * 8);
                    const uint16_t *p = px + (y * 4 + x) * 3;
                    d[0] = p[0];
                    d[1] = p[1];
                    d[2] = p[2];
                    d[3] = 0x3c00;
                }
            }
        }
    }
    return MIME_BCN_OK;
}
