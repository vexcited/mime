/*
 * text built up piece by piece, for disassembly and error messages: a
 * growing buffer and a printf of its own, which formats doubles exactly
 * as the c runtime's %f does, rounding the exact binary value.
 */
#include "d3dcompiler_private.h"

void DcTextAppend(DcText *t, const char *s, SIZE_T n)
{
    if (t->failed)
        return;
    if (t->length + n + 1 > t->capacity) {
        SIZE_T capacity = t->capacity ? t->capacity * 2 : 1024;
        while (capacity < t->length + n + 1)
            capacity *= 2;
        char *buf = DcAlloc(capacity);
        if (!buf) {
            t->failed = TRUE;
            return;
        }
        if (t->buf) {
            memcpy(buf, t->buf, t->length);
            DcFree(t->buf);
        }
        t->buf = buf;
        t->capacity = capacity;
    }
    memcpy(t->buf + t->length, s, n);
    t->length += n;
    t->buf[t->length] = 0;
}

void DcTextPuts(DcText *t, const char *s) { DcTextAppend(t, s, strlen(s)); }

void DcTextFree(DcText *t)
{
    DcFree(t->buf);
    t->buf = NULL;
    t->length = t->capacity = 0;
}

UINT DcTextColumn(const DcText *t)
{
    SIZE_T i = t->length;
    while (i && t->buf[i - 1] != '\n')
        i--;
    return (UINT)(t->length - i);
}

void DcTextAlign(DcText *t, UINT column)
{
    for (UINT at = DcTextColumn(t); at < column; at++)
        DcTextAppend(t, " ", 1);
}

/* ------------------------------------------------------------------ exact doubles */

#define BIG_WORDS 48

typedef struct Big {
    UINT32 w[BIG_WORDS];
    int n;
} Big;

static void big_set(Big *b, UINT64 v)
{
    b->w[0] = (UINT32)v;
    b->w[1] = (UINT32)(v >> 32);
    b->n = b->w[1] ? 2 : b->w[0] ? 1 : 0;
}

static void big_shl(Big *b, int bits)
{
    Big r;
    memset(&r, 0, sizeof(r));
    int words = bits / 32, rest = bits % 32;
    for (int i = 0; i < b->n; i++) {
        UINT64 v = (UINT64)b->w[i] << rest;
        if (i + words < BIG_WORDS)
            r.w[i + words] |= (UINT32)v;
        if (i + words + 1 < BIG_WORDS)
            r.w[i + words + 1] |= (UINT32)(v >> 32);
    }
    r.n = b->n + words + 1 < BIG_WORDS ? b->n + words + 1 : BIG_WORDS;
    while (r.n && !r.w[r.n - 1])
        r.n--;
    *b = r;
}

static void big_mul_small(Big *b, UINT32 m)
{
    UINT64 carry = 0;
    for (int i = 0; i < b->n; i++) {
        UINT64 v = (UINT64)b->w[i] * m + carry;
        b->w[i] = (UINT32)v;
        carry = v >> 32;
    }
    if (carry && b->n < BIG_WORDS)
        b->w[b->n++] = (UINT32)carry;
}

static UINT32 big_div_small(Big *b, UINT32 d)
{
    UINT64 rem = 0;
    for (int i = b->n - 1; i >= 0; i--) {
        UINT64 v = rem << 32 | b->w[i];
        b->w[i] = (UINT32)(v / d);
        rem = v % d;
    }
    while (b->n && !b->w[b->n - 1])
        b->n--;
    return (UINT32)rem;
}

static void big_add_small(Big *b, UINT32 v)
{
    for (int i = 0; v && i < BIG_WORDS; i++) {
        if (i == b->n)
            b->w[b->n++] = 0;
        UINT64 s = (UINT64)b->w[i] + v;
        b->w[i] = (UINT32)s;
        v = (UINT32)(s >> 32);
    }
}

/* the decimal digits of the number, most significant first. */
static int big_digits(Big b, char *out, int size)
{
    char tmp[400];
    int n = 0;
    if (!b.n)
        tmp[n++] = '0';
    while (b.n && n < (int)sizeof(tmp) - 9) {
        UINT32 r = big_div_small(&b, 1000000000);
        for (int i = 0; i < 9; i++) {
            tmp[n++] = (char)('0' + r % 10);
            r /= 10;
            if (!b.n && !r)
                break;
        }
    }
    int count = 0;
    while (n && count < size - 1)
        out[count++] = tmp[--n];
    out[count] = 0;
    return count;
}

/* the bits of the fraction below bit k, kept apart from the integer part. */
static void big_low_bits(Big *b, int k)
{
    int words = k / 32, rest = k % 32;
    if (words < b->n) {
        if (rest)
            b->w[words] &= (1u << rest) - 1;
        else
            b->w[words] = 0;
        for (int i = words + 1; i < b->n; i++)
            b->w[i] = 0;
    }
    while (b->n && !b->w[b->n - 1])
        b->n--;
}

static int big_bit(const Big *b, int k) { return k / 32 < b->n ? (b->w[k / 32] >> (k % 32)) & 1 : 0; }

/* compares the low k bits of the number with half of 2^k. */
static int big_compare_half(const Big *b, int k)
{
    if (!big_bit(b, k - 1))
        return -1;
    for (int i = 0; i < k - 1; i++)
        if (big_bit(b, i))
            return 1;
    return 0;
}

static void big_shr(Big *b, int bits)
{
    int words = bits / 32, rest = bits % 32;
    for (int i = 0; i < b->n; i++) {
        UINT64 v = i + words < b->n ? b->w[i + words] : 0;
        UINT64 hi = i + words + 1 < b->n ? b->w[i + words + 1] : 0;
        b->w[i] = (UINT32)((v | hi << 32) >> rest);
    }
    b->n = b->n > words ? b->n - words : 0;
    while (b->n && !b->w[b->n - 1])
        b->n--;
}

/* %f of the double with the precision, as text without the sign. */
static int format_fixed(double value, int precision, char *out, int size)
{
    UINT64 bits;
    memcpy(&bits, &value, 8);
    int exponent = (int)((bits >> 52) & 0x7FF);
    UINT64 mantissa = bits & ((1ull << 52) - 1);
    if (exponent == 0x7FF) {
        const char *s = mantissa ? "nan" : "inf";
        int n = 0;
        while (s[n] && n < size - 1)
            out[n] = s[n], n++;
        out[n] = 0;
        return n;
    }
    if (exponent)
        mantissa |= 1ull << 52;
    else
        exponent = 1;
    int e = exponent - 1075;
    Big integer, fraction;
    big_set(&integer, mantissa);
    int k = 0;
    if (e >= 0) {
        big_shl(&integer, e);
        big_set(&fraction, 0);
    } else {
        k = -e;
        fraction = integer;
        big_low_bits(&fraction, k);
        big_shr(&integer, k);
    }
    /* the digits of the fraction, rounded half to even on the exact value. */
    Big digits;
    big_set(&digits, 0);
    if (k) {
        Big scaled = fraction;
        for (int i = 0; i < precision; i++)
            big_mul_small(&scaled, 10);
        int round = big_compare_half(&scaled, k);
        digits = scaled;
        big_shr(&digits, k);
        if (round > 0 || (round == 0 && digits.n && (digits.w[0] & 1)))
            big_add_small(&digits, 1);
        Big limit;
        big_set(&limit, 1);
        for (int i = 0; i < precision; i++)
            big_mul_small(&limit, 10);
        int carry = digits.n == limit.n;
        for (int i = 0; carry && i < digits.n; i++)
            carry = digits.w[i] == limit.w[i];
        if (carry) {
            big_set(&digits, 0);
            big_add_small(&integer, 1);
        }
    }
    int n = big_digits(integer, out, size);
    if (precision > 0 && n < size - 2) {
        out[n++] = '.';
        char frac[64];
        int count = big_digits(digits, frac, sizeof(frac));
        for (int i = count; i < precision && n < size - 1; i++)
            out[n++] = '0';
        for (int i = 0; i < count && n < size - 1; i++)
            out[n++] = frac[i];
        out[n] = 0;
    }
    return n;
}

/* ------------------------------------------------------------------ printf */

static void pad(DcText *t, int n, char c)
{
    while (n-- > 0)
        DcTextAppend(t, &c, 1);
}

static void put_field(DcText *t, const char *s, int n, int width, BOOL left, char fill, BOOL negative)
{
    int total = n + (negative ? 1 : 0);
    if (!left && fill == ' ')
        pad(t, width - total, ' ');
    if (negative)
        DcTextAppend(t, "-", 1);
    if (!left && fill == '0')
        pad(t, width - total, '0');
    DcTextAppend(t, s, n);
    if (left)
        pad(t, width - total, ' ');
}

void DcTextVprintf(DcText *t, const char *format, va_list ap)
{
    for (const char *p = format; *p; p++) {
        if (*p != '%') {
            const char *start = p;
            while (p[1] && p[1] != '%')
                p++;
            DcTextAppend(t, start, p - start + 1);
            continue;
        }
        p++;
        BOOL left = FALSE;
        char fill = ' ';
        for (;; p++) {
            if (*p == '-')
                left = TRUE;
            else if (*p == '0')
                fill = '0';
            else
                break;
        }
        int width = 0, precision = -1;
        if (*p == '*') {
            width = va_arg(ap, int);
            p++;
        } else {
            while (*p >= '0' && *p <= '9')
                width = width * 10 + (*p++ - '0');
        }
        if (*p == '.') {
            p++;
            precision = 0;
            while (*p >= '0' && *p <= '9')
                precision = precision * 10 + (*p++ - '0');
        }
        BOOL wide = FALSE;
        if (p[0] == 'I' && p[1] == '6' && p[2] == '4') {
            wide = TRUE;
            p += 3;
        } else if (*p == 'l' && p[1] == 'l') {
            wide = TRUE;
            p += 2;
        } else if (*p == 'l') {
            p++;
        }
        char buf[400];
        int n = 0;
        switch (*p) {
        case '%':
            DcTextAppend(t, "%", 1);
            break;
        case 'c': {
            char c = (char)va_arg(ap, int);
            put_field(t, &c, 1, width, left, ' ', FALSE);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            n = (int)strlen(s);
            if (precision >= 0 && precision < n)
                n = precision;
            put_field(t, s, n, width, left, ' ', FALSE);
            break;
        }
        case 'd':
        case 'i': {
            INT64 v = wide ? va_arg(ap, INT64) : va_arg(ap, int);
            UINT64 u = v < 0 ? (UINT64)0 - (UINT64)v : (UINT64)v;
            char tmp[24];
            do
                tmp[n++] = (char)('0' + u % 10);
            while (u /= 10);
            for (int i = 0; i < n; i++)
                buf[i] = tmp[n - 1 - i];
            put_field(t, buf, n, width, left, fill, v < 0);
            break;
        }
        case 'u':
        case 'x':
        case 'X':
        case 'p': {
            UINT64 u;
            int base = *p == 'u' ? 10 : 16;
            const char *digits = *p == 'x' ? "0123456789abcdef" : "0123456789ABCDEF";
            if (*p == 'p') {
                u = (UINT_PTR)va_arg(ap, void *);
                width = sizeof(void *) * 2;
                fill = '0';
            } else {
                u = wide ? va_arg(ap, UINT64) : va_arg(ap, unsigned int);
            }
            char tmp[24];
            do
                tmp[n++] = digits[u % base];
            while (u /= base);
            for (int i = 0; i < n; i++)
                buf[i] = tmp[n - 1 - i];
            put_field(t, buf, n, width, left, fill, FALSE);
            break;
        }
        case 'f': {
            double v = va_arg(ap, double);
            UINT64 bits;
            memcpy(&bits, &v, 8);
            n = format_fixed(v, precision < 0 ? 6 : precision, buf, sizeof(buf));
            put_field(t, buf, n, width, left, fill, (bits >> 63) != 0);
            break;
        }
        default:
            p--;
            break;
        }
    }
}

void DcTextPrintf(DcText *t, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    DcTextVprintf(t, format, ap);
    va_end(ap);
}
