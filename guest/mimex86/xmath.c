/*
 * the elementary functions behind the x87 transcendental instructions,
 * in double precision. the core has no c library to call.
 */
#include "xmath.h"

static inline uint64_t bits(double d) { uint64_t u; __builtin_memcpy(&u, &d, 8); return u; }
static inline double from_bits(uint64_t u) { double d; __builtin_memcpy(&d, &u, 8); return d; }

double xm_scalbn(double x, int n)
{
    /* multiply in steps that stay representable. */
    while (n > 1023) {
        x *= 0x1p1023;
        n -= 1023;
        if (n > 1023 && (x != x || x == 0 || x * 2 == x))
            return x * 0x1p1023;
    }
    while (n < -1022) {
        x *= 0x1p-1022;
        n += 1022;
        if (n < -1022 && (x != x || x == 0))
            return x * 0x1p-1022;
    }
    return x * from_bits((uint64_t)(1023 + n) << 52);
}

double xm_frexp(double x, int *e)
{
    uint64_t u = bits(x);
    int exp = (int)((u >> 52) & 0x7FF);
    if (!exp) {
        if (x == 0) {
            *e = 0;
            return x;
        }
        x *= 0x1p64;
        u = bits(x);
        exp = (int)((u >> 52) & 0x7FF) - 64;
    } else if (exp == 0x7FF) {
        *e = 0;
        return x;
    }
    *e = exp - 1022;
    return from_bits((u & ~(0x7FFull << 52)) | (1022ull << 52));
}

/* exact remainder of x / y truncated toward zero, and the low quotient bits. */
double xm_fmod(double x, double y, uint64_t *quotient)
{
    *quotient = 0;
    if (y == 0 || x != x || y != y || x - x != 0)
        return (x * y) / (x * y);
    double ax = __builtin_fabs(x), ay = __builtin_fabs(y);
    if (ax < ay)
        return x;
    int ex, ey;
    xm_frexp(ax, &ex);
    xm_frexp(ay, &ey);
    uint64_t q = 0;
    double r = ax;
    for (int k = ex - ey; k >= 0; k--) {
        double t = xm_scalbn(ay, k);
        q <<= 1;
        if (r >= t) {
            r -= t;
            q |= 1;
        }
    }
    *quotient = q;
    return x < 0 ? -r : r;
}

/* sin and cos on [-pi/4, pi/4]. */
static double sin_kernel(double x)
{
    double z = x * x;
    return x + x * z * (-1.66666666666666324348e-01 + z * (8.33333333332248946124e-03 +
           z * (-1.98412698298579493134e-04 + z * (2.75573137070700676789e-06 +
           z * (-2.50507602534068634195e-08 + z * 1.58969099521155010221e-10)))));
}

static double cos_kernel(double x)
{
    double z = x * x;
    return 1.0 - 0.5 * z + z * z * (4.16666666666666019037e-02 + z * (-1.38888888888741095749e-03 +
           z * (2.48015872894767294178e-05 + z * (-2.75573143513906633035e-07 +
           z * (2.08757232129817482790e-09 + z * -1.13596475577881948265e-11)))));
}

/* reduces x by multiples of pi/2; returns the quadrant. */
static int reduce(double x, double *r)
{
    /* pi/2 split in 33 bit parts, so n times each part is exact for moderate n. */
    static const double pio2_1 = 1.57079632673412561417e+00, pio2_2 = 6.07710050630396597660e-11,
                        pio2_3 = 2.02226624871116645580e-21, pio2_3t = 8.47842766036889956997e-32;
    double n = __builtin_rint(x * 6.36619772367581382433e-01);
    *r = (((x - n * pio2_1) - n * pio2_2) - n * pio2_3) - n * pio2_3t;
    return (int)((int64_t)n & 3);
}

double xm_sin(double x)
{
    double r;
    switch (reduce(x, &r)) {
    case 0: return sin_kernel(r);
    case 1: return cos_kernel(r);
    case 2: return -sin_kernel(r);
    default: return -cos_kernel(r);
    }
}

double xm_cos(double x)
{
    double r;
    switch (reduce(x, &r)) {
    case 0: return cos_kernel(r);
    case 1: return -sin_kernel(r);
    case 2: return -cos_kernel(r);
    default: return sin_kernel(r);
    }
}

double xm_tan(double x)
{
    return xm_sin(x) / xm_cos(x);
}

/* atan on [0, 1] through the reduction atan(x) = pi/6 + atan((x*sqrt3 - 1) / (x + sqrt3)). */
static double atan_kernel(double x)
{
    int shifted = 0;
    if (x > 0.2679491924311227) {
        x = (x * 1.7320508075688772 - 1.0) / (x + 1.7320508075688772);
        shifted = 1;
    }
    double z = x * x;
    double p = x * (1.0 + z * (-3.33333333333329318027e-01 + z * (1.99999999998764832476e-01 +
               z * (-1.42857142725034663711e-01 + z * (1.11111104054623557880e-01 +
               z * (-9.09088713343650656196e-02 + z * (7.69187620504482999495e-02 +
               z * (-6.66107313738753120669e-02 + z * (5.83357013379057348645e-02 +
               z * (-4.97687799461593236017e-02 + z * (3.65315727442169155270e-02 +
               z * -1.62858201153657823623e-02)))))))))));
    return shifted ? 5.23598775598298873077e-01 + p : p;
}

double xm_atan2(double y, double x)
{
    static const double pi = 3.14159265358979311600e+00, pio2 = 1.57079632679489655800e+00;
    if (x != x || y != y)
        return x + y;
    double ay = __builtin_fabs(y), ax = __builtin_fabs(x);
    double r;
    if (ax == 0 && ay == 0) {
        r = 0;
    } else if (ay > ax) {
        r = (ax - ax != 0 && ay - ay != 0) ? pi / 4 : pio2 - atan_kernel(ax / ay);
    } else {
        r = (ax - ax != 0 && ay - ay != 0) ? pi / 4 : atan_kernel(ay / ax);
    }
    if (__builtin_signbit(x))
        r = pi - r;
    return __builtin_signbit(y) ? -r : r;
}

/* log2(x) for x > 0. */
double xm_log2(double x)
{
    if (x != x || x < 0)
        return (x - x) / (x - x);
    if (x == 0)
        return -1.0 / 0.0;
    if (x - x != 0)
        return x;
    int e;
    double m = xm_frexp(x, &e); /* x = m * 2^e, m in [0.5, 1) */
    if (m < 0.70710678118654752440) {
        m *= 2;
        e--;
    }
    /* log(m) = 2 atanh((m - 1) / (m + 1)). */
    double s = (m - 1) / (m + 1), z = s * s, term = s, sum = 0;
    for (int k = 1; k < 40; k += 2) {
        sum += term / k;
        term *= z;
        if (__builtin_fabs(term) < 1e-20)
            break;
    }
    return e + 2 * sum * 1.44269504088896338700e+00;
}

/* 2^x. */
double xm_exp2(double x)
{
    if (x != x)
        return x;
    if (x > 1024)
        return 1.0 / 0.0;
    if (x < -1080)
        return 0;
    double n = __builtin_rint(x);
    double f = (x - n) * 6.93147180559945286227e-01; /* in [-ln2/2, ln2/2] */
    double sum = 1, term = 1;
    for (int k = 1; k < 30; k++) {
        term *= f / k;
        sum += term;
        if (__builtin_fabs(term) < 1e-18)
            break;
    }
    return xm_scalbn(sum, (int)n);
}

/* 2^x - 1, precise for small x. */
double xm_exp2m1(double x)
{
    if (__builtin_fabs(x) > 0.5)
        return xm_exp2(x) - 1;
    double f = x * 6.93147180559945286227e-01, sum = 0, term = 1;
    for (int k = 1; k < 30; k++) {
        term *= f / k;
        sum += term;
        if (__builtin_fabs(term) < 1e-20)
            break;
    }
    return sum;
}

/* log2(1 + x), precise for small x. */
double xm_log2p1(double x)
{
    if (__builtin_fabs(x) > 0.25)
        return xm_log2(1 + x);
    double s = x / (2 + x), z = s * s, term = s, sum = 0;
    for (int k = 1; k < 40; k += 2) {
        sum += term / k;
        term *= z;
        if (__builtin_fabs(term) < 1e-22)
            break;
    }
    return 2 * sum * 1.44269504088896338700e+00;
}
