/*
 * the c99 functions that the universal crt adds to the classic set: selection, rounding, the remainder, scaling and
 * stepping functions, the exponential and logarithm of small arguments, the error function and the gamma function.
 * included at the end of mathlib.c for the double-double helpers it keeps.
 *
 * long double is double on windows, so the l forms call the double ones. Unlike the classic functions these do not
 * touch errno but raise the floating point exception flags, as the universal crt does. Where a result is a
 * double-double it is rounded once at the end.
 */

typedef union {
    float f;
    uint32_t u;
} FBITS;

static inline uint32_t fbits(float f)
{
    FBITS b = {f};
    return b.u;
}

static inline float from_fbits(uint32_t u)
{
    FBITS b;
    b.u = u;
    return b.f;
}

static inline int is_fnan(float f)
{
    return (fbits(f) & 0x7fffffffu) > 0x7f800000u;
}

/* nan or infinity. */
static inline int not_finite(double x)
{
    return ((bits(x) >> 52) & 0x7ff) == 0x7ff;
}

#define SIGN_BIT 0x8000000000000000ull

/* ------------------------------------------------------------------ selection and sign */

/* the larger; a nan loses to a number, and of two zeros the second wins unless the first is positive. */
DECLSPEC_EXPORT double __cdecl fmax(double x, double y)
{
    if (is_nan(x) || (!is_nan(y) && (y > x || (x == y && x < 0.0))))
        return y;
    return x;
}

DECLSPEC_EXPORT double __cdecl fmin(double x, double y)
{
    if (is_nan(x) || (!is_nan(y) && y <= x && (x != y || x >= 0.0)))
        return y;
    return x;
}

DECLSPEC_EXPORT float __cdecl fmaxf(float x, float y)
{
    if (is_fnan(x) || (!is_fnan(y) && (y > x || (x == y && x < 0.0f))))
        return y;
    return x;
}

DECLSPEC_EXPORT float __cdecl fminf(float x, float y)
{
    if (is_fnan(x) || (!is_fnan(y) && y <= x && (x != y || x >= 0.0f)))
        return y;
    return x;
}

DECLSPEC_EXPORT double __cdecl fmaxl(double x, double y) { return fmax(x, y); }
DECLSPEC_EXPORT double __cdecl fminl(double x, double y) { return fmin(x, y); }

DECLSPEC_EXPORT double __cdecl fdim(double x, double y)
{
    if (is_nan(x))
        return x;
    if (is_nan(y))
        return y;
    if (y >= x)
        return 0.0;
    return x - y;
}

DECLSPEC_EXPORT float __cdecl fdimf(float x, float y)
{
    if (is_fnan(x))
        return x;
    if (is_fnan(y))
        return y;
    if (y >= x)
        return 0.0f;
    return x - y;
}

DECLSPEC_EXPORT double __cdecl fdiml(double x, double y) { return fdim(x, y); }

DECLSPEC_EXPORT double __cdecl copysign(double x, double y)
{
    return from_bits((bits(x) & ~SIGN_BIT) | (bits(y) & SIGN_BIT));
}

DECLSPEC_EXPORT float __cdecl copysignf(float x, float y)
{
    return from_fbits((fbits(x) & 0x7fffffffu) | (fbits(y) & 0x80000000u));
}

DECLSPEC_EXPORT double __cdecl copysignl(double x, double y) { return copysign(x, y); }

/* the quiet nan whatever the tag says. */
DECLSPEC_EXPORT double __cdecl nan(const char *tag)
{
    return from_bits(0x7ff8000000000000ull);
}

DECLSPEC_EXPORT float __cdecl nanf(const char *tag)
{
    return from_fbits(0x7fc00000u);
}

DECLSPEC_EXPORT double __cdecl nanl(const char *tag) { return nan(tag); }

/* ------------------------------------------------------------------ stepping and scaling */

/* the next double from x toward y, with the flags of a result that overflows or turns subnormal. */
static double step_toward(double x, double y)
{
    if (is_nan(x))
        return x;
    if (is_nan(y) || x == y)
        return y;
    if (is_inf(x))
        return from_bits(bits(x) - 1);
    uint64_t u;
    if (x == 0) {
        MsvcrtFpRaise(MSVCRT_FE_UNDERFLOW);
        return from_bits(y > x ? 1 : SIGN_BIT | 1);
    }
    if ((y > x && x > 0) || (x > y && x < 0)) {
        u = bits(x) + 1;
        if ((u & 0x7ff0000000000000ull) == 0x7ff0000000000000ull) {
            MsvcrtFpRaise(MSVCRT_FE_OVERFLOW);
            return from_bits(u);
        }
    } else {
        u = bits(x) - 1;
    }
    if (!(u & 0x7ff0000000000000ull))
        MsvcrtFpRaise(MSVCRT_FE_UNDERFLOW);
    return from_bits(u);
}

DECLSPEC_EXPORT double __cdecl nextafter(double x, double y) { return step_toward(x, y); }
DECLSPEC_EXPORT double __cdecl nextafterl(double x, double y) { return step_toward(x, y); }
DECLSPEC_EXPORT double __cdecl nexttoward(double x, double y) { return step_toward(x, y); }
DECLSPEC_EXPORT double __cdecl nexttowardl(double x, double y) { return step_toward(x, y); }

DECLSPEC_EXPORT float __cdecl nexttowardf(float x, double y)
{
    uint32_t u = fbits(x);
    if (is_fnan(x))
        return x;
    if (is_nan(y))
        return (float)y;
    double dx = x;
    if (dx == y)
        return (float)y;
    if ((u & 0x7fffffffu) == 0x7f800000u)
        return from_fbits(u - 1);
    if (!(u & 0x7fffffffu)) {
        MsvcrtFpRaise(MSVCRT_FE_UNDERFLOW);
        return from_fbits(y > dx ? 1u : 0x80000001u);
    }
    uint32_t r;
    if ((y > dx && dx > 0) || (dx > y && dx < 0)) {
        r = u + 1;
        if ((r & 0x7f800000u) == 0x7f800000u) {
            MsvcrtFpRaise(MSVCRT_FE_OVERFLOW);
            return from_fbits(r);
        }
    } else {
        r = u - 1;
    }
    if (!(r & 0x7f800000u))
        MsvcrtFpRaise(MSVCRT_FE_UNDERFLOW);
    return from_fbits(r);
}

DECLSPEC_EXPORT float __cdecl nextafterf(float x, float y) { return nexttowardf(x, y); }

/* x times 2^n with the flag of a result that overflows or underflows to zero. */
static double scale_flagged(double x, int n)
{
    if (!n || x == 0 || not_finite(x))
        return x;
    if (n > 3000)
        n = 3000;
    else if (n < -3000)
        n = -3000;
    double r = scale(x, n);
    if (is_inf(r))
        MsvcrtFpRaise(MSVCRT_FE_OVERFLOW);
    else if (r == 0)
        MsvcrtFpRaise(MSVCRT_FE_UNDERFLOW);
    return r;
}

DECLSPEC_EXPORT double __cdecl scalbn(double x, int n) { return scale_flagged(x, n); }
DECLSPEC_EXPORT double __cdecl scalbln(double x, long n) { return scale_flagged(x, (int)n); }
DECLSPEC_EXPORT double __cdecl scalbnl(double x, int n) { return scale_flagged(x, n); }
DECLSPEC_EXPORT double __cdecl scalblnl(double x, long n) { return scale_flagged(x, (int)n); }
DECLSPEC_EXPORT float __cdecl scalbnf(float x, int n) { return (float)scale_flagged(x, n); }
DECLSPEC_EXPORT float __cdecl scalblnf(float x, long n) { return (float)scale_flagged(x, (int)n); }

/* the exponent of x as a number, the one of _logb. */
DECLSPEC_EXPORT double __cdecl logb(double x) { return _logb(x); }
DECLSPEC_EXPORT float __cdecl logbf(float x) { return _logbf(x); }
DECLSPEC_EXPORT double __cdecl logbl(double x) { return _logb(x); }
DECLSPEC_EXPORT int __cdecl ilogbl(double x) { return ilogb(x); }

/* ------------------------------------------------------------------ rounding */

/* halfway cases go away from zero. */
DECLSPEC_EXPORT double __cdecl round(double x)
{
    uint64_t u = bits(x);
    int e = (int)((u >> 52) & 0x7ff);
    if (e >= 0x433)
        return x;
    if (e < 0x3ff) {
        uint64_t r = u & SIGN_BIT;
        if (e == 0x3fe)
            r |= 0x3ff0000000000000ull;
        return from_bits(r);
    }
    int shift = e - 0x3ff;
    u += 0x0008000000000000ull >> shift;
    return from_bits(u & ~(0x000fffffffffffffull >> shift));
}

DECLSPEC_EXPORT float __cdecl roundf(float x) { return (float)round(x); }
DECLSPEC_EXPORT double __cdecl roundl(double x) { return round(x); }

/* the integer nearest by the current rounding mode, by adding and taking away a number that has no fraction. */
static double rint_(double x)
{
    uint64_t u = bits(x);
    if (((u >> 52) & 0x7ff) >= 0x433)
        return x;
    volatile double magic = (u & SIGN_BIT) ? -0x1p52 : 0x1p52;
    volatile double t = x + magic;
    t = t - magic;
    if (t == 0)
        return from_bits(u & SIGN_BIT);
    return t;
}

DECLSPEC_EXPORT double __cdecl rint(double x) { return rint_(x); }
DECLSPEC_EXPORT float __cdecl rintf(float x) { return (float)rint_(x); }
DECLSPEC_EXPORT double __cdecl rintl(double x) { return rint_(x); }

/* the same, without leaving the inexact flag set when it was not. */
static double nearbyint_(double x)
{
    unsigned before = MsvcrtFpGetStatus();
    double r = rint_(x);
    if (!(before & MSVCRT_FE_INEXACT))
        MsvcrtFpSetStatus(MsvcrtFpGetStatus() & ~(unsigned)MSVCRT_FE_INEXACT);
    return r;
}

DECLSPEC_EXPORT double __cdecl nearbyint(double x) { return nearbyint_(x); }
DECLSPEC_EXPORT float __cdecl nearbyintf(float x) { return (float)nearbyint_(x); }
DECLSPEC_EXPORT double __cdecl nearbyintl(double x) { return nearbyint_(x); }

/*
 * to an integer type: a value that does not fit, a nan and an infinity raise the invalid flag and give 0, as the
 * universal crt does. The long of windows has 32 bits.
 */
static long to_long(double v)
{
    if (!not_finite(v) && v >= -2147483648.0 && v <= 2147483647.0)
        return (long)v;
    MsvcrtFpRaise(MSVCRT_FE_INVALID);
    return 0;
}

static long long to_long_long(double v)
{
    if (!not_finite(v) && v >= -0x1p63 && v <= 0x1p63)
        return v == 0x1p63 ? INT64_MIN : (long long)v;
    MsvcrtFpRaise(MSVCRT_FE_INVALID);
    return 0;
}

DECLSPEC_EXPORT long __cdecl lround(double x) { return to_long(round(x)); }
DECLSPEC_EXPORT long __cdecl lroundf(float x) { return to_long(round(x)); }
DECLSPEC_EXPORT long __cdecl lroundl(double x) { return to_long(round(x)); }
DECLSPEC_EXPORT long long __cdecl llround(double x) { return to_long_long(round(x)); }
DECLSPEC_EXPORT long long __cdecl llroundf(float x) { return to_long_long(round(x)); }
DECLSPEC_EXPORT long long __cdecl llroundl(double x) { return to_long_long(round(x)); }
DECLSPEC_EXPORT long __cdecl lrint(double x) { return to_long(rint_(x)); }
DECLSPEC_EXPORT long __cdecl lrintf(float x) { return to_long(rint_(x)); }
DECLSPEC_EXPORT long __cdecl lrintl(double x) { return to_long(rint_(x)); }
DECLSPEC_EXPORT long long __cdecl llrint(double x) { return to_long_long(rint_(x)); }
DECLSPEC_EXPORT long long __cdecl llrintf(float x) { return to_long_long(rint_(x)); }
DECLSPEC_EXPORT long long __cdecl llrintl(double x) { return to_long_long(rint_(x)); }

DECLSPEC_EXPORT double __cdecl truncl(double x) { return trunc_(x); }

/* ------------------------------------------------------------------ remainder */

/*
 * x - n y for the integer n nearest to x / y, halfway cases to the even one, exact. The low bits of n, with the sign
 * of x / y, go to *quo. Works on the mantissas by long division, so no rounding happens on the way.
 */
static double ieee_remainder(double x, double y, int *quo)
{
    if (quo)
        *quo = 0;
    if (is_nan(x))
        return x;
    if (is_nan(y))
        return y;
    if (is_inf(x) || y == 0)
        return MsvcrtMathError(_DOMAIN, "remainder", x, y, NAN_IND);
    if (is_inf(y))
        return x;
    uint64_t ux = bits(x), uy = bits(y);
    int ex = (int)((ux >> 52) & 0x7ff), ey = (int)((uy >> 52) & 0x7ff);
    uint64_t mx = ux & 0x000fffffffffffffull, my = uy & 0x000fffffffffffffull;
    if (ex)
        mx |= 1ull << 52;
    else
        ex = 1;
    if (ey)
        my |= 1ull << 52;
    else
        ey = 1;
    int negative = (ux & SIGN_BIT) != 0;
    int quotient_negative = negative != ((uy & SIGN_BIT) != 0);
    /* |x| is mx 2^d and |y| is my, in units of 2^(ey - 1075). */
    int d = ex - ey;
    uint64_t q, r;
    int below = 0;
    if (d < -1)
        return x;
    if (d == -1) {
        if (mx <= my)
            return x;
        /* |x| is above half of |y|: the quotient is 1 and the remainder |x| - |y|, in half units. */
        q = 1;
        double magnitude = scale((double)(2 * my - mx), ey - 1076);
        if (quo)
            *quo = quotient_negative ? -1 : 1;
        return negative ? magnitude : -magnitude;
    }
    q = mx / my;
    r = mx % my;
    for (int i = 0; i < d; i++) {
        r <<= 1;
        q <<= 1;
        if (r >= my) {
            r -= my;
            q |= 1;
        }
    }
    uint64_t twice = r << 1;
    if (twice > my || (twice == my && (q & 1))) {
        r = my - r;
        q++;
        below = 1;
    }
    if (quo) {
        int low = (int)(q & 7);
        *quo = quotient_negative ? -low : low;
    }
    double magnitude = scale((double)r, ey - 1075);
    if (r == 0)
        return from_bits(ux & SIGN_BIT);
    return (negative != below) ? -magnitude : magnitude;
}

DECLSPEC_EXPORT double __cdecl remainder(double x, double y) { return ieee_remainder(x, y, nullptr); }
DECLSPEC_EXPORT double __cdecl remainderl(double x, double y) { return ieee_remainder(x, y, nullptr); }
DECLSPEC_EXPORT float __cdecl remainderf(float x, float y) { return (float)ieee_remainder(x, y, nullptr); }

DECLSPEC_EXPORT double __cdecl remquo(double x, double y, int *quo) { return ieee_remainder(x, y, quo); }
DECLSPEC_EXPORT double __cdecl remquol(double x, double y, int *quo) { return ieee_remainder(x, y, quo); }
DECLSPEC_EXPORT float __cdecl remquof(float x, float y, int *quo) { return (float)ieee_remainder(x, y, quo); }

/* ------------------------------------------------------------------ exponentials and logarithms */

/* 2^x, exact for whole numbers. */
DECLSPEC_EXPORT double __cdecl exp2(double x)
{
    if (is_nan(x))
        return x;
    if (x >= 1024)
        return MsvcrtMathError(_OVERFLOW, "exp2", x, 0, INF);
    if (x < -1080) {
        if (is_inf(x))
            return 0;
        return MsvcrtMathError(_UNDERFLOW, "exp2", x, 0, 0);
    }
    double n = round(x);
    double f = x - n;
    if (f == 0)
        return scale(1, (int)n);
    DD z = two_prod(f, LN2_HI);
    z.lo += f * LN2_LO;
    z = fast_two_sum(z.hi, z.lo);
    int k;
    DD e = exp_kernel(z.hi, z.lo, &k);
    double r = scale_dd(e, k + (int)n);
    if (r == 0)
        return MsvcrtMathError(_UNDERFLOW, "exp2", x, 0, 0);
    return r;
}

DECLSPEC_EXPORT float __cdecl exp2f(float x) { return (float)exp2(x); }
DECLSPEC_EXPORT double __cdecl exp2l(double x) { return exp2(x); }

/* e^x - 1: exact near 0, -1 far below it. */
DECLSPEC_EXPORT double __cdecl expm1(double x)
{
    if (is_nan(x) || x == 0)
        return x;
    if (x > EXP_OVERFLOW)
        return MsvcrtMathError(_OVERFLOW, "expm1", x, 0, INF);
    if (x < -40) {
        MsvcrtFpRaise(MSVCRT_FE_INEXACT);
        return -1.0;
    }
    DD r = expm1_dd(x);
    return r.hi + r.lo;
}

DECLSPEC_EXPORT float __cdecl expm1f(float x) { return (float)expm1(x); }
DECLSPEC_EXPORT double __cdecl expm1l(double x) { return expm1(x); }

/* log(1 + x): the sum 1 + x is split in two so the digits it loses are put back. */
DECLSPEC_EXPORT double __cdecl log1p(double x)
{
    if (is_nan(x) || x == 0)
        return x;
    if (x < -1) {
        MsvcrtFpRaise(MSVCRT_FE_INVALID);
        return from_bits(0x7ff8000000000000ull);
    }
    if (x == -1) {
        MsvcrtFpRaise(MSVCRT_FE_DIVBYZERO);
        return -INF;
    }
    if (is_inf(x))
        return x;
    if (fabs_(x) < 0x1p-27)
        return x - 0.5 * x * x;
    DD u = two_sum(1.0, x);
    DD l = log_kernel(u.hi);
    l.lo += u.lo / u.hi;
    return l.hi + l.lo;
}

DECLSPEC_EXPORT float __cdecl log1pf(float x) { return (float)log1p(x); }
DECLSPEC_EXPORT double __cdecl log1pl(double x) { return log1p(x); }

DECLSPEC_EXPORT double __cdecl hypot(double x, double y) { return _hypot(x, y); }
DECLSPEC_EXPORT float __cdecl hypotf(float x, float y) { return _hypotf(x, y); }
DECLSPEC_EXPORT double __cdecl hypotl(double x, double y) { return _hypot(x, y); }

DECLSPEC_EXPORT double __cdecl log2l(double x) { return log2(x); }
DECLSPEC_EXPORT double __cdecl cbrtl(double x) { return cbrt(x); }
DECLSPEC_EXPORT double __cdecl acoshl(double x) { return acosh(x); }
DECLSPEC_EXPORT double __cdecl asinhl(double x) { return asinh(x); }
DECLSPEC_EXPORT double __cdecl atanhl(double x) { return atanh(x); }

/* ------------------------------------------------------------------ error function */

#define TWO_OVER_SQRTPI_HI 0x1.20dd750429b6dp+0
#define TWO_OVER_SQRTPI_LO 0x1.1ae3a914fed80p-56
#define INV_SQRTPI_HI 0x1.20dd750429b6dp-1
#define INV_SQRTPI_LO 0x1.1ae3a914fed80p-57

/* e^-(x x) as a double-double times 2^*n. */
static DD exp_neg_square(double x, int *n)
{
    DD s = two_prod(x, x);
    return exp_kernel(-s.hi, -s.lo, n);
}

/*
 * erf for x in (0, 2.5): 2/sqrt(pi) e^-x^2 times the sum of 2^k x^(2k+1) / (1 3 5 ... 2k+1), whose terms are all
 * positive, so nothing cancels.
 */
static DD erf_series(double x)
{
    DD x2 = two_prod(x, x);
    DD two_x2 = dd(2 * x2.hi, 2 * x2.lo);
    DD term = dd(x, 0), sum = term;
    for (int k = 1; k < 300; k++) {
        term = dd_div(dd_mul(term, two_x2), dd(2 * k + 1, 0));
        sum = dd_add(sum, term);
        if (term.hi < sum.hi * 1e-34)
            break;
    }
    int n;
    DD e = exp_neg_square(x, &n);
    DD r = dd_mul(dd_mul(sum, e), dd(TWO_OVER_SQRTPI_HI, TWO_OVER_SQRTPI_LO));
    return dd(scale(r.hi, n), scale(r.lo, n));
}

/*
 * erfc for x of 2.5 and more, by the continued fraction e^-x^2 / (sqrt(pi) (x + 1/2 / (x + 1 / (x + 3/2 / ...)))).
 * Returns the mantissa part, the power of two goes to *n.
 */
static DD erfc_fraction(double x, int *n)
{
    DD f = dd(x, 0);
    for (int k = 120; k >= 1; k--)
        f = dd_add(dd(x, 0), dd_div(dd(k * 0.5, 0), f));
    DD e = exp_neg_square(x, n);
    return dd_div(dd_mul(e, dd(INV_SQRTPI_HI, INV_SQRTPI_LO)), f);
}

#define ERF_SPLIT 2.5
#define ERFC_UNDERFLOW 27.3

DECLSPEC_EXPORT double __cdecl erf(double x)
{
    if (is_nan(x) || x == 0)
        return x;
    if (is_inf(x))
        return x < 0 ? -1.0 : 1.0;
    double a = fabs_(x);
    DD r;
    if (a < ERF_SPLIT) {
        r = erf_series(a);
    } else if (a > 6.5) {
        r = dd(1, 0);
        MsvcrtFpRaise(MSVCRT_FE_INEXACT);
    } else {
        int n;
        DD c = erfc_fraction(a, &n);
        c = dd(scale(c.hi, n), scale(c.lo, n));
        r = dd_add(dd(1, 0), dd(-c.hi, -c.lo));
    }
    double v = r.hi + r.lo;
    return x < 0 ? -v : v;
}

DECLSPEC_EXPORT double __cdecl erfc(double x)
{
    if (is_nan(x))
        return x;
    if (is_inf(x))
        return x < 0 ? 2.0 : 0.0;
    double a = fabs_(x);
    DD r;
    if (a < ERF_SPLIT) {
        DD e = a == 0 ? dd(0, 0) : erf_series(a);
        r = x < 0 ? dd_add(dd(1, 0), e) : dd_add(dd(1, 0), dd(-e.hi, -e.lo));
    } else if (a > ERFC_UNDERFLOW && x > 0) {
        MsvcrtFpRaise(MSVCRT_FE_UNDERFLOW);
        return 0.0;
    } else if (x > 0) {
        int n;
        DD c = erfc_fraction(a, &n);
        double v = scale_dd(c, n);
        if (v == 0)
            MsvcrtFpRaise(MSVCRT_FE_UNDERFLOW);
        return v;
    } else if (a > 6.5) {
        MsvcrtFpRaise(MSVCRT_FE_INEXACT);
        return 2.0;
    } else {
        int n;
        DD c = erfc_fraction(a, &n);
        c = dd(scale(c.hi, n), scale(c.lo, n));
        r = dd_add(dd(2, 0), dd(-c.hi, -c.lo));
    }
    return r.hi + r.lo;
}

DECLSPEC_EXPORT float __cdecl erff(float x) { return (float)erf(x); }
DECLSPEC_EXPORT float __cdecl erfcf(float x) { return (float)erfc(x); }
DECLSPEC_EXPORT double __cdecl erfl(double x) { return erf(x); }
DECLSPEC_EXPORT double __cdecl erfcl(double x) { return erfc(x); }

/* ------------------------------------------------------------------ gamma function */

#define HALF_LN_2PI_HI 0x1.d67f1c864beb5p-1
#define HALF_LN_2PI_LO -0x1.65b5a1b7ff5dfp-55
#define LN_PI_HI 0x1.250d048e7a1bdp+0
#define LN_PI_LO 0x1.7abf2ad8d5088p-57
#define GAMMA_SHIFT 20.0

/*
 * log gamma of z of 20 and more as a double-double, from the stirling series
 * (z - 1/2) ln z - z + ln(2 pi) / 2 + 1/12z - 1/360z^3 + 1/1260z^5 ... which at 20 is good to 1e-21.
 */
static DD lgamma_stirling(DD z)
{
    DD lz = log_kernel(z.hi);
    lz.lo += z.lo / z.hi;
    lz = fast_two_sum(lz.hi, lz.lo);
    DD t = dd_mul(lz, dd_add(z, dd(-0.5, 0)));
    t = dd_add(t, dd(-z.hi, -z.lo));
    t = dd_add(t, dd(HALF_LN_2PI_HI, HALF_LN_2PI_LO));
    double iz = 1 / z.hi, iz2 = iz * iz;
    double tail = iz * (1.0 / 12 +
                        iz2 * (-1.0 / 360 +
                               iz2 * (1.0 / 1260 +
                                      iz2 * (-1.0 / 1680 + iz2 * (1.0 / 1188 + iz2 * (-691.0 / 360360 + iz2 / 156))))));
    return dd_add(t, dd(tail, 0));
}

/*
 * log gamma of x above 0, with the product it was shifted by taken out: x + n is the first of x + k to pass the
 * limit and *product is x (x + 1) ... (x + n - 1), the sums kept exact in a double-double.
 */
static DD lgamma_positive(DD x, DD *product)
{
    DD p = dd(1, 0);
    int n = x.hi < GAMMA_SHIFT ? (int)(GAMMA_SHIFT - x.hi) + 1 : 0;
    for (int i = 0; i < n; i++)
        p = dd_mul(p, dd_add(x, dd(i, 0)));
    *product = p;
    return lgamma_stirling(dd_add(x, dd(n, 0)));
}

/* log of a double-double above zero. */
static DD log_dd(DD v)
{
    DD l = log_kernel(v.hi);
    l.lo += v.lo / v.hi;
    return fast_two_sum(l.hi, l.lo);
}

/* log gamma of x above 0 as a double-double. */
static DD lgamma_of_positive(DD x)
{
    DD product;
    DD g = lgamma_positive(x, &product);
    DD lp = log_dd(product);
    return dd_add(g, dd(-lp.hi, -lp.lo));
}

/* sin(pi m) for m in (0, 1), summing the taylor series of the smaller of m and 1 - m in double-double. */
static DD sin_pi(double m)
{
    if (m > 0.5)
        m = 1 - m;
    DD a = dd_mul_d(dd(PI_HI, PI_LO), m);
    DD a2 = dd_mul(a, a);
    DD term = a, sum = a;
    for (int n = 1; n < 40; n++) {
        term = dd_div(dd_mul(term, a2), dd((2 * n) * (2 * n + 1), 0));
        term = dd(-term.hi, -term.lo);
        sum = dd_add(sum, term);
        if (fabs_(term.hi) < fabs_(sum.hi) * 1e-34)
            break;
    }
    return sum;
}

/* log |gamma x| for finite x that is no pole, as a double-double. */
static DD lgamma_dd(double x, int *negative)
{
    *negative = 0;
    if (x > 0)
        return lgamma_of_positive(dd(x, 0));
    /* below zero the reflection log|gamma x| = log(pi) - log|sin(pi x)| - log gamma(1 - x). */
    double fl = floor(x);
    double m = x - fl;
    *negative = fmod(fl, 2) != 0;
    DD g = lgamma_of_positive(two_sum(1, -x));
    DD ls = log_dd(sin_pi(m));
    DD r = dd_add(dd(LN_PI_HI, LN_PI_LO), dd(-ls.hi, -ls.lo));
    return dd_add(r, dd(-g.hi, -g.lo));
}

#define EULER_HI 0x1.2788cfc6fb619p-1
#define EULER_LO -0x1.6cb90701fbfabp-58
#define ONE_MINUS_EULER_HI 0x1.b0ee6072093cep-2
#define ONE_MINUS_EULER_LO 0x1.6cb90701fbfabp-58

/*
 * the series of log gamma around 1 and 2, in e = x - 1 and e = x - 2 with |e| below 1/100:
 * log gamma(1 + e) = -g e + sum zeta(k) (-e)^k / k and log gamma(2 + e) = (1 - g) e + sum (zeta(k) - 1) (-e)^k / k,
 * g being euler's constant. The coefficients are the sums for k from 2 to 14.
 */
static const double g_lgamma1[] = {
    0.8224670334241132,    -0.40068563438653143, 0.27058080842778454, -0.20738555102867398, 0.1695571769974082,
    -0.1440498967688461,   0.12550966952474304,  -0.11133426586956469, 0.1000994575127818,   -0.09095401714582904,
    0.083353840546109,     -0.0769325164113522,  0.07143294629536133,
};
static const double g_lgamma2[] = {
    0.3224670334241132,     -0.0673523010531981,    0.020580808427784546,   -0.007385551028673986,
    0.0028905103307415234,  -0.001192753911703261,  0.0005096695247430425,  -0.00022315475845357939,
    9.945751278180853e-05,  -4.492623673813314e-05, 2.050721277567069e-05,  -9.439488275268397e-06,
    4.374866789907488e-06,
};

/* e times the first coefficient and e squared times the polynomial of the others, as a double-double. */
static double lgamma_series(const double *c, DD first, double e)
{
    double p = 0;
    for (int k = 12; k >= 0; k--)
        p = c[k] + e * p;
    DD lead = dd_mul_d(first, e);
    return lead.hi + (lead.lo + e * e * p);
}

static double lgamma_near_one(double e)
{
    return lgamma_series(g_lgamma1, dd(-EULER_HI, -EULER_LO), e);
}

static double lgamma_near_two(double e)
{
    return lgamma_series(g_lgamma2, dd(ONE_MINUS_EULER_HI, ONE_MINUS_EULER_LO), e);
}

DECLSPEC_EXPORT double __cdecl lgamma(double x)
{
    if (is_nan(x))
        return x;
    if (is_inf(x))
        return INF;
    if (x == 0 || (x < 0 && floor(x) == x)) {
        MsvcrtFpRaise(MSVCRT_FE_DIVBYZERO);
        return INF;
    }
    if (x == 1 || x == 2)
        return 0.0;
    if (fabs_(x - 1) < 0.01)
        return lgamma_near_one(x - 1);
    if (fabs_(x - 2) < 0.01)
        return lgamma_near_two(x - 2);
    if (x > 2.5e305) {
        MsvcrtFpRaise(MSVCRT_FE_OVERFLOW);
        return INF;
    }
    int negative;
    DD r = lgamma_dd(x, &negative);
    return r.hi + r.lo;
}

DECLSPEC_EXPORT float __cdecl lgammaf(float x) { return (float)lgamma(x); }
DECLSPEC_EXPORT double __cdecl lgammal(double x) { return lgamma(x); }

DECLSPEC_EXPORT double __cdecl tgamma(double x)
{
    if (is_nan(x))
        return x;
    if (x == 0) {
        MsvcrtFpRaise(MSVCRT_FE_DIVBYZERO);
        return (bits(x) & SIGN_BIT) ? -INF : INF;
    }
    if (x < 0 && (floor(x) == x || is_inf(x))) {
        MsvcrtFpRaise(MSVCRT_FE_INVALID);
        return from_bits(0x7ff8000000000000ull);
    }
    if (x == INF)
        return x;
    if (x > 171.7) {
        MsvcrtFpRaise(MSVCRT_FE_OVERFLOW);
        return INF;
    }
    /* far below zero the result would be a subnormal at most, and is flushed to a positive zero. */
    if (x <= -174.0) {
        MsvcrtFpRaise(MSVCRT_FE_UNDERFLOW);
        return 0.0;
    }
    if (x > 0) {
        /* gamma(x) = gamma(x + n) / (x (x + 1) ... (x + n - 1)). */
        DD lg = lgamma_of_positive(dd(x, 0));
        int n;
        DD e = exp_kernel(lg.hi, lg.lo, &n);
        double r = scale_dd(e, n);
        if (is_inf(r))
            MsvcrtFpRaise(MSVCRT_FE_OVERFLOW);
        return r;
    }
    int negative;
    DD lg = lgamma_dd(x, &negative);
    if (lg.hi < -745) {
        MsvcrtFpRaise(MSVCRT_FE_UNDERFLOW);
        return negative ? -0.0 : 0.0;
    }
    int n;
    DD e = exp_kernel(lg.hi, lg.lo, &n);
    double r = scale_dd(e, n);
    return negative ? -r : r;
}

DECLSPEC_EXPORT float __cdecl tgammaf(float x) { return (float)tgamma(x); }
DECLSPEC_EXPORT double __cdecl tgammal(double x) { return tgamma(x); }
