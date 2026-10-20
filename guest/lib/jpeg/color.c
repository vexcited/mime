#include "internal.h"

/*
 * planar rows to interleaved pixels. the YCbCr to RGB transform is the 16 bit fixed point one
 * with the factors 1.40200, 0.34414, 0.71414 and 1.77200 and a half added before every shift.
 */

#define FIX_CR_R 91881
#define FIX_CB_B 116130
#define FIX_CR_G 46802
#define FIX_CB_G 22554

static inline uint8_t clamp_sample(int v)
{
    return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v;
}

static inline void ycc_pixel(int y, int cb, int cr, int *r, int *g, int *b)
{
    cb -= 128;
    cr -= 128;
    *r = clamp_sample(y + ((FIX_CR_R * cr + 32768) >> 16));
    *g = clamp_sample(y + ((-FIX_CB_G * cb - FIX_CR_G * cr + 32768) >> 16));
    *b = clamp_sample(y + ((FIX_CB_B * cb + 32768) >> 16));
}

#ifdef MIME_JPEG_SSE2

/* 16 pixels of YCbCr to R, G and B bytes. */
static inline void ycc16(__m128i y, __m128i cb, __m128i cr, __m128i *r, __m128i *g, __m128i *b)
{
    const __m128i z = _mm_setzero_si128();
    const __m128i c128 = _mm_set1_epi16(128);
    const __m128i kr = _mm_set1_epi32(26345);
    const __m128i kb = _mm_set1_epi32((int)(uint16_t)-14942);
    const __m128i kg = _mm_set1_epi32((int)(((uint32_t)(uint16_t)18734 << 16) | (uint16_t)-22554));
    const __m128i half = _mm_set1_epi32(32768);
    __m128i rr[2], gg[2], bb[2];

    for (int h = 0; h < 2; h++) {
        __m128i yw = h ? _mm_unpackhi_epi8(y, z) : _mm_unpacklo_epi8(y, z);
        __m128i xb = _mm_sub_epi16(h ? _mm_unpackhi_epi8(cb, z) : _mm_unpacklo_epi8(cb, z), c128);
        __m128i xr = _mm_sub_epi16(h ? _mm_unpackhi_epi8(cr, z) : _mm_unpacklo_epi8(cr, z), c128);
        __m128i tr = _mm_packs_epi32(
            _mm_srai_epi32(_mm_add_epi32(_mm_madd_epi16(_mm_unpacklo_epi16(xr, z), kr), half), 16),
            _mm_srai_epi32(_mm_add_epi32(_mm_madd_epi16(_mm_unpackhi_epi16(xr, z), kr), half), 16));
        __m128i tb = _mm_packs_epi32(
            _mm_srai_epi32(_mm_add_epi32(_mm_madd_epi16(_mm_unpacklo_epi16(xb, z), kb), half), 16),
            _mm_srai_epi32(_mm_add_epi32(_mm_madd_epi16(_mm_unpackhi_epi16(xb, z), kb), half), 16));
        __m128i tg = _mm_packs_epi32(
            _mm_srai_epi32(_mm_add_epi32(_mm_madd_epi16(_mm_unpacklo_epi16(xb, xr), kg), half), 16),
            _mm_srai_epi32(_mm_add_epi32(_mm_madd_epi16(_mm_unpackhi_epi16(xb, xr), kg), half), 16));
        rr[h] = _mm_add_epi16(_mm_add_epi16(yw, xr), tr);
        bb[h] = _mm_add_epi16(_mm_add_epi16(yw, _mm_add_epi16(xb, xb)), tb);
        gg[h] = _mm_sub_epi16(_mm_add_epi16(yw, tg), xr);
    }
    *r = _mm_packus_epi16(rr[0], rr[1]);
    *g = _mm_packus_epi16(gg[0], gg[1]);
    *b = _mm_packus_epi16(bb[0], bb[1]);
}

/* four pixels of b, g, r, 0 to 12 bytes. */
static inline __m128i pack4(__m128i px)
{
    const __m128i m1 = _mm_set_epi32(0, 0xffffff, 0, 0xffffff);
    const __m128i m2 = _mm_set_epi32(0xffff, 0xff000000u, 0xffff, 0xff000000u);
    __m128i y = _mm_or_si128(_mm_and_si128(px, m1), _mm_and_si128(_mm_srli_epi64(px, 8), m2));
    const __m128i lo6 = _mm_set_epi32(0, 0, 0xffff, 0xffffffffu);
    const __m128i mid6 = _mm_set_epi32(0, 0xffffffffu, 0xffff0000u, 0);
    return _mm_or_si128(_mm_and_si128(y, lo6), _mm_and_si128(_mm_srli_si128(y, 2), mid6));
}

static inline void store_bgr16(uint8_t *o, __m128i b, __m128i g, __m128i r)
{
    const __m128i z = _mm_setzero_si128();
    __m128i bg_lo = _mm_unpacklo_epi8(b, g);
    __m128i bg_hi = _mm_unpackhi_epi8(b, g);
    __m128i r_lo = _mm_unpacklo_epi8(r, z);
    __m128i r_hi = _mm_unpackhi_epi8(r, z);
    _mm_storeu_si128((__m128i *)o, pack4(_mm_unpacklo_epi16(bg_lo, r_lo)));
    _mm_storeu_si128((__m128i *)(o + 12), pack4(_mm_unpackhi_epi16(bg_lo, r_lo)));
    _mm_storeu_si128((__m128i *)(o + 24), pack4(_mm_unpacklo_epi16(bg_hi, r_hi)));
    __m128i last = pack4(_mm_unpackhi_epi16(bg_hi, r_hi));
    _mm_storel_epi64((__m128i *)(o + 36), last);
    int tail = _mm_cvtsi128_si32(_mm_srli_si128(last, 8));
    __builtin_memcpy(o + 44, &tail, 4);
}

static inline void store_4x16(uint8_t *o, __m128i a, __m128i b, __m128i c, __m128i d)
{
    __m128i ab_lo = _mm_unpacklo_epi8(a, b);
    __m128i ab_hi = _mm_unpackhi_epi8(a, b);
    __m128i cd_lo = _mm_unpacklo_epi8(c, d);
    __m128i cd_hi = _mm_unpackhi_epi8(c, d);
    _mm_storeu_si128((__m128i *)o, _mm_unpacklo_epi16(ab_lo, cd_lo));
    _mm_storeu_si128((__m128i *)(o + 16), _mm_unpackhi_epi16(ab_lo, cd_lo));
    _mm_storeu_si128((__m128i *)(o + 32), _mm_unpacklo_epi16(ab_hi, cd_hi));
    _mm_storeu_si128((__m128i *)(o + 48), _mm_unpackhi_epi16(ab_hi, cd_hi));
}

#define LD(p, i) _mm_loadu_si128((const __m128i *)((p) + (i)))

#endif

void mime_jpeg_row_ycc_bgr(const uint8_t *y, const uint8_t *cb, const uint8_t *cr, uint8_t *out, uint32_t n)
{
    uint32_t i = 0;
#ifdef MIME_JPEG_SSE2
    for (; i + 16 <= n; i += 16) {
        __m128i r, g, b;
        ycc16(LD(y, i), LD(cb, i), LD(cr, i), &r, &g, &b);
        store_bgr16(out + 3 * (size_t)i, b, g, r);
    }
#endif
    for (; i < n; i++) {
        int r, g, b;
        ycc_pixel(y[i], cb[i], cr[i], &r, &g, &b);
        out[3 * (size_t)i] = (uint8_t)b;
        out[3 * (size_t)i + 1] = (uint8_t)g;
        out[3 * (size_t)i + 2] = (uint8_t)r;
    }
}

void mime_jpeg_row_rgb_bgr(const uint8_t *r, const uint8_t *g, const uint8_t *b, uint8_t *out, uint32_t n)
{
    uint32_t i = 0;
#ifdef MIME_JPEG_SSE2
    for (; i + 16 <= n; i += 16)
        store_bgr16(out + 3 * (size_t)i, LD(b, i), LD(g, i), LD(r, i));
#endif
    for (; i < n; i++) {
        out[3 * (size_t)i] = b[i];
        out[3 * (size_t)i + 1] = g[i];
        out[3 * (size_t)i + 2] = r[i];
    }
}

void mime_jpeg_row_cmyk(const uint8_t *p0, const uint8_t *p1, const uint8_t *p2, const uint8_t *p3, uint8_t *out,
                        uint32_t n, int ycck, int inverted)
{
    uint32_t i = 0;
    /* ycck stores 255 minus the color, so the converted values are already the inverted ones. */
    uint8_t flip_cmy = (uint8_t)(ycck ? (inverted ? 0x00 : 0xff) : (inverted ? 0xff : 0x00));
    uint8_t flip_k = (uint8_t)(inverted ? 0xff : 0x00);
#ifdef MIME_JPEG_SSE2
    const __m128i fc = _mm_set1_epi8((char)flip_cmy);
    const __m128i fk = _mm_set1_epi8((char)flip_k);
    for (; i + 16 <= n; i += 16) {
        __m128i c, m, y, k = _mm_xor_si128(LD(p3, i), fk);
        if (ycck) {
            ycc16(LD(p0, i), LD(p1, i), LD(p2, i), &c, &m, &y);
        } else {
            c = LD(p0, i);
            m = LD(p1, i);
            y = LD(p2, i);
        }
        store_4x16(out + 4 * (size_t)i, _mm_xor_si128(c, fc), _mm_xor_si128(m, fc), _mm_xor_si128(y, fc), k);
    }
#endif
    for (; i < n; i++) {
        int c = p0[i], m = p1[i], y = p2[i];
        if (ycck)
            ycc_pixel(p0[i], p1[i], p2[i], &c, &m, &y);
        out[4 * (size_t)i] = (uint8_t)(c ^ flip_cmy);
        out[4 * (size_t)i + 1] = (uint8_t)(m ^ flip_cmy);
        out[4 * (size_t)i + 2] = (uint8_t)(y ^ flip_cmy);
        out[4 * (size_t)i + 3] = (uint8_t)(p3[i] ^ flip_k);
    }
}
