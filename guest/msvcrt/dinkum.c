/*
 * floating point helpers the universal crt's c++ library uses, for double and float. long double is double here, so
 * the _ld forms are the _d ones.
 *
 * classes: 2 nan, 1 infinity, 0 zero, -1 normal, -2 subnormal. functions that change a number return -1 for a finite
 * nonzero result, 0 for zero, and 1 or 2 for infinity or nan. _dexp, _dlog and _dsin call the library functions, which
 * can differ from windows in the last digit.
 */
#include "msvcrt_private.h"

double __cdecl sin(double x);
double __cdecl cos(double x);
double __cdecl exp(double x);
double __cdecl log(double x);
double __cdecl log10(double x);
double __cdecl fabs(double x);
double __cdecl round(double x);
void MsvcrtFpRaise(unsigned flags);

#define D_SIGN 0x8000000000000000ull
#define D_MANT 0x000FFFFFFFFFFFFFull
#define D_INF 0x7FF0000000000000ull
#define F_SIGN 0x80000000u
#define F_MANT 0x007FFFFFu
#define F_INF 0x7F800000u

static uint64_t dbits(double x)
{
    uint64_t u;
    memcpy(&u, &x, sizeof(u));
    return u;
}

static double dfrom(uint64_t u)
{
    double x;
    memcpy(&x, &u, sizeof(x));
    return x;
}

static uint32_t fbits(float x)
{
    uint32_t u;
    memcpy(&u, &x, sizeof(u));
    return u;
}

static float ffrom(uint32_t u)
{
    float x;
    memcpy(&x, &u, sizeof(x));
    return x;
}

/* the library's own flag bits: 1 invalid, 4 division by zero, 8 overflow, 16 underflow. */
static void raise_flags(unsigned mask)
{
    unsigned flags = 0;
    if (mask & 1)
        flags |= 0x10;
    if (mask & 4)
        flags |= 0x08;
    if (mask & 8)
        flags |= 0x04;
    if (mask & 16)
        flags |= 0x02;
    MsvcrtFpRaise(flags);
}

static short class_d(uint64_t u)
{
    uint64_t e = (u >> 52) & 0x7FF;
    if (e == 0x7FF)
        return (u & D_MANT) ? 2 : 1;
    if (e)
        return -1;
    return (u & ~D_SIGN) ? -2 : 0;
}

static short class_f(uint32_t u)
{
    uint32_t e = u & F_INF;
    if (e == F_INF)
        return (u & F_MANT) ? 2 : 1;
    if (e)
        return -1;
    return (u & ~F_SIGN) ? -2 : 0;
}

DECLSPEC_EXPORT short __cdecl _dclass(double x) { return class_d(dbits(x)); }
DECLSPEC_EXPORT short __cdecl _fdclass(float x) { return class_f(fbits(x)); }
DECLSPEC_EXPORT short __cdecl _dtest(double *x) { return class_d(dbits(*x)); }
DECLSPEC_EXPORT short __cdecl _fdtest(float *x) { return class_f(fbits(*x)); }
DECLSPEC_EXPORT int __cdecl _dsign(double x) { return (int)(dbits(x) >> 63); }
DECLSPEC_EXPORT int __cdecl _fdsign(float x) { return (int)(fbits(x) >> 31); }

/* 0 when either is a nan, else 1 when y is above x, 4 when x is above y and 2 for equal. */
DECLSPEC_EXPORT int __cdecl _dpcomp(double x, double y)
{
    if (class_d(dbits(x)) == 2 || class_d(dbits(y)) == 2)
        return 0;
    return y > x ? 1 : x > y ? 4 : 2;
}

DECLSPEC_EXPORT int __cdecl _fdpcomp(float x, float y)
{
    if (class_f(fbits(x)) == 2 || class_f(fbits(y)) == 2)
        return 0;
    return y > x ? 1 : x > y ? 4 : 2;
}

/* horner's rule; the table has n + 1 coefficients, highest power first. */
DECLSPEC_EXPORT double __cdecl _dpoly(double x, const double *table, int n)
{
    double result = *table;
    while (--n >= 0)
        result = result * x + *++table;
    return result;
}

DECLSPEC_EXPORT float __cdecl _fdpoly(float x, const float *table, int n)
{
    float result = *table;
    while (--n >= 0)
        result = (float)(result * x) + *++table;
    return result;
}

/* normalizes a subnormal in place and returns its new exponent. */
DECLSPEC_EXPORT short __cdecl _dnorm(unsigned short *s)
{
    uint64_t u;
    memcpy(&u, s, sizeof(u));
    uint64_t sign = u & D_SIGN, mant = u & D_MANT;
    short exponent = 1;
    if (mant) {
        int index = 63 - __builtin_clzll(mant);
        exponent = (short)(index - 51);
        mant = (mant << (52 - index)) & D_MANT;
    }
    u = mant | sign;
    memcpy(s, &u, sizeof(u));
    return exponent;
}

DECLSPEC_EXPORT short __cdecl _fdnorm(unsigned short *s)
{
    uint32_t u;
    memcpy(&u, s, sizeof(u));
    uint32_t mant = u & F_MANT;
    short exponent = 1;
    if (mant) {
        int index = 31 - __builtin_clz(mant);
        exponent = (short)(index - 22);
        mant = (mant << (23 - index)) & F_MANT;
    }
    u = mant | (u & F_SIGN);
    memcpy(s, &u, sizeof(u));
    return exponent;
}

/* x times 2^exp in place, rounding to even when the result is subnormal. */
DECLSPEC_EXPORT short __cdecl _dscale(double *x, int exp)
{
    uint64_t u = dbits(*x);
    int e = (int)((u >> 52) & 0x7FF);
    if (e == 0x7FF)
        return (u & D_MANT) ? 2 : 1;
    if (!e) {
        if (!(u & D_MANT))
            return 0;
        e = _dnorm((unsigned short *)x);
        u = dbits(*x);
    }
    if (exp > 0 && 2047 - e <= exp) {
        *x = dfrom((u & D_SIGN) | D_INF);
        return 1;
    }
    if (-e < exp) {
        *x = dfrom((u & ~D_INF) | ((uint64_t)(e + exp) << 52));
        return -1;
    }
    int low = e + exp - 1;
    uint64_t sign = u & D_SIGN;
    if (low <= -54) {
        *x = dfrom(sign);
        return 0;
    }
    uint64_t mant = (u & D_MANT) | (1ull << 52);
    int shift = -low;
    uint64_t rest = mant << (64 - shift);
    uint64_t result = sign | (mant >> shift);
    if (rest > D_SIGN || (rest == D_SIGN && (result & 1)))
        result++;
    *x = dfrom(result);
    return result != sign ? -1 : 0;
}

DECLSPEC_EXPORT short __cdecl _fdscale(float *x, int exp)
{
    uint32_t u = fbits(*x);
    int e = (int)((u >> 23) & 0xFF);
    if (e == 0xFF)
        return (u & F_MANT) ? 2 : 1;
    if (!e) {
        if (!(u & F_MANT))
            return 0;
        e = _fdnorm((unsigned short *)x);
        u = fbits(*x);
    }
    if (exp > 0 && 255 - e <= exp) {
        *x = ffrom((u & F_SIGN) | F_INF);
        return 1;
    }
    if (-e < exp) {
        *x = ffrom((u & ~F_INF) | ((uint32_t)(e + exp) << 23));
        return -1;
    }
    int low = e + exp - 1;
    uint32_t sign = u & F_SIGN;
    if (low <= -25) {
        *x = ffrom(sign);
        return 0;
    }
    uint32_t mant = (u & F_MANT) | (1u << 23);
    int shift = -low;
    uint32_t rest = mant << (32 - shift);
    uint32_t result = sign | (mant >> shift);
    if (rest > F_SIGN || (rest == F_SIGN && (result & 1)))
        result++;
    *x = ffrom(result);
    return result != sign ? -1 : 0;
}

/* splits x into a fraction in [0.5, 1) and a power of two. */
DECLSPEC_EXPORT short __cdecl _dunscale(short *exp, double *x)
{
    uint64_t u = dbits(*x);
    int e = (int)((u >> 52) & 0x7FF);
    if (e == 0x7FF) {
        *exp = 0;
        return (u & D_MANT) ? 2 : 1;
    }
    if (!e) {
        if (!(u & D_MANT)) {
            *exp = 0;
            return 0;
        }
        e = _dnorm((unsigned short *)x);
        u = dbits(*x);
    }
    *x = dfrom((u & (D_SIGN | D_MANT)) | 0x3FE0000000000000ull);
    *exp = (short)(e - 1022);
    return -1;
}

DECLSPEC_EXPORT short __cdecl _fdunscale(short *exp, float *x)
{
    uint32_t u = fbits(*x);
    int e = (int)((u >> 23) & 0xFF);
    if (e == 0xFF) {
        *exp = 0;
        return (u & F_MANT) ? 2 : 1;
    }
    if (!e) {
        if (!(u & F_MANT)) {
            *exp = 0;
            return 0;
        }
        e = _fdnorm((unsigned short *)x);
        u = fbits(*x);
    }
    *x = ffrom((u & (F_SIGN | F_MANT)) | 0x3F000000u);
    *exp = (short)(e - 126);
    return -1;
}

/* clears the bits of x below 2^exp, so exp zero gives the integral part. */
DECLSPEC_EXPORT short __cdecl _d_int(double *x, short exp)
{
    uint64_t u = dbits(*x);
    int e = (int)((u >> 52) & 0x7FF);
    if (e == 0x7FF)
        return (u & D_MANT) ? 2 : 1;
    if (!e && !(u & ~D_SIGN))
        return 0;
    int count = 1075 - exp - e;
    if (count <= 0)
        return 0;
    if (count > 52) {
        *x = dfrom(u & D_SIGN);
        return -1;
    }
    uint64_t mask = (1ull << count) - 1;
    *x = dfrom(u & ~mask);
    return (u & mask) ? -1 : 0;
}

DECLSPEC_EXPORT short __cdecl _fd_int(float *x, short exp)
{
    uint32_t u = fbits(*x);
    int e = (int)((u >> 23) & 0xFF);
    if (e == 0xFF)
        return (u & F_MANT) ? 2 : 1;
    if (!e && !(u & ~F_SIGN))
        return 0;
    int count = 150 - exp - e;
    if (count <= 0)
        return 0;
    if (count > 23) {
        *x = ffrom(u & F_SIGN);
        return -1;
    }
    uint32_t mask = (1u << count) - 1;
    *x = ffrom(u & ~mask);
    return (u & mask) ? -1 : 0;
}

/* x becomes exp(x) * y * 2^eoff. zero, infinity and nan follow ieee rules, so infinity times zero is invalid. */
static short exp_scaled(double *px, double y, int eoff)
{
    double x = *px;
    short cx = class_d(dbits(x)), cy = class_d(dbits(y));
    if (cx == 2)
        return 2;
    if (cx < 0 && cy < 0) {
        if (x < -1842.0) {
            *px = y * 0.0;
            return 0;
        }
        if (x > 1842.0) {
            *px = y * dfrom(D_INF);
            raise_flags(8);
            return 1;
        }
        double n = round(x * 1.4426950408889634);
        double r = x - n * 0.6931471805599453;
        double v = y * exp(r);
        *px = v;
        short code = _dscale(px, (int)n + eoff);
        if (code == 0)
            raise_flags(16);
        else if (code == 1)
            raise_flags(8);
        return code;
    }
    if (cy == 2) {
        *px = y;
        return 2;
    }
    if (cy == 0) {
        if (x != dfrom(D_INF)) {
            *px = y;
            return 0;
        }
        raise_flags(1);
        *px = dfrom(0x7FF8000000000000ull);
        return 2;
    }
    if (cy == 1) {
        if (x != -dfrom(D_INF)) {
            *px = y;
            return 1;
        }
        raise_flags(1);
        *px = dfrom(0x7FF8000000000000ull);
        return 2;
    }
    if (cx == 0) {
        *px = y;
        short code = _dscale(px, eoff);
        if (code == 0)
            raise_flags(16);
        else if (code == 1)
            raise_flags(8);
        return code;
    }
    if (x == dfrom(D_INF)) {
        *px = x * y;
        return 1;
    }
    *px = y * 0.0;
    return 0;
}

DECLSPEC_EXPORT short __cdecl _dexp(double *x, double y, int eoff)
{
    return exp_scaled(x, y, eoff);
}

DECLSPEC_EXPORT short __cdecl _fdexp(float *x, float y, int eoff)
{
    double wide = *x;
    short code = exp_scaled(&wide, y, eoff);
    *x = (float)wide;
    uint32_t u = fbits(*x);
    if (code < 0)
        code = class_f(u) == 1 ? 1 : class_f(u) == 0 ? 0 : -1;
    return code;
}

/* natural log, base 10 when base is positive, base 2 when negative. */
DECLSPEC_EXPORT double __cdecl _dlog(double x, int base)
{
    short c = class_d(dbits(x));
    if (c == 2)
        return x;
    if (c == 0) {
        raise_flags(4);
        return -dfrom(D_INF);
    }
    if (x < 0) {
        raise_flags(1);
        return dfrom(0x7FF8000000000000ull);
    }
    if (c == 1)
        return x;
    if (base > 0)
        return log10(x);
    if (base < 0)
        return log(x) * 1.4426950408889634;
    return log(x);
}

DECLSPEC_EXPORT float __cdecl _fdlog(float x, int base)
{
    return (float)_dlog(x, base);
}

/* sin of x shifted by a number of quarter turns. */
DECLSPEC_EXPORT double __cdecl _dsin(double x, unsigned quarter)
{
    short c = class_d(dbits(x));
    if (c == 2)
        return x;
    if (c == 1) {
        raise_flags(1);
        return dfrom(0x7FF8000000000000ull);
    }
    switch (quarter & 3) {
    case 0:
        return sin(x);
    case 1:
        return cos(x);
    case 2:
        return -sin(x);
    default:
        return -cos(x);
    }
}

DECLSPEC_EXPORT float __cdecl _fdsin(float x, unsigned quarter)
{
    return (float)_dsin(x, quarter);
}

DECLSPEC_EXPORT float __cdecl fabsf(float x)
{
    return ffrom(fbits(x) & ~F_SIGN);
}
