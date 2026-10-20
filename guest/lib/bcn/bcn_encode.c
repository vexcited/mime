#include "bcn.h"

/*
 * bc1 range fit: the principal axis of the block colors gives the end points,
 * which are then refined by least squares against the indices they produce.
 * only 32 bit floats are used.
 */

typedef struct Rgb {
    int r, g, b;
} Rgb;

static inline Rgb px_rgb(uint32_t p)
{
    Rgb c = {(int)((p >> 16) & 255), (int)((p >> 8) & 255), (int)(p & 255)};
    return c;
}

static inline unsigned quant5(float v)
{
    int q = (int)(v * (31.0f / 255.0f) + 0.5f);
    return (unsigned)(q < 0 ? 0 : q > 31 ? 31 : q);
}

static inline unsigned quant6(float v)
{
    int q = (int)(v * (63.0f / 255.0f) + 0.5f);
    return (unsigned)(q < 0 ? 0 : q > 63 ? 63 : q);
}

static inline unsigned pack565(unsigned r, unsigned g, unsigned b)
{
    return r << 11 | g << 5 | b;
}

static void unpack565(unsigned c, Rgb *out)
{
    unsigned r = c >> 11, g = (c >> 5) & 63, b = c & 31;
    out->r = (int)(r << 3 | r >> 2);
    out->g = (int)(g << 2 | g >> 4);
    out->b = (int)(b << 3 | b >> 2);
}

/* the palette a decoder builds from the two end points. */
static void palette_of(unsigned c0, unsigned c1, bool four, Rgb pal[4])
{
    Rgb a, b;
    unpack565(c0, &a);
    unpack565(c1, &b);
    pal[0] = a;
    pal[1] = b;
    if (four) {
        pal[2].r = (2 * a.r + b.r) * 21846 >> 16;
        pal[2].g = (2 * a.g + b.g) * 21846 >> 16;
        pal[2].b = (2 * a.b + b.b) * 21846 >> 16;
        pal[3].r = (a.r + 2 * b.r) * 21846 >> 16;
        pal[3].g = (a.g + 2 * b.g) * 21846 >> 16;
        pal[3].b = (a.b + 2 * b.b) * 21846 >> 16;
    } else {
        pal[2].r = (a.r + b.r) >> 1;
        pal[2].g = (a.g + b.g) >> 1;
        pal[2].b = (a.b + b.b) >> 1;
        pal[3].r = pal[3].g = pal[3].b = 0;
    }
}

static inline int dist2(Rgb a, Rgb b)
{
    int dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
    return dr * dr + dg * dg + db * db;
}

/* indices of the nearest palette entries and the total error. skip marks pixels without an index to choose. */
static int assign(const Rgb px[16], unsigned skip, const Rgb pal[4], unsigned entries, unsigned idx[16])
{
    int total = 0;
    for (int i = 0; i < 16; i++) {
        if (skip & (1u << i)) {
            idx[i] = 3;
            continue;
        }
        int best = dist2(px[i], pal[0]);
        unsigned bi = 0;
        for (unsigned k = 1; k < entries; k++) {
            int d = dist2(px[i], pal[k]);
            if (d < best) {
                best = d;
                bi = k;
            }
        }
        idx[i] = bi;
        total += best;
    }
    return total;
}

/* the bit layout of the indices is 0 c0, 1 c1, 2 and 3 between, written as the stream order. */
static uint32_t pack_indices(const unsigned idx[16])
{
    uint32_t bits = 0;
    for (int i = 0; i < 16; i++)
        bits |= (uint32_t)idx[i] << (2 * i);
    return bits;
}

static void write_bc1(uint8_t out[8], unsigned c0, unsigned c1, uint32_t bits)
{
    out[0] = (uint8_t)c0;
    out[1] = (uint8_t)(c0 >> 8);
    out[2] = (uint8_t)c1;
    out[3] = (uint8_t)(c1 >> 8);
    out[4] = (uint8_t)bits;
    out[5] = (uint8_t)(bits >> 8);
    out[6] = (uint8_t)(bits >> 16);
    out[7] = (uint8_t)(bits >> 24);
}

/* swaps the end points for the mode needed, and remaps the indices with them. */
static void order_endpoints(unsigned *c0, unsigned *c1, bool four, unsigned idx[16], unsigned skip)
{
    bool swap = four ? *c0 < *c1 : *c0 > *c1;
    if (!swap)
        return;
    unsigned t = *c0;
    *c0 = *c1;
    *c1 = t;
    for (int i = 0; i < 16; i++) {
        if (skip & (1u << i))
            continue;
        if (four) {
            static const unsigned map4[4] = {1, 0, 3, 2};
            idx[i] = map4[idx[i]];
        } else {
            static const unsigned map3[4] = {1, 0, 2, 3};
            idx[i] = map3[idx[i]];
        }
    }
}

/* the interpolation position of each index, in sixths: 0 and 6 are the end points. */
static inline float position(unsigned idx, bool four)
{
    if (four) {
        static const float t[4] = {0.0f, 1.0f, 1.0f / 3.0f, 2.0f / 3.0f};
        return t[idx];
    }
    static const float t3[4] = {0.0f, 1.0f, 0.5f, 0.0f};
    return t3[idx];
}

static inline int expand_q(int q, bool six)
{
    return six ? (q << 2 | q >> 4) : (q << 3 | q >> 2);
}

/* the value that palette entry k has for the end point values a and b of one channel. */
static inline int blend(int a, int b, unsigned k, bool four)
{
    if (k == 0)
        return a;
    if (k == 1)
        return b;
    if (four)
        return k == 2 ? (2 * a + b) * 21846 >> 16 : (a + 2 * b) * 21846 >> 16;
    return (a + b) >> 1;
}

/*
 * solid blocks: the channels are independent once the palette entry is chosen,
 * so every entry is tried with the best nearby quantised end points per channel.
 */
static void solid_block(Rgb c, bool four, unsigned *best0, unsigned *best1)
{
    const int target[3] = {c.r, c.g, c.b};
    int best_total = 0x7fffffff;
    *best0 = *best1 = 0;
    for (unsigned k = 0; k < (four ? 4u : 3u); k++) {
        int pick_a[3] = {0, 0, 0}, pick_b[3] = {0, 0, 0}, total = 0;
        for (int ch = 0; ch < 3; ch++) {
            bool six = ch == 1;
            int q = (int)(six ? quant6((float)target[ch]) : quant5((float)target[ch]));
            int max = six ? 63 : 31;
            int err = 0x7fffffff;
            for (int da = -2; da <= 2; da++)
                for (int db = -2; db <= 2; db++) {
                    int qa = q + da, qb = q + db;
                    if (qa < 0 || qa > max || qb < 0 || qb > max)
                        continue;
                    int v = blend(expand_q(qa, six), expand_q(qb, six), k, four) - target[ch];
                    if (v * v < err) {
                        err = v * v;
                        pick_a[ch] = qa;
                        pick_b[ch] = qb;
                    }
                }
            total += err;
        }
        if (total < best_total) {
            best_total = total;
            *best0 = pack565((unsigned)pick_a[0], (unsigned)pick_a[1], (unsigned)pick_a[2]);
            *best1 = pack565((unsigned)pick_b[0], (unsigned)pick_b[1], (unsigned)pick_b[2]);
        }
    }
    if (four ? *best0 < *best1 : *best0 > *best1) {
        unsigned t = *best0;
        *best0 = *best1;
        *best1 = t;
    }
}

void mime_bcn_encode_bc1_block(const uint32_t src[16], uint8_t out[8], bool alpha)
{
    Rgb px[16];
    unsigned skip = 0;
    for (int i = 0; i < 16; i++) {
        px[i] = px_rgb(src[i]);
        if (alpha && (src[i] >> 24) < 128)
            skip |= 1u << i;
    }
    unsigned opaque = 16 - (unsigned)__builtin_popcount(skip);
    bool four = skip == 0;
    if (!opaque) {
        write_bc1(out, 0, 0, 0xffffffffu);
        return;
    }

    /* statistics of the opaque pixels. */
    float mean[3] = {0, 0, 0};
    int lo[3] = {255, 255, 255}, hi[3] = {0, 0, 0};
    for (int i = 0; i < 16; i++) {
        if (skip & (1u << i))
            continue;
        int v[3] = {px[i].r, px[i].g, px[i].b};
        for (int c = 0; c < 3; c++) {
            mean[c] += (float)v[c];
            if (v[c] < lo[c])
                lo[c] = v[c];
            if (v[c] > hi[c])
                hi[c] = v[c];
        }
    }
    for (int c = 0; c < 3; c++)
        mean[c] /= (float)opaque;

    unsigned idx[16];
    unsigned c0 = 0, c1 = 0;
    if (lo[0] == hi[0] && lo[1] == hi[1] && lo[2] == hi[2]) {
        solid_block(px[(unsigned)__builtin_ctz(~skip)], four, &c0, &c1);
        Rgb pal[4];
        palette_of(c0, c1, four, pal);
        assign(px, skip, pal, four ? 4 : 3, idx);
        order_endpoints(&c0, &c1, four, idx, skip);
        write_bc1(out, c0, c1, pack_indices(idx));
        return;
    }

    float cov[6] = {0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 16; i++) {
        if (skip & (1u << i))
            continue;
        float d[3] = {(float)px[i].r - mean[0], (float)px[i].g - mean[1], (float)px[i].b - mean[2]};
        cov[0] += d[0] * d[0];
        cov[1] += d[0] * d[1];
        cov[2] += d[0] * d[2];
        cov[3] += d[1] * d[1];
        cov[4] += d[1] * d[2];
        cov[5] += d[2] * d[2];
    }
    float axis[3] = {(float)(hi[0] - lo[0]), (float)(hi[1] - lo[1]), (float)(hi[2] - lo[2])};
    for (int it = 0; it < 4; it++) {
        float x = cov[0] * axis[0] + cov[1] * axis[1] + cov[2] * axis[2];
        float y = cov[1] * axis[0] + cov[3] * axis[1] + cov[4] * axis[2];
        float z = cov[2] * axis[0] + cov[4] * axis[1] + cov[5] * axis[2];
        float m = x < 0 ? -x : x;
        if ((y < 0 ? -y : y) > m)
            m = y < 0 ? -y : y;
        if ((z < 0 ? -z : z) > m)
            m = z < 0 ? -z : z;
        if (m <= 0.0f)
            break;
        axis[0] = x / m;
        axis[1] = y / m;
        axis[2] = z / m;
    }
    float tmin = 1e30f, tmax = -1e30f;
    for (int i = 0; i < 16; i++) {
        if (skip & (1u << i))
            continue;
        float t = ((float)px[i].r - mean[0]) * axis[0] + ((float)px[i].g - mean[1]) * axis[1] +
                  ((float)px[i].b - mean[2]) * axis[2];
        if (t < tmin)
            tmin = t;
        if (t > tmax)
            tmax = t;
    }
    /* the axis is not unit length, the projection is divided by its squared length. */
    float norm = axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2];
    float e0[3], e1[3];
    for (int c = 0; c < 3; c++) {
        e0[c] = mean[c] + axis[c] * (tmax / norm);
        e1[c] = mean[c] + axis[c] * (tmin / norm);
    }

    int best_err = 0x7fffffff;
    unsigned best0 = 0, best1 = 0;
    unsigned best_idx[16] = {0};
    for (int round = 0; round < 3; round++) {
        c0 = pack565(quant5(e0[0]), quant6(e0[1]), quant5(e0[2]));
        c1 = pack565(quant5(e1[0]), quant6(e1[1]), quant5(e1[2]));
        /* the decoder takes the four color mode only when c0 is above c1 and the three color mode otherwise. */
        if (four ? c0 < c1 : c0 > c1) {
            unsigned t = c0;
            c0 = c1;
            c1 = t;
        }
        Rgb pal[4];
        palette_of(c0, c1, four, pal);
        int err = assign(px, skip, pal, four ? 4 : 3, idx);
        if (err < best_err) {
            best_err = err;
            best0 = c0;
            best1 = c1;
            __builtin_memcpy(best_idx, idx, sizeof(idx));
        } else {
            break;
        }
        if (!err)
            break;
        /* least squares end points for the current indices. */
        float a2 = 0, b2 = 0, ab = 0;
        float ax[3] = {0, 0, 0}, bx[3] = {0, 0, 0};
        for (int i = 0; i < 16; i++) {
            if (skip & (1u << i))
                continue;
            float t = position(idx[i], four);
            float a = 1.0f - t, b = t;
            float v[3] = {(float)px[i].r, (float)px[i].g, (float)px[i].b};
            a2 += a * a;
            b2 += b * b;
            ab += a * b;
            for (int c = 0; c < 3; c++) {
                ax[c] += a * v[c];
                bx[c] += b * v[c];
            }
        }
        float det = a2 * b2 - ab * ab;
        if (det < 1e-6f && det > -1e-6f)
            break;
        for (int c = 0; c < 3; c++) {
            e0[c] = (ax[c] * b2 - bx[c] * ab) / det;
            e1[c] = (bx[c] * a2 - ax[c] * ab) / det;
        }
    }
    __builtin_memcpy(idx, best_idx, sizeof(idx));
    order_endpoints(&best0, &best1, four, idx, skip);
    write_bc1(out, best0, best1, pack_indices(idx));
}

/* alpha. */

void mime_bcn_encode_alpha_block(const uint8_t alpha[16], uint8_t out[8])
{
    unsigned lo = 255, hi = 0, lo_in = 255, hi_in = 0;
    bool has0 = false, has255 = false;
    for (int i = 0; i < 16; i++) {
        unsigned a = alpha[i];
        if (a < lo)
            lo = a;
        if (a > hi)
            hi = a;
        if (a == 0)
            has0 = true;
        else if (a == 255)
            has255 = true;
        else {
            if (a < lo_in)
                lo_in = a;
            if (a > hi_in)
                hi_in = a;
        }
    }
    uint8_t best_out[8] = {0};
    unsigned best_err = 0xffffffffu;

    for (int mode = 0; mode < 2; mode++) {
        unsigned e0, e1;
        unsigned pal[8];
        if (mode == 0) {
            /* eight values, the first end point above the second. */
            e0 = hi;
            e1 = lo;
            pal[0] = e0;
            pal[1] = e1;
            for (unsigned i = 1; i <= 6; i++)
                pal[1 + i] = ((7 - i) * e0 + i * e1) * 9363 >> 16;
        } else {
            /* six values and the extremes, worth a try only if the block has them. */
            if (!has0 && !has255)
                break;
            if (hi_in < lo_in) {
                lo_in = hi_in = has0 ? 0 : 255;
            }
            e0 = lo_in;
            e1 = hi_in;
            pal[0] = e0;
            pal[1] = e1;
            for (unsigned i = 1; i <= 4; i++)
                pal[1 + i] = ((5 - i) * e0 + i * e1) * 13108 >> 16;
            pal[6] = 0;
            pal[7] = 255;
        }
        unsigned err = 0;
        uint64_t bits = 0;
        for (int i = 0; i < 16; i++) {
            unsigned best = 0xffffffffu, bi = 0;
            for (unsigned k = 0; k < 8; k++) {
                int d = (int)alpha[i] - (int)pal[k];
                unsigned dd = (unsigned)(d * d);
                if (dd < best) {
                    best = dd;
                    bi = k;
                }
            }
            err += best;
            bits |= (uint64_t)bi << (3 * i);
        }
        if (err < best_err) {
            best_err = err;
            best_out[0] = (uint8_t)e0;
            best_out[1] = (uint8_t)e1;
            for (int k = 0; k < 6; k++)
                best_out[2 + k] = (uint8_t)(bits >> (8 * k));
        }
    }
    __builtin_memcpy(out, best_out, 8);
}

void mime_bcn_encode_bc3_block(const uint32_t src[16], uint8_t out[16])
{
    uint8_t alpha[16];
    for (int i = 0; i < 16; i++)
        alpha[i] = (uint8_t)(src[i] >> 24);
    mime_bcn_encode_alpha_block(alpha, out);
    mime_bcn_encode_bc1_block(src, out + 8, false);
}

int mime_bcn_encode(unsigned format, const uint8_t *bgra, size_t stride, unsigned width, unsigned height, bool alpha,
                    void *dst, size_t dst_size)
{
    if ((format != MIME_BCN_BC1 && format != MIME_BCN_BC3) || !bgra || !dst || !width || !height ||
        stride < (size_t)width * 4)
        return MIME_BCN_ERR_PARAM;
    size_t need = mime_bcn_size(format, width, height);
    if (!need)
        return MIME_BCN_ERR_UNSUPPORTED;
    if (dst_size < need)
        return MIME_BCN_ERR_PARAM;
    uint8_t *d = dst;
    unsigned bs = mime_bcn_block_size(format);
    for (unsigned by = 0; by < (height + 3) / 4; by++) {
        for (unsigned bx = 0; bx < (width + 3) / 4; bx++, d += bs) {
            uint32_t px[16];
            for (unsigned y = 0; y < 4; y++) {
                unsigned sy = by * 4 + y < height ? by * 4 + y : height - 1;
                for (unsigned x = 0; x < 4; x++) {
                    unsigned sx = bx * 4 + x < width ? bx * 4 + x : width - 1;
                    __builtin_memcpy(&px[y * 4 + x], bgra + (size_t)sy * stride + (size_t)sx * 4, 4);
                }
            }
            if (format == MIME_BCN_BC1)
                mime_bcn_encode_bc1_block(px, d, alpha);
            else
                mime_bcn_encode_bc3_block(px, d);
        }
    }
    return MIME_BCN_OK;
}
