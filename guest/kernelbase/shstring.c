/*
 * the shlwapi string functions kernelbase exports: searching, comparing, copying and number
 * parsing, wide and ansi, plus QISearch and indirect strings.
 */
#include "shlwapi_kb.h"

/* ------------------------------------------------------------------ characters */

/* an ansi character as a WORD, the lead byte high for a double byte one. */
static WORD ansi_char(LPCSTR p)
{
    if (IsDBCSLeadByte((BYTE)p[0]) && p[1])
        return (WORD)(((BYTE)p[0] << 8) | (BYTE)p[1]);
    return (BYTE)p[0];
}

static int ansi_char_to_wide(WORD c, WCHAR *out)
{
    char bytes[3];
    int n = 0;
    if (c > 0xFF)
        bytes[n++] = (char)(c >> 8);
    bytes[n++] = (char)c;
    int count = MultiByteToWideChar(CP_ACP, 0, bytes, n, out, 2);
    return count > 0 ? count : 0;
}

static BOOL wide_equal_nocase(LPCWSTR a, int alen, LPCWSTR b, int blen)
{
    return CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, a, alen, b, blen) == CSTR_EQUAL;
}

/* zero when the characters match without case, as the C runtime compares. */
LWSTDAPI BOOL WINAPI ChrCmpIW(WCHAR c1, WCHAR c2)
{
    return !wide_equal_nocase(&c1, 1, &c2, 1);
}

LWSTDAPI BOOL WINAPI ChrCmpIA(WORD c1, WORD c2)
{
    WCHAR w1[2], w2[2];
    int n1 = ansi_char_to_wide(c1, w1), n2 = ansi_char_to_wide(c2, w2);
    if (!n1 || !n2)
        return c1 != c2;
    return !wide_equal_nocase(w1, n1, w2, n2);
}

static WORD ctype1(WCHAR c)
{
    WORD type = 0;
    GetStringTypeW(CT_CTYPE1, &c, 1, &type);
    return type;
}

LWSTDAPI BOOL WINAPI IsCharSpaceW(WCHAR c)
{
    return (ctype1(c) & C1_SPACE) != 0;
}

LWSTDAPI BOOL WINAPI IsCharSpaceA(CHAR c)
{
    WCHAR w;
    if (MultiByteToWideChar(CP_ACP, 0, &c, 1, &w, 1) != 1)
        return FALSE;
    return IsCharSpaceW(w);
}

LWSTDAPI BOOL WINAPI IsCharBlankW(WCHAR c)
{
    return (ctype1(c) & C1_BLANK) != 0;
}

LWSTDAPI BOOL WINAPI IsCharCntrlW(WCHAR c)
{
    return (ctype1(c) & C1_CNTRL) != 0;
}

LWSTDAPI BOOL WINAPI IsCharDigitW(WCHAR c)
{
    return (ctype1(c) & C1_DIGIT) != 0;
}

LWSTDAPI BOOL WINAPI IsCharPunctW(WCHAR c)
{
    return (ctype1(c) & C1_PUNCT) != 0;
}

LWSTDAPI BOOL WINAPI IsCharXDigitW(WCHAR c)
{
    return (ctype1(c) & C1_XDIGIT) != 0;
}

/* ------------------------------------------------------------------ finding characters */

LWSTDAPI LPWSTR WINAPI StrChrW(LPCWSTR string, WCHAR c)
{
    if (!string)
        return nullptr;
    for (; *string; string++)
        if (*string == c)
            return (LPWSTR)string;
    return nullptr;
}

LWSTDAPI LPWSTR WINAPI StrChrIW(LPCWSTR string, WCHAR c)
{
    if (!string)
        return nullptr;
    for (; *string; string++)
        if (!ChrCmpIW(*string, c))
            return (LPWSTR)string;
    return nullptr;
}

LWSTDAPI LPWSTR WINAPI StrChrNW(LPCWSTR string, WCHAR c, UINT max)
{
    if (!string)
        return nullptr;
    for (; max && *string; string++, max--)
        if (*string == c)
            return (LPWSTR)string;
    return nullptr;
}

LWSTDAPI LPWSTR WINAPI StrChrNIW(LPCWSTR string, WCHAR c, UINT max)
{
    if (!string)
        return nullptr;
    for (; max && *string; string++, max--)
        if (!ChrCmpIW(*string, c))
            return (LPWSTR)string;
    return nullptr;
}

LWSTDAPI LPSTR WINAPI StrChrA(LPCSTR string, WORD c)
{
    if (!string)
        return nullptr;
    for (; *string; string = KbShNextA(string))
        if (ansi_char(string) == c)
            return (LPSTR)string;
    return nullptr;
}

LWSTDAPI LPSTR WINAPI StrChrIA(LPCSTR string, WORD c)
{
    if (!string)
        return nullptr;
    for (; *string; string = KbShNextA(string))
        if (!ChrCmpIA(ansi_char(string), c))
            return (LPSTR)string;
    return nullptr;
}

/* the last occurrence before end, or before the terminator without one. */
LWSTDAPI LPWSTR WINAPI StrRChrW(LPCWSTR string, LPCWSTR end, WCHAR c)
{
    if (!string)
        return nullptr;
    if (!end)
        end = string + wcslen(string);
    LPCWSTR found = nullptr;
    for (; string < end && *string; string++)
        if (*string == c)
            found = string;
    return (LPWSTR)found;
}

LWSTDAPI LPWSTR WINAPI StrRChrIW(LPCWSTR string, LPCWSTR end, WCHAR c)
{
    if (!string)
        return nullptr;
    if (!end)
        end = string + wcslen(string);
    LPCWSTR found = nullptr;
    for (; string < end && *string; string++)
        if (!ChrCmpIW(*string, c))
            found = string;
    return (LPWSTR)found;
}

LWSTDAPI LPSTR WINAPI StrRChrA(LPCSTR string, LPCSTR end, WORD c)
{
    if (!string)
        return nullptr;
    if (!end)
        end = string + strlen(string);
    LPCSTR found = nullptr;
    for (; string < end && *string; string = KbShNextA(string))
        if (ansi_char(string) == c)
            found = string;
    return (LPSTR)found;
}

LWSTDAPI LPSTR WINAPI StrRChrIA(LPCSTR string, LPCSTR end, WORD c)
{
    if (!string)
        return nullptr;
    if (!end)
        end = string + strlen(string);
    LPCSTR found = nullptr;
    for (; string < end && *string; string = KbShNextA(string))
        if (!ChrCmpIA(ansi_char(string), c))
            found = string;
    return (LPSTR)found;
}

LWSTDAPI LPWSTR WINAPI StrPBrkW(LPCWSTR string, LPCWSTR set)
{
    if (!string || !set)
        return nullptr;
    for (; *string; string++)
        if (StrChrW(set, *string))
            return (LPWSTR)string;
    return nullptr;
}

LWSTDAPI LPSTR WINAPI StrPBrkA(LPCSTR string, LPCSTR set)
{
    if (!string || !set)
        return nullptr;
    for (; *string; string = KbShNextA(string))
        if (StrChrA(set, ansi_char(string)))
            return (LPSTR)string;
    return nullptr;
}

LWSTDAPI int WINAPI StrSpnW(LPCWSTR string, LPCWSTR set)
{
    if (!string || !set)
        return 0;
    LPCWSTR p = string;
    while (*p && StrChrW(set, *p))
        p++;
    return (int)(p - string);
}

LWSTDAPI int WINAPI StrSpnA(LPCSTR string, LPCSTR set)
{
    if (!string || !set)
        return 0;
    LPCSTR p = string;
    while (*p && StrChrA(set, ansi_char(p)))
        p = KbShNextA(p);
    return (int)(p - string);
}

LWSTDAPI int WINAPI StrCSpnW(LPCWSTR string, LPCWSTR set)
{
    if (!string || !set)
        return 0;
    LPCWSTR p = string;
    while (*p && !StrChrW(set, *p))
        p++;
    return (int)(p - string);
}

LWSTDAPI int WINAPI StrCSpnIW(LPCWSTR string, LPCWSTR set)
{
    if (!string || !set)
        return 0;
    LPCWSTR p = string;
    while (*p && !StrChrIW(set, *p))
        p++;
    return (int)(p - string);
}

LWSTDAPI int WINAPI StrCSpnA(LPCSTR string, LPCSTR set)
{
    if (!string || !set)
        return 0;
    LPCSTR p = string;
    while (*p && !StrChrA(set, ansi_char(p)))
        p = KbShNextA(p);
    return (int)(p - string);
}

LWSTDAPI int WINAPI StrCSpnIA(LPCSTR string, LPCSTR set)
{
    if (!string || !set)
        return 0;
    LPCSTR p = string;
    while (*p && !StrChrIA(set, ansi_char(p)))
        p = KbShNextA(p);
    return (int)(p - string);
}

/* ------------------------------------------------------------------ comparing */

static int compare(DWORD flags, LPCWSTR a, int alen, LPCWSTR b, int blen)
{
    return CompareStringW(LOCALE_USER_DEFAULT, flags, a, alen, b, blen) - CSTR_EQUAL;
}

static int bounded(LPCWSTR s, int count)
{
    int n = 0;
    while (n < count && s[n])
        n++;
    return n;
}

static int bounded_a(LPCSTR s, int count)
{
    int n = 0;
    while (n < count && s[n])
        n++;
    return n;
}

static int compare_a(DWORD flags, LPCSTR a, int alen, LPCSTR b, int blen)
{
    return CompareStringA(LOCALE_USER_DEFAULT, flags, a, alen, b, blen) - CSTR_EQUAL;
}

LWSTDAPI int WINAPI StrCmpW(LPCWSTR s1, LPCWSTR s2)
{
    return compare(0, s1, -1, s2, -1);
}

LWSTDAPI int WINAPI StrCmpIW(LPCWSTR s1, LPCWSTR s2)
{
    return compare(NORM_IGNORECASE, s1, -1, s2, -1);
}

LWSTDAPI int WINAPI StrCmpNW(LPCWSTR s1, LPCWSTR s2, int count)
{
    if (count <= 0)
        return 0;
    return compare(0, s1, bounded(s1, count), s2, bounded(s2, count));
}

LWSTDAPI int WINAPI StrCmpNIW(LPCWSTR s1, LPCWSTR s2, int count)
{
    if (count <= 0)
        return 0;
    return compare(NORM_IGNORECASE, s1, bounded(s1, count), s2, bounded(s2, count));
}

LWSTDAPI int WINAPI StrCmpNA(LPCSTR s1, LPCSTR s2, int count)
{
    if (count <= 0)
        return 0;
    return compare_a(0, s1, bounded_a(s1, count), s2, bounded_a(s2, count));
}

LWSTDAPI int WINAPI StrCmpNIA(LPCSTR s1, LPCSTR s2, int count)
{
    if (count <= 0)
        return 0;
    return compare_a(NORM_IGNORECASE, s1, bounded_a(s1, count), s2, bounded_a(s2, count));
}

/* the "C" forms compare code points, the "I" ones folding ascii case. */
static int ordinal(LPCWSTR a, LPCWSTR b, int count, BOOL fold)
{
    for (; count; a++, b++, count--) {
        WCHAR ca = *a, cb = *b;
        if (fold) {
            ca = KbUpcase(ca);
            cb = KbUpcase(cb);
        }
        if (ca != cb)
            return ca < cb ? -1 : 1;
        if (!ca)
            break;
    }
    return 0;
}

static int ordinal_a(LPCSTR a, LPCSTR b, int count, BOOL fold)
{
    for (; count; a++, b++, count--) {
        BYTE ca = (BYTE)*a, cb = (BYTE)*b;
        if (fold) {
            if (ca >= 'a' && ca <= 'z')
                ca -= 32;
            if (cb >= 'a' && cb <= 'z')
                cb -= 32;
        }
        if (ca != cb)
            return ca < cb ? -1 : 1;
        if (!ca)
            break;
    }
    return 0;
}

LWSTDAPI int WINAPI StrCmpCW(LPCWSTR s1, LPCWSTR s2)
{
    return ordinal(s1, s2, -1, FALSE);
}

LWSTDAPI int WINAPI StrCmpICW(LPCWSTR s1, LPCWSTR s2)
{
    return ordinal(s1, s2, -1, TRUE);
}

LWSTDAPI int WINAPI StrCmpNCW(LPCWSTR s1, LPCWSTR s2, int count)
{
    return count > 0 ? ordinal(s1, s2, count, FALSE) : 0;
}

LWSTDAPI int WINAPI StrCmpNICW(LPCWSTR s1, LPCWSTR s2, int count)
{
    return count > 0 ? ordinal(s1, s2, count, TRUE) : 0;
}

LWSTDAPI int WINAPI StrCmpCA(LPCSTR s1, LPCSTR s2)
{
    return ordinal_a(s1, s2, -1, FALSE);
}

LWSTDAPI int WINAPI StrCmpICA(LPCSTR s1, LPCSTR s2)
{
    return ordinal_a(s1, s2, -1, TRUE);
}

LWSTDAPI int WINAPI StrCmpNCA(LPCSTR s1, LPCSTR s2, int count)
{
    return count > 0 ? ordinal_a(s1, s2, count, FALSE) : 0;
}

LWSTDAPI int WINAPI StrCmpNICA(LPCSTR s1, LPCSTR s2, int count)
{
    return count > 0 ? ordinal_a(s1, s2, count, TRUE) : 0;
}

/* digit runs compare by value and letters without case, as explorer sorts names. */
LWSTDAPI int WINAPI StrCmpLogicalW(LPCWSTR s1, LPCWSTR s2)
{
    return compare(SORT_DIGITSASNUMBERS | NORM_IGNORECASE | NORM_LINGUISTIC_CASING, s1, -1, s2, -1);
}

LWSTDAPI BOOL WINAPI StrIsIntlEqualW(BOOL case_sensitive, LPCWSTR s1, LPCWSTR s2, int count)
{
    if (!s1 || !s2)
        return FALSE;
    int a = count < 0 ? -1 : bounded(s1, count), b = count < 0 ? -1 : bounded(s2, count);
    return compare(case_sensitive ? 0 : NORM_IGNORECASE, s1, a, s2, b) == 0;
}

LWSTDAPI BOOL WINAPI StrIsIntlEqualA(BOOL case_sensitive, LPCSTR s1, LPCSTR s2, int count)
{
    if (!s1 || !s2)
        return FALSE;
    int a = count < 0 ? -1 : bounded_a(s1, count), b = count < 0 ? -1 : bounded_a(s2, count);
    return compare_a(case_sensitive ? 0 : NORM_IGNORECASE, s1, a, s2, b) == 0;
}

/* ------------------------------------------------------------------ finding strings */

LWSTDAPI LPWSTR WINAPI StrStrW(LPCWSTR string, LPCWSTR search)
{
    if (!string || !search)
        return nullptr;
    SIZE_T length = wcslen(search);
    for (; *string; string++)
        if (*string == *search && !wcsncmp(string, search, length))
            return (LPWSTR)string;
    return nullptr;
}

LWSTDAPI LPWSTR WINAPI StrStrIW(LPCWSTR string, LPCWSTR search)
{
    if (!string || !search)
        return nullptr;
    int length = (int)wcslen(search);
    for (; *string; string++)
        if (!ChrCmpIW(*string, *search) && wide_equal_nocase(string, bounded(string, length), search, length))
            return (LPWSTR)string;
    return nullptr;
}

LWSTDAPI LPWSTR WINAPI StrStrNW(LPCWSTR string, LPCWSTR search, UINT max)
{
    if (!string || !search)
        return nullptr;
    UINT length = (UINT)wcslen(search);
    for (; max >= length && *string; string++, max--)
        if (!wcsncmp(string, search, length))
            return (LPWSTR)string;
    return nullptr;
}

LWSTDAPI LPWSTR WINAPI StrStrNIW(LPCWSTR string, LPCWSTR search, UINT max)
{
    if (!string || !search)
        return nullptr;
    UINT length = (UINT)wcslen(search);
    for (; max >= length && *string; string++, max--)
        if (wide_equal_nocase(string, bounded(string, (int)length), search, (int)length))
            return (LPWSTR)string;
    return nullptr;
}

LWSTDAPI LPSTR WINAPI StrStrA(LPCSTR string, LPCSTR search)
{
    if (!string || !search)
        return nullptr;
    SIZE_T length = strlen(search);
    for (; *string; string = KbShNextA(string))
        if (!strncmp(string, search, length))
            return (LPSTR)string;
    return nullptr;
}

LWSTDAPI LPSTR WINAPI StrStrIA(LPCSTR string, LPCSTR search)
{
    if (!string || !search)
        return nullptr;
    int length = (int)strlen(search);
    for (; *string; string = KbShNextA(string))
        if (!compare_a(NORM_IGNORECASE, string, bounded_a(string, length), search, length))
            return (LPSTR)string;
    return nullptr;
}

/* the last match starting before end. */
LWSTDAPI LPWSTR WINAPI StrRStrIW(LPCWSTR string, LPCWSTR end, LPCWSTR search)
{
    if (!string || !search || !*search)
        return nullptr;
    if (!end)
        end = string + wcslen(string);
    int length = (int)wcslen(search);
    LPCWSTR found = nullptr;
    for (LPCWSTR p = string; p < end && *p; p++)
        if (wide_equal_nocase(p, bounded(p, length), search, length))
            found = p;
    return (LPWSTR)found;
}

LWSTDAPI LPSTR WINAPI StrRStrIA(LPCSTR string, LPCSTR end, LPCSTR search)
{
    if (!string || !search || !*search)
        return nullptr;
    if (!end)
        end = string + strlen(string);
    int length = (int)strlen(search);
    LPCSTR found = nullptr;
    for (LPCSTR p = string; p < end && *p; p = KbShNextA(p))
        if (!compare_a(NORM_IGNORECASE, p, bounded_a(p, length), search, length))
            found = p;
    return (LPSTR)found;
}

/* ------------------------------------------------------------------ copying */

LWSTDAPI LPWSTR WINAPI StrCpyNW(LPWSTR dest, LPCWSTR src, int size)
{
    if (!dest || size <= 0)
        return dest;
    KbShCopy(dest, (SIZE_T)size, src ? src : L"");
    return dest;
}

/* the terminator of what was copied. */
LWSTDAPI LPWSTR WINAPI StrCpyNXW(LPWSTR dest, LPCWSTR src, int size)
{
    if (!dest || !src || size <= 0)
        return dest;
    while (size > 1 && *src) {
        *dest++ = *src++;
        size--;
    }
    *dest = 0;
    return dest;
}

LWSTDAPI LPSTR WINAPI StrCpyNXA(LPSTR dest, LPCSTR src, int size)
{
    if (!dest || !src || size <= 0)
        return dest;
    while (size > 1 && *src) {
        *dest++ = *src++;
        size--;
    }
    *dest = 0;
    return dest;
}

LWSTDAPI LPWSTR WINAPI StrCatBuffW(LPWSTR dest, LPCWSTR src, int size)
{
    if (!dest || !src || size <= 0)
        return dest;
    SIZE_T length = wcsnlen(dest, (SIZE_T)size);
    if (length < (SIZE_T)size)
        KbShCopy(dest + length, (SIZE_T)size - length, src);
    return dest;
}

LWSTDAPI LPSTR WINAPI StrCatBuffA(LPSTR dest, LPCSTR src, int size)
{
    if (!dest || !src || size <= 0)
        return dest;
    SIZE_T length = strnlen(dest, (SIZE_T)size);
    if (length < (SIZE_T)size)
        lstrcpynA(dest + length, src, size - (int)length);
    return dest;
}

/* src appended at index at, the end of dest when at is -1; the index of the new terminator. */
LWSTDAPI DWORD WINAPI StrCatChainW(LPWSTR dest, DWORD size, DWORD at, LPCWSTR src)
{
    if (!size)
        return 0;
    if (at == (DWORD)-1)
        at = (DWORD)wcsnlen(dest, size);
    if (at >= size)
        at = size - 1;
    while (at < size - 1 && src && *src)
        dest[at++] = *src++;
    dest[at] = 0;
    return at;
}

/* a LocalAlloc copy; a missing string gives an empty one. */
LWSTDAPI LPWSTR WINAPI StrDupW(LPCWSTR string)
{
    SIZE_T bytes = ((string ? wcslen(string) : 0) + 1) * sizeof(WCHAR);
    LPWSTR copy = LocalAlloc(LMEM_FIXED, bytes);
    if (copy) {
        if (string)
            memcpy(copy, string, bytes);
        else
            copy[0] = 0;
    }
    return copy;
}

LWSTDAPI LPSTR WINAPI StrDupA(LPCSTR string)
{
    SIZE_T bytes = (string ? strlen(string) : 0) + 1;
    LPSTR copy = LocalAlloc(LMEM_FIXED, bytes);
    if (copy) {
        if (string)
            memcpy(copy, string, bytes);
        else
            copy[0] = 0;
    }
    return copy;
}

/* ------------------------------------------------------------------ trimming */

LWSTDAPI BOOL WINAPI StrTrimW(LPWSTR string, LPCWSTR trim)
{
    if (!string || !trim)
        return FALSE;
    BOOL trimmed = FALSE;
    LPWSTR start = string;
    while (*start && StrChrW(trim, *start))
        start++;
    LPWSTR tail = nullptr;
    for (LPWSTR p = start; *p; p++) {
        if (StrChrW(trim, *p)) {
            if (!tail)
                tail = p;
        } else {
            tail = nullptr;
        }
    }
    if (tail) {
        *tail = 0;
        trimmed = TRUE;
    }
    if (start > string) {
        memmove(string, start, (wcslen(start) + 1) * sizeof(WCHAR));
        trimmed = TRUE;
    }
    return trimmed;
}

LWSTDAPI BOOL WINAPI StrTrimA(LPSTR string, LPCSTR trim)
{
    if (!string || !trim)
        return FALSE;
    BOOL trimmed = FALSE;
    LPSTR start = string;
    while (*start && StrChrA(trim, ansi_char(start)))
        start = KbShNextA(start);
    LPSTR tail = nullptr;
    for (LPSTR p = start; *p; p = KbShNextA(p)) {
        if (StrChrA(trim, ansi_char(p))) {
            if (!tail)
                tail = p;
        } else {
            tail = nullptr;
        }
    }
    if (tail) {
        *tail = 0;
        trimmed = TRUE;
    }
    if (start > string) {
        memmove(string, start, strlen(start) + 1);
        trimmed = TRUE;
    }
    return trimmed;
}

/* ------------------------------------------------------------------ numbers */

/* spaces, tabs and line feeds, a sign, then decimal digits or "0x" and hex digits that drop the
   sign; at least one digit makes it TRUE, and the value wraps. */
static BOOL parse_int64(LPCWSTR string, DWORD flags, LONGLONG *value)
{
    if (!string)
        return FALSE;
    while (*string == L' ' || *string == L'\t' || *string == L'\n')
        string++;
    BOOL negative = FALSE;
    if (*string == L'+' || *string == L'-') {
        negative = *string == L'-';
        string++;
    }
    ULONGLONG result = 0;
    BOOL digits = FALSE;
    if ((flags & STIF_SUPPORT_HEX) && string[0] == L'0' && (string[1] == L'x' || string[1] == L'X')) {
        negative = FALSE;
        for (string += 2;; string++) {
            WCHAR c = *string;
            int digit = c >= L'0' && c <= L'9' ? c - L'0'
                        : c >= L'a' && c <= L'f' ? c - L'a' + 10
                        : c >= L'A' && c <= L'F' ? c - L'A' + 10
                                                 : -1;
            if (digit < 0)
                break;
            result = result * 16 + (ULONGLONG)digit;
            digits = TRUE;
        }
    } else {
        for (; *string >= L'0' && *string <= L'9'; string++) {
            result = result * 10 + (ULONGLONG)(*string - L'0');
            digits = TRUE;
        }
    }
    if (value)
        *value = negative ? -(LONGLONG)result : (LONGLONG)result;
    return digits;
}

LWSTDAPI BOOL WINAPI StrToInt64ExW(LPCWSTR string, DWORD flags, LONGLONG *value)
{
    return parse_int64(string, flags, value);
}

LWSTDAPI BOOL WINAPI StrToInt64ExA(LPCSTR string, DWORD flags, LONGLONG *value)
{
    if (!string)
        return FALSE;
    PWSTR w = KbShAnsiToWide(string);
    BOOL result = w && parse_int64(w, flags, value);
    KbFree(w);
    return result;
}

LWSTDAPI BOOL WINAPI StrToIntExW(LPCWSTR string, DWORD flags, int *value)
{
    LONGLONG wide = 0;
    BOOL result = parse_int64(string, flags, &wide);
    if (value)
        *value = result ? (int)wide : 0;
    return result;
}

LWSTDAPI BOOL WINAPI StrToIntExA(LPCSTR string, DWORD flags, int *value)
{
    LONGLONG wide = 0;
    BOOL result = StrToInt64ExA(string, flags, &wide);
    if (value)
        *value = result ? (int)wide : 0;
    return result;
}

/* an optional minus and decimal digits, no spaces or plus. */
LWSTDAPI int WINAPI StrToIntW(LPCWSTR string)
{
    if (!string)
        return 0;
    BOOL negative = *string == L'-';
    if (negative)
        string++;
    int result = 0;
    for (; *string >= L'0' && *string <= L'9'; string++)
        result = result * 10 + (*string - L'0');
    return negative ? -result : result;
}

LWSTDAPI int WINAPI StrToIntA(LPCSTR string)
{
    if (!string)
        return 0;
    BOOL negative = *string == '-';
    if (negative)
        string++;
    int result = 0;
    for (; *string >= '0' && *string <= '9'; string++)
        result = result * 10 + (*string - '0');
    return negative ? -result : result;
}

/* ------------------------------------------------------------------ the rest */

/* cuts an ansi string to fit size characters without splitting a double byte one; the length
   kept with its terminator. */
LWSTDAPI int WINAPI SHTruncateString(LPSTR string, UINT size)
{
    if (!string || !size)
        return 0;
    LPSTR p = string, last = string;
    while (*p && (UINT)(p - string) < size - 1) {
        last = KbShNextA(p);
        if ((UINT)(last - string) > size - 1)
            break;
        p = last;
    }
    *p = 0;
    return (int)(p - string) + 1;
}

/* the first entry of the table for riid, or of the table for IUnknown; E_POINTER without out. */
LWSTDAPI HRESULT WINAPI QISearch(void *base, LPCQITAB table, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    LPCQITAB entry = table;
    for (; entry->piid; entry++)
        if (IsEqualIID(riid, entry->piid))
            break;
    if (!entry->piid) {
        if (!IsEqualIID(riid, &IID_IUnknown)) {
            *out = nullptr;
            return E_NOINTERFACE;
        }
        entry = table;
    }
    IUnknown *object = (IUnknown *)((BYTE *)base + entry->dwOffset);
    object->lpVtbl->AddRef(object);
    *out = object;
    return S_OK;
}

/* string id in a module's RT_STRING table, read without user32. */
static HRESULT load_string(HMODULE module, UINT id, LPWSTR out, UINT size)
{
    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW((id >> 4) + 1), (LPCWSTR)RT_STRING);
    if (!resource)
        return HRESULT_FROM_WIN32(ERROR_RESOURCE_NAME_NOT_FOUND);
    const WCHAR *table = LockResource(LoadResource(module, resource));
    if (!table)
        return HRESULT_FROM_WIN32(ERROR_RESOURCE_DATA_NOT_FOUND);
    for (UINT i = 0; i < (id & 15); i++)
        table += *table + 1;
    UINT length = *table;
    if (!length)
        return HRESULT_FROM_WIN32(ERROR_RESOURCE_NAME_NOT_FOUND);
    if (length >= size)
        length = size - 1;
    memcpy(out, table + 1, length * sizeof(WCHAR));
    out[length] = 0;
    return S_OK;
}

/* "@file,-id" with an optional ";version" read from the file's string table, anything else
   copied as it is. */
LWSTDAPI HRESULT WINAPI SHLoadIndirectString(LPCWSTR source, LPWSTR out, UINT size, void **reserved)
{
    if (!source || !out || !size)
        return E_INVALIDARG;
    if (source[0] != L'@')
        return KbShCopy(out, size, source) == S_OK ? S_OK : E_POINTER;
    WCHAR spec[MAX_PATH * 2];
    if (FAILED(KbShCopy(spec, RTL_NUMBER_OF(spec), source + 1)))
        return E_INVALIDARG;
    LPWSTR semicolon = wcschr(spec, L';');
    if (semicolon)
        *semicolon = 0;
    LPWSTR comma = wcsrchr(spec, L',');
    if (!comma || comma[1] != L'-')
        return E_FAIL;
    *comma = 0;
    UINT id = (UINT)StrToIntW(comma + 2);
    WCHAR file[MAX_PATH];
    if (!ExpandEnvironmentStringsW(spec, file, MAX_PATH))
        return HRESULT_FROM_WIN32(GetLastError());
    HMODULE module = LoadLibraryExW(file, nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!module)
        return HRESULT_FROM_WIN32(GetLastError());
    HRESULT hr = load_string(module, id, out, size);
    FreeLibrary(module);
    if (FAILED(hr))
        *out = 0;
    return hr;
}
