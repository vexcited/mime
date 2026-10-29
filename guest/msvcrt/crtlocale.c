/*
 * the string helpers of the old c runtime that other programs call through msvcrt: they map, compare and classify
 * strings with the national language functions of kernel32. the locale argument is not read, as this runtime has
 * the c locale only.
 */
#include "msvcrt_private.h"

extern unsigned int __lc_codepage;

/* the length of a string that may stop early, counted up to a limit. */
static int bounded_length(const char *s, int count)
{
    int n = 0;
    while (n < count && s[n])
        n++;
    return n;
}

static int bounded_wide_length(const wchar_t *s, int count)
{
    int n = 0;
    while (n < count && s[n])
        n++;
    return n;
}

int __cdecl __crtLCMapStringW(void *locale, LCID lcid, DWORD flags, const wchar_t *source, int count, wchar_t *dest,
                              int dest_count)
{
    if (count > 0)
        count = bounded_wide_length(source, count);
    return LCMapStringW(lcid, flags, source, count, dest, dest_count);
}

int __cdecl __crtGetStringTypeW(void *locale, DWORD type, const wchar_t *source, int count, WORD *types)
{
    return count >= -1 && GetStringTypeW(type, source, count, types);
}

int __cdecl __crtCompareStringW(void *locale, LCID lcid, DWORD flags, const wchar_t *a, int a_count,
                                const wchar_t *b, int b_count)
{
    if (a_count > 0)
        a_count = bounded_wide_length(a, a_count);
    if (b_count > 0)
        b_count = bounded_wide_length(b, b_count);
    if (a_count && b_count)
        return CompareStringW(lcid, flags, a, a_count, b, b_count);
    /* an empty side orders by the lengths alone. */
    return a_count == b_count ? CSTR_EQUAL : a_count < b_count ? CSTR_LESS_THAN : CSTR_GREATER_THAN;
}

static wchar_t *widen(UINT code_page, DWORD flags, const char *source, int count, int *wide_count)
{
    int n = MultiByteToWideChar(code_page, flags, source, count, nullptr, 0);
    if (n < 1)
        return nullptr;
    wchar_t *wide = malloc(n * sizeof(wchar_t));
    if (wide && !MultiByteToWideChar(code_page, MB_PRECOMPOSED, source, count, wide, n)) {
        free(wide);
        return nullptr;
    }
    *wide_count = n;
    return wide;
}

int __cdecl __crtLCMapStringA(void *locale, LCID lcid, DWORD flags, const char *source, int count, char *dest,
                              int dest_count, int code_page, int strict)
{
    if (count > 0)
        count = bounded_length(source, count);
    UINT cp = code_page ? code_page : __lc_codepage;
    int wide_count, result = 0;
    wchar_t *wide = widen(cp, strict ? MB_PRECOMPOSED | MB_ERR_INVALID_CHARS : MB_PRECOMPOSED, source, count, &wide_count);
    if (!wide)
        return 0;
    int mapped = LCMapStringW(lcid, flags, wide, wide_count, nullptr, 0);
    if (mapped) {
        if (flags & LCMAP_SORTKEY) {
            /* a sort key is bytes already. */
            if (dest_count && mapped <= dest_count)
                LCMapStringW(lcid, flags, wide, wide_count, (wchar_t *)dest, dest_count);
            result = mapped;
        } else {
            wchar_t *text = malloc(mapped * sizeof(wchar_t));
            if (text && LCMapStringW(lcid, flags, wide, wide_count, text, mapped))
                result = WideCharToMultiByte(cp, 0, text, mapped, dest, dest_count, nullptr, nullptr);
            free(text);
        }
    }
    free(wide);
    return result;
}

int __cdecl __crtCompareStringA(void *locale, LCID lcid, DWORD flags, const char *a, int a_count, const char *b,
                                int b_count, int code_page)
{
    if (a_count < -1 || b_count < -1)
        return 0;
    if (a_count > 0)
        a_count = bounded_length(a, a_count);
    if (b_count > 0)
        b_count = bounded_length(b, b_count);
    UINT cp = code_page ? code_page : __lc_codepage;
    if (!a_count || !b_count) {
        if (a_count == b_count)
            return CSTR_EQUAL;
        if (b_count > 1)
            return CSTR_LESS_THAN;
        if (a_count > 1)
            return CSTR_GREATER_THAN;
        CPINFO info;
        if (!GetCPInfo(cp, &info))
            return 0;
        if (a_count > 0 || b_count > 0) {
            /* a lone lead byte is not a character, so it counts as equal to nothing. */
            const char *only = a_count > 0 ? a : b;
            if (info.MaxCharSize >= 2)
                for (int i = 0; info.LeadByte[i] && info.LeadByte[i + 1]; i += 2)
                    if ((BYTE)*only >= info.LeadByte[i] && (BYTE)*only <= info.LeadByte[i + 1])
                        return CSTR_EQUAL;
            return a_count > 0 ? CSTR_GREATER_THAN : CSTR_LESS_THAN;
        }
    }
    int a_wide, b_wide, result = 0;
    wchar_t *wa = widen(cp, MB_PRECOMPOSED | MB_ERR_INVALID_CHARS, a, a_count, &a_wide);
    wchar_t *wb = wa ? widen(cp, MB_PRECOMPOSED | MB_ERR_INVALID_CHARS, b, b_count, &b_wide) : nullptr;
    if (wa && wb)
        result = CompareStringW(lcid, flags, wa, a_wide, wb, b_wide);
    free(wa);
    free(wb);
    return result;
}
