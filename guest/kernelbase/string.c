/*
 * character classification, case mapping and the lstr* string functions.
 */
#include "kernelbase_private.h"

/* ------------------------------------------------------------------ case mapping */

typedef struct CASE_RANGE {
    WCHAR First;
    WCHAR Last;
    SHORT Delta;  /* added to an upper case character to get its lower case form. */
    UCHAR Paired; /* 1: alternating upper and lower code points, upper on even ones; 2: upper on odd ones. */
} CASE_RANGE;

/* ranges of upper case letters and how they map to lower case. */
static const CASE_RANGE g_case_ranges[] = {
    {0x0041, 0x005A, 32, 0},  {0x00C0, 0x00D6, 32, 0},  {0x00D8, 0x00DE, 32, 0},  {0x0100, 0x012F, 1, 1},
    {0x0132, 0x0137, 1, 1},   {0x0139, 0x0148, 1, 2},   {0x014A, 0x0177, 1, 1},   {0x0179, 0x017E, 1, 2},
    {0x0386, 0x0386, 38, 0},  {0x0388, 0x038A, 37, 0},  {0x038C, 0x038C, 64, 0},  {0x038E, 0x038F, 63, 0},
    {0x0391, 0x03A1, 32, 0},  {0x03A3, 0x03AB, 32, 0},  {0x03E2, 0x03EF, 1, 1},   {0x0400, 0x040F, 80, 0},
    {0x0410, 0x042F, 32, 0},  {0x0460, 0x0481, 1, 1},   {0x048A, 0x04BF, 1, 1},   {0x04C1, 0x04CE, 1, 2},
    {0x04D0, 0x052F, 1, 1},   {0x0531, 0x0556, 48, 0},  {0x10A0, 0x10C5, 7264, 0}, {0x1E00, 0x1E95, 1, 1},
    {0x1EA0, 0x1EFF, 1, 1},   {0x1F08, 0x1F0F, -8, 0},  {0x1F18, 0x1F1D, -8, 0},  {0x1F28, 0x1F2F, -8, 0},
    {0x1F38, 0x1F3F, -8, 0},  {0x1F48, 0x1F4D, -8, 0},  {0x1F68, 0x1F6F, -8, 0},  {0x2160, 0x216F, 16, 0},
    {0x24B6, 0x24CF, 26, 0},  {0x2C00, 0x2C2E, 48, 0},  {0xFF21, 0xFF3A, 32, 0},
};

static BOOL range_contains(const CASE_RANGE *r, WCHAR c)
{
    if (c < r->First || c > r->Last)
        return FALSE;
    if (r->Paired == 1)
        return (c & 1) == 0;
    if (r->Paired == 2)
        return (c & 1) == 1;
    return TRUE;
}

WCHAR KbDowncase(WCHAR c)
{
    if (c < 0x80)
        return (c >= 'A' && c <= 'Z') ? (WCHAR)(c + 32) : c;
    if (c == 0x0178)
        return 0x00FF;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_case_ranges); i++)
        if (range_contains(&g_case_ranges[i], c))
            return (WCHAR)(c + g_case_ranges[i].Delta);
    return c;
}

WCHAR KbUpcase(WCHAR c)
{
    if (c < 0x80)
        return (c >= 'a' && c <= 'z') ? (WCHAR)(c - 32) : c;
    switch (c) {
    case 0x00FF:
        return 0x0178;
    case 0x0131:
        return 'I';
    case 0x03C2:
        return 0x03A3;
    case 0x00B5:
        return 0x039C;
    }
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_case_ranges); i++) {
        const CASE_RANGE *r = &g_case_ranges[i];
        WCHAR upper = (WCHAR)(c - r->Delta);
        if (upper != c && range_contains(r, upper))
            return upper;
    }
    return c;
}

/* ------------------------------------------------------------------ classification */

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

/* ------------------------------------------------------------------ lstr functions */

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
        return NULL;
    wcscpy(dst, src);
    return dst;
}

LPSTR WINAPI lstrcpyA(LPSTR dst, LPCSTR src)
{
    if (!dst || !src)
        return NULL;
    strcpy(dst, src);
    return dst;
}

LPWSTR WINAPI lstrcpynW(LPWSTR dst, LPCWSTR src, int count)
{
    if (!dst || (!src && count > 1))
        return NULL;
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
        return NULL;
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
        return NULL;
    wcscat(dst, src);
    return dst;
}

LPSTR WINAPI lstrcatA(LPSTR dst, LPCSTR src)
{
    if (!dst || !src)
        return NULL;
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


/* number times numerator over denominator through a 64 bit product, rounded half away from zero; -1 on a zero divisor or overflow. */
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
