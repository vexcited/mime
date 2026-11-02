/* exact conversions between doubles and decimal text: the shortest text that reads back as the same number, and
 * decimal text read as the nearest double. both use integers of a few thousand bits. */
#include <xmldom/xmldom.h>

#define BIG_WORDS 80

typedef struct Big {
    UINT32 w[BIG_WORDS];
    int n; /* words in use */
} Big;

static void big_trim(Big *b)
{
    while (b->n && !b->w[b->n - 1])
        b->n--;
}

static void big_u64(Big *b, UINT64 v)
{
    b->n = 0;
    while (v) {
        b->w[b->n++] = (UINT32)v;
        v >>= 32;
    }
}

static void big_mul(Big *b, UINT32 m)
{
    UINT64 carry = 0;
    for (int i = 0; i < b->n; i++) {
        UINT64 t = (UINT64)b->w[i] * m + carry;
        b->w[i] = (UINT32)t;
        carry = t >> 32;
    }
    if (carry && b->n < BIG_WORDS)
        b->w[b->n++] = (UINT32)carry;
}

static void big_add_small(Big *b, UINT32 v)
{
    UINT64 carry = v;
    for (int i = 0; i < b->n && carry; i++) {
        UINT64 t = (UINT64)b->w[i] + carry;
        b->w[i] = (UINT32)t;
        carry = t >> 32;
    }
    if (carry && b->n < BIG_WORDS)
        b->w[b->n++] = (UINT32)carry;
}

static void big_shl(Big *b, int bits)
{
    if (!b->n || bits <= 0)
        return;
    int words = bits / 32, rem = bits % 32;
    if (b->n + words + 1 > BIG_WORDS)
        return;
    if (rem) {
        UINT32 carry = 0;
        for (int i = 0; i < b->n; i++) {
            UINT32 v = b->w[i];
            b->w[i] = (v << rem) | carry;
            carry = v >> (32 - rem);
        }
        if (carry)
            b->w[b->n++] = carry;
    }
    if (words) {
        memmove(b->w + words, b->w, (SIZE_T)b->n * 4);
        memset(b->w, 0, (SIZE_T)words * 4);
        b->n += words;
    }
}

static int big_cmp(const Big *a, const Big *b)
{
    if (a->n != b->n)
        return a->n < b->n ? -1 : 1;
    for (int i = a->n - 1; i >= 0; i--)
        if (a->w[i] != b->w[i])
            return a->w[i] < b->w[i] ? -1 : 1;
    return 0;
}

/* a -= b, a >= b. */
static void big_sub(Big *a, const Big *b)
{
    INT64 borrow = 0;
    for (int i = 0; i < a->n; i++) {
        INT64 t = (INT64)a->w[i] - (i < b->n ? b->w[i] : 0) - borrow;
        borrow = t < 0;
        a->w[i] = (UINT32)(t + (borrow ? 0x100000000ll : 0));
    }
    big_trim(a);
}

static void big_add(Big *a, const Big *b)
{
    UINT64 carry = 0;
    int n = a->n > b->n ? a->n : b->n;
    for (int i = 0; i < n; i++) {
        UINT64 t = (UINT64)(i < a->n ? a->w[i] : 0) + (i < b->n ? b->w[i] : 0) + carry;
        a->w[i] = (UINT32)t;
        carry = t >> 32;
    }
    a->n = n;
    if (carry && a->n < BIG_WORDS)
        a->w[a->n++] = (UINT32)carry;
}

static void big_pow10(Big *b, int e)
{
    while (e >= 9) {
        big_mul(b, 1000000000u);
        e -= 9;
    }
    UINT32 p = 1;
    while (e-- > 0)
        p *= 10;
    if (p > 1)
        big_mul(b, p);
}

static int bit_length(UINT64 v)
{
    int n = 0;
    while (v) {
        n++;
        v >>= 1;
    }
    return n;
}

/* ------------------------------------------------------------------ shortest text of a double */

/* digits (without the point) and the power such that value = 0.digits * 10^point. value is positive and finite. */
static int shortest_digits(double value, WCHAR *digits, int *point)
{
    UINT64 bits;
    memcpy(&bits, &value, 8);
    int bexp = (int)((bits >> 52) & 0x7FF);
    UINT64 f = bits & 0xFFFFFFFFFFFFFull;
    int e;
    if (bexp) {
        f |= 1ull << 52;
        e = bexp - 1075;
    } else {
        e = -1074;
    }
    BOOL even = (f & 1) == 0;
    Big r, s, mp, mm;
    BOOL boundary = f == (1ull << 52) && bexp > 1;
    if (e >= 0) {
        big_u64(&r, f);
        big_shl(&r, e + 1 + (boundary ? 1 : 0));
        big_u64(&s, boundary ? 4 : 2);
        big_u64(&mm, 1);
        big_shl(&mm, e + (boundary ? 0 : 0));
        big_u64(&mp, 1);
        big_shl(&mp, e + (boundary ? 1 : 0));
    } else {
        big_u64(&r, f);
        big_shl(&r, 1 + (boundary ? 1 : 0));
        big_u64(&s, 1);
        big_shl(&s, -e + 1 + (boundary ? 1 : 0));
        big_u64(&mm, 1);
        big_u64(&mp, boundary ? 2 : 1);
    }
    /* estimate the power of ten of the first digit. */
    int len = bit_length(f);
    double est = (double)(e + len - 1) * 0.30102999566398114 - 1e-10;
    int k = (int)est;
    if ((double)k < est)
        k++;
    if (k >= 0) {
        big_pow10(&s, k);
    } else {
        big_pow10(&r, -k);
        big_pow10(&mp, -k);
        big_pow10(&mm, -k);
    }
    /* fix the estimate: r + m+ must be below s (or equal when the mantissa is odd). */
    Big t = r;
    big_add(&t, &mp);
    int c = big_cmp(&t, &s);
    /* when the estimate was one too low the first digit is read without scaling by ten. */
    BOOL scale_now = TRUE;
    if (c > 0 || (c == 0 && even)) {
        k++;
        scale_now = FALSE;
    }
    int n = 0;
    for (;;) {
        if (scale_now) {
            big_mul(&r, 10);
            big_mul(&mp, 10);
            big_mul(&mm, 10);
        }
        scale_now = TRUE;
        int d = 0;
        while (big_cmp(&r, &s) >= 0) {
            big_sub(&r, &s);
            d++;
        }
        int c1 = big_cmp(&r, &mm);
        BOOL tc1 = even ? c1 <= 0 : c1 < 0;
        t = r;
        big_add(&t, &mp);
        int c2 = big_cmp(&t, &s);
        BOOL tc2 = even ? c2 >= 0 : c2 > 0;
        if (!tc1 && !tc2) {
            digits[n++] = (WCHAR)('0' + d);
            if (n > 24)
                break;
            continue;
        }
        if (tc1 && !tc2) {
            digits[n++] = (WCHAR)('0' + d);
        } else if (!tc1 && tc2) {
            digits[n++] = (WCHAR)('0' + d + 1);
        } else {
            Big twice = r;
            big_shl(&twice, 1);
            digits[n++] = (WCHAR)('0' + (big_cmp(&twice, &s) < 0 ? d : d + 1));
        }
        break;
    }
    /* a carry to ten is not possible by the construction, but keep the digits valid. */
    for (int i = n - 1; i > 0; i--)
        if (digits[i] > '9') {
            digits[i] = '0';
            digits[i - 1]++;
        }
    if (digits[0] > '9') {
        digits[0] = '1';
        for (int i = 1; i < n; i++)
            digits[i] = '0';
        k++;
    }
    *point = k;
    return n;
}

void xd_format_double(double value, XBuf *out)
{
    if (xd_isnan(value)) {
        xb_puta(out, "NaN");
        return;
    }
    if (value == 0) {
        xb_putc(out, '0');
        return;
    }
    if (value > 1.7976931348623157e308) {
        xb_puta(out, "Infinity");
        return;
    }
    if (value < -1.7976931348623157e308) {
        xb_puta(out, "-Infinity");
        return;
    }
    if (value < 0) {
        xb_putc(out, '-');
        value = -value;
    }
    WCHAR digits[40];
    int point;
    int n = shortest_digits(value, digits, &point);
    if (point <= 0) {
        xb_puta(out, "0.");
        for (int i = 0; i < -point; i++)
            xb_putc(out, '0');
        xb_put(out, digits, (UINT)n);
    } else if (point >= n) {
        xb_put(out, digits, (UINT)n);
        for (int i = 0; i < point - n; i++)
            xb_putc(out, '0');
    } else {
        xb_put(out, digits, (UINT)point);
        xb_putc(out, '.');
        xb_put(out, digits + point, (UINT)(n - point));
    }
}

/* ------------------------------------------------------------------ the nearest double of decimal text */

/* the double whose bits are m * 2^q, m below 2^53 or the boundary of the next binade. */
static double infinity_value(void)
{
    UINT64 bits = 0x7FF0000000000000ull;
    double d;
    memcpy(&d, &bits, 8);
    return d;
}

static double make_double(UINT64 m, int q)
{
    if (m == 0)
        return 0;
    /* normalize to 53 bits. */
    while (m < (1ull << 52) && q > -1074) {
        m <<= 1;
        q--;
    }
    while (m >= (1ull << 53)) {
        m >>= 1;
        q++;
    }
    UINT64 bits;
    if (q > 971)
        return infinity_value();
    if (m < (1ull << 52)) {
        bits = m; /* denormal, q is -1074 */
    } else {
        bits = ((UINT64)(q + 1075) << 52) | (m & 0xFFFFFFFFFFFFFull);
    }
    double d;
    memcpy(&d, &bits, 8);
    return d;
}

static void split_double(double d, UINT64 *m, int *q)
{
    UINT64 bits;
    memcpy(&bits, &d, 8);
    int bexp = (int)((bits >> 52) & 0x7FF);
    UINT64 f = bits & 0xFFFFFFFFFFFFFull;
    if (bexp) {
        *m = f | (1ull << 52);
        *q = bexp - 1075;
    } else {
        *m = f;
        *q = -1074;
    }
}

/* compares M*10^e with the exact value (a * 2^b). */
static int compare_decimal(const Big *M, int e, UINT64 a, int b)
{
    Big l = *M, r;
    big_u64(&r, a);
    if (e >= 0)
        big_pow10(&l, e);
    else
        big_pow10(&r, -e);
    if (b >= 0)
        big_shl(&r, b);
    else
        big_shl(&l, -b);
    return big_cmp(&l, &r);
}

static double pow10_double(int e)
{
    double r = 1, base = 10;
    if (e < 0)
        e = -e;
    while (e) {
        if (e & 1)
            r *= base;
        base *= base;
        e >>= 1;
    }
    return r;
}

double xd_decimal_to_double(const WCHAR *digits, UINT count, int exp10)
{
    /* the leading zeros do not count and a long tail is only a sticky digit. */
    UINT start = 0;
    while (start < count && digits[start] == '0')
        start++;
    if (start == count)
        return 0;
    Big M;
    big_u64(&M, 0);
    UINT used = 0;
    BOOL sticky = FALSE;
    for (UINT i = start; i < count; i++) {
        if (used >= 60) {
            if (digits[i] != '0')
                sticky = TRUE;
            exp10++;
            continue;
        }
        big_mul(&M, 10);
        big_add_small(&M, (UINT32)(digits[i] - '0'));
        used++;
    }
    if (sticky) {
        big_mul(&M, 10);
        big_add_small(&M, 1);
        exp10--;
    }
    /* the fast path: both are exact doubles. */
    if (M.n <= 2 && !sticky) {
        UINT64 v = M.n ? M.w[0] : 0;
        if (M.n == 2)
            v |= (UINT64)M.w[1] << 32;
        if (v < (1ull << 53) && exp10 >= -22 && exp10 <= 22)
            return exp10 < 0 ? (double)v / pow10_double(-exp10) : (double)v * pow10_double(exp10);
    }
    int total_digits = (int)used + exp10 + (sticky ? 1 : 0);
    if (total_digits > 310)
        return infinity_value();
    if (total_digits < -330)
        return 0;
    /* an estimate from the top digits. */
    double approx = 0;
    UINT lead = used < 17 ? used : 17;
    for (UINT i = 0; i < lead; i++)
        approx = approx * 10 + (digits[start + i] - '0');
    int e2 = exp10 + (int)(used - lead);
    double z;
    if (e2 > 0) {
        z = approx;
        int e = e2;
        while (e > 0) {
            int step = e > 300 ? 300 : e;
            z *= pow10_double(step);
            e -= step;
            if (z > 1.7976931348623157e308)
                break;
        }
    } else {
        z = approx;
        int e = -e2;
        while (e > 0) {
            int step = e > 300 ? 300 : e;
            z /= pow10_double(step);
            e -= step;
        }
    }
    if (z > 1.7976931348623157e308)
        z = 1.7976931348623157e308;
    UINT64 m;
    int q;
    split_double(z, &m, &q);
    for (int guard = 0; guard < 6; guard++) {
        /* the midpoints either side of the double. */
        BOOL low_boundary = m == (1ull << 52) && q > -1074;
        int up = compare_decimal(&M, exp10, 2 * m + 1, q - 1);
        if (up > 0 || (up == 0 && (m & 1))) {
            m++;
            if (m == (1ull << 53)) {
                m >>= 1;
                q++;
            }
            if (q > 971)
                return infinity_value();
            continue;
        }
        int down = low_boundary ? compare_decimal(&M, exp10, 4 * m - 1, q - 2) : compare_decimal(&M, exp10, 2 * m - 1, q - 1);
        if (down < 0 || (down == 0 && (m & 1))) {
            if (low_boundary) {
                m = (1ull << 53) - 1;
                q--;
            } else if (m) {
                m--;
            }
            continue;
        }
        break;
    }
    return make_double(m, q);
}
