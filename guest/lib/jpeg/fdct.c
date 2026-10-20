/*
 * the forward transform of a block and its quantization. the transform is the transpose of the one in idct.c: the
 * sums of the samples at n and 7 minus n give the even coefficients and their differences the odd ones, the odd part
 * being a 4 by 4 matrix of cosines.
 */
#include "internal.h"

#define C1 0.98078528f
#define C2 0.92387953f
#define C3 0.83146961f
#define C4 0.70710678f
#define C5 0.55557023f
#define C6 0.38268343f
#define C7 0.19509032f

void jpeg_fdct_prepare(const uint16_t *qtable, float *inv_quant)
{
    /* the two passes give four times the coefficient of the definition. */
    for (int i = 0; i < 64; i++)
        inv_quant[i] = 0.25f / (float)qtable[i];
}

#ifndef MIME_JPEG_SSE2
static inline void fdct8(const float *in, float *out, int step)
{
    float x0 = in[0], x1 = in[step], x2 = in[2 * step], x3 = in[3 * step];
    float x4 = in[4 * step], x5 = in[5 * step], x6 = in[6 * step], x7 = in[7 * step];
    float s0 = x0 + x7, s1 = x1 + x6, s2 = x2 + x5, s3 = x3 + x4;
    float d0 = x0 - x7, d1 = x1 - x6, d2 = x2 - x5, d3 = x3 - x4;
    float t0 = s0 - s3, t1 = s1 - s2;
    out[0] = (s0 + s1 + s2 + s3) * C4;
    out[4 * step] = (s0 - s1 - s2 + s3) * C4;
    out[2 * step] = t0 * C2 + t1 * C6;
    out[6 * step] = t0 * C6 - t1 * C2;
    out[step] = d0 * C1 + d1 * C3 + d2 * C5 + d3 * C7;
    out[3 * step] = d0 * C3 - d1 * C7 - d2 * C1 - d3 * C5;
    out[5 * step] = d0 * C5 - d1 * C1 + d2 * C7 + d3 * C3;
    out[7 * step] = d0 * C7 - d1 * C5 + d2 * C3 - d3 * C1;
}

static inline int round_away(float v)
{
    return v >= 0 ? (int)(v + 0.5f) : -(int)(0.5f - v);
}
#endif

#ifdef MIME_JPEG_SSE2
/* the same transform on four columns at once, in the same order of operations. */
static inline void fdct8_vec(const __m128 *x, __m128 *y)
{
    const __m128 c1 = _mm_set1_ps(C1), c2 = _mm_set1_ps(C2), c3 = _mm_set1_ps(C3), c4 = _mm_set1_ps(C4);
    const __m128 c5 = _mm_set1_ps(C5), c6 = _mm_set1_ps(C6), c7 = _mm_set1_ps(C7);
    __m128 s0 = _mm_add_ps(x[0], x[7]), s1 = _mm_add_ps(x[1], x[6]), s2 = _mm_add_ps(x[2], x[5]);
    __m128 s3 = _mm_add_ps(x[3], x[4]);
    __m128 d0 = _mm_sub_ps(x[0], x[7]), d1 = _mm_sub_ps(x[1], x[6]), d2 = _mm_sub_ps(x[2], x[5]);
    __m128 d3 = _mm_sub_ps(x[3], x[4]);
    __m128 t0 = _mm_sub_ps(s0, s3), t1 = _mm_sub_ps(s1, s2);
    y[0] = _mm_mul_ps(_mm_add_ps(_mm_add_ps(_mm_add_ps(s0, s1), s2), s3), c4);
    y[4] = _mm_mul_ps(_mm_add_ps(_mm_sub_ps(_mm_sub_ps(s0, s1), s2), s3), c4);
    y[2] = _mm_add_ps(_mm_mul_ps(t0, c2), _mm_mul_ps(t1, c6));
    y[6] = _mm_sub_ps(_mm_mul_ps(t0, c6), _mm_mul_ps(t1, c2));
    y[1] = _mm_add_ps(_mm_add_ps(_mm_add_ps(_mm_mul_ps(d0, c1), _mm_mul_ps(d1, c3)), _mm_mul_ps(d2, c5)),
                      _mm_mul_ps(d3, c7));
    y[3] = _mm_sub_ps(_mm_sub_ps(_mm_sub_ps(_mm_mul_ps(d0, c3), _mm_mul_ps(d1, c7)), _mm_mul_ps(d2, c1)),
                      _mm_mul_ps(d3, c5));
    y[5] = _mm_add_ps(_mm_add_ps(_mm_sub_ps(_mm_mul_ps(d0, c5), _mm_mul_ps(d1, c1)), _mm_mul_ps(d2, c7)),
                      _mm_mul_ps(d3, c3));
    y[7] = _mm_sub_ps(_mm_add_ps(_mm_sub_ps(_mm_mul_ps(d0, c7), _mm_mul_ps(d1, c5)), _mm_mul_ps(d2, c3)),
                      _mm_mul_ps(d3, c1));
}

/* four coefficients scaled to steps, rounded half away from zero, as 32 bit integers. */
static inline __m128i quantize4(__m128 v, __m128 inv)
{
    __m128 x = _mm_mul_ps(v, inv);
    __m128 negative = _mm_cmplt_ps(x, _mm_setzero_ps());
    __m128 magnitude = _mm_andnot_ps(_mm_set1_ps(-0.0f), x);
    __m128i t = _mm_cvttps_epi32(_mm_add_ps(magnitude, _mm_set1_ps(0.5f)));
    __m128i m = _mm_castps_si128(negative);
    return _mm_sub_epi32(_mm_xor_si128(t, m), m);
}

void jpeg_fdct_quant(const uint8_t *samples, size_t stride, const float *inv_quant, int16_t *out)
{
    __m128 a[2][8], b[2][8], c[2][8], d[2][8];
    const __m128i zero = _mm_setzero_si128();
    const __m128 center = _mm_set1_ps(128.0f);
    for (int y = 0; y < 8; y++) {
        __m128i p = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(samples + y * stride)), zero);
        a[0][y] = _mm_sub_ps(_mm_cvtepi32_ps(_mm_unpacklo_epi16(p, zero)), center);
        a[1][y] = _mm_sub_ps(_mm_cvtepi32_ps(_mm_unpackhi_epi16(p, zero)), center);
    }
    /* down the columns, half 0 being the columns 0 to 3. */
    fdct8_vec(a[0], b[0]);
    fdct8_vec(a[1], b[1]);
    /* along the rows: the lanes become the vertical frequencies. */
    for (int h = 0; h < 2; h++) {
        for (int g = 0; g < 2; g++) {
            __m128 p = b[h][4 * g], q = b[h][4 * g + 1], r = b[h][4 * g + 2], s = b[h][4 * g + 3];
            _MM_TRANSPOSE4_PS(p, q, r, s);
            c[g][4 * h] = p;
            c[g][4 * h + 1] = q;
            c[g][4 * h + 2] = r;
            c[g][4 * h + 3] = s;
        }
    }
    fdct8_vec(c[0], d[0]);
    fdct8_vec(c[1], d[1]);
    /* rows of coefficients again. */
    __m128 e[8][2];
    for (int g = 0; g < 2; g++) {
        for (int h = 0; h < 2; h++) {
            __m128 p = d[g][4 * h], q = d[g][4 * h + 1], r = d[g][4 * h + 2], s = d[g][4 * h + 3];
            _MM_TRANSPOSE4_PS(p, q, r, s);
            e[4 * g][h] = p;
            e[4 * g + 1][h] = q;
            e[4 * g + 2][h] = r;
            e[4 * g + 3][h] = s;
        }
    }
    /* the ranges of the sequential process: 11 bits for the DC difference and 10 for the others. */
    const __m128i limit = _mm_set1_epi16(1023), limit_dc = _mm_set_epi16(1023, 1023, 1023, 1023, 1023, 1023, 1023, 2047);
    for (int u = 0; u < 8; u++) {
        __m128i q = _mm_packs_epi32(quantize4(e[u][0], _mm_loadu_ps(inv_quant + 8 * u)),
                                    quantize4(e[u][1], _mm_loadu_ps(inv_quant + 8 * u + 4)));
        __m128i hi = u ? limit : limit_dc;
        q = _mm_min_epi16(_mm_max_epi16(q, _mm_sub_epi16(zero, hi)), hi);
        _mm_storeu_si128((__m128i *)(out + 8 * u), q);
    }
}
#else
void jpeg_fdct_quant(const uint8_t *samples, size_t stride, const float *inv_quant, int16_t *out)
{
    float w[64], t[64];
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            w[y * 8 + x] = (float)samples[y * stride + x] - 128.0f;
    for (int x = 0; x < 8; x++)
        fdct8(w + x, t + x, 8);
    for (int y = 0; y < 8; y++)
        fdct8(t + y * 8, w + y * 8, 1);
    for (int i = 0; i < 64; i++) {
        int q = round_away(w[i] * inv_quant[i]);
        /* the ranges of the sequential process: 11 bits for the DC difference and 10 for the others. */
        int limit = i == 0 ? 2047 : 1023;
        out[i] = (int16_t)(q < -limit ? -limit : q > limit ? limit : q);
    }
}
#endif
