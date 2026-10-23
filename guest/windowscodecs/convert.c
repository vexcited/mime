/*
 * pixel conversion uses three canonical forms: straight BGRA32 for 8-bit channels, RGBA64 with 16-bit channels
 * for other integers, and linear RGBA128 floats for float and fixed-point formats. reductions truncate;
 * expansions repeat bits, as on Windows.
 */
#include "wic_private.h"
#include "convert.h"

#ifdef __SSE2__
#include <emmintrin.h>
#endif

#include "gamma_tables.inc"

enum { CT_U8, CT_U16, CT_F32, CT_F16, CT_FIX13, CT_FIX16, CT_555, CT_565, CT_5551, CT_101010, CT_1010102, CT_1010102XR };
enum { M_RGB, M_GRAY, M_CMYK, M_INDEXED, M_ALPHA };

typedef struct FormatDesc {
    const GUID *guid;
    BYTE bits;
    BYTE ctype;
    BYTE model;
    BYTE nchan;    /* stored channels of an unpacked pixel, or the bits of an indexed one. */
    signed char r; /* channel positions; gray uses r, CMYK uses r, g, b, a. */
    signed char g;
    signed char b;
    signed char a;
    BYTE alpha;
    BYTE premul;
} FormatDesc;

#define F(guid, bits, ct, model, nchan, r, g, b, a, alpha, premul) {&GUID_WICPixelFormat##guid, bits, ct, model, nchan, r, g, b, a, alpha, premul}

static const FormatDesc g_formats[] = {
    F(1bppIndexed, 1, CT_U8, M_INDEXED, 1, 0, -1, -1, -1, 0, 0),
    F(2bppIndexed, 2, CT_U8, M_INDEXED, 2, 0, -1, -1, -1, 0, 0),
    F(4bppIndexed, 4, CT_U8, M_INDEXED, 4, 0, -1, -1, -1, 0, 0),
    F(8bppIndexed, 8, CT_U8, M_INDEXED, 8, 0, -1, -1, -1, 0, 0),
    F(BlackWhite, 1, CT_U8, M_GRAY, 1, 0, -1, -1, -1, 0, 0),
    F(2bppGray, 2, CT_U8, M_GRAY, 2, 0, -1, -1, -1, 0, 0),
    F(4bppGray, 4, CT_U8, M_GRAY, 4, 0, -1, -1, -1, 0, 0),
    F(8bppGray, 8, CT_U8, M_GRAY, 1, 0, -1, -1, -1, 0, 0),
    F(16bppGray, 16, CT_U16, M_GRAY, 1, 0, -1, -1, -1, 0, 0),
    F(16bppGrayFixedPoint, 16, CT_FIX13, M_GRAY, 1, 0, -1, -1, -1, 0, 0),
    F(16bppGrayHalf, 16, CT_F16, M_GRAY, 1, 0, -1, -1, -1, 0, 0),
    F(32bppGrayFloat, 32, CT_F32, M_GRAY, 1, 0, -1, -1, -1, 0, 0),
    F(32bppGrayFixedPoint, 32, CT_FIX16, M_GRAY, 1, 0, -1, -1, -1, 0, 0),
    F(8bppAlpha, 8, CT_U8, M_ALPHA, 1, -1, -1, -1, 0, 1, 0),
    F(16bppBGR555, 16, CT_555, M_RGB, 3, 2, 1, 0, -1, 0, 0),
    F(16bppBGR565, 16, CT_565, M_RGB, 3, 2, 1, 0, -1, 0, 0),
    F(16bppBGRA5551, 16, CT_5551, M_RGB, 4, 2, 1, 0, 3, 1, 0),
    F(24bppBGR, 24, CT_U8, M_RGB, 3, 2, 1, 0, -1, 0, 0),
    F(24bppRGB, 24, CT_U8, M_RGB, 3, 0, 1, 2, -1, 0, 0),
    F(32bppBGR, 32, CT_U8, M_RGB, 4, 2, 1, 0, -1, 0, 0),
    F(32bppRGB, 32, CT_U8, M_RGB, 4, 0, 1, 2, -1, 0, 0),
    F(32bppBGRA, 32, CT_U8, M_RGB, 4, 2, 1, 0, 3, 1, 0),
    F(32bppPBGRA, 32, CT_U8, M_RGB, 4, 2, 1, 0, 3, 1, 1),
    F(32bppRGBA, 32, CT_U8, M_RGB, 4, 0, 1, 2, 3, 1, 0),
    F(32bppPRGBA, 32, CT_U8, M_RGB, 4, 0, 1, 2, 3, 1, 1),
    F(48bppRGB, 48, CT_U16, M_RGB, 3, 0, 1, 2, -1, 0, 0),
    F(48bppBGR, 48, CT_U16, M_RGB, 3, 2, 1, 0, -1, 0, 0),
    F(64bppRGB, 64, CT_U16, M_RGB, 4, 0, 1, 2, -1, 0, 0),
    F(64bppRGBA, 64, CT_U16, M_RGB, 4, 0, 1, 2, 3, 1, 0),
    F(64bppPRGBA, 64, CT_U16, M_RGB, 4, 0, 1, 2, 3, 1, 1),
    F(64bppBGRA, 64, CT_U16, M_RGB, 4, 2, 1, 0, 3, 1, 0),
    F(64bppPBGRA, 64, CT_U16, M_RGB, 4, 2, 1, 0, 3, 1, 1),
    F(32bppCMYK, 32, CT_U8, M_CMYK, 4, 0, 1, 2, 3, 0, 0),
    F(64bppCMYK, 64, CT_U16, M_CMYK, 4, 0, 1, 2, 3, 0, 0),
    F(32bppBGR101010, 32, CT_101010, M_RGB, 3, 2, 1, 0, -1, 0, 0),
    F(32bppRGBA1010102, 32, CT_1010102, M_RGB, 4, 0, 1, 2, 3, 1, 0),
    F(32bppRGBA1010102XR, 32, CT_1010102XR, M_RGB, 4, 0, 1, 2, 3, 1, 0),
    F(48bppRGBFixedPoint, 48, CT_FIX13, M_RGB, 3, 0, 1, 2, -1, 0, 0),
    F(48bppBGRFixedPoint, 48, CT_FIX13, M_RGB, 3, 2, 1, 0, -1, 0, 0),
    F(64bppRGBFixedPoint, 64, CT_FIX13, M_RGB, 4, 0, 1, 2, -1, 0, 0),
    F(64bppRGBAFixedPoint, 64, CT_FIX13, M_RGB, 4, 0, 1, 2, 3, 1, 0),
    F(64bppBGRAFixedPoint, 64, CT_FIX13, M_RGB, 4, 2, 1, 0, 3, 1, 0),
    F(96bppRGBFixedPoint, 96, CT_FIX16, M_RGB, 3, 0, 1, 2, -1, 0, 0),
    F(128bppRGBFixedPoint, 128, CT_FIX16, M_RGB, 4, 0, 1, 2, -1, 0, 0),
    F(128bppRGBAFixedPoint, 128, CT_FIX16, M_RGB, 4, 0, 1, 2, 3, 1, 0),
    F(48bppRGBHalf, 48, CT_F16, M_RGB, 3, 0, 1, 2, -1, 0, 0),
    F(64bppRGBHalf, 64, CT_F16, M_RGB, 4, 0, 1, 2, -1, 0, 0),
    F(64bppRGBAHalf, 64, CT_F16, M_RGB, 4, 0, 1, 2, 3, 1, 0),
    F(64bppPRGBAHalf, 64, CT_F16, M_RGB, 4, 0, 1, 2, 3, 1, 1),
    F(96bppRGBFloat, 96, CT_F32, M_RGB, 3, 0, 1, 2, -1, 0, 0),
    F(128bppRGBFloat, 128, CT_F32, M_RGB, 4, 0, 1, 2, -1, 0, 0),
    F(128bppRGBAFloat, 128, CT_F32, M_RGB, 4, 0, 1, 2, 3, 1, 0),
    F(128bppPRGBAFloat, 128, CT_F32, M_RGB, 4, 0, 1, 2, 3, 1, 1),
};

#undef F

static const FormatDesc *find_desc(const GUID *guid)
{
    for (UINT i = 0; i < WIC_ARRAY_SIZE(g_formats); i++) {
        if (IsEqualGUID(g_formats[i].guid, guid))
            return &g_formats[i];
    }
    return nullptr;
}

BOOL WicCanConvert(const GUID *from, const GUID *to)
{
    const FormatDesc *a = find_desc(from), *b = find_desc(to);
    if (!a || !b)
        return FALSE;
    /* alpha-only destinations require a source with alpha. */
    if (b->model == M_ALPHA && !a->alpha)
        return FALSE;
    if (a->model == M_ALPHA && b->model != M_ALPHA && !b->alpha && b->model != M_GRAY && b->model != M_RGB)
        return FALSE;
    return TRUE;
}

/* numbers. */

static float half_to_float(UINT16 h)
{
    UINT sign = (UINT)(h >> 15) << 31, exp = (h >> 10) & 31, mant = h & 1023;
    UINT bits;
    if (!exp) {
        if (!mant) {
            bits = sign;
        } else {
            exp = 1;
            while (!(mant & 1024)) {
                mant <<= 1;
                exp--;
            }
            mant &= 1023;
            bits = sign | ((exp + 112) << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        bits = sign | 0x7f800000 | (mant << 13);
    } else {
        bits = sign | ((exp + 112) << 23) | (mant << 13);
    }
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

static UINT16 float_to_half(float f)
{
    UINT bits;
    memcpy(&bits, &f, 4);
    UINT sign = (bits >> 16) & 0x8000, exp = (bits >> 23) & 255, mant = bits & 0x7fffff;
    if (exp == 255)
        return (UINT16)(sign | 0x7c00 | (mant ? 0x200 : 0));
    int e = (int)exp - 127 + 15;
    if (e >= 31)
        return (UINT16)(sign | 0x7c00);
    if (e <= 0) {
        if (e < -10)
            return (UINT16)sign;
        mant |= 0x800000;
        UINT shift = (UINT)(14 - e);
        UINT half = mant >> shift;
        if ((mant >> (shift - 1)) & 1)
            half++;
        return (UINT16)(sign | half);
    }
    UINT half = sign | ((UINT)e << 10) | (mant >> 13);
    if (mant & 0x1000)
        half++;
    return (UINT16)half;
}

static float to_linear8(BYTE v)
{
    float f;
    UINT bits = g_to_linear_bits[v];
    memcpy(&f, &bits, 4);
    return f * (1.0f / 255.0f);
}

static float to_linear16(UINT v)
{
    UINT hi = v >> 8, lo = v & 255;
    float a = to_linear8((BYTE)hi);
    if (hi == 255)
        return a;
    float b = to_linear8((BYTE)(hi + 1));
    return a + (b - a) * ((float)lo * (1.0f / 256.0f));
}

static BYTE linear_to_srgb8(float v)
{
    if (!(v > 0.0f))
        return 0;
    if (v >= 1.0f)
        return 255;
    float x = v * 3354.0f;
    UINT i = (UINT)x;
    if (x - (float)i >= 0.5f)
        i++;
    return g_to_srgb[i];
}

static UINT linear_to_srgb16(float v)
{
    if (!(v > 0.0f))
        return 0;
    if (v >= 1.0f)
        return 65535;
    /* the table has 8-bit steps; search between adjacent entries for the low byte. */
    BYTE hi = linear_to_srgb8(v);
    float lo_lin = to_linear8(hi), hi_lin = hi < 255 ? to_linear8((BYTE)(hi + 1)) : 1.0f;
    float lo_prev = hi > 0 ? to_linear8((BYTE)(hi - 1)) : 0.0f;
    float t;
    if (v >= lo_lin) {
        t = hi_lin > lo_lin ? (v - lo_lin) / (hi_lin - lo_lin) : 0.0f;
        return (UINT)hi * 257 + (UINT)(t * 128.0f);
    }
    t = lo_lin > lo_prev ? (lo_lin - v) / (lo_lin - lo_prev) : 0.0f;
    UINT base = (UINT)hi * 257;
    UINT delta = (UINT)(t * 128.0f);
    return base > delta ? base - delta : 0;
}

static inline BYTE div255(UINT t)
{
    t += 128;
    return (BYTE)((t + (t >> 8)) >> 8);
}

static inline UINT16 div65535(unsigned long long t)
{
    t += 32768;
    return (UINT16)((t + (t >> 16)) >> 16);
}

/* linear luminance using Rec. 709 weights, summed green first. */
#define GRAY_R 0.2126f
#define GRAY_G 0.7152f
#define GRAY_B 0.0722f

static float luminance(float red, float green, float blue)
{
    float sum = green * GRAY_G;
    sum += red * GRAY_R;
    sum += blue * GRAY_B;
    return sum;
}

static BYTE gray8_of(BYTE r, BYTE g, BYTE b)
{
    return linear_to_srgb8(luminance(to_linear8(r), to_linear8(g), to_linear8(b)));
}

static UINT gray16_of(UINT r, UINT g, UINT b)
{
    return linear_to_srgb16(luminance(to_linear16(r), to_linear16(g), to_linear16(b)));
}

/* canonical forms. */

typedef enum { CANON_8, CANON_16, CANON_F } Canon;

static Canon canon_of(const FormatDesc *d)
{
    switch (d->ctype) {
    case CT_U8:
    case CT_555:
    case CT_565:
    case CT_5551:
        return CANON_8;
    case CT_U16:
    case CT_101010:
    case CT_1010102:
        return CANON_16;
    default:
        return CANON_F;
    }
}

static inline UINT bit_get(const BYTE *row, UINT x, UINT bits)
{
    UINT per = 8 / bits, byte = row[x / per];
    UINT shift = 8 - bits - (x % per) * bits;
    return (byte >> shift) & ((1u << bits) - 1);
}

static inline void bit_put(BYTE *row, UINT x, UINT bits, UINT value)
{
    UINT per = 8 / bits;
    UINT shift = 8 - bits - (x % per) * bits;
    BYTE mask = (BYTE)(((1u << bits) - 1) << shift);
    row[x / per] = (BYTE)((row[x / per] & ~mask) | ((value << shift) & mask));
}

/* read a channel as a 16-bit-range value or float. */
static void read8(const FormatDesc *d, const BYTE *src, UINT x0, BYTE *out, UINT count, const WICColor *palette,
                  UINT palette_count)
{
    switch (d->model) {
    case M_INDEXED:
        for (UINT i = 0; i < count; i++) {
            UINT index = bit_get(src, x0 + i, d->bits);
            WICColor c = index < palette_count ? palette[index] : 0xff000000;
            memcpy(out + i * 4, &c, 4);
        }
        return;
    case M_GRAY:
        if (d->bits < 8) {
            UINT max = (1u << d->bits) - 1;
            for (UINT i = 0; i < count; i++) {
                UINT v = bit_get(src, x0 + i, d->bits);
                BYTE g = (BYTE)(v * 255 / max);
                out[i * 4] = out[i * 4 + 1] = out[i * 4 + 2] = g;
                out[i * 4 + 3] = 255;
            }
        } else {
            for (UINT i = 0; i < count; i++) {
                BYTE g = src[x0 + i];
                out[i * 4] = out[i * 4 + 1] = out[i * 4 + 2] = g;
                out[i * 4 + 3] = 255;
            }
        }
        return;
    case M_ALPHA:
        for (UINT i = 0; i < count; i++) {
            out[i * 4] = out[i * 4 + 1] = out[i * 4 + 2] = 0;
            out[i * 4 + 3] = src[x0 + i];
        }
        return;
    default:
        break;
    }
    if (d->model == M_CMYK) {
        const BYTE *p = src + (SIZE_T)x0 * 4;
        for (UINT i = 0; i < count; i++, p += 4) {
            UINT k = 255 - p[3];
            out[i * 4 + 2] = div255((255 - p[0]) * k);
            out[i * 4 + 1] = div255((255 - p[1]) * k);
            out[i * 4] = div255((255 - p[2]) * k);
            out[i * 4 + 3] = 255;
        }
        return;
    }
    switch (d->ctype) {
    case CT_555:
    case CT_565:
    case CT_5551: {
        const UINT16 *p = (const UINT16 *)src + x0;
        for (UINT i = 0; i < count; i++) {
            UINT v = p[i], r, g, b, a = 255;
            if (d->ctype == CT_565) {
                r = (v >> 11) & 31;
                g = (v >> 5) & 63;
                b = v & 31;
                g = (g << 2) | (g >> 4);
            } else {
                r = (v >> 10) & 31;
                g = (v >> 5) & 31;
                b = v & 31;
                g = (g << 3) | (g >> 2);
                if (d->ctype == CT_5551)
                    a = (v & 0x8000) ? 255 : 0;
            }
            out[i * 4 + 2] = (BYTE)((r << 3) | (r >> 2));
            out[i * 4 + 1] = (BYTE)g;
            out[i * 4] = (BYTE)((b << 3) | (b >> 2));
            out[i * 4 + 3] = (BYTE)a;
        }
        return;
    }
    default:
        break;
    }
    UINT n = d->nchan;
    const BYTE *p = src + (SIZE_T)x0 * n;
    BOOL premul = d->premul;
    for (UINT i = 0; i < count; i++, p += n) {
        BYTE a = d->a >= 0 ? p[d->a] : 255;
        BYTE r = p[d->r], g = p[d->g], b = p[d->b];
        if (premul && a != 255) {
            if (!a) {
                r = g = b = 0;
            } else {
                r = WicStraightChannel(r, a);
                g = WicStraightChannel(g, a);
                b = WicStraightChannel(b, a);
            }
        }
        out[i * 4] = b;
        out[i * 4 + 1] = g;
        out[i * 4 + 2] = r;
        out[i * 4 + 3] = a;
    }
}

static void write8(const FormatDesc *d, BYTE *dst, UINT x0, const BYTE *in, UINT count)
{
    switch (d->model) {
    case M_GRAY:
        if (d->bits < 8) {
            for (UINT i = 0; i < count; i++) {
                BYTE g = gray8_of(in[i * 4 + 2], in[i * 4 + 1], in[i * 4]);
                bit_put(dst, x0 + i, d->bits, g >> (8 - d->bits));
            }
        } else {
            for (UINT i = 0; i < count; i++)
                dst[x0 + i] = gray8_of(in[i * 4 + 2], in[i * 4 + 1], in[i * 4]);
        }
        return;
    case M_ALPHA:
        for (UINT i = 0; i < count; i++)
            dst[x0 + i] = in[i * 4 + 3];
        return;
    case M_CMYK: {
        BYTE *p = dst + (SIZE_T)x0 * 4;
        for (UINT i = 0; i < count; i++, p += 4) {
            BYTE r = in[i * 4 + 2], g = in[i * 4 + 1], b = in[i * 4];
            BYTE max = r > g ? (r > b ? r : b) : (g > b ? g : b);
            BYTE k = (BYTE)(255 - max);
            if (max == 0) {
                p[0] = p[1] = p[2] = 0;
            } else {
                p[0] = (BYTE)((max - r) * 255 / max);
                p[1] = (BYTE)((max - g) * 255 / max);
                p[2] = (BYTE)((max - b) * 255 / max);
            }
            p[3] = k;
        }
        return;
    }
    default:
        break;
    }
    switch (d->ctype) {
    case CT_555:
    case CT_565:
    case CT_5551: {
        UINT16 *p = (UINT16 *)dst + x0;
        for (UINT i = 0; i < count; i++) {
            UINT b = in[i * 4], g = in[i * 4 + 1], r = in[i * 4 + 2], a = in[i * 4 + 3];
            if (d->ctype == CT_565)
                p[i] = (UINT16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            else
                p[i] = (UINT16)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3) |
                                (d->ctype == CT_5551 && a >= 128 ? 0x8000 : 0));
        }
        return;
    }
    default:
        break;
    }
    UINT n = d->nchan;
    BYTE *p = dst + (SIZE_T)x0 * n;
    BOOL premul = d->premul;
    for (UINT i = 0; i < count; i++, p += n) {
        BYTE b = in[i * 4], g = in[i * 4 + 1], r = in[i * 4 + 2], a = in[i * 4 + 3];
        if (premul && a != 255) {
            r = div255(r * (UINT)a);
            g = div255(g * (UINT)a);
            b = div255(b * (UINT)a);
        }
        p[d->r] = r;
        p[d->g] = g;
        p[d->b] = b;
        if (d->a >= 0)
            p[d->a] = a;
        else if (n == 4)
            p[3 - 0] = p[3] = 255;
    }
}

/* the 16 bit canonical form: R, G, B, A of 16 bits, straight. */
static void read16(const FormatDesc *d, const BYTE *src, UINT x0, UINT16 *out, UINT count)
{
    if (d->model == M_GRAY) {
        const UINT16 *p = (const UINT16 *)src + x0;
        for (UINT i = 0; i < count; i++) {
            out[i * 4] = out[i * 4 + 1] = out[i * 4 + 2] = p[i];
            out[i * 4 + 3] = 65535;
        }
        return;
    }
    if (d->model == M_CMYK) {
        const UINT16 *p = (const UINT16 *)src + (SIZE_T)x0 * 4;
        for (UINT i = 0; i < count; i++, p += 4) {
            UINT64 k = 65535 - p[3];
            out[i * 4] = div65535((65535 - p[0]) * k);
            out[i * 4 + 1] = div65535((65535 - p[1]) * k);
            out[i * 4 + 2] = div65535((65535 - p[2]) * k);
            out[i * 4 + 3] = 65535;
        }
        return;
    }
    if (d->ctype == CT_101010 || d->ctype == CT_1010102) {
        const UINT *p = (const UINT *)src + x0;
        for (UINT i = 0; i < count; i++) {
            UINT v = p[i];
            UINT r, g, b;
            if (d->ctype == CT_101010) {
                b = v & 1023;
                g = (v >> 10) & 1023;
                r = (v >> 20) & 1023;
            } else {
                r = v & 1023;
                g = (v >> 10) & 1023;
                b = (v >> 20) & 1023;
            }
            out[i * 4] = (UINT16)((r << 6) | (r >> 4));
            out[i * 4 + 1] = (UINT16)((g << 6) | (g >> 4));
            out[i * 4 + 2] = (UINT16)((b << 6) | (b >> 4));
            UINT a = d->ctype == CT_1010102 ? (v >> 30) & 3 : 3;
            out[i * 4 + 3] = (UINT16)(a * 21845);
        }
        return;
    }
    UINT n = d->nchan;
    const UINT16 *p = (const UINT16 *)src + (SIZE_T)x0 * n;
    for (UINT i = 0; i < count; i++, p += n) {
        UINT a = d->a >= 0 ? p[d->a] : 65535;
        UINT r = p[d->r], g = p[d->g], b = p[d->b];
        if (d->premul && a != 65535) {
            if (!a) {
                r = g = b = 0;
            } else {
                UINT64 m = ((UINT64)65535 << 16) / a;
                UINT64 rr = (r * m) >> 16, gg = (g * m) >> 16, bb = (b * m) >> 16;
                r = rr > 65535 ? 65535 : (UINT)rr;
                g = gg > 65535 ? 65535 : (UINT)gg;
                b = bb > 65535 ? 65535 : (UINT)bb;
            }
        }
        out[i * 4] = (UINT16)r;
        out[i * 4 + 1] = (UINT16)g;
        out[i * 4 + 2] = (UINT16)b;
        out[i * 4 + 3] = (UINT16)a;
    }
}

static void write16(const FormatDesc *d, BYTE *dst, UINT x0, const UINT16 *in, UINT count)
{
    if (d->model == M_GRAY) {
        UINT16 *p = (UINT16 *)dst + x0;
        for (UINT i = 0; i < count; i++)
            p[i] = (UINT16)gray16_of(in[i * 4], in[i * 4 + 1], in[i * 4 + 2]);
        return;
    }
    if (d->model == M_CMYK) {
        UINT16 *p = (UINT16 *)dst + (SIZE_T)x0 * 4;
        for (UINT i = 0; i < count; i++, p += 4) {
            UINT r = in[i * 4], g = in[i * 4 + 1], b = in[i * 4 + 2];
            UINT max = r > g ? (r > b ? r : b) : (g > b ? g : b);
            p[3] = (UINT16)(65535 - max);
            if (!max) {
                p[0] = p[1] = p[2] = 0;
            } else {
                p[0] = (UINT16)((max - r) * 65535ull / max);
                p[1] = (UINT16)((max - g) * 65535ull / max);
                p[2] = (UINT16)((max - b) * 65535ull / max);
            }
        }
        return;
    }
    if (d->ctype == CT_101010 || d->ctype == CT_1010102) {
        UINT *p = (UINT *)dst + x0;
        for (UINT i = 0; i < count; i++) {
            UINT r = in[i * 4] >> 6, g = in[i * 4 + 1] >> 6, b = in[i * 4 + 2] >> 6;
            if (d->ctype == CT_101010)
                p[i] = b | (g << 10) | (r << 20);
            else
                p[i] = r | (g << 10) | (b << 20) | ((UINT)(in[i * 4 + 3] >> 14) << 30);
        }
        return;
    }
    UINT n = d->nchan;
    UINT16 *p = (UINT16 *)dst + (SIZE_T)x0 * n;
    for (UINT i = 0; i < count; i++, p += n) {
        UINT r = in[i * 4], g = in[i * 4 + 1], b = in[i * 4 + 2], a = in[i * 4 + 3];
        if (d->premul && a != 65535) {
            r = div65535((UINT64)r * a);
            g = div65535((UINT64)g * a);
            b = div65535((UINT64)b * a);
        }
        p[d->r] = (UINT16)r;
        p[d->g] = (UINT16)g;
        p[d->b] = (UINT16)b;
        if (d->a >= 0)
            p[d->a] = (UINT16)a;
        else if (n == 4)
            p[3] = 65535;
    }
}

/* the float canonical form: scRGB, linear light, straight alpha. */
static float channel_to_float(const FormatDesc *d, const BYTE *p, int index)
{
    switch (d->ctype) {
    case CT_F32:
        return ((const float *)p)[index];
    case CT_F16:
        return half_to_float(((const UINT16 *)p)[index]);
    case CT_FIX13:
        return (float)((const INT16 *)p)[index] * (1.0f / 8192.0f);
    case CT_FIX16:
        return (float)((const INT32 *)p)[index] * (1.0f / 65536.0f);
    default:
        return 0.0f;
    }
}

static void float_to_channel(const FormatDesc *d, BYTE *p, int index, float v)
{
    switch (d->ctype) {
    case CT_F32:
        ((float *)p)[index] = v;
        break;
    case CT_F16:
        ((UINT16 *)p)[index] = float_to_half(v);
        break;
    case CT_FIX13: {
        float s = v * 8192.0f;
        s = s > 32767.0f ? 32767.0f : s < -32768.0f ? -32768.0f : s;
        ((INT16 *)p)[index] = (INT16)(int)(s + (s < 0 ? -0.5f : 0.5f));
        break;
    }
    case CT_FIX16: {
        float s = v * 65536.0f;
        s = s > 2147483520.0f ? 2147483520.0f : s < -2147483648.0f ? -2147483648.0f : s;
        ((INT32 *)p)[index] = (INT32)(s + (s < 0 ? -0.5f : 0.5f));
        break;
    }
    default:
        break;
    }
}

static void readf(const FormatDesc *d, const BYTE *src, UINT x0, float *out, UINT count, const WICColor *palette,
                  UINT palette_count)
{
    Canon canon = canon_of(d);
    if (canon == CANON_8) {
        BYTE *tmp = WicAlloc((SIZE_T)count * 4);
        if (!tmp) {
            memset(out, 0, (SIZE_T)count * 16);
            return;
        }
        read8(d, src, x0, tmp, count, palette, palette_count);
        for (UINT i = 0; i < count; i++) {
            out[i * 4] = to_linear8(tmp[i * 4 + 2]);
            out[i * 4 + 1] = to_linear8(tmp[i * 4 + 1]);
            out[i * 4 + 2] = to_linear8(tmp[i * 4]);
            out[i * 4 + 3] = (float)tmp[i * 4 + 3] * (1.0f / 255.0f);
        }
        WicFree(tmp);
        return;
    }
    if (canon == CANON_16) {
        UINT16 *tmp = WicAlloc((SIZE_T)count * 8);
        if (!tmp) {
            memset(out, 0, (SIZE_T)count * 16);
            return;
        }
        read16(d, src, x0, tmp, count);
        for (UINT i = 0; i < count; i++) {
            out[i * 4] = to_linear16(tmp[i * 4]);
            out[i * 4 + 1] = to_linear16(tmp[i * 4 + 1]);
            out[i * 4 + 2] = to_linear16(tmp[i * 4 + 2]);
            out[i * 4 + 3] = (float)tmp[i * 4 + 3] * (1.0f / 65535.0f);
        }
        WicFree(tmp);
        return;
    }
    UINT n = d->model == M_GRAY ? 1 : d->nchan;
    UINT stride_bytes = d->bits / 8;
    const BYTE *p = src + (SIZE_T)x0 * stride_bytes;
    for (UINT i = 0; i < count; i++, p += stride_bytes) {
        float r, g, b, a = 1.0f;
        if (d->model == M_GRAY) {
            r = g = b = channel_to_float(d, p, 0);
        } else {
            r = channel_to_float(d, p, d->r);
            g = channel_to_float(d, p, d->g);
            b = channel_to_float(d, p, d->b);
            if (d->a >= 0)
                a = channel_to_float(d, p, d->a);
        }
        (void)n;
        if (d->premul && a != 1.0f) {
            if (a > 0.0f) {
                r /= a;
                g /= a;
                b /= a;
            } else {
                r = g = b = 0.0f;
            }
        }
        out[i * 4] = r;
        out[i * 4 + 1] = g;
        out[i * 4 + 2] = b;
        out[i * 4 + 3] = a;
    }
}

static void writef(const FormatDesc *d, BYTE *dst, UINT x0, const float *in, UINT count)
{
    Canon canon = canon_of(d);
    if (canon == CANON_8) {
        BYTE *tmp = WicAlloc((SIZE_T)count * 4);
        if (!tmp)
            return;
        for (UINT i = 0; i < count; i++) {
            tmp[i * 4 + 2] = linear_to_srgb8(in[i * 4]);
            tmp[i * 4 + 1] = linear_to_srgb8(in[i * 4 + 1]);
            tmp[i * 4] = linear_to_srgb8(in[i * 4 + 2]);
            float a = in[i * 4 + 3] * 255.0f + 0.5f;
            tmp[i * 4 + 3] = a <= 0 ? 0 : a >= 255 ? 255 : (BYTE)(int)a;
        }
        write8(d, dst, x0, tmp, count);
        WicFree(tmp);
        return;
    }
    if (canon == CANON_16) {
        UINT16 *tmp = WicAlloc((SIZE_T)count * 8);
        if (!tmp)
            return;
        for (UINT i = 0; i < count; i++) {
            tmp[i * 4] = (UINT16)linear_to_srgb16(in[i * 4]);
            tmp[i * 4 + 1] = (UINT16)linear_to_srgb16(in[i * 4 + 1]);
            tmp[i * 4 + 2] = (UINT16)linear_to_srgb16(in[i * 4 + 2]);
            float a = in[i * 4 + 3] * 65535.0f + 0.5f;
            tmp[i * 4 + 3] = a <= 0 ? 0 : a >= 65535 ? 65535 : (UINT16)(int)a;
        }
        write16(d, dst, x0, tmp, count);
        WicFree(tmp);
        return;
    }
    UINT stride_bytes = d->bits / 8;
    BYTE *p = dst + (SIZE_T)x0 * stride_bytes;
    for (UINT i = 0; i < count; i++, p += stride_bytes) {
        float r = in[i * 4], g = in[i * 4 + 1], b = in[i * 4 + 2], a = in[i * 4 + 3];
        if (d->model == M_GRAY) {
            float_to_channel(d, p, 0, r * GRAY_R + g * GRAY_G + b * GRAY_B);
            continue;
        }
        if (d->premul) {
            r *= a;
            g *= a;
            b *= a;
        }
        float_to_channel(d, p, d->r, r);
        float_to_channel(d, p, d->g, g);
        float_to_channel(d, p, d->b, b);
        if (d->a >= 0)
            float_to_channel(d, p, d->a, a);
        else if (d->nchan == 4)
            float_to_channel(d, p, 3, 1.0f);
    }
}

/* fast paths. */

/* common BGRA paths: remove alpha or swap channels. */
static BOOL fast_row(const FormatDesc *s, const FormatDesc *d, const BYTE *src, BYTE *dst, UINT x0, UINT count)
{
    if (s->ctype != CT_U8 || d->ctype != CT_U8 || s->model != M_RGB || d->model != M_RGB)
        return FALSE;
    if (s->premul || d->premul)
        return FALSE;
    UINT sn = s->nchan, dn = d->nchan;
    const BYTE *p = src + (SIZE_T)x0 * sn;
    BYTE *q = dst + (SIZE_T)x0 * dn;
    /* 32 bit with the same channel order, 24 bit with the same order. */
    if (sn == 4 && dn == 4 && s->r == d->r && s->b == d->b && s->a == d->a) {
        memcpy(q, p, (SIZE_T)count * 4);
        if (d->a < 0) {
            for (UINT i = 0; i < count; i++)
                q[i * 4 + 3] = 255;
        }
        return TRUE;
    }
    if (sn == 4 && dn == 4 && s->r == d->b && s->b == d->r && s->a == d->a) {
        UINT i = 0;
#ifdef __SSE2__
        for (; i + 4 <= count; i += 4) {
            __m128i v = _mm_loadu_si128((const __m128i *)(p + i * 4));
            __m128i rb = _mm_and_si128(v, _mm_set1_epi32(0x00ff00ff));
            __m128i ga = _mm_andnot_si128(_mm_set1_epi32(0x00ff00ff), v);
            __m128i swapped = _mm_or_si128(_mm_slli_epi32(rb, 16), _mm_srli_epi32(rb, 16));
            __m128i out = _mm_or_si128(swapped, ga);
            if (d->a < 0)
                out = _mm_or_si128(out, _mm_set1_epi32((int)0xff000000));
            _mm_storeu_si128((__m128i *)(q + i * 4), out);
        }
#endif
        for (; i < count; i++) {
            q[i * 4] = p[i * 4 + 2];
            q[i * 4 + 1] = p[i * 4 + 1];
            q[i * 4 + 2] = p[i * 4];
            q[i * 4 + 3] = d->a < 0 ? 255 : p[i * 4 + 3];
        }
        return TRUE;
    }
    /* 24 bit to 32 bit and back, with or without swapping red and blue. */
    BOOL swap = s->r != d->r;
    if (sn == 3 && dn == 4) {
        for (UINT i = 0; i < count; i++) {
            BYTE c0 = p[i * 3 + 0], c1 = p[i * 3 + 1], c2 = p[i * 3 + 2];
            if (swap) {
                BYTE t = c0;
                c0 = c2;
                c2 = t;
            }
            q[i * 4] = c0;
            q[i * 4 + 1] = c1;
            q[i * 4 + 2] = c2;
            q[i * 4 + 3] = 255;
        }
        return TRUE;
    }
    if (sn == 4 && dn == 3) {
        for (UINT i = 0; i < count; i++) {
            BYTE c0 = p[i * 4 + 0], c1 = p[i * 4 + 1], c2 = p[i * 4 + 2];
            if (swap) {
                BYTE t = c0;
                c0 = c2;
                c2 = t;
            }
            q[i * 3] = c0;
            q[i * 3 + 1] = c1;
            q[i * 3 + 2] = c2;
        }
        return TRUE;
    }
    if (sn == 3 && dn == 3) {
        for (UINT i = 0; i < count; i++) {
            BYTE c0 = p[i * 3 + 0], c1 = p[i * 3 + 1], c2 = p[i * 3 + 2];
            if (swap) {
                BYTE t = c0;
                c0 = c2;
                c2 = t;
            }
            q[i * 3] = c0;
            q[i * 3 + 1] = c1;
            q[i * 3 + 2] = c2;
        }
        return TRUE;
    }
    return FALSE;
}

/* converters. */

struct WicConverter {
    const FormatDesc *src;
    const FormatDesc *dst;
    Canon canon;
    const WICColor *palette;
    UINT palette_count;
};

/* convert width 4-byte pixels starting at x0 in both rows. */
static void convert_core(const WicConverter *c, const BYTE *src, BYTE *dst, UINT x0, UINT width)
{
    if (c->src == c->dst) {
        UINT bits = c->src->bits;
        if (bits >= 8) {
            memcpy(dst + (SIZE_T)x0 * (bits / 8), src + (SIZE_T)x0 * (bits / 8), (SIZE_T)width * (bits / 8));
            return;
        }
    }
    if (fast_row(c->src, c->dst, src, dst, x0, width))
        return;
    Canon canon = c->canon;
    if (canon == CANON_8) {
        BYTE *tmp = WicAlloc((SIZE_T)width * 4);
        if (!tmp)
            return;
        read8(c->src, src, x0, tmp, width, c->palette, c->palette_count);
        write8(c->dst, dst, x0, tmp, width);
        WicFree(tmp);
    } else if (canon == CANON_16) {
        UINT16 *tmp = WicAlloc((SIZE_T)width * 8);
        if (!tmp)
            return;
        Canon from = canon_of(c->src);
        if (from == CANON_8) {
            BYTE *t8 = WicAlloc((SIZE_T)width * 4);
            if (!t8) {
                WicFree(tmp);
                return;
            }
            read8(c->src, src, x0, t8, width, c->palette, c->palette_count);
            for (UINT i = 0; i < width; i++) {
                tmp[i * 4] = (UINT16)(t8[i * 4 + 2] * 257);
                tmp[i * 4 + 1] = (UINT16)(t8[i * 4 + 1] * 257);
                tmp[i * 4 + 2] = (UINT16)(t8[i * 4] * 257);
                tmp[i * 4 + 3] = (UINT16)(t8[i * 4 + 3] * 257);
            }
            WicFree(t8);
        } else {
            read16(c->src, src, x0, tmp, width);
        }
        Canon to = canon_of(c->dst);
        if (to == CANON_8) {
            BYTE *t8 = WicAlloc((SIZE_T)width * 4);
            if (t8) {
                for (UINT i = 0; i < width; i++) {
                    t8[i * 4 + 2] = (BYTE)(tmp[i * 4] >> 8);
                    t8[i * 4 + 1] = (BYTE)(tmp[i * 4 + 1] >> 8);
                    t8[i * 4] = (BYTE)(tmp[i * 4 + 2] >> 8);
                    t8[i * 4 + 3] = (BYTE)(tmp[i * 4 + 3] >> 8);
                }
                write8(c->dst, dst, x0, t8, width);
                WicFree(t8);
            }
        } else {
            write16(c->dst, dst, x0, tmp, width);
        }
        WicFree(tmp);
    } else {
        float *tmp = WicAlloc((SIZE_T)width * 16);
        if (!tmp)
            return;
        readf(c->src, src, x0, tmp, width, c->palette, c->palette_count);
        writef(c->dst, dst, x0, tmp, width);
        WicFree(tmp);
    }
}

/* choose the nearest palette entry by perceptually weighted channel distance. */
static UINT nearest_entry(const WICColor *palette, UINT count, BYTE r, BYTE g, BYTE b, BYTE a)
{
    UINT best = 0;
    UINT64 best_dist = ~0ull;
    for (UINT i = 0; i < count; i++) {
        WICColor c = palette[i];
        int da = (int)(c >> 24) - a, dr = (int)((c >> 16) & 255) - r, dg = (int)((c >> 8) & 255) - g,
            db = (int)(c & 255) - b;
        UINT64 dist = (UINT64)(dr * dr * 3 + dg * dg * 4 + db * db * 2) + (UINT64)(da * da) * 6;
        if (dist < best_dist) {
            best_dist = dist;
            best = i;
            if (!dist)
                break;
        }
    }
    return best;
}

static const BYTE g_bayer4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

static BYTE bayer(UINT n, UINT x, UINT y)
{
    /* the n by n matrix of Bayer's recursion, scaled to 0..255. */
    UINT v = 0, size = n;
    UINT bits = 0;
    for (UINT t = n; t > 1; t >>= 1)
        bits++;
    UINT xx = x % n, yy = y % n;
    for (UINT i = 0; i < bits; i++) {
        UINT bx = (xx >> i) & 1, by = (yy >> i) & 1;
        v = (v << 2) | ((bx ^ by) << 1) | by;
    }
    (void)size;
    return (BYTE)((v * 256 + n * n / 2) / (n * n));
}

static const BYTE g_spiral4[16] = {12, 8, 9, 13, 7, 0, 1, 10, 6, 3, 2, 11, 15, 5, 4, 14};

static BYTE spiral(UINT n, UINT x, UINT y, BOOL dual)
{
    if (n == 4) {
        BYTE v = g_spiral4[(y % 4) * 4 + x % 4];
        return (BYTE)(v * 16 + 8);
    }
    /* a clustered dot for 8 by 8: distance from the center of the 4 by 4 cell, folded. */
    UINT cx = x % 8, cy = y % 8;
    int dx = (int)(cx % 4) * 2 - 3, dy = (int)(cy % 4) * 2 - 3;
    UINT d = (UINT)(dx * dx + dy * dy);
    UINT v = d * 8 + (dual && (((cx / 4) + (cy / 4)) & 1) ? 128 : 0);
    return (BYTE)(v > 255 ? 255 : v);
}

static int dither_threshold(WICBitmapDitherType type, UINT x, UINT y)
{
    switch (type) {
    case WICBitmapDitherTypeOrdered4x4:
        return g_bayer4[(y % 4) * 4 + x % 4] * 16 + 8;
    case WICBitmapDitherTypeOrdered8x8:
        return bayer(8, x, y);
    case WICBitmapDitherTypeOrdered16x16:
        return bayer(16, x, y);
    case WICBitmapDitherTypeSpiral4x4:
        return spiral(4, x, y, FALSE);
    case WICBitmapDitherTypeSpiral8x8:
        return spiral(8, x, y, FALSE);
    case WICBitmapDitherTypeDualSpiral4x4:
        return spiral(4, x, y, TRUE);
    case WICBitmapDitherTypeDualSpiral8x8:
        return spiral(8, x, y, TRUE);
    default:
        return 128;
    }
}

/* cache nearest palette entries for 5-bit colors to avoid per-pixel searches. */
typedef struct NearestCache {
    BYTE *index; /* 32768 entries, 0xff when not known yet, for palettes of up to 255 entries. */
    BOOL usable;
} NearestCache;

struct WicIndexer {
    WICColor palette[256];
    UINT count;
    UINT transparent;     /* the entry of a transparent color, or ~0. */
    float threshold;      /* the alpha below which a pixel is transparent. */
    WICBitmapDitherType dither;
    UINT spread;          /* palette step controls ordered dithering strength. */
    BYTE *cache;
    int *errors;          /* the rows of error diffusion: 2 rows of (width + 2) times 3. */
    UINT width;
    BOOL error_ready;
};

WicIndexer *WicIndexerNew(const WICColor *palette, UINT count, WICBitmapDitherType dither, double threshold_percent,
                          UINT width)
{
    WicIndexer *ix = WicAllocZero(sizeof(*ix));
    if (!ix)
        return nullptr;
    memcpy(ix->palette, palette, count * sizeof(WICColor));
    ix->count = count;
    ix->transparent = ~0u;
    for (UINT i = 0; i < count; i++) {
        if ((palette[i] >> 24) == 0) {
            ix->transparent = i;
            break;
        }
    }
    /* truncate the percentage of 255, but round halfway pixels up. */
    double level = threshold_percent * 2.55;
    double whole = level < 0.0 ? 0.0 : level > 255.0 ? 255.0 : (double)(int)level;
    if (level >= 0.0 && level < 255.0 && level - whole == 0.5)
        whole += 1.0;
    ix->threshold = (float)whole;
    ix->dither = dither;
    ix->width = width;
    /* estimate the palette cube side from its size; its step is roughly 255 divided by the side. */
    UINT spread = 64;
    if (count <= 2)
        spread = 255;
    else if (count <= 4)
        spread = 85;
    else if (count <= 16)
        spread = 51;
    else if (count <= 64)
        spread = 36;
    else if (count <= 256)
        spread = 51;
    ix->spread = spread;
    if (count && count < 256) {
        ix->cache = WicAlloc(32768);
        if (ix->cache)
            memset(ix->cache, 0xff, 32768);
    }
    if (dither == WICBitmapDitherTypeErrorDiffusion) {
        ix->errors = WicAllocZero((SIZE_T)(width + 2) * 6 * sizeof(int));
        if (!ix->errors) {
            WicFree(ix->cache);
            WicFree(ix);
            return nullptr;
        }
    }
    return ix;
}

void WicIndexerFree(WicIndexer *ix)
{
    if (!ix)
        return;
    WicFree(ix->cache);
    WicFree(ix->errors);
    WicFree(ix);
}

static UINT indexer_lookup(WicIndexer *ix, BYTE r, BYTE g, BYTE b, BYTE a)
{
    if (a < ix->threshold && ix->transparent != ~0u)
        return ix->transparent;
    if (ix->cache && a == 255) {
        UINT key = ((UINT)(r >> 3) << 10) | ((UINT)(g >> 3) << 5) | (b >> 3);
        if (ix->cache[key] != 0xff)
            return ix->cache[key];
        BYTE cr = (BYTE)((r & 0xf8) | (r >> 5)), cg = (BYTE)((g & 0xf8) | (g >> 5)), cb = (BYTE)((b & 0xf8) | (b >> 5));
        (void)cr;
        (void)cg;
        (void)cb;
        UINT idx = nearest_entry(ix->palette, ix->count, r, g, b, a);
        if (ix->palette[idx] == (0xff000000u | ((UINT)r << 16) | ((UINT)g << 8) | b))
            ix->cache[key] = (BYTE)idx;
        return idx;
    }
    return nearest_entry(ix->palette, ix->count, r, g, b, a);
}

/* convert image row y from straight BGRA to packed indices. */
void WicIndexerRow(WicIndexer *ix, const BYTE *bgra, BYTE *dst, UINT x0, UINT count, UINT bits, UINT col, UINT y)
{
    BOOL ordered = ix->dither >= WICBitmapDitherTypeOrdered4x4 && ix->dither <= WICBitmapDitherTypeDualSpiral8x8;
    BOOL diffuse = ix->dither == WICBitmapDitherTypeErrorDiffusion && ix->errors;
    int *cur = nullptr, *next = nullptr;
    if (diffuse) {
        int *rows = ix->errors;
        UINT stride = (ix->width + 2) * 3;
        cur = rows + (y & 1 ? stride : 0);
        next = rows + (y & 1 ? 0 : stride);
        if (x0 == 0 && count == ix->width)
            memset(next, 0, stride * sizeof(int));
    }
    for (UINT i = 0; i < count; i++) {
        UINT x = x0 + i;
        UINT ax = col + x;
        int b = bgra[i * 4], g = bgra[i * 4 + 1], r = bgra[i * 4 + 2];
        BYTE a = bgra[i * 4 + 3];
        UINT index;
        if (ordered) {
            int offset = ((dither_threshold(ix->dither, ax, y) - 128) * (int)ix->spread) / 256;
            int rr = r + offset, gg = g + offset, bb = b + offset;
            rr = rr < 0 ? 0 : rr > 255 ? 255 : rr;
            gg = gg < 0 ? 0 : gg > 255 ? 255 : gg;
            bb = bb < 0 ? 0 : bb > 255 ? 255 : bb;
            index = indexer_lookup(ix, (BYTE)rr, (BYTE)gg, (BYTE)bb, a);
        } else if (diffuse) {
            int *e = cur + (x + 1) * 3;
            int rr = r + (e[0] >> 4), gg = g + (e[1] >> 4), bb = b + (e[2] >> 4);
            rr = rr < 0 ? 0 : rr > 255 ? 255 : rr;
            gg = gg < 0 ? 0 : gg > 255 ? 255 : gg;
            bb = bb < 0 ? 0 : bb > 255 ? 255 : bb;
            index = indexer_lookup(ix, (BYTE)rr, (BYTE)gg, (BYTE)bb, a);
            WICColor c = ix->palette[index];
            int er = rr - (int)((c >> 16) & 255), eg = gg - (int)((c >> 8) & 255), eb = bb - (int)(c & 255);
            if (index == ix->transparent && a < ix->threshold)
                er = eg = eb = 0;
            int *n0 = cur + (x + 2) * 3, *n1 = next + x * 3, *n2 = next + (x + 1) * 3, *n3 = next + (x + 2) * 3;
            n0[0] += er * 7; n0[1] += eg * 7; n0[2] += eb * 7;
            n1[0] += er * 3; n1[1] += eg * 3; n1[2] += eb * 3;
            n2[0] += er * 5; n2[1] += eg * 5; n2[2] += eb * 5;
            n3[0] += er;     n3[1] += eg;     n3[2] += eb;
        } else {
            index = indexer_lookup(ix, (BYTE)r, (BYTE)g, (BYTE)b, a);
        }
        if (bits == 8)
            dst[x] = (BYTE)index;
        else
            bit_put(dst, x, bits, index);
    }
}

/* entry points. */

HRESULT WicConvertRows(const GUID *from, const GUID *to, const BYTE *src, UINT src_stride, BYTE *dest,
                       UINT dest_stride, UINT width, UINT height, const WICColor *palette, UINT palette_count,
                       const WICColor *dest_palette, UINT dest_count, WICBitmapDitherType dither, UINT first_col,
                       UINT first_row, double threshold)
{
    const FormatDesc *s = find_desc(from), *d = find_desc(to);
    if (!s || !d)
        return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    WicConverter conv = {s, d, canon_of(s) == CANON_F || canon_of(d) == CANON_F ? CANON_F
                                : canon_of(s) == CANON_16 || canon_of(d) == CANON_16 ? CANON_16 : CANON_8,
                         palette, palette_count};
    if (d->model == M_INDEXED) {
        WicIndexer *ix = WicIndexerNew(dest_palette, dest_count, dither, threshold, width);
        BYTE *tmp = WicAlloc((SIZE_T)width * 4);
        if (!ix || !tmp) {
            WicIndexerFree(ix);
            WicFree(tmp);
            return E_OUTOFMEMORY;
        }
        WicConverter to_bgra = {s, nullptr, CANON_8, palette, palette_count};
        for (UINT y = 0; y < height; y++) {
            const BYTE *row = src + (SIZE_T)y * src_stride;
            if (canon_of(s) == CANON_8) {
                read8(s, row, 0, tmp, width, palette, palette_count);
            } else {
                float *f = WicAlloc((SIZE_T)width * 16);
                if (!f) {
                    WicIndexerFree(ix);
                    WicFree(tmp);
                    return E_OUTOFMEMORY;
                }
                readf(s, row, 0, f, width, palette, palette_count);
                for (UINT i = 0; i < width; i++) {
                    tmp[i * 4 + 2] = linear_to_srgb8(f[i * 4]);
                    tmp[i * 4 + 1] = linear_to_srgb8(f[i * 4 + 1]);
                    tmp[i * 4] = linear_to_srgb8(f[i * 4 + 2]);
                    float al = f[i * 4 + 3] * 255.0f + 0.5f;
                    tmp[i * 4 + 3] = al <= 0 ? 0 : al >= 255 ? 255 : (BYTE)(int)al;
                }
                WicFree(f);
            }
            WicIndexerRow(ix, tmp, dest + (SIZE_T)y * dest_stride, 0, width, d->bits, first_col, first_row + y);
        }
        (void)to_bgra;
        WicIndexerFree(ix);
        WicFree(tmp);
        return S_OK;
    }
    /* sixteen bit destinations dither when asked to. */
    BOOL dither_small = dither != WICBitmapDitherTypeNone && dither <= WICBitmapDitherTypeDualSpiral8x8 &&
                        (d->ctype == CT_555 || d->ctype == CT_565 || d->ctype == CT_5551);
    for (UINT y = 0; y < height; y++) {
        const BYTE *row = src + (SIZE_T)y * src_stride;
        BYTE *out = dest + (SIZE_T)y * dest_stride;
        if (dither_small) {
            BYTE *tmp = WicAlloc((SIZE_T)width * 4);
            if (!tmp)
                return E_OUTOFMEMORY;
            read8(s, row, 0, tmp, width, palette, palette_count);
            UINT bits_r = d->ctype == CT_565 ? 5 : 5, bits_g = d->ctype == CT_565 ? 6 : 5;
            for (UINT x = 0; x < width; x++) {
                int t = dither_threshold(dither, first_col + x, first_row + y);
                for (int ch = 0; ch < 3; ch++) {
                    UINT levels = ch == 1 ? bits_g : bits_r;
                    int step = 256 >> levels;
                    int v = tmp[x * 4 + ch] + ((t - 128) * step) / 256;
                    tmp[x * 4 + ch] = (BYTE)(v < 0 ? 0 : v > 255 ? 255 : v);
                }
            }
            write8(d, out, 0, tmp, width);
            WicFree(tmp);
        } else {
            convert_core(&conv, row, out, 0, width);
        }
    }
    return S_OK;
}
