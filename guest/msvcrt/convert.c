/* integer conversions: the strtol and atoi families, narrow and wide, and integer to string. */
#include "msvcrt_private.h"

#define FL_UNSIGNED 1
#define FL_NEG 2
#define FL_OVERFLOW 4
#define FL_READDIGIT 8

static int wchar_to_digit(unsigned c)
{
    static const unsigned short zeros[] = {0x30,  0xff10, 0x660, 0x6f0, 0x966, 0x9e6,  0xa66,  0xae6,  0xb66,
                                           0xc66, 0xce6,  0xd66, 0xe50, 0xed0, 0xf20, 0x1040, 0x17e0, 0x1810};
    for (size_t i = 0; i < sizeof(zeros) / sizeof(zeros[0]); i++)
        if (c >= zeros[i] && c < zeros[i] + 10u)
            return c - zeros[i];
    return -1;
}

static inline unsigned get_char(const void *s, size_t i, int wide)
{
    return wide ? ((const wchar_t *)s)[i] : ((const unsigned char *)s)[i];
}

static int char_is_space(unsigned c, int wide)
{
    if (wide)
        return (MsvcrtWctype((wint_t)c) & _SPACE) != 0;
    return (MsvcrtCtype[c + 1] & _SPACE) != 0;
}

/* the value of digit c, or -1 when it is not a digit or letter. */
static int digit_value(unsigned c, int wide)
{
    if (wide) {
        int d = wchar_to_digit(c);
        if (d >= 0)
            return d;
        if (c >= 'a' && c <= 'z')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'Z')
            return c - 'A' + 10;
        return -1;
    }
    if (c >= '0' && c <= '9')
        return c - '0';
    if (MsvcrtCtype[c + 1] & (_UPPER | _LOWER))
        return MsvcrtToUpper((int)c) - 'A' + 10;
    return -1;
}

/*
 * msvcrt's strtoxl: clears errno, skips spaces, takes a sign and a base prefix, then digits. without overflow checks
 * the value wraps like atoi. returns the 64-bit magnitude, clamped and negated like msvcrt.
 */
static uint64_t strtox(const void *str, void *end, int base, int flags, int bits, int nocheck, int wide)
{
    size_t i = 0;
    if (end)
        *(const void **)end = str;
    if (!str || (base && (base < 2 || base > 36))) {
        MSVCRT_INVALID(EINVAL);
        return 0;
    }
    MsvcrtSetErrno(0);
    unsigned c = get_char(str, i++, wide);
    while (char_is_space(c, wide))
        c = get_char(str, i++, wide);
    if (c == '-') {
        flags |= FL_NEG;
        c = get_char(str, i++, wide);
    } else if (c == '+') {
        c = get_char(str, i++, wide);
    }
    unsigned next = get_char(str, i, wide);
    int zero = wide ? wchar_to_digit(c) == 0 : c == '0';
    if (!base) {
        if (zero) {
            if (next == 'x' || next == 'X') {
                base = 16;
                c = get_char(str, i + 1, wide);
                i += 2;
            } else {
                base = 8;
            }
        } else {
            base = 10;
        }
    } else if (base == 16 && zero && (next == 'x' || next == 'X')) {
        c = get_char(str, i + 1, wide);
        i += 2;
    }
    uint64_t max = bits == 64 ? UINT64_MAX : UINT32_MAX;
    uint64_t limit = max / base, rem = max % base;
    uint64_t value = 0;
    for (;;) {
        int d = digit_value(c, wide);
        if (d < 0 || d >= base)
            break;
        flags |= FL_READDIGIT;
        if (nocheck || value < limit || (value == limit && (uint64_t)d <= rem)) {
            value = value * base + d;
            if (bits == 32)
                value &= UINT32_MAX;
        } else {
            flags |= FL_OVERFLOW;
            if (!end)
                break;
        }
        c = get_char(str, i++, wide);
    }
    i--;
    if (!(flags & FL_READDIGIT)) {
        value = 0;
        i = 0;
    } else {
        uint64_t smax = bits == 64 ? (uint64_t)INT64_MAX : (uint64_t)INT32_MAX;
        int overflow = flags & FL_OVERFLOW;
        if (!overflow && !(flags & FL_UNSIGNED))
            overflow = (flags & FL_NEG) ? value > smax + 1 : value > smax;
        if (overflow && !nocheck) {
            MsvcrtSetErrno(ERANGE);
            value = (flags & FL_UNSIGNED) ? max : smax + ((flags & FL_NEG) != 0);
        }
    }
    if (end) {
        if (wide)
            *(const wchar_t **)end = (const wchar_t *)str + i;
        else
            *(const char **)end = (const char *)str + i;
    }
    if (flags & FL_NEG)
        value = (0 - value) & max;
    return value;
}

long __cdecl strtol(const char *s, char **end, int base)
{
    return (long)(int32_t)strtox(s, end, base, 0, 32, 0, 0);
}

unsigned long __cdecl strtoul(const char *s, char **end, int base)
{
    return (unsigned long)strtox(s, end, base, FL_UNSIGNED, 32, 0, 0);
}

long long __cdecl _strtoi64(const char *s, char **end, int base)
{
    return (long long)strtox(s, end, base, 0, 64, 0, 0);
}

unsigned long long __cdecl _strtoui64(const char *s, char **end, int base)
{
    return strtox(s, end, base, FL_UNSIGNED, 64, 0, 0);
}

long __cdecl _strtol_l(const char *s, char **end, int base, void *locale)
{
    return strtol(s, end, base);
}

unsigned long __cdecl _strtoul_l(const char *s, char **end, int base, void *locale)
{
    return strtoul(s, end, base);
}

long long __cdecl _strtoi64_l(const char *s, char **end, int base, void *locale)
{
    return _strtoi64(s, end, base);
}

unsigned long long __cdecl _strtoui64_l(const char *s, char **end, int base, void *locale)
{
    return _strtoui64(s, end, base);
}

long __cdecl wcstol(const wchar_t *s, wchar_t **end, int base)
{
    return (long)(int32_t)strtox(s, end, base, 0, 32, 0, 1);
}

unsigned long __cdecl wcstoul(const wchar_t *s, wchar_t **end, int base)
{
    return (unsigned long)strtox(s, end, base, FL_UNSIGNED, 32, 0, 1);
}

long long __cdecl _wcstoi64(const wchar_t *s, wchar_t **end, int base)
{
    return (long long)strtox(s, end, base, 0, 64, 0, 1);
}

unsigned long long __cdecl _wcstoui64(const wchar_t *s, wchar_t **end, int base)
{
    return strtox(s, end, base, FL_UNSIGNED, 64, 0, 1);
}

long __cdecl _wcstol_l(const wchar_t *s, wchar_t **end, int base, void *locale)
{
    return wcstol(s, end, base);
}

unsigned long __cdecl _wcstoul_l(const wchar_t *s, wchar_t **end, int base, void *locale)
{
    return wcstoul(s, end, base);
}

long long __cdecl _wcstoi64_l(const wchar_t *s, wchar_t **end, int base, void *locale)
{
    return _wcstoi64(s, end, base);
}

unsigned long long __cdecl _wcstoui64_l(const wchar_t *s, wchar_t **end, int base, void *locale)
{
    return _wcstoui64(s, end, base);
}

/* atoi and atol wrap on overflow; _atoi64 clamps like _strtoi64. */
int __cdecl atoi(const char *s)
{
    return s ? (int)(int32_t)strtox(s, nullptr, 10, 0, 32, 1, 0) : 0;
}

long __cdecl atol(const char *s)
{
    return atoi(s);
}

long long __cdecl _atoi64(const char *s)
{
    return s ? _strtoi64(s, nullptr, 10) : 0;
}

int __cdecl _atoi_l(const char *s, void *locale)
{
    return atoi(s);
}

long __cdecl _atol_l(const char *s, void *locale)
{
    return atoi(s);
}

long long __cdecl _atoi64_l(const char *s, void *locale)
{
    return _atoi64(s);
}

int __cdecl _wtoi(const wchar_t *s)
{
    return s ? (int)(int32_t)strtox(s, nullptr, 10, 0, 32, 1, 1) : 0;
}

long __cdecl _wtol(const wchar_t *s)
{
    return _wtoi(s);
}

long long __cdecl _wtoi64(const wchar_t *s)
{
    return s ? _wcstoi64(s, nullptr, 10) : 0;
}

int __cdecl _wtoi_l(const wchar_t *s, void *locale)
{
    return _wtoi(s);
}

long __cdecl _wtol_l(const wchar_t *s, void *locale)
{
    return _wtoi(s);
}

long long __cdecl _wtoi64_l(const wchar_t *s, void *locale)
{
    return _wtoi64(s);
}

/*
 * the C99 names of the 64-bit conversions: long long and intmax_t are _strtoi64's 64-bit integer, and the _l forms
 * ignore the locale.
 */
long long __cdecl strtoll(const char *s, char **end, int base)
{
    return _strtoi64(s, end, base);
}

unsigned long long __cdecl strtoull(const char *s, char **end, int base)
{
    return _strtoui64(s, end, base);
}

long long __cdecl strtoimax(const char *s, char **end, int base)
{
    return _strtoi64(s, end, base);
}

unsigned long long __cdecl strtoumax(const char *s, char **end, int base)
{
    return _strtoui64(s, end, base);
}

long long __cdecl _strtoll_l(const char *s, char **end, int base, void *locale)
{
    return _strtoi64(s, end, base);
}

unsigned long long __cdecl _strtoull_l(const char *s, char **end, int base, void *locale)
{
    return _strtoui64(s, end, base);
}

long long __cdecl _strtoimax_l(const char *s, char **end, int base, void *locale)
{
    return _strtoi64(s, end, base);
}

unsigned long long __cdecl _strtoumax_l(const char *s, char **end, int base, void *locale)
{
    return _strtoui64(s, end, base);
}

long long __cdecl wcstoll(const wchar_t *s, wchar_t **end, int base)
{
    return _wcstoi64(s, end, base);
}

unsigned long long __cdecl wcstoull(const wchar_t *s, wchar_t **end, int base)
{
    return _wcstoui64(s, end, base);
}

long long __cdecl wcstoimax(const wchar_t *s, wchar_t **end, int base)
{
    return _wcstoi64(s, end, base);
}

unsigned long long __cdecl wcstoumax(const wchar_t *s, wchar_t **end, int base)
{
    return _wcstoui64(s, end, base);
}

long long __cdecl _wcstoll_l(const wchar_t *s, wchar_t **end, int base, void *locale)
{
    return _wcstoi64(s, end, base);
}

unsigned long long __cdecl _wcstoull_l(const wchar_t *s, wchar_t **end, int base, void *locale)
{
    return _wcstoui64(s, end, base);
}

long long __cdecl _wcstoimax_l(const wchar_t *s, wchar_t **end, int base, void *locale)
{
    return _wcstoi64(s, end, base);
}

unsigned long long __cdecl _wcstoumax_l(const wchar_t *s, wchar_t **end, int base, void *locale)
{
    return _wcstoui64(s, end, base);
}

long long __cdecl atoll(const char *s)
{
    return _atoi64(s);
}

long long __cdecl _atoll_l(const char *s, void *locale)
{
    return _atoi64(s);
}

long long __cdecl _wtoll(const wchar_t *s)
{
    return _wtoi64(s);
}

long long __cdecl _wtoll_l(const wchar_t *s, void *locale)
{
    return _wtoi64(s);
}

long long __cdecl imaxabs(long long value)
{
    return value < 0 ? -value : value;
}

typedef struct {
    long long quot;
    long long rem;
} IMAXDIV;

IMAXDIV __cdecl imaxdiv(long long numerator, long long denominator)
{
    IMAXDIV r = {numerator / denominator, numerator % denominator};
    return r;
}

/* ------------------------------------------------------------------ integer to string */

/* writes value in radix, with a minus sign when neg, into buf of size characters. */
static errno_t xtoa(uint64_t value, void *buf, size_t size, unsigned radix, int neg, int wide)
{
    if (!buf || !size) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    if (wide)
        *(wchar_t *)buf = 0;
    else
        *(char *)buf = 0;
    if (size <= (size_t)(neg ? 2 : 1)) {
        MSVCRT_INVALID(ERANGE);
        return ERANGE;
    }
    if (radix < 2 || radix > 36) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    char tmp[72];
    size_t n = 0;
    do {
        unsigned d = value % radix;
        tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        value /= radix;
    } while (value);
    if (neg)
        tmp[n++] = '-';
    if (n >= size) {
        MSVCRT_INVALID(ERANGE);
        return ERANGE;
    }
    for (size_t i = 0; i < n; i++) {
        if (wide)
            ((wchar_t *)buf)[i] = tmp[n - 1 - i];
        else
            ((char *)buf)[i] = tmp[n - 1 - i];
    }
    if (wide)
        ((wchar_t *)buf)[n] = 0;
    else
        ((char *)buf)[n] = 0;
    return 0;
}

static void *xtoa_unsafe(uint64_t value, void *buf, unsigned radix, int neg, int wide)
{
    xtoa(value, buf, 72, radix, neg, wide);
    return buf;
}

char *__cdecl _itoa(int value, char *buf, int radix)
{
    int neg = radix == 10 && value < 0;
    return xtoa_unsafe(neg ? 0u - (uint32_t)value : (uint32_t)value, buf, radix, neg, 0);
}

char *__cdecl _ltoa(long value, char *buf, int radix)
{
    return _itoa(value, buf, radix);
}

char *__cdecl _ultoa(unsigned long value, char *buf, int radix)
{
    return xtoa_unsafe(value, buf, radix, 0, 0);
}

char *__cdecl _i64toa(long long value, char *buf, int radix)
{
    int neg = radix == 10 && value < 0;
    return xtoa_unsafe(neg ? 0 - (uint64_t)value : (uint64_t)value, buf, radix, neg, 0);
}

char *__cdecl _ui64toa(unsigned long long value, char *buf, int radix)
{
    return xtoa_unsafe(value, buf, radix, 0, 0);
}

wchar_t *__cdecl _itow(int value, wchar_t *buf, int radix)
{
    int neg = radix == 10 && value < 0;
    return xtoa_unsafe(neg ? 0u - (uint32_t)value : (uint32_t)value, buf, radix, neg, 1);
}

wchar_t *__cdecl _ltow(long value, wchar_t *buf, int radix)
{
    return _itow(value, buf, radix);
}

wchar_t *__cdecl _ultow(unsigned long value, wchar_t *buf, int radix)
{
    return xtoa_unsafe(value, buf, radix, 0, 1);
}

wchar_t *__cdecl _i64tow(long long value, wchar_t *buf, int radix)
{
    int neg = radix == 10 && value < 0;
    return xtoa_unsafe(neg ? 0 - (uint64_t)value : (uint64_t)value, buf, radix, neg, 1);
}

wchar_t *__cdecl _ui64tow(unsigned long long value, wchar_t *buf, int radix)
{
    return xtoa_unsafe(value, buf, radix, 0, 1);
}

errno_t __cdecl _itoa_s(int value, char *buf, size_t size, int radix)
{
    int neg = radix == 10 && value < 0;
    return xtoa(neg ? 0u - (uint32_t)value : (uint32_t)value, buf, size, radix, neg, 0);
}

errno_t __cdecl _ltoa_s(long value, char *buf, size_t size, int radix)
{
    return _itoa_s(value, buf, size, radix);
}

errno_t __cdecl _ultoa_s(unsigned long value, char *buf, size_t size, int radix)
{
    return xtoa(value, buf, size, radix, 0, 0);
}

errno_t __cdecl _i64toa_s(long long value, char *buf, size_t size, int radix)
{
    int neg = radix == 10 && value < 0;
    return xtoa(neg ? 0 - (uint64_t)value : (uint64_t)value, buf, size, radix, neg, 0);
}

errno_t __cdecl _ui64toa_s(unsigned long long value, char *buf, size_t size, int radix)
{
    return xtoa(value, buf, size, radix, 0, 0);
}

errno_t __cdecl _itow_s(int value, wchar_t *buf, size_t size, int radix)
{
    int neg = radix == 10 && value < 0;
    return xtoa(neg ? 0u - (uint32_t)value : (uint32_t)value, buf, size, radix, neg, 1);
}

errno_t __cdecl _ltow_s(long value, wchar_t *buf, size_t size, int radix)
{
    return _itow_s(value, buf, size, radix);
}

errno_t __cdecl _ultow_s(unsigned long value, wchar_t *buf, size_t size, int radix)
{
    return xtoa(value, buf, size, radix, 0, 1);
}

errno_t __cdecl _i64tow_s(long long value, wchar_t *buf, size_t size, int radix)
{
    int neg = radix == 10 && value < 0;
    return xtoa(neg ? 0 - (uint64_t)value : (uint64_t)value, buf, size, radix, neg, 1);
}

errno_t __cdecl _ui64tow_s(unsigned long long value, wchar_t *buf, size_t size, int radix)
{
    return xtoa(value, buf, size, radix, 0, 1);
}

/* ------------------------------------------------------------------ small integer helpers */

/* the rotations are compiler builtins, exported under their msvcrt names through msvcrt.def. */

int __cdecl abs(int v)
{
    return v < 0 ? -v : v;
}

long __cdecl labs(long v)
{
    return v < 0 ? -v : v;
}

long long __cdecl _abs64(long long v)
{
    return v < 0 ? -v : v;
}

long long __cdecl llabs(long long v)
{
    return v < 0 ? -v : v;
}

typedef struct {
    int quot;
    int rem;
} div_t;

typedef struct {
    long quot;
    long rem;
} ldiv_t;

typedef struct {
    long long quot;
    long long rem;
} lldiv_t;

div_t __cdecl div(int a, int b)
{
    div_t r = {a / b, a % b};
    return r;
}

ldiv_t __cdecl ldiv(long a, long b)
{
    ldiv_t r = {a / b, a % b};
    return r;
}

lldiv_t __cdecl lldiv(long long a, long long b)
{
    lldiv_t r = {a / b, a % b};
    return r;
}

unsigned int __cdecl MsvcrtRotl(unsigned int v, int shift)
{
    shift &= 31;
    return shift ? (v << shift) | (v >> (32 - shift)) : v;
}

unsigned int __cdecl MsvcrtRotr(unsigned int v, int shift)
{
    shift &= 31;
    return shift ? (v >> shift) | (v << (32 - shift)) : v;
}

unsigned long __cdecl MsvcrtLrotl(unsigned long v, int shift)
{
    return MsvcrtRotl(v, shift);
}

unsigned long __cdecl MsvcrtLrotr(unsigned long v, int shift)
{
    return MsvcrtRotr(v, shift);
}

unsigned long long __cdecl MsvcrtRotl64(unsigned long long v, int shift)
{
    shift &= 63;
    return shift ? (v << shift) | (v >> (64 - shift)) : v;
}

unsigned long long __cdecl MsvcrtRotr64(unsigned long long v, int shift)
{
    shift &= 63;
    return shift ? (v >> shift) | (v << (64 - shift)) : v;
}

unsigned short __cdecl _byteswap_ushort(unsigned short v)
{
    return (unsigned short)((v >> 8) | (v << 8));
}

unsigned long __cdecl _byteswap_ulong(unsigned long v)
{
    return __builtin_bswap32(v);
}

unsigned long long __cdecl _byteswap_uint64(unsigned long long v)
{
    return __builtin_bswap64(v);
}
