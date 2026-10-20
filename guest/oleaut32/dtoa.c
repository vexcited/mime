/*
 * exact conversions between doubles and decimal digits, with a small big number type. Nothing here depends on the
 * locale: the callers put the decimal point and the exponent where their format wants them.
 */
#include "dtoa.h"

/* 2^1074 and 10^340 both fit. */
#define BIG_LIMBS 44

typedef struct {
    int Count;
    ULONG Limb[BIG_LIMBS];
} BIG;

static void big_trim(BIG *b)
{
    while (b->Count && !b->Limb[b->Count - 1])
        b->Count--;
}

static void big_set(BIG *b, ULONGLONG v)
{
    b->Limb[0] = (ULONG)v;
    b->Limb[1] = (ULONG)(v >> 32);
    b->Count = 2;
    big_trim(b);
}

/* b = b * m + a. */
static void big_mul_add(BIG *b, ULONG m, ULONG a)
{
    ULONGLONG carry = a;
    for (int i = 0; i < b->Count; i++) {
        ULONGLONG t = (ULONGLONG)b->Limb[i] * m + carry;
        b->Limb[i] = (ULONG)t;
        carry = t >> 32;
    }
    if (carry && b->Count < BIG_LIMBS)
        b->Limb[b->Count++] = (ULONG)carry;
}

/* b = b / d, returns the remainder. */
static ULONG big_div_small(BIG *b, ULONG d)
{
    ULONGLONG rem = 0;
    for (int i = b->Count - 1; i >= 0; i--) {
        ULONGLONG t = (rem << 32) | b->Limb[i];
        b->Limb[i] = (ULONG)(t / d);
        rem = t % d;
    }
    big_trim(b);
    return (ULONG)rem;
}

static void big_shl(BIG *b, int bits)
{
    if (!b->Count || !bits)
        return;
    int words = bits / 32, rest = bits % 32;
    int count = b->Count + words + 1;
    if (count > BIG_LIMBS)
        count = BIG_LIMBS;
    ULONG out[BIG_LIMBS] = {0};
    for (int i = 0; i < b->Count; i++) {
        ULONGLONG v = (ULONGLONG)b->Limb[i] << rest;
        if (i + words < count)
            out[i + words] |= (ULONG)v;
        if (i + words + 1 < count)
            out[i + words + 1] |= (ULONG)(v >> 32);
    }
    memcpy(b->Limb, out, sizeof(ULONG) * count);
    b->Count = count;
    big_trim(b);
}

static int big_cmp(const BIG *a, const BIG *b)
{
    if (a->Count != b->Count)
        return a->Count < b->Count ? -1 : 1;
    for (int i = a->Count - 1; i >= 0; i--)
        if (a->Limb[i] != b->Limb[i])
            return a->Limb[i] < b->Limb[i] ? -1 : 1;
    return 0;
}

/* a = a - b, for a >= b. */
static void big_sub(BIG *a, const BIG *b)
{
    LONGLONG borrow = 0;
    for (int i = 0; i < a->Count; i++) {
        LONGLONG t = (LONGLONG)a->Limb[i] - (i < b->Count ? b->Limb[i] : 0) - borrow;
        borrow = t < 0;
        a->Limb[i] = (ULONG)t;
    }
    big_trim(a);
}

static int big_bits(const BIG *b)
{
    if (!b->Count)
        return 0;
    ULONG top = b->Limb[b->Count - 1];
    int n = 0;
    while (top) {
        n++;
        top >>= 1;
    }
    return (b->Count - 1) * 32 + n;
}

static int big_bit(const BIG *b, int i)
{
    return i / 32 < b->Count ? (int)((b->Limb[i / 32] >> (i % 32)) & 1) : 0;
}

static void big_pow10(BIG *b, int e)
{
    static const ULONG small[9] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000};
    big_set(b, 1);
    for (; e >= 9; e -= 9)
        big_mul_add(b, 1000000000, 0);
    if (e)
        big_mul_add(b, small[e], 0);
}

/* r = a * b. */
static void big_mul(BIG *r, const BIG *a, const BIG *b)
{
    ULONG out[BIG_LIMBS] = {0};
    for (int i = 0; i < a->Count; i++) {
        ULONGLONG carry = 0;
        for (int j = 0; j < b->Count && i + j < BIG_LIMBS; j++) {
            ULONGLONG t = (ULONGLONG)a->Limb[i] * b->Limb[j] + out[i + j] + carry;
            out[i + j] = (ULONG)t;
            carry = t >> 32;
        }
        if (i + b->Count < BIG_LIMBS)
            out[i + b->Count] = (ULONG)carry;
    }
    memcpy(r->Limb, out, sizeof(out));
    r->Count = BIG_LIMBS;
    big_trim(r);
}

/* the 64 bits from the top of b, whose length is bits (at least 1): b is top times 2^*shift plus a rest. */
static ULONGLONG big_top64(const BIG *b, int *shift, BOOL *rest)
{
    int bits = big_bits(b);
    ULONGLONG top = 0;
    *rest = FALSE;
    if (bits <= 64) {
        for (int i = 0; i < b->Count; i++)
            top |= (ULONGLONG)b->Limb[i] << (32 * i);
        *shift = 0;
        return top;
    }
    int low = bits - 64;
    for (int i = 0; i < 64; i++)
        if (big_bit(b, low + i))
            top |= 1ull << i;
    for (int i = 0; i < low && !*rest; i++)
        *rest = big_bit(b, i) != 0;
    *shift = low;
    return top;
}

/* ------------------------------------------------------------------ doubles to digits */

int AutDoubleDigits(double x, int ndigits, char *digits, int *point)
{
    ULONGLONG bits, m;
    int e;
    *point = 0;
    digits[0] = 0;
    if (ndigits > 40)
        ndigits = 40;
    if (ndigits < 1)
        ndigits = 1;
    memcpy(&bits, &x, 8);
    bits &= 0x7FFFFFFFFFFFFFFFull;
    e = (int)(bits >> 52);
    if (e == 0x7FF)
        return 0;
    m = bits & 0xFFFFFFFFFFFFFull;
    if (e) {
        m |= 1ull << 52;
        e -= 1075;
    } else {
        e = -1074;
    }
    if (!m)
        return 0;

    /* the leading digits of the exact value, one more than asked, and whether anything nonzero follows. */
    char all[360];
    int total = 0, pt = 0;
    BOOL more = FALSE;
    BIG n;
    if (e >= 0) {
        big_set(&n, m);
        big_shl(&n, e);
        char rev[360];
        int count = 0;
        while (n.Count) {
            ULONG r = big_div_small(&n, 1000000000);
            for (int i = 0; i < 9; i++) {
                rev[count++] = (char)('0' + r % 10);
                r /= 10;
            }
        }
        while (count > 1 && rev[count - 1] == '0')
            count--;
        pt = count;
        for (int i = 0; i < count; i++) {
            char c = rev[count - 1 - i];
            if (total < ndigits + 1)
                all[total++] = c;
            else if (c != '0')
                more = TRUE;
        }
    } else {
        int k = -e;
        ULONGLONG ipart = k < 64 ? m >> k : 0;
        ULONGLONG frac = k < 64 ? m & ((1ull << k) - 1) : m;
        if (ipart) {
            char rev[24];
            int count = 0;
            while (ipart) {
                rev[count++] = (char)('0' + ipart % 10);
                ipart /= 10;
            }
            pt = count;
            for (int i = 0; i < count; i++) {
                char c = rev[count - 1 - i];
                if (total < ndigits + 1)
                    all[total++] = c;
                else if (c != '0')
                    more = TRUE;
            }
        }
        /* the fraction is frac / 2^k: ten times it, the part above k bits is the next digit. */
        big_set(&n, frac);
        int zeros = 0;
        while (n.Count) {
            big_mul_add(&n, 10, 0);
            int digit = 0;
            for (int b = k + 3; b >= k; b--)
                digit = digit * 2 + big_bit(&n, b);
            for (int b = k; b < k + 4; b++)
                if (b / 32 < n.Count)
                    n.Limb[b / 32] &= ~(1u << (b % 32));
            big_trim(&n);
            if (!total && !digit) {
                zeros++;
                continue;
            }
            if (total < ndigits + 1) {
                all[total++] = (char)('0' + digit);
            } else {
                if (digit)
                    more = TRUE;
                if (more)
                    break;
            }
        }
        if (!pt)
            pt = -zeros;
    }
    if (!total)
        return 0;

    int keep = total < ndigits ? total : ndigits;
    if (total > ndigits) {
        char c = all[ndigits];
        /* a tie goes away from zero, as the runtime of C that windows' text comes from rounds it. */
        BOOL up = c >= '5';
        if (up) {
            int i = ndigits - 1;
            for (; i >= 0; i--) {
                if (all[i] == '9') {
                    all[i] = '0';
                } else {
                    all[i]++;
                    break;
                }
            }
            if (i < 0) {
                memmove(all + 1, all, ndigits);
                all[0] = '1';
                pt++;
                keep = ndigits;
            }
        }
    }
    while (keep > 1 && all[keep - 1] == '0')
        keep--;
    if (keep == 1 && all[0] == '0')
        return 0;
    memcpy(digits, all, keep);
    digits[keep] = 0;
    *point = pt;
    return keep;
}

/* ------------------------------------------------------------------ digits to doubles */

static const double g_exact_pow10[23] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
                                         1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};

static double dbl_infinity(void)
{
    ULONGLONG bits = 0x7FF0000000000000ull;
    double r;
    memcpy(&r, &bits, 8);
    return r;
}

/* mant * 2^exp2 rounded to nearest even, with a sticky bit for what lies below mant. */
static double make_double(ULONGLONG mant, int exp2, BOOL sticky)
{
    if (!mant)
        return 0.0;
    int len = 0;
    for (ULONGLONG t = mant; t; t >>= 1)
        len++;
    /* the exponent of the lowest of the 53 bits to keep, not below the smallest denormal's. */
    int low = exp2 + len - 53;
    if (low < -1074)
        low = -1074;
    int drop = low - exp2;
    ULONGLONG kept;
    if (drop <= 0) {
        kept = mant << -drop;
    } else if (drop > 64) {
        kept = 0;
    } else {
        ULONGLONG rest = drop == 64 ? mant : mant & ((1ull << drop) - 1);
        kept = drop == 64 ? 0 : mant >> drop;
        ULONGLONG half = 1ull << (drop - 1);
        if (rest > half || (rest == half && (sticky || (kept & 1))))
            kept++;
    }
    if (kept >= (1ull << 53)) {
        kept >>= 1;
        low++;
    }
    ULONGLONG out;
    if (kept >= (1ull << 52)) {
        if (low + 1075 >= 0x7FF)
            return dbl_infinity();
        out = ((ULONGLONG)(low + 1075) << 52) | (kept & 0xFFFFFFFFFFFFFull);
    } else {
        out = kept;
    }
    double r;
    memcpy(&r, &out, 8);
    return r;
}

double AutDigitsToDouble(const BYTE *digits, int count, int point)
{
    ULONGLONG small = 0;
    int used = 0, first = 0;
    BOOL sticky = FALSE;
    BIG num;
    big_set(&num, 0);
    while (first < count && !digits[first])
        first++;
    for (int i = first; i < count; i++) {
        if (used < 30) {
            if (used < 19)
                small = small * 10 + digits[i];
            big_mul_add(&num, 10, digits[i]);
            if (!num.Count && digits[i])
                big_set(&num, digits[i]);
            used++;
        } else if (digits[i]) {
            sticky = TRUE;
        }
    }
    if (!used || (!small && !sticky && !num.Count))
        return 0.0;
    /* the value is the digits times ten to the scale; its magnitude is ten to the (point - first). */
    int scale = point - first - used;
    int magnitude = point - first;
    if (magnitude > 310)
        return dbl_infinity();
    if (magnitude < -330)
        return 0.0;
    if (!sticky && used <= 19 && small < (1ull << 53)) {
        double v = (double)(LONGLONG)small;
        if (scale == 0)
            return v;
        if (scale > 0 && scale <= 22)
            return v * g_exact_pow10[scale];
        if (scale < 0 && scale >= -22)
            return v / g_exact_pow10[-scale];
    }
    if (scale >= 0) {
        BIG p, r;
        big_pow10(&p, scale);
        big_mul(&r, &num, &p);
        int shift;
        BOOL rest;
        ULONGLONG top = big_top64(&r, &shift, &rest);
        return make_double(top, shift, rest || sticky);
    }
    BIG den, shifted, q_den;
    big_pow10(&den, -scale);
    int dbits = big_bits(&den), nbits = big_bits(&num);
    /* a quotient of 62 or 63 bits: shift the numerator up, or the denominator when the numerator is the larger. */
    int k = dbits - nbits + 62;
    shifted = num;
    if (k >= 0) {
        big_shl(&shifted, k);
    } else {
        big_shl(&den, -k);
    }
    ULONGLONG q = 0;
    for (int i = 62; i >= 0; i--) {
        q_den = den;
        big_shl(&q_den, i);
        if (big_cmp(&shifted, &q_den) >= 0) {
            big_sub(&shifted, &q_den);
            q |= 1ull << i;
        }
    }
    return make_double(q, -k, shifted.Count != 0 || sticky);
}
