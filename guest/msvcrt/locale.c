/*
 * locale: msvcrt's c locale, the only one supported, with setlocale,
 * localeconv, collation and the multibyte conversions of that locale.
 */
#include "msvcrt_private.h"

#define LC_ALL 0
#define LC_COLLATE 1
#define LC_CTYPE 2
#define LC_MONETARY 3
#define LC_NUMERIC 4
#define LC_TIME 5

struct lconv {
    char *decimal_point;
    char *thousands_sep;
    char *grouping;
    char *int_curr_symbol;
    char *currency_symbol;
    char *mon_decimal_point;
    char *mon_thousands_sep;
    char *mon_grouping;
    char *positive_sign;
    char *negative_sign;
    char int_frac_digits;
    char frac_digits;
    char p_cs_precedes;
    char p_sep_by_space;
    char n_cs_precedes;
    char n_sep_by_space;
    char p_sign_posn;
    char n_sign_posn;
};

static char g_point[] = ".";
static char g_empty[] = "";

static struct lconv g_lconv = {
    g_point, g_empty, g_empty, g_empty, g_empty, g_empty, g_empty, g_empty, g_empty, g_empty,
    CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX,
};

/* the exported locale data of the c locale. */
unsigned int __lc_codepage;
unsigned int __lc_collate_cp;
LCID __lc_handle[6];

static char g_locale_name[] = "C";

char *__cdecl setlocale(int category, const char *locale)
{
    if (category < LC_ALL || category > LC_TIME) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    if (!locale || !*locale || !strcmp(locale, "C"))
        return g_locale_name;
    /* only the c locale exists: other names fail like an unknown locale. */
    return nullptr;
}

wchar_t *__cdecl _wsetlocale(int category, const wchar_t *locale)
{
    static wchar_t name[] = L"C";
    if (category < LC_ALL || category > LC_TIME) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    if (!locale || !*locale || !wcscmp(locale, L"C"))
        return name;
    return nullptr;
}

struct lconv *__cdecl localeconv(void)
{
    return &g_lconv;
}

int __cdecl __lconv_init(void)
{
    g_lconv.int_frac_digits = g_lconv.frac_digits = CHAR_MAX;
    g_lconv.p_cs_precedes = g_lconv.p_sep_by_space = CHAR_MAX;
    g_lconv.n_cs_precedes = g_lconv.n_sep_by_space = CHAR_MAX;
    g_lconv.p_sign_posn = g_lconv.n_sign_posn = CHAR_MAX;
    return 0;
}

unsigned int __cdecl ___lc_codepage_func(void)
{
    return __lc_codepage;
}

unsigned int __cdecl ___lc_collate_cp_func(void)
{
    return __lc_collate_cp;
}

LCID *__cdecl ___lc_handle_func(void)
{
    return __lc_handle;
}

int __cdecl _configthreadlocale(int type)
{
    if (type < -1 || type > 2) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    return 2; /* _DISABLE_PER_THREAD_LOCALE */
}

/* ------------------------------------------------------------------ collation */

int __cdecl strcoll(const char *a, const char *b)
{
    if (!a || !b) {
        MSVCRT_INVALID(EINVAL);
        return INT_MAX;
    }
    return strcmp(a, b);
}

int __cdecl _stricoll(const char *a, const char *b)
{
    return _stricmp(a, b);
}

int __cdecl _strncoll(const char *a, const char *b, size_t n)
{
    return strncmp(a, b, n);
}

int __cdecl _strnicoll(const char *a, const char *b, size_t n)
{
    return _strnicmp(a, b, n);
}

int __cdecl wcscoll(const wchar_t *a, const wchar_t *b)
{
    if (!a || !b) {
        MSVCRT_INVALID(EINVAL);
        return INT_MAX;
    }
    return wcscmp(a, b);
}

int __cdecl _wcsicoll(const wchar_t *a, const wchar_t *b)
{
    return _wcsicmp(a, b);
}

int __cdecl _wcsncoll(const wchar_t *a, const wchar_t *b, size_t n)
{
    return wcsncmp(a, b, n);
}

int __cdecl _wcsnicoll(const wchar_t *a, const wchar_t *b, size_t n)
{
    return _wcsnicmp(a, b, n);
}

size_t __cdecl strxfrm(char *dst, const char *src, size_t n)
{
    if (!src || (n && !dst)) {
        MSVCRT_INVALID(EINVAL);
        return INT_MAX;
    }
    size_t len = strlen(src);
    if (len < n)
        memcpy(dst, src, len + 1);
    else if (n) {
        MsvcrtSetErrno(ERANGE);
    }
    return len;
}

size_t __cdecl wcsxfrm(wchar_t *dst, const wchar_t *src, size_t n)
{
    if (!src || (n && !dst)) {
        MSVCRT_INVALID(EINVAL);
        return INT_MAX;
    }
    size_t len = wcslen(src);
    if (len < n)
        memcpy(dst, src, (len + 1) * sizeof(wchar_t));
    else if (n) {
        MsvcrtSetErrno(ERANGE);
    }
    return len;
}

/* ------------------------------------------------------------------ multibyte conversions */

/* in the c locale a byte and a wide character up to 0xff are the same value. */
int __cdecl mbtowc(wchar_t *dst, const char *src, size_t n)
{
    if (!src || !n)
        return 0;
    if (!*src) {
        if (dst)
            *dst = 0;
        return 0;
    }
    if (dst)
        *dst = (unsigned char)*src;
    return 1;
}

int __cdecl _mbtowc_l(wchar_t *dst, const char *src, size_t n, void *locale)
{
    return mbtowc(dst, src, n);
}

int __cdecl mblen(const char *s, size_t n)
{
    if (!s || !*s || !n)
        return 0;
    return 1;
}

int __cdecl _mblen_l(const char *s, size_t n, void *locale)
{
    return mblen(s, n);
}

int __cdecl wctomb(char *dst, wchar_t c)
{
    if (!dst)
        return 0;
    if (c > 0xff) {
        MsvcrtSetErrno(EILSEQ);
        return -1;
    }
    *dst = (char)c;
    return 1;
}

errno_t __cdecl wctomb_s(int *len, char *dst, size_t size, wchar_t c)
{
    if (!dst && size) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    if (!dst) {
        if (len)
            *len = 0;
        return 0;
    }
    if (c > 0xff) {
        if (len)
            *len = -1;
        MsvcrtSetErrno(EILSEQ);
        return EILSEQ;
    }
    if (!size) {
        if (len)
            *len = -1;
        MSVCRT_INVALID(ERANGE);
        return ERANGE;
    }
    *dst = (char)c;
    if (len)
        *len = 1;
    return 0;
}

size_t __cdecl mbstowcs(wchar_t *dst, const char *src, size_t n)
{
    if (!src) {
        MSVCRT_INVALID(EINVAL);
        return (size_t)-1;
    }
    if (!dst)
        return strlen(src);
    size_t i = 0;
    for (; i < n; i++) {
        dst[i] = (unsigned char)src[i];
        if (!src[i])
            return i;
    }
    return i;
}

size_t __cdecl _mbstowcs_l(wchar_t *dst, const char *src, size_t n, void *locale)
{
    return mbstowcs(dst, src, n);
}

errno_t __cdecl mbstowcs_s(size_t *converted, wchar_t *dst, size_t size, const char *src, size_t count)
{
    if ((!dst && size) || !src) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    size_t len = strlen(src);
    if (count != (size_t)-1 && count < len)
        len = count;
    if (!dst) {
        if (converted)
            *converted = len + 1;
        return 0;
    }
    errno_t r = 0;
    if (len >= size) {
        if (count == (size_t)-1) {
            len = size - 1;
            r = STRUNCATE;
        } else {
            *dst = 0;
            MSVCRT_INVALID(ERANGE);
            return ERANGE;
        }
    }
    for (size_t i = 0; i < len; i++)
        dst[i] = (unsigned char)src[i];
    dst[len] = 0;
    if (converted)
        *converted = len + 1;
    return r;
}

size_t __cdecl wcstombs(char *dst, const wchar_t *src, size_t n)
{
    if (!src) {
        MSVCRT_INVALID(EINVAL);
        return (size_t)-1;
    }
    size_t i = 0;
    if (!dst) {
        for (; src[i]; i++)
            if (src[i] > 0xff) {
                MsvcrtSetErrno(EILSEQ);
                return (size_t)-1;
            }
        return i;
    }
    for (; i < n; i++) {
        if (src[i] > 0xff) {
            MsvcrtSetErrno(EILSEQ);
            return (size_t)-1;
        }
        dst[i] = (char)src[i];
        if (!src[i])
            return i;
    }
    return i;
}

size_t __cdecl _wcstombs_l(char *dst, const wchar_t *src, size_t n, void *locale)
{
    return wcstombs(dst, src, n);
}

errno_t __cdecl wcstombs_s(size_t *converted, char *dst, size_t size, const wchar_t *src, size_t count)
{
    if ((!dst && size) || !src) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    size_t len = 0;
    while (src[len] && (count == (size_t)-1 || len < count)) {
        if (src[len] > 0xff) {
            if (dst)
                *dst = 0;
            MsvcrtSetErrno(EILSEQ);
            return EILSEQ;
        }
        len++;
    }
    if (!dst) {
        if (converted)
            *converted = len + 1;
        return 0;
    }
    errno_t r = 0;
    if (len >= size) {
        if (count == (size_t)-1) {
            len = size - 1;
            r = STRUNCATE;
        } else {
            *dst = 0;
            MSVCRT_INVALID(ERANGE);
            return ERANGE;
        }
    }
    for (size_t i = 0; i < len; i++)
        dst[i] = (char)src[i];
    dst[len] = 0;
    if (converted)
        *converted = len + 1;
    return r;
}

typedef int mbstate_t;

wint_t __cdecl btowc(int c)
{
    if (c == EOF)
        return WEOF;
    return (unsigned char)c;
}

int __cdecl wctob(wint_t c)
{
    return c <= 0xff ? (int)c : EOF;
}

size_t __cdecl mbrtowc(wchar_t *dst, const char *src, size_t n, mbstate_t *state)
{
    if (!src)
        return 0;
    if (!n)
        return (size_t)-2;
    if (dst)
        *dst = (unsigned char)*src;
    return *src ? 1 : 0;
}

size_t __cdecl mbrlen(const char *src, size_t n, mbstate_t *state)
{
    return mbrtowc(nullptr, src, n, state);
}

size_t __cdecl wcrtomb(char *dst, wchar_t c, mbstate_t *state)
{
    if (!dst)
        return 1;
    if (c > 0xff) {
        MsvcrtSetErrno(EILSEQ);
        return (size_t)-1;
    }
    *dst = (char)c;
    return 1;
}

errno_t __cdecl wcrtomb_s(size_t *len, char *dst, size_t size, wchar_t c, mbstate_t *state)
{
    int n;
    errno_t r = wctomb_s(&n, dst, size, c);
    if (len)
        *len = (size_t)n;
    return r;
}

int __cdecl mbsinit(const mbstate_t *state)
{
    return !state || !*state;
}

size_t __cdecl mbsrtowcs(wchar_t *dst, const char **src, size_t n, mbstate_t *state)
{
    if (!src || !*src) {
        MSVCRT_INVALID(EINVAL);
        return (size_t)-1;
    }
    const char *s = *src;
    if (!dst)
        return strlen(s);
    size_t i = 0;
    for (; i < n; i++) {
        dst[i] = (unsigned char)s[i];
        if (!s[i]) {
            *src = nullptr;
            return i;
        }
    }
    *src = s + i;
    return i;
}

size_t __cdecl wcsrtombs(char *dst, const wchar_t **src, size_t n, mbstate_t *state)
{
    if (!src || !*src) {
        MSVCRT_INVALID(EINVAL);
        return (size_t)-1;
    }
    const wchar_t *s = *src;
    size_t i = 0;
    if (!dst) {
        for (; s[i]; i++)
            if (s[i] > 0xff) {
                MsvcrtSetErrno(EILSEQ);
                return (size_t)-1;
            }
        return i;
    }
    for (; i < n; i++) {
        if (s[i] > 0xff) {
            *src = s + i;
            MsvcrtSetErrno(EILSEQ);
            return (size_t)-1;
        }
        dst[i] = (char)s[i];
        if (!s[i]) {
            *src = nullptr;
            return i;
        }
    }
    *src = s + i;
    return i;
}
