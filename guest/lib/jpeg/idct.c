#include "internal.h"

/*
 * inverse dct with the 13 bit fixed point constants of the slow integer method, in the
 * Loeffler, Ligtenberg and Moschytz factorization: a column pass that keeps 2 extra bits, then a
 * row pass. arithmetic is done modulo 2^32 so that damaged coefficients cannot overflow, the
 * intermediate rows saturate to 16 bits, which is what the SSE2 code does with packssdw.
 */

#define CONST_BITS 13
#define PASS1_BITS 2

typedef uint32_t u32;

#define MUL(a, c) ((u32)(int32_t)(a) * (u32)(int32_t)(c))
#define SHR(x, n) ((int32_t)(x) >> (n))
#define DESCALE(x, n) SHR((x) + (1u << ((n) - 1)), n)

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

static inline int16_t dequant(int16_t c, uint16_t q)
{
    return (int16_t)((u32)(uint16_t)c * q);
}

static inline int16_t sat16(int32_t v)
{
    return v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)v;
}

static inline uint8_t clamp_sample(int32_t v)
{
    return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v;
}

/* one dimensional transform of 8 values, results before descaling. */
#define IDCT_1D(i0, i1, i2, i3, i4, i5, i6, i7, r0, r1, r2, r3, r4, r5, r6, r7)                          \
    do {                                                                                                  \
        u32 z1_ = MUL((i2) + (i6), FIX_0_541196100);                                                      \
        u32 tmp2_ = z1_ + MUL((i6), -FIX_1_847759065);                                                    \
        u32 tmp3_ = z1_ + MUL((i2), FIX_0_765366865);                                                     \
        u32 tmp0_ = (u32)((i0) + (i4)) << CONST_BITS;                                                     \
        u32 tmp1_ = (u32)((i0) - (i4)) << CONST_BITS;                                                     \
        u32 tmp10_ = tmp0_ + tmp3_, tmp13_ = tmp0_ - tmp3_;                                               \
        u32 tmp11_ = tmp1_ + tmp2_, tmp12_ = tmp1_ - tmp2_;                                               \
        u32 a1_ = (u32)((i7) + (i1)), a2_ = (u32)((i5) + (i3));                                           \
        u32 a3_ = (u32)((i7) + (i3)), a4_ = (u32)((i5) + (i1));                                           \
        u32 a5_ = (a3_ + a4_) * (u32)FIX_1_175875602;                                                     \
        u32 o0_ = MUL((i7), FIX_0_298631336), o1_ = MUL((i5), FIX_2_053119869);                           \
        u32 o2_ = MUL((i3), FIX_3_072711026), o3_ = MUL((i1), FIX_1_501321110);                           \
        a1_ *= (u32)-FIX_0_899976223;                                                                     \
        a2_ *= (u32)-FIX_2_562915447;                                                                     \
        a3_ = a3_ * (u32)-FIX_1_961570560 + a5_;                                                          \
        a4_ = a4_ * (u32)-FIX_0_390180644 + a5_;                                                          \
        o0_ += a1_ + a3_;                                                                                 \
        o1_ += a2_ + a4_;                                                                                 \
        o2_ += a2_ + a3_;                                                                                 \
        o3_ += a1_ + a4_;                                                                                 \
        r0 = tmp10_ + o3_;                                                                                \
        r7 = tmp10_ - o3_;                                                                                \
        r1 = tmp11_ + o2_;                                                                                \
        r6 = tmp11_ - o2_;                                                                                \
        r2 = tmp12_ + o1_;                                                                                \
        r5 = tmp12_ - o1_;                                                                                \
        r3 = tmp13_ + o0_;                                                                                \
        r4 = tmp13_ - o0_;                                                                                \
    } while (0)

[[maybe_unused]] static void idct8_scalar(const int16_t *coef, const uint16_t *qt, uint8_t *out, size_t stride)
{
    int16_t ws[64];

    for (int c = 0; c < 8; c++) {
        const int16_t *p = coef + c;
        if (!(p[8] | p[16] | p[24] | p[32] | p[40] | p[48] | p[56])) {
            int16_t v = sat16((int32_t)((u32)(int32_t)dequant(p[0], qt[c]) << PASS1_BITS));
            for (int r = 0; r < 8; r++)
                ws[r * 8 + c] = v;
            continue;
        }
        const uint16_t *q = qt + c;
        int32_t i0 = dequant(p[0], q[0]), i1 = dequant(p[8], q[8]), i2 = dequant(p[16], q[16]);
        int32_t i3 = dequant(p[24], q[24]), i4 = dequant(p[32], q[32]), i5 = dequant(p[40], q[40]);
        int32_t i6 = dequant(p[48], q[48]), i7 = dequant(p[56], q[56]);
        u32 r0, r1, r2, r3, r4, r5, r6, r7;
        IDCT_1D(i0, i1, i2, i3, i4, i5, i6, i7, r0, r1, r2, r3, r4, r5, r6, r7);
        ws[c] = sat16(DESCALE(r0, CONST_BITS - PASS1_BITS));
        ws[8 + c] = sat16(DESCALE(r1, CONST_BITS - PASS1_BITS));
        ws[16 + c] = sat16(DESCALE(r2, CONST_BITS - PASS1_BITS));
        ws[24 + c] = sat16(DESCALE(r3, CONST_BITS - PASS1_BITS));
        ws[32 + c] = sat16(DESCALE(r4, CONST_BITS - PASS1_BITS));
        ws[40 + c] = sat16(DESCALE(r5, CONST_BITS - PASS1_BITS));
        ws[48 + c] = sat16(DESCALE(r6, CONST_BITS - PASS1_BITS));
        ws[56 + c] = sat16(DESCALE(r7, CONST_BITS - PASS1_BITS));
    }
    for (int r = 0; r < 8; r++) {
        const int16_t *w = ws + r * 8;
        uint8_t *o = out + (size_t)r * stride;
        if (!(w[1] | w[2] | w[3] | w[4] | w[5] | w[6] | w[7])) {
            uint8_t v = clamp_sample(SHR((u32)(int32_t)w[0] + 16u, 5) + 128);
            for (int c = 0; c < 8; c++)
                o[c] = v;
            continue;
        }
        u32 r0, r1, r2, r3, r4, r5, r6, r7;
        IDCT_1D(w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7], r0, r1, r2, r3, r4, r5, r6, r7);
        o[0] = clamp_sample(DESCALE(r0, CONST_BITS + PASS1_BITS + 3) + 128);
        o[1] = clamp_sample(DESCALE(r1, CONST_BITS + PASS1_BITS + 3) + 128);
        o[2] = clamp_sample(DESCALE(r2, CONST_BITS + PASS1_BITS + 3) + 128);
        o[3] = clamp_sample(DESCALE(r3, CONST_BITS + PASS1_BITS + 3) + 128);
        o[4] = clamp_sample(DESCALE(r4, CONST_BITS + PASS1_BITS + 3) + 128);
        o[5] = clamp_sample(DESCALE(r5, CONST_BITS + PASS1_BITS + 3) + 128);
        o[6] = clamp_sample(DESCALE(r6, CONST_BITS + PASS1_BITS + 3) + 128);
        o[7] = clamp_sample(DESCALE(r7, CONST_BITS + PASS1_BITS + 3) + 128);
    }
}

#define FIX_0_211164243 1730
#define FIX_0_509795579 4176
#define FIX_0_601344887 4926
#define FIX_0_720959822 5906
#define FIX_0_850430095 6967
#define FIX_1_061594337 8697
#define FIX_1_272758580 10426
#define FIX_1_451774981 11893
#define FIX_2_172734803 17799
#define FIX_3_624509785 29692

/* the four outputs of the 4 point reduced transform of 8 inputs, the input at 4 is unused. */
static inline void idct4_1d(const int32_t in[8], int shift_dc, u32 res[4])
{
    u32 t0 = (u32)in[0] << (CONST_BITS + shift_dc);
    u32 t2 = MUL(in[2], FIX_1_847759065) + MUL(in[6], -FIX_0_765366865);
    u32 t10 = t0 + t2;
    u32 t12 = t0 - t2;
    u32 o0 = MUL(in[7], -FIX_0_211164243) + MUL(in[5], FIX_1_451774981) + MUL(in[3], -FIX_2_172734803) +
             MUL(in[1], FIX_1_061594337);
    u32 o2 = MUL(in[7], -FIX_0_509795579) + MUL(in[5], -FIX_0_601344887) + MUL(in[3], FIX_0_899976223) +
             MUL(in[1], FIX_2_562915447);
    res[0] = t10 + o2;
    res[3] = t10 - o2;
    res[1] = t12 + o0;
    res[2] = t12 - o0;
}

static void idct4_scalar(const int16_t *coef, const uint16_t *qt, uint8_t *out, size_t stride)
{
    int32_t ws[4 * 8];
    int32_t in[8];
    u32 res[4];

    for (int c = 0; c < 8; c++) {
        if (c == 4)
            continue;
        for (int r = 0; r < 8; r++)
            in[r] = dequant(coef[r * 8 + c], qt[r * 8 + c]);
        idct4_1d(in, 1, res);
        for (int r = 0; r < 4; r++)
            ws[r * 8 + c] = DESCALE(res[r], CONST_BITS - PASS1_BITS + 1);
    }
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 8; c++)
            in[c] = ws[r * 8 + c];
        idct4_1d(in, 1, res);
        uint8_t *o = out + (size_t)r * stride;
        for (int c = 0; c < 4; c++)
            o[c] = clamp_sample(DESCALE(res[c], CONST_BITS + PASS1_BITS + 3 + 1) + 128);
    }
}

static inline u32 idct2_odd(const int32_t in[8])
{
    return MUL(in[7], -FIX_0_720959822) + MUL(in[5], FIX_0_850430095) + MUL(in[3], -FIX_1_272758580) +
           MUL(in[1], FIX_3_624509785);
}

static void idct2_scalar(const int16_t *coef, const uint16_t *qt, uint8_t *out, size_t stride)
{
    int32_t ws[2 * 8];
    int32_t in[8];

    for (int c = 0; c < 8; c++) {
        if (c == 2 || c == 4 || c == 6)
            continue;
        for (int r = 0; r < 8; r++)
            in[r] = dequant(coef[r * 8 + c], qt[r * 8 + c]);
        u32 t10 = (u32)in[0] << (CONST_BITS + 2);
        u32 t0 = idct2_odd(in);
        ws[c] = DESCALE(t10 + t0, CONST_BITS - PASS1_BITS + 2);
        ws[8 + c] = DESCALE(t10 - t0, CONST_BITS - PASS1_BITS + 2);
    }
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 8; c++)
            in[c] = ws[r * 8 + c];
        u32 t10 = (u32)in[0] << (CONST_BITS + 2);
        u32 t0 = idct2_odd(in);
        uint8_t *o = out + (size_t)r * stride;
        o[0] = clamp_sample(DESCALE(t10 + t0, CONST_BITS + PASS1_BITS + 3 + 2) + 128);
        o[1] = clamp_sample(DESCALE(t10 - t0, CONST_BITS + PASS1_BITS + 3 + 2) + 128);
    }
}

static void idct1_scalar(const int16_t *coef, const uint16_t *qt, uint8_t *out, size_t stride)
{
    (void)stride;
    int32_t dc = dequant(coef[0], qt[0]);
    out[0] = clamp_sample(DESCALE((u32)dc, 3) + 128);
}

#ifdef MIME_JPEG_SSE2

#define K2(a, b) _mm_set1_epi32((int)(((u32)(uint16_t)(int16_t)(b) << 16) | (u32)(uint16_t)(int16_t)(a)))

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

/* the transform of 4 lanes, results descaled by shift with rounding, as 32 bit values. */
static inline void idct_half(__m128i p26, __m128i p04, __m128i p75, __m128i p31, int shift, __m128i d[8])
{
    const __m128i c_a = K2(FIX_0_541196100 + FIX_0_765366865, FIX_0_541196100);
    const __m128i c_b = K2(FIX_0_541196100, FIX_0_541196100 - FIX_1_847759065);
    const __m128i c_p = K2(1 << CONST_BITS, 1 << CONST_BITS);
    const __m128i c_m = K2(1 << CONST_BITS, -(1 << CONST_BITS));
    /* the odd sums, weights of the inputs 7, 5, 3 and 1 with the shared terms folded in. */
    const __m128i k0a = K2(-11363, 9633), k0b = K2(-6436, 2260);
    const __m128i k1a = K2(9633, 2261), k1b = K2(-11362, 6437);
    const __m128i k2a = K2(-6436, -11362), k2b = K2(-2259, 9633);
    const __m128i k3a = K2(2260, 6437), k3b = K2(9633, 11363);
    const __m128i rnd = _mm_set1_epi32(1 << (shift - 1));
    __m128i tmp3 = _mm_madd_epi16(p26, c_a);
    __m128i tmp2 = _mm_madd_epi16(p26, c_b);
    __m128i tmp0 = _mm_madd_epi16(p04, c_p);
    __m128i tmp1 = _mm_madd_epi16(p04, c_m);
    __m128i t10 = _mm_add_epi32(_mm_add_epi32(tmp0, tmp3), rnd);
    __m128i t13 = _mm_add_epi32(_mm_sub_epi32(tmp0, tmp3), rnd);
    __m128i t11 = _mm_add_epi32(_mm_add_epi32(tmp1, tmp2), rnd);
    __m128i t12 = _mm_add_epi32(_mm_sub_epi32(tmp1, tmp2), rnd);
    __m128i o0 = _mm_add_epi32(_mm_madd_epi16(p75, k0a), _mm_madd_epi16(p31, k0b));
    __m128i o1 = _mm_add_epi32(_mm_madd_epi16(p75, k1a), _mm_madd_epi16(p31, k1b));
    __m128i o2 = _mm_add_epi32(_mm_madd_epi16(p75, k2a), _mm_madd_epi16(p31, k2b));
    __m128i o3 = _mm_add_epi32(_mm_madd_epi16(p75, k3a), _mm_madd_epi16(p31, k3b));
    d[0] = _mm_srai_epi32(_mm_add_epi32(t10, o3), shift);
    d[7] = _mm_srai_epi32(_mm_sub_epi32(t10, o3), shift);
    d[1] = _mm_srai_epi32(_mm_add_epi32(t11, o2), shift);
    d[6] = _mm_srai_epi32(_mm_sub_epi32(t11, o2), shift);
    d[2] = _mm_srai_epi32(_mm_add_epi32(t12, o1), shift);
    d[5] = _mm_srai_epi32(_mm_sub_epi32(t12, o1), shift);
    d[3] = _mm_srai_epi32(_mm_add_epi32(t13, o0), shift);
    d[4] = _mm_srai_epi32(_mm_sub_epi32(t13, o0), shift);
}

/* one dimensional transform of the 8 vectors in v, across the vectors, lane by lane. */
static inline void idct_pass(__m128i *v, int shift)
{
    __m128i lo[8], hi[8];
    idct_half(_mm_unpacklo_epi16(v[2], v[6]), _mm_unpacklo_epi16(v[0], v[4]), _mm_unpacklo_epi16(v[7], v[5]),
              _mm_unpacklo_epi16(v[3], v[1]), shift, lo);
    idct_half(_mm_unpackhi_epi16(v[2], v[6]), _mm_unpackhi_epi16(v[0], v[4]), _mm_unpackhi_epi16(v[7], v[5]),
              _mm_unpackhi_epi16(v[3], v[1]), shift, hi);
    for (int i = 0; i < 8; i++)
        v[i] = _mm_packs_epi32(lo[i], hi[i]);
}

static void idct8_sse2(const int16_t *coef, const uint16_t *qt, uint8_t *out, size_t stride)
{
    __m128i v[8];
    for (int i = 0; i < 8; i++)
        v[i] = _mm_mullo_epi16(_mm_loadu_si128((const __m128i *)(coef + i * 8)),
                               _mm_loadu_si128((const __m128i *)(qt + i * 8)));
    idct_pass(v, CONST_BITS - PASS1_BITS);
    transpose8(v);
    idct_pass(v, CONST_BITS + PASS1_BITS + 3);
    transpose8(v);
    const __m128i bias = _mm_set1_epi16(128);
    for (int i = 0; i < 8; i += 2) {
        __m128i p = _mm_packus_epi16(_mm_adds_epi16(v[i], bias), _mm_adds_epi16(v[i + 1], bias));
        _mm_storel_epi64((__m128i *)(out + (size_t)i * stride), p);
        _mm_storel_epi64((__m128i *)(out + (size_t)(i + 1) * stride), _mm_srli_si128(p, 8));
    }
}

#endif

static inline bool block_ac_zero(const int16_t *coef)
{
#ifdef MIME_JPEG_SSE2
    __m128i a = _mm_loadu_si128((const __m128i *)(coef + 8));
    for (int i = 2; i < 8; i++)
        a = _mm_or_si128(a, _mm_loadu_si128((const __m128i *)(coef + i * 8)));
    a = _mm_or_si128(a, _mm_srli_si128(_mm_loadu_si128((const __m128i *)coef), 2));
    return _mm_movemask_epi8(_mm_cmpeq_epi8(a, _mm_setzero_si128())) == 0xffff;
#else
    uint16_t a = 0;
    for (int i = 1; i < 64; i++)
        a |= (uint16_t)coef[i];
    return a == 0;
#endif
}

void mime_jpeg_idct_blocks(int dct, const int16_t *coef, size_t nblocks, const uint16_t *qt, uint8_t *out,
                           size_t stride)
{
    switch (dct) {
    case 8:
        for (size_t i = 0; i < nblocks; i++, coef += 64, out += 8) {
            if (block_ac_zero(coef)) {
                uint8_t v = clamp_sample(SHR((u32)(int32_t)dequant(coef[0], qt[0]) + 4u, 3) + 128);
#ifdef MIME_JPEG_SSE2
                __m128i fill = _mm_set1_epi8((char)v);
                for (int r = 0; r < 8; r++)
                    _mm_storel_epi64((__m128i *)(out + (size_t)r * stride), fill);
#else
                for (int r = 0; r < 8; r++)
                    __builtin_memset(out + (size_t)r * stride, v, 8);
#endif
                continue;
            }
#ifdef MIME_JPEG_SSE2
            idct8_sse2(coef, qt, out, stride);
#else
            idct8_scalar(coef, qt, out, stride);
#endif
        }
        break;
    case 4:
        for (size_t i = 0; i < nblocks; i++, coef += 64, out += 4)
            idct4_scalar(coef, qt, out, stride);
        break;
    case 2:
        for (size_t i = 0; i < nblocks; i++, coef += 64, out += 2)
            idct2_scalar(coef, qt, out, stride);
        break;
    default:
        for (size_t i = 0; i < nblocks; i++, coef += 64, out += 1)
            idct1_scalar(coef, qt, out, stride);
        break;
    }
}
