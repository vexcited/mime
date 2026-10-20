/*
 * powers and logarithms for the pieces of the library that raise a number to a real exponent. The guest has
 * no math library, so doubles are reduced to a small range and summed as series.
 */
#include "gp_private.h"

static const double kLn2 = 0.693147180559945309417232121458;

double gp_log(double x)
{
    if (!(x > 0.0))
        return -1.0e308;
    UINT64 bits;
    memcpy(&bits, &x, 8);
    INT e = (INT)((bits >> 52) & 0x7ff) - 1023;
    bits = (bits & 0x000fffffffffffffull) | 0x3ff0000000000000ull;
    double m;
    memcpy(&m, &bits, 8);
    if (m > 1.41421356237309505) {
        m *= 0.5;
        e++;
    }
    double s = (m - 1.0) / (m + 1.0), s2 = s * s, term = s, sum = 0.0;
    for (INT k = 1; k < 30; k += 2) {
        sum += term / k;
        term *= s2;
    }
    return 2.0 * sum + e * kLn2;
}

double gp_exp(double x)
{
    if (x > 709.0)
        return 1.0e308;
    if (x < -708.0)
        return 0.0;
    double kd = x / kLn2;
    INT k = (INT)(kd < 0 ? kd - 0.5 : kd + 0.5);
    double r = x - k * kLn2, term = 1.0, sum = 1.0;
    for (INT n = 1; n < 20; n++) {
        term *= r / n;
        sum += term;
    }
    UINT64 bits = (UINT64)(k + 1023) << 52;
    double scale;
    memcpy(&scale, &bits, 8);
    return sum * scale;
}

double gp_pow(double x, double y)
{
    if (y == 0.0)
        return 1.0;
    if (x <= 0.0)
        return 0.0;
    return gp_exp(y * gp_log(x));
}

/*
 * the first half of the sigma bell shape: how far a blend has come, in 1/65536, at the first 128 of 256 steps. the
 * shape is the rising half of a gaussian bell summed up, a gaussian one sixth wide centered in the middle, scaled to
 * run from 0 to 65536 over the 256 steps. the second half is the mirror image. the form of the curve is a guess.
 */
static double bell_area(double upto, INT steps)
{
    const double sigma = 1.0 / 6.0;
    double h = upto / steps, sum = 0.0;
    for (INT i = 0; i <= steps; i++) {
        double u = i * h - 0.5;
        double weight = (i == 0 || i == steps) ? 1.0 : (i & 1) ? 4.0 : 2.0;
        sum += weight * gp_exp(-(u * u) / (2.0 * sigma * sigma));
    }
    return sum * h / 3.0;
}

const float *gp_sigma_table(void)
{
    static float table[128];
    static BOOL ready;
    if (!ready) {
        double total = bell_area(1.0, 256);
        for (INT i = 0; i < 128; i++)
            table[i] = i == 0 ? 0.0f : (float)(65536.0 * bell_area(i / 255.0, 64) / total);
        ready = TRUE;
    }
    return table;
}
