#include "internal.h"

/*
 * forward dct in the slow integer method with 13 bit constants, followed by the quantization.
 * the transform leaves its results 8 times too large, quantizing divides by 8 times the table
 * entry with rounding to nearest, away from zero on ties. the division is done by multiplying
 * with a reciprocal in two 16 bit steps so that SSE2 can do it without a per lane shift.
 */

#define CONST_BITS 13
#define PASS1_BITS 2

#define DESCALE(x, n) (((x) + (1 << ((n) - 1))) >> (n))

#define FIX_0_298631336 2446
#define FIX_0_390180644 3196
#define FIX_0_541196100 4433
#define FIX_0_765366865 6270
#define FIX_0_899976223 7373
#define FIX_1_175875602 9633
#define FIX_1_501321110 12299
#define FIX_1_847759065 15137
#define FIX_1_961570560 16069
#define FIX_2_053119869 16819
#define FIX_2_562915447 20995
#define FIX_3_072711026 25172

void mime_jpeg_make_quantizer(const uint16_t qt[64], MimeJpegQuantizer *qz)
{
    for (int i = 0; i < 64; i++) {
        uint32_t q = qt[i] < 1 ? 1 : qt[i] > 8191 ? 8191 : qt[i];
        uint32_t d = q * 8;
        unsigned l = 3;
        while ((1u << l) < d)
            l++;
        qz->bias[i] = (uint16_t)(q * 4);
        qz->mul[i] = (uint16_t)((((1u << l) - d) << 16) / d + 1);
        qz->scale[i] = (uint16_t)(1u << (17 - l));
    }
}

/* the transform of 8 values in place, one dimension. pass 1 works on rows. */
static inline void fdct_1d(int32_t *p, int step, int pass)
{
    int32_t d0 = p[0], d1 = p[step], d2 = p[2 * step], d3 = p[3 * step];
    int32_t d4 = p[4 * step], d5 = p[5 * step], d6 = p[6 * step], d7 = p[7 * step];
    int32_t tmp0 = d0 + d7, tmp7 = d0 - d7;
    int32_t tmp1 = d1 + d6, tmp6 = d1 - d6;
    int32_t tmp2 = d2 + d5, tmp5 = d2 - d5;
    int32_t tmp3 = d3 + d4, tmp4 = d3 - d4;
    int32_t tmp10 = tmp0 + tmp3, tmp13 = tmp0 - tmp3;
    int32_t tmp11 = tmp1 + tmp2, tmp12 = tmp1 - tmp2;
    int shift = pass == 1 ? CONST_BITS - PASS1_BITS : CONST_BITS + PASS1_BITS;

    if (pass == 1) {
        p[0] = (tmp10 + tmp11) * (1 << PASS1_BITS);
        p[4 * step] = (tmp10 - tmp11) * (1 << PASS1_BITS);
    } else {
        p[0] = DESCALE(tmp10 + tmp11, PASS1_BITS);
        p[4 * step] = DESCALE(tmp10 - tmp11, PASS1_BITS);
    }
    int32_t z1 = (tmp12 + tmp13) * FIX_0_541196100;
    p[2 * step] = DESCALE(z1 + tmp13 * FIX_0_765366865, shift);
    p[6 * step] = DESCALE(z1 + tmp12 * -FIX_1_847759065, shift);

    z1 = tmp4 + tmp7;
    int32_t z2 = tmp5 + tmp6;
    int32_t z3 = tmp4 + tmp6;
    int32_t z4 = tmp5 + tmp7;
    int32_t z5 = (z3 + z4) * FIX_1_175875602;
    tmp4 *= FIX_0_298631336;
    tmp5 *= FIX_2_053119869;
    tmp6 *= FIX_3_072711026;
    tmp7 *= FIX_1_501321110;
    z1 *= -FIX_0_899976223;
    z2 *= -FIX_2_562915447;
    z3 *= -FIX_1_961570560;
    z4 *= -FIX_0_390180644;
    z3 += z5;
    z4 += z5;
    p[7 * step] = DESCALE(tmp4 + z1 + z3, shift);
    p[5 * step] = DESCALE(tmp5 + z2 + z4, shift);
    p[3 * step] = DESCALE(tmp6 + z2 + z3, shift);
    p[1 * step] = DESCALE(tmp7 + z1 + z4, shift);
}

[[maybe_unused]] static void fdct_scalar(const uint8_t *src, size_t stride, int16_t out[64])
{
    int32_t ws[64];
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            ws[r * 8 + c] = (int32_t)src[(size_t)r * stride + c] - 128;
    for (int r = 0; r < 8; r++)
        fdct_1d(ws + r * 8, 1, 1);
    for (int c = 0; c < 8; c++)
        fdct_1d(ws + c, 8, 2);
    for (int i = 0; i < 64; i++)
        out[i] = (int16_t)ws[i];
}

[[maybe_unused]] static void quantize_scalar(const int16_t *f, const MimeJpegQuantizer *qz, int16_t *out)
{
    for (int i = 0; i < 64; i++) {
        int32_t v = f[i];
        uint32_t a = (uint32_t)(v < 0 ? -v : v);
        uint32_t n = (a + qz->bias[i]) & 0xffff;
        uint32_t t1 = (n * qz->mul[i]) >> 16;
        uint32_t u = t1 + ((n - t1) >> 1);
        int32_t q = (int32_t)((u * qz->scale[i]) >> 16);
        out[i] = (int16_t)(v < 0 ? -q : q);
    }
}

#ifdef MIME_JPEG_SSE2

#define K2(a, b) _mm_set1_epi32((int)(((uint32_t)(uint16_t)(int16_t)(b) << 16) | (uint32_t)(uint16_t)(int16_t)(a)))

static inline void transpose8(__m128i *r)
{
    __m128i a0 = _mm_unpacklo_epi16(r[0], r[1]);
    __m128i a1 = _mm_unpackhi_epi16(r[0], r[1]);
    __m128i a2 = _mm_unpacklo_epi16(r[2], r[3]);
    __m128i a3 = _mm_unpackhi_epi16(r[2], r[3]);
    __m128i a4 = _mm_unpacklo_epi16(r[4], r[5]);
    __m128i a5 = _mm_unpackhi_epi16(r[4], r[5]);
    __m128i a6 = _mm_unpacklo_epi16(r[6], r[7]);
    __m128i a7 = _mm_unpackhi_epi16(r[6], r[7]);
    __m128i b0 = _mm_unpacklo_epi32(a0, a2);
    __m128i b1 = _mm_unpackhi_epi32(a0, a2);
    __m128i b2 = _mm_unpacklo_epi32(a1, a3);
    __m128i b3 = _mm_unpackhi_epi32(a1, a3);
    __m128i b4 = _mm_unpacklo_epi32(a4, a6);
    __m128i b5 = _mm_unpackhi_epi32(a4, a6);
    __m128i b6 = _mm_unpacklo_epi32(a5, a7);
    __m128i b7 = _mm_unpackhi_epi32(a5, a7);
    r[0] = _mm_unpacklo_epi64(b0, b4);
    r[1] = _mm_unpackhi_epi64(b0, b4);
    r[2] = _mm_unpacklo_epi64(b1, b5);
    r[3] = _mm_unpackhi_epi64(b1, b5);
    r[4] = _mm_unpacklo_epi64(b2, b6);
    r[5] = _mm_unpackhi_epi64(b2, b6);
    r[6] = _mm_unpacklo_epi64(b3, b7);
    r[7] = _mm_unpackhi_epi64(b3, b7);
}

/* the rotations of four lanes of the even and odd parts, descaled to 32 bit values. */
static inline void fdct_half(__m128i p_even, __m128i p45, __m128i p67, int shift, __m128i o[8])
{
    const __m128i c2 = K2(FIX_0_541196100 + FIX_0_765366865, FIX_0_541196100);
    const __m128i c6 = K2(FIX_0_541196100, FIX_0_541196100 - FIX_1_847759065);
    const __m128i k0a = K2(-11363, 9633), k0b = K2(-6436, 2260);
    const __m128i k1a = K2(9633, 2261), k1b = K2(-11362, 6437);
    const __m128i k2a = K2(-6436, -11362), k2b = K2(-2259, 9633);
    const __m128i k3a = K2(2260, 6437), k3b = K2(9633, 11363);
    const __m128i rnd = _mm_set1_epi32(1 << (shift - 1));
    o[2] = _mm_srai_epi32(_mm_add_epi32(_mm_madd_epi16(p_even, c2), rnd), shift);
    o[6] = _mm_srai_epi32(_mm_add_epi32(_mm_madd_epi16(p_even, c6), rnd), shift);
    o[7] = _mm_srai_epi32(_mm_add_epi32(_mm_add_epi32(_mm_madd_epi16(p45, k0a), _mm_madd_epi16(p67, k0b)), rnd), shift);
    o[5] = _mm_srai_epi32(_mm_add_epi32(_mm_add_epi32(_mm_madd_epi16(p45, k1a), _mm_madd_epi16(p67, k1b)), rnd), shift);
    o[3] = _mm_srai_epi32(_mm_add_epi32(_mm_add_epi32(_mm_madd_epi16(p45, k2a), _mm_madd_epi16(p67, k2b)), rnd), shift);
    o[1] = _mm_srai_epi32(_mm_add_epi32(_mm_add_epi32(_mm_madd_epi16(p45, k3a), _mm_madd_epi16(p67, k3b)), rnd), shift);
}

/* one dimensional transform across the 8 vectors, lane by lane. */
static inline void fdct_pass(__m128i *v, int pass)
{
    __m128i t0 = _mm_add_epi16(v[0], v[7]), t7 = _mm_sub_epi16(v[0], v[7]);
    __m128i t1 = _mm_add_epi16(v[1], v[6]), t6 = _mm_sub_epi16(v[1], v[6]);
    __m128i t2 = _mm_add_epi16(v[2], v[5]), t5 = _mm_sub_epi16(v[2], v[5]);
    __m128i t3 = _mm_add_epi16(v[3], v[4]), t4 = _mm_sub_epi16(v[3], v[4]);
    __m128i t10 = _mm_add_epi16(t0, t3), t13 = _mm_sub_epi16(t0, t3);
    __m128i t11 = _mm_add_epi16(t1, t2), t12 = _mm_sub_epi16(t1, t2);
    __m128i o0, o4, lo[8], hi[8];
    int shift = pass == 1 ? CONST_BITS - PASS1_BITS : CONST_BITS + PASS1_BITS;

    if (pass == 1) {
        o0 = _mm_slli_epi16(_mm_add_epi16(t10, t11), PASS1_BITS);
        o4 = _mm_slli_epi16(_mm_sub_epi16(t10, t11), PASS1_BITS);
    } else {
        const __m128i two = _mm_set1_epi16(1 << (PASS1_BITS - 1));
        o0 = _mm_srai_epi16(_mm_add_epi16(_mm_add_epi16(t10, t11), two), PASS1_BITS);
        o4 = _mm_srai_epi16(_mm_add_epi16(_mm_sub_epi16(t10, t11), two), PASS1_BITS);
    }
    fdct_half(_mm_unpacklo_epi16(t13, t12), _mm_unpacklo_epi16(t4, t5), _mm_unpacklo_epi16(t6, t7), shift, lo);
    fdct_half(_mm_unpackhi_epi16(t13, t12), _mm_unpackhi_epi16(t4, t5), _mm_unpackhi_epi16(t6, t7), shift, hi);
    v[0] = o0;
    v[4] = o4;
    v[1] = _mm_packs_epi32(lo[1], hi[1]);
    v[2] = _mm_packs_epi32(lo[2], hi[2]);
    v[3] = _mm_packs_epi32(lo[3], hi[3]);
    v[5] = _mm_packs_epi32(lo[5], hi[5]);
    v[6] = _mm_packs_epi32(lo[6], hi[6]);
    v[7] = _mm_packs_epi32(lo[7], hi[7]);
}

static void fdct_quant_sse2(const uint8_t *src, size_t stride, const MimeJpegQuantizer *qz, int16_t *out)
{
    const __m128i z = _mm_setzero_si128();
    const __m128i c128 = _mm_set1_epi16(128);
    __m128i v[8];
    for (int i = 0; i < 8; i++)
        v[i] = _mm_sub_epi16(_mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(src + (size_t)i * stride)), z), c128);
    transpose8(v);
    fdct_pass(v, 1);
    transpose8(v);
    fdct_pass(v, 2);
    for (int i = 0; i < 8; i++) {
        __m128i sgn = _mm_srai_epi16(v[i], 15);
        __m128i a = _mm_sub_epi16(_mm_xor_si128(v[i], sgn), sgn);
        __m128i n = _mm_add_epi16(a, _mm_loadu_si128((const __m128i *)(qz->bias + i * 8)));
        __m128i t1 = _mm_mulhi_epu16(n, _mm_loadu_si128((const __m128i *)(qz->mul + i * 8)));
        __m128i u = _mm_add_epi16(t1, _mm_srli_epi16(_mm_sub_epi16(n, t1), 1));
        __m128i q = _mm_mulhi_epu16(u, _mm_loadu_si128((const __m128i *)(qz->scale + i * 8)));
        _mm_storeu_si128((__m128i *)(out + i * 8), _mm_sub_epi16(_mm_xor_si128(q, sgn), sgn));
    }
}

#endif

void mime_jpeg_fdct_quant(const uint8_t *src, size_t stride, const MimeJpegQuantizer *qz, int16_t out[64])
{
#ifdef MIME_JPEG_SSE2
    fdct_quant_sse2(src, stride, qz, out);
#else
    int16_t f[64];
    fdct_scalar(src, stride, f);
    quantize_scalar(f, qz, out);
#endif
}
