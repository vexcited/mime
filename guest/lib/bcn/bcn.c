#include "bcn.h"

static inline uint32_t rd16(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8;
}

static inline uint32_t rd32(const uint8_t *p)
{
    uint32_t v;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    __builtin_memcpy(&v, p, 4);
#else
    v = rd16(p) | rd16(p + 2) << 16;
#endif
    return v;
}

static inline uint32_t pack(uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{
    return a << 24 | r << 16 | g << 8 | b;
}

unsigned mime_bcn_block_size(unsigned format)
{
    switch (format) {
    case MIME_BCN_BC1:
    case MIME_BCN_BC4U:
    case MIME_BCN_BC4S:
        return 8;
    case MIME_BCN_BC2:
    case MIME_BCN_BC3:
    case MIME_BCN_BC5U:
    case MIME_BCN_BC5S:
    case MIME_BCN_BC6H_UF16:
    case MIME_BCN_BC6H_SF16:
    case MIME_BCN_BC7:
        return 16;
    default:
        return 0;
    }
}

size_t mime_bcn_size(unsigned format, unsigned width, unsigned height)
{
    unsigned bs = mime_bcn_block_size(format);
    if (!bs)
        return 0;
    uint64_t blocks = (uint64_t)((width + 3) / 4) * ((height + 3) / 4);
    uint64_t bytes = blocks * bs;
    if (bytes > (size_t)-1)
        return 0;
    return (size_t)bytes;
}

/* the color part of bc1, with the 4 color mode forced for bc2 and bc3. */
static void bc1_color(const uint8_t *src, uint32_t out[16], bool four_colors, bool opaque)
{
    uint32_t c0 = rd16(src), c1 = rd16(src + 2);
    uint32_t r0 = (c0 >> 11) & 31, g0 = (c0 >> 5) & 63, b0 = c0 & 31;
    uint32_t r1 = (c1 >> 11) & 31, g1 = (c1 >> 5) & 63, b1 = c1 & 31;
    r0 = r0 << 3 | r0 >> 2;
    g0 = g0 << 2 | g0 >> 4;
    b0 = b0 << 3 | b0 >> 2;
    r1 = r1 << 3 | r1 >> 2;
    g1 = g1 << 2 | g1 >> 4;
    b1 = b1 << 3 | b1 >> 2;
    uint32_t pal[4];
    pal[0] = pack(r0, g0, b0, 255);
    pal[1] = pack(r1, g1, b1, 255);
    if (c0 > c1 || four_colors) {
        pal[2] = pack((2 * r0 + r1) * 21846 >> 16, (2 * g0 + g1) * 21846 >> 16, (2 * b0 + b1) * 21846 >> 16, 255);
        pal[3] = pack((r0 + 2 * r1) * 21846 >> 16, (g0 + 2 * g1) * 21846 >> 16, (b0 + 2 * b1) * 21846 >> 16, 255);
    } else {
        pal[2] = pack((r0 + r1) >> 1, (g0 + g1) >> 1, (b0 + b1) >> 1, 255);
        pal[3] = opaque ? 0xff000000u : 0;
    }
    uint32_t bits = rd32(src + 4);
    for (int i = 0; i < 16; i++)
        out[i] = pal[(bits >> (2 * i)) & 3];
}

static void unpremultiply(uint32_t out[16])
{
    for (int i = 0; i < 16; i++) {
        uint32_t p = out[i], a = p >> 24;
        if (a == 0 || a == 255)
            continue;
        uint32_t r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
        r = (r * 255 + a / 2) / a;
        g = (g * 255 + a / 2) / a;
        b = (b * 255 + a / 2) / a;
        out[i] = pack(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b, a);
    }
}

static void premultiply(uint32_t out[16])
{
    for (int i = 0; i < 16; i++) {
        uint32_t p = out[i], a = p >> 24;
        if (a == 255)
            continue;
        uint32_t r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
        r = r * a + 128;
        g = g * a + 128;
        b = b * a + 128;
        out[i] = pack((r + (r >> 8)) >> 8, (g + (g >> 8)) >> 8, (b + (b >> 8)) >> 8, a);
    }
}

void mime_bcn_decode_bc1_block(const uint8_t *src, uint32_t out[16], unsigned flags)
{
    bc1_color(src, out, false, (flags & MIME_BCN_OPAQUE) != 0);
}

void mime_bcn_decode_bc2_block(const uint8_t *src, uint32_t out[16], unsigned flags)
{
    bc1_color(src + 8, out, true, true);
    for (int i = 0; i < 16; i++) {
        uint32_t a = (src[i >> 1] >> ((i & 1) * 4)) & 15;
        out[i] = (out[i] & 0x00ffffffu) | (a * 17) << 24;
    }
    if (flags & MIME_BCN_UNPREMULTIPLY)
        unpremultiply(out);
    else if (flags & MIME_BCN_PREMULTIPLY)
        premultiply(out);
}

/* eight values from two end points, 8 bit unsigned or biased signed. */
static void bc4_palette(const uint8_t *src, uint8_t pal[8], bool is_signed)
{
    unsigned e0 = src[0], e1 = src[1], lo = 0, hi = 255;
    if (is_signed) {
        int s0 = (int8_t)src[0], s1 = (int8_t)src[1];
        e0 = (unsigned)((s0 < -127 ? -127 : s0) + 128);
        e1 = (unsigned)((s1 < -127 ? -127 : s1) + 128);
    }
    pal[0] = (uint8_t)e0;
    pal[1] = (uint8_t)e1;
    if (e0 > e1) {
        for (unsigned i = 1; i <= 6; i++)
            pal[1 + i] = (uint8_t)(((7 - i) * e0 + i * e1) * 9363 >> 16);
    } else {
        for (unsigned i = 1; i <= 4; i++)
            pal[1 + i] = (uint8_t)(((5 - i) * e0 + i * e1) * 13108 >> 16);
        pal[6] = (uint8_t)lo;
        pal[7] = (uint8_t)hi;
    }
}

void mime_bcn_decode_bc4_block(const uint8_t *src, uint8_t out[16], bool is_signed)
{
    uint8_t pal[8];
    bc4_palette(src, pal, is_signed);
    uint64_t bits = (uint64_t)rd32(src + 2) | (uint64_t)rd16(src + 6) << 32;
    for (int i = 0; i < 16; i++)
        out[i] = pal[(bits >> (3 * i)) & 7];
}

void mime_bcn_decode_bc5_block(const uint8_t *src, uint8_t out_r[16], uint8_t out_g[16], bool is_signed)
{
    mime_bcn_decode_bc4_block(src, out_r, is_signed);
    mime_bcn_decode_bc4_block(src + 8, out_g, is_signed);
}

void mime_bcn_decode_bc3_block(const uint8_t *src, uint32_t out[16], unsigned flags)
{
    bc1_color(src + 8, out, true, true);
    uint8_t alpha[16];
    mime_bcn_decode_bc4_block(src, alpha, false);
    for (int i = 0; i < 16; i++)
        out[i] = (out[i] & 0x00ffffffu) | (uint32_t)alpha[i] << 24;
    if (flags & MIME_BCN_UNPREMULTIPLY)
        unpremultiply(out);
    else if (flags & MIME_BCN_PREMULTIPLY)
        premultiply(out);
}

/* decodes one block of any format into 16 pixels. */
static void decode_block(unsigned format, const uint8_t *src, uint32_t px[16], unsigned flags)
{
    switch (format) {
    case MIME_BCN_BC1:
        bc1_color(src, px, false, (flags & MIME_BCN_OPAQUE) != 0);
        break;
    case MIME_BCN_BC2:
        mime_bcn_decode_bc2_block(src, px, flags);
        break;
    case MIME_BCN_BC3:
        mime_bcn_decode_bc3_block(src, px, flags);
        break;
    case MIME_BCN_BC4U:
    case MIME_BCN_BC4S: {
        uint8_t v[16];
        mime_bcn_decode_bc4_block(src, v, format == MIME_BCN_BC4S);
        for (int i = 0; i < 16; i++)
            px[i] = 0xff000000u | v[i] * 0x010101u;
        break;
    }
    case MIME_BCN_BC5U:
    case MIME_BCN_BC5S: {
        uint8_t r[16], g[16];
        mime_bcn_decode_bc5_block(src, r, g, format == MIME_BCN_BC5S);
        /* a channel that is not stored is zero, which is 128 in the biased signed formats. */
        uint32_t blue = format == MIME_BCN_BC5S ? 128 : 0;
        for (int i = 0; i < 16; i++)
            px[i] = pack(r[i], g[i], blue, 255);
        break;
    }
    case MIME_BCN_BC6H_UF16:
    case MIME_BCN_BC6H_SF16: {
        uint16_t half[48];
        mime_bcn_decode_bc6h_block(src, format == MIME_BCN_BC6H_SF16, half);
        for (int i = 0; i < 16; i++)
            px[i] = pack(mime_bcn_half_to_u8(half[i * 3]), mime_bcn_half_to_u8(half[i * 3 + 1]),
                         mime_bcn_half_to_u8(half[i * 3 + 2]), 255);
        break;
    }
    default:
        mime_bcn_decode_bc7_block(src, px, flags);
        break;
    }
}

int mime_bcn_decode(unsigned format, const void *src, size_t src_size, unsigned width, unsigned height, uint8_t *dst,
                    size_t dst_stride, unsigned flags)
{
    unsigned bs = mime_bcn_block_size(format);
    if (!bs || !width || !height || !dst || (!src && src_size))
        return MIME_BCN_ERR_PARAM;
    if (dst_stride < (size_t)width * 4)
        return MIME_BCN_ERR_PARAM;
    size_t need = mime_bcn_size(format, width, height);
    if (!need)
        return MIME_BCN_ERR_UNSUPPORTED;
    if (src_size < need)
        return MIME_BCN_ERR_DATA;

    const uint8_t *s = src;
    unsigned bw = (width + 3) / 4, bh = (height + 3) / 4;
    for (unsigned by = 0; by < bh; by++) {
        uint8_t *row = dst + (size_t)by * 4 * dst_stride;
        unsigned rows = height - by * 4 < 4 ? height - by * 4 : 4;
        for (unsigned bx = 0; bx < bw; bx++, s += bs) {
            uint32_t px[16];
            decode_block(format, s, px, flags);
            unsigned cols = width - bx * 4 < 4 ? width - bx * 4 : 4;
            uint8_t *d = row + (size_t)bx * 16;
            for (unsigned y = 0; y < rows; y++)
                __builtin_memcpy(d + y * dst_stride, px + y * 4, cols * 4);
        }
    }
    return MIME_BCN_OK;
}
