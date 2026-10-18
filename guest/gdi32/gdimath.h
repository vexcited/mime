/*
 * the little floating point gdi32 needs, without a c library: square
 * roots, sines, cosines and arc tangents in double precision.
 */
#pragma once

#define GDI_PI 3.14159265358979323846

static inline double GdiSqrt(double v)
{
    if (v <= 0)
        return 0;
    double r = v > 1 ? v : 1;
    for (int i = 0; i < 64; i++) {
        double n = (r + v / r) / 2;
        if (n == r)
            break;
        r = n;
    }
    return r;
}

/* sine and cosine: the angle reduced to a quarter turn around zero, then their series. */
static inline void GdiSinCos(double a, double *s, double *c)
{
    double turns = a / (GDI_PI / 2);
    long long q = (long long)(turns >= 0 ? turns + 0.5 : turns - 0.5);
    double x = a - (double)q * (GDI_PI / 2);
    double x2 = x * x;
    double sx = x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72 * (1 - x2 / 110)))));
    double cx = 1 - x2 / 2 * (1 - x2 / 12 * (1 - x2 / 30 * (1 - x2 / 56 * (1 - x2 / 90))));
    switch (q & 3) {
    case 0:
        *s = sx;
        *c = cx;
        break;
    case 1:
        *s = cx;
        *c = -sx;
        break;
    case 2:
        *s = -sx;
        *c = -cx;
        break;
    default:
        *s = -cx;
        *c = sx;
        break;
    }
}

/* the arc tangent of a ratio up to one, from its series after two halvings of the angle. */
static inline double gdi_atan_small(double x)
{
    for (int i = 0; i < 2; i++)
        x = x / (1 + GdiSqrt(1 + x * x));
    double x2 = x * x, sum = 0, term = x;
    for (int k = 0; k < 12; k++) {
        sum += term / (2 * k + 1);
        term *= -x2;
    }
    return 4 * sum;
}

static inline double GdiAtan2(double y, double x)
{
    if (x == 0 && y == 0)
        return 0;
    double ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    double a = ay <= ax ? gdi_atan_small(ay / ax) : GDI_PI / 2 - gdi_atan_small(ax / ay);
    if (x < 0)
        a = GDI_PI - a;
    return y < 0 ? -a : a;
}
