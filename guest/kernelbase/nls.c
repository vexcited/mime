/*
 * code pages: the ansi and oem tables, utf-8, utf-7 and the conversion
 * functions between them and utf-16.
 */
#include "kernelbase_private.h"

typedef struct CODE_PAGE {
    UINT Id;
    UINT MaxCharSize;
    const WCHAR *High; /* 128 entries for bytes 0x80..0xFF, NULL when not a table code page. */
    const WCHAR *Name;
} CODE_PAGE;

static constexpr WCHAR g_cp1252[128] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D,
    0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A,
    0x0153, 0x009D, 0x017E, 0x0178, 0x00A0, 0x00A1, 0x00A2, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7, 0x00A8, 0x00A9,
    0x00AA, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x00AF, 0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
    0x00B8, 0x00B9, 0x00BA, 0x00BB, 0x00BC, 0x00BD, 0x00BE, 0x00BF, 0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4, 0x00C5,
    0x00C6, 0x00C7, 0x00C8, 0x00C9, 0x00CA, 0x00CB, 0x00CC, 0x00CD, 0x00CE, 0x00CF, 0x00D0, 0x00D1, 0x00D2, 0x00D3,
    0x00D4, 0x00D5, 0x00D6, 0x00D7, 0x00D8, 0x00D9, 0x00DA, 0x00DB, 0x00DC, 0x00DD, 0x00DE, 0x00DF, 0x00E0, 0x00E1,
    0x00E2, 0x00E3, 0x00E4, 0x00E5, 0x00E6, 0x00E7, 0x00E8, 0x00E9, 0x00EA, 0x00EB, 0x00EC, 0x00ED, 0x00EE, 0x00EF,
    0x00F0, 0x00F1, 0x00F2, 0x00F3, 0x00F4, 0x00F5, 0x00F6, 0x00F7, 0x00F8, 0x00F9, 0x00FA, 0x00FB, 0x00FC, 0x00FD,
    0x00FE, 0x00FF,
};

#include <mime/cp437.h>

/* the glyphs MB_USEGLYPHCHARS substitutes for the oem control characters 0x00..0x1F. */
static constexpr WCHAR g_cp437_glyphs[32] = {
    0x0000, 0x263A, 0x263B, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022, 0x25D8, 0x25CB, 0x25D9,
    0x2642, 0x2640, 0x266A, 0x266B, 0x263C, 0x25BA, 0x25C4, 0x2195, 0x203C, 0x00B6, 0x00A7,
    0x25AC, 0x21A8, 0x2191, 0x2193, 0x2192, 0x2190, 0x221F, 0x2194, 0x25B2, 0x25BC,
};

static WCHAR g_latin1[128];
static WCHAR g_ascii_high[128];

static const CODE_PAGE g_code_pages[] = {
    {1252, 1, g_cp1252, L"1252  (ANSI - Latin I)"},
    {437, 1, g_cp437, L"437   (OEM - United States)"},
    {28591, 1, g_latin1, L"28591 (ISO 8859-1 Latin I)"},
    {20127, 1, g_ascii_high, L"20127 (US-ASCII)"},
    {CP_UTF8, 4, nullptr, L"65001 (UTF-8)"},
    {CP_UTF7, 5, nullptr, L"65000 (UTF-7)"},
    {CP_SYMBOL, 1, nullptr, L"42    (Symbol)"},
};

static UINT g_acp = 1252;
static UINT g_oemcp = 437;

void KbInitializeNls(void)
{
    for (UINT i = 0; i < 128; i++) {
        g_latin1[i] = (WCHAR)(0x80 + i);
        g_ascii_high[i] = (WCHAR)i;
    }
    PPEB peb = KbPeb();
    if (peb->ActiveCodePage)
        g_acp = peb->ActiveCodePage;
    if (peb->OemCodePage)
        g_oemcp = peb->OemCodePage;
}

static UINT resolve_code_page(UINT cp)
{
    switch (cp) {
    case CP_ACP:
    case CP_THREAD_ACP:
        return g_acp;
    case CP_OEMCP:
        return g_oemcp;
    default:
        return cp;
    }
}

static const CODE_PAGE *find_code_page(UINT cp)
{
    cp = resolve_code_page(cp);
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_code_pages); i++)
        if (g_code_pages[i].Id == cp)
            return &g_code_pages[i];
    return nullptr;
}

UINT WINAPI GetACP(void)
{
    return g_acp;
}

UINT WINAPI GetOEMCP(void)
{
    return g_oemcp;
}

BOOL WINAPI IsValidCodePage(UINT cp)
{
    /* the pseudo code pages are not valid code page numbers here. */
    if (cp == CP_ACP || cp == CP_OEMCP || cp == CP_MACCP || cp == CP_THREAD_ACP)
        return FALSE;
    return find_code_page(cp) != nullptr;
}

BOOL WINAPI GetCPInfo(UINT cp, LPCPINFO info)
{
    if (!info) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    const CODE_PAGE *page = find_code_page(cp);
    if (!page) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    memset(info, 0, sizeof(*info));
    info->MaxCharSize = page->MaxCharSize;
    info->DefaultChar[0] = '?';
    return TRUE;
}

BOOL WINAPI GetCPInfoExW(UINT cp, DWORD flags, LPCPINFOEXW info)
{
    if (!info) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    const CODE_PAGE *page = find_code_page(cp);
    if (!page) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    memset(info, 0, sizeof(*info));
    info->MaxCharSize = page->MaxCharSize;
    info->DefaultChar[0] = '?';
    info->UnicodeDefaultChar = (page->Id == CP_UTF8 || page->Id == CP_UTF7) ? 0xFFFD : '?';
    info->CodePage = page->Id;
    wcscpy(info->CodePageName, page->Name);
    return TRUE;
}

BOOL WINAPI GetCPInfoExA(UINT cp, DWORD flags, LPCPINFOEXA info)
{
    CPINFOEXW w;
    if (!info) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!GetCPInfoExW(cp, flags, &w))
        return FALSE;
    memcpy(info, &w, offsetof(CPINFOEXA, CodePageName));
    WideCharToMultiByte(CP_ACP, 0, w.CodePageName, -1, info->CodePageName, MAX_PATH, nullptr, nullptr);
    return TRUE;
}

BOOL WINAPI EnumSystemCodePagesW(CODEPAGE_ENUMPROCW proc, DWORD flags)
{
    if (!proc) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_code_pages); i++) {
        WCHAR buf[16];
        KbFormat(buf, RTL_NUMBER_OF(buf), L"%u", g_code_pages[i].Id);
        if (!proc(buf))
            break;
    }
    return TRUE;
}

BOOL WINAPI EnumSystemCodePagesA(CODEPAGE_ENUMPROCA proc, DWORD flags)
{
    if (!proc) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_code_pages); i++) {
        char buf[16];
        KbFormatA(buf, sizeof(buf), "%u", g_code_pages[i].Id);
        if (!proc(buf))
            break;
    }
    return TRUE;
}

BOOL WINAPI IsDBCSLeadByte(BYTE c)
{
    return FALSE;
}

BOOL WINAPI IsDBCSLeadByteEx(UINT cp, BYTE c)
{
    if (!find_code_page(cp)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ output helper */

typedef struct WIDE_OUT {
    LPWSTR Buffer;
    int Size; /* 0 means only count. */
    int Count;
    BOOL Overflow;
} WIDE_OUT;

static void put_wide(WIDE_OUT *o, WCHAR c)
{
    if (o->Size) {
        if (o->Count >= o->Size) {
            o->Overflow = TRUE;
            return;
        }
        o->Buffer[o->Count] = c;
    }
    o->Count++;
}

typedef struct BYTE_OUT {
    LPSTR Buffer;
    int Size;
    int Count;
    BOOL Overflow;
} BYTE_OUT;

static void put_byte(BYTE_OUT *o, CHAR c)
{
    if (o->Size) {
        if (o->Count >= o->Size) {
            o->Overflow = TRUE;
            return;
        }
        o->Buffer[o->Count] = c;
    }
    o->Count++;
}

/* ------------------------------------------------------------------ utf-8 */

static BOOL utf8_to_wide(const UCHAR *s, int n, WIDE_OUT *o, BOOL strict)
{
    int i = 0;
    while (i < n && !o->Overflow) {
        UCHAR b = s[i];
        if (b < 0x80) {
            put_wide(o, b);
            i++;
            continue;
        }
        ULONG cp;
        int need;
        UCHAR lo = 0x80, hi = 0xBF;
        if (b >= 0xC2 && b <= 0xDF) {
            need = 1;
            cp = b & 0x1F;
        } else if (b >= 0xE0 && b <= 0xEF) {
            need = 2;
            cp = b & 0x0F;
            if (b == 0xE0)
                lo = 0xA0;
            else if (b == 0xED)
                hi = 0x9F;
        } else if (b >= 0xF0 && b <= 0xF4) {
            need = 3;
            cp = b & 0x07;
            if (b == 0xF0)
                lo = 0x90;
            else if (b == 0xF4)
                hi = 0x8F;
        } else {
            if (strict)
                return FALSE;
            put_wide(o, 0xFFFD);
            i++;
            continue;
        }
        /* consume the maximal valid prefix; an incomplete sequence becomes one replacement character. */
        int j = 1;
        for (; j <= need && i + j < n; j++) {
            UCHAR c = s[i + j];
            if (c < lo || c > hi)
                break;
            lo = 0x80;
            hi = 0xBF;
            cp = (cp << 6) | (c & 0x3F);
        }
        if (j <= need) {
            if (strict)
                return FALSE;
            put_wide(o, 0xFFFD);
            i += j;
            continue;
        }
        i += need + 1;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            put_wide(o, (WCHAR)(0xD800 + (cp >> 10)));
            put_wide(o, (WCHAR)(0xDC00 + (cp & 0x3FF)));
        } else {
            put_wide(o, (WCHAR)cp);
        }
    }
    return TRUE;
}

static BOOL wide_to_utf8(const WCHAR *s, int n, BYTE_OUT *o, BOOL strict)
{
    for (int i = 0; i < n && !o->Overflow; i++) {
        ULONG cp = s[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < n && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            i++;
        } else if (cp >= 0xD800 && cp <= 0xDFFF) {
            if (strict)
                return FALSE;
            cp = 0xFFFD;
        }
        if (cp < 0x80) {
            put_byte(o, (CHAR)cp);
        } else if (cp < 0x800) {
            put_byte(o, (CHAR)(0xC0 | (cp >> 6)));
            put_byte(o, (CHAR)(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            put_byte(o, (CHAR)(0xE0 | (cp >> 12)));
            put_byte(o, (CHAR)(0x80 | ((cp >> 6) & 0x3F)));
            put_byte(o, (CHAR)(0x80 | (cp & 0x3F)));
        } else {
            put_byte(o, (CHAR)(0xF0 | (cp >> 18)));
            put_byte(o, (CHAR)(0x80 | ((cp >> 12) & 0x3F)));
            put_byte(o, (CHAR)(0x80 | ((cp >> 6) & 0x3F)));
            put_byte(o, (CHAR)(0x80 | (cp & 0x3F)));
        }
    }
    return TRUE;
}

/* ------------------------------------------------------------------ utf-7 */

static constexpr char g_base64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int base64_value(UCHAR c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

static void utf7_to_wide(const UCHAR *s, int n, WIDE_OUT *o)
{
    int i = 0;
    while (i < n && !o->Overflow) {
        if (s[i] != '+') {
            put_wide(o, s[i++]);
            continue;
        }
        i++;
        if (i < n && s[i] == '-') {
            put_wide(o, '+');
            i++;
            continue;
        }
        ULONG bits = 0;
        int count = 0;
        while (i < n) {
            int v = base64_value(s[i]);
            if (v < 0)
                break;
            bits = (bits << 6) | (ULONG)v;
            count += 6;
            if (count >= 16) {
                count -= 16;
                put_wide(o, (WCHAR)(bits >> count));
                bits &= (1u << count) - 1;
            }
            i++;
        }
        if (i < n && s[i] == '-')
            i++;
    }
}

static BOOL utf7_direct(WCHAR c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
        return TRUE;
    /* the terminator is copied as is too. */
    return !c || (c < 0x80 && strchr("'(),-./:? \t\r\n!\"#$%&*;<=>@[]^_`{|}", (char)c) != nullptr);
}

static void wide_to_utf7(const WCHAR *s, int n, BYTE_OUT *o)
{
    int i = 0;
    while (i < n && !o->Overflow) {
        WCHAR c = s[i];
        if (c == '+') {
            put_byte(o, '+');
            put_byte(o, '-');
            i++;
            continue;
        }
        if (utf7_direct(c)) {
            put_byte(o, (CHAR)c);
            i++;
            continue;
        }
        put_byte(o, '+');
        ULONG bits = 0;
        int count = 0;
        while (i < n && !utf7_direct(s[i]) && s[i] != '+') {
            bits = (bits << 16) | s[i++];
            count += 16;
            while (count >= 6) {
                count -= 6;
                put_byte(o, g_base64[(bits >> count) & 0x3F]);
            }
        }
        if (count)
            put_byte(o, g_base64[(bits << (6 - count)) & 0x3F]);
        /* the terminating '-' is needed when the next character could be read as base64. */
        if (i >= n || base64_value((UCHAR)s[i]) >= 0 || s[i] == '-')
            put_byte(o, '-');
    }
}

/* ------------------------------------------------------------------ best fit */

static constexpr char g_best_fit_latin1[] = "AAAAAA?CEEEEIIIIDNOOOOO?OUUUUY??aaaaaa?ceeeeiiiidnooooo?ouuuuy?y";
static const char g_best_fit_latin_a[] =
    "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIi??JjKkkLlLlLlLlLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUu"
    "UuUuUuWwYyYZzZzZzs";
MIME_STATIC_ASSERT(sizeof(g_best_fit_latin1) == 65, "latin1 best fit");
MIME_STATIC_ASSERT(sizeof(g_best_fit_latin_a) == 129, "latin extended best fit");

static WCHAR best_fit(WCHAR c)
{
    if (c >= 0xC0 && c <= 0xFF)
        return (WCHAR)(UCHAR)g_best_fit_latin1[c - 0xC0];
    if (c >= 0x100 && c <= 0x17F)
        return (WCHAR)(UCHAR)g_best_fit_latin_a[c - 0x100];
    if (c >= 0xFF01 && c <= 0xFF5E)
        return (WCHAR)(c - 0xFF01 + 0x21);
    switch (c) {
    case 0x00A0:
    case 0x2002:
    case 0x2003:
    case 0x2009:
    case 0x3000:
        return ' ';
    case 0x2010:
    case 0x2011:
    case 0x2012:
    case 0x2013:
    case 0x2014:
    case 0x2212:
        return '-';
    case 0x2018:
    case 0x2019:
    case 0x201A:
    case 0x2032:
        return '\'';
    case 0x201C:
    case 0x201D:
    case 0x201E:
    case 0x2033:
        return '"';
    case 0x2022:
    case 0x2026:
        return '.';
    case 0x00A9:
        return 'c';
    case 0x00AE:
        return 'R';
    case 0x00AB:
        return '<';
    case 0x00BB:
        return '>';
    default:
        return '?';
    }
}

/* ------------------------------------------------------------------ single byte tables */

static WCHAR sbcs_to_wide(const CODE_PAGE *page, UCHAR b, DWORD flags)
{
    if (page->Id == CP_SYMBOL)
        return b < 0x20 ? b : (WCHAR)(0xF000 + b);
    if (b < 0x80) {
        if ((flags & MB_USEGLYPHCHARS) && page->High == g_cp437) {
            if (b < 0x20 && b)
                return g_cp437_glyphs[b];
            if (b == 0x7F)
                return 0x2302;
        }
        return b;
    }
    return page->High[b - 0x80];
}

/* returns the byte for a character, or -1 when the code page cannot represent it. */
static int wide_to_sbcs(const CODE_PAGE *page, WCHAR c)
{
    if (page->Id == CP_SYMBOL) {
        if (c < 0x20)
            return c;
        if (c >= 0xF020 && c <= 0xF0FF)
            return c - 0xF000;
        return -1;
    }
    if (c < 0x80)
        return c;
    for (int i = 0; i < 128; i++)
        if (page->High[i] == c)
            return 0x80 + i;
    return -1;
}

/* ------------------------------------------------------------------ public conversions */

int WINAPI MultiByteToWideChar(UINT cp, DWORD flags, LPCCH src, int src_len, LPWSTR dst, int dst_len)
{
    if (!src || !src_len || dst_len < 0 || (dst_len && !dst) || (dst_len && (const void *)src == (void *)dst)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    const CODE_PAGE *page = find_code_page(cp);
    if (!page) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (page->Id == CP_UTF7 || page->Id == CP_SYMBOL) {
        if (flags) {
            KbSetError(ERROR_INVALID_FLAGS);
            return 0;
        }
    } else if (page->Id == CP_UTF8) {
        if (flags & ~(DWORD)MB_ERR_INVALID_CHARS) {
            KbSetError(ERROR_INVALID_FLAGS);
            return 0;
        }
    } else if ((flags & ~(DWORD)(MB_PRECOMPOSED | MB_COMPOSITE | MB_USEGLYPHCHARS | MB_ERR_INVALID_CHARS)) ||
               ((flags & MB_PRECOMPOSED) && (flags & MB_COMPOSITE))) {
        KbSetError(ERROR_INVALID_FLAGS);
        return 0;
    }
    if (src_len < 0)
        src_len = (int)strlen(src) + 1;
    WIDE_OUT o = {dst, dst_len, 0, FALSE};
    const UCHAR *s = (const UCHAR *)src;
    if (page->Id == CP_UTF8) {
        if (!utf8_to_wide(s, src_len, &o, (flags & MB_ERR_INVALID_CHARS) != 0)) {
            KbSetError(ERROR_NO_UNICODE_TRANSLATION);
            return 0;
        }
    } else if (page->Id == CP_UTF7) {
        utf7_to_wide(s, src_len, &o);
    } else {
        for (int i = 0; i < src_len && !o.Overflow; i++)
            put_wide(&o, sbcs_to_wide(page, s[i], flags));
    }
    if (o.Overflow) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    return o.Count;
}

int WINAPI WideCharToMultiByte(UINT cp, DWORD flags, LPCWCH src, int src_len, LPSTR dst, int dst_len,
                               LPCCH default_char, LPBOOL used_default)
{
    if (!src || !src_len || dst_len < 0 || (dst_len && !dst) || (dst_len && (const void *)src == (void *)dst)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    const CODE_PAGE *page = find_code_page(cp);
    if (!page) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    BOOL unicode = page->Id == CP_UTF8 || page->Id == CP_UTF7;
    if (unicode) {
        if (default_char || used_default) {
            KbSetError(ERROR_INVALID_PARAMETER);
            return 0;
        }
        if ((page->Id == CP_UTF7 && flags) || (page->Id == CP_UTF8 && (flags & ~(DWORD)WC_ERR_INVALID_CHARS))) {
            KbSetError(ERROR_INVALID_FLAGS);
            return 0;
        }
    } else if (flags & ~(DWORD)(WC_COMPOSITECHECK | WC_DISCARDNS | WC_SEPCHARS | WC_DEFAULTCHAR |
                                WC_NO_BEST_FIT_CHARS)) {
        KbSetError(ERROR_INVALID_FLAGS);
        return 0;
    }
    if (src_len < 0)
        src_len = (int)wcslen(src) + 1;
    BYTE_OUT o = {dst, dst_len, 0, FALSE};
    if (page->Id == CP_UTF8) {
        if (!wide_to_utf8(src, src_len, &o, (flags & WC_ERR_INVALID_CHARS) != 0)) {
            KbSetError(ERROR_NO_UNICODE_TRANSLATION);
            return 0;
        }
    } else if (page->Id == CP_UTF7) {
        wide_to_utf7(src, src_len, &o);
    } else {
        CHAR fallback = default_char ? *default_char : '?';
        BOOL used = FALSE;
        for (int i = 0; i < src_len && !o.Overflow; i++) {
            WCHAR c = src[i];
            int b = wide_to_sbcs(page, c);
            if (b < 0 && !(flags & WC_NO_BEST_FIT_CHARS) && !default_char) {
                WCHAR fit = best_fit(c);
                if (fit != '?')
                    b = wide_to_sbcs(page, fit);
            }
            if (b < 0) {
                /* a surrogate pair yields a single default character. */
                if (c >= 0xD800 && c <= 0xDBFF && i + 1 < src_len && src[i + 1] >= 0xDC00 && src[i + 1] <= 0xDFFF)
                    i++;
                used = TRUE;
                b = (UCHAR)fallback;
            }
            put_byte(&o, (CHAR)b);
        }
        if (used_default)
            *used_default = used;
    }
    if (o.Overflow) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    return o.Count;
}
