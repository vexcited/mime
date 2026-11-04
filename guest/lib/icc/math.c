#include "icc_internal.h"

const double icc_d50[3] = {0.9642, 1.0, 0.8249};

typedef union {
    double d;
    uint64_t u;
} Bits;

double icc_log(double x)
{
    if (!(x > 0))
        return x == 0 ? -1e308 : 0.0 / 0.0;
    Bits b = {x};
    int e = (int)((b.u >> 52) & 0x7ff);
    if (!e) {
        /* subnormal: scale up first. */
        b.d = x * 18014398509481984.0;
        e = (int)((b.u >> 52) & 0x7ff) - 54;
    }
    e -= 1023;
    b.u = (b.u & 0xfffffffffffffull) | 0x3ff0000000000000ull;
    double m = b.d;
    if (m > 1.4142135623730951) {
        m *= 0.5;
        e++;
    }
    double s = (m - 1) / (m + 1), s2 = s * s, term = s, sum = 0;
    for (int k = 1; k < 40; k += 2) {
        sum += term / k;
        term *= s2;
        if (term < 1e-20 && term > -1e-20)
            break;
    }
    return 2 * sum + e * 0.6931471805599453;
}

double icc_exp(double x)
{
    if (x > 709)
        return 1e308;
    if (x < -708)
        return 0;
    double kf = x * 1.4426950408889634;
    int k = (int)(kf < 0 ? kf - 0.5 : kf + 0.5);
    double r = (x - k * 0.693147180369123816490) - k * 1.90821492927058770002e-10;
    double sum = 1, term = 1;
    for (int i = 1; i < 18; i++) {
        term *= r / i;
        sum += term;
    }
    Bits b;
    b.u = (uint64_t)(k + 1023) << 52;
    return sum * b.d;
}

double icc_pow(double x, double y)
{
    if (y == 0)
        return 1;
    if (x <= 0)
        return 0;
    if (y == 1)
        return x;
    return icc_exp(y * icc_log(x));
}

double icc_s15(const uint8_t *p) { return (int32_t)icc_be32(p) / 65536.0; }

void icc_put_s15(uint8_t *p, double v)
{
    double f = v * 65536.0;
    icc_put32(p, (uint32_t)(int32_t)(f < 0 ? f - 0.5 : f + 0.5));
}

void icc_mat_mul(const double a[9], const double b[9], double out[9])
{
    double r[9];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            r[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
    memcpy(out, r, sizeof(r));
}

void icc_mat_apply(const double m[9], const double in[3], double out[3])
{
    double r[3];
    for (int i = 0; i < 3; i++)
        r[i] = m[i * 3] * in[0] + m[i * 3 + 1] * in[1] + m[i * 3 + 2] * in[2];
    memcpy(out, r, sizeof(r));
}

bool icc_mat_invert(const double m[9], double out[9])
{
    double a = m[4] * m[8] - m[5] * m[7], b = m[5] * m[6] - m[3] * m[8], c = m[3] * m[7] - m[4] * m[6];
    double det = m[0] * a + m[1] * b + m[2] * c;
    if (det == 0 || (det < 1e-12 && det > -1e-12))
        return false;
    double r[9] = {a, m[2] * m[7] - m[1] * m[8], m[1] * m[5] - m[2] * m[4],
                   b, m[0] * m[8] - m[2] * m[6], m[2] * m[3] - m[0] * m[5],
                   c, m[1] * m[6] - m[0] * m[7], m[0] * m[4] - m[1] * m[3]};
    for (int i = 0; i < 9; i++)
        out[i] = r[i] / det;
    return true;
}

void icc_bradford(const double from[3], const double to[3], double out[9])
{
    static const double brad[9] = {0.8951, 0.2664, -0.1614, -0.7502, 1.7135, 0.0367, 0.0389, -0.0685, 1.0296};
    double inv[9], s[3], d[3];
    icc_mat_invert(brad, inv);
    icc_mat_apply(brad, from, s);
    icc_mat_apply(brad, to, d);
    double scale[9] = {d[0] / s[0], 0, 0, 0, d[1] / s[1], 0, 0, 0, d[2] / s[2]};
    icc_mat_mul(scale, brad, out);
    icc_mat_mul(inv, out, out);
}

static double lab_f(double t)
{
    const double limit = 216.0 / 24389.0;
    return t > limit ? icc_pow(t, 1.0 / 3.0) : (24389.0 / 27.0 * t + 16) / 116;
}

static double lab_finv(double t)
{
    const double limit = 6.0 / 29.0;
    return t > limit ? t * t * t : (116 * t - 16) * 27.0 / 24389.0;
}

void icc_xyz_to_lab(const double xyz[3], double lab[3])
{
    double fx = lab_f(xyz[0] / icc_d50[0]), fy = lab_f(xyz[1] / icc_d50[1]), fz = lab_f(xyz[2] / icc_d50[2]);
    lab[0] = 116 * fy - 16;
    lab[1] = 500 * (fx - fy);
    lab[2] = 200 * (fy - fz);
}

void icc_lab_to_xyz(const double lab[3], double xyz[3])
{
    double fy = (lab[0] + 16) / 116, fx = fy + lab[1] / 500, fz = fy - lab[2] / 200;
    xyz[0] = lab_finv(fx) * icc_d50[0];
    xyz[1] = lab_finv(fy) * icc_d50[1];
    xyz[2] = lab_finv(fz) * icc_d50[2];
}

double icc_curve_eval(const IccCurve *c, double x)
{
    if (c->kind == CURVE_IDENTITY)
        return x;
    if (c->kind == CURVE_TABLE) {
        const float *t = c->table;
        uint32_t n = c->n;
        if (!c->inverse) {
            if (x <= 0)
                return t[0];
            if (x >= 1)
                return t[n - 1];
            double pos = x * (n - 1);
            uint32_t i = (uint32_t)pos;
            if (i >= n - 1)
                return t[n - 1];
            double f = pos - i;
            return t[i] + (t[i + 1] - t[i]) * f;
        }
        /* the inverse of a monotonic table, found by bisection. */
        bool rising = t[n - 1] >= t[0];
        double lo_v = rising ? t[0] : t[n - 1], hi_v = rising ? t[n - 1] : t[0];
        /* past an end that slopes the line of the end segment goes on, so gamut checks see how far out a color is. */
        if (x < lo_v || x > hi_v) {
            bool low = x < lo_v;
            uint32_t a = low == rising ? 0 : n - 2;
            double ta = t[a], tb = t[a + 1];
            if (tb != ta)
                return (a + (x - ta) / (tb - ta)) / (n - 1);
        }
        /* on a flat end the inverse is where the table leaves it. */
        if (x <= lo_v) {
            uint32_t i = 0;
            if (rising)
                while (i + 1 < n && t[i + 1] <= lo_v)
                    i++;
            else
                for (i = n - 1; i > 0 && t[i - 1] <= lo_v; i--) {
                }
            return (double)i / (n - 1);
        }
        if (x >= hi_v) {
            uint32_t i = 0;
            if (rising)
                for (i = n - 1; i > 0 && t[i - 1] >= hi_v; i--) {
                }
            else
                while (i + 1 < n && t[i + 1] >= hi_v)
                    i++;
            return (double)i / (n - 1);
        }
        uint32_t lo = 0, hi = n - 1;
        while (hi - lo > 1) {
            uint32_t mid = (lo + hi) / 2;
            if ((t[mid] <= x) == rising)
                lo = mid;
            else
                hi = mid;
        }
        double a = t[lo], b = t[hi];
        double f = b != a ? (x - a) / (b - a) : 0;
        return (lo + f) / (n - 1);
    }
    const double *p = c->p;
    double g = p[0], a = p[1], b = p[2], cc = p[3], d = p[4], e = p[5], f = p[6];
    /* colors below zero mirror those above when the curve starts at zero, as scrgb wants. */
    bool mirror = x < 0 && f == 0 && (d > 0 || (b == 0 && e == 0));
    if (mirror)
        x = -x;
    double y;
    if (!c->inverse) {
        if (x >= d) {
            double base = a * x + b;
            y = (base > 0 ? icc_pow(base, g) : 0) + e;
        } else {
            y = cc * x + f;
        }
    } else {
        double base_d = a * d + b;
        double knee = (base_d > 0 ? icc_pow(base_d, g) : 0) + e;
        if (x >= knee || d <= 0) {
            double v = x - e;
            y = a != 0 && v > 0 ? (icc_pow(v, 1 / g) - b) / a : (a != 0 ? -b / a : 0);
            if (y < d && d > 0)
                y = d;
        } else {
            y = cc != 0 ? (x - f) / cc : 0;
        }
    }
    return mirror ? -y : y;
}
