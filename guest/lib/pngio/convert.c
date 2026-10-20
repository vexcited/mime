#include "pngio_internal.h"

#if PNGIO_SSE2
#include <emmintrin.h>
#endif

enum {
    CV_COPY,
    CV_SWAP16,
    CV_RGB_BGR,
    CV_RGBA_BGRA,
    CV_GA8_BGRA,
    CV_GA16_RGBA64,
    CV_GRAY8_KEY,
    CV_GRAYLOW_KEY,
    CV_RGB8_KEY,
    CV_GRAY16_KEY,
    CV_RGB16_KEY,
};

static inline uint32_t load32(const uint8_t *p)
{
    uint32_t v;
    __builtin_memcpy(&v, p, 4);
    return v;
}

static inline void store32(uint8_t *p, uint32_t v)
{
    __builtin_memcpy(p, &v, 4);
}

static inline uint32_t rd32(const uint8_t *p)
{
#if PNGIO_LE
    return load32(p);
#else
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
#endif
}

static inline void wr32(uint8_t *p, uint32_t v)
{
#if PNGIO_LE
    store32(p, v);
#else
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
#endif
}

void mime_pngio_swap16(void *dst, const void *src, size_t count)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    size_t i = 0;

#if PNGIO_SSE2
    if (pngio_use_simd()) {
        for (; i + 8 <= count; i += 8) {
            __m128i x = _mm_loadu_si128((const __m128i *)(s + 2 * i));
            x = _mm_or_si128(_mm_slli_epi16(x, 8), _mm_srli_epi16(x, 8));
            _mm_storeu_si128((__m128i *)(d + 2 * i), x);
        }
    }
#endif
    for (; i + 2 <= count; i += 2) {
        uint32_t v = rd32(s + 2 * i);
        wr32(d + 2 * i, ((v & 0x00ff00ffu) << 8) | ((v >> 8) & 0x00ff00ffu));
    }
    if (i < count) {
        uint8_t a = s[2 * i], b = s[2 * i + 1];
        d[2 * i] = b;
        d[2 * i + 1] = a;
    }
}

void pngio_swap_rb32(uint8_t *dst, const uint8_t *src, size_t count)
{
    size_t i = 0;

#if PNGIO_SSE2
    if (pngio_use_simd()) {
        const __m128i ga_mask = _mm_set1_epi32((int)0xff00ff00u);
        const __m128i rb_mask = _mm_set1_epi32(0x00ff00ff);
        for (; i + 4 <= count; i += 4) {
            __m128i x = _mm_loadu_si128((const __m128i *)(src + 4 * i));
            __m128i rb = _mm_and_si128(x, rb_mask);
            __m128i r = _mm_or_si128(_mm_slli_epi32(rb, 16), _mm_srli_epi32(rb, 16));
            _mm_storeu_si128((__m128i *)(dst + 4 * i), _mm_or_si128(_mm_and_si128(x, ga_mask), r));
        }
    }
#endif
    for (; i < count; i++) {
        uint32_t v = rd32(src + 4 * i);
        wr32(dst + 4 * i, (v & 0xff00ff00u) | ((v & 0xff) << 16) | ((v >> 16) & 0xff));
    }
}

/* four pixels, twelve bytes, in three words. */
void pngio_swap_rb24(uint8_t *dst, const uint8_t *src, size_t count)
{
    size_t i = 0;

    for (; i + 4 <= count; i += 4) {
        uint32_t w0 = rd32(src + 3 * i), w1 = rd32(src + 3 * i + 4), w2 = rd32(src + 3 * i + 8);
        uint32_t o0 = (w0 & 0x0000ff00u) | ((w0 & 0xffu) << 16) | ((w0 >> 16) & 0xffu) | ((w1 & 0xff00u) << 16);
        uint32_t o1 = (w1 & 0xffu) | (w1 & 0xff000000u) | ((w0 >> 24) << 8) | ((w2 & 0xffu) << 16);
        uint32_t o2 = ((w1 >> 16) & 0xffu) | ((w2 >> 24) << 8) | (w2 & 0x00ff0000u) | ((w2 & 0xff00u) << 16);
        wr32(dst + 3 * i, o0);
        wr32(dst + 3 * i + 4, o1);
        wr32(dst + 3 * i + 8, o2);
    }
    for (; i < count; i++) {
        uint8_t a = src[3 * i], b = src[3 * i + 1], c = src[3 * i + 2];
        dst[3 * i] = c;
        dst[3 * i + 1] = b;
        dst[3 * i + 2] = a;
    }
}

/* four bgrx pixels, sixteen bytes, to twelve bytes of rgb. */
void pngio_bgrx_to_rgb24(uint8_t *dst, const uint8_t *src, size_t count)
{
    size_t i = 0;

    for (; i + 4 <= count; i += 4) {
        uint32_t x0 = rd32(src + 4 * i), x1 = rd32(src + 4 * i + 4);
        uint32_t x2 = rd32(src + 4 * i + 8), x3 = rd32(src + 4 * i + 12);
        uint32_t o0 = ((x0 >> 16) & 0xffu) | (x0 & 0xff00u) | ((x0 & 0xffu) << 16) | ((x1 & 0x00ff0000u) << 8);
        uint32_t o1 = ((x1 >> 8) & 0xffu) | ((x1 & 0xffu) << 8) | (x2 & 0x00ff0000u) | ((x2 & 0xff00u) << 16);
        uint32_t o2 = (x2 & 0xffu) | (((x3 >> 16) & 0xffu) << 8) | ((x3 & 0xff00u) << 8) | ((x3 & 0xffu) << 24);
        wr32(dst + 3 * i, o0);
        wr32(dst + 3 * i + 4, o1);
        wr32(dst + 3 * i + 8, o2);
    }
    for (; i < count; i++) {
        uint8_t b = src[4 * i], g = src[4 * i + 1], r = src[4 * i + 2];
        dst[3 * i] = r;
        dst[3 * i + 1] = g;
        dst[3 * i + 2] = b;
    }
}

void pngio_ga8_to_bgra(uint8_t *dst, const uint8_t *src, size_t count)
{
    size_t i = 0;

#if PNGIO_SSE2
    if (pngio_use_simd()) {
        const __m128i lo = _mm_set1_epi16(0x00ff), one = _mm_set1_epi16(0x0101);
        for (; i + 8 <= count; i += 8) {
            __m128i v = _mm_loadu_si128((const __m128i *)(src + 2 * i));
            __m128i gg = _mm_mullo_epi16(_mm_and_si128(v, lo), one);
            _mm_storeu_si128((__m128i *)(dst + 4 * i), _mm_unpacklo_epi16(gg, v));
            _mm_storeu_si128((__m128i *)(dst + 4 * i + 16), _mm_unpackhi_epi16(gg, v));
        }
    }
#endif
    for (; i < count; i++) {
        uint8_t g = src[2 * i], a = src[2 * i + 1];
        dst[4 * i] = g;
        dst[4 * i + 1] = g;
        dst[4 * i + 2] = g;
        dst[4 * i + 3] = a;
    }
}

void pngio_ga16_to_rgba64(uint8_t *dst, const uint8_t *src, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        uint8_t gh = src[4 * i], gl = src[4 * i + 1], ah = src[4 * i + 2], al = src[4 * i + 3];
        uint8_t *d = dst + 8 * i;
        d[0] = d[2] = d[4] = gl;
        d[1] = d[3] = d[5] = gh;
        d[6] = al;
        d[7] = ah;
    }
}

void pngio_conv_init(PngioConv *cv, const MimePngioInfo *info, int format)
{
    unsigned depth = info->bit_depth;

    __builtin_memset(cv, 0, sizeof(*cv));
    cv->depth = depth;
    cv->bits = mime_pngio_bits_per_pixel(format);
    cv->kind = CV_COPY;
    if (info->has_trns_key) {
        cv->key[0] = info->trns_key[0];
        cv->key[1] = info->trns_key[1];
        cv->key[2] = info->trns_key[2];
    }

    switch (info->color_type) {
    case MIME_PNGIO_COLOR_GRAY:
        if (depth == 16) {
            if (format == MIME_PNGIO_FMT_RGBA64) {
                cv->kind = CV_GRAY16_KEY;
            } else {
                cv->kind = CV_SWAP16;
                cv->samples = 1;
            }
        } else if (format == MIME_PNGIO_FMT_BGRA32) {
            if (depth == 8) {
                cv->kind = CV_GRAY8_KEY;
            } else {
                cv->kind = CV_GRAYLOW_KEY;
                unsigned max = (1u << depth) - 1;
                for (unsigned v = 0; v <= max; v++) {
                    uint32_t g = v * 255 / max;
                    cv->lut[v] = g | g << 8 | g << 16 | (v == cv->key[0] ? 0u : 0xff000000u);
                }
            }
        }
        break;
    case MIME_PNGIO_COLOR_RGB:
        if (depth == 8) {
            if (format == MIME_PNGIO_FMT_BGR24)
                cv->kind = CV_RGB_BGR;
            else if (format == MIME_PNGIO_FMT_BGRA32)
                cv->kind = CV_RGB8_KEY;
        } else if (format == MIME_PNGIO_FMT_RGBA64) {
            cv->kind = CV_RGB16_KEY;
        } else {
            cv->kind = CV_SWAP16;
            cv->samples = 3;
        }
        break;
    case MIME_PNGIO_COLOR_GRAY_ALPHA:
        if (depth == 8) {
            if (format == MIME_PNGIO_FMT_BGRA32)
                cv->kind = CV_GA8_BGRA;
        } else if (format == MIME_PNGIO_FMT_RGBA64) {
            cv->kind = CV_GA16_RGBA64;
        } else {
            cv->kind = CV_SWAP16;
            cv->samples = 2;
        }
        break;
    case MIME_PNGIO_COLOR_RGBA:
        if (depth == 8) {
            if (format == MIME_PNGIO_FMT_BGRA32)
                cv->kind = CV_RGBA_BGRA;
        } else {
            cv->kind = CV_SWAP16;
            cv->samples = 4;
        }
        break;
    default:
        break;
    }
}

static void gray8_key(const PngioConv *cv, const uint8_t *src, uint8_t *dst, uint32_t count)
{
    uint32_t i = 0;
    uint8_t key = (uint8_t)cv->key[0];

#if PNGIO_SSE2
    if (pngio_use_simd()) {
        const __m128i k = _mm_set1_epi8((char)key);
        for (; i + 16 <= count; i += 16) {
            __m128i g = _mm_loadu_si128((const __m128i *)(src + i));
            __m128i a = _mm_cmpeq_epi8(g, k);
            a = _mm_xor_si128(a, _mm_set1_epi8(-1));
            __m128i gg_lo = _mm_unpacklo_epi8(g, g), gg_hi = _mm_unpackhi_epi8(g, g);
            __m128i ga_lo = _mm_unpacklo_epi8(g, a), ga_hi = _mm_unpackhi_epi8(g, a);
            _mm_storeu_si128((__m128i *)(dst + 4 * i), _mm_unpacklo_epi16(gg_lo, ga_lo));
            _mm_storeu_si128((__m128i *)(dst + 4 * i + 16), _mm_unpackhi_epi16(gg_lo, ga_lo));
            _mm_storeu_si128((__m128i *)(dst + 4 * i + 32), _mm_unpacklo_epi16(gg_hi, ga_hi));
            _mm_storeu_si128((__m128i *)(dst + 4 * i + 48), _mm_unpackhi_epi16(gg_hi, ga_hi));
        }
    }
#endif
    for (; i < count; i++) {
        uint8_t g = src[i];
        dst[4 * i] = g;
        dst[4 * i + 1] = g;
        dst[4 * i + 2] = g;
        dst[4 * i + 3] = g == key ? 0 : 255;
    }
}

void pngio_conv_row(const PngioConv *cv, const uint8_t *src, uint8_t *dst, uint32_t count)
{
    uint32_t i;

    switch (cv->kind) {
    case CV_COPY: {
        size_t bits = (size_t)count * cv->bits, bytes = (bits + 7) >> 3;
        __builtin_memcpy(dst, src, bytes);
        if (bits & 7)
            dst[bytes - 1] &= (uint8_t)(0xffu << (8 - (bits & 7)));
        break;
    }
    case CV_SWAP16:
        mime_pngio_swap16(dst, src, (size_t)count * cv->samples);
        break;
    case CV_RGB_BGR:
        pngio_swap_rb24(dst, src, count);
        break;
    case CV_RGBA_BGRA:
        pngio_swap_rb32(dst, src, count);
        break;
    case CV_GA8_BGRA:
        pngio_ga8_to_bgra(dst, src, count);
        break;
    case CV_GA16_RGBA64:
        pngio_ga16_to_rgba64(dst, src, count);
        break;
    case CV_GRAY8_KEY:
        gray8_key(cv, src, dst, count);
        break;
    case CV_GRAYLOW_KEY: {
        unsigned depth = cv->depth, mask = (1u << depth) - 1;
        for (i = 0; i < count; i++) {
            size_t bit = (size_t)i * depth;
            unsigned v = (src[bit >> 3] >> (8 - depth - (bit & 7))) & mask;
            wr32(dst + 4 * (size_t)i, cv->lut[v]);
        }
        break;
    }
    case CV_RGB8_KEY: {
        unsigned kr = cv->key[0] & 0xff, kg = cv->key[1] & 0xff, kb = cv->key[2] & 0xff;
        for (i = 0; i < count; i++, src += 3, dst += 4) {
            unsigned r = src[0], g = src[1], b = src[2];
            dst[0] = (uint8_t)b;
            dst[1] = (uint8_t)g;
            dst[2] = (uint8_t)r;
            dst[3] = r == kr && g == kg && b == kb ? 0 : 255;
        }
        break;
    }
    case CV_GRAY16_KEY:
        for (i = 0; i < count; i++, src += 2, dst += 8) {
            dst[0] = dst[2] = dst[4] = src[1];
            dst[1] = dst[3] = dst[5] = src[0];
            unsigned a = pngio_be16(src) == cv->key[0] ? 0 : 0xff;
            dst[6] = dst[7] = (uint8_t)a;
        }
        break;
    case CV_RGB16_KEY:
        for (i = 0; i < count; i++, src += 6, dst += 8) {
            dst[0] = src[1];
            dst[1] = src[0];
            dst[2] = src[3];
            dst[3] = src[2];
            dst[4] = src[5];
            dst[5] = src[4];
            bool hit = pngio_be16(src) == cv->key[0] && pngio_be16(src + 2) == cv->key[1] &&
                       pngio_be16(src + 4) == cv->key[2];
            dst[6] = dst[7] = hit ? 0 : 0xff;
        }
        break;
    }
}

void pngio_unconv_row(int format, const uint8_t *src, uint8_t *dst, uint32_t count)
{
    unsigned bits = mime_pngio_bits_per_pixel(format);

    switch (format) {
    case MIME_PNGIO_FMT_GRAY16:
        mime_pngio_swap16(dst, src, count);
        break;
    case MIME_PNGIO_FMT_GRAY_ALPHA32:
        mime_pngio_swap16(dst, src, (size_t)count * 2);
        break;
    case MIME_PNGIO_FMT_RGB48:
        mime_pngio_swap16(dst, src, (size_t)count * 3);
        break;
    case MIME_PNGIO_FMT_RGBA64:
        mime_pngio_swap16(dst, src, (size_t)count * 4);
        break;
    case MIME_PNGIO_FMT_BGR24:
        pngio_swap_rb24(dst, src, count);
        break;
    case MIME_PNGIO_FMT_BGRA32:
        pngio_swap_rb32(dst, src, count);
        break;
    case MIME_PNGIO_FMT_BGRX32:
        pngio_bgrx_to_rgb24(dst, src, count);
        break;
    default: {
        size_t nbits = (size_t)count * bits, bytes = (nbits + 7) >> 3;
        __builtin_memcpy(dst, src, bytes);
        if (nbits & 7)
            dst[bytes - 1] &= (uint8_t)(0xffu << (8 - (nbits & 7)));
        break;
    }
    }
}
