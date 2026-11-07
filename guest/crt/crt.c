/*
 * freestanding c runtime shared by every guest module: the kernel,
 * drivers and user mode dlls each link a private copy.
 * compiled with -fno-builtin so the compiler does not turn these loops
 * back into calls to themselves.
 */
#include <stddef.h>
#include <stdint.h>

typedef uint64_t __attribute__((may_alias, aligned(1))) u64u;
typedef uint32_t __attribute__((may_alias, aligned(1))) u32u;

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (n >= 32) {
        u64u a = ((const u64u *)s)[0], b = ((const u64u *)s)[1];
        u64u c = ((const u64u *)s)[2], e = ((const u64u *)s)[3];
        ((u64u *)d)[0] = a;
        ((u64u *)d)[1] = b;
        ((u64u *)d)[2] = c;
        ((u64u *)d)[3] = e;
        d += 32;
        s += 32;
        n -= 32;
    }
    while (n >= 8) {
        *(u64u *)d = *(const u64u *)s;
        d += 8;
        s += 8;
        n -= 8;
    }
    while (n--)
        *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    if (d == s || n == 0)
        return dst;
    if (d < s || d >= s + n)
        return memcpy(dst, src, n);
    d += n;
    s += n;
    while (n >= 8) {
        d -= 8;
        s -= 8;
        n -= 8;
        *(u64u *)d = *(const u64u *)s;
    }
    while (n--)
        *--d = *--s;
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    uint8_t *d = dst;
    uint64_t v = (uint8_t)c;
    v |= v << 8;
    v |= v << 16;
    v |= v << 32;
    while (n >= 32) {
        ((u64u *)d)[0] = v;
        ((u64u *)d)[1] = v;
        ((u64u *)d)[2] = v;
        ((u64u *)d)[3] = v;
        d += 32;
        n -= 32;
    }
    while (n >= 8) {
        *(u64u *)d = v;
        d += 8;
        n -= 8;
    }
    while (n--)
        *d++ = (uint8_t)c;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    while (n >= 8 && *(const u64u *)x == *(const u64u *)y) {
        x += 8;
        y += 8;
        n -= 8;
    }
    for (; n; n--, x++, y++) {
        if (*x != *y)
            return *x < *y ? -1 : 1;
    }
    return 0;
}

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return (size_t)(p - s);
}

size_t strnlen(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n])
        n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b)
            return (unsigned char)*a - (unsigned char)*b;
        if (!*a)
            return 0;
    }
    return 0;
}

static inline int ascii_lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

int _stricmp(const char *a, const char *b)
{
    while (*a && ascii_lower((unsigned char)*a) == ascii_lower((unsigned char)*b)) {
        a++;
        b++;
    }
    return ascii_lower((unsigned char)*a) - ascii_lower((unsigned char)*b);
}

int _strnicmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        int x = ascii_lower((unsigned char)*a), y = ascii_lower((unsigned char)*b);
        if (x != y)
            return x - y;
        if (!x)
            return 0;
    }
    return 0;
}

char *strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++))
        ;
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    for (; i < n && src[i]; i++)
        dst[i] = src[i];
    for (; i < n; i++)
        dst[i] = 0;
    return dst;
}

char *strcat(char *dst, const char *src)
{
    strcpy(dst + strlen(dst), src);
    return dst;
}

char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c)
            return (char *)s;
        if (!*s)
            return nullptr;
    }
}

char *strrchr(const char *s, int c)
{
    const char *last = nullptr;
    for (;; s++) {
        if (*s == (char)c)
            last = s;
        if (!*s)
            return (char *)last;
    }
}

char *strstr(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    for (; *hay; hay++)
        if (!strncmp(hay, needle, n))
            return (char *)hay;
    return n ? nullptr : (char *)hay;
}

size_t wcslen(const wchar_t *s)
{
    const wchar_t *p = s;
    while (*p)
        p++;
    return (size_t)(p - s);
}

size_t wcsnlen(const wchar_t *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n])
        n++;
    return n;
}

int wcscmp(const wchar_t *a, const wchar_t *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)*a - (int)*b;
}

int wcsncmp(const wchar_t *a, const wchar_t *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b)
            return (int)*a - (int)*b;
        if (!*a)
            return 0;
    }
    return 0;
}

static inline wchar_t wide_upper(wchar_t c)
{
    if (c >= 'a' && c <= 'z')
        return (wchar_t)(c - 32);
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7)
        return (wchar_t)(c - 32);
    return c;
}

int _wcsicmp(const wchar_t *a, const wchar_t *b)
{
    while (*a && wide_upper(*a) == wide_upper(*b)) {
        a++;
        b++;
    }
    return (int)wide_upper(*a) - (int)wide_upper(*b);
}

int _wcsnicmp(const wchar_t *a, const wchar_t *b, size_t n)
{
    for (; n; n--, a++, b++) {
        wchar_t x = wide_upper(*a), y = wide_upper(*b);
        if (x != y)
            return (int)x - (int)y;
        if (!x)
            return 0;
    }
    return 0;
}

wchar_t *wcscpy(wchar_t *dst, const wchar_t *src)
{
    wchar_t *d = dst;
    while ((*d++ = *src++))
        ;
    return dst;
}

wchar_t *wcscat(wchar_t *dst, const wchar_t *src)
{
    wcscpy(dst + wcslen(dst), src);
    return dst;
}

wchar_t *wcsncat(wchar_t *dst, const wchar_t *src, size_t n)
{
    wchar_t *d = dst + wcslen(dst);
    while (n-- && *src)
        *d++ = *src++;
    *d = 0;
    return dst;
}

wchar_t *wcschr(const wchar_t *s, wchar_t c)
{
    for (;; s++) {
        if (*s == c)
            return (wchar_t *)s;
        if (!*s)
            return nullptr;
    }
}

wchar_t *wcsrchr(const wchar_t *s, wchar_t c)
{
    const wchar_t *last = nullptr;
    for (;; s++) {
        if (*s == c)
            last = s;
        if (!*s)
            return (wchar_t *)last;
    }
}

wchar_t *wcsstr(const wchar_t *hay, const wchar_t *needle)
{
    size_t n = wcslen(needle);
    if (!n)
        return (wchar_t *)hay;
    for (; *hay; hay++) {
        if (wcsncmp(hay, needle, n) == 0)
            return (wchar_t *)hay;
    }
    return nullptr;
}

wchar_t towupper(wchar_t c)
{
    return wide_upper(c);
}

wchar_t towlower(wchar_t c)
{
    if (c >= 'A' && c <= 'Z')
        return (wchar_t)(c + 32);
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7)
        return (wchar_t)(c + 32);
    return c;
}

int toupper(int c)
{
    return (c >= 'a' && c <= 'z') ? c - 32 : c;
}

int tolower(int c)
{
    return ascii_lower(c);
}

int abs(int v)
{
    return v < 0 ? -v : v;
}

long labs(long v)
{
    return v < 0 ? -v : v;
}

/* the msvc abi expects this symbol whenever floating point is used. */
int _fltused = 0x9875;

wchar_t *_wcslwr(wchar_t *s)
{
    for (wchar_t *p = s; *p; p++)
        *p = towlower(*p);
    return s;
}
