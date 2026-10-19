/*
 * floating point conversions: exact decimal digits of a double for
 * printf and _ecvt, msvcrt's digit string formatting, and correctly
 * rounded strtod. both directions use a small fixed size big integer.
 */
#include "msvcrt_private.h"

#define BN_LIMBS 200

typedef struct {
    int n;
    uint32_t d[BN_LIMBS];
} BIGNUM;

static void bn_set(BIGNUM *b, uint64_t v)
{
    b->n = 0;
    while (v) {
        b->d[b->n++] = (uint32_t)v;
        v >>= 32;
    }
}

static void bn_mul_small(BIGNUM *b, uint32_t m)
{
    uint64_t carry = 0;
    for (int i = 0; i < b->n; i++) {
        uint64_t t = (uint64_t)b->d[i] * m + carry;
        b->d[i] = (uint32_t)t;
        carry = t >> 32;
    }
    if (carry && b->n < BN_LIMBS)
        b->d[b->n++] = (uint32_t)carry;
}

static void bn_add_small(BIGNUM *b, uint32_t a)
{
    uint64_t carry = a;
    for (int i = 0; i < b->n && carry; i++) {
        uint64_t t = (uint64_t)b->d[i] + carry;
        b->d[i] = (uint32_t)t;
        carry = t >> 32;
    }
    if (carry && b->n < BN_LIMBS)
        b->d[b->n++] = (uint32_t)carry;
}

static void bn_mul_pow5(BIGNUM *b, int k)
{
    while (k >= 13) {
        bn_mul_small(b, 1220703125u);
        k -= 13;
    }
    static const uint32_t p5[13] = {1, 5, 25, 125, 625, 3125, 15625, 78125, 390625, 1953125, 9765625, 48828125, 244140625};
    if (k)
        bn_mul_small(b, p5[k]);
}

static void bn_shl(BIGNUM *b, int bits)
{
    if (!b->n || !bits)
        return;
    int words = bits / 32, shift = bits % 32;
    int n = b->n + words + 1;
    if (n > BN_LIMBS)
        n = BN_LIMBS;
    for (int i = n - 1; i >= 0; i--) {
        int src = i - words;
        uint32_t hi = src >= 0 && src < b->n ? b->d[src] : 0;
        uint32_t lo = src - 1 >= 0 && src - 1 < b->n ? b->d[src - 1] : 0;
        b->d[i] = shift ? (hi << shift) | (lo >> (32 - shift)) : hi;
    }
    b->n = n;
    while (b->n && !b->d[b->n - 1])
        b->n--;
}

static void bn_shr1(BIGNUM *b)
{
    for (int i = 0; i < b->n; i++)
        b->d[i] = (b->d[i] >> 1) | (i + 1 < b->n ? b->d[i + 1] << 31 : 0);
    while (b->n && !b->d[b->n - 1])
        b->n--;
}

static int bn_cmp(const BIGNUM *a, const BIGNUM *b)
{
    if (a->n != b->n)
        return a->n < b->n ? -1 : 1;
    for (int i = a->n - 1; i >= 0; i--)
        if (a->d[i] != b->d[i])
            return a->d[i] < b->d[i] ? -1 : 1;
    return 0;
}

/* a -= b, with a >= b. */
static void bn_sub(BIGNUM *a, const BIGNUM *b)
{
    int64_t borrow = 0;
    for (int i = 0; i < a->n; i++) {
        int64_t t = (int64_t)a->d[i] - (i < b->n ? b->d[i] : 0) - borrow;
        borrow = t < 0;
        a->d[i] = (uint32_t)(t + (borrow << 32));
    }
    while (a->n && !a->d[a->n - 1])
        a->n--;
}

static uint32_t bn_divmod_small(BIGNUM *b, uint32_t d)
{
    uint64_t rem = 0;
    for (int i = b->n - 1; i >= 0; i--) {
        uint64_t t = (rem << 32) | b->d[i];
        b->d[i] = (uint32_t)(t / d);
        rem = t % d;
    }
    while (b->n && !b->d[b->n - 1])
        b->n--;
    return (uint32_t)rem;
}

static int bn_bitlen(const BIGNUM *b)
{
    if (!b->n)
        return 0;
    return (b->n - 1) * 32 + (32 - __builtin_clz(b->d[b->n - 1]));
}

static int bn_bit(const BIGNUM *b, int i)
{
    int w = i / 32;
    return w < b->n ? (b->d[w] >> (i % 32)) & 1 : 0;
}

/* ------------------------------------------------------------------ double to decimal */

/* all decimal digits of a big integer, most significant first; returns the count. */
static int bn_to_digits(BIGNUM *b, char *out, int max)
{
    char tmp[1200];
    int n = 0;
    while (b->n && n + 9 <= (int)sizeof(tmp)) {
        uint32_t chunk = bn_divmod_small(b, 1000000000u);
        for (int i = 0; i < 9; i++) {
            tmp[n++] = (char)('0' + chunk % 10);
            chunk /= 10;
        }
    }
    while (n > 1 && tmp[n - 1] == '0')
        n--;
    int count = 0;
    for (int i = n - 1; i >= 0 && count < max; i--)
        out[count++] = tmp[i];
    return n;
}

/*
 * the value's digits rounded half up to max_digits significant digits,
 * trailing zeros removed, like msvcrt's $I10_OUTPUT. zero gives the
 * single digit 0 with decpt 0; infinities and nans give msvcrt's
 * "1#INF" family with decpt 1.
 */
void MsvcrtDoubleToDecimal(double value, int max_digits, MSVCRT_DECIMAL *out)
{
    uint64_t bits;
    memcpy(&bits, &value, 8);
    out->sign = (int)(bits >> 63);
    int exp = (int)((bits >> 52) & 0x7ff);
    uint64_t frac = bits & 0xfffffffffffffull;
    if (exp == 0x7ff) {
        const char *s;
        if (!frac)
            s = "1#INF";
        else if (!(frac & 0x8000000000000ull))
            s = "1#SNAN";
        else if (out->sign && frac == 0x8000000000000ull)
            s = "1#IND";
        else
            s = "1#QNAN";
        out->ndigits = (int)strlen(s);
        memcpy(out->digits, s, out->ndigits + 1);
        out->decpt = 1;
        return;
    }
    if (!exp && !frac) {
        out->digits[0] = '0';
        out->digits[1] = 0;
        out->ndigits = 1;
        out->decpt = 0;
        return;
    }
    uint64_t m = exp ? frac | (1ull << 52) : frac;
    int e = exp ? exp - 1075 : -1074;
    BIGNUM b;
    bn_set(&b, m);
    int scale = 0;
    if (e >= 0)
        bn_shl(&b, e);
    else {
        bn_mul_pow5(&b, -e);
        scale = -e;
    }
    if (max_digits > (int)sizeof(out->digits) - 2)
        max_digits = (int)sizeof(out->digits) - 2;
    char digits[sizeof(out->digits)];
    int total = bn_to_digits(&b, digits, max_digits + 1);
    int decpt = total - scale;
    int n = total < max_digits + 1 ? total : max_digits + 1;
    if (n > max_digits) {
        int round = digits[max_digits] >= '5';
        n = max_digits;
        if (round) {
            int i = n - 1;
            while (i >= 0 && digits[i] == '9')
                digits[i--] = '0';
            if (i < 0) {
                digits[0] = '1';
                n = 1;
                decpt++;
            } else {
                digits[i]++;
            }
        }
    }
    while (n > 1 && digits[n - 1] == '0')
        n--;
    memcpy(out->digits, digits, n);
    out->digits[n] = 0;
    out->ndigits = n;
    out->decpt = decpt;
}

/*
 * msvcrt's _fptostr: copies count digits behind a reserved leading zero,
 * padding with zeros, rounds half up on the next digit and adjusts decpt
 * when the rounding carries out.
 */
static void fptostr(char *buf, int count, MSVCRT_DECIMAL *d)
{
    const char *m = d->digits;
    char *p = buf;
    *p++ = '0';
    int n = count;
    while (n > 0) {
        *p++ = *m ? *m++ : '0';
        n--;
    }
    *p = 0;
    if (n >= 0 && *m >= '5') {
        while (*--p == '9')
            *p = '0';
        ++*p;
    }
    if (*buf == '1')
        d->decpt++;
    else
        memmove(buf, buf + 1, strlen(buf + 1) + 1);
}

/* inserts c at p, moving the rest of the string. */
static void insert_char(char *p, char c)
{
    memmove(p + 1, p, strlen(p) + 1);
    *p = c;
}

/* msvcrt's _cftoe2: d.ddde+xxx from the digits at buf, the first already shifted when g_fmt is 0. */
static void cftoe2(char *buf, int ndec, int caps, MSVCRT_DECIMAL *d, int g_fmt)
{
    char *p = buf + d->sign;
    if (g_fmt && ndec > 0)
        insert_char(p, ' ');
    if (ndec > 0) {
        p[0] = p[1];
        p[1] = '.';
        p++;
    }
    char *e = p + ndec + (g_fmt == 0);
    strcpy(e, "e+000");
    if (caps)
        *e = 'E';
    if (d->digits[0] != '0') {
        int x = d->decpt - 1;
        if (x < 0) {
            x = -x;
            e[1] = '-';
        }
        if (x >= 100) {
            e[2] += (char)(x / 100);
            x %= 100;
        }
        if (x >= 10) {
            e[3] += (char)(x / 10);
            x %= 10;
        }
        e[4] += (char)x;
    }
    if ((MsvcrtOutputFormat & 1) && e[2] == '0')
        memmove(e + 2, e + 3, 3);
}

/* msvcrt's _cftof2: places the decimal point and the leading zeros. */
static void cftof2(char *buf, int ndec, MSVCRT_DECIMAL *d, int g_fmt)
{
    char *p = buf;
    if (g_fmt && d->decpt - 1 == ndec) {
        char *z = buf + d->sign + d->decpt - 1;
        z[0] = '0';
        z[1] = 0;
    }
    if (d->sign)
        p++;
    if (d->decpt > 0) {
        p += d->decpt;
    } else {
        insert_char(p, '0');
        p++;
    }
    if (ndec > 0) {
        insert_char(p, '.');
        p++;
        if (d->decpt < 0) {
            int zeros = -d->decpt;
            if (!g_fmt && ndec < zeros)
                zeros = ndec;
            memmove(p + zeros, p, strlen(p) + 1);
            memset(p, '0', zeros);
        }
    }
}

static void cropzeros(char *buf)
{
    while (*buf && *buf != '.')
        buf++;
    if (!*buf++)
        return;
    while (*buf && *buf != 'e' && *buf != 'E')
        buf++;
    char *stop = buf--;
    while (*buf == '0')
        buf--;
    if (*buf == '.')
        buf--;
    while ((*++buf = *stop++))
        ;
}

static void forcdecpt(char *buf)
{
    if (MsvcrtToLower((unsigned char)*buf) != 'e') {
        do
            buf++;
        while (MsvcrtIsDigit((unsigned char)*buf));
    }
    if (MsvcrtToLower((unsigned char)*buf) == 'x')
        buf += 2;
    insert_char(buf, '.');
}

/* %a: hexadecimal digits of the mantissa, rounded half up on the next digit. */
static void cftoa(double value, char *buf, int prec, int caps)
{
    uint64_t bits;
    memcpy(&bits, &value, 8);
    int exp = (int)((bits >> 52) & 0x7ff);
    uint64_t frac = bits & 0xfffffffffffffull;
    char *p = buf;
    if (exp == 0x7ff) {
        MSVCRT_DECIMAL d;
        MsvcrtDoubleToDecimal(value, 17, &d);
        char tmp[400];
        tmp[0] = 0;
        fptostr(tmp + d.sign + (prec > 0), prec + 1, &d);
        if (d.sign)
            tmp[0] = '-';
        cftoe2(tmp, prec, 0, &d, 0);
        if (d.sign)
            *p++ = '-';
        *p++ = '0';
        *p++ = caps ? 'X' : 'x';
        strcpy(p, tmp + d.sign);
        char *e = strrchr(p, 'e');
        if (e) {
            *e = caps ? 'P' : 'p';
            e[3] = 0;
        }
        return;
    }
    int bias = 1023;
    if ((int64_t)bits < 0)
        *p++ = '-';
    *p++ = '0';
    *p++ = caps ? 'X' : 'x';
    if (exp) {
        *p++ = '1';
    } else {
        *p++ = '0';
        bias = frac ? 1022 : 0;
    }
    char *point = p;
    *p++ = prec ? '.' : 0;
    char hexa = caps ? 'A' : 'a';
    if (frac) {
        int shift = 48;
        uint64_t mask = 0xf000000000000ull;
        int n = prec;
        for (;;) {
            unsigned digit = (unsigned)((frac & mask) >> shift);
            if (n <= 0) {
                if (digit > 8) {
                    char *q = p - 1;
                    while (*q == 'f' || *q == 'F') {
                        *q = '0';
                        q--;
                    }
                    if (q == point)
                        q[-1]++;
                    else if (*q == '9')
                        *q = hexa;
                    else
                        (*q)++;
                }
                break;
            }
            *p++ = (char)(digit < 10 ? '0' + digit : hexa + digit - 10);
            n--;
            mask >>= 4;
            shift -= 4;
            if (shift < 0) {
                while (n-- > 0)
                    *p++ = '0';
                break;
            }
        }
    } else {
        for (int i = 0; i < prec; i++)
            *p++ = '0';
    }
    if (*point)
        point = p;
    *point = caps ? 'P' : 'p';
    p = point + 1;
    long x = exp - bias;
    *p++ = x < 0 ? '-' : '+';
    if (x < 0)
        x = -x;
    char tmp[8];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + x % 10);
        x /= 10;
    } while (x);
    while (n)
        *p++ = tmp[--n];
    *p = 0;
}

/*
 * formats value for printf conversion fmt (e, f, g or a), with msvcrt's
 * 17 significant digits and three digit exponents. buf holds at least
 * prec + 350 characters.
 */
void MsvcrtFormatDouble(double value, int fmt, int prec, int caps, int alt, char *buf)
{
    MSVCRT_DECIMAL d;
    buf[0] = 0;
    if (fmt == 'a') {
        cftoa(value, buf, prec, caps);
        if (alt && !prec)
            forcdecpt(buf);
        return;
    }
    MsvcrtDoubleToDecimal(value, 17, &d);
    if (d.sign)
        buf[0] = '-';
    if (fmt == 'e') {
        fptostr(buf + d.sign + (prec > 0), prec + 1, &d);
        cftoe2(buf, prec, caps, &d, 0);
    } else if (fmt == 'f') {
        fptostr(buf + d.sign, prec + d.decpt, &d);
        cftof2(buf, prec, &d, 0);
    } else {
        int decpt = d.decpt;
        fptostr(buf + d.sign, prec, &d);
        int x = d.decpt - 1;
        if (x < -4 || x >= prec) {
            cftoe2(buf, prec, caps, &d, 1);
        } else {
            if (decpt <= x) {
                char *q = buf + d.sign;
                while (*q)
                    q++;
                q[-1] = 0;
            }
            cftof2(buf, prec, &d, 1);
        }
        if (!alt)
            cropzeros(buf);
    }
    if (alt && !prec)
        forcdecpt(buf);
}

/* ------------------------------------------------------------------ _ecvt, _fcvt, _gcvt */

static errno_t cvt_digits(char *buf, size_t size, double value, int ndigits, int *decpt, int *sign, int fixed)
{
    if (!buf || !size || !decpt || !sign) {
        MSVCRT_INVALID(EINVAL);
        if (buf && size)
            *buf = 0;
        return EINVAL;
    }
    MSVCRT_DECIMAL d;
    MsvcrtDoubleToDecimal(value, 17, &d);
    int count = fixed ? d.decpt + ndigits : ndigits;
    if (count > 348)
        count = 348;
    if (size <= (size_t)(count > 0 ? count : 0) + 1) {
        *buf = 0;
        MSVCRT_INVALID(ERANGE);
        return ERANGE;
    }
    fptostr(buf, count, &d);
    *decpt = d.decpt;
    *sign = d.sign;
    return 0;
}

char *__cdecl _ecvt(double value, int ndigits, int *decpt, int *sign)
{
    char *buf = MsvcrtThreadData()->cvt_buf;
    if (ndigits > 348)
        ndigits = 348;
    if (ndigits < 0)
        ndigits = 0;
    return cvt_digits(buf, 350, value, ndigits, decpt, sign, 0) ? nullptr : buf;
}

char *__cdecl _fcvt(double value, int ndec, int *decpt, int *sign)
{
    char *buf = MsvcrtThreadData()->cvt_buf;
    return cvt_digits(buf, 350, value, ndec, decpt, sign, 1) ? nullptr : buf;
}

errno_t __cdecl _ecvt_s(char *buf, size_t size, double value, int ndigits, int *decpt, int *sign)
{
    return cvt_digits(buf, size, value, ndigits, decpt, sign, 0);
}

errno_t __cdecl _fcvt_s(char *buf, size_t size, double value, int ndec, int *decpt, int *sign)
{
    return cvt_digits(buf, size, value, ndec, decpt, sign, 1);
}

errno_t __cdecl _gcvt_s(char *buf, size_t size, double value, int ndigits)
{
    if (!buf || !size) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    *buf = 0;
    if ((size_t)ndigits >= size) {
        MSVCRT_INVALID(ERANGE);
        return ERANGE;
    }
    MSVCRT_DECIMAL d;
    MsvcrtDoubleToDecimal(value, 17, &d);
    char tmp[800];
    int x = d.decpt - 1;
    if (x < -1 || x > ndigits - 1)
        MsvcrtFormatDouble(value, 'e', ndigits - 1, 0, 0, tmp);
    else
        MsvcrtFormatDouble(value, 'f', ndigits - d.decpt, 0, 0, tmp);
    if (strlen(tmp) >= size) {
        MSVCRT_INVALID(ERANGE);
        return ERANGE;
    }
    strcpy(buf, tmp);
    /* drops the zeros ending the fraction but keeps the point. */
    char *p = buf;
    while (*p && *p != '.')
        p++;
    if (*p) {
        char *q = p + 1;
        while (*q && *q != 'e')
            q++;
        char *stop = q;
        do
            q--;
        while (*q == '0');
        q++;
        while ((*q++ = *stop++))
            ;
    }
    return 0;
}

char *__cdecl _gcvt(double value, int ndigits, char *buf)
{
    return _gcvt_s(buf, (size_t)ndigits + 32, value, ndigits) ? nullptr : buf;
}

/* ------------------------------------------------------------------ decimal to double */

/*
 * builds the double nearest to mant * 2^e2 (mant with its top bit set),
 * rounding half to even with sticky the bits below mant. reports
 * overflow (1) and underflow to zero (2) in *range.
 */
static double make_double(uint64_t mant, int e2, int sticky, int *range)
{
    /* value = mant * 2^e2, mant in [2^63, 2^64). exponent of the leading bit: */
    int lead = e2 + 63;
    if (lead > 1023) {
        *range = 1;
        return __builtin_inf();
    }
    int shift = 11;
    if (lead < -1022)
        shift += -1022 - lead;
    uint64_t result;
    if (shift >= 64) {
        /* below the smallest subnormal: it or zero, ties going to zero. */
        if (shift > 64 || !((mant << 1) || sticky)) {
            *range = 2;
            return 0;
        }
        result = 1;
        double v;
        memcpy(&v, &result, 8);
        return v;
    }
    uint64_t kept = mant >> shift;
    uint64_t rest = mant & ((1ull << shift) - 1);
    uint64_t half = 1ull << (shift - 1);
    if (rest > half || (rest == half && (sticky || (kept & 1))))
        kept++;
    if (lead < -1022) {
        /* subnormal, possibly rounded up to the smallest normal. */
        if (!kept) {
            *range = 2;
            return 0;
        }
        result = kept;
    } else {
        if (kept >> 53) {
            kept >>= 1;
            lead++;
            if (lead > 1023) {
                *range = 1;
                return __builtin_inf();
            }
        }
        result = ((uint64_t)(lead + 1023) << 52) | (kept & 0xfffffffffffffull);
    }
    double v;
    memcpy(&v, &result, 8);
    return v;
}

static constexpr double g_pow10[] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
                                 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};

#define MAX_SIG_DIGITS 800

/*
 * the decimal digits * 10^exp10 as a 64 bit mantissa with its top bit set, a power of two and a sticky flag for what is
 * below the mantissa; digits may end with a sticky 1.
 */
static void decimal_mantissa(const char *digits, int nd, int exp10, uint64_t *mantissa, int *exponent, int *sticky_out)
{
    BIGNUM num;
    bn_set(&num, 0);
    for (int i = 0; i < nd; i++) {
        bn_mul_small(&num, 10);
        bn_add_small(&num, (uint32_t)(digits[i] - '0'));
    }
    uint64_t mant;
    int e2, sticky = 0;
    if (exp10 >= 0) {
        bn_mul_pow5(&num, exp10);
        int len = bn_bitlen(&num);
        e2 = exp10 + len - 64;
        mant = 0;
        for (int i = 0; i < 64; i++) {
            int bit = len - 1 - i;
            mant = (mant << 1) | (uint64_t)(bit >= 0 ? bn_bit(&num, bit) : 0);
        }
        for (int bit = len - 65; bit >= 0 && !sticky; bit--)
            sticky = bn_bit(&num, bit);
    } else {
        BIGNUM den;
        bn_set(&den, 1);
        bn_mul_pow5(&den, -exp10);
        /* num * 2^s / 5^k with a quotient of 66 bits; the power of two of 10^k goes in e2. */
        int s = 66 + bn_bitlen(&den) - bn_bitlen(&num);
        if (s > 0)
            bn_shl(&num, s);
        else
            bn_shl(&den, -s);
        int qbits = 68;
        bn_shl(&den, qbits);
        uint64_t qhi = 0, qlo = 0;
        for (int i = qbits; i >= 0; i--) {
            int one = bn_cmp(&num, &den) >= 0;
            if (one)
                bn_sub(&num, &den);
            qhi = (qhi << 1) | (qlo >> 63);
            qlo = (qlo << 1) | (uint64_t)one;
            if (i)
                bn_shr1(&den);
        }
        sticky = num.n != 0;
        int len = qhi ? 128 - __builtin_clzll(qhi) : 64 - __builtin_clzll(qlo);
        if (len > 64) {
            int sh = len - 64;
            if (qlo & ((1ull << sh) - 1))
                sticky = 1;
            mant = (qlo >> sh) | (qhi << (64 - sh));
        } else {
            mant = qlo << (64 - len);
        }
        e2 = exp10 - s + len - 64;
    }
    *mantissa = mant;
    *exponent = e2;
    *sticky_out = sticky;
}

/* the double nearest to the decimal digits * 10^exp10; digits may end with a sticky 1. */
static double decimal_to_double(const char *digits, int nd, int exp10, int *range)
{
    *range = 0;
    /* exact when both the digits and the power of ten are exact doubles. */
    if (nd <= 15 && exp10 >= -22 && exp10 <= 22) {
        uint64_t v = 0;
        for (int i = 0; i < nd; i++)
            v = v * 10 + (digits[i] - '0');
        double r = (double)v;
        return exp10 >= 0 ? r * g_pow10[exp10] : r / g_pow10[-exp10];
    }
    if (nd + exp10 > 310) {
        *range = 1;
        return __builtin_inf();
    }
    if (nd + exp10 < -324) {
        *range = 2;
        return 0;
    }
    uint64_t mant;
    int e2, sticky;
    decimal_mantissa(digits, nd, exp10, &mant, &e2, &sticky);
    return make_double(mant, e2, sticky, range);
}

/*
 * the float nearest to mant * 2^e2, rounded once: going through a double would round twice and miss halfway cases.
 * mant has its top bit set; *range is 1 on overflow and 2 on underflow to zero.
 */
static float make_float(uint64_t mant, int e2, int sticky, int *range)
{
    int lead = e2 + 63;
    if (lead > 127) {
        *range = 1;
        return __builtin_inff();
    }
    int shift = 40;
    if (lead < -126)
        shift += -126 - lead;
    uint32_t result;
    if (shift >= 64) {
        if (shift > 64 || !((mant << 1) || sticky)) {
            *range = 2;
            return 0;
        }
        result = 1;
        float v;
        memcpy(&v, &result, 4);
        return v;
    }
    uint64_t kept = mant >> shift;
    uint64_t rest = mant & ((1ull << shift) - 1);
    uint64_t half = 1ull << (shift - 1);
    if (rest > half || (rest == half && (sticky || (kept & 1))))
        kept++;
    if (lead < -126) {
        if (!kept) {
            *range = 2;
            return 0;
        }
        result = (uint32_t)kept;
    } else {
        if (kept >> 24) {
            kept >>= 1;
            lead++;
            if (lead > 127) {
                *range = 1;
                return __builtin_inff();
            }
        }
        result = ((uint32_t)(lead + 127) << 23) | (uint32_t)(kept & 0x7fffff);
    }
    float v;
    memcpy(&v, &result, 4);
    return v;
}

static float decimal_to_float(const char *digits, int nd, int exp10, int *range)
{
    *range = 0;
    if (nd + exp10 > 40) {
        *range = 1;
        return __builtin_inff();
    }
    if (nd + exp10 < -50) {
        *range = 2;
        return 0;
    }
    uint64_t mant;
    int e2, sticky;
    decimal_mantissa(digits, nd, exp10, &mant, &e2, &sticky);
    return make_float(mant, e2, sticky, range);
}

/* the case folded letter of an ascii character, to compare words without the locale. */
#define FOLD(c) ((c) >= 'A' && (c) <= 'Z' ? (c) + 32 : (c))

static int hex_digit_value(unsigned c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/*
 * reads a floating point number as the universal crt does: optional sign, then decimal digits with an optional point
 * and an exponent introduced by e, E, d or D, or 0x and hexadecimal digits with an optional point and a binary
 * exponent introduced by p, or INF, INFINITY, NAN, NAN(IND) and NAN(SNAN) in any case. *range is 1 on overflow and 2
 * when a nonzero number rounds to zero. With single the number is rounded to a float and given as a double.
 */
#define PARSE_FLOAT(NAME, T)                                                                                      \
    static double NAME(const T *s, const T **end, int *range, int single)                                        \
    {                                                                                                             \
        const T *p = s;                                                                                           \
        char digits[MAX_SIG_DIGITS + 2];                                                                          \
        int nd = 0, dexp = 0, any = 0, sticky = 0, neg = 0;                                                       \
        *range = 0;                                                                                               \
        *end = s;                                                                                                 \
        if (*p == '-' || *p == '+')                                                                               \
            neg = *p++ == '-';                                                                                    \
        /* infinity and not a number. */                                                                          \
        if (FOLD(p[0]) == 'i' && FOLD(p[1]) == 'n' && FOLD(p[2]) == 'f') {                                        \
            const T *q = p + 3;                                                                                   \
            if (FOLD(q[0]) == 'i' && FOLD(q[1]) == 'n' && FOLD(q[2]) == 'i' && FOLD(q[3]) == 't' &&               \
                FOLD(q[4]) == 'y')                                                                                \
                q += 5;                                                                                           \
            *end = q;                                                                                             \
            return neg ? -__builtin_inf() : __builtin_inf();                                                      \
        }                                                                                                         \
        if (FOLD(p[0]) == 'n' && FOLD(p[1]) == 'a' && FOLD(p[2]) == 'n') {                                        \
            const T *q = p + 3;                                                                                   \
            uint64_t bits = 0x7ff8000000000000ull;                                                                \
            if (*q == '(') {                                                                                      \
                const T *r = q + 1;                                                                               \
                while ((*r >= '0' && *r <= '9') || (*r >= 'a' && *r <= 'z') || (*r >= 'A' && *r <= 'Z') ||        \
                       *r == '_')                                                                                 \
                    r++;                                                                                          \
                if (*r == ')') {                                                                                  \
                    if (FOLD(q[1]) == 'i' && FOLD(q[2]) == 'n' && FOLD(q[3]) == 'd' && r == q + 4)                \
                        bits = 0xfff8000000000000ull;                                                             \
                    else if (FOLD(q[1]) == 's' && FOLD(q[2]) == 'n' && FOLD(q[3]) == 'a' && FOLD(q[4]) == 'n' &&  \
                             r == q + 5)                                                                          \
                        bits = 0x7ff4000000000000ull;                                                             \
                    q = r + 1;                                                                                    \
                }                                                                                                 \
            }                                                                                                     \
            if (neg)                                                                                              \
                bits |= 0x8000000000000000ull;                                                                    \
            *end = q;                                                                                             \
            double nan;                                                                                           \
            memcpy(&nan, &bits, 8);                                                                               \
            return nan;                                                                                           \
        }                                                                                                         \
        /* hexadecimal digits with a binary exponent. */                                                          \
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X') &&                                                        \
            (hex_digit_value(p[2]) >= 0 || (p[2] == '.' && hex_digit_value(p[3]) >= 0))) {                        \
            const T *q = p + 2;                                                                                   \
            uint64_t mant = 0;                                                                                    \
            int exp2 = 0, hsticky = 0, seen_point = 0;                                                            \
            for (;; q++) {                                                                                        \
                int d = hex_digit_value(*q);                                                                      \
                if (d < 0) {                                                                                      \
                    if (*q == '.' && !seen_point) {                                                               \
                        seen_point = 1;                                                                           \
                        continue;                                                                                 \
                    }                                                                                             \
                    break;                                                                                        \
                }                                                                                                 \
                if (!(mant >> 60)) {                                                                              \
                    mant = mant << 4 | (uint64_t)d;                                                               \
                    if (seen_point)                                                                               \
                        exp2 -= 4;                                                                                \
                } else {                                                                                          \
                    if (d)                                                                                        \
                        hsticky = 1;                                                                              \
                    if (!seen_point)                                                                              \
                        exp2 += 4;                                                                                \
                }                                                                                                 \
            }                                                                                                     \
            if (*q == 'p' || *q == 'P') {                                                                         \
                const T *r = q + 1;                                                                               \
                int eneg = 0, x = 0;                                                                              \
                if (*r == '-' || *r == '+')                                                                       \
                    eneg = *r++ == '-';                                                                           \
                if (*r >= '0' && *r <= '9') {                                                                     \
                    while (*r >= '0' && *r <= '9') {                                                              \
                        if (x < 100000)                                                                           \
                            x = x * 10 + (*r - '0');                                                              \
                        r++;                                                                                      \
                    }                                                                                             \
                    exp2 += eneg ? -x : x;                                                                        \
                    q = r;                                                                                        \
                }                                                                                                 \
            }                                                                                                     \
            *end = q;                                                                                             \
            if (!mant)                                                                                            \
                return neg ? -0.0 : 0.0;                                                                          \
            int lz = __builtin_clzll(mant);                                                                       \
            mant <<= lz;                                                                                          \
            exp2 -= lz;                                                                                           \
            double hv;                                                                                            \
            if (single)                                                                                           \
                hv = make_float(mant, exp2, hsticky, range);                                                      \
            else                                                                                                  \
                hv = make_double(mant, exp2, hsticky, range);                                                     \
            if (*range == 2)                                                                                      \
                return neg ? -0.0 : 0.0;                                                                          \
            return neg ? -hv : hv;                                                                                \
        }                                                                                                         \
        while (*p == '0') {                                                                                       \
            p++;                                                                                                  \
            any = 1;                                                                                              \
        }                                                                                                         \
        while (*p >= '0' && *p <= '9') {                                                                          \
            any = 1;                                                                                              \
            if (nd < MAX_SIG_DIGITS)                                                                              \
                digits[nd++] = (char)*p;                                                                          \
            else if (*p != '0')                                                                                   \
                sticky = 1;                                                                                       \
            dexp++;                                                                                               \
            p++;                                                                                                  \
        }                                                                                                         \
        if (*p == '.') {                                                                                          \
            p++;                                                                                                  \
            if (!nd)                                                                                              \
                while (*p == '0') {                                                                               \
                    p++;                                                                                          \
                    dexp--;                                                                                       \
                    any = 1;                                                                                      \
                }                                                                                                 \
            while (*p >= '0' && *p <= '9') {                                                                      \
                any = 1;                                                                                          \
                if (nd < MAX_SIG_DIGITS)                                                                          \
                    digits[nd++] = (char)*p;                                                                      \
                else if (*p != '0')                                                                               \
                    sticky = 1;                                                                                   \
                p++;                                                                                              \
            }                                                                                                     \
        }                                                                                                         \
        if (!any)                                                                                                 \
            return 0;                                                                                             \
        if (*p == 'e' || *p == 'E' || *p == 'd' || *p == 'D') {                                                   \
            const T *q = p + 1;                                                                                   \
            int eneg = 0;                                                                                         \
            if (*q == '-' || *q == '+')                                                                           \
                eneg = *q++ == '-';                                                                               \
            if (*q >= '0' && *q <= '9') {                                                                         \
                int x = 0;                                                                                        \
                while (*q >= '0' && *q <= '9') {                                                                  \
                    if (x < 100000)                                                                               \
                        x = x * 10 + (*q - '0');                                                                  \
                    q++;                                                                                          \
                }                                                                                                 \
                dexp += eneg ? -x : x;                                                                            \
                p = q;                                                                                            \
            }                                                                                                     \
        }                                                                                                         \
        *end = p;                                                                                                 \
        if (sticky) {                                                                                             \
            digits[nd++] = '1';                                                                                   \
        } else {                                                                                                  \
            while (nd && digits[nd - 1] == '0')                                                                   \
                nd--;                                                                                             \
        }                                                                                                         \
        if (!nd)                                                                                                  \
            return neg ? -0.0 : 0.0;                                                                              \
        double v = single ? (double)decimal_to_float(digits, nd, dexp - nd, range)                                \
                          : decimal_to_double(digits, nd, dexp - nd, range);                                      \
        if (*range == 2)                                                                                          \
            return 0.0;                                                                                           \
        return neg ? -v : v;                                                                                      \
    }

PARSE_FLOAT(parse_float_a, char)
PARSE_FLOAT(parse_float_w, wchar_t)

double MsvcrtStrtod(const char *s, char **end, int *range_error)
{
    const char *e;
    double v = parse_float_a(s, &e, range_error, 0);
    if (end)
        *end = (char *)e;
    return v;
}

double MsvcrtWcstod(const wchar_t *s, wchar_t **end, int *range_error)
{
    const wchar_t *e;
    double v = parse_float_w(s, &e, range_error, 0);
    if (end)
        *end = (wchar_t *)e;
    return v;
}

double __cdecl strtod(const char *s, char **end)
{
    if (end)
        *end = (char *)s;
    if (!s) {
        MSVCRT_INVALID(EINVAL);
        return 0;
    }
    const char *p = s;
    while (MSVCRT_CTYPE(*p) & _SPACE)
        p++;
    int range;
    char *e;
    double v = MsvcrtStrtod(p, &e, &range);
    MsvcrtSetErrno(0);
    if (e == p) {
        if (end)
            *end = (char *)s;
        return 0;
    }
    if (end)
        *end = e;
    if (range)
        MsvcrtSetErrno(ERANGE);
    return v;
}

double __cdecl _strtod_l(const char *s, char **end, void *locale)
{
    return strtod(s, end);
}

double __cdecl wcstod(const wchar_t *s, wchar_t **end)
{
    if (end)
        *end = (wchar_t *)s;
    if (!s) {
        MSVCRT_INVALID(EINVAL);
        return 0;
    }
    const wchar_t *p = s;
    while (MsvcrtWctype(*p) & _SPACE)
        p++;
    int range;
    wchar_t *e;
    double v = MsvcrtWcstod(p, &e, &range);
    MsvcrtSetErrno(0);
    if (e == p) {
        if (end)
            *end = (wchar_t *)s;
        return 0;
    }
    if (end)
        *end = e;
    if (range)
        MsvcrtSetErrno(ERANGE);
    return v;
}

double __cdecl _wcstod_l(const wchar_t *s, wchar_t **end, void *locale)
{
    return wcstod(s, end);
}

/*
 * the float forms round once to a float, which strtod and a cast would not; long double is double here, so strtold
 * and wcstold are the double conversions.
 */
float __cdecl strtof(const char *s, char **end)
{
    if (end)
        *end = (char *)s;
    if (!s) {
        MSVCRT_INVALID(EINVAL);
        return 0;
    }
    const char *p = s, *e;
    while (MSVCRT_CTYPE(*p) & _SPACE)
        p++;
    int range;
    double v = parse_float_a(p, &e, &range, 1);
    MsvcrtSetErrno(0);
    if (e == p)
        return 0;
    if (end)
        *end = (char *)e;
    if (range)
        MsvcrtSetErrno(ERANGE);
    return (float)v;
}

float __cdecl wcstof(const wchar_t *s, wchar_t **end)
{
    if (end)
        *end = (wchar_t *)s;
    if (!s) {
        MSVCRT_INVALID(EINVAL);
        return 0;
    }
    const wchar_t *p = s, *e;
    while (MsvcrtWctype(*p) & _SPACE)
        p++;
    int range;
    double v = parse_float_w(p, &e, &range, 1);
    MsvcrtSetErrno(0);
    if (e == p)
        return 0;
    if (end)
        *end = (wchar_t *)e;
    if (range)
        MsvcrtSetErrno(ERANGE);
    return (float)v;
}

float __cdecl _strtof_l(const char *s, char **end, void *locale)
{
    return strtof(s, end);
}

float __cdecl _wcstof_l(const wchar_t *s, wchar_t **end, void *locale)
{
    return wcstof(s, end);
}

double __cdecl strtold(const char *s, char **end)
{
    return strtod(s, end);
}

double __cdecl _strtold_l(const char *s, char **end, void *locale)
{
    return strtod(s, end);
}

double __cdecl wcstold(const wchar_t *s, wchar_t **end)
{
    return wcstod(s, end);
}

double __cdecl _wcstold_l(const wchar_t *s, wchar_t **end, void *locale)
{
    return wcstod(s, end);
}

double __cdecl atof(const char *s)
{
    if (!s) {
        MSVCRT_INVALID(EINVAL);
        return 0;
    }
    return strtod(s, nullptr);
}

double __cdecl _atof_l(const char *s, void *locale)
{
    return atof(s);
}

double __cdecl _wtof(const wchar_t *s)
{
    if (!s) {
        MSVCRT_INVALID(EINVAL);
        return 0;
    }
    return wcstod(s, nullptr);
}

double __cdecl _wtof_l(const wchar_t *s, void *locale)
{
    return _wtof(s);
}

typedef struct {
    double x;
} _CRT_DOUBLE;

typedef struct {
    float f;
} _CRT_FLOAT;

typedef struct {
    double x;
} _LDOUBLE;

/* _OVERFLOW 3 and _UNDERFLOW 4 like msvcrt's _atodbl. */
int __cdecl _atodbl(_CRT_DOUBLE *out, char *s)
{
    const char *p = s;
    while (MSVCRT_CTYPE(*p) & _SPACE)
        p++;
    int range;
    out->x = MsvcrtStrtod(p, nullptr, &range);
    return range == 1 ? 3 : range == 2 ? 4 : 0;
}

int __cdecl _atodbl_l(_CRT_DOUBLE *out, char *s, void *locale)
{
    return _atodbl(out, s);
}

int __cdecl _atoldbl(_LDOUBLE *out, char *s)
{
    return _atodbl((_CRT_DOUBLE *)out, s);
}

int __cdecl _atoldbl_l(_LDOUBLE *out, char *s, void *locale)
{
    return _atodbl((_CRT_DOUBLE *)out, s);
}

int __cdecl _atoflt(_CRT_FLOAT *out, const char *s)
{
    const char *p = s;
    while (MSVCRT_CTYPE(*p) & _SPACE)
        p++;
    int range;
    double v = MsvcrtStrtod(p, nullptr, &range);
    if (!range && (v > FLT_MAX || v < -FLT_MAX))
        range = 1;
    out->f = (float)v;
    if (!range && v != 0 && out->f == 0)
        range = 2;
    return range == 1 ? 3 : range == 2 ? 4 : 0;
}

int __cdecl _atoflt_l(_CRT_FLOAT *out, const char *s, void *locale)
{
    return _atoflt(out, s);
}
