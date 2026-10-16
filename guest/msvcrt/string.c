/*
 * memory and string functions, narrow and wide.
 */
#include "msvcrt_private.h"

typedef uint64_t __attribute__((may_alias, aligned(1))) u64u;

/* ------------------------------------------------------------------ memory */

void *__cdecl memcpy(void *dst, const void *src, size_t n)
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

void *__cdecl memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    if (d == s || !n)
        return dst;
    if (d < s || d >= s + n) {
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

void *__cdecl memset(void *dst, int c, size_t n)
{
    uint8_t *d = dst;
    uint64_t v = (uint8_t)c * 0x0101010101010101ull;
    while (n >= 8) {
        *(u64u *)d = v;
        d += 8;
        n -= 8;
    }
    while (n--)
        *d++ = (uint8_t)c;
    return dst;
}

int __cdecl memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    while (n >= 8 && *(const u64u *)x == *(const u64u *)y) {
        x += 8;
        y += 8;
        n -= 8;
    }
    for (; n; n--, x++, y++)
        if (*x != *y)
            return *x < *y ? -1 : 1;
    return 0;
}

void *__cdecl memchr(const void *s, int c, size_t n)
{
    const uint8_t *p = s;
    for (; n; n--, p++)
        if (*p == (uint8_t)c)
            return (void *)p;
    return nullptr;
}

void *__cdecl _memccpy(void *dst, const void *src, int c, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (n--) {
        if ((*d++ = *s++) == (uint8_t)c)
            return d;
    }
    return nullptr;
}

int __cdecl _memicmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    if (n && (!a || !b)) {
        MSVCRT_INVALID(EINVAL);
        return INT_MAX;
    }
    for (; n; n--, x++, y++) {
        int cx = MsvcrtToLower(*x), cy = MsvcrtToLower(*y);
        if (cx != cy)
            return cx - cy;
    }
    return 0;
}

int __cdecl _memicmp_l(const void *a, const void *b, size_t n, void *locale)
{
    return _memicmp(a, b, n);
}

errno_t __cdecl memcpy_s(void *dst, size_t size, const void *src, size_t count)
{
    if (!count)
        return 0;
    if (!dst) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    if (!src || size < count) {
        memset(dst, 0, size);
        if (!src) {
            MSVCRT_INVALID(EINVAL);
            return EINVAL;
        }
        MSVCRT_INVALID(ERANGE);
        return ERANGE;
    }
    memcpy(dst, src, count);
    return 0;
}

errno_t __cdecl memmove_s(void *dst, size_t size, const void *src, size_t count)
{
    if (!count)
        return 0;
    if (!dst || !src) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    if (size < count) {
        MSVCRT_INVALID(ERANGE);
        return ERANGE;
    }
    memmove(dst, src, count);
    return 0;
}

/* ------------------------------------------------------------------ narrow strings */

size_t __cdecl strlen(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return p - s;
}

size_t __cdecl strnlen(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n])
        n++;
    return n;
}

int __cdecl strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a < (unsigned char)*b ? -1 : (unsigned char)*a > (unsigned char)*b;
}

int __cdecl strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b)
            return (unsigned char)*a < (unsigned char)*b ? -1 : 1;
        if (!*a)
            break;
    }
    return 0;
}

int __cdecl _stricmp(const char *a, const char *b)
{
    if (!a || !b) {
        MSVCRT_INVALID(EINVAL);
        return INT_MAX;
    }
    int ca, cb;
    do {
        ca = MsvcrtToLower((unsigned char)*a++);
        cb = MsvcrtToLower((unsigned char)*b++);
    } while (ca && ca == cb);
    return ca - cb;
}

int __cdecl _strnicmp(const char *a, const char *b, size_t n)
{
    if (!n)
        return 0;
    if (!a || !b) {
        MSVCRT_INVALID(EINVAL);
        return INT_MAX;
    }
    int ca, cb;
    do {
        ca = MsvcrtToLower((unsigned char)*a++);
        cb = MsvcrtToLower((unsigned char)*b++);
    } while (--n && ca && ca == cb);
    return ca - cb;
}

int __cdecl _strcmpi(const char *a, const char *b)
{
    return _stricmp(a, b);
}

int __cdecl _stricmp_l(const char *a, const char *b, void *locale)
{
    return _stricmp(a, b);
}

int __cdecl _strnicmp_l(const char *a, const char *b, size_t n, void *locale)
{
    return _strnicmp(a, b, n);
}

char *__cdecl strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++))
        ;
    return dst;
}

char *__cdecl strncpy(char *dst, const char *src, size_t n)
{
    char *d = dst;
    while (n && (*d = *src++)) {
        d++;
        n--;
    }
    while (n--)
        *d++ = 0;
    return dst;
}

char *__cdecl strcat(char *dst, const char *src)
{
    strcpy(dst + strlen(dst), src);
    return dst;
}

char *__cdecl strncat(char *dst, const char *src, size_t n)
{
    char *d = dst + strlen(dst);
    while (n-- && *src)
        *d++ = *src++;
    *d = 0;
    return dst;
}

char *__cdecl strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c)
            return (char *)s;
        if (!*s)
            return nullptr;
    }
}

char *__cdecl strrchr(const char *s, int c)
{
    const char *r = nullptr;
    for (;; s++) {
        if (*s == (char)c)
            r = s;
        if (!*s)
            return (char *)r;
    }
}

char *__cdecl strstr(const char *hay, const char *needle)
{
    if (!*needle)
        return (char *)hay;
    size_t n = strlen(needle);
    for (; *hay; hay++)
        if (*hay == *needle && !strncmp(hay, needle, n))
            return (char *)hay;
    return nullptr;
}

size_t __cdecl strspn(const char *s, const char *accept)
{
    unsigned char map[32] = {};
    for (; *accept; accept++)
        map[(unsigned char)*accept >> 3] |= 1 << (*accept & 7);
    size_t n = 0;
    while (s[n] && (map[(unsigned char)s[n] >> 3] & (1 << (s[n] & 7))))
        n++;
    return n;
}

size_t __cdecl strcspn(const char *s, const char *reject)
{
    unsigned char map[32] = {};
    map[0] = 1;
    for (; *reject; reject++)
        map[(unsigned char)*reject >> 3] |= 1 << (*reject & 7);
    size_t n = 0;
    while (!(map[(unsigned char)s[n] >> 3] & (1 << (s[n] & 7))))
        n++;
    return n;
}

char *__cdecl strpbrk(const char *s, const char *accept)
{
    s += strcspn(s, accept);
    return *s ? (char *)s : nullptr;
}

char *__cdecl _strspnp(const char *s, const char *accept)
{
    s += strspn(s, accept);
    return *s ? (char *)s : nullptr;
}

char *__cdecl strtok_s(char *s, const char *delim, char **context)
{
    if (!context || !delim || (!s && !*context)) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    if (!s)
        s = *context;
    s += strspn(s, delim);
    if (!*s) {
        *context = s;
        return nullptr;
    }
    char *end = s + strcspn(s, delim);
    if (*end)
        *end++ = 0;
    *context = end;
    return s;
}

char *__cdecl strtok(char *s, const char *delim)
{
    MSVCRT_THREAD_DATA *ptd = MsvcrtThreadData();
    if (!s) {
        s = ptd->strtok_next;
        if (!s)
            return nullptr;
    }
    return strtok_s(s, delim, &ptd->strtok_next);
}

char *__cdecl _strdup(const char *s)
{
    if (!s)
        return nullptr;
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (d)
        memcpy(d, s, n);
    return d;
}

char *__cdecl _strlwr(char *s)
{
    if (!s) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    for (char *p = s; *p; p++)
        *p = (char)MsvcrtToLower((unsigned char)*p);
    return s;
}

char *__cdecl _strupr(char *s)
{
    if (!s) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    for (char *p = s; *p; p++)
        *p = (char)MsvcrtToUpper((unsigned char)*p);
    return s;
}

static errno_t case_s(char *s, size_t size, int upper)
{
    if (!s || !size) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    size_t n = strnlen(s, size);
    if (n >= size) {
        *s = 0;
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    for (; *s; s++)
        *s = (char)(upper ? MsvcrtToUpper((unsigned char)*s) : MsvcrtToLower((unsigned char)*s));
    return 0;
}

errno_t __cdecl _strlwr_s(char *s, size_t size)
{
    return case_s(s, size, 0);
}

errno_t __cdecl _strupr_s(char *s, size_t size)
{
    return case_s(s, size, 1);
}

char *__cdecl _strrev(char *s)
{
    size_t n = strlen(s);
    for (size_t i = 0; i < n / 2; i++) {
        char t = s[i];
        s[i] = s[n - 1 - i];
        s[n - 1 - i] = t;
    }
    return s;
}

char *__cdecl _strset(char *s, int c)
{
    for (char *p = s; *p; p++)
        *p = (char)c;
    return s;
}

char *__cdecl _strnset(char *s, int c, size_t n)
{
    for (char *p = s; *p && n; p++, n--)
        *p = (char)c;
    return s;
}

errno_t __cdecl strcpy_s(char *dst, size_t size, const char *src)
{
    if (!dst || !size) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    if (!src) {
        *dst = 0;
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    size_t i = 0;
    for (; i < size; i++)
        if (!(dst[i] = src[i]))
            return 0;
    *dst = 0;
    MSVCRT_INVALID(ERANGE);
    return ERANGE;
}

errno_t __cdecl strcat_s(char *dst, size_t size, const char *src)
{
    if (!dst || !size) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    if (!src) {
        *dst = 0;
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    size_t n = strnlen(dst, size);
    if (n == size) {
        *dst = 0;
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    for (size_t i = n; i < size; i++)
        if (!(dst[i] = src[i - n]))
            return 0;
    *dst = 0;
    MSVCRT_INVALID(ERANGE);
    return ERANGE;
}

errno_t __cdecl strncpy_s(char *dst, size_t size, const char *src, size_t count)
{
    if (!count && !dst && !size)
        return 0;
    if (!dst || !size) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    if (!count) {
        *dst = 0;
        return 0;
    }
    if (!src) {
        *dst = 0;
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    size_t i = 0;
    int truncate = count == (size_t)-1;
    while (i < size && i < count && src[i]) {
        dst[i] = src[i];
        i++;
    }
    if (i < size) {
        dst[i] = 0;
        return 0;
    }
    if (truncate) {
        dst[size - 1] = 0;
        return STRUNCATE;
    }
    *dst = 0;
    MSVCRT_INVALID(ERANGE);
    return ERANGE;
}

errno_t __cdecl strncat_s(char *dst, size_t size, const char *src, size_t count)
{
    if (!count && !dst && !size)
        return 0;
    if (!dst || !size) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    if (!src && count) {
        *dst = 0;
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    size_t n = strnlen(dst, size);
    if (n == size) {
        *dst = 0;
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    size_t i = 0;
    while (n + i < size && i < count && src[i]) {
        dst[n + i] = src[i];
        i++;
    }
    if (n + i < size) {
        dst[n + i] = 0;
        return 0;
    }
    if (count == (size_t)-1) {
        dst[size - 1] = 0;
        return STRUNCATE;
    }
    *dst = 0;
    MSVCRT_INVALID(ERANGE);
    return ERANGE;
}

/* ------------------------------------------------------------------ wide strings */

static inline wchar_t wlower(wchar_t c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static inline wchar_t wupper(wchar_t c)
{
    return c >= 'a' && c <= 'z' ? c - 32 : c;
}

size_t __cdecl wcslen(const wchar_t *s)
{
    const wchar_t *p = s;
    while (*p)
        p++;
    return p - s;
}

size_t __cdecl wcsnlen(const wchar_t *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n])
        n++;
    return n;
}

int __cdecl wcscmp(const wchar_t *a, const wchar_t *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a < *b ? -1 : *a > *b;
}

int __cdecl wcsncmp(const wchar_t *a, const wchar_t *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b)
            return *a < *b ? -1 : 1;
        if (!*a)
            break;
    }
    return 0;
}

int __cdecl _wcsicmp(const wchar_t *a, const wchar_t *b)
{
    if (!a || !b) {
        MSVCRT_INVALID(EINVAL);
        return INT_MAX;
    }
    wchar_t ca, cb;
    do {
        ca = wlower(*a++);
        cb = wlower(*b++);
    } while (ca && ca == cb);
    return (int)ca - (int)cb;
}

int __cdecl _wcsnicmp(const wchar_t *a, const wchar_t *b, size_t n)
{
    if (!n)
        return 0;
    if (!a || !b) {
        MSVCRT_INVALID(EINVAL);
        return INT_MAX;
    }
    wchar_t ca, cb;
    do {
        ca = wlower(*a++);
        cb = wlower(*b++);
    } while (--n && ca && ca == cb);
    return (int)ca - (int)cb;
}

int __cdecl _wcsicmp_l(const wchar_t *a, const wchar_t *b, void *locale)
{
    return _wcsicmp(a, b);
}

int __cdecl _wcsnicmp_l(const wchar_t *a, const wchar_t *b, size_t n, void *locale)
{
    return _wcsnicmp(a, b, n);
}

wchar_t *__cdecl wcscpy(wchar_t *dst, const wchar_t *src)
{
    wchar_t *d = dst;
    while ((*d++ = *src++))
        ;
    return dst;
}

wchar_t *__cdecl wcsncpy(wchar_t *dst, const wchar_t *src, size_t n)
{
    wchar_t *d = dst;
    while (n && (*d = *src++)) {
        d++;
        n--;
    }
    while (n--)
        *d++ = 0;
    return dst;
}

wchar_t *__cdecl wcscat(wchar_t *dst, const wchar_t *src)
{
    wcscpy(dst + wcslen(dst), src);
    return dst;
}

wchar_t *__cdecl wcsncat(wchar_t *dst, const wchar_t *src, size_t n)
{
    wchar_t *d = dst + wcslen(dst);
    while (n-- && *src)
        *d++ = *src++;
    *d = 0;
    return dst;
}

wchar_t *__cdecl wcschr(const wchar_t *s, wchar_t c)
{
    for (;; s++) {
        if (*s == c)
            return (wchar_t *)s;
        if (!*s)
            return nullptr;
    }
}

wchar_t *__cdecl wcsrchr(const wchar_t *s, wchar_t c)
{
    const wchar_t *r = nullptr;
    for (;; s++) {
        if (*s == c)
            r = s;
        if (!*s)
            return (wchar_t *)r;
    }
}

wchar_t *__cdecl wcsstr(const wchar_t *hay, const wchar_t *needle)
{
    if (!*needle)
        return (wchar_t *)hay;
    size_t n = wcslen(needle);
    for (; *hay; hay++)
        if (*hay == *needle && !wcsncmp(hay, needle, n))
            return (wchar_t *)hay;
    return nullptr;
}

size_t __cdecl wcsspn(const wchar_t *s, const wchar_t *accept)
{
    size_t n = 0;
    for (; s[n]; n++) {
        const wchar_t *a = accept;
        while (*a && *a != s[n])
            a++;
        if (!*a)
            break;
    }
    return n;
}

size_t __cdecl wcscspn(const wchar_t *s, const wchar_t *reject)
{
    size_t n = 0;
    for (; s[n]; n++) {
        const wchar_t *r = reject;
        while (*r && *r != s[n])
            r++;
        if (*r)
            break;
    }
    return n;
}

wchar_t *__cdecl wcspbrk(const wchar_t *s, const wchar_t *accept)
{
    s += wcscspn(s, accept);
    return *s ? (wchar_t *)s : nullptr;
}

wchar_t *__cdecl _wcsspnp(const wchar_t *s, const wchar_t *accept)
{
    s += wcsspn(s, accept);
    return *s ? (wchar_t *)s : nullptr;
}

wchar_t *__cdecl wcstok_s(wchar_t *s, const wchar_t *delim, wchar_t **context)
{
    if (!context || !delim || (!s && !*context)) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    if (!s)
        s = *context;
    s += wcsspn(s, delim);
    if (!*s) {
        *context = s;
        return nullptr;
    }
    wchar_t *end = s + wcscspn(s, delim);
    if (*end)
        *end++ = 0;
    *context = end;
    return s;
}

wchar_t *__cdecl wcstok(wchar_t *s, const wchar_t *delim)
{
    MSVCRT_THREAD_DATA *ptd = MsvcrtThreadData();
    if (!s) {
        s = ptd->wcstok_next;
        if (!s)
            return nullptr;
    }
    return wcstok_s(s, delim, &ptd->wcstok_next);
}

wchar_t *__cdecl _wcsdup(const wchar_t *s)
{
    if (!s)
        return nullptr;
    size_t n = (wcslen(s) + 1) * sizeof(wchar_t);
    wchar_t *d = malloc(n);
    if (d)
        memcpy(d, s, n);
    return d;
}

wchar_t *__cdecl _wcslwr(wchar_t *s)
{
    if (!s) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    for (wchar_t *p = s; *p; p++)
        *p = wlower(*p);
    return s;
}

wchar_t *__cdecl _wcsupr(wchar_t *s)
{
    if (!s) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    for (wchar_t *p = s; *p; p++)
        *p = wupper(*p);
    return s;
}

static errno_t wcase_s(wchar_t *s, size_t size, int upper)
{
    if (!s || !size) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    if (wcsnlen(s, size) >= size) {
        *s = 0;
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    for (; *s; s++)
        *s = upper ? wupper(*s) : wlower(*s);
    return 0;
}

errno_t __cdecl _wcslwr_s(wchar_t *s, size_t size)
{
    return wcase_s(s, size, 0);
}

errno_t __cdecl _wcsupr_s(wchar_t *s, size_t size)
{
    return wcase_s(s, size, 1);
}

wchar_t *__cdecl _wcsrev(wchar_t *s)
{
    size_t n = wcslen(s);
    for (size_t i = 0; i < n / 2; i++) {
        wchar_t t = s[i];
        s[i] = s[n - 1 - i];
        s[n - 1 - i] = t;
    }
    return s;
}

wchar_t *__cdecl _wcsset(wchar_t *s, wchar_t c)
{
    for (wchar_t *p = s; *p; p++)
        *p = c;
    return s;
}

wchar_t *__cdecl _wcsnset(wchar_t *s, wchar_t c, size_t n)
{
    for (wchar_t *p = s; *p && n; p++, n--)
        *p = c;
    return s;
}

errno_t __cdecl wcscpy_s(wchar_t *dst, size_t size, const wchar_t *src)
{
    if (!dst || !size) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    if (!src) {
        *dst = 0;
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    for (size_t i = 0; i < size; i++)
        if (!(dst[i] = src[i]))
            return 0;
    *dst = 0;
    MSVCRT_INVALID(ERANGE);
    return ERANGE;
}

errno_t __cdecl wcscat_s(wchar_t *dst, size_t size, const wchar_t *src)
{
    if (!dst || !size) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    if (!src) {
        *dst = 0;
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    size_t n = wcsnlen(dst, size);
    if (n == size) {
        *dst = 0;
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    for (size_t i = n; i < size; i++)
        if (!(dst[i] = src[i - n]))
            return 0;
    *dst = 0;
    MSVCRT_INVALID(ERANGE);
    return ERANGE;
}

errno_t __cdecl wcsncpy_s(wchar_t *dst, size_t size, const wchar_t *src, size_t count)
{
    if (!count && !dst && !size)
        return 0;
    if (!dst || !size) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    if (!count) {
        *dst = 0;
        return 0;
    }
    if (!src) {
        *dst = 0;
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    size_t i = 0;
    while (i < size && i < count && src[i]) {
        dst[i] = src[i];
        i++;
    }
    if (i < size) {
        dst[i] = 0;
        return 0;
    }
    if (count == (size_t)-1) {
        dst[size - 1] = 0;
        return STRUNCATE;
    }
    *dst = 0;
    MSVCRT_INVALID(ERANGE);
    return ERANGE;
}

errno_t __cdecl wcsncat_s(wchar_t *dst, size_t size, const wchar_t *src, size_t count)
{
    if (!count && !dst && !size)
        return 0;
    if (!dst || !size) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    if (!src && count) {
        *dst = 0;
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    size_t n = wcsnlen(dst, size);
    if (n == size) {
        *dst = 0;
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    size_t i = 0;
    while (n + i < size && i < count && src[i]) {
        dst[n + i] = src[i];
        i++;
    }
    if (n + i < size) {
        dst[n + i] = 0;
        return 0;
    }
    if (count == (size_t)-1) {
        dst[size - 1] = 0;
        return STRUNCATE;
    }
    *dst = 0;
    MSVCRT_INVALID(ERANGE);
    return ERANGE;
}

/* ------------------------------------------------------------------ multibyte strings */

/* lead byte flags of the multibyte code page, the ansi code page at startup. */
static unsigned char g_mblead[256];
static int g_mbcp_init;
static int g_mbcp;

static void mbcp_init(void)
{
    if (g_mbcp_init)
        return;
    CPINFO info;
    g_mbcp = GetACP();
    if (GetCPInfo(g_mbcp, &info) && info.MaxCharSize > 1) {
        for (int i = 0; i < MAX_LEADBYTES && (info.LeadByte[i] || info.LeadByte[i + 1]); i += 2)
            for (int b = info.LeadByte[i]; b <= info.LeadByte[i + 1]; b++)
                g_mblead[b] = 1;
    }
    g_mbcp_init = 1;
}

int __cdecl _ismbblead(unsigned int c)
{
    mbcp_init();
    return g_mblead[c & 0xff] ? 4 : 0;
}

int __cdecl _ismbbtrail(unsigned int c)
{
    mbcp_init();
    unsigned char b = c & 0xff;
    return g_mbcp != 1252 && b >= 0x40 && b != 0x7f ? 8 : 0;
}

int __cdecl _getmbcp(void)
{
    mbcp_init();
    return g_mbcp;
}

int __cdecl _setmbcp(int cp)
{
    if (cp == -3 || cp == -4 || cp == -5) {
        cp = GetACP();
    } else if (cp == -2) {
        cp = GetOEMCP();
    }
    CPINFO info;
    if (!GetCPInfo(cp, &info)) {
        MsvcrtSetErrno(EINVAL);
        return -1;
    }
    memset(g_mblead, 0, sizeof(g_mblead));
    if (info.MaxCharSize > 1) {
        for (int i = 0; i < MAX_LEADBYTES && (info.LeadByte[i] || info.LeadByte[i + 1]); i += 2)
            for (int b = info.LeadByte[i]; b <= info.LeadByte[i + 1]; b++)
                g_mblead[b] = 1;
    }
    g_mbcp = cp;
    g_mbcp_init = 1;
    return 0;
}

static inline int lead(unsigned char c)
{
    mbcp_init();
    return g_mblead[c];
}

unsigned char *__cdecl _mbschr(const unsigned char *s, unsigned int c)
{
    for (; *s; s++) {
        if (lead(*s) && s[1]) {
            if (((unsigned)s[0] << 8 | s[1]) == c)
                return (unsigned char *)s;
            s++;
            continue;
        }
        if (*s == c)
            return (unsigned char *)s;
    }
    return c ? nullptr : (unsigned char *)s;
}

unsigned char *__cdecl _mbsrchr(const unsigned char *s, unsigned int c)
{
    if (!s) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    const unsigned char *r = nullptr;
    for (;; s++) {
        if (lead(*s) && s[1]) {
            if (((unsigned)s[0] << 8 | s[1]) == c)
                r = s;
            s++;
            continue;
        }
        if (*s == c)
            r = s;
        if (!*s)
            break;
    }
    return (unsigned char *)r;
}

size_t __cdecl _mbslen(const unsigned char *s)
{
    size_t n = 0;
    for (; *s; s++, n++)
        if (lead(*s) && s[1])
            s++;
    return n;
}

size_t __cdecl _mbstrlen(const char *s)
{
    return _mbslen((const unsigned char *)s);
}

unsigned char *__cdecl _mbsinc(const unsigned char *s)
{
    return (unsigned char *)(lead(*s) && s[1] ? s + 2 : s + 1);
}

unsigned char *__cdecl _mbsdec(const unsigned char *start, const unsigned char *cur)
{
    if (cur <= start)
        return nullptr;
    const unsigned char *p = start, *prev = start;
    while (p < cur) {
        prev = p;
        p = _mbsinc(p);
    }
    return (unsigned char *)prev;
}

unsigned int __cdecl _mbsnextc(const unsigned char *s)
{
    if (lead(*s) && s[1])
        return (unsigned)s[0] << 8 | s[1];
    return *s;
}

int __cdecl _mbscmp(const unsigned char *a, const unsigned char *b)
{
    for (;;) {
        unsigned ca = _mbsnextc(a), cb = _mbsnextc(b);
        if (ca != cb)
            return ca < cb ? -1 : 1;
        if (!ca)
            return 0;
        a = _mbsinc(a);
        b = _mbsinc(b);
    }
}

static unsigned mb_lower(unsigned c)
{
    return c < 256 ? (unsigned)MsvcrtToLower((int)c) : c;
}

int __cdecl _mbsicmp(const unsigned char *a, const unsigned char *b)
{
    for (;;) {
        unsigned ca = mb_lower(_mbsnextc(a)), cb = mb_lower(_mbsnextc(b));
        if (ca != cb)
            return ca < cb ? -1 : 1;
        if (!ca)
            return 0;
        a = _mbsinc(a);
        b = _mbsinc(b);
    }
}

int __cdecl _mbsncmp(const unsigned char *a, const unsigned char *b, size_t n)
{
    for (; n; n--) {
        unsigned ca = _mbsnextc(a), cb = _mbsnextc(b);
        if (ca != cb)
            return ca < cb ? -1 : 1;
        if (!ca)
            return 0;
        a = _mbsinc(a);
        b = _mbsinc(b);
    }
    return 0;
}

int __cdecl _mbsnicmp(const unsigned char *a, const unsigned char *b, size_t n)
{
    for (; n; n--) {
        unsigned ca = mb_lower(_mbsnextc(a)), cb = mb_lower(_mbsnextc(b));
        if (ca != cb)
            return ca < cb ? -1 : 1;
        if (!ca)
            return 0;
        a = _mbsinc(a);
        b = _mbsinc(b);
    }
    return 0;
}

int __cdecl _mbsnbcmp(const unsigned char *a, const unsigned char *b, size_t n)
{
    return strncmp((const char *)a, (const char *)b, n);
}

int __cdecl _mbsnbicmp(const unsigned char *a, const unsigned char *b, size_t n)
{
    return _strnicmp((const char *)a, (const char *)b, n);
}

unsigned char *__cdecl _mbsstr(const unsigned char *hay, const unsigned char *needle)
{
    size_t n = strlen((const char *)needle);
    if (!n)
        return (unsigned char *)hay;
    for (; *hay; hay = _mbsinc(hay))
        if (!strncmp((const char *)hay, (const char *)needle, n))
            return (unsigned char *)hay;
    return nullptr;
}

unsigned char *__cdecl _mbscpy(unsigned char *dst, const unsigned char *src)
{
    return (unsigned char *)strcpy((char *)dst, (const char *)src);
}

unsigned char *__cdecl _mbscat(unsigned char *dst, const unsigned char *src)
{
    return (unsigned char *)strcat((char *)dst, (const char *)src);
}

unsigned char *__cdecl _mbsdup(const unsigned char *s)
{
    return (unsigned char *)_strdup((const char *)s);
}

size_t __cdecl _mbsspn(const unsigned char *s, const unsigned char *set)
{
    const unsigned char *p = s;
    while (*p) {
        unsigned c = _mbsnextc(p);
        if (!_mbschr(set, c))
            break;
        p = _mbsinc(p);
    }
    return p - s;
}

size_t __cdecl _mbscspn(const unsigned char *s, const unsigned char *set)
{
    const unsigned char *p = s;
    while (*p) {
        unsigned c = _mbsnextc(p);
        if (_mbschr(set, c))
            break;
        p = _mbsinc(p);
    }
    return p - s;
}

unsigned char *__cdecl _mbspbrk(const unsigned char *s, const unsigned char *set)
{
    s += _mbscspn(s, set);
    return *s ? (unsigned char *)s : nullptr;
}

unsigned char *__cdecl _mbsspnp(const unsigned char *s, const unsigned char *set)
{
    s += _mbsspn(s, set);
    return *s ? (unsigned char *)s : nullptr;
}

unsigned char *__cdecl _mbstok(unsigned char *s, const unsigned char *delim)
{
    MSVCRT_THREAD_DATA *ptd = MsvcrtThreadData();
    if (!s) {
        s = ptd->mbstok_next;
        if (!s)
            return nullptr;
    }
    s += _mbsspn(s, delim);
    if (!*s) {
        ptd->mbstok_next = s;
        return nullptr;
    }
    unsigned char *end = s + _mbscspn(s, delim);
    if (*end) {
        unsigned char *next = _mbsinc(end);
        *end = 0;
        if (next == end + 2)
            end[1] = 0;
        end = next;
    }
    ptd->mbstok_next = end;
    return s;
}

unsigned char *__cdecl _mbslwr(unsigned char *s)
{
    for (unsigned char *p = s; *p; p = _mbsinc(p))
        if (!lead(*p))
            *p = (unsigned char)MsvcrtToLower(*p);
    return s;
}

unsigned char *__cdecl _mbsupr(unsigned char *s)
{
    for (unsigned char *p = s; *p; p = _mbsinc(p))
        if (!lead(*p))
            *p = (unsigned char)MsvcrtToUpper(*p);
    return s;
}

size_t __cdecl _mbsnbcnt(const unsigned char *s, size_t n)
{
    const unsigned char *p = s;
    for (; n && *p; n--)
        p = _mbsinc(p);
    return p - s;
}

size_t __cdecl _mbsnccnt(const unsigned char *s, size_t n)
{
    size_t count = 0;
    const unsigned char *p = s;
    while (*p && (size_t)(p - s) < n) {
        p = _mbsinc(p);
        count++;
    }
    return count;
}

unsigned char *__cdecl _mbsncpy(unsigned char *dst, const unsigned char *src, size_t n)
{
    size_t bytes = _mbsnbcnt(src, n);
    unsigned char *d = dst;
    memcpy(d, src, bytes);
    d += bytes;
    for (size_t i = bytes; i < n; i++)
        *d++ = 0;
    return dst;
}

unsigned char *__cdecl _mbsnbcpy(unsigned char *dst, const unsigned char *src, size_t n)
{
    return (unsigned char *)strncpy((char *)dst, (const char *)src, n);
}

unsigned char *__cdecl _mbsncat(unsigned char *dst, const unsigned char *src, size_t n)
{
    size_t bytes = _mbsnbcnt(src, n);
    return (unsigned char *)strncat((char *)dst, (const char *)src, bytes);
}

unsigned char *__cdecl _mbsnbcat(unsigned char *dst, const unsigned char *src, size_t n)
{
    return (unsigned char *)strncat((char *)dst, (const char *)src, n);
}

unsigned char *__cdecl _mbsrev(unsigned char *s)
{
    _strrev((char *)s);
    for (unsigned char *p = s; *p; p++) {
        if (p[1] && lead(p[1])) {
            unsigned char t = p[0];
            p[0] = p[1];
            p[1] = t;
            p++;
        }
    }
    return s;
}

int __cdecl _mbbtype(unsigned char c, int type)
{
    if (type == 1)
        return _ismbbtrail(c) ? 2 : -1;
    return lead(c) ? 1 : 0;
}

int __cdecl _ismbcdigit(unsigned int c)
{
    return c < 256 && MsvcrtIsDigit((int)c);
}

int __cdecl _ismbcspace(unsigned int c)
{
    return c < 256 && MsvcrtIsSpace((int)c);
}

int __cdecl _ismbcalpha(unsigned int c)
{
    return c < 256 && (MSVCRT_CTYPE(c) & (_UPPER | _LOWER));
}

int __cdecl _ismbcupper(unsigned int c)
{
    return c < 256 && (MSVCRT_CTYPE(c) & _UPPER);
}

int __cdecl _ismbclower(unsigned int c)
{
    return c < 256 && (MSVCRT_CTYPE(c) & _LOWER);
}

unsigned int __cdecl _mbctolower(unsigned int c)
{
    return c < 256 ? (unsigned)MsvcrtToLower((int)c) : c;
}

unsigned int __cdecl _mbctoupper(unsigned int c)
{
    return c < 256 ? (unsigned)MsvcrtToUpper((int)c) : c;
}

size_t __cdecl _mbclen(const unsigned char *s)
{
    return lead(*s) ? 2 : 1;
}

void __cdecl _mbccpy(unsigned char *dst, const unsigned char *src)
{
    dst[0] = src[0];
    if (lead(src[0]))
        dst[1] = src[1];
}
