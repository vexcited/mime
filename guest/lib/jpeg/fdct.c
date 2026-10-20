/*
 * the forward transform of a block and its quantization. the transform is the transpose of the one in idct.c: the
 * sums of the samples at n and 7 - n give the even coefficients and their differences the odd ones, the odd part
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
    /* the two passes leave four times the coefficient, whose normalization is one quarter. */
    for (int i = 0; i < 64; i++)
        inv_quant[i] = 0.25f / (float)qtable[i];
}

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

void jpeg_fdct_quant(const uint8_t *samples, size_t stride, const float *inv_quant, int16_t *out)
{
    float w[64], t[64];
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            w[y * 8 + x] = (float)samples[y * stride + x] - 128.0f;
    for (int y = 0; y < 8; y++)
        fdct8(w + y * 8, t + y * 8, 1);
    for (int x = 0; x < 8; x++)
        fdct8(t + x, w + x, 8);
    for (int i = 0; i < 64; i++) {
        int q = round_away(w[i] * inv_quant[i]);
        /* the ranges of the sequential process: 11 bits for the DC difference and 10 for the others. */
        int limit = i == 0 ? 2047 : 1023;
        out[i] = (int16_t)(q < -limit ? -limit : q > limit ? limit : q);
    }
}
