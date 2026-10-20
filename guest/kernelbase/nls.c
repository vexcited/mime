/*
 * code pages: the tables of the ansi, oem and mac code pages, utf-8, utf-7 and the conversion functions between them and
 * utf-16. The tables come from tools/nls/gencp.py.
 */
#include "kernelbase_private.h"
#include "nls_cpdata.h"

#define CP_ALIAS_EUC_KR 51949
#define CP_ALIAS_EUC_KR_TARGET 20949

static UINT g_acp = 1252;
static UINT g_oemcp = 437;

void KbInitializeNls(void)
{
    PPEB peb = KbPeb();
    if (peb->ActiveCodePage)
        g_acp = peb->ActiveCodePage;
    if (peb->OemCodePage)
        g_oemcp = peb->OemCodePage;
}

/* ------------------------------------------------------------------ the tables */

static const NLS_CPDATA *find_table_page(UINT cp)
{
    if (cp == CP_ALIAS_EUC_KR)
        cp = CP_ALIAS_EUC_KR_TARGET;
    LONG lo = 0, hi = (LONG)RTL_NUMBER_OF(g_nls_codepages) - 1;
    while (lo <= hi) {
        LONG mid = (lo + hi) / 2;
        if (g_nls_codepages[mid].Id == cp)
            return &g_nls_codepages[mid];
        if (g_nls_codepages[mid].Id < cp)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return nullptr;
}

static WCHAR single_to_wide(const NLS_CPDATA *p, UINT b)
{
    return g_nls_to_wide[p->ToWide + b];
}

static BOOL is_lead(const NLS_CPDATA *p, UINT b)
{
    return p->Kind != NLS_KIND_SBCS && g_nls_lead_row[p->LeadRow + b] != 0;
}

/* the character of a lead and trail byte, or 0xFFFF. */
static WCHAR pair_to_wide(const NLS_CPDATA *p, UINT lead, UINT trail)
{
    UINT row = g_nls_lead_row[p->LeadRow + lead];
    if (!row || trail < p->TrailMin || trail > p->TrailMax)
        return 0xFFFF;
    return g_nls_rows[p->Rows + (row - 1) * (p->TrailMax - p->TrailMin + 1) + (trail - p->TrailMin)];
}

static BOOL is_undefined_byte(const NLS_CPDATA *p, UINT b)
{
    return (g_nls_undefined[p->Undefined + (b >> 5)] >> (b & 31)) & 1;
}

/* the byte or double byte code of a character, or -1. */
static LONG wide_to_code(const NLS_CPDATA *p, WCHAR c)
{
    UINT block = g_nls_dir[p->Dir + (c >> 8)];
    if (block == 0xFFFF)
        return -1;
    UINT v = p->Kind == NLS_KIND_SBCS ? g_nls_pages8[block * 256 + (c & 255)] : g_nls_pages16[block * 256 + (c & 255)];
    return (!v && c) ? -1 : (LONG)v;
}

/* whether a code stands for the character itself and not for a best fit of it. */
static BOOL code_round_trips(const NLS_CPDATA *p, WCHAR c, ULONG code)
{
    if (code > 0xFF)
        return pair_to_wide(p, code >> 8, code & 255) == c;
    if (is_lead(p, code))
        return FALSE;
    return single_to_wide(p, code) == c;
}

/* the pseudo code pages and the code page of the locale. */
UINT KbLocaleCodePage(LCID lcid, UINT kind);

static UINT resolve_code_page(UINT cp)
{
    switch (cp) {
    case CP_ACP:
        return g_acp;
    case CP_OEMCP:
        return g_oemcp;
    case CP_MACCP:
        return KbLocaleCodePage(LOCALE_SYSTEM_DEFAULT, 2);
    case CP_THREAD_ACP:
        return KbLocaleCodePage(NtCurrentTeb()->CurrentLocale, 0);
    default:
        return cp;
    }
}

UINT KbResolveCodePage(UINT cp)
{
    return resolve_code_page(cp);
}

/* the table code pages and the unicode ones, which GetCPInfo knows. */
static BOOL known_code_page(UINT cp)
{
    return cp == CP_UTF8 || cp == CP_UTF7 || find_table_page(cp) != nullptr;
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
    /* the pseudo code pages and the symbol one are not valid code page numbers here. */
    if (cp <= CP_THREAD_ACP || cp == CP_SYMBOL)
        return FALSE;
    return known_code_page(cp);
}

static const WCHAR *code_page_name(const NLS_CPDATA *p, UINT cp)
{
    if (p)
        return p->Name;
    return cp == CP_UTF8 ? L"65001 (UTF-8)" : L"65000 (UTF-7)";
}

static BOOL fill_cp_info(UINT cp, LPCPINFO info, WCHAR *unicode_default, UINT *number, const WCHAR **name)
{
    UINT resolved = resolve_code_page(cp);
    if (!info || !known_code_page(resolved)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    const NLS_CPDATA *p = find_table_page(resolved);
    memset(info, 0, sizeof(*info));
    info->DefaultChar[0] = '?';
    if (p) {
        info->MaxCharSize = p->Kind == NLS_KIND_SBCS ? 1 : p->Kind == NLS_KIND_GB18030 ? 4 : 2;
        info->DefaultChar[0] = (BYTE)p->DefaultChar;
        memcpy(info->LeadByte, p->LeadBytes, MAX_LEADBYTES);
        *unicode_default = p->UnicodeDefault;
    } else {
        info->MaxCharSize = resolved == CP_UTF8 ? 4 : 5;
        *unicode_default = 0xFFFD;
    }
    *number = resolved;
    *name = code_page_name(p, resolved);
    return TRUE;
}

BOOL WINAPI GetCPInfo(UINT cp, LPCPINFO info)
{
    WCHAR u;
    UINT n;
    const WCHAR *name;
    return fill_cp_info(cp, info, &u, &n, &name);
}

BOOL WINAPI GetCPInfoExW(UINT cp, DWORD flags, LPCPINFOEXW info)
{
    if (!info) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR u;
    UINT n;
    const WCHAR *name;
    CPINFO basic;
    UINT resolved = resolve_code_page(cp);
    if (!fill_cp_info(cp, &basic, &u, &n, &name))
        return FALSE;
    /* the unicode code pages answer whatever the flags are. */
    if (flags && resolved != CP_UTF8 && resolved != CP_UTF7) {
        KbSetError(ERROR_INVALID_FLAGS);
        return FALSE;
    }
    memset(info, 0, sizeof(*info));
    info->MaxCharSize = basic.MaxCharSize;
    memcpy(info->DefaultChar, basic.DefaultChar, sizeof(info->DefaultChar));
    memcpy(info->LeadByte, basic.LeadByte, sizeof(info->LeadByte));
    info->UnicodeDefaultChar = u;
    info->CodePage = n;
    wcscpy(info->CodePageName, name);
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
    if (flags != CP_INSTALLED && flags != CP_SUPPORTED) {
        KbSetError(ERROR_INVALID_FLAGS);
        return FALSE;
    }
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_nls_codepages); i++) {
        WCHAR buf[16];
        KbFormat(buf, RTL_NUMBER_OF(buf), L"%u", g_nls_codepages[i].Id);
        if (!proc(buf))
            return TRUE;
    }
    static const UINT unicode[] = {CP_UTF7, CP_UTF8};
    for (ULONG i = 0; i < RTL_NUMBER_OF(unicode); i++) {
        WCHAR buf[16];
        KbFormat(buf, RTL_NUMBER_OF(buf), L"%u", unicode[i]);
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
    if (flags != CP_INSTALLED && flags != CP_SUPPORTED) {
        KbSetError(ERROR_INVALID_FLAGS);
        return FALSE;
    }
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_nls_codepages); i++) {
        char buf[16];
        KbFormatA(buf, sizeof(buf), "%u", g_nls_codepages[i].Id);
        if (!proc(buf))
            return TRUE;
    }
    static const UINT unicode[] = {CP_UTF7, CP_UTF8};
    for (ULONG i = 0; i < RTL_NUMBER_OF(unicode); i++) {
        char buf[16];
        KbFormatA(buf, sizeof(buf), "%u", unicode[i]);
        if (!proc(buf))
            break;
    }
    return TRUE;
}

BOOL WINAPI IsDBCSLeadByte(BYTE c)
{
    const NLS_CPDATA *p = find_table_page(g_acp);
    return p && is_lead(p, c);
}

BOOL WINAPI IsDBCSLeadByteEx(UINT cp, BYTE c)
{
    UINT resolved = resolve_code_page(cp);
    if (!known_code_page(resolved)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    const NLS_CPDATA *p = find_table_page(resolved);
    return p && is_lead(p, c);
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

/* ------------------------------------------------------------------ table code pages to utf-16 */

/* the characters the control codes of an oem code page show as, for MB_USEGLYPHCHARS. */
static constexpr WCHAR g_glyphs[32] = {
    0x0000, 0x263A, 0x263B, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022, 0x25D8, 0x25CB, 0x25D9,
    0x2642, 0x2640, 0x266A, 0x266B, 0x263C, 0x25BA, 0x25C4, 0x2195, 0x203C, 0x00B6, 0x00A7,
    0x25AC, 0x21A8, 0x2191, 0x2193, 0x2192, 0x2190, 0x221F, 0x2194, 0x25B2, 0x25BC,
};

/*
 * The decomposition MB_COMPOSITE makes: the canonical one of Unicode but for a few characters. The letters with a stroke
 * and the Greek ones with a tonos have another mark, and a few have none.
 */
typedef struct COMPOSITE_FORM {
    WCHAR Char;
    WCHAR Base;
    WCHAR Mark;
} COMPOSITE_FORM;

static const COMPOSITE_FORM g_composite_forms[] = {
    {0x00D8, 0x004F, 0x0338}, {0x00F8, 0x006F, 0x0338}, {0x0110, 0x0044, 0x0335}, {0x0111, 0x0064, 0x0335},
    {0x0126, 0x0048, 0x0335}, {0x0127, 0x0068, 0x0335}, {0x0141, 0x004C, 0x0337}, {0x0142, 0x006C, 0x0337},
    {0x0166, 0x0054, 0x0335}, {0x0167, 0x0074, 0x0335}, {0x0386, 0x0391, 0x030D}, {0x0388, 0x0395, 0x030D},
    {0x0389, 0x0397, 0x030D}, {0x038A, 0x0399, 0x030D}, {0x038C, 0x039F, 0x030D}, {0x038E, 0x03A5, 0x030D},
    {0x038F, 0x03A9, 0x030D}, {0x0390, 0x03B9, 0x0344}, {0x03AC, 0x03B1, 0x030D}, {0x03AD, 0x03B5, 0x030D},
    {0x03AE, 0x03B7, 0x030D}, {0x03AF, 0x03B9, 0x030D}, {0x03B0, 0x03C5, 0x0344}, {0x03CC, 0x03BF, 0x030D},
    {0x03CD, 0x03C5, 0x030D}, {0x03CE, 0x03C9, 0x030D},
};

/* the character a base and a mark compose to, or 0. */
static ULONG compose_pair(WCHAR base, WCHAR mark)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_composite_forms); i++)
        if (g_composite_forms[i].Base == base && g_composite_forms[i].Mark == mark)
            return g_composite_forms[i].Char;
    ULONG r = KbUniCompose(base, mark, FALSE);
    if (!r)
        return 0;
    /* the composites Windows decomposes another way are not composed this way. */
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_composite_forms); i++)
        if (g_composite_forms[i].Char == r)
            return 0;
    return r;
}

/* the characters Windows leaves whole although Unicode decomposes them. */
static BOOL composite_exempt(WCHAR c)
{
    return c == 0x0385 || (c >= 0x0622 && c <= 0x0626) || c == 0x2260;
}

static int composite_parts(WCHAR c, WCHAR *out)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_composite_forms); i++)
        if (g_composite_forms[i].Char == c) {
            out[0] = g_composite_forms[i].Base;
            out[1] = g_composite_forms[i].Mark;
            return 2;
        }
    if (composite_exempt(c))
        return 0;
    int n = KbUniDecompose(c, FALSE, out);
    /* a character whose decomposition is another one, such as the ohm sign, stays as it is. */
    return n > 1 ? n : 0;
}

/* one character of a table, decomposed when the caller asked for it. */
static void put_table_wide(WIDE_OUT *o, WCHAR c, DWORD flags)
{
    if ((flags & MB_COMPOSITE) && c >= 0xC0) {
        WCHAR parts[20];
        int n = composite_parts(c, parts);
        if (n) {
            for (int i = 0; i < n; i++)
                put_wide(o, parts[i]);
            return;
        }
    }
    put_wide(o, c);
}

static WCHAR single_with_glyphs(const NLS_CPDATA *p, UINT b, DWORD flags)
{
    if ((flags & MB_USEGLYPHCHARS) && (p->Flags & (NLS_CP_GLYPHS | NLS_CP_GLYPH_DEL))) {
        if ((p->Flags & NLS_CP_GLYPHS) && b < 0x20 && b)
            return g_glyphs[b];
        if (b == 0x7F)
            return 0x2302;
    }
    return single_to_wide(p, b);
}

/* ------------------------------------------------------------------ gb18030 */

#define GB_SUPPLEMENTARY_POINTER 189000

/* the character of a four byte code, or 0 when the pointer stands for none. */
static ULONG gb_char_of_pointer(ULONG pointer)
{
    if (pointer == 7457)
        return 0xE7C7;
    if (pointer >= GB_SUPPLEMENTARY_POINTER)
        return pointer <= GB_SUPPLEMENTARY_POINTER + 0xFFFFF ? 0x10000 + pointer - GB_SUPPLEMENTARY_POINTER : 0;
    LONG lo = 0, hi = (LONG)RTL_NUMBER_OF(g_nls_gb_pointer) - 1, found = -1;
    while (lo <= hi) {
        LONG mid = (lo + hi) / 2;
        if (g_nls_gb_pointer[mid] <= pointer) {
            found = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    if (found < 0 || g_nls_gb_pointer[found] >= GB_SUPPLEMENTARY_POINTER)
        return 0;
    ULONG c = g_nls_gb_char[found] + pointer - g_nls_gb_pointer[found];
    return c <= 0xFFFF ? c : 0;
}

/* the four byte pointer of a character, or -1. */
static LONG gb_pointer_of_char(ULONG c)
{
    if (c >= 0x10000)
        return c <= 0x10FFFF ? (LONG)(GB_SUPPLEMENTARY_POINTER + c - 0x10000) : -1;
    if (c == 0xE7C7)
        return 7457;
    LONG lo = 0, hi = (LONG)RTL_NUMBER_OF(g_nls_gb_char) - 1, found = -1;
    while (lo <= hi) {
        LONG mid = (lo + hi) / 2;
        if (g_nls_gb_char[mid] <= c) {
            found = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    if (found < 0 || g_nls_gb_pointer[found] >= GB_SUPPLEMENTARY_POINTER)
        return -1;
    ULONG length = g_nls_gb_pointer[found + 1] - g_nls_gb_pointer[found];
    if (c - g_nls_gb_char[found] >= length)
        return -1;
    return (LONG)(g_nls_gb_pointer[found] + c - g_nls_gb_char[found]);
}

/* returns FALSE for a byte the code page does not define when the flags ask for an error. */
static BOOL table_to_wide(const NLS_CPDATA *p, DWORD flags, const UCHAR *s, int n, WIDE_OUT *o)
{
    BOOL strict = (flags & MB_ERR_INVALID_CHARS) != 0;
    for (int i = 0; i < n && !o->Overflow; i++) {
        UINT b = s[i];
        if (!is_lead(p, b)) {
            WCHAR c = single_with_glyphs(p, b, flags);
            if (c == 0xFFFF) {
                /* a byte that is no character of gb18030. */
                if (strict)
                    return FALSE;
                put_wide(o, p->UnicodeDefault);
                continue;
            }
            if (strict && is_undefined_byte(p, b))
                return FALSE;
            put_table_wide(o, c, flags);
            continue;
        }
        if (p->Kind == NLS_KIND_GB18030 && i + 1 < n && s[i + 1] >= 0x30 && s[i + 1] <= 0x39) {
            ULONG c = 0;
            if (i + 3 < n && s[i + 2] >= 0x81 && s[i + 2] <= 0xFE && s[i + 3] >= 0x30 && s[i + 3] <= 0x39)
                c = gb_char_of_pointer(((b - 0x81) * 10 + (s[i + 1] - 0x30)) * 1260 + (s[i + 2] - 0x81) * 10 + (s[i + 3] - 0x30));
            if (!c) {
                if (strict)
                    return FALSE;
                put_wide(o, p->UnicodeDefault);
                continue;
            }
            i += 3;
            if (c >= 0x10000) {
                put_wide(o, (WCHAR)(0xD800 + ((c - 0x10000) >> 10)));
                put_wide(o, (WCHAR)(0xDC00 + ((c - 0x10000) & 0x3FF)));
            } else {
                put_table_wide(o, (WCHAR)c, flags);
            }
            continue;
        }
        /* a lead byte at the end of the text, or one a null byte follows, is a lone one. */
        if (i + 1 >= n || !s[i + 1]) {
            if (strict)
                return FALSE;
            put_wide(o, p->UnicodeDefault);
            continue;
        }
        WCHAR c = pair_to_wide(p, b, s[i + 1]);
        i++;
        if (c == 0xFFFF) {
            if (strict)
                return FALSE;
            put_wide(o, p->UnicodeDefault);
            continue;
        }
        put_table_wide(o, c, flags);
    }
    return TRUE;
}

/* ------------------------------------------------------------------ utf-16 to table code pages */

typedef struct TABLE_OUT {
    BYTE_OUT *Out;
    const NLS_CPDATA *Page;
    DWORD Flags;
    UCHAR Default[2];
    int DefaultLength;
    BOOL Used;
} TABLE_OUT;

static void put_default(TABLE_OUT *t)
{
    for (int i = 0; i < t->DefaultLength; i++)
        put_byte(t->Out, (CHAR)t->Default[i]);
    t->Used = TRUE;
}

static void put_four_bytes(TABLE_OUT *t, ULONG pointer)
{
    put_byte(t->Out, (CHAR)(0x81 + pointer / 12600));
    put_byte(t->Out, (CHAR)(0x30 + pointer / 1260 % 10));
    put_byte(t->Out, (CHAR)(0x81 + pointer / 10 % 126));
    put_byte(t->Out, (CHAR)(0x30 + pointer % 10));
}

/* a character of gb18030 that has no two byte code. */
static void put_gb_extra(TABLE_OUT *t, ULONG c)
{
    LONG pointer = gb_pointer_of_char(c);
    if (pointer < 0)
        put_default(t);
    else
        put_four_bytes(t, (ULONG)pointer);
}

/* one character: its code, the code of its best fit, or the default character. */
static void put_table_char(TABLE_OUT *t, WCHAR c)
{
    LONG code = wide_to_code(t->Page, c);
    if (code < 0 && t->Page->Kind == NLS_KIND_GB18030 && !(c >= 0xD800 && c <= 0xDFFF) && c != 0xE5E5) {
        put_gb_extra(t, c);
        return;
    }
    if (code < 0 || ((t->Flags & WC_NO_BEST_FIT_CHARS) && !code_round_trips(t->Page, c, (ULONG)code))) {
        put_default(t);
        return;
    }
    if (code > 0xFF)
        put_byte(t->Out, (CHAR)(code >> 8));
    put_byte(t->Out, (CHAR)(code & 0xFF));
}

static void wide_to_table(TABLE_OUT *t, const WCHAR *s, int n)
{
    DWORD flags = t->Flags;
    BOOL gb = t->Page->Kind == NLS_KIND_GB18030;
    for (int i = 0; i < n && !t->Out->Overflow; i++) {
        WCHAR c = s[i];
        if (gb && c >= 0xD800 && c <= 0xDBFF && i + 1 < n && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            put_gb_extra(t, 0x10000 + (((ULONG)c - 0xD800) << 10) + (s[i + 1] - 0xDC00));
            i++;
            continue;
        }
        if (!(flags & WC_COMPOSITECHECK)) {
            put_table_char(t, c);
            continue;
        }
        /* a character and the nonspacing marks after it. The first mark composes with the character when Unicode has
           the composite, the others and the marks of a cluster with no character are left over. */
        int j = i + 1;
        BOOL lone_marks = KbUniIsNonspacing(c);
        if (lone_marks)
            j = i;
        while (j < n && KbUniIsNonspacing(s[j]))
            j++;
        int marks = j - (lone_marks ? i : i + 1);
        if (!marks) {
            put_table_char(t, c);
            continue;
        }
        int first_leftover = lone_marks ? i : i + 1;
        WCHAR composite = 0;
        if (!lone_marks) {
            ULONG r = compose_pair(c, s[i + 1]);
            if (r && r < 0x10000) {
                composite = (WCHAR)r;
                first_leftover++;
            }
        }
        int leftover = j - first_leftover;
        if (!leftover) {
            put_table_char(t, composite ? composite : c);
        } else if (flags & WC_DISCARDNS) {
            if (!lone_marks)
                put_table_char(t, composite ? composite : c);
        } else if (flags & WC_DEFAULTCHAR) {
            put_default(t);
        } else {
            if (!lone_marks)
                put_table_char(t, composite ? composite : c);
            for (int k = first_leftover; k < j; k++)
                put_table_char(t, s[k]);
        }
        i = j - 1;
    }
}

/* ------------------------------------------------------------------ public conversions */

int WINAPI MultiByteToWideChar(UINT cp, DWORD flags, LPCCH src, int src_len, LPWSTR dst, int dst_len)
{
    if (!src || !src_len || dst_len < 0 || (dst_len && !dst) || (dst_len && (const void *)src == (void *)dst)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    UINT resolved = resolve_code_page(cp);
    const NLS_CPDATA *page = find_table_page(resolved);
    if (!page && resolved != CP_UTF8 && resolved != CP_UTF7 && resolved != CP_SYMBOL) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (resolved == CP_UTF7 || resolved == CP_SYMBOL) {
        if (flags) {
            KbSetError(ERROR_INVALID_FLAGS);
            return 0;
        }
    } else if (resolved == CP_UTF8) {
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
    if (resolved == CP_UTF8) {
        if (!utf8_to_wide(s, src_len, &o, (flags & MB_ERR_INVALID_CHARS) != 0)) {
            KbSetError(ERROR_NO_UNICODE_TRANSLATION);
            return 0;
        }
    } else if (resolved == CP_UTF7) {
        utf7_to_wide(s, src_len, &o);
    } else if (resolved == CP_SYMBOL) {
        for (int i = 0; i < src_len && !o.Overflow; i++)
            put_wide(&o, s[i] < 0x20 ? s[i] : (WCHAR)(0xF000 + s[i]));
    } else if (!table_to_wide(page, flags, s, src_len, &o)) {
        KbSetError(ERROR_NO_UNICODE_TRANSLATION);
        return 0;
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
    UINT resolved = resolve_code_page(cp);
    const NLS_CPDATA *page = find_table_page(resolved);
    if (!page && resolved != CP_UTF8 && resolved != CP_UTF7 && resolved != CP_SYMBOL) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!page) {
        if (default_char || used_default) {
            KbSetError(ERROR_INVALID_PARAMETER);
            return 0;
        }
        if (resolved == CP_UTF8 ? (flags & ~(DWORD)WC_ERR_INVALID_CHARS) : flags) {
            KbSetError(ERROR_INVALID_FLAGS);
            return 0;
        }
    } else if ((flags & ~(DWORD)(WC_COMPOSITECHECK | WC_DISCARDNS | WC_SEPCHARS | WC_DEFAULTCHAR | WC_NO_BEST_FIT_CHARS)) ||
               ((flags & (WC_DISCARDNS | WC_SEPCHARS | WC_DEFAULTCHAR)) && !(flags & WC_COMPOSITECHECK))) {
        KbSetError(ERROR_INVALID_FLAGS);
        return 0;
    }
    if (src_len < 0)
        src_len = (int)wcslen(src) + 1;
    BYTE_OUT o = {dst, dst_len, 0, FALSE};
    if (resolved == CP_UTF8) {
        if (!wide_to_utf8(src, src_len, &o, (flags & WC_ERR_INVALID_CHARS) != 0)) {
            KbSetError(ERROR_NO_UNICODE_TRANSLATION);
            return 0;
        }
    } else if (resolved == CP_UTF7) {
        wide_to_utf7(src, src_len, &o);
    } else if (resolved == CP_SYMBOL) {
        for (int i = 0; i < src_len && !o.Overflow; i++) {
            WCHAR c = src[i];
            put_byte(&o, (CHAR)(c < 0x20 ? c : (c >= 0xF020 && c <= 0xF0FF ? c - 0xF000 : '?')));
        }
    } else {
        TABLE_OUT t = {&o, page, flags, {(UCHAR)page->DefaultChar, 0}, 1, FALSE};
        if (default_char) {
            t.Default[0] = (UCHAR)default_char[0];
            if (is_lead(page, t.Default[0]) && default_char[1]) {
                t.Default[1] = (UCHAR)default_char[1];
                t.DefaultLength = 2;
            }
        }
        wide_to_table(&t, src, src_len);
        if (used_default)
            *used_default = t.Used;
    }
    if (o.Overflow) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    return o.Count;
}
