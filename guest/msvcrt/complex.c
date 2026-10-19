/*
 * the complex functions of the universal crt, which take and return the structures of its headers by value. The
 * arithmetic is done in double precision with the identities of each function, with the sign of zero kept where a
 * branch cut makes it matter. The single precision forms widen and narrow, and the long double forms are the double
 * ones, as long double is double here.
 */
#include "msvcrt_private.h"

typedef struct {
    double Val[2];
} DCOMPLEX;

typedef struct {
    float Val[2];
} FCOMPLEX;

double __cdecl sin(double x);
double __cdecl cos(double x);
double __cdecl tan(double x);
double __cdecl sinh(double x);
double __cdecl cosh(double x);
double __cdecl tanh(double x);
double __cdecl exp(double x);
double __cdecl log(double x);
double __cdecl log1p(double x);
double __cdecl sqrt(double x);
double __cdecl atan2(double y, double x);
double __cdecl asinh(double x);
double __cdecl acosh(double x);
double __cdecl copysign(double x, double y);
double __cdecl fabs(double x);
double __cdecl _hypot(double x, double y);

#define PI 3.14159265358979323846
#define LN10 2.30258509299404568402

typedef struct {
    double R, I;
} Z;

static Z mk(double r, double i)
{
    Z z = {r, i};
    return z;
}

static Z mul(Z a, Z b)
{
    return mk(a.R * b.R - a.I * b.I, a.R * b.I + a.I * b.R);
}

static int inf_part(Z z)
{
    return __builtin_isinf(z.R) || __builtin_isinf(z.I);
}

static Z zexp(Z z)
{
    if (z.I == 0)
        return mk(exp(z.R), z.I);
    double e = exp(z.R);
    if (__builtin_isinf(z.R) && z.R < 0)
        return mk(copysign(0.0, cos(z.I)), copysign(0.0, sin(z.I)));
    return mk(e * cos(z.I), e * sin(z.I));
}

static Z zlog(Z z)
{
    return mk(log(_hypot(z.R, z.I)), atan2(z.I, z.R));
}

/* the root with a real part that is not negative. */
static Z zsqrt(Z z)
{
    if (z.R == 0 && z.I == 0)
        return mk(0.0, z.I);
    if (__builtin_isinf(z.I))
        return mk(__builtin_inf(), z.I);
    if (__builtin_isnan(z.R) || __builtin_isnan(z.I))
        return mk(__builtin_nan(""), __builtin_nan(""));
    double scale = 1;
    double big = fabs(z.R) > fabs(z.I) ? fabs(z.R) : fabs(z.I);
    if (big > 1e300) {
        scale = 4;
        z = mk(z.R / 16, z.I / 16);
    }
    double t = sqrt((fabs(z.R) + _hypot(z.R, z.I)) / 2);
    Z r;
    if (z.R >= 0)
        r = mk(t, z.I / (2 * t));
    else
        r = mk(fabs(z.I) / (2 * t), copysign(t, z.I));
    return scale == 1 ? r : mk(r.R * scale, r.I * scale);
}

static Z zsin(Z z)
{
    return mk(sin(z.R) * cosh(z.I), cos(z.R) * sinh(z.I));
}

static Z zcos(Z z)
{
    return mk(cos(z.R) * cosh(z.I), -sin(z.R) * sinh(z.I));
}

/* the sine and cosine of the real part, the tangent of the imaginary part and the square of its hyperbolic secant. */
static Z ztan(Z z)
{
    double sx = sin(z.R), cx = cos(z.R), ty = tanh(z.I);
    double sech = 1 / cosh(z.I);
    double d = cx * cx + sx * sx * ty * ty;
    return mk(sx * cx * sech * sech / d, ty / d);
}

static Z zsinh(Z z)
{
    return mk(sinh(z.R) * cos(z.I), cosh(z.R) * sin(z.I));
}

static Z zcosh(Z z)
{
    return mk(cosh(z.R) * cos(z.I), sinh(z.R) * sin(z.I));
}

/* tanh z is -i tan(i z). */
static Z ztanh(Z z)
{
    Z t = ztan(mk(-z.I, z.R));
    return mk(t.I, -t.R);
}

/* atanh for the part of the plane that the sign symmetries of the function leave. */
static Z zatanh(Z z)
{
    double a = z.R, b = z.I;
    double d = (1 - a) * (1 - a) + b * b;
    return mk(0.25 * log1p(4 * a / d), 0.5 * atan2(2 * b, (1 - a) * (1 + a) - b * b));
}

static Z zasinh(Z z)
{
    double a = fabs(z.R), b = fabs(z.I);
    Z w = zsqrt(mk(a * a - b * b + 1, 2 * a * b));
    Z r = zlog(mk(a + w.R, b + w.I));
    return mk(copysign(r.R, z.R), copysign(r.I, z.I));
}

static Z zacosh(Z z)
{
    Z p = zsqrt(mk((z.R + 1) / 2, z.I / 2));
    Z m = zsqrt(mk((z.R - 1) / 2, z.I / 2));
    Z r = zlog(mk(p.R + m.R, p.I + m.I));
    return mk(2 * r.R, 2 * r.I);
}

static Z zasin(Z z)
{
    Z t = zasinh(mk(-z.I, z.R));
    return mk(t.I, -t.R);
}

static Z zacos(Z z)
{
    Z s = zasin(z);
    return mk(PI / 2 - s.R, -s.I);
}

static Z zatan(Z z)
{
    Z t = zatanh(mk(-z.I, z.R));
    return mk(t.I, -t.R);
}

static Z zpow(Z x, Z y)
{
    if (x.R == 0 && x.I == 0) {
        if (y.R == 0 && y.I == 0)
            return mk(1, 0);
        if (y.R > 0)
            return mk(0, 0);
        return mk(__builtin_inf(), 0);
    }
    return zexp(mul(y, zlog(x)));
}

static Z zproj(Z z)
{
    return inf_part(z) ? mk(__builtin_inf(), copysign(0.0, z.I)) : z;
}

static Z in(DCOMPLEX z)
{
    return mk(z.Val[0], z.Val[1]);
}

static DCOMPLEX out(Z z)
{
    DCOMPLEX d = {{z.R, z.I}};
    return d;
}

static Z fin(FCOMPLEX z)
{
    return mk(z.Val[0], z.Val[1]);
}

static FCOMPLEX fout(Z z)
{
    FCOMPLEX f = {{(float)z.R, (float)z.I}};
    return f;
}

#define UNARY(name, fn)                                                                                              \
    DECLSPEC_EXPORT DCOMPLEX __cdecl name(DCOMPLEX z) { return out(fn(in(z))); }                                      \
    DECLSPEC_EXPORT FCOMPLEX __cdecl name##f(FCOMPLEX z) { return fout(fn(fin(z))); }

UNARY(cexp, zexp)
UNARY(clog, zlog)
UNARY(csqrt, zsqrt)
UNARY(csin, zsin)
UNARY(ccos, zcos)
UNARY(ctan, ztan)
UNARY(csinh, zsinh)
UNARY(ccosh, zcosh)
UNARY(ctanh, ztanh)
UNARY(casin, zasin)
UNARY(cacos, zacos)
UNARY(catan, zatan)
UNARY(casinh, zasinh)
UNARY(cacosh, zacosh)
UNARY(catanh, zatanh)
UNARY(cproj, zproj)

static Z zlog10(Z z)
{
    Z l = zlog(z);
    return mk(l.R / LN10, l.I / LN10);
}

static Z zconj(Z z)
{
    return mk(z.R, -z.I);
}

UNARY(clog10, zlog10)
UNARY(conj, zconj)

DECLSPEC_EXPORT DCOMPLEX __cdecl cpow(DCOMPLEX x, DCOMPLEX y) { return out(zpow(in(x), in(y))); }
DECLSPEC_EXPORT FCOMPLEX __cdecl cpowf(FCOMPLEX x, FCOMPLEX y) { return fout(zpow(fin(x), fin(y))); }

DECLSPEC_EXPORT double __cdecl cabs(DCOMPLEX z) { return _hypot(z.Val[0], z.Val[1]); }
DECLSPEC_EXPORT float __cdecl cabsf(FCOMPLEX z) { return (float)_hypot(z.Val[0], z.Val[1]); }
DECLSPEC_EXPORT double __cdecl carg(DCOMPLEX z) { return atan2(z.Val[1], z.Val[0]); }
DECLSPEC_EXPORT float __cdecl cargf(FCOMPLEX z) { return (float)atan2(z.Val[1], z.Val[0]); }
DECLSPEC_EXPORT double __cdecl creal(DCOMPLEX z) { return z.Val[0]; }
DECLSPEC_EXPORT float __cdecl crealf(FCOMPLEX z) { return z.Val[0]; }
DECLSPEC_EXPORT double __cdecl cimag(DCOMPLEX z) { return z.Val[1]; }
DECLSPEC_EXPORT float __cdecl cimagf(FCOMPLEX z) { return z.Val[1]; }
DECLSPEC_EXPORT double __cdecl norm(DCOMPLEX z) { return z.Val[0] * z.Val[0] + z.Val[1] * z.Val[1]; }
DECLSPEC_EXPORT float __cdecl normf(FCOMPLEX z) { return z.Val[0] * z.Val[0] + z.Val[1] * z.Val[1]; }

/* the helpers the compiler calls to build and multiply complex numbers. */
DECLSPEC_EXPORT DCOMPLEX __cdecl _Cbuild(double r, double i)
{
    DCOMPLEX z = {{r, i}};
    return z;
}

DECLSPEC_EXPORT FCOMPLEX __cdecl _FCbuild(float r, float i)
{
    FCOMPLEX z = {{r, i}};
    return z;
}

DECLSPEC_EXPORT DCOMPLEX __cdecl _Cmulcc(DCOMPLEX a, DCOMPLEX b) { return out(mul(in(a), in(b))); }
DECLSPEC_EXPORT FCOMPLEX __cdecl _FCmulcc(FCOMPLEX a, FCOMPLEX b) { return fout(mul(fin(a), fin(b))); }

DECLSPEC_EXPORT DCOMPLEX __cdecl _Cmulcr(DCOMPLEX a, double b)
{
    DCOMPLEX z = {{a.Val[0] * b, a.Val[1] * b}};
    return z;
}

DECLSPEC_EXPORT FCOMPLEX __cdecl _FCmulcr(FCOMPLEX a, float b)
{
    FCOMPLEX z = {{a.Val[0] * b, a.Val[1] * b}};
    return z;
}
