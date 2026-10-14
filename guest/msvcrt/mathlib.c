/*
 * the mathematical functions. each works in double-double arithmetic
 * where the final rounding needs it, so results stay within one ulp:
 * table driven exp and log, payne-hanek reduction for trigonometry, and
 * arctangent tables for the inverse functions. polynomials are taylor
 * series truncated where the next term is below 2^-60 of the result.
 */
#include "msvcrt_private.h"
#include "mathtab.h"

#pragma STDC FP_CONTRACT OFF

typedef union {
    double d;
    uint64_t u;
} DBITS;

static inline uint64_t bits(double d)
{
    DBITS b = {d};
    return b.u;
}

static inline double from_bits(uint64_t u)
{
    DBITS b;
    b.u = u;
    return b.d;
}

/* msvcrt's default nan, the negative quiet nan of sse. */
#define NAN_IND from_bits(0xfff8000000000000ull)
#define INF from_bits(0x7ff0000000000000ull)

static inline int is_nan(double x)
{
    return (bits(x) & 0x7fffffffffffffffull) > 0x7ff0000000000000ull;
}

static inline int is_inf(double x)
{
    return (bits(x) & 0x7fffffffffffffffull) == 0x7ff0000000000000ull;
}

static inline double fabs_(double x)
{
    return from_bits(bits(x) & 0x7fffffffffffffffull);
}

static inline double sqrt_(double x)
{
    return __builtin_elementwise_sqrt(x);
}

/* 2^n for n from -1022 to 1023. */
static inline double pow2(int n)
{
    return from_bits((uint64_t)(n + 1023) << 52);
}

/* x * 2^n for any n, with one final rounding for subnormal results. */
static double scale(double x, int n)
{
    if (n > 1023) {
        x *= pow2(1023);
        n -= 1023;
        if (n > 1023) {
            x *= pow2(1023);
            n -= 1023;
            if (n > 1023)
                n = 1023;
        }
    } else if (n < -1022) {
        /* scales most of the way first, so only the last product rounds into the subnormals. */
        x *= pow2(-1022 + 53);
        n += 1022 - 53;
        if (n < -1022) {
            x *= pow2(-1022 + 53);
            n += 1022 - 53;
            if (n < -1022)
                n = -1022;
        }
    }
    return x * pow2(n);
}

/* ------------------------------------------------------------------ double-double arithmetic */

typedef struct {
    double hi, lo;
} DD;

static inline DD two_sum(double a, double b)
{
    double s = a + b;
    double bb = s - a;
    DD r = {s, (a - (s - bb)) + (b - bb)};
    return r;
}

static inline DD fast_two_sum(double a, double b)
{
    double s = a + b;
    DD r = {s, b - (s - a)};
    return r;
}

static inline void split(double a, double *hi, double *lo)
{
    double c = 134217729.0 * a;
    *hi = c - (c - a);
    *lo = a - *hi;
}

static inline DD two_prod(double a, double b)
{
    double p = a * b;
    double ah, al, bh, bl;
    split(a, &ah, &al);
    split(b, &bh, &bl);
    DD r = {p, ((ah * bh - p) + ah * bl + al * bh) + al * bl};
    return r;
}

static inline DD dd_add(DD a, DD b)
{
    DD s = two_sum(a.hi, b.hi);
    DD t = two_sum(a.lo, b.lo);
    s.lo += t.hi;
    s = fast_two_sum(s.hi, s.lo);
    s.lo += t.lo;
    return fast_two_sum(s.hi, s.lo);
}

static inline DD dd_mul(DD a, DD b)
{
    DD p = two_prod(a.hi, b.hi);
    p.lo += a.hi * b.lo + a.lo * b.hi;
    return fast_two_sum(p.hi, p.lo);
}

static inline DD dd_mul_d(DD a, double b)
{
    DD p = two_prod(a.hi, b);
    p.lo += a.lo * b;
    return fast_two_sum(p.hi, p.lo);
}

static inline DD dd_div(DD a, DD b)
{
    double q = a.hi / b.hi;
    DD p = dd_mul_d(b, q);
    DD r = two_sum(a.hi, -p.hi);
    r.lo -= p.lo;
    r.lo += a.lo;
    double q2 = (r.hi + r.lo) / b.hi;
    return fast_two_sum(q, q2);
}

static inline DD dd_sqrt(DD a)
{
    double s = sqrt_(a.hi);
    DD p = two_prod(s, s);
    double c = ((a.hi - p.hi) - p.lo + a.lo) / (2 * s);
    return fast_two_sum(s, c);
}

static inline DD dd(double hi, double lo)
{
    DD r = {hi, lo};
    return r;
}

/* (hi + lo) 2^n rounded once, also when the result is subnormal. */
static double scale_dd(DD v, int n)
{
    int e = (int)((bits(v.hi) >> 52) & 0x7ff) - 1022;
    if (e + n >= -1021 || v.hi == 0)
        return scale(v.hi + v.lo, n);
    if (e + n < -1100)
        return scale(v.hi, n);
    /* adding 2^-1022 scaled by 2^-n makes the sum round at the subnormal unit. */
    double c = from_bits(((uint64_t)(-1022 - n + 1023) << 52) | (bits(v.hi) & 0x8000000000000000ull));
    DD s = two_sum(c, v.hi);
    double w = s.hi + (s.lo + v.lo);
    return scale(w - c, n);
}

/* ------------------------------------------------------------------ errors */

struct _exception {
    int type;
    char *name;
    double arg1;
    double arg2;
    double retval;
};

typedef int(__cdecl *MATHERR)(struct _exception *);

static PVOID g_usermatherr;
/* nonzero, like msvcrt's 9876, until a program installs its _matherr. */
static int g_matherr_off = 1;

void __cdecl __setusermatherr(MATHERR handler)
{
    g_usermatherr = EncodePointer((PVOID)handler);
    g_matherr_off = 0;
}

int __cdecl _matherr(struct _exception *e)
{
    if (g_matherr_off)
        return 0;
    MATHERR handler = (MATHERR)DecodePointer(g_usermatherr);
    return handler ? handler(e) : 0;
}

static void errno_from_type(int type)
{
    if (type == _DOMAIN)
        MsvcrtSetErrno(EDOM);
    else if (type == _SING || type == _OVERFLOW)
        MsvcrtSetErrno(ERANGE);
}

/* reports a math error through the program's _matherr when one is installed, else through errno. */
double MsvcrtMathError(int type, const char *name, double arg1, double arg2, double retval)
{
    if (g_matherr_off) {
        errno_from_type(type);
        return retval;
    }
    struct _exception e = {type, (char *)name, arg1, arg2, retval};
    if (!_matherr(&e))
        errno_from_type(type);
    return e.retval;
}

/* ------------------------------------------------------------------ basic operations */

double __cdecl fabs(double x)
{
    return fabs_(x);
}

double __cdecl floor(double x)
{
    uint64_t u = bits(x);
    int e = (int)((u >> 52) & 0x7ff) - 1023;
    if (e >= 52)
        return x;
    if (e < 0) {
        if (!(u & 0x7fffffffffffffffull))
            return x;
        return (int64_t)u < 0 ? -1.0 : 0.0;
    }
    uint64_t mask = 0x000fffffffffffffull >> e;
    if (!(u & mask))
        return x;
    if ((int64_t)u < 0)
        u += mask;
    return from_bits(u & ~mask);
}

double __cdecl ceil(double x)
{
    uint64_t u = bits(x);
    int e = (int)((u >> 52) & 0x7ff) - 1023;
    if (e >= 52)
        return x;
    if (e < 0) {
        if (!(u & 0x7fffffffffffffffull))
            return x;
        return (int64_t)u < 0 ? -0.0 : 1.0;
    }
    uint64_t mask = 0x000fffffffffffffull >> e;
    if (!(u & mask))
        return x;
    if ((int64_t)u >= 0)
        u += mask;
    return from_bits(u & ~mask);
}

static double trunc_(double x)
{
    uint64_t u = bits(x);
    int e = (int)((u >> 52) & 0x7ff) - 1023;
    if (e >= 52)
        return x;
    if (e < 0)
        return from_bits(u & 0x8000000000000000ull);
    return from_bits(u & ~(0x000fffffffffffffull >> e));
}

/* the nearest integer, halfway cases away from zero, for |x| below 2^62. */
static int64_t round_to_int(double x)
{
    return (int64_t)(x + (x >= 0 ? 0.5 : -0.5));
}

double __cdecl modf(double x, double *ip)
{
    double t = trunc_(x);
    if (ip)
        *ip = t;
    if (is_inf(x))
        return from_bits(bits(x) & 0x8000000000000000ull);
    if (is_nan(x))
        return x;
    double f = x - t;
    return from_bits(bits(f) | (bits(x) & 0x8000000000000000ull));
}

double __cdecl frexp(double x, int *exp)
{
    uint64_t u = bits(x);
    int e = (int)((u >> 52) & 0x7ff);
    *exp = 0;
    if (e == 0x7ff || !(u & 0x7fffffffffffffffull))
        return x;
    if (!e) {
        x *= pow2(64);
        u = bits(x);
        e = (int)((u >> 52) & 0x7ff) - 64;
    }
    *exp = e - 1022;
    return from_bits((u & 0x800fffffffffffffull) | 0x3fe0000000000000ull);
}

double __cdecl ldexp(double x, int n)
{
    if (is_nan(x) || is_inf(x) || x == 0)
        return x;
    double r = scale(x, n);
    if (is_inf(r))
        return MsvcrtMathError(_OVERFLOW, "ldexp", x, n, r);
    if (r == 0)
        return MsvcrtMathError(_UNDERFLOW, "ldexp", x, n, r);
    return r;
}

double __cdecl _scalb(double x, long n)
{
    if (n > 100000)
        n = 100000;
    if (n < -100000)
        n = -100000;
    if (is_nan(x) || is_inf(x) || x == 0)
        return x;
    double r = scale(x, (int)n);
    if (is_inf(r))
        return MsvcrtMathError(_OVERFLOW, "_scalb", x, n, r);
    if (r == 0)
        return MsvcrtMathError(_UNDERFLOW, "_scalb", x, n, r);
    return r;
}

double __cdecl _copysign(double x, double y)
{
    return from_bits((bits(x) & 0x7fffffffffffffffull) | (bits(y) & 0x8000000000000000ull));
}

double __cdecl _chgsign(double x)
{
    return from_bits(bits(x) ^ 0x8000000000000000ull);
}

int __cdecl _isnan(double x)
{
    return is_nan(x);
}

int __cdecl _finite(double x)
{
    return ((bits(x) >> 52) & 0x7ff) != 0x7ff;
}

#define _FPCLASS_SNAN 0x0001
#define _FPCLASS_QNAN 0x0002
#define _FPCLASS_NINF 0x0004
#define _FPCLASS_NN 0x0008
#define _FPCLASS_ND 0x0010
#define _FPCLASS_NZ 0x0020
#define _FPCLASS_PZ 0x0040
#define _FPCLASS_PD 0x0080
#define _FPCLASS_PN 0x0100
#define _FPCLASS_PINF 0x0200

int __cdecl _fpclass(double x)
{
    uint64_t u = bits(x);
    int neg = (int64_t)u < 0;
    int e = (int)((u >> 52) & 0x7ff);
    uint64_t f = u & 0x000fffffffffffffull;
    if (e == 0x7ff) {
        if (!f)
            return neg ? _FPCLASS_NINF : _FPCLASS_PINF;
        return (f & 0x0008000000000000ull) ? _FPCLASS_QNAN : _FPCLASS_SNAN;
    }
    if (!e)
        return f ? (neg ? _FPCLASS_ND : _FPCLASS_PD) : (neg ? _FPCLASS_NZ : _FPCLASS_PZ);
    return neg ? _FPCLASS_NN : _FPCLASS_PN;
}

double __cdecl _logb(double x)
{
    if (is_nan(x))
        return x;
    if (is_inf(x))
        return INF;
    if (x == 0)
        return MsvcrtMathError(_SING, "_logb", x, 0, -INF);
    int e;
    frexp(x, &e);
    return e - 1;
}

double __cdecl _nextafter(double x, double y)
{
    if (is_nan(x))
        return x;
    if (is_nan(y))
        return y;
    if (x == y)
        return x;
    if (x == 0) {
        double r = from_bits(1 | (bits(y) & 0x8000000000000000ull));
        return r;
    }
    uint64_t u = bits(x);
    if ((x < y) == (x > 0))
        u++;
    else
        u--;
    double r = from_bits(u);
    if (is_inf(r))
        MsvcrtSetErrno(ERANGE);
    return r;
}

/* exact remainder of x / y with the sign of x, by long division of the mantissas. */
double __cdecl fmod(double x, double y)
{
    if (is_nan(x) || is_nan(y))
        return x + y;
    if (is_inf(x) || y == 0)
        return MsvcrtMathError(_DOMAIN, "fmod", x, y, NAN_IND);
    if (is_inf(y) || x == 0)
        return x;
    uint64_t ux = bits(x), uy = bits(y);
    uint64_t sign = ux & 0x8000000000000000ull;
    ux &= 0x7fffffffffffffffull;
    uy &= 0x7fffffffffffffffull;
    if (ux < uy)
        return x;
    if (ux == uy)
        return from_bits(sign);
    int ex = (int)(ux >> 52), ey = (int)(uy >> 52);
    uint64_t mx = ux & 0x000fffffffffffffull, my = uy & 0x000fffffffffffffull;
    if (ex)
        mx |= 1ull << 52;
    else {
        ex = 1;
        while (!(mx & (1ull << 52))) {
            mx <<= 1;
            ex--;
        }
    }
    if (ey)
        my |= 1ull << 52;
    else {
        ey = 1;
        while (!(my & (1ull << 52))) {
            my <<= 1;
            ey--;
        }
    }
    for (; ex > ey; ex--) {
        if (mx >= my)
            mx -= my;
        mx <<= 1;
    }
    if (mx >= my)
        mx -= my;
    if (!mx)
        return from_bits(sign);
    while (!(mx & (1ull << 52))) {
        mx <<= 1;
        ey--;
    }
    if (ey > 0)
        return from_bits(sign | (uint64_t)ey << 52 | (mx & 0x000fffffffffffffull));
    return from_bits(sign | (mx >> (1 - ey)));
}

double __cdecl sqrt(double x)
{
    if (is_nan(x))
        return x;
    if (x < 0)
        return MsvcrtMathError(_DOMAIN, "sqrt", x, 0, NAN_IND);
    return sqrt_(x);
}

/* ------------------------------------------------------------------ exponential */

/*
 * e^(xh + xl) as hi + lo times 2^*n, for |x| below 746: x = k ln2/64 + r
 * with |r| below ln2/128, e^x = 2^(k/64) e^r, 2^(j/64) from the table.
 */
static DD exp_kernel(double xh, double xl, int *n)
{
    int k = (int)round_to_int(xh * (64 / LN2_HI));
    double r1 = xh - k * LN2_64_HI;
    DD r = two_sum(r1, -k * LN2_64_LO);
    r.lo += xl;
    r = fast_two_sum(r.hi, r.lo);
    int j = k & 63;
    *n = (k - j) / 64;
    double rh = r.hi;
    /* q is e^r - 1 - r; the taylor series ends at r^7, below 2^-75 relative. */
    double q = rh * rh *
               (0.5 + rh * (1.0 / 6 + rh * (1.0 / 24 + rh * (1.0 / 120 + rh * (1.0 / 720 + rh * (1.0 / 5040))))));
    DD p = fast_two_sum(rh, q + r.lo * (1 + rh));
    double th = g_exp2_table[j][0], tl = g_exp2_table[j][1];
    DD tp = two_prod(th, p.hi);
    tp.lo += th * p.lo + tl * p.hi + tl;
    DD s = fast_two_sum(th, tp.hi);
    s.lo += tp.lo;
    return fast_two_sum(s.hi, s.lo);
}

#define EXP_OVERFLOW 709.782712893383973096
#define EXP_UNDERFLOW -745.13321910194110842

double __cdecl exp(double x)
{
    if (is_nan(x))
        return x;
    if (x > EXP_OVERFLOW)
        return MsvcrtMathError(_OVERFLOW, "exp", x, 0, INF);
    if (x < EXP_UNDERFLOW) {
        if (is_inf(x))
            return 0;
        return MsvcrtMathError(_UNDERFLOW, "exp", x, 0, 0);
    }
    if (fabs_(x) < 0x1p-60)
        return 1 + x;
    int n;
    DD e = exp_kernel(x, 0, &n);
    double r = scale_dd(e, n);
    if (r == 0)
        return MsvcrtMathError(_UNDERFLOW, "exp", x, 0, 0);
    return r;
}

/* e^x - 1 as a double-double. */
static DD expm1_dd(double x)
{
    if (fabs_(x) < 0x1p-5) {
        /* taylor series to x^11, below 2^-60 of x for |x| < 1/32. */
        double q = x * x *
                   (0.5 +
                    x * (1.0 / 6 +
                         x * (1.0 / 24 +
                              x * (1.0 / 120 +
                                   x * (1.0 / 720 +
                                        x * (1.0 / 5040 + x * (1.0 / 40320 + x * (1.0 / 362880 + x / 3628800))))))));
        return fast_two_sum(x, q);
    }
    int n;
    DD e = exp_kernel(x, 0, &n);
    e.hi = scale(e.hi, n);
    e.lo = scale(e.lo, n);
    DD r = two_sum(e.hi, -1);
    r.lo += e.lo;
    return fast_two_sum(r.hi, r.lo);
}

/* ------------------------------------------------------------------ logarithm */

/*
 * log x as hi + lo for finite x > 0: x = 2^e m with m in [0.70, 1.42],
 * c = k/128 the table point nearest m, r = (m - c)/c with |r| < 1/180,
 * log x = e ln2 + log c + log(1 + r).
 */
static DD log_kernel(double x)
{
    int e;
    double m = frexp(x, &e);
    m *= 2;
    e--;
    if (m > 1.41421356237309504880) {
        m *= 0.5;
        e++;
    }
    int k = (int)round_to_int(m * 128);
    double c = k / 128.0;
    double d = m - c;
    /* r = d / c to double-double precision. */
    double rh = d / c;
    DD pc = two_prod(rh, c);
    double rl = ((d - pc.hi) - pc.lo) / c;
    DD r = fast_two_sum(rh, rl);
    /* log(1 + r) = r - r^2/2 + r^3/3 - ...: terms past r^9 are below 2^-75. */
    DD r2 = two_prod(r.hi, r.hi);
    r2.lo += 2 * r.hi * r.lo;
    double t = r.hi;
    double tail =
        t * t * t *
        (1.0 / 3 + t * (-1.0 / 4 + t * (1.0 / 5 + t * (-1.0 / 6 + t * (1.0 / 7 + t * (-1.0 / 8 + t * (1.0 / 9)))))));
    DD poly = dd_add(r, dd(-0.5 * r2.hi, -0.5 * r2.lo + tail));
    DD lc = dd(g_log_table[k - 90][0], g_log_table[k - 90][1]);
    DD el = dd_mul_d(dd(LN2_HI, LN2_LO), e);
    return dd_add(dd_add(el, lc), poly);
}

double __cdecl log(double x)
{
    if (is_nan(x))
        return x;
    if (x == 0)
        return MsvcrtMathError(_SING, "log", x, 0, -INF);
    if (x < 0)
        return MsvcrtMathError(_DOMAIN, "log", x, 0, NAN_IND);
    if (is_inf(x))
        return x;
    DD l = log_kernel(x);
    return l.hi + l.lo;
}

double __cdecl log10(double x)
{
    if (is_nan(x))
        return x;
    if (x == 0)
        return MsvcrtMathError(_SING, "log10", x, 0, -INF);
    if (x < 0)
        return MsvcrtMathError(_DOMAIN, "log10", x, 0, NAN_IND);
    if (is_inf(x))
        return x;
    DD l = dd_mul(log_kernel(x), dd(INV_LN10_HI, INV_LN10_LO));
    return l.hi + l.lo;
}

/* ------------------------------------------------------------------ power */

/* 0 when y is no integer, 1 when it is odd, 2 when even. */
static int integer_kind(double y)
{
    if (is_inf(y) || is_nan(y))
        return 0;
    if (fabs_(y) >= 0x1p53)
        return 2;
    if (trunc_(y) != y)
        return 0;
    return ((int64_t)y & 1) ? 1 : 2;
}

double __cdecl pow(double x, double y)
{
    if (y == 0)
        return 1;
    if (x == 1)
        return 1;
    if (is_nan(x) || is_nan(y))
        return x + y;
    int kind = integer_kind(y);
    if (is_inf(y)) {
        double ax = fabs_(x);
        if (ax == 1)
            return 1;
        if ((ax < 1) == (y < 0))
            return INF;
        return 0;
    }
    if (x == 0) {
        int neg = (int64_t)bits(x) < 0 && kind == 1;
        if (y < 0)
            return MsvcrtMathError(_SING, "pow", x, y, neg ? -INF : INF);
        return neg ? -0.0 : 0.0;
    }
    if (is_inf(x)) {
        int neg = x < 0 && kind == 1;
        if (y < 0)
            return neg ? -0.0 : 0.0;
        return neg ? -INF : INF;
    }
    double sign = 1;
    if (x < 0) {
        if (!kind)
            return MsvcrtMathError(_DOMAIN, "pow", x, y, NAN_IND);
        if (kind == 1)
            sign = -1;
        x = -x;
    }
    DD l = log_kernel(x);
    DD z = two_prod(y, l.hi);
    z.lo += y * l.lo;
    z = fast_two_sum(z.hi, z.lo);
    if (z.hi > EXP_OVERFLOW + 1)
        return MsvcrtMathError(_OVERFLOW, "pow", sign * x, y, sign * INF);
    if (z.hi < EXP_UNDERFLOW - 1)
        return MsvcrtMathError(_UNDERFLOW, "pow", sign * x, y, sign * 0.0);
    int n;
    DD e = exp_kernel(z.hi, z.lo, &n);
    double r = scale_dd(e, n);
    if (is_inf(r))
        return MsvcrtMathError(_OVERFLOW, "pow", sign * x, y, sign * INF);
    if (r == 0)
        return MsvcrtMathError(_UNDERFLOW, "pow", sign * x, y, sign * 0.0);
    return sign * r;
}

/* ------------------------------------------------------------------ hyperbolic functions */

double __cdecl sinh(double x)
{
    if (is_nan(x) || is_inf(x))
        return x;
    double ax = fabs_(x);
    double s = x < 0 ? -1 : 1;
    if (ax < 0x1p-28)
        return x;
    if (ax < 22) {
        /* (E + E/(E + 1))/2 with E = e^|x| - 1. */
        DD e = expm1_dd(ax);
        DD q = dd_div(e, dd_add(e, dd(1, 0)));
        DD r = dd_add(e, q);
        return s * 0.5 * (r.hi + r.lo);
    }
    if (ax > EXP_OVERFLOW + 0.6931471805599453)
        return MsvcrtMathError(_OVERFLOW, "sinh", x, 0, s * INF);
    /* e^|x|/2 = e^(|x| - ln2), which avoids the early overflow of e^|x|. */
    DD z = two_sum(ax, -LN2_HI);
    int n;
    DD e = exp_kernel(z.hi, z.lo - LN2_LO, &n);
    double r = scale(e.hi + e.lo, n);
    if (is_inf(r))
        return MsvcrtMathError(_OVERFLOW, "sinh", x, 0, s * INF);
    return s * r;
}

double __cdecl cosh(double x)
{
    if (is_nan(x))
        return x;
    double ax = fabs_(x);
    if (is_inf(x))
        return INF;
    if (ax < 0x1p-28)
        return 1;
    if (ax < 22) {
        int n;
        DD e = exp_kernel(ax, 0, &n);
        e.hi = scale(e.hi, n);
        e.lo = scale(e.lo, n);
        DD r = dd_add(e, dd_div(dd(1, 0), e));
        return 0.5 * (r.hi + r.lo);
    }
    if (ax > EXP_OVERFLOW + 0.6931471805599453)
        return MsvcrtMathError(_OVERFLOW, "cosh", x, 0, INF);
    DD z = two_sum(ax, -LN2_HI);
    int n;
    DD e = exp_kernel(z.hi, z.lo - LN2_LO, &n);
    double r = scale(e.hi + e.lo, n);
    if (is_inf(r))
        return MsvcrtMathError(_OVERFLOW, "cosh", x, 0, INF);
    return r;
}

double __cdecl tanh(double x)
{
    if (is_nan(x))
        return x;
    double ax = fabs_(x);
    double s = x < 0 ? -1 : 1;
    if (ax < 0x1p-28)
        return x;
    if (ax >= 22)
        return s;
    /* E/(E + 2) with E = e^(2|x|) - 1. */
    DD e = expm1_dd(2 * ax);
    DD r = dd_div(e, dd_add(e, dd(2, 0)));
    return s * (r.hi + r.lo);
}

/* ------------------------------------------------------------------ trigonometric functions */

/* reduces |x| to r = |x| - q pi/2 with |r| <= pi/4 as hi + lo; returns q mod 4. */
static int reduce(double x, DD *r)
{
    if (x <= 0.785398163397448309616) {
        *r = dd(x, 0);
        return 0;
    }
    if (x < 0x1p20) {
        /* cody and waite with pi/2 in four parts; k p1, k p2 and k p3 are exact. */
        int k = (int)round_to_int(x * TWO_OVER_PI_HI);
        double a = x - k * PIO2_1;
        DD s = two_sum(a, -k * PIO2_2);
        DD t = two_sum(s.hi, -k * PIO2_3);
        double lo = s.lo + t.lo - k * PIO2_4;
        *r = fast_two_sum(t.hi, lo);
        return k & 3;
    }
    /*
     * payne and hanek: x = M 2^E with an integer M of 53 bits. the bits of
     * 2/pi above 2^(2-E) only add multiples of 4 to x 2/pi, so the product
     * starts with the word holding that bit and uses 8 words, 256 bits.
     */
    uint64_t u = bits(x);
    int E = (int)((u >> 52) & 0x7ff) - 1075;
    uint64_t M = (u & 0x000fffffffffffffull) | (1ull << 52);
    int s = (E - 2) / 32;
    if (s < 0)
        s = 0;
    uint32_t w[8];
    for (int i = 0; i < 8; i++)
        w[i] = s + i < 48 ? g_two_over_pi[s + i] : 0;
    /* p = M * (w0 w1 ... w7), most significant word first in w, little endian in p. */
    uint32_t p[11] = {0};
    uint32_t m0 = (uint32_t)M, m1 = (uint32_t)(M >> 32);
    for (int i = 0; i < 8; i++) {
        uint64_t wi = w[7 - i];
        uint64_t carry = 0;
        uint64_t t0 = wi * m0 + p[i] + carry;
        p[i] = (uint32_t)t0;
        carry = t0 >> 32;
        uint64_t t1 = wi * m1 + p[i + 1] + carry;
        p[i + 1] = (uint32_t)t1;
        carry = t1 >> 32;
        for (int j = i + 2; carry && j < 11; j++) {
            uint64_t t2 = (uint64_t)p[j] + carry;
            p[j] = (uint32_t)t2;
            carry = t2 >> 32;
        }
    }
    /* the value is p 2^(E - 32 (s + 8)); the binary point sits at bit b of p. */
    int b = 32 * (s + 8) - E;
    /* reads 64 bits of p starting at bit position pos. */
#define BITS64(pos)                                                                                               \
    ({                                                                                                            \
        int pos_ = (pos);                                                                                         \
        uint64_t v_ = 0;                                                                                          \
        for (int i_ = 63; i_ >= 0; i_--) {                                                                        \
            int bit_ = pos_ + i_;                                                                                 \
            v_ = (v_ << 1) | (bit_ >= 0 && bit_ < 352 ? (p[bit_ >> 5] >> (bit_ & 31)) & 1 : 0);                   \
        }                                                                                                         \
        v_;                                                                                                       \
    })
    int q = (int)(BITS64(b) & 3);
    uint64_t f_hi = BITS64(b - 64);
    uint64_t f_lo = BITS64(b - 128);
#undef BITS64
    if (f_hi >> 63) {
        /* fraction >= 1/2: round the quadrant up and use the negative remainder. */
        q = (q + 1) & 3;
        f_lo = ~f_lo + 1;
        f_hi = ~f_hi + (f_lo == 0);
        int64_t a = (int64_t)f_hi;
        double ah = (double)a;
        int64_t arem = a - (int64_t)ah;
        DD v = two_sum(ah * 0x1p-64, (double)arem * 0x1p-64);
        v.lo += (double)f_lo * 0x1p-128;
        v = fast_two_sum(v.hi, v.lo);
        v = dd(-v.hi, -v.lo);
        *r = dd_mul(v, dd(PI_2_HI, PI_2_LO));
        return q;
    }
    double ah = (double)f_hi;
    int64_t arem = (int64_t)(f_hi - (uint64_t)ah);
    DD v = two_sum(ah * 0x1p-64, (double)arem * 0x1p-64);
    v.lo += (double)f_lo * 0x1p-128;
    v = fast_two_sum(v.hi, v.lo);
    *r = dd_mul(v, dd(PI_2_HI, PI_2_LO));
    return q;
}

/* sin(h + l) for |h| <= pi/4 as hi + lo; terms past h^17 are below 2^-60 of h. */
static DD sin_kernel(DD x)
{
    double h = x.hi, l = x.lo;
    /* the h^3/6 term in double-double, the rest in double. */
    DD z = two_prod(h, h);
    double zh = z.hi;
    DD h3 = two_prod(h, zh);
    h3.lo += h * z.lo;
    DD t1 = dd_mul(h3, dd(-0x1.5555555555555p-3, -0x1.5555555555555p-57));
    double poly =
        1.0 / 120 +
        zh * (-1.0 / 5040 +
              zh * (1.0 / 362880 +
                    zh * (-1.0 / 39916800 +
                          zh * (1.0 / 6227020800.0 + zh * (-1.0 / 1307674368000.0 + zh * (1.0 / 355687428096000.0))))));
    double rest = h * zh * zh * poly + l * (1 - 0.5 * zh);
    DD s = two_sum(h, t1.hi);
    s.lo += t1.lo + rest;
    return fast_two_sum(s.hi, s.lo);
}

/* cos(h + l) for |h| <= pi/4 as hi + lo. */
static DD cos_kernel(DD x)
{
    double h = x.hi, l = x.lo;
    DD z = two_prod(h, h);
    double hz = 0.5 * z.hi;
    double w = 1 - hz;
    double e = (1 - w) - hz;
    double zh = z.hi;
    double poly =
        1.0 / 24 +
        zh * (-1.0 / 720 +
              zh * (1.0 / 40320 +
                    zh * (-1.0 / 3628800 +
                          zh * (1.0 / 479001600 +
                                zh * (-1.0 / 87178291200.0 +
                                      zh * (1.0 / 20922789888000.0 + zh * (-1.0 / 6402373705728000.0)))))));
    double tail = e - 0.5 * z.lo - h * l + zh * zh * poly;
    return fast_two_sum(w, tail);
}

double __cdecl sin(double x)
{
    if (is_nan(x))
        return x;
    if (is_inf(x))
        return MsvcrtMathError(_DOMAIN, "sin", x, 0, NAN_IND);
    double ax = fabs_(x);
    if (ax < 0x1p-27)
        return x;
    DD r;
    int q = reduce(ax, &r);
    DD v = (q & 1) ? cos_kernel(r) : sin_kernel(r);
    double res = v.hi + v.lo;
    if (q & 2)
        res = -res;
    return x < 0 ? -res : res;
}

double __cdecl cos(double x)
{
    if (is_nan(x))
        return x;
    if (is_inf(x))
        return MsvcrtMathError(_DOMAIN, "cos", x, 0, NAN_IND);
    double ax = fabs_(x);
    if (ax < 0x1p-28)
        return 1;
    DD r;
    int q = reduce(ax, &r);
    DD v = (q & 1) ? sin_kernel(r) : cos_kernel(r);
    double res = v.hi + v.lo;
    if ((q + 1) & 2)
        res = -res;
    return res;
}

double __cdecl tan(double x)
{
    if (is_nan(x))
        return x;
    if (is_inf(x))
        return MsvcrtMathError(_DOMAIN, "tan", x, 0, NAN_IND);
    double ax = fabs_(x);
    if (ax < 0x1p-27)
        return x;
    DD r;
    int q = reduce(ax, &r);
    DD s = sin_kernel(r), c = cos_kernel(r);
    DD t = (q & 1) ? dd_div(c, s) : dd_div(s, c);
    double res = t.hi + t.lo;
    if (q & 1)
        res = -res;
    return x < 0 ? -res : res;
}

/* ------------------------------------------------------------------ inverse trigonometric functions */

/* atan(x) for x >= 0 given as hi + lo, as hi + lo. */
static DD atan_dd(DD x)
{
    int big = x.hi > 1;
    if (big) {
        if (x.hi > 0x1p60)
            return dd(PI_2_HI, PI_2_LO);
        x = dd_div(dd(1, 0), x);
    }
    int j = (int)round_to_int(x.hi * 16);
    double c = j / 16.0;
    DD t;
    if (j) {
        /* t = (x - c)/(1 + x c); x - c is exact by sterbenz' lemma. */
        DD num = fast_two_sum(x.hi - c, x.lo);
        DD xc = two_prod(x.hi, c);
        xc.lo += x.lo * c;
        DD den = two_sum(1, xc.hi);
        den.lo += xc.lo;
        den = fast_two_sum(den.hi, den.lo);
        t = dd_div(num, den);
    } else {
        t = x;
    }
    /* atan t = t - t^3/3 + t^5/5 - ...: |t| <= 1/32, terms past t^15 are below 2^-75. */
    double th = t.hi;
    double z = th * th;
    double tail =
        th * z *
        (-1.0 / 3 +
         z * (1.0 / 5 + z * (-1.0 / 7 + z * (1.0 / 9 + z * (-1.0 / 11 + z * (1.0 / 13 + z * (-1.0 / 15)))))));
    DD a = fast_two_sum(th, tail + t.lo * (1 - z));
    DD res = dd_add(dd(g_atan_table[j][0], g_atan_table[j][1]), a);
    if (big)
        res = dd_add(dd(PI_2_HI, PI_2_LO), dd(-res.hi, -res.lo));
    return res;
}

double __cdecl atan(double x)
{
    if (is_nan(x))
        return x;
    double ax = fabs_(x);
    if (ax < 0x1p-27)
        return x;
    DD r = atan_dd(dd(ax, 0));
    double res = r.hi + r.lo;
    return x < 0 ? -res : res;
}

/* the angle of (x, y) for x and y finite and not both zero, positive y. */
static double atan2_finite(double y, double x)
{
    double ax = fabs_(x);
    int ey, ex;
    frexp(y, &ey);
    frexp(ax, &ex);
    DD a;
    if (ey - ex > 60) {
        a = dd(PI_2_HI, PI_2_LO);
    } else if (ex - ey > 60 && x > 0) {
        /* tiny ratio: atan(y/x) rounds to y/x. */
        return y / x;
    } else {
        /* y/x as a double-double, scaled away from overflow and underflow. */
        int shift = (ey + ex) / 2;
        double ys = scale(y, -shift), xs = scale(ax, -shift);
        double q = ys / xs;
        DD p = two_prod(q, xs);
        double ql = ((ys - p.hi) - p.lo) / xs;
        a = atan_dd(fast_two_sum(q, ql));
    }
    if (x < 0)
        a = dd_add(dd(PI_HI, PI_LO), dd(-a.hi, -a.lo));
    return a.hi + a.lo;
}

double __cdecl atan2(double y, double x)
{
    if (is_nan(x) || is_nan(y))
        return x + y;
    double sy = (int64_t)bits(y) < 0 ? -1 : 1;
    double ay = fabs_(y);
    if (ay == 0) {
        if ((int64_t)bits(x) < 0)
            return sy * PI_HI;
        return y;
    }
    if (x == 0)
        return sy * PI_2_HI;
    if (is_inf(x)) {
        if (is_inf(y))
            return sy * (x > 0 ? 0.785398163397448309616 : 2.35619449019234492885);
        return x > 0 ? sy * 0.0 : sy * PI_HI;
    }
    if (is_inf(y))
        return sy * PI_2_HI;
    return sy * atan2_finite(ay, x);
}

double __cdecl asin(double x)
{
    if (is_nan(x))
        return x;
    double ax = fabs_(x);
    if (ax > 1)
        return MsvcrtMathError(_DOMAIN, "asin", x, 0, NAN_IND);
    if (ax < 0x1p-27)
        return x;
    if (ax == 1)
        return x < 0 ? -PI_2_HI : PI_2_HI;
    /* atan(x / sqrt(1 - x^2)) with 1 - x^2 exact as hi + lo. */
    DD x2 = two_prod(ax, ax);
    DD one_minus = two_sum(1, -x2.hi);
    one_minus.lo -= x2.lo;
    one_minus = fast_two_sum(one_minus.hi, one_minus.lo);
    DD s = dd_sqrt(one_minus);
    DD r = atan_dd(dd_div(dd(ax, 0), s));
    double res = r.hi + r.lo;
    return x < 0 ? -res : res;
}

double __cdecl acos(double x)
{
    if (is_nan(x))
        return x;
    double ax = fabs_(x);
    if (ax > 1)
        return MsvcrtMathError(_DOMAIN, "acos", x, 0, NAN_IND);
    if (x == 1)
        return 0;
    if (x == -1)
        return PI_HI;
    if (ax < 0x1p-57)
        return PI_2_HI;
    /* atan2(sqrt(1 - x^2), x). */
    DD x2 = two_prod(ax, ax);
    DD one_minus = two_sum(1, -x2.hi);
    one_minus.lo -= x2.lo;
    one_minus = fast_two_sum(one_minus.hi, one_minus.lo);
    DD s = dd_sqrt(one_minus);
    DD a = atan_dd(dd_div(s, dd(ax, 0)));
    if (x < 0)
        a = dd_add(dd(PI_HI, PI_LO), dd(-a.hi, -a.lo));
    return a.hi + a.lo;
}

/* ------------------------------------------------------------------ hypotenuse */

double __cdecl _hypot(double x, double y)
{
    if (is_inf(x) || is_inf(y))
        return INF;
    if (is_nan(x) || is_nan(y))
        return x + y;
    double ax = fabs_(x), ay = fabs_(y);
    if (ax < ay) {
        double t = ax;
        ax = ay;
        ay = t;
    }
    if (ay == 0)
        return ax;
    int ex;
    frexp(ax, &ex);
    int ey;
    frexp(ay, &ey);
    if (ex - ey > 60)
        return ax;
    /* scales the larger to about 1, then sqrt(a^2 + b^2) in double-double. */
    double a = scale(ax, -ex), b = scale(ay, -ex);
    DD a2 = two_prod(a, a), b2 = two_prod(b, b);
    DD s = dd_sqrt(dd_add(a2, b2));
    double r = scale_dd(s, ex);
    if (is_inf(r))
        return MsvcrtMathError(_OVERFLOW, "_hypot", x, y, INF);
    return r;
}

typedef struct {
    double x, y;
} _complex;

double __cdecl _cabs(_complex z)
{
    return _hypot(z.x, z.y);
}

/* ------------------------------------------------------------------ single precision forms */

/* rounds a double result to float, reporting a new overflow or underflow. */
static float narrow(double r, const char *name, double arg1, double arg2)
{
    float f = (float)r;
    if (!is_inf(r) && is_inf((double)f))
        MsvcrtMathError(_OVERFLOW, name, arg1, arg2, r);
    else if (r != 0 && f == 0)
        MsvcrtMathError(_UNDERFLOW, name, arg1, arg2, r);
    return f;
}

float __cdecl acosf(float x)
{
    return (float)acos(x);
}

float __cdecl asinf(float x)
{
    return (float)asin(x);
}

float __cdecl atanf(float x)
{
    return (float)atan(x);
}

float __cdecl atan2f(float y, float x)
{
    return (float)atan2(y, x);
}

float __cdecl ceilf(float x)
{
    return (float)ceil(x);
}

float __cdecl floorf(float x)
{
    return (float)floor(x);
}

float __cdecl cosf(float x)
{
    return (float)cos(x);
}

float __cdecl sinf(float x)
{
    return (float)sin(x);
}

float __cdecl tanf(float x)
{
    return (float)tan(x);
}

float __cdecl coshf(float x)
{
    return narrow(cosh(x), "coshf", x, 0);
}

float __cdecl sinhf(float x)
{
    return narrow(sinh(x), "sinhf", x, 0);
}

float __cdecl tanhf(float x)
{
    return (float)tanh(x);
}

float __cdecl expf(float x)
{
    return narrow(exp(x), "expf", x, 0);
}

float __cdecl logf(float x)
{
    return (float)log(x);
}

float __cdecl log10f(float x)
{
    return (float)log10(x);
}

float __cdecl powf(float x, float y)
{
    return narrow(pow(x, y), "powf", x, y);
}

float __cdecl sqrtf(float x)
{
    return (float)sqrt(x);
}

float __cdecl fmodf(float x, float y)
{
    return (float)fmod(x, y);
}

float __cdecl modff(float x, float *ip)
{
    double i;
    float f = (float)modf(x, &i);
    if (ip)
        *ip = (float)i;
    return f;
}

float __cdecl _hypotf(float x, float y)
{
    return narrow(_hypot(x, y), "_hypotf", x, y);
}

float __cdecl _copysignf(float x, float y)
{
    return (float)_copysign(x, y);
}

float __cdecl _chgsignf(float x)
{
    return -x;
}

float __cdecl _logbf(float x)
{
    return (float)_logb(x);
}

float __cdecl _nextafterf(float x, float y)
{
    if (x != x || y != y)
        return x + y;
    if (x == y)
        return x;
    union {
        float f;
        uint32_t u;
    } v = {x};
    if (x == 0)
        v.u = 1 | ((y < 0) << 31);
    else if ((x < y) == (x > 0))
        v.u++;
    else
        v.u--;
    return v.f;
}

int __cdecl _isnanf(float x)
{
    return x != x;
}

int __cdecl _finitef(float x)
{
    union {
        float f;
        uint32_t u;
    } v = {x};
    return ((v.u >> 23) & 0xff) != 0xff;
}

int __cdecl _fpclassf(float x)
{
    return _fpclass(x);
}

float __cdecl _scalbf(float x, long n)
{
    return narrow(_scalb(x, n), "_scalbf", x, n);
}

/* newer crt exports share the existing range reduction and error handling. */
DECLSPEC_EXPORT double __cdecl trunc(double x) { return trunc_(x); }
DECLSPEC_EXPORT float __cdecl truncf(float x) { return (float)trunc_(x); }
DECLSPEC_EXPORT double __cdecl log2(double x)
{
    if (!(x > 0) || is_inf(x)) return log(x);
    DD value = log_kernel(x);
    DD result = dd_mul_d(value, 1.44269504088896340736);
    result.lo += value.hi * 2.0355273740931033e-17;
    return result.hi + result.lo;
}
DECLSPEC_EXPORT int __cdecl ilogb(double x)
{
    if (!x || is_nan(x)) { *_errno() = EDOM; return (-2147483647 - 1); }
    if (is_inf(x)) { *_errno() = EDOM; return 2147483647; }
    int exponent;
    frexp(x, &exponent);
    return exponent - 1;
}
DECLSPEC_EXPORT int __cdecl ilogbf(float x) { return ilogb(x); }
static double log1p_small(double x)
{
    double sum = 1 + x;
    if (sum == 1) return x;
    return log(sum) * (x / (sum - 1));
}
DECLSPEC_EXPORT double __cdecl asinh(double x)
{
    double a = fabs_(x), value;
    if (is_nan(x) || is_inf(x) || a < 0x1p-28) return x;
    if (a > 0x1p28) value = log(a) + (LN2_HI + LN2_LO);
    else value = log1p_small(a + a * a / (1 + sqrt_(1 + a * a)));
    return x < 0 ? -value : value;
}
DECLSPEC_EXPORT double __cdecl acosh(double x)
{
    if (is_nan(x) || x == INF) return x;
    if (x < 1) return MsvcrtMathError(_DOMAIN, "acosh", x, 0, NAN_IND);
    if (x > 0x1p28) return log(x) + (LN2_HI + LN2_LO);
    double d = x - 1;
    return log1p_small(d + sqrt_(d * (x + 1)));
}
DECLSPEC_EXPORT double __cdecl atanh(double x)
{
    double a = fabs_(x);
    if (is_nan(x) || a < 0x1p-28) return x;
    if (a > 1) return MsvcrtMathError(_DOMAIN, "atanh", x, 0, NAN_IND);
    if (a == 1) return MsvcrtMathError(_SING, "atanh", x, 0, x < 0 ? -INF : INF);
    double value = 0.5 * log1p_small(2 * a / (1 - a));
    return x < 0 ? -value : value;
}
DECLSPEC_EXPORT double __cdecl cbrt(double x)
{
    if (!x || is_nan(x) || is_inf(x)) return x;
    int exponent;
    double m = frexp(fabs_(x), &exponent);
    int remainder = exponent % 3;
    if (remainder < 0) remainder += 3;
    m = scale(m, remainder);
    double root = 1;
    for (int i = 0; i < 8; i++) root = (2 * root + m / (root * root)) / 3;
    root = scale(root, (exponent - remainder) / 3);
    return x < 0 ? -root : root;
}
DECLSPEC_EXPORT float __cdecl log2f(float x) { return (float)log2(x); }
DECLSPEC_EXPORT float __cdecl asinhf(float x) { return (float)asinh(x); }
DECLSPEC_EXPORT float __cdecl acoshf(float x) { return (float)acosh(x); }
DECLSPEC_EXPORT float __cdecl atanhf(float x) { return (float)atanh(x); }
DECLSPEC_EXPORT float __cdecl cbrtf(float x) { return (float)cbrt(x); }
