/* ClearType glyph rendering: bilevel samples six across a pixel, filtered to a level from 0 to 6 for each stripe. */
#include "ttf.h"
#include "ttf_internal.h"

#define SUBPIXELS 3
#define OVER 6

static int fdiv(int a, int b)
{
    int q = a / b;
    return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

static int cdiv(int a, int b)
{
    return -fdiv(-a, b);
}

bool mime_ttf_cleartype_bitmap(const MimeTtfFont *font, const MimeAllocator *alloc, uint16_t glyph,
                               const MimeTtfRenderParams *params, int over_y, const MimeTtfScan *scan,
                               MimeTtfBitmap *out)
{
    __builtin_memset(out, 0, sizeof(*out));
    if (over_y != 1 && over_y != 5)
        return false;
    MimeTtfRenderParams p = *params;
    p.over_x = OVER;
    p.over_y = over_y;
    p.shift_x = params->shift_x * (float)OVER;
    p.shift_y = params->shift_y * (float)over_y;
    MimeTtfBitmap s;
    if (!mime_ttf_scan_bitmap(font, alloc, glyph, &p, scan, &s))
        return false;
    if (!s.data)
        return true;
    if (mime_ttf_thin(font)) {
        int nw = (s.x1 - s.x0) + 1;
        int oh = s.y1 - s.y0;
        uint8_t *nd = mime_alloc(alloc, (size_t)nw * (size_t)oh);
        if (!nd) {
            mime_ttf_bitmap_free(alloc, &s);
            return false;
        }
        int ow = s.x1 - s.x0;
        for (int y = 0; y < oh; y++) {
            uint8_t *dst = nd + (size_t)y * (size_t)nw;
            const uint8_t *src = s.data + (size_t)y * (size_t)s.stride;
            for (int x = 0; x < nw; x++) {
                int a = x < ow ? src[x] : 0;
                int b = x > 0 && x - 1 < ow ? src[x - 1] : 0;
                dst[x] = a | b;
            }
        }
        mime_free(alloc, s.data);
        s.data = nd;
        s.stride = nw;
        s.x1 += 1;
    }
    int sw = s.x1 - s.x0, sh = s.y1 - s.y0;

    /* a stripe looks at the six samples around its centre, so it reaches one stripe past the ink on each side. */
    int first = cdiv(s.x0 - 3, 2), last = fdiv(s.x1 + 1, 2);
    int px0 = fdiv(first, SUBPIXELS), px1 = fdiv(last, SUBPIXELS) + 1;
    /* the pixels are those a quarter of a pixel around the points touch, the closed intervals. */
    float lo, hi;
    if (mime_ttf_glyph_control_extent(font, glyph, &p, &lo, &hi)) {
        float m = (lo - 1.5f) / 6.0f, e = (hi + 1.5f) / 6.0f;
        int fm = (int)m;
        if ((float)fm > m)
            fm--;
        int fe = (int)e;
        if ((float)fe > e)
            fe--;
        px0 = ((float)fm < m ? fm + 1 : fm) - 1;
        px1 = fe + 1;
    }
    int py0 = fdiv(s.y0, over_y), py1 = cdiv(s.y1, over_y);
    int w = px1 - px0, h = py1 - py0;
    uint8_t *levels = mime_alloc(alloc, (size_t)w * (size_t)h * SUBPIXELS);
    if (!levels) {
        mime_ttf_bitmap_free(alloc, &s);
        return false;
    }
    static const int weight[5] = {4, 9, 10, 9, 4};
    for (int py = py0; py < py1; py++) {
        for (int k = 0; k < w * SUBPIXELS; k++) {
            int stripe = px0 * SUBPIXELS + k;
            int wx = 2 * stripe - 2 - s.x0;
            int total = 0;
            for (int r = 0; r < over_y; r++) {
                int row = py * over_y + r - s.y0;
                if (row < 0 || row >= sh)
                    continue;
                int level = 0;
                for (int t = 0; t < 6; t++)
                    if (wx + t >= 0 && wx + t < sw)
                        level += s.data[(size_t)row * (size_t)sw + (size_t)(wx + t)];
                total += over_y == 1 ? level : weight[r] * level;
            }
            levels[(size_t)(py - py0) * (size_t)w * SUBPIXELS + (size_t)k] =
                (uint8_t)(over_y == 1 ? total : (total + 18) / 36);
        }
    }
    mime_ttf_bitmap_free(alloc, &s);
    out->x0 = px0;
    out->y0 = py0;
    out->x1 = px1;
    out->y1 = py1;
    out->stride = w * SUBPIXELS;
    out->data = levels;
    return true;
}

/* natural logarithm of a positive number below or equal to one, by the series of the inverse hyperbolic tangent. */
static double ln_small(double x)
{
    union {
        double d;
        uint64_t u;
    } v = {x};
    int e = (int)((v.u >> 52) & 0x7FF) - 1023;
    v.u = (v.u & 0x000FFFFFFFFFFFFFull) | 0x3FF0000000000000ull;
    double m = v.d;
    double t = (m - 1) / (m + 1), t2 = t * t, term = t, sum = 0;
    for (int i = 1; i < 60; i += 2) {
        sum += term / (double)i;
        term *= t2;
    }
    return 2 * sum + (double)e * 0.69314718055994530942;
}

static double exp_neg(double z)
{
    /* z is at most zero. */
    const double ln2 = 0.69314718055994530942;
    int k = (int)(z / ln2 - 0.5);
    double r = z - (double)k * ln2, term = 1, sum = 1;
    for (int i = 1; i < 30; i++) {
        term *= r / (double)i;
        sum += term;
    }
    union {
        double d;
        uint64_t u;
    } scale = {0};
    scale.u = (uint64_t)(1023 + k) << 52;
    return sum * scale.d;
}

static double pow_unit(double x, double y)
{
    return x <= 0 ? 0 : exp_neg(y * ln_small(x));
}

void mime_ttf_gamma_tables(unsigned gamma, uint8_t *linear, uint8_t *encoded)
{
    /* the gamma of the setting is the hundreds as tenths, from 1.0 to 2.2. */
    unsigned tenths = gamma / 100;
    if (tenths < 10)
        tenths = 10;
    if (tenths > 22)
        tenths = 22;
    double g = (double)tenths / 10.0;
    for (int i = 0; i < 256; i++) {
        double x = (double)i / 255.0;
        linear[i] = (uint8_t)(int)(255.0 * pow_unit(x, g) + 0.5);
        encoded[i] = (uint8_t)(int)(255.0 * pow_unit(x, 1.0 / g) + 0.5);
    }
}
