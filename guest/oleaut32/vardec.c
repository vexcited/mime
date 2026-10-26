/* arithmetic on DECIMAL and CY values, and rounding and comparing of doubles. */
#include "oleaut32_private.h"

#include <mime/win32/oleauto.h>

double __cdecl pow(double x, double y);
double __cdecl floor(double x);
double __cdecl modf(double x, double *whole);
double __cdecl fmod(double x, double y);
int *__cdecl _errno(void);

#define E_INVALID_ARGUMENT ((HRESULT)0x80070057L)
#define VARCMP_LT 0
#define VARCMP_EQ 1
#define VARCMP_GT 2

/* ------------------------------------------------------------------ unsigned numbers of 224 bits */

#define BIG_WORDS 7

typedef struct {
    ULONG w[BIG_WORDS]; /* least significant first. */
} BIG;

static const ULONG g_pow10[10] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000};

static void big_from_decimal(const DECIMAL *d, BIG *n)
{
    memset(n, 0, sizeof(*n));
    n->w[0] = (ULONG)d->Lo64;
    n->w[1] = (ULONG)(d->Lo64 >> 32);
    n->w[2] = d->Hi32;
}

static BOOL big_is_zero(const BIG *n)
{
    for (ULONG i = 0; i < BIG_WORDS; i++)
        if (n->w[i])
            return FALSE;
    return TRUE;
}

static ULONG big_bits(const BIG *n)
{
    for (ULONG i = BIG_WORDS; i-- > 0;) {
        if (!n->w[i])
            continue;
        ULONG bits = 32 * i;
        for (ULONG top = n->w[i]; top; top >>= 1)
            bits++;
        return bits;
    }
    return 0;
}

static int big_compare(const BIG *a, const BIG *b)
{
    for (ULONG i = BIG_WORDS; i-- > 0;)
        if (a->w[i] != b->w[i])
            return a->w[i] < b->w[i] ? -1 : 1;
    return 0;
}

/* the carry out of the top word is lost, the numbers are far from filling it. */
static void big_mul_small(BIG *n, ULONG m)
{
    ULONGLONG carry = 0;
    for (ULONG i = 0; i < BIG_WORDS; i++) {
        ULONGLONG p = (ULONGLONG)n->w[i] * m + carry;
        n->w[i] = (ULONG)p;
        carry = p >> 32;
    }
}

static void big_mul_pow10(BIG *n, ULONG digits)
{
    while (digits) {
        ULONG step = digits > 9 ? 9 : digits;
        big_mul_small(n, g_pow10[step]);
        digits -= step;
    }
}

static ULONG big_div_small(BIG *n, ULONG d)
{
    ULONGLONG rem = 0;
    for (ULONG i = BIG_WORDS; i-- > 0;) {
        ULONGLONG cur = (rem << 32) | n->w[i];
        n->w[i] = (ULONG)(cur / d);
        rem = cur % d;
    }
    return (ULONG)rem;
}

static void big_add(BIG *a, const BIG *b)
{
    ULONGLONG carry = 0;
    for (ULONG i = 0; i < BIG_WORDS; i++) {
        ULONGLONG s = (ULONGLONG)a->w[i] + b->w[i] + carry;
        a->w[i] = (ULONG)s;
        carry = s >> 32;
    }
}

static void big_add_small(BIG *a, ULONG v)
{
    ULONGLONG carry = v;
    for (ULONG i = 0; i < BIG_WORDS && carry; i++) {
        ULONGLONG s = (ULONGLONG)a->w[i] + carry;
        a->w[i] = (ULONG)s;
        carry = s >> 32;
    }
}

/* a must not be smaller than b. */
static void big_sub(BIG *a, const BIG *b)
{
    LONGLONG borrow = 0;
    for (ULONG i = 0; i < BIG_WORDS; i++) {
        LONGLONG d = (LONGLONG)a->w[i] - b->w[i] - borrow;
        borrow = d < 0;
        a->w[i] = (ULONG)d;
    }
}

static void big_shift_left_one(BIG *n, ULONG bit)
{
    for (ULONG i = 0; i < BIG_WORDS; i++) {
        ULONG next = n->w[i] >> 31;
        n->w[i] = (n->w[i] << 1) | bit;
        bit = next;
    }
}

/* the quotient replaces n and the remainder goes to rem. */
static void big_divide(BIG *n, const BIG *d, BIG *rem)
{
    memset(rem, 0, sizeof(*rem));
    ULONG bits = big_bits(n);
    BIG q = {};
    for (ULONG i = bits; i-- > 0;) {
        big_shift_left_one(rem, (n->w[i / 32] >> (i % 32)) & 1);
        if (big_compare(rem, d) >= 0) {
            big_sub(rem, d);
            q.w[i / 32] |= 1u << (i % 32);
        }
    }
    *n = q;
}

static void decimal_from_big(DECIMAL *out, const BIG *n, ULONG scale, ULONG sign)
{
    out->wReserved = 0;
    out->scale = (BYTE)scale;
    out->sign = (BYTE)sign;
    out->Hi32 = n->w[2];
    out->Lo64 = n->w[0] | ((ULONGLONG)n->w[1] << 32);
}

/*
 * brings a number down to 96 bits by dividing by powers of ten, rounding half to even, and the scale down to at most
 * 28. How many digits go is estimated from the width, not found by trying, which sometimes costs a digit of the result.
 */
static BOOL fit_decimal(BIG *n, LONG *scale)
{
    ULONG bits = big_bits(n);
    LONG drop = 0;
    if (bits > 96) {
        drop = (LONG)(77 * (bits - 97) / 256 + 1);
        if (drop > *scale)
            return FALSE;
    }
    if (*scale - 28 > drop)
        drop = *scale - 28;
    if (!drop)
        return TRUE;
    *scale -= drop;
    ULONG sticky = 0, rem = 0, divisor = 1;
    while (drop > 0) {
        ULONG step = drop > 9 ? 9 : drop;
        sticky |= rem;
        divisor = g_pow10[step];
        rem = big_div_small(n, divisor);
        drop -= step;
    }
    for (;;) {
        if (big_bits(n) > 96) {
            (*scale)--;
            sticky |= rem;
            divisor = 10;
            rem = big_div_small(n, divisor);
            continue;
        }
        ULONG half = divisor >> 1;
        if (rem < half || (rem == half && !(sticky | (n->w[0] & 1))))
            break;
        big_add_small(n, 1);
        if (big_bits(n) <= 96)
            break;
        sticky = 0;
        (*scale)--;
        divisor = 10;
        rem = big_div_small(n, divisor);
    }
    return *scale >= 0;
}

/* ------------------------------------------------------------------ decimal */

static HRESULT add_or_subtract(const DECIMAL *a, const DECIMAL *b, DECIMAL *out, BYTE flip)
{
    BYTE sign_a = a->sign & 0x80, sign_b = (b->sign & 0x80) ^ flip;
    /* q is the operand with the smaller scale, the first one when they are equal, brought to the scale of p. */
    BIG q, p;
    BYTE sign_q = sign_a, sign_p = sign_b;
    LONG scale = a->scale;
    big_from_decimal(a, &q);
    big_from_decimal(b, &p);
    if (a->scale > b->scale) {
        BIG swap = q;
        q = p;
        p = swap;
        sign_q = sign_b;
        sign_p = sign_a;
    } else {
        scale = b->scale;
    }
    ULONG difference = (ULONG)(a->scale > b->scale ? a->scale - b->scale : b->scale - a->scale);
    if (difference) {
        if (difference > 9 && big_is_zero(&q)) {
            decimal_from_big(out, &p, (ULONG)scale, sign_p);
            return S_OK;
        }
        big_mul_pow10(&q, difference);
    }
    BYTE sign = sign_q;
    if (sign_q == sign_p) {
        big_add(&q, &p);
    } else if (big_compare(&q, &p) >= 0) {
        big_sub(&q, &p);
    } else {
        big_sub(&p, &q);
        q = p;
        sign ^= 0x80;
    }
    if (!fit_decimal(&q, &scale))
        return DISP_E_OVERFLOW;
    decimal_from_big(out, &q, (ULONG)scale, sign);
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VarDecAdd(const DECIMAL *left, const DECIMAL *right, DECIMAL *result)
{
    return add_or_subtract(left, right, result, 0);
}

WINOLEAUTAPI HRESULT WINAPI VarDecSub(const DECIMAL *left, const DECIMAL *right, DECIMAL *result)
{
    return add_or_subtract(left, right, result, 0x80);
}

WINOLEAUTAPI HRESULT WINAPI VarDecMul(const DECIMAL *left, const DECIMAL *right, DECIMAL *result)
{
    LONG scale = left->scale + right->scale;
    BYTE sign = left->sign ^ right->sign;
    BIG a, b, product = {};
    big_from_decimal(left, &a);
    big_from_decimal(right, &b);
    /* a product of two small numbers that the scale makes less than half a unit of the last place is exactly zero. */
    if (scale > 47 && !a.w[2] && !a.w[1] && !b.w[2] && !b.w[1]) {
        memset(result, 0, sizeof(*result));
        return S_OK;
    }
    BOOL wide = a.w[1] || a.w[2] || b.w[1] || b.w[2];
    for (ULONG i = 0; i < 3; i++) {
        ULONGLONG carry = 0;
        for (ULONG j = 0; j < 3; j++) {
            ULONGLONG cur = (ULONGLONG)a.w[i] * b.w[j] + product.w[i + j] + carry;
            product.w[i + j] = (ULONG)cur;
            carry = cur >> 32;
        }
        product.w[i + 3] += (ULONG)carry;
    }
    /* a product of large operands that is zero is a plain zero. */
    if (wide && big_is_zero(&product)) {
        memset(result, 0, sizeof(*result));
        return S_OK;
    }
    if (!fit_decimal(&product, &scale))
        return DISP_E_OVERFLOW;
    decimal_from_big(result, &product, (ULONG)scale, sign);
    return S_OK;
}

/*
 * how many digits can follow the quotient: as many as fit in 96 bits, to the scale of 28, at most nine at once. Decided
 * on the upper 64 bits of the quotient. -1 when a negative scale cannot be brought back to zero.
 */
static int digits_that_fit(const BIG *q, LONG scale)
{
    /* the upper 64 bits of the largest number that stays in 96 bits after that many digits. */
    static const ULONGLONG limits[10] = {0,
                                         0x1999999999999999ull,
                                         0x028f5c28f5c28f5cull,
                                         0x004189374bc6a7efull,
                                         0x00068db8bac710cbull,
                                         0x0000a7c5ac471b47ull,
                                         0x000010c6f7a0b5edull,
                                         0x000001ad7f29abcaull,
                                         0x0000002af31dc461ull,
                                         0x000000044b82fa09ull};
    int fit = 0;
    if (scale < 28) {
        ULONGLONG top = ((ULONGLONG)q->w[2] << 32) | q->w[1];
        int most = scale > 19 ? (int)(28 - scale) : 9;
        /* a small quotient takes nine digits at once, whatever the scale: a negative one is brought up by them. */
        if (scale <= 19 && top < limits[9])
            return 9;
        for (int k = most; k > 0; k--) {
            if (top < limits[k]) {
                fit = k;
                break;
            }
        }
    }
    return scale + fit < 0 ? -1 : fit;
}

WINOLEAUTAPI HRESULT WINAPI VarDecDiv(const DECIMAL *left, const DECIMAL *right, DECIMAL *result)
{
    BIG divisor, quotient, rem;
    big_from_decimal(right, &divisor);
    if (big_is_zero(&divisor))
        return DISP_E_DIVBYZERO;
    big_from_decimal(left, &quotient);
    LONG scale = (LONG)left->scale - (LONG)right->scale;
    big_divide(&quotient, &divisor, &rem);
    for (;;) {
        int digits;
        if (!big_is_zero(&rem)) {
            digits = digits_that_fit(&quotient, scale);
            if (digits < 0)
                return DISP_E_OVERFLOW;
            if (digits == 0) {
                /* what is left decides the rounding: past half goes up, exactly half to the even one. */
                BIG twice = rem;
                big_shift_left_one(&twice, 0);
                int order = big_compare(&twice, &divisor);
                if (order > 0 || (order == 0 && (quotient.w[0] & 1))) {
                    big_add_small(&quotient, 1);
                    if (big_bits(&quotient) > 96)
                        return DISP_E_OVERFLOW;
                }
                break;
            }
        } else {
            if (scale >= 0)
                break;
            digits = scale < -9 ? 9 : (int)-scale;
        }
        scale += digits;
        big_mul_small(&quotient, g_pow10[digits]);
        if (big_bits(&quotient) > 96)
            return DISP_E_OVERFLOW;
        big_mul_small(&rem, g_pow10[digits]);
        BIG step;
        step = rem;
        big_divide(&step, &divisor, &rem);
        big_add(&quotient, &step);
    }
    /* trailing zeros of the quotient are not kept. */
    while (scale > 0) {
        BIG shorter = quotient;
        if (big_div_small(&shorter, 10))
            break;
        quotient = shorter;
        scale--;
    }
    decimal_from_big(result, &quotient, (ULONG)scale, left->sign ^ right->sign);
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VarDecAbs(const DECIMAL *in, DECIMAL *result)
{
    DECIMAL copy = *in;
    copy.sign &= (BYTE)~0x80;
    *result = copy;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VarDecNeg(const DECIMAL *in, DECIMAL *result)
{
    DECIMAL copy = *in;
    copy.sign ^= 0x80;
    *result = copy;
    return S_OK;
}

/* drops the fraction, and says whether any of it was not zero. */
static BOOL truncate_decimal(const DECIMAL *in, DECIMAL *out)
{
    BIG n;
    big_from_decimal(in, &n);
    BOOL lost = FALSE;
    for (LONG left = in->scale; left > 0;) {
        ULONG step = left > 9 ? 9 : (ULONG)left;
        lost |= big_div_small(&n, g_pow10[step]) != 0;
        left -= step;
    }
    decimal_from_big(out, &n, 0, in->sign);
    out->wReserved = in->wReserved;
    return lost;
}

WINOLEAUTAPI HRESULT WINAPI VarDecFix(const DECIMAL *in, DECIMAL *result)
{
    DECIMAL out;
    truncate_decimal(in, &out);
    *result = out;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VarDecInt(const DECIMAL *in, DECIMAL *result)
{
    DECIMAL out;
    if (truncate_decimal(in, &out) && (out.sign & 0x80)) {
        /* towards minus infinity: the magnitude grows, and past 96 bits the carry is dropped. */
        if (++out.Lo64 == 0)
            out.Hi32++;
    }
    *result = out;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VarDecRound(const DECIMAL *in, int decimals, DECIMAL *result)
{
    if (decimals < 0)
        return E_INVALID_ARGUMENT;
    if ((int)in->scale <= decimals) {
        *result = *in;
        return S_OK;
    }
    BIG n;
    big_from_decimal(in, &n);
    ULONG sticky = 0, rem = 0, divisor = 1;
    for (LONG left = in->scale - decimals; left > 0;) {
        ULONG step = left > 9 ? 9 : (ULONG)left;
        sticky |= rem;
        divisor = g_pow10[step];
        rem = big_div_small(&n, divisor);
        left -= step;
    }
    ULONG half = divisor >> 1;
    if (rem > half || (rem == half && (sticky | (n.w[0] & 1))))
        big_add_small(&n, 1);
    decimal_from_big(result, &n, (ULONG)decimals, in->sign);
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VarDecCmp(const DECIMAL *left, const DECIMAL *right)
{
    /* a zero has no sign: the classes are less than zero, zero and more than zero. */
    int class_left = (left->Hi32 | left->Lo64) ? ((left->sign & 0x80) ? -1 : 1) : 0;
    int class_right = (right->Hi32 | right->Lo64) ? ((right->sign & 0x80) ? -1 : 1) : 0;
    if (class_left != class_right)
        return class_left > class_right ? VARCMP_GT : VARCMP_LT;
    if (!class_left)
        return VARCMP_EQ;
    DECIMAL difference = {};
    if (FAILED(add_or_subtract(left, right, &difference, 0x80)))
        return VARCMP_EQ;
    if (!(difference.Hi32 | difference.Lo64))
        return VARCMP_EQ;
    return (difference.sign & 0x80) ? VARCMP_LT : VARCMP_GT;
}

WINOLEAUTAPI HRESULT WINAPI VarDecCmpR8(const DECIMAL *left, double right)
{
    DECIMAL converted = {};
    if (SUCCEEDED(VarDecFromR8(right, &converted)))
        return VarDecCmp(left, &converted);
    /* out of the range of a decimal, the sign tells which side it is on. */
    return right < 0.0 ? VARCMP_GT : VARCMP_LT;
}

/* ------------------------------------------------------------------ currency */

#define CY_UNIT 10000

WINOLEAUTAPI HRESULT WINAPI VarCyAbs(CY in, CY *result)
{
    if (in.int64 < 0) {
        in.int64 = (LONGLONG)(0 - (ULONGLONG)in.int64);
        if (in.int64 < 0)
            return DISP_E_OVERFLOW;
    }
    result->int64 = in.int64;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VarCyAdd(CY left, CY right, CY *result)
{
    ULONGLONG sum = (ULONGLONG)left.int64 + (ULONGLONG)right.int64;
    if ((left.int64 < 0) == (right.int64 < 0) && ((LONGLONG)sum < 0) != (left.int64 < 0))
        return DISP_E_OVERFLOW;
    result->int64 = (LONGLONG)sum;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VarCySub(CY left, CY right, CY *result)
{
    ULONGLONG difference = (ULONGLONG)left.int64 - (ULONGLONG)right.int64;
    if ((left.int64 < 0) != (right.int64 < 0) && ((LONGLONG)difference < 0) != (left.int64 < 0))
        return DISP_E_OVERFLOW;
    result->int64 = (LONGLONG)difference;
    return S_OK;
}

/* the 128 bit product of two 64 bit numbers. */
static void multiply_64(ULONGLONG a, ULONGLONG b, ULONGLONG *high, ULONGLONG *low)
{
    ULONGLONG a_low = (ULONG)a, a_high = a >> 32, b_low = (ULONG)b, b_high = b >> 32;
    ULONGLONG ll = a_low * b_low, lh = a_low * b_high, hl = a_high * b_low, hh = a_high * b_high;
    ULONGLONG middle = (ll >> 32) + (ULONG)lh + (ULONG)hl;
    *low = (middle << 32) | (ULONG)ll;
    *high = hh + (lh >> 32) + (hl >> 32) + (middle >> 32);
}

/* a 128 bit number below 10000 times 2^64, divided by 10000. */
static ULONGLONG divide_by_unit(ULONGLONG high, ULONGLONG low, ULONG *remainder)
{
    ULONGLONG rem = high;
    ULONGLONG quotient_high = (rem << 32 | (low >> 32)) / CY_UNIT;
    rem = (rem << 32 | (low >> 32)) % CY_UNIT;
    ULONGLONG quotient_low = (rem << 32 | (ULONG)low) / CY_UNIT;
    rem = (rem << 32 | (ULONG)low) % CY_UNIT;
    *remainder = (ULONG)rem;
    return (quotient_high << 32) | (quotient_low & 0xFFFFFFFFull);
}

static LONGLONG magnitude(LONGLONG v)
{
    return v < 0 ? (LONGLONG)(0 - (ULONGLONG)v) : v;
}

WINOLEAUTAPI HRESULT WINAPI VarCyMul(CY left, CY right, CY *result)
{
    BOOL negative = (left.int64 < 0) != (right.int64 < 0);
    ULONGLONG high, low;
    multiply_64((ULONGLONG)magnitude(left.int64), (ULONGLONG)magnitude(right.int64), &high, &low);
    if (high >= CY_UNIT)
        return DISP_E_OVERFLOW;
    ULONG rem;
    ULONGLONG q = divide_by_unit(high, low, &rem);
    if (rem + (q & 1) > CY_UNIT / 2) {
        if (++q == 0)
            return DISP_E_OVERFLOW;
    }
    if (negative)
        q = 0 - q;
    if (q && ((LONGLONG)q < 0) != negative)
        return DISP_E_OVERFLOW;
    result->int64 = (LONGLONG)q;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VarCyMulI8(CY left, LONG64 right, CY *result)
{
    BOOL negative = (left.int64 < 0) != (right < 0);
    ULONGLONG high, low;
    multiply_64((ULONGLONG)magnitude(left.int64), (ULONGLONG)magnitude(right), &high, &low);
    if (high || (LONGLONG)low < 0)
        return DISP_E_OVERFLOW;
    result->int64 = negative ? (LONGLONG)(0 - low) : (LONGLONG)low;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VarCyMulI4(CY left, LONG right, CY *result)
{
    return VarCyMulI8(left, right, result) == S_OK ? S_OK : DISP_E_OVERFLOW;
}

WINOLEAUTAPI HRESULT WINAPI VarCyNeg(CY in, CY *result)
{
    if (in.int64 == (LONGLONG)0x8000000000000000ull)
        return DISP_E_OVERFLOW;
    result->int64 = -in.int64;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VarCyRound(CY in, int decimals, CY *result)
{
    if (decimals < 0)
        return E_INVALID_ARGUMENT;
    if (decimals < 4) {
        static const LONG units[4] = {10000, 1000, 100, 10};
        LONG unit = units[decimals];
        LONGLONG q = in.int64 / unit, r = in.int64 % unit;
        LONGLONG twice = 2 * r < 0 ? -2 * r : 2 * r;
        if (twice != unit || (q & 1))
            q += (2 * r) / unit;
        LONGLONG rounded = (LONGLONG)((ULONGLONG)q * (ULONGLONG)unit);
        if ((q < 0) != (rounded < 0) && q != 0)
            return DISP_E_OVERFLOW;
        in.int64 = rounded;
    }
    result->int64 = in.int64;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VarCyFix(CY in, CY *result)
{
    result->int64 = (in.int64 / CY_UNIT) * CY_UNIT;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VarCyInt(CY in, CY *result)
{
    LONGLONG rem = in.int64 % CY_UNIT;
    if (rem) {
        LONGLONG down = in.int64 < 0 ? rem + CY_UNIT : rem;
        LONGLONG floored = in.int64 - down;
        if ((floored < 0) != (in.int64 < 0))
            return DISP_E_OVERFLOW;
        in.int64 = floored;
    }
    result->int64 = in.int64;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VarCyCmp(CY left, CY right)
{
    return left.int64 < right.int64 ? VARCMP_LT : left.int64 == right.int64 ? VARCMP_EQ : VARCMP_GT;
}

WINOLEAUTAPI HRESULT WINAPI VarCyCmpR8(CY left, double right)
{
    CY converted = {};
    if (FAILED(VarCyFromR8(right, &converted)))
        return right > 0.0 ? VARCMP_LT : VARCMP_GT;
    return VarCyCmp(left, converted);
}

/* ------------------------------------------------------------------ doubles */

WINOLEAUTAPI HRESULT WINAPI VarR4CmpR8(float left, double right)
{
    float narrowed = (float)right;
    if (narrowed <= left)
        return left > narrowed ? VARCMP_GT : VARCMP_EQ;
    return VARCMP_LT;
}

WINOLEAUTAPI HRESULT WINAPI VarR8Pow(double left, double right, double *result)
{
    if (left == 0.0) {
        if (right == 0.0) {
            *result = 1.0;
            return S_OK;
        }
        if (right < 0.0)
            return E_INVALID_ARGUMENT;
    }
    if (left < 0.0 && right != floor(right))
        return E_INVALID_ARGUMENT;
    *_errno() = 0;
    double value = pow(left, right);
    int error = *_errno();
    if (error == 34 && value != 0.0)
        return DISP_E_OVERFLOW;
    if (error == 33)
        return E_INVALID_ARGUMENT;
    *result = value;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VarR8Round(double in, int decimals, double *result)
{
    static const double scales[23] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
                                      1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
    if ((unsigned int)decimals > 22)
        return E_INVALID_ARGUMENT;
    double magnitude_of_input = in < 0 ? -in : in;
    if (magnitude_of_input < 1.0e16) {
        double whole;
        double scaled = in * scales[decimals];
        double fraction = modf(scaled, &whole);
        double magnitude_of_fraction = fraction < 0 ? -fraction : fraction;
        /* exactly half is left to the even whole, anything else goes to the nearest. */
        if (magnitude_of_fraction != 0.5 || fmod(whole, 2.0) != 0.0)
            whole += (double)(int)(fraction + fraction);
        in = whole / scales[decimals];
    }
    *result = in;
    return S_OK;
}
