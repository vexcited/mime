/*
 * the inverse transform of a block, in floating point. a block is dequantized on loading, transformed down the
 * columns and then along the rows, the two passes being the same one dimensional transform of the even and odd
 * parts of the eight inputs. the output n is the sum over the coefficients u of c_u X_u cos of the angle
 * 2n plus 1 times u pi over 16, and the output 7 minus n differs only in the sign of the share of the odd
 * coefficients.
 *
 * the transforms to a smaller size give the averages of the groups of samples of the full one, see below.
 */
#include "internal.h"

#define C1 0.98078528f
#define C2 0.92387953f
#define C3 0.83146961f
#define C4 0.70710678f
#define C5 0.55557023f
#define C6 0.38268343f
#define C7 0.19509032f

void jpeg_idct_prepare(const uint16_t *qtable, float *dequant)
{
    /* the factor of a quarter is the two normalizations of one half each. */
    for (int i = 0; i < 64; i++)
        dequant[i] = (float)qtable[i] * 0.25f;
}

static inline int to_sample(float v)
{
    int t = (int)(v + 128.5f);
    return t < 0 ? 0 : t > 255 ? 255 : t;
}

#ifndef MIME_JPEG_SSE2
static inline void idct8(const float *in, float *out, int step)
{
    float x0 = in[0], x1 = in[step], x2 = in[2 * step], x3 = in[3 * step];
    float x4 = in[4 * step], x5 = in[5 * step], x6 = in[6 * step], x7 = in[7 * step];
    float a = (x0 + x4) * C4, b = (x0 - x4) * C4;
    float p = x2 * C2 + x6 * C6, q = x2 * C6 - x6 * C2;
    float e0 = a + p, e1 = b + q, e2 = b - q, e3 = a - p;
    float o0 = x1 * C1 + x3 * C3 + x5 * C5 + x7 * C7;
    float o1 = x1 * C3 - x3 * C7 - x5 * C1 - x7 * C5;
    float o2 = x1 * C5 - x3 * C1 + x5 * C7 + x7 * C3;
    float o3 = x1 * C7 - x3 * C5 + x5 * C3 - x7 * C1;
    out[0] = e0 + o0;
    out[7 * step] = e0 - o0;
    out[step] = e1 + o1;
    out[6 * step] = e1 - o1;
    out[2 * step] = e2 + o2;
    out[5 * step] = e2 - o2;
    out[3 * step] = e3 + o3;
    out[4 * step] = e3 - o3;
}
#endif

#ifdef MIME_JPEG_SSE2
/* the same transform on four columns at once, in the same order of operations. */
static inline void idct8_vec(const __m128 *x, __m128 *y)
{
    const __m128 c1 = _mm_set1_ps(C1), c2 = _mm_set1_ps(C2), c3 = _mm_set1_ps(C3), c4 = _mm_set1_ps(C4);
    const __m128 c5 = _mm_set1_ps(C5), c6 = _mm_set1_ps(C6), c7 = _mm_set1_ps(C7);
    __m128 a = _mm_mul_ps(_mm_add_ps(x[0], x[4]), c4), b = _mm_mul_ps(_mm_sub_ps(x[0], x[4]), c4);
    __m128 p = _mm_add_ps(_mm_mul_ps(x[2], c2), _mm_mul_ps(x[6], c6));
    __m128 q = _mm_sub_ps(_mm_mul_ps(x[2], c6), _mm_mul_ps(x[6], c2));
    __m128 e0 = _mm_add_ps(a, p), e1 = _mm_add_ps(b, q), e2 = _mm_sub_ps(b, q), e3 = _mm_sub_ps(a, p);
    __m128 o0 = _mm_add_ps(_mm_add_ps(_mm_add_ps(_mm_mul_ps(x[1], c1), _mm_mul_ps(x[3], c3)), _mm_mul_ps(x[5], c5)),
                           _mm_mul_ps(x[7], c7));
    __m128 o1 = _mm_sub_ps(_mm_sub_ps(_mm_sub_ps(_mm_mul_ps(x[1], c3), _mm_mul_ps(x[3], c7)), _mm_mul_ps(x[5], c1)),
                           _mm_mul_ps(x[7], c5));
    __m128 o2 = _mm_add_ps(_mm_add_ps(_mm_sub_ps(_mm_mul_ps(x[1], c5), _mm_mul_ps(x[3], c1)), _mm_mul_ps(x[5], c7)),
                           _mm_mul_ps(x[7], c3));
    __m128 o3 = _mm_sub_ps(_mm_add_ps(_mm_sub_ps(_mm_mul_ps(x[1], c7), _mm_mul_ps(x[3], c5)), _mm_mul_ps(x[5], c3)),
                           _mm_mul_ps(x[7], c1));
    y[0] = _mm_add_ps(e0, o0);
    y[7] = _mm_sub_ps(e0, o0);
    y[1] = _mm_add_ps(e1, o1);
    y[6] = _mm_sub_ps(e1, o1);
    y[2] = _mm_add_ps(e2, o2);
    y[5] = _mm_sub_ps(e2, o2);
    y[3] = _mm_add_ps(e3, o3);
    y[4] = _mm_sub_ps(e3, o3);
}

/* the same with the last four inputs zero, which gives the same results with fewer operations. */
static inline void idct8_vec_low(const __m128 *x, __m128 *y)
{
    const __m128 c1 = _mm_set1_ps(C1), c2 = _mm_set1_ps(C2), c3 = _mm_set1_ps(C3), c4 = _mm_set1_ps(C4);
    const __m128 c5 = _mm_set1_ps(C5), c6 = _mm_set1_ps(C6), c7 = _mm_set1_ps(C7);
    __m128 a = _mm_mul_ps(x[0], c4);
    __m128 p = _mm_mul_ps(x[2], c2), q = _mm_mul_ps(x[2], c6);
    __m128 e0 = _mm_add_ps(a, p), e1 = _mm_add_ps(a, q), e2 = _mm_sub_ps(a, q), e3 = _mm_sub_ps(a, p);
    __m128 o0 = _mm_add_ps(_mm_mul_ps(x[1], c1), _mm_mul_ps(x[3], c3));
    __m128 o1 = _mm_sub_ps(_mm_mul_ps(x[1], c3), _mm_mul_ps(x[3], c7));
    __m128 o2 = _mm_sub_ps(_mm_mul_ps(x[1], c5), _mm_mul_ps(x[3], c1));
    __m128 o3 = _mm_sub_ps(_mm_mul_ps(x[1], c7), _mm_mul_ps(x[3], c5));
    y[0] = _mm_add_ps(e0, o0);
    y[7] = _mm_sub_ps(e0, o0);
    y[1] = _mm_add_ps(e1, o1);
    y[6] = _mm_sub_ps(e1, o1);
    y[2] = _mm_add_ps(e2, o2);
    y[5] = _mm_sub_ps(e2, o2);
    y[3] = _mm_add_ps(e3, o3);
    y[4] = _mm_sub_ps(e3, o3);
}

/* a row of eight coefficients, dequantized, as two vectors of floats. */
static inline void load_row(const int16_t *coef, const float *dq, __m128 *lo, __m128 *hi)
{
    __m128i r = _mm_loadu_si128((const __m128i *)coef);
    __m128i l = _mm_srai_epi32(_mm_unpacklo_epi16(r, r), 16), h = _mm_srai_epi32(_mm_unpackhi_epi16(r, r), 16);
    *lo = _mm_mul_ps(_mm_cvtepi32_ps(l), _mm_loadu_ps(dq));
    *hi = _mm_mul_ps(_mm_cvtepi32_ps(h), _mm_loadu_ps(dq + 4));
}

static void idct_full(const int16_t *coef, const float *dq, int last, uint8_t *out, size_t stride)
{
    __m128 in[2][8], t[2][8], u[2][8], r[2][8];
    /*
     * the first ten coefficients in zigzag order lie in the four first rows and columns, and then the second half
     * of the columns and the second half of the rows are zero.
     */
    bool low = last < 10;
    __m128 zero = _mm_setzero_ps();
    for (int k = 0; k < (low ? 4 : 8); k++)
        load_row(coef + 8 * k, dq + 8 * k, &in[0][k], &in[1][k]);
    /* down the columns: half 0 holds the columns 0 to 3 of every row, half 1 the others. */
    if (low) {
        idct8_vec_low(in[0], t[0]);
        for (int k = 0; k < 8; k++)
            t[1][k] = zero;
    } else {
        idct8_vec(in[0], t[0]);
        idct8_vec(in[1], t[1]);
    }
    /* along the rows, which are now the lanes: turn four by four blocks around. */
    for (int h = 0; h < (low ? 1 : 2); h++) {
        for (int g = 0; g < 2; g++) {
            __m128 a = t[h][4 * g], b = t[h][4 * g + 1], c = t[h][4 * g + 2], d = t[h][4 * g + 3];
            _MM_TRANSPOSE4_PS(a, b, c, d);
            u[g][4 * h] = a;
            u[g][4 * h + 1] = b;
            u[g][4 * h + 2] = c;
            u[g][4 * h + 3] = d;
        }
    }
    if (low) {
        idct8_vec_low(u[0], r[0]);
        idct8_vec_low(u[1], r[1]);
    } else {
        idct8_vec(u[0], r[0]);
        idct8_vec(u[1], r[1]);
    }
    /* back to rows of pixels. */
    const __m128 bias = _mm_set1_ps(128.5f);
    for (int g = 0; g < 2; g++) {
        for (int h = 0; h < 2; h++) {
            __m128 a = r[g][4 * h], b = r[g][4 * h + 1], c = r[g][4 * h + 2], d = r[g][4 * h + 3];
            _MM_TRANSPOSE4_PS(a, b, c, d);
            t[h][4 * g] = _mm_add_ps(a, bias);
            t[h][4 * g + 1] = _mm_add_ps(b, bias);
            t[h][4 * g + 2] = _mm_add_ps(c, bias);
            t[h][4 * g + 3] = _mm_add_ps(d, bias);
        }
    }
    for (int y = 0; y < 8; y++) {
        __m128i v = _mm_packs_epi32(_mm_cvttps_epi32(t[0][y]), _mm_cvttps_epi32(t[1][y]));
        _mm_storel_epi64((__m128i *)(out + y * stride), _mm_packus_epi16(v, v));
    }
}
#else
static void idct_full(const int16_t *coef, const float *dq, int last, uint8_t *out, size_t stride)
{
    float w[64], t[64];
    (void)last;
    for (int i = 0; i < 64; i++)
        w[i] = (float)coef[i] * dq[i];
    for (int v = 0; v < 8; v++) {
        if (!(coef[8 + v] | coef[16 + v] | coef[24 + v] | coef[32 + v] | coef[40 + v] | coef[48 + v] | coef[56 + v])) {
            float dc = w[v] * C4;
            for (int n = 0; n < 8; n++)
                t[n * 8 + v] = dc;
        } else {
            idct8(w + v, t + v, 8);
        }
    }
    for (int y = 0; y < 8; y++) {
        float r[8];
        idct8(t + y * 8, r, 1);
        for (int x = 0; x < 8; x++)
            out[y * stride + x] = (uint8_t)to_sample(r[x]);
    }
}
#endif

/*
 * the smaller decodes are the averages of the groups of 8 / n samples of the full decode, which is to say the full
 * transform followed by a box filter, written as one matrix: row i holds, for each frequency u, the mean over group i
 * of the one dimensional basis function, which is c_u over 2 times the cosine of the angle 2x plus 1 times u pi
 * over 16.
 */
static const float r4[4][8] = {
    {0.35355339f, 0.45306372f, 0.32664074f, 0.15909482f, 0.0f, -0.10630376f, -0.13529903f, -0.09011998f},
    {0.35355339f, 0.18766514f, -0.32664074f, -0.38408888f, 0.0f, 0.25663998f, 0.13529903f, -0.03732892f},
    {0.35355339f, -0.18766514f, -0.32664074f, 0.38408888f, 0.0f, -0.25663998f, 0.13529903f, 0.03732892f},
    {0.35355339f, -0.45306372f, 0.32664074f, -0.15909482f, 0.0f, 0.10630376f, -0.13529903f, 0.09011998f},
};
static const float r2[2][8] = {
    {0.35355339f, 0.32036443f, 0.0f, -0.11249703f, 0.0f, 0.07516811f, 0.0f, -0.06372445f},
    {0.35355339f, -0.32036443f, 0.0f, 0.11249703f, 0.0f, -0.07516811f, 0.0f, 0.06372445f},
};

static void idct_reduced(const int16_t *coef, const float *dq, int n, uint8_t *out, size_t stride)
{
    float w[64], t[4][8];
    const float *r = n == 4 ? &r4[0][0] : &r2[0][0];
    for (int i = 0; i < 64; i++)
        w[i] = (float)coef[i] * dq[i] * 4.0f; /* the table holds a quarter of the step, the matrices want all of it. */
    for (int y = 0; y < n; y++) {
        for (int v = 0; v < 8; v++) {
            float s = 0;
            for (int u = 0; u < 8; u++)
                s += r[y * 8 + u] * w[u * 8 + v];
            t[y][v] = s;
        }
    }
    for (int y = 0; y < n; y++) {
        for (int x = 0; x < n; x++) {
            float s = 0;
            for (int v = 0; v < 8; v++)
                s += r[x * 8 + v] * t[y][v];
            out[y * stride + x] = (uint8_t)to_sample(s);
        }
    }
}

void jpeg_idct(const int16_t *coef, const float *dq, int last, int n, uint8_t *out, size_t stride)
{
    if (last == 0) {
        /* only the DC coefficient: a flat block of the mean. */
        uint8_t v = (uint8_t)to_sample((float)coef[0] * dq[0] * 0.5f);
        for (int y = 0; y < n; y++)
            __builtin_memset(out + y * stride, v, (size_t)n);
        return;
    }
    switch (n) {
    case 8:
        idct_full(coef, dq, last, out, stride);
        break;
    case 1:
        out[0] = (uint8_t)to_sample((float)coef[0] * dq[0] * 0.5f);
        break;
    default:
        idct_reduced(coef, dq, n, out, stride);
        break;
    }
}
