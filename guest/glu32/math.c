/* sine, cosine and the square root, for the angles of the quadrics and the projections. */
#include "glu32_private.h"

#define PI_OVER_2_HIGH 1.57079632673412561417e+00
#define PI_OVER_2_LOW 6.07710050650619224932e-11
#define TWO_OVER_PI 6.36619772367581382433e-01

/* the sine of r within a quarter of pi of zero, as a series. */
static double sine_kernel(double r)
{
    double r2 = r * r;
    return r * (1.0 + r2 * (-1.0 / 6 + r2 * (1.0 / 120 + r2 * (-1.0 / 5040 + r2 * (1.0 / 362880 +
            r2 * (-1.0 / 39916800 + r2 * (1.0 / 6227020800.0 + r2 * (-1.0 / 1307674368000.0))))))));
}

static double cosine_kernel(double r)
{
    double r2 = r * r;
    return 1.0 + r2 * (-1.0 / 2 + r2 * (1.0 / 24 + r2 * (-1.0 / 720 + r2 * (1.0 / 40320 + r2 * (-1.0 / 3628800 +
            r2 * (1.0 / 479001600.0 + r2 * (-1.0 / 87178291200.0)))))));
}

/* the quadrant of x and what is left of it, for angles that are not too large to keep their digits. */
static int reduce(double x, double *remainder)
{
    double n = x * TWO_OVER_PI;
    long long whole = (long long)(n < 0 ? n - 0.5 : n + 0.5);
    *remainder = (x - (double)whole * PI_OVER_2_HIGH) - (double)whole * PI_OVER_2_LOW;
    return (int)(whole & 3);
}

double GluSin(double x)
{
    double r;
    switch (reduce(x, &r)) {
    case 0:
        return sine_kernel(r);
    case 1:
        return cosine_kernel(r);
    case 2:
        return -sine_kernel(r);
    default:
        return -cosine_kernel(r);
    }
}

double GluCos(double x)
{
    double r;
    switch (reduce(x, &r)) {
    case 0:
        return cosine_kernel(r);
    case 1:
        return -sine_kernel(r);
    case 2:
        return -cosine_kernel(r);
    default:
        return sine_kernel(r);
    }
}

double GluSqrt(double value)
{
    return __builtin_elementwise_sqrt(value);
}
