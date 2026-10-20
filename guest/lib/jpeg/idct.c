/*
 * the inverse transform of a block, in floating point. a block is dequantized on loading, transformed down the
 * columns and then along the rows, the two passes being the same one dimensional transform of the even and odd
 * parts of the eight inputs: x[n] is the sum over the coefficients X[u] of c(u) X[u] cos((2n+1) u pi / 16), and the
 * outputs n and 7 - n differ only in the sign of the odd coefficients' share.
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

/*
 * the smaller decodes are the averages of the groups of 8 / n samples of the full decode, which is to say the full
 * transform followed by a box filter, written as one matrix: row i holds, for each frequency u, the mean over group i
 * of the one dimensional basis function c(u) / 2 * cos((2x + 1) u pi / 16).
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
