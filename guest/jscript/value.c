/*
 * strings, numbers and the conversions between the values of the language.
 */
#include "js.h"


/* the not a number the processor makes has its sign bit set, the one of the constant NaN does not. */
const double js_nan = -__builtin_nan("");
const double js_pnan = __builtin_nan("");

BOOL is_nan(double d)
{
    return d != d;
}

BOOL is_inf(double d)
{
    return d == __builtin_inf() || d == -__builtin_inf();
}

/* ------------------------------------------------------------------ strings */

Str *str_alloc(Engine *e, SIZE_T len)
{
    Str *s = gc_alloc(e, sizeof(Str) + len * sizeof(WCHAR), GC_STR);
    if (s)
        s->len = (ULONG)len;
    return s;
}

Str *str_new(Engine *e, const WCHAR *text, SIZE_T len)
{
    Str *s = str_alloc(e, len);
    if (s && len)
        memcpy(s->s, text, len * sizeof(WCHAR));
    return s;
}

Str *str_from_z(Engine *e, const WCHAR *z)
{
    return str_new(e, z, wcslen(z));
}

Str *str_from_ascii(Engine *e, const char *z)
{
    SIZE_T n = strlen(z);
    Str *s = str_alloc(e, n);
    for (SIZE_T i = 0; i < n; i++)
        s->s[i] = (BYTE)z[i];
    return s;
}

Str *str_cat(Engine *e, Str *a, Str *b)
{
    if (!a->len)
        return b;
    if (!b->len)
        return a;
    Str *s = str_alloc(e, (SIZE_T)a->len + b->len);
    memcpy(s->s, a->s, a->len * sizeof(WCHAR));
    memcpy(s->s + a->len, b->s, b->len * sizeof(WCHAR));
    return s;
}

ULONG str_hash(Str *s)
{
    if (s->hash)
        return s->hash;
    ULONG h = 2166136261u;
    for (ULONG i = 0; i < s->len; i++)
        h = (h ^ s->s[i]) * 16777619u;
    s->hash = h | 0x80000000u;
    return s->hash;
}

BOOL str_equal(Str *a, Str *b)
{
    if (a == b)
        return TRUE;
    if (a->len != b->len)
        return FALSE;
    if (a->hash && b->hash && a->hash != b->hash)
        return FALSE;
    return !memcmp(a->s, b->s, a->len * sizeof(WCHAR));
}

int str_compare(Str *a, Str *b)
{
    ULONG n = a->len < b->len ? a->len : b->len;
    for (ULONG i = 0; i < n; i++)
        if (a->s[i] != b->s[i])
            return a->s[i] < b->s[i] ? -1 : 1;
    return a->len < b->len ? -1 : a->len > b->len;
}

Str *str_empty(Engine *e)
{
    return str_intern(e, L"", 0);
}

/* the names the engine keeps: one block per distinct text, found by pointer afterwards. */
Str *str_intern(Engine *e, const WCHAR *text, SIZE_T len)
{
    ULONG hash = 2166136261u;
    for (SIZE_T i = 0; i < len; i++)
        hash = (hash ^ text[i]) * 16777619u;
    hash |= 0x80000000u;
    if (e->InternCount * 2 >= e->InternCap) {
        ULONG cap = e->InternCap ? e->InternCap * 2 : 1024;
        Str **table = js_alloc(cap * sizeof(Str *));
        for (ULONG i = 0; i < e->InternCap; i++) {
            Str *s = e->Interned[i];
            if (!s)
                continue;
            ULONG k = s->hash & (cap - 1);
            while (table[k])
                k = (k + 1) & (cap - 1);
            table[k] = s;
        }
        js_free(e->Interned);
        e->Interned = table;
        e->InternCap = cap;
    }
    ULONG k = hash & (e->InternCap - 1);
    while (e->Interned[k]) {
        Str *s = e->Interned[k];
        if (s->hash == hash && s->len == len && !memcmp(s->s, text, len * sizeof(WCHAR)))
            return s;
        k = (k + 1) & (e->InternCap - 1);
    }
    BOOL saved = e->NoGc;
    e->NoGc = TRUE;
    Str *s = str_new(e, text, len);
    e->NoGc = saved;
    s->hash = hash;
    e->Interned[k] = s;
    e->InternCount++;
    return s;
}

Str *str_intern_str(Engine *e, Str *s)
{
    if (s->hash) {
        ULONG k = s->hash & (e->InternCap ? e->InternCap - 1 : 0);
        if (e->InternCap && e->Interned[k] == s)
            return s;
    }
    return str_intern(e, s->s, s->len);
}

Str *str_intern_z(Engine *e, const WCHAR *z)
{
    return str_intern(e, z, wcslen(z));
}

Str *str_intern_ascii(Engine *e, const char *z)
{
    WCHAR buffer[128];
    SIZE_T n = strlen(z);
    if (n > 127)
        n = 127;
    for (SIZE_T i = 0; i < n; i++)
        buffer[i] = (BYTE)z[i];
    return str_intern(e, buffer, n);
}

/* an array index is the canonical text of an integer below 2^32 - 1. */
BOOL str_to_index(Str *s, ULONG *index)
{
    if (!s->len || s->len > 10)
        return FALSE;
    if (s->s[0] == L'0')
        return s->len == 1 ? (*index = 0, TRUE) : FALSE;
    unsigned long long v = 0;
    for (ULONG i = 0; i < s->len; i++) {
        WCHAR c = s->s[i];
        if (c < L'0' || c > L'9')
            return FALSE;
        v = v * 10 + (c - L'0');
    }
    if (v >= 0xFFFFFFFFull)
        return FALSE;
    *index = (ULONG)v;
    return TRUE;
}

Str *index_to_str(Engine *e, ULONG index)
{
    WCHAR buffer[12];
    int n = 0;
    do {
        buffer[n++] = (WCHAR)(L'0' + index % 10);
        index /= 10;
    } while (index);
    Str *s = str_alloc(e, n);
    for (int i = 0; i < n; i++)
        s->s[i] = buffer[n - 1 - i];
    return s;
}

/* ------------------------------------------------------------------ numbers */

int atoi_local(const char *s)
{
    int sign = 1, v = 0;
    if (*s == '-')
        sign = -1, s++;
    else if (*s == '+')
        s++;
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (*s++ - '0');
    return sign * v;
}

/* ------------------------------------------------------------------ the digits of a double */

#define LIMBS 100

typedef struct Big {
    ULONG w[LIMBS];
    int n;
} Big;

static void big_mul(Big *b, ULONG m)
{
    unsigned long long carry = 0;
    for (int i = 0; i < b->n; i++) {
        unsigned long long v = (unsigned long long)b->w[i] * m + carry;
        b->w[i] = (ULONG)(v % 1000000000u);
        carry = v / 1000000000u;
    }
    while (carry && b->n < LIMBS) {
        b->w[b->n++] = (ULONG)(carry % 1000000000u);
        carry /= 1000000000u;
    }
}

/* the exact decimal digits of m times two to the e, the exponent is that of the first digit. */
static int exact_digits_me(unsigned long long m, int e, char *out, int cap)
{
    Big b = {};
    while (m) {
        b.w[b.n++] = (ULONG)(m % 1000000000u);
        m /= 1000000000u;
    }
    int k = 0;
    if (e > 0) {
        for (int i = e; i > 0;) {
            int step = i > 29 ? 29 : i;
            big_mul(&b, 1u << step);
            i -= step;
        }
    } else {
        k = -e;
        for (int i = k; i > 0;) {
            int step = i > 13 ? 13 : i;
            ULONG f = 1;
            for (int j = 0; j < step; j++)
                f *= 5;
            big_mul(&b, f);
            i -= step;
        }
    }
    char text[LIMBS * 9 + 1];
    int len = 0;
    for (int i = b.n - 1; i >= 0; i--) {
        char piece[10];
        int pl = 0;
        ULONG v = b.w[i];
        for (int j = 0; j < 9; j++) {
            piece[pl++] = (char)('0' + v % 10);
            v /= 10;
        }
        if (i == b.n - 1)
            while (pl > 1 && piece[pl - 1] == '0')
                pl--;
        while (pl)
            text[len++] = piece[--pl];
    }
    int n = len < cap - 1 ? len : cap - 1;
    memcpy(out, text, n);
    out[n] = 0;
    return len - 1 - k;
}

static void unpack_double(double x, unsigned long long *m, int *e, BOOL *narrow)
{
    unsigned long long bits;
    memcpy(&bits, &x, 8);
    int be = (int)((bits >> 52) & 0x7FF);
    *m = bits & 0xFFFFFFFFFFFFFull;
    *narrow = FALSE;
    if (be) {
        *m |= 1ull << 52;
        *e = be - 1075;
        *narrow = (bits & 0xFFFFFFFFFFFFFull) == 0 && be > 1;
    } else
        *e = -1074;
}

static int exact_digits(double x, char *out, int cap)
{
    unsigned long long m;
    int e;
    BOOL narrow;
    unpack_double(x, &m, &e, &narrow);
    return exact_digits_me(m, e, out, cap);
}

static BOOL digits_roundtrip(const char *d, int exp10, double x)
{
    char t[64];
    int o = 0;
    t[o++] = d[0];
    if (d[1]) {
        t[o++] = '.';
        for (int i = 1; d[i]; i++)
            t[o++] = d[i];
    }
    t[o++] = 'e';
    int v = exp10;
    if (v < 0) {
        t[o++] = '-';
        v = -v;
    }
    char r[8];
    int rn = 0;
    do {
        r[rn++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (rn)
        t[o++] = r[--rn];
    t[o] = 0;
    return strtod(t, nullptr) == x;
}

/* the shortest digits that read back as the number, nearest rounded. */
static int nearest_digits(double x, char *out)
{
    char all[800];
    int E = exact_digits(x, all, sizeof(all));
    for (int p = 1; p <= 17; p++) {
        char t[24];
        int te = E;
        int n = (int)strlen(all);
        for (int i = 0; i < p; i++)
            t[i] = i < n ? all[i] : '0';
        t[p] = 0;
        if (p < n && all[p] >= '5') {
            int i = p - 1;
            while (i >= 0 && t[i] == '9')
                t[i--] = '0';
            if (i >= 0)
                t[i]++;
            else {
                memmove(t + 1, t, p + 1);
                t[0] = '1';
                t[p] = 0;
                te++;
            }
        }
        if (digits_roundtrip(t, te, x) || p == 17) {
            int k = p;
            while (k > 1 && t[k - 1] == '0')
                k--;
            t[k] = 0;
            strcpy(out, t);
            return te;
        }
    }
    return E;
}

/* a string of digits the way a bound a little below a number reads: the last digit that is no zero goes down by one and
   nines follow. */
static void minus_epsilon(const char *s, int len, char *out, int cap)
{
    int j = len - 1;
    while (j >= 0 && s[j] == '0')
        j--;
    for (int i = 0; i < cap - 1; i++) {
        if (j < 0 || i < j)
            out[i] = i < len ? s[i] : '0';
        else if (i == j)
            out[i] = (char)(s[i] - 1);
        else
            out[i] = '9';
    }
    out[cap - 1] = 0;
}

static BOOL all_zero_from(const char *s, int i)
{
    for (; s[i]; i++)
        if (s[i] != '0')
            return FALSE;
    return TRUE;
}

/* the digits of JScript: those that the outer bounds of the number share, then the middle of the first two that differ
   in the inner bounds. the bounds are the number and half a gap each way, moved a hair outward or inward when the digits
   of a scaled number are found with a small error. */
int shortest_digits(double x, char *out)
{
    if (x < 0)
        x = -x;
    if (x < 9007199254740992.0 && x == floor(x)) {
        char t[24];
        int n = 0;
        unsigned long long v = (unsigned long long)x;
        while (v) {
            t[n++] = (char)('0' + v % 10);
            v /= 10;
        }
        int k = 0;
        for (int i = n - 1; i >= 0; i--)
            out[k++] = t[i];
        int e = k - 1;
        while (k > 1 && out[k - 1] == '0')
            k--;
        out[k] = 0;
        return e;
    }
    unsigned long long m;
    int e;
    BOOL narrow;
    unpack_double(x, &m, &e, &narrow);
    char dl[800], dh[800], lpad[820];
    int eh = exact_digits_me(2 * m + 1, e - 1, dh, sizeof(dh));
    int el = narrow ? exact_digits_me(4 * m - 1, e - 2, dl, sizeof(dl)) : exact_digits_me(2 * m - 1, e - 1, dl, sizeof(dl));
    int pad = eh - el;
    if (pad < 0)
        pad = 0;
    for (int i = 0; i < pad; i++)
        lpad[i] = '0';
    strcpy(lpad + pad, dl);
    BOOL scaled = x >= 2147483648.0 || x < 1.0;
    BOOL even = (m & 1) == 0;
    char ol[80], oh[80], il[80], ih[80];
    int lenl = (int)strlen(lpad), lenh = (int)strlen(dh);
    for (int i = 0; i < 79; i++) {
        il[i] = i < lenl ? lpad[i] : '0';
        oh[i] = i < lenh ? dh[i] : '0';
    }
    il[79] = oh[79] = 0;
    if (scaled) {
        minus_epsilon(lpad, lenl, ol, 80);
        minus_epsilon(dh, lenh, ih, 80);
    } else {
        memcpy(ol, il, 80);
        memcpy(ih, oh, 80);
    }
    int n = 0;
    for (int i = 0; i < 60; i++) {
        if (ol[i] == oh[i]) {
            out[n++] = oh[i];
            continue;
        }
        int dlv = il[i] - '0', dhv = ih[i] - '0';
        if (dlv >= dhv)
            return nearest_digits(x, out);
        BOOL tail_l = all_zero_from(il, i + 1), tail_h = all_zero_from(ih, i + 1);
        if (!(dlv != 0 || !tail_l || scaled || !even))
            break;
        if (dhv - dlv > 1) {
            out[n++] = (char)('0' + ((dlv + dhv + 1) >> 1));
            break;
        }
        if (tail_h && !scaled && !even)
            return nearest_digits(x, out);
        out[n++] = ih[i];
        break;
    }
    out[n] = 0;
    int k = n;
    while (k > 1 && out[k - 1] == '0')
        k--;
    out[k] = 0;
    return eh;
}

/* count significant digits of a finite nonzero number rounded as usual. */
int double_digits(double x, int count, char *out)
{
    char all[800];
    if (x < 0)
        x = -x;
    int E = exact_digits(x, all, sizeof(all));
    int n = (int)strlen(all);
    if (count >= n) {
        memcpy(out, all, n);
        for (int i = n; i < count; i++)
            out[i] = '0';
        out[count] = 0;
        return E;
    }
    BOOL up = all[count] >= '5';
    memcpy(out, all, count);
    out[count] = 0;
    if (up) {
        int i = count - 1;
        while (i >= 0 && out[i] == '9')
            out[i--] = '0';
        if (i >= 0)
            out[i]++;
        else {
            memmove(out + 1, out, count);
            out[0] = '1';
            out[count] = 0;
            E++;
        }
    }
    return E;
}

Str *num_to_str(Engine *e, double n)
{
    if (is_nan(n))
        return str_intern_ascii(e, "NaN");
    if (n == 0)
        return str_intern_ascii(e, "0");
    if (n == __builtin_inf())
        return str_intern_ascii(e, "Infinity");
    if (n == -__builtin_inf())
        return str_intern_ascii(e, "-Infinity");
    if (n >= 0 && n < 1e9 && n == (double)(ULONG)n)
        return index_to_str(e, (ULONG)n);
    BOOL neg = n < 0;
    if (neg)
        n = -n;
    char digits[24];
    int k, exp10;
    exp10 = shortest_digits(n, digits);
    k = (int)strlen(digits);
    int point = exp10 + 1;
    char out[64];
    int o = 0;
    if (neg)
        out[o++] = '-';
    if (k <= point && point <= 21) {
        for (int i = 0; i < k; i++)
            out[o++] = digits[i];
        for (int i = k; i < point; i++)
            out[o++] = '0';
    } else if (0 < point && point <= 21) {
        for (int i = 0; i < point; i++)
            out[o++] = digits[i];
        out[o++] = '.';
        for (int i = point; i < k; i++)
            out[o++] = digits[i];
    } else if (-6 < point && point <= 0) {
        out[o++] = '0';
        out[o++] = '.';
        for (int i = point; i < 0; i++)
            out[o++] = '0';
        for (int i = 0; i < k; i++)
            out[o++] = digits[i];
    } else {
        out[o++] = digits[0];
        if (k > 1) {
            out[o++] = '.';
            for (int i = 1; i < k; i++)
                out[o++] = digits[i];
        }
        out[o++] = 'e';
        int x = point - 1;
        out[o++] = x < 0 ? '-' : '+';
        if (x < 0)
            x = -x;
        char t[8];
        int tn = 0;
        do {
            t[tn++] = (char)('0' + x % 10);
            x /= 10;
        } while (x);
        while (tn)
            out[o++] = t[--tn];
    }
    out[o] = 0;
    return str_from_ascii(e, out);
}

Str *num_to_radix(Engine *e, double n, int radix)
{
    if (is_nan(n))
        return str_intern_ascii(e, "NaN");
    if (n == __builtin_inf())
        return str_intern_ascii(e, "Infinity");
    if (n == -__builtin_inf())
        return str_intern_ascii(e, "-Infinity");
    if (n == 0)
        return str_intern_ascii(e, "0");
    BOOL neg = n < 0;
    if (neg)
        n = -n;
    {
        /* far from one, the number is written as a digit, its fraction and the power of the radix. */
        unsigned long long bits;
        memcpy(&bits, &n, 8);
        int bexp = (int)((bits >> 52) & 0x7FF) - 1023;
        if (bexp < -60 || bexp > 60) {
            static const char digit[] = "0123456789abcdefghijklmnopqrstuvwxyz";
            double r = radix;
            int maxdigits = 1;
            for (double v = 1; v < 9007199254740992.0; v *= r)
                maxdigits++;
            int power = (int)floor(log(n) / log(r) + 1.0);
            double f = 1.0, base_ = r;
            unsigned pw = power < 0 ? (unsigned)-power : (unsigned)power;
            for (;;) {
                if (pw & 1)
                    f *= base_;
                pw >>= 1;
                if (!pw)
                    break;
                base_ *= base_;
            }
            double scale = power >= 0 ? f : 1.0 / f;
            double v = n / scale;
            while (v < 1.0) {
                v *= r;
                power--;
            }
            char out[128];
            int o = 0;
            if (neg)
                out[o++] = '-';
            int d = (int)v;
            out[o++] = digit[d < radix ? d : radix - 1];
            double fr = v - (double)d;
            if (fr != 0.0) {
                out[o++] = '.';
                int left = maxdigits - 1;
                while (fr != 0.0 && left-- > 0) {
                    double t = r * fr;
                    int dd = (int)t;
                    out[o++] = digit[dd < radix ? dd : radix - 1];
                    fr = t - (double)dd;
                }
            }
            out[o++] = '(';
            out[o++] = 'e';
            out[o++] = power >= 0 ? '+' : '-';
            int ap = power < 0 ? -power : power;
            char t[8];
            int tn = 0;
            do {
                t[tn++] = (char)('0' + ap % 10);
                ap /= 10;
            } while (ap);
            while (tn)
                out[o++] = t[--tn];
            out[o++] = ')';
            out[o] = 0;
            return str_from_ascii(e, out);
        }
    }
    double ip = floor(n);
    double fp = n - ip;
    char tmp[1200];
    int t = 0;
    static const char digit[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    if (ip == 0)
        tmp[t++] = '0';
    while (ip >= 1 && t < 1100) {
        double q = floor(ip / radix);
        int d = (int)(ip - q * radix);
        tmp[t++] = digit[d];
        ip = q;
    }
    char out[1300];
    int o = 0;
    if (neg)
        out[o++] = '-';
    while (t)
        out[o++] = tmp[--t];
    if (fp > 0) {
        out[o++] = '.';
        int count = 0;
        /* digits until the fraction is gone or the precision of a double is spent. */
        double delta = 0.5 * (__builtin_nextafter(n, __builtin_inf()) - n);
        do {
            fp *= radix;
            delta *= radix;
            int d = (int)fp;
            out[o++] = digit[d];
            fp -= d;
            if (fp > 0.5 || (fp == 0.5 && (d & 1))) {
                if (fp + delta > 1) {
                    /* round up and carry. */
                    int i = o - 1;
                    for (;;) {
                        if (i < 0 || out[i] == '.')
                            break;
                        int v = out[i] > '9' ? out[i] - 'a' + 10 : out[i] - '0';
                        if (v + 1 < radix) {
                            out[i] = digit[v + 1];
                            break;
                        }
                        o = i;
                        i--;
                    }
                    break;
                }
            }
            count++;
        } while (fp >= delta && count < 1000);
    }
    out[o] = 0;
    return str_from_ascii(e, out);
}

BOOL is_js_space(WCHAR c)
{
    return c == 9 || c == 10 || c == 11 || c == 12 || c == 13 || c == 32 || c == 0xA0 || c == 0x2028 || c == 0x2029 ||
           c == 0xFEFF || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x202F || c == 0x205F || c == 0x3000;
}

static int hexval(WCHAR c)
{
    if (c >= L'0' && c <= L'9')
        return c - L'0';
    if (c >= L'a' && c <= L'f')
        return c - L'a' + 10;
    if (c >= L'A' && c <= L'F')
        return c - L'A' + 10;
    return -1;
}

double str_to_num(Str *s)
{
    const WCHAR *p = s->s, *end = s->s + s->len;
    while (p < end && is_js_space(*p))
        p++;
    while (end > p && is_js_space(end[-1]))
        end--;
    if (p == end)
        return 0;
    if (end - p > 2 && p[0] == L'0' && (p[1] == L'x' || p[1] == L'X')) {
        double v = 0;
        for (const WCHAR *c = p + 2; c < end; c++) {
            int d = hexval(*c);
            if (d < 0)
                return js_nan;
            v = v * 16 + d;
        }
        return v;
    }
    const WCHAR *c = p;
    BOOL neg = FALSE;
    if (*c == L'+' || *c == L'-') {
        neg = *c == L'-';
        c++;
    }
    if (end - c == 8 && !wcsncmp(c, L"Infinity", 8))
        return neg ? -__builtin_inf() : __builtin_inf();
    char buffer[400];
    int n = 0;
    const WCHAR *d = c;
    int digits = 0;
    while (d < end && *d >= L'0' && *d <= L'9') {
        d++;
        digits++;
    }
    if (d < end && *d == L'.') {
        d++;
        while (d < end && *d >= L'0' && *d <= L'9') {
            d++;
            digits++;
        }
    }
    if (!digits)
        return js_nan;
    if (d < end && (*d == L'e' || *d == L'E')) {
        d++;
        if (d < end && (*d == L'+' || *d == L'-'))
            d++;
        int ed = 0;
        while (d < end && *d >= L'0' && *d <= L'9') {
            d++;
            ed++;
        }
        if (!ed)
            return js_nan;
    }
    if (d != end)
        return js_nan;
    if (end - c >= (long)sizeof(buffer))
        return js_nan;
    for (const WCHAR *q = c; q < end; q++)
        buffer[n++] = (char)*q;
    buffer[n] = 0;
    double v = strtod(buffer, nullptr);
    return neg ? -v : v;
}

double to_integer_d(double n)
{
    if (is_nan(n))
        return 0;
    if (n == 0 || is_inf(n))
        return n;
    return n < 0 ? -floor(-n) : floor(n);
}

LONG d_to_int32(double n)
{
    if (is_nan(n) || is_inf(n) || n == 0)
        return 0;
    /* values beyond 64 bits come out as zero, as the conversion of the processor leaves them. */
    if (n >= 9223372036854775808.0 || n < -9223372036854775808.0)
        return 0;
    return (LONG)(long long)n;
}

ULONG d_to_uint32(double n)
{
    return (ULONG)d_to_int32(n);
}

/* ------------------------------------------------------------------ conversions */

int to_primitive(Engine *e, Val *v, int hint, Val *out)
{
    if (v->t != T_OBJ) {
        *out = *v;
        return RC_OK;
    }
    return obj_default_value(e, v->u.o, hint, out);
}

int to_number(Engine *e, Val *v, double *out)
{
    switch (v->t) {
    case T_UNDEF:
    case T_HOLE:
        *out = js_nan;
        return RC_OK;
    case T_NULL:
        *out = 0;
        return RC_OK;
    case T_BOOL:
        *out = v->u.b ? 1 : 0;
        return RC_OK;
    case T_NUM:
        *out = v->u.n;
        return RC_OK;
    case T_STR:
        *out = str_to_num(v->u.s);
        return RC_OK;
    case T_VAR: {
        VARIANT r;
        VariantInit(&r);
        if (SUCCEEDED(VariantChangeType(&r, &v->u.v->v, 0, VT_R8))) {
            *out = r.dblVal;
            return RC_OK;
        }
        *out = js_nan;
        return RC_OK;
    }
    default: {
        Val p;
        int rc = to_primitive(e, v, 1, &p);
        if (rc)
            return rc;
        return to_number(e, &p, out);
    }
    }
}

int to_string(Engine *e, Val *v, Str **out)
{
    switch (v->t) {
    case T_UNDEF:
    case T_HOLE:
        *out = str_intern_ascii(e, "undefined");
        return RC_OK;
    case T_NULL:
        *out = str_intern_ascii(e, "null");
        return RC_OK;
    case T_BOOL:
        *out = str_intern_ascii(e, v->u.b ? "true" : "false");
        return RC_OK;
    case T_NUM:
        *out = num_to_str(e, v->u.n);
        return RC_OK;
    case T_STR:
        *out = v->u.s;
        return RC_OK;
    case T_VAR: {
        VARIANT r;
        VariantInit(&r);
        if (SUCCEEDED(VariantChangeTypeEx(&r, &v->u.v->v, 0x409, 0, VT_BSTR))) {
            *out = str_new(e, r.bstrVal, SysStringLen(r.bstrVal));
            VariantClear(&r);
            return RC_OK;
        }
        *out = str_empty(e);
        return RC_OK;
    }
    default: {
        Val p;
        int rc = to_primitive(e, v, 2, &p);
        if (rc)
            return rc;
        return to_string(e, &p, out);
    }
    }
}

int to_boolean_val(Val *v)
{
    switch (v->t) {
    case T_BOOL:
        return v->u.b;
    case T_NUM:
        return !(v->u.n == 0 || is_nan(v->u.n));
    case T_STR:
        return v->u.s->len != 0;
    case T_OBJ:
        return 1;
    case T_VAR:
        return 1;
    default:
        return 0;
    }
}

int to_object(Engine *e, Val *v, Obj **out)
{
    switch (v->t) {
    case T_OBJ:
        *out = v->u.o;
        return RC_OK;
    case T_UNDEF:
    case T_NULL:
    case T_HOLE:
        return throw_error(e, ERR_TYPE, 5007, nullptr);
    case T_BOOL: {
        Obj *o = obj_new(e, C_BOOLEAN, e->BooleanProto);
        o->u.prim.prim = *v;
        *out = o;
        return RC_OK;
    }
    case T_NUM: {
        Obj *o = obj_new(e, C_NUMBER, e->NumberProto);
        o->u.prim.prim = *v;
        *out = o;
        return RC_OK;
    }
    case T_STR: {
        Obj *o = obj_new(e, C_STRING, e->StringProto);
        o->u.prim.prim = *v;
        *out = o;
        return RC_OK;
    }
    default: {
        Obj *o = obj_new(e, C_OBJECT, e->ObjectProto);
        *out = o;
        return RC_OK;
    }
    }
}

int to_int32(Engine *e, Val *v, LONG *out)
{
    double d;
    int rc = to_number(e, v, &d);
    if (rc)
        return rc;
    *out = d_to_int32(d);
    return RC_OK;
}

int to_uint32(Engine *e, Val *v, ULONG *out)
{
    double d;
    int rc = to_number(e, v, &d);
    if (rc)
        return rc;
    *out = d_to_uint32(d);
    return RC_OK;
}

BOOL is_callable(Val *v)
{
    if (v->t != T_OBJ)
        return FALSE;
    return v->u.o->cls == C_FUNCTION || (v->u.o->cls == C_HOST && v->u.o->u.host.Callable);
}

const WCHAR *typeof_name(Val *v)
{
    switch (v->t) {
    case T_UNDEF:
    case T_HOLE:
        return L"undefined";
    case T_NULL:
        return L"object";
    case T_BOOL:
        return L"boolean";
    case T_NUM:
        return L"number";
    case T_STR:
        return L"string";
    case T_OBJ:
        return v->u.o->cls == C_FUNCTION ? L"function" : L"object";
    case T_VAR:
        return (v->u.v->v.vt == VT_DATE) ? L"date" : L"unknown";
    }
    return L"undefined";
}

BOOL strict_equals(Val *a, Val *b)
{
    if (a->t != b->t)
        return FALSE;
    switch (a->t) {
    case T_UNDEF:
    case T_NULL:
    case T_HOLE:
        return TRUE;
    case T_BOOL:
        return a->u.b == b->u.b;
    case T_NUM:
        return a->u.n == b->u.n;
    case T_STR:
        return str_equal(a->u.s, b->u.s);
    case T_OBJ:
        return a->u.o == b->u.o;
    default:
        return a->u.v == b->u.v;
    }
}

int loose_equals(Engine *e, Val *a, Val *b, BOOL *result)
{
    if (a->t == b->t || ((a->t == T_UNDEF || a->t == T_NULL) && (b->t == T_UNDEF || b->t == T_NULL))) {
        if ((a->t == T_UNDEF || a->t == T_NULL) && (b->t == T_UNDEF || b->t == T_NULL)) {
            *result = TRUE;
            return RC_OK;
        }
        if (a->t == T_OBJ && a->u.o != b->u.o && a->u.o->cls == C_HOST && b->u.o->cls == C_HOST) {
            *result = host_same_object(a->u.o, b->u.o);
            return RC_OK;
        }
        *result = strict_equals(a, b);
        return RC_OK;
    }
    if (a->t == T_UNDEF || a->t == T_NULL || b->t == T_UNDEF || b->t == T_NULL) {
        *result = FALSE;
        return RC_OK;
    }
    if ((a->t == T_NUM && b->t == T_STR) || (a->t == T_STR && b->t == T_NUM) || a->t == T_BOOL || b->t == T_BOOL) {
        if (a->t == T_OBJ || b->t == T_OBJ) {
            Val pa, pb;
            int rc = to_primitive(e, a, 0, &pa);
            if (rc)
                return rc;
            rc = to_primitive(e, b, 0, &pb);
            if (rc)
                return rc;
            return loose_equals(e, &pa, &pb, result);
        }
        double x, y;
        int rc = to_number(e, a, &x);
        if (rc)
            return rc;
        rc = to_number(e, b, &y);
        if (rc)
            return rc;
        *result = x == y;
        return RC_OK;
    }
    if (a->t == T_OBJ && b->t != T_OBJ) {
        Val p;
        int rc = to_primitive(e, a, 0, &p);
        if (rc)
            return rc;
        return loose_equals(e, &p, b, result);
    }
    if (b->t == T_OBJ && a->t != T_OBJ) {
        Val p;
        int rc = to_primitive(e, b, 0, &p);
        if (rc)
            return rc;
        return loose_equals(e, a, &p, result);
    }
    *result = FALSE;
    return RC_OK;
}

/* result: 1 when a < b, 0 when not, -1 when a number was NaN. */
int compare_values(Engine *e, Val *a, Val *b, BOOL left_first, int *result)
{
    Val pa, pb;
    int rc;
    if (left_first) {
        if ((rc = to_primitive(e, a, 1, &pa)) || (rc = to_primitive(e, b, 1, &pb)))
            return rc;
    } else {
        if ((rc = to_primitive(e, b, 1, &pb)) || (rc = to_primitive(e, a, 1, &pa)))
            return rc;
    }
    if (pa.t == T_STR && pb.t == T_STR) {
        *result = str_compare(pa.u.s, pb.u.s) < 0;
        return RC_OK;
    }
    double x, y;
    if ((rc = to_number(e, &pa, &x)) || (rc = to_number(e, &pb, &y)))
        return rc;
    if (is_nan(x) || is_nan(y))
        *result = -1;
    else
        *result = x < y;
    return RC_OK;
}
