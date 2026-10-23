/* character classification, case mapping and the lstr* functions. */
#include "kernelbase_private.h"

/* simple Unicode case mappings. */
WCHAR KbDowncase(WCHAR c)
{
    return (WCHAR)KbUniLower(c);
}

WCHAR KbUpcase(WCHAR c)
{
    return (WCHAR)KbUniUpper(c);
}

WORD KbCharType1(WCHAR c)
{
    WORD t = 0;
    if (c < 0x20 || (c >= 0x7F && c < 0xA0)) {
        t = C1_CNTRL;
        if (c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r')
            t |= C1_SPACE;
        if (c == '\t')
            t |= C1_BLANK;
        return t | C1_DEFINED;
    }
    if (c == ' ' || c == 0xA0 || c == 0x3000 || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 ||
        c == 0x202F || c == 0x205F || c == 0x1680)
        return C1_SPACE | C1_BLANK | C1_DEFINED;
    if (c >= '0' && c <= '9')
        return C1_DIGIT | C1_XDIGIT | C1_DEFINED;
    if ((c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))
        t |= C1_XDIGIT;
    if (c >= 0xFF10 && c <= 0xFF19)
        return C1_DIGIT | C1_DEFINED;
    if (c >= 0xD800 && c <= 0xDFFF)
        return 0;
    if (KbDowncase(c) != c)
        return t | C1_UPPER | C1_ALPHA | C1_DEFINED;
    if (KbUpcase(c) != c || c == 0xDF || c == 0xAA || c == 0xBA || c == 0x0138 || c == 0x0149)
        return t | C1_LOWER | C1_ALPHA | C1_DEFINED;
    if (c < 0x80)
        return (c >= '!' && c <= '~') ? C1_PUNCT | C1_DEFINED : C1_DEFINED;
    if (c >= 0xA1 && c <= 0xBF)
        return C1_PUNCT | C1_DEFINED;
    if (c == 0xD7 || c == 0xF7)
        return C1_PUNCT | C1_DEFINED;
    if ((c >= 0x2010 && c <= 0x2027) || (c >= 0x2030 && c <= 0x205E) || (c >= 0x20A0 && c <= 0x20CF) ||
        (c >= 0x2100 && c <= 0x214F && c != 0x2126) || (c >= 0x2190 && c <= 0x2BFF) || (c >= 0x3001 && c <= 0x3003) ||
        (c >= 0x3008 && c <= 0x3011) || (c >= 0xFF01 && c <= 0xFF0F) || (c >= 0xFF1A && c <= 0xFF20) ||
        (c >= 0xFE30 && c <= 0xFE6F))
        return C1_PUNCT | C1_DEFINED;
    /* combining marks are neither letters nor punctuation. */
    if ((c >= 0x0300 && c <= 0x036F) || (c >= 0x20D0 && c <= 0x20FF) || (c >= 0xFE20 && c <= 0xFE2F))
        return C1_DEFINED;
    if (c >= 0xE000 && c <= 0xF8FF)
        return C1_DEFINED;
    return C1_ALPHA | C1_DEFINED;
}

int WINAPI lstrlenW(LPCWSTR s)
{
    return s ? (int)wcslen(s) : 0;
}

int WINAPI lstrlenA(LPCSTR s)
{
    return s ? (int)strlen(s) : 0;
}

LPWSTR WINAPI lstrcpyW(LPWSTR dst, LPCWSTR src)
{
    if (!dst || !src)
        return nullptr;
    wcscpy(dst, src);
    return dst;
}

LPSTR WINAPI lstrcpyA(LPSTR dst, LPCSTR src)
{
    if (!dst || !src)
        return nullptr;
    strcpy(dst, src);
    return dst;
}

LPWSTR WINAPI lstrcpynW(LPWSTR dst, LPCWSTR src, int count)
{
    if (!dst || (!src && count > 1))
        return nullptr;
    if (count <= 0)
        return dst;
    int i = 0;
    for (; i < count - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = 0;
    return dst;
}

LPSTR WINAPI lstrcpynA(LPSTR dst, LPCSTR src, int count)
{
    if (!dst || (!src && count > 1))
        return nullptr;
    if (count <= 0)
        return dst;
    int i = 0;
    for (; i < count - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = 0;
    return dst;
}

LPWSTR WINAPI lstrcatW(LPWSTR dst, LPCWSTR src)
{
    if (!dst || !src)
        return nullptr;
    wcscat(dst, src);
    return dst;
}

LPSTR WINAPI lstrcatA(LPSTR dst, LPCSTR src)
{
    if (!dst || !src)
        return nullptr;
    strcat(dst, src);
    return dst;
}

int WINAPI lstrcmpW(LPCWSTR a, LPCWSTR b)
{
    if (!a || !b)
        return a ? 1 : b ? -1 : 0;
    return CompareStringW(LOCALE_USER_DEFAULT, 0, a, -1, b, -1) - CSTR_EQUAL;
}

int WINAPI lstrcmpiW(LPCWSTR a, LPCWSTR b)
{
    if (!a || !b)
        return a ? 1 : b ? -1 : 0;
    return CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, a, -1, b, -1) - CSTR_EQUAL;
}

int WINAPI lstrcmpA(LPCSTR a, LPCSTR b)
{
    if (!a || !b)
        return a ? 1 : b ? -1 : 0;
    return CompareStringA(LOCALE_USER_DEFAULT, 0, a, -1, b, -1) - CSTR_EQUAL;
}

int WINAPI lstrcmpiA(LPCSTR a, LPCSTR b)
{
    if (!a || !b)
        return a ? 1 : b ? -1 : 0;
    return CompareStringA(LOCALE_USER_DEFAULT, NORM_IGNORECASE, a, -1, b, -1) - CSTR_EQUAL;
}


/* rounds half away from zero, returns -1 on a zero divisor or overflow. */
int WINAPI MulDiv(int number, int numerator, int denominator)
{
    if (!denominator)
        return -1;
    LONGLONG product = (LONGLONG)number * numerator;
    LONGLONG divisor = denominator;
    if (divisor < 0) {
        divisor = -divisor;
        product = -product;
    }
    LONGLONG half = divisor / 2;
    LONGLONG result = product >= 0 ? (product + half) / divisor : (product - half) / divisor;
    if (result > 0x7FFFFFFF || result < -0x7FFFFFFFLL - 1)
        return -1;
    return (int)result;
}
