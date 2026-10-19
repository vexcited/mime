/*
 * the bessel functions of the first and second kind, _j0 _j1 _jn _y0 _y1 _yn. Included at the end of mathlib.c for
 * the double-double helpers it keeps.
 *
 * Up to an argument of 25 the power series are summed in double-double, where the large terms that cancel are
 * harmless. Past that the asymptotic series of hankel give both kinds from one amplitude and one phase. The
 * orders above one come from the three term recurrence: forward where it is stable, which is always for the second
 * kind and for the first kind while the order is below the argument, and the backward recurrence from a high order
 * normalised by the sum of the even orders where the order is above the argument of the first kind.
 */

#define BESSEL_SERIES_LIMIT 25.0

static const DD BESSEL_TWO_OVER_PI = {0.6366197723675814, -3.935735335036497e-17};
static const DD BESSEL_ONE_OVER_PI = {0.3183098861837907, -1.9678676675182486e-17};
static const DD BESSEL_EULER = {0.5772156649015329, -4.942915152430645e-18};
static const DD BESSEL_PI_4 = {0.7853981633974483, 3.061616997868383e-17};
static const DD BESSEL_PI_34 = {2.356194490192345, 9.184850993605148e-17};

/*
 * the sum of the first kind and the sum with the harmonic weights the second kind needs, for the order 0 or 1 or
 * above. The weights are h(k) for the order 0 and h(k) + h(k + 1) for the order 1, with h the harmonic numbers.
 */
static void bessel_series(double x, int order, DD *j, DD *weighted)
{
    DD half = dd(x / 2, 0);
    DD q = dd_mul(half, half);
    DD term = dd(1, 0);
    for (int i = 1; i <= order; i++)
        term = dd_div(dd_mul(term, half), dd(i, 0));
    DD sum = term, wsum = dd(0, 0);
    DD h_k = dd(0, 0);
    DD h_next = dd(1, 0);
    if (order == 1)
        wsum = dd_add(h_k, h_next), wsum = dd_mul(wsum, term);
    for (int k = 1; k < 400; k++) {
        term = dd_div(dd_mul(term, q), dd((double)k * (k + order), 0));
        term.hi = -term.hi;
        term.lo = -term.lo;
        sum = dd_add(sum, term);
        h_k = h_next;
        h_next = dd_add(h_next, dd_div(dd(1, 0), dd(k + 1, 0)));
        DD weight = order == 1 ? dd_add(h_k, h_next) : h_k;
        wsum = dd_add(wsum, dd_mul(weight, term));
        if (k > x && fabs_(term.hi) < 1e-31 * (1 + fabs_(sum.hi)))
            break;
    }
    *j = sum;
    *weighted = wsum;
}

/* the amplitude series of hankel at x for the order 0 or 1, as the cosine and sine coefficients. */
static void bessel_hankel(double x, int order, double *p, double *q)
{
    double mu = 4.0 * order * order;
    double term = 1, sum_p = 1, sum_q = 0;
    double last = 1e300;
    for (int k = 1; k < 200; k++) {
        double factor = (mu - (2.0 * k - 1) * (2.0 * k - 1)) / (8.0 * k * x);
        term *= factor;
        if (fabs_(term) > last)
            break;
        last = fabs_(term);
        int sign = ((k / 2) & 1) ? -1 : 1;
        if (k & 1)
            sum_q += sign * term;
        else
            sum_p += sign * term;
        if (last < 1e-18)
            break;
    }
    *p = sum_p;
    *q = sum_q;
}

/* the first kind and the second kind from the asymptotic series, for x above the limit of the series. */
static void bessel_large(double x, int order, double *j, double *y)
{
    double p, q;
    bessel_hankel(x, order, &p, &q);
    DD phase = two_sum(x, -(order ? BESSEL_PI_34.hi : BESSEL_PI_4.hi));
    phase.lo -= order ? BESSEL_PI_34.lo : BESSEL_PI_4.lo;
    double s = sin(phase.hi), c = cos(phase.hi);
    double cc = c - phase.lo * s, ss = s + phase.lo * c;
    double amplitude = sqrt_(0.6366197723675814 / x);
    *j = amplitude * (p * cc - q * ss);
    *y = amplitude * (p * ss + q * cc);
}

static double bessel_j_small(double x, int order)
{
    DD j, w;
    bessel_series(x, order, &j, &w);
    return j.hi + j.lo;
}

static double bessel_y_small(double x, int order)
{
    DD j, w;
    bessel_series(x, order, &j, &w);
    DD logx = log_kernel(x / 2);
    DD factor = dd_mul(dd_add(logx, BESSEL_EULER), j);
    if (order == 0) {
        DD r = dd_mul(BESSEL_TWO_OVER_PI, dd_add(factor, dd(-w.hi, -w.lo)));
        return r.hi + r.lo;
    }
    DD r = dd_mul(BESSEL_TWO_OVER_PI, factor);
    DD tail = dd_mul(BESSEL_ONE_OVER_PI, w);
    DD inv = dd_div(dd(-2, 0), dd_mul(dd(3.141592653589793, 1.2246467991473532e-16), dd(x, 0)));
    r = dd_add(r, dd(-tail.hi, -tail.lo));
    r = dd_add(r, inv);
    return r.hi + r.lo;
}

double __cdecl _j0(double x)
{
    if (is_nan(x))
        return x;
    x = fabs_(x);
    if (is_inf(x))
        return 0;
    if (x <= BESSEL_SERIES_LIMIT)
        return bessel_j_small(x, 0);
    double j, y;
    bessel_large(x, 0, &j, &y);
    return j;
}

double __cdecl _j1(double x)
{
    if (is_nan(x))
        return x;
    double a = fabs_(x);
    if (is_inf(a))
        return 0;
    double r;
    if (a <= BESSEL_SERIES_LIMIT) {
        r = bessel_j_small(a, 1);
    } else {
        double j, y;
        bessel_large(a, 1, &j, &y);
        r = j;
    }
    return x < 0 ? -r : r;
}

double __cdecl _y0(double x)
{
    if (is_nan(x))
        return x;
    if (x < 0)
        return MsvcrtMathError(_DOMAIN, "_y0", x, 0, NAN_IND);
    if (x == 0)
        return MsvcrtMathError(_SING, "_y0", x, 0, -INF);
    if (is_inf(x))
        return 0;
    if (x <= BESSEL_SERIES_LIMIT)
        return bessel_y_small(x, 0);
    double j, y;
    bessel_large(x, 0, &j, &y);
    return y;
}

double __cdecl _y1(double x)
{
    if (is_nan(x))
        return x;
    if (x < 0)
        return MsvcrtMathError(_DOMAIN, "_y1", x, 0, NAN_IND);
    if (x == 0)
        return MsvcrtMathError(_SING, "_y1", x, 0, -INF);
    if (is_inf(x))
        return 0;
    if (x <= BESSEL_SERIES_LIMIT)
        return bessel_y_small(x, 1);
    double j, y;
    bessel_large(x, 1, &j, &y);
    return y;
}

/* the first kind of order n above one for a positive argument. */
static double bessel_jn_positive(int n, double x)
{
    if (x <= BESSEL_SERIES_LIMIT) {
        DD j, w;
        bessel_series(x, n, &j, &w);
        return j.hi + j.lo;
    }
    if (n < x) {
        double previous = _j0(x), current = _j1(x);
        for (int k = 1; k < n; k++) {
            double next = 2.0 * k / x * current - previous;
            previous = current;
            current = next;
        }
        return current;
    }
    /* backward from an even order well above n, in a scale that does not overflow. */
    int start = 2 * ((n + (int)(2.0 * sqrt_(40.0 * n)) + 20) / 2);
    double upper = 0, lower = 1e-300, wanted = 0, total = 0;
    for (int k = start; k > 0; k--) {
        double below = 2.0 * k / x * lower - upper;
        upper = lower;
        lower = below;
        if (fabs_(lower) > 1e250) {
            lower *= 1e-250;
            upper *= 1e-250;
            wanted *= 1e-250;
            total *= 1e-250;
        }
        if (k - 1 == n)
            wanted = lower;
        if (!((k - 1) & 1) && k - 1 > 0)
            total += 2 * lower;
    }
    total += lower;
    return wanted / total;
}

double __cdecl _jn(int n, double x)
{
    if (is_nan(x))
        return x;
    int sign = 1;
    if (n < 0) {
        n = -n;
        if (n & 1)
            sign = -sign;
    }
    if (x < 0) {
        x = -x;
        if (n & 1)
            sign = -sign;
    }
    if (is_inf(x))
        return 0;
    double r = n == 0 ? _j0(x) : n == 1 ? _j1(x) : x == 0 ? 0 : bessel_jn_positive(n, x);
    return sign < 0 ? -r : r;
}

double __cdecl _yn(int n, double x)
{
    if (is_nan(x))
        return x;
    if (x < 0)
        return MsvcrtMathError(_DOMAIN, "_yn", x, n, NAN_IND);
    int sign = 1;
    if (n < 0) {
        n = -n;
        if (n & 1)
            sign = -1;
    }
    if (x == 0)
        return MsvcrtMathError(_SING, "_yn", x, n, sign < 0 ? INF : -INF);
    if (is_inf(x))
        return 0;
    double previous = _y0(x);
    double r = previous;
    if (n >= 1) {
        double current = _y1(x);
        for (int k = 1; k < n; k++) {
            double next = 2.0 * k / x * current - previous;
            previous = current;
            current = next;
            if (is_inf(current))
                break;
        }
        r = current;
    }
    return sign < 0 ? -r : r;
}
