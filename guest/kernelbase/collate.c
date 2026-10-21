/*
 * string comparison, searching, mapping, folding, classification and
 * normalization.
 */
#include "kernelbase_private.h"
#include "nls_locslots.h"

/* ------------------------------------------------------------------ decomposition data */

typedef struct DECOMPOSITION {
    WCHAR Composed;
    WCHAR Base;
    WCHAR Mark;
} DECOMPOSITION;

#define GRAVE 0x0300
#define ACUTE 0x0301
#define CIRCUMFLEX 0x0302
#define TILDE 0x0303
#define MACRON 0x0304
#define BREVE 0x0306
#define DOT 0x0307
#define DIAERESIS 0x0308
#define RING 0x030A
#define DOUBLE_ACUTE 0x030B
#define CARON 0x030C
#define CEDILLA 0x0327
#define OGONEK 0x0328

/* an upper case letter and its lower case form one code point later. */
#define PAIR(c, base, mark) {(c), (base), (mark)}, {(c) + 1, (base) + 32, (mark)}
/* latin-1 letters whose lower case form is 0x20 higher. */
#define L1(c, base, mark) {(c), (base), (mark)}, {(c) + 0x20, (base) + 32, (mark)}

/* canonical decompositions. */
static const DECOMPOSITION g_decompositions[] = {
    L1(0xC0, 'A', GRAVE),      L1(0xC1, 'A', ACUTE),      L1(0xC2, 'A', CIRCUMFLEX), L1(0xC3, 'A', TILDE),
    L1(0xC4, 'A', DIAERESIS),  L1(0xC5, 'A', RING),       L1(0xC7, 'C', CEDILLA),    L1(0xC8, 'E', GRAVE),
    L1(0xC9, 'E', ACUTE),      L1(0xCA, 'E', CIRCUMFLEX), L1(0xCB, 'E', DIAERESIS),  L1(0xCC, 'I', GRAVE),
    L1(0xCD, 'I', ACUTE),      L1(0xCE, 'I', CIRCUMFLEX), L1(0xCF, 'I', DIAERESIS),  L1(0xD1, 'N', TILDE),
    L1(0xD2, 'O', GRAVE),      L1(0xD3, 'O', ACUTE),      L1(0xD4, 'O', CIRCUMFLEX), L1(0xD5, 'O', TILDE),
    L1(0xD6, 'O', DIAERESIS),  L1(0xD9, 'U', GRAVE),      L1(0xDA, 'U', ACUTE),      L1(0xDB, 'U', CIRCUMFLEX),
    L1(0xDC, 'U', DIAERESIS),  L1(0xDD, 'Y', ACUTE),      {0xFF, 'y', DIAERESIS},    PAIR(0x100, 'A', MACRON),
    PAIR(0x102, 'A', BREVE),   PAIR(0x104, 'A', OGONEK),  PAIR(0x106, 'C', ACUTE),   PAIR(0x108, 'C', CIRCUMFLEX),
    PAIR(0x10A, 'C', DOT),     PAIR(0x10C, 'C', CARON),   PAIR(0x10E, 'D', CARON),   PAIR(0x112, 'E', MACRON),
    PAIR(0x114, 'E', BREVE),   PAIR(0x116, 'E', DOT),     PAIR(0x118, 'E', OGONEK),  PAIR(0x11A, 'E', CARON),
    PAIR(0x11C, 'G', CIRCUMFLEX), PAIR(0x11E, 'G', BREVE), PAIR(0x120, 'G', DOT),    PAIR(0x122, 'G', CEDILLA),
    PAIR(0x124, 'H', CIRCUMFLEX), PAIR(0x128, 'I', TILDE), PAIR(0x12A, 'I', MACRON), PAIR(0x12C, 'I', BREVE),
    PAIR(0x12E, 'I', OGONEK),  {0x130, 'I', DOT},         PAIR(0x134, 'J', CIRCUMFLEX), PAIR(0x136, 'K', CEDILLA),
    {0x139, 'L', ACUTE},       {0x13A, 'l', ACUTE},       {0x13B, 'L', CEDILLA},     {0x13C, 'l', CEDILLA},
    {0x13D, 'L', CARON},       {0x13E, 'l', CARON},       {0x143, 'N', ACUTE},       {0x144, 'n', ACUTE},
    {0x145, 'N', CEDILLA},     {0x146, 'n', CEDILLA},     {0x147, 'N', CARON},       {0x148, 'n', CARON},
    PAIR(0x14C, 'O', MACRON),  PAIR(0x14E, 'O', BREVE),   PAIR(0x150, 'O', DOUBLE_ACUTE), PAIR(0x154, 'R', ACUTE),
    PAIR(0x156, 'R', CEDILLA), PAIR(0x158, 'R', CARON),   PAIR(0x15A, 'S', ACUTE),   PAIR(0x15C, 'S', CIRCUMFLEX),
    PAIR(0x15E, 'S', CEDILLA), PAIR(0x160, 'S', CARON),   PAIR(0x162, 'T', CEDILLA), PAIR(0x164, 'T', CARON),
    PAIR(0x168, 'U', TILDE),   PAIR(0x16A, 'U', MACRON),  PAIR(0x16C, 'U', BREVE),   PAIR(0x16E, 'U', RING),
    PAIR(0x170, 'U', DOUBLE_ACUTE), PAIR(0x172, 'U', OGONEK), PAIR(0x174, 'W', CIRCUMFLEX), PAIR(0x176, 'Y', CIRCUMFLEX),
    {0x178, 'Y', DIAERESIS},   {0x179, 'Z', ACUTE},       {0x17A, 'z', ACUTE},       {0x17B, 'Z', DOT},
    {0x17C, 'z', DOT},         {0x17D, 'Z', CARON},       {0x17E, 'z', CARON},
};

static const DECOMPOSITION *find_decomposition(WCHAR c)
{
    if (c < 0xC0 || c > 0x17E)
        return nullptr;
    /* the table pairs upper and lower case letters, so it is searched linearly. */
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_decompositions); i++)
        if (g_decompositions[i].Composed == c)
            return &g_decompositions[i];
    return nullptr;
}

static WCHAR compose(WCHAR base, WCHAR mark)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_decompositions); i++)
        if (g_decompositions[i].Base == base && g_decompositions[i].Mark == mark)
            return g_decompositions[i].Composed;
    return 0;
}

static BOOL is_combining(WCHAR c)
{
    return (c >= 0x0300 && c <= 0x036F) || (c >= 0x1AB0 && c <= 0x1AFF) || (c >= 0x1DC0 && c <= 0x1DFF) ||
           (c >= 0x20D0 && c <= 0x20FF) || (c >= 0xFE20 && c <= 0xFE2F);
}

/* ligatures that expand to two letters. */
static BOOL expand_ligature(WCHAR c, WCHAR out[2])
{
    switch (c) {
    case 0xC6:
        out[0] = 'A', out[1] = 'E';
        return TRUE;
    case 0xE6:
        out[0] = 'a', out[1] = 'e';
        return TRUE;
    case 0xDF:
        out[0] = 's', out[1] = 's';
        return TRUE;
    case 0x132:
        out[0] = 'I', out[1] = 'J';
        return TRUE;
    case 0x133:
        out[0] = 'i', out[1] = 'j';
        return TRUE;
    case 0x152:
        out[0] = 'O', out[1] = 'E';
        return TRUE;
    case 0x153:
        out[0] = 'o', out[1] = 'e';
        return TRUE;
    case 0xDE:
        out[0] = 'T', out[1] = 'H';
        return TRUE;
    case 0xFE:
        out[0] = 't', out[1] = 'h';
        return TRUE;
    }
    return FALSE;
}

static BOOL is_fullwidth(WCHAR c)
{
    return (c >= 0xFF01 && c <= 0xFF5E) || c == 0x3000;
}

static WCHAR narrow(WCHAR c)
{
    if (c >= 0xFF01 && c <= 0xFF5E)
        return (WCHAR)(c - 0xFF01 + 0x21);
    if (c == 0x3000)
        return ' ';
    return c;
}

static WCHAR widen(WCHAR c)
{
    if (c >= 0x21 && c <= 0x7E)
        return (WCHAR)(c - 0x21 + 0xFF01);
    if (c == ' ')
        return 0x3000;
    return c;
}

static BOOL is_katakana(WCHAR c)
{
    return c >= 0x30A1 && c <= 0x30F6;
}

static BOOL is_hiragana(WCHAR c)
{
    return c >= 0x3041 && c <= 0x3096;
}

static int string_length(LPCWSTR s, int len)
{
    return len < 0 ? (int)wcslen(s) : len;
}

/* ------------------------------------------------------------------ sort orders */

/* the sort order a locale name or an identifier stands for, and whether its language casts i and I in the Turkish way. */
typedef struct SORT_ORDER {
    UINT Collator;
    BOOL Turkic;
} SORT_ORDER;

#define NO_LOCALE_INDEX 0xFFFFFFFFu

static SORT_ORDER sort_of_locale(UINT locale, PCSTR alternate)
{
    SORT_ORDER o = {0, FALSE};
    if (locale == NO_LOCALE_INDEX)
        return o;
    PCWSTR language = KbLocaleText(locale, NLS_S_SISO639LANGNAME);
    o.Turkic = !wcscmp(language, L"tr") || !wcscmp(language, L"az");
    o.Collator = alternate ? KbCollatorByName(alternate) : 0;
    if (!o.Collator)
        o.Collator = (UINT)KbLocaleNumber(locale, NLS_N_COLLATOR);
    return o;
}

/* the sorts that have a name of their own, written after an underscore in the name of the locale. */
static PCSTR alternate_of_suffix(PCWSTR suffix)
{
    static const struct {
        PCWSTR Suffix;
        PCSTR Collator;
    } sorts[] = {{L"phoneb", "de@phonebook"}, {L"tradnl", "es@traditional"}};
    for (ULONG i = 0; i < RTL_NUMBER_OF(sorts); i++)
        if (!_wcsicmp(suffix, sorts[i].Suffix))
            return sorts[i].Collator;
    return nullptr;
}

static SORT_ORDER sort_of_name(PCWSTR name)
{
    SORT_ORDER none = {0, FALSE};
    if (!name || !*name)
        return sort_of_locale(KbLocaleFromLcid(LOCALE_USER_DEFAULT, TRUE), nullptr);
    if (!_wcsicmp(name, LOCALE_NAME_SYSTEM_DEFAULT))
        return sort_of_locale(KbLocaleFromLcid(LOCALE_SYSTEM_DEFAULT, TRUE), nullptr);
    WCHAR base[LOCALE_NAME_MAX_LENGTH];
    PCWSTR underscore = wcsrchr(name, '_');
    PCSTR alternate = nullptr;
    if (underscore && alternate_of_suffix(underscore + 1)) {
        alternate = alternate_of_suffix(underscore + 1);
        SIZE_T n = (SIZE_T)(underscore - name);
        if (n >= RTL_NUMBER_OF(base))
            return none;
        memcpy(base, name, n * sizeof(WCHAR));
        base[n] = 0;
        name = base;
    }
    return sort_of_locale(KbLocaleFromName(name), alternate);
}

static SORT_ORDER sort_of_lcid(LCID lcid)
{
    lcid = KbResolveLcid(lcid);
    PCSTR alternate = nullptr;
    switch (lcid) {
    case 0x10407:
        alternate = "de@phonebook";
        break;
    case 0x040A:
        /* the traditional Spanish order is the sort of its own identifier, 0x040A. */
        alternate = "es@traditional";
        break;
    }
    return sort_of_locale(KbLocaleFromLcid(lcid, TRUE), alternate);
}

static int compare_strings(SORT_ORDER order, DWORD flags, LPCWSTR a, int alen, LPCWSTR b, int blen)
{
    return KbCompareStrings(order.Collator, order.Turkic, flags, a, alen, b, blen);
}

#define COMPARE_FLAGS                                                                                             \
    (NORM_IGNORECASE | NORM_IGNORENONSPACE | NORM_IGNORESYMBOLS | SORT_DIGITSASNUMBERS | LINGUISTIC_IGNORECASE | \
     LINGUISTIC_IGNOREDIACRITIC | SORT_STRINGSORT | NORM_IGNOREKANATYPE | NORM_IGNOREWIDTH | NORM_LINGUISTIC_CASING)

static int compare_checked(SORT_ORDER order, DWORD flags, LPCWSTR a, int alen, LPCWSTR b, int blen)
{
    if (!a || !b) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (flags & ~(DWORD)(COMPARE_FLAGS | LOCALE_USE_CP_ACP)) {
        KbSetError(ERROR_INVALID_FLAGS);
        return 0;
    }
    return compare_strings(order, flags, a, string_length(a, alen), b, string_length(b, blen));
}

int WINAPI CompareStringEx(LPCWSTR name, DWORD flags, LPCWCH a, int alen, LPCWCH b, int blen,
                           LPNLSVERSIONINFO version, LPVOID reserved, LPARAM param)
{
    if (name && *name && !IsValidLocaleName(name) && _wcsicmp(name, LOCALE_NAME_SYSTEM_DEFAULT)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (reserved || param) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return compare_checked(sort_of_name(name), flags, a, alen, b, blen);
}

int WINAPI CompareStringW(LCID locale, DWORD flags, PCNZWCH a, int alen, PCNZWCH b, int blen)
{
    return compare_checked(sort_of_lcid(locale), flags, a, alen, b, blen);
}

int WINAPI CompareStringA(LCID locale, DWORD flags, LPCSTR a, int alen, LPCSTR b, int blen)
{
    if (!a || !b) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    UINT cp = CP_ACP;
    if (!(flags & LOCALE_USE_CP_ACP)) {
        DWORD value;
        if (GetLocaleInfoW(locale, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER, (LPWSTR)&value,
                           sizeof(value) / sizeof(WCHAR)))
            cp = value;
    }
    int wa_len = MultiByteToWideChar(cp, 0, a, alen, nullptr, 0);
    int wb_len = MultiByteToWideChar(cp, 0, b, blen, nullptr, 0);
    if ((!wa_len && alen) || (!wb_len && blen))
        return 0;
    PWSTR wa = KbAlloc(((SIZE_T)wa_len + 1) * sizeof(WCHAR));
    PWSTR wb = KbAlloc(((SIZE_T)wb_len + 1) * sizeof(WCHAR));
    int result = 0;
    if (wa && wb) {
        MultiByteToWideChar(cp, 0, a, alen, wa, wa_len);
        MultiByteToWideChar(cp, 0, b, blen, wb, wb_len);
        /* a terminator included by a length of -1 is not part of the comparison. */
        if (alen < 0 && wa_len)
            wa_len--;
        if (blen < 0 && wb_len)
            wb_len--;
        result = compare_checked(sort_of_lcid(locale), flags & ~(DWORD)LOCALE_USE_CP_ACP, wa, wa_len, wb, wb_len);
    } else {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
    }
    KbFree(wa);
    KbFree(wb);
    return result;
}

int WINAPI CompareStringOrdinal(LPCWCH a, int alen, LPCWCH b, int blen, BOOL ignore_case)
{
    if (!a || !b) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    alen = string_length(a, alen);
    blen = string_length(b, blen);
    int n = alen < blen ? alen : blen;
    for (int i = 0; i < n; i++) {
        WCHAR x = a[i], y = b[i];
        if (ignore_case) {
            x = KbUpcase(x);
            y = KbUpcase(y);
        }
        if (x != y)
            return x < y ? CSTR_LESS_THAN : CSTR_GREATER_THAN;
    }
    return alen < blen ? CSTR_LESS_THAN : alen > blen ? CSTR_GREATER_THAN : CSTR_EQUAL;
}

/* ------------------------------------------------------------------ searching */

static int find_nls(SORT_ORDER order, DWORD flags, LPCWSTR source, int source_len, LPCWSTR value, int value_len, LPINT found_len)
{
    DWORD position = flags & (FIND_STARTSWITH | FIND_ENDSWITH | FIND_FROMSTART | FIND_FROMEND);
    DWORD compare = flags & ~position;
    if (!source || !value || !source_len || !value_len || source_len < -1 || value_len < -1) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    if ((position & (position - 1)) || (compare & ~(DWORD)COMPARE_FLAGS) || (compare & SORT_STRINGSORT)) {
        KbSetError(ERROR_INVALID_FLAGS);
        return -1;
    }
    if (!position)
        position = FIND_FROMSTART;
    int n = string_length(source, source_len);
    int m = string_length(value, value_len);
    BOOL backward = position == FIND_FROMEND || position == FIND_ENDSWITH;
    for (int step = 0; step <= n; step++) {
        int start = backward ? n - step : step;
        if (position == FIND_STARTSWITH && start != 0)
            break;
        /* the shortest matching span from this start, or the one ending the source. */
        for (int end = start; end <= n; end++) {
            if (position == FIND_ENDSWITH && end != n)
                continue;
            if (end == start && m)
                continue;
            if (compare_strings(order, compare, source + start, end - start, value, m) == CSTR_EQUAL) {
                if (found_len)
                    *found_len = end - start;
                return start;
            }
        }
    }
    KbSetError(ERROR_SUCCESS);
    return -1;
}

int WINAPI FindNLSString(LCID locale, DWORD flags, LPCWSTR source, int source_len, LPCWSTR value, int value_len,
                         LPINT found_len)
{
    return find_nls(sort_of_lcid(locale), flags, source, source_len, value, value_len, found_len);
}

int WINAPI FindNLSStringEx(LPCWSTR name, DWORD flags, LPCWSTR source, int source_len, LPCWSTR value, int value_len,
                           LPINT found_len, LPNLSVERSIONINFO version, LPVOID reserved, LPARAM param)
{
    if (reserved || param) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    return find_nls(sort_of_name(name), flags, source, source_len, value, value_len, found_len);
}

int WINAPI FindStringOrdinal(DWORD flags, LPCWSTR source, int source_len, LPCWSTR value, int value_len,
                             BOOL ignore_case)
{
    if (!source || !value || source_len < -1 || value_len < -1) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    if (flags != FIND_FROMSTART && flags != FIND_FROMEND && flags != FIND_STARTSWITH && flags != FIND_ENDSWITH) {
        KbSetError(ERROR_INVALID_FLAGS);
        return -1;
    }
    int n = string_length(source, source_len);
    int m = string_length(value, value_len);
    KbSetError(ERROR_SUCCESS);
    if (m > n)
        return -1;
    for (int step = 0; step <= n - m; step++) {
        int start = (flags == FIND_FROMEND || flags == FIND_ENDSWITH) ? n - m - step : step;
        if ((flags == FIND_STARTSWITH && start != 0) || (flags == FIND_ENDSWITH && start != n - m))
            break;
        if (CompareStringOrdinal(source + start, m, value, m, ignore_case) == CSTR_EQUAL)
            return start;
    }
    return -1;
}

/* ------------------------------------------------------------------ mapping */

typedef struct BYTE_OUT {
    PBYTE Buffer;
    int Size;
    int Length;
} BYTE_OUT;

#define LCMAP_CASE_FLAGS (LCMAP_LOWERCASE | LCMAP_UPPERCASE)
#define LCMAP_VALID                                                                                               \
    (LCMAP_LOWERCASE | LCMAP_UPPERCASE | LCMAP_SORTKEY | LCMAP_BYTEREV | LCMAP_HASH | LCMAP_HIRAGANA |            \
     LCMAP_KATAKANA | LCMAP_HALFWIDTH | LCMAP_FULLWIDTH | LCMAP_LINGUISTIC_CASING | LCMAP_SIMPLIFIED_CHINESE |   \
     LCMAP_TRADITIONAL_CHINESE | NORM_IGNORECASE | NORM_IGNORENONSPACE | NORM_IGNORESYMBOLS | NORM_IGNOREKANATYPE | \
     NORM_IGNOREWIDTH | SORT_STRINGSORT | SORT_DIGITSASNUMBERS | LINGUISTIC_IGNORECASE |                         \
     LINGUISTIC_IGNOREDIACRITIC | NORM_LINGUISTIC_CASING)

static int map_string(SORT_ORDER order, DWORD flags, LPCWSTR src, int src_len, LPWSTR dst, int dst_len)
{
    if (!src || !src_len || src_len < -1 || dst_len < 0 || (dst_len && !dst)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!flags || (flags & ~(DWORD)LCMAP_VALID)) {
        KbSetError(ERROR_INVALID_FLAGS);
        return 0;
    }
    DWORD casing = flags & LCMAP_CASE_FLAGS;
    BOOL sortkey = (flags & LCMAP_SORTKEY) != 0;
    BOOL hash = (flags & LCMAP_HASH) != 0;
    if ((sortkey || hash) && (flags & (LCMAP_CASE_FLAGS | LCMAP_HIRAGANA | LCMAP_KATAKANA | LCMAP_HALFWIDTH |
                                       LCMAP_FULLWIDTH | LCMAP_SIMPLIFIED_CHINESE | LCMAP_TRADITIONAL_CHINESE))) {
        KbSetError(ERROR_INVALID_FLAGS);
        return 0;
    }
    if ((flags & (LCMAP_HIRAGANA | LCMAP_KATAKANA)) == (LCMAP_HIRAGANA | LCMAP_KATAKANA) ||
        (flags & (LCMAP_HALFWIDTH | LCMAP_FULLWIDTH)) == (LCMAP_HALFWIDTH | LCMAP_FULLWIDTH) ||
        (flags & (LCMAP_SIMPLIFIED_CHINESE | LCMAP_TRADITIONAL_CHINESE)) ==
            (LCMAP_SIMPLIFIED_CHINESE | LCMAP_TRADITIONAL_CHINESE)) {
        KbSetError(ERROR_INVALID_FLAGS);
        return 0;
    }
    if (!sortkey && !hash && (flags & (SORT_STRINGSORT | SORT_DIGITSASNUMBERS | LINGUISTIC_IGNORECASE |
                                       LINGUISTIC_IGNOREDIACRITIC | NORM_IGNORECASE))) {
        KbSetError(ERROR_INVALID_FLAGS);
        return 0;
    }
    int len = src_len < 0 ? (int)wcslen(src) + 1 : src_len;
    if (sortkey) {
        int key_len = src_len < 0 ? len - 1 : len;
        return KbSortKey(order.Collator, order.Turkic, flags & ~(DWORD)LCMAP_SORTKEY, src, key_len, (PBYTE)dst, dst_len);
    }
    if (hash) {
        int key_len = src_len < 0 ? len - 1 : len;
        int need = KbSortKey(order.Collator, order.Turkic, flags & ~(DWORD)LCMAP_HASH, src, key_len, nullptr, 0);
        if (!dst_len)
            return sizeof(DWORD);
        if (dst_len < (int)sizeof(DWORD)) {
            KbSetError(ERROR_INSUFFICIENT_BUFFER);
            return 0;
        }
        PBYTE key = KbAlloc((SIZE_T)need);
        if (!key) {
            KbSetError(ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }
        KbSortKey(order.Collator, order.Turkic, flags & ~(DWORD)LCMAP_HASH, src, key_len, key, need);
        DWORD h = 2166136261u;
        for (int i = 0; i < need; i++)
            h = (h ^ key[i]) * 16777619u;
        KbFree(key);
        memcpy(dst, &h, sizeof(h));
        return sizeof(DWORD);
    }
    if (dst && src != dst && dst < src + len && src < dst + dst_len) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (flags & LCMAP_BYTEREV) {
        if (dst_len && dst_len < len) {
            KbSetError(ERROR_INSUFFICIENT_BUFFER);
            return 0;
        }
        if (!dst_len)
            return len;
    }
    int out = 0;
    BOOL turkic = order.Turkic && (flags & LCMAP_LINGUISTIC_CASING);
    BOOL word_start = TRUE;
    for (int i = 0; i < len; i++) {
        ULONG c = src[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < len && src[i + 1] >= 0xDC00 && src[i + 1] < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (src[i + 1] - 0xDC00);
            i++;
        }
        if ((flags & NORM_IGNORESYMBOLS) && c < 0x10000 && (KbCharType1((WCHAR)c) & (C1_PUNCT | C1_SPACE)))
            continue;
        if ((flags & NORM_IGNORENONSPACE) && KbUniIsNonspacing(c))
            continue;
        WCHAR units[24];
        int count = 0;
        BOOL letter = KbUniUpper(c) != c || KbUniLower(c) != c;
        BOOL upper = casing == LCMAP_UPPERCASE || (casing == LCMAP_TITLECASE && word_start);
        BOOL lower = casing == LCMAP_LOWERCASE || (casing == LCMAP_TITLECASE && !word_start);
        word_start = !letter && c != '\'';
        if (upper)
            c = (turkic && c == 'i') ? 0x130 : KbUniUpper(c);
        else if (lower)
            c = (turkic && c == 'I') ? 0x131 : (turkic && c == 0x130) ? 'i' : KbUniLower(c);
        if ((flags & LCMAP_HIRAGANA) && c >= 0x30A1 && c <= 0x30F6)
            c -= 0x60;
        else if ((flags & LCMAP_KATAKANA) && c >= 0x3041 && c <= 0x3096)
            c += 0x60;
        if (flags & (LCMAP_HALFWIDTH | NORM_IGNOREWIDTH))
            c = narrow(c < 0x10000 ? (WCHAR)c : 0);
        else if (flags & LCMAP_FULLWIDTH)
            c = widen(c < 0x10000 ? (WCHAR)c : 0);
        if (c >= 0x10000) {
            units[count++] = (WCHAR)(0xD800 + ((c - 0x10000) >> 10));
            units[count++] = (WCHAR)(0xDC00 + ((c - 0x10000) & 0x3FF));
        } else if (flags & NORM_IGNORENONSPACE) {
            /* the letter without its marks. */
            WCHAR parts[20];
            int n = (c >= 0xC0 && !(c >= 0xAC00 && c < 0xD7A4)) ? KbUniDecompose(c, FALSE, parts) : 0;
            if (n) {
                for (int k = 0; k < n; k++)
                    if (!KbUniIsNonspacing(parts[k]))
                        units[count++] = parts[k];
            } else {
                units[count++] = (WCHAR)c;
            }
        } else {
            units[count++] = (WCHAR)c;
        }
        for (int k = 0; k < count; k++) {
            WCHAR u = units[k];
            if (flags & LCMAP_BYTEREV)
                u = (WCHAR)((u >> 8) | (u << 8));
            if (dst_len) {
                if (out >= dst_len) {
                    KbSetError(ERROR_INSUFFICIENT_BUFFER);
                    return 0;
                }
                dst[out] = u;
            }
            out++;
        }
    }
    return out;
}

int WINAPI LCMapStringEx(LPCWSTR name, DWORD flags, LPCWSTR src, int src_len, LPWSTR dst, int dst_len,
                         LPNLSVERSIONINFO version, LPVOID reserved, LPARAM param)
{
    if (version || reserved || param) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (name && *name && !IsValidLocaleName(name) && _wcsicmp(name, LOCALE_NAME_SYSTEM_DEFAULT)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return map_string(sort_of_name(name), flags, src, src_len, dst, dst_len);
}

int WINAPI LCMapStringW(LCID locale, DWORD flags, LPCWSTR src, int src_len, LPWSTR dst, int dst_len)
{
    return map_string(sort_of_lcid(locale), flags, src, src_len, dst, dst_len);
}

int WINAPI LCMapStringA(LCID locale, DWORD flags, LPCSTR src, int src_len, LPSTR dst, int dst_len)
{
    if (!src || !src_len || src_len < -1 || dst_len < 0 || (dst_len && !dst) || (src == dst && (flags & LCMAP_SORTKEY))) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    UINT cp = CP_ACP;
    if (!(flags & LOCALE_USE_CP_ACP)) {
        DWORD value;
        if (GetLocaleInfoW(locale, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER, (LPWSTR)&value,
                           sizeof(value) / sizeof(WCHAR)))
            cp = value;
    }
    flags &= ~(DWORD)LOCALE_USE_CP_ACP;
    int wlen = MultiByteToWideChar(cp, 0, src, src_len, nullptr, 0);
    if (!wlen)
        return 0;
    PWSTR wsrc = KbAlloc((SIZE_T)wlen * sizeof(WCHAR));
    if (!wsrc) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    MultiByteToWideChar(cp, 0, src, src_len, wsrc, wlen);
    int result = 0;
    if (flags & (LCMAP_SORTKEY | LCMAP_HASH)) {
        result = map_string(sort_of_lcid(locale), flags, wsrc, src_len < 0 ? -1 : wlen, (LPWSTR)dst, dst_len);
    } else {
        int need = map_string(sort_of_lcid(locale), flags, wsrc, wlen, nullptr, 0);
        PWSTR wdst = need ? KbAlloc((SIZE_T)need * sizeof(WCHAR)) : nullptr;
        if (wdst) {
            map_string(sort_of_lcid(locale), flags, wsrc, wlen, wdst, need);
            result = WideCharToMultiByte(cp, 0, wdst, need, nullptr, 0, nullptr, nullptr);
            if (dst_len) {
                if (result > dst_len) {
                    KbSetError(ERROR_INSUFFICIENT_BUFFER);
                    result = 0;
                } else {
                    result = WideCharToMultiByte(cp, 0, wdst, need, dst, dst_len, nullptr, nullptr);
                }
            }
            KbFree(wdst);
        }
    }
    KbFree(wsrc);
    return result;
}

/* ------------------------------------------------------------------ folding */

static WCHAR fold_digit(WCHAR c)
{
    if (c == 0xB2 || c == 0xB3)
        return (WCHAR)('2' + c - 0xB2);
    if (c == 0xB9)
        return '1';
    if (c == 0x2070)
        return '0';
    if (c >= 0x2074 && c <= 0x2079)
        return (WCHAR)('4' + c - 0x2074);
    if (c >= 0x2080 && c <= 0x2089)
        return (WCHAR)('0' + c - 0x2080);
    if (c >= 0x0660 && c <= 0x0669)
        return (WCHAR)('0' + c - 0x0660);
    if (c >= 0x06F0 && c <= 0x06F9)
        return (WCHAR)('0' + c - 0x06F0);
    if (c >= 0x0966 && c <= 0x096F)
        return (WCHAR)('0' + c - 0x0966);
    if (c >= 0xFF10 && c <= 0xFF19)
        return (WCHAR)('0' + c - 0xFF10);
    if (c >= 0x2460 && c <= 0x2468)
        return (WCHAR)('1' + c - 0x2460);
    return c;
}

/* compatibility zone characters with simple equivalents. */
static WCHAR fold_czone(WCHAR c)
{
    if (c >= 0xFF01 && c <= 0xFF5E)
        return (WCHAR)(c - 0xFF01 + 0x21);
    if (c == 0x3000)
        return ' ';
    return c;
}

typedef struct WIDE_OUT {
    LPWSTR Buffer;
    int Size;
    int Length;
    BOOL Overflow;
} WIDE_OUT;

static void put_wide(WIDE_OUT *o, WCHAR c)
{
    if (o->Size) {
        if (o->Length < o->Size)
            o->Buffer[o->Length] = c;
        else
            o->Overflow = TRUE;
    }
    o->Length++;
}

int WINAPI FoldStringW(DWORD flags, LPCWSTR src, int src_len, LPWSTR dst, int dst_len)
{
    if (!src || !src_len || src_len < -1 || dst_len < 0 || (dst_len && !dst) || src == dst) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!flags || (flags & ~(DWORD)(MAP_FOLDCZONE | MAP_PRECOMPOSED | MAP_COMPOSITE | MAP_FOLDDIGITS |
                                    MAP_EXPAND_LIGATURES)) ||
        ((flags & MAP_PRECOMPOSED) && (flags & MAP_COMPOSITE)) ||
        ((flags & MAP_EXPAND_LIGATURES) && (flags & (MAP_PRECOMPOSED | MAP_COMPOSITE)))) {
        KbSetError(ERROR_INVALID_FLAGS);
        return 0;
    }
    int len = src_len < 0 ? (int)wcslen(src) + 1 : src_len;
    WIDE_OUT o = {dst, dst_len, 0, FALSE};
    for (int i = 0; i < len; i++) {
        WCHAR c = src[i];
        if (flags & MAP_FOLDCZONE)
            c = fold_czone(c);
        if (flags & MAP_FOLDDIGITS)
            c = fold_digit(c);
        if (flags & MAP_EXPAND_LIGATURES) {
            WCHAR pair[2];
            if (c != 0xDE && c != 0xFE && expand_ligature(c, pair)) {
                put_wide(&o, pair[0]);
                put_wide(&o, pair[1]);
                continue;
            }
        }
        if (flags & MAP_COMPOSITE) {
            const DECOMPOSITION *d = find_decomposition(c);
            if (d) {
                put_wide(&o, d->Base);
                put_wide(&o, d->Mark);
                continue;
            }
        }
        if ((flags & MAP_PRECOMPOSED) && i + 1 < len && is_combining(src[i + 1])) {
            WCHAR composed = compose(c, src[i + 1]);
            if (composed) {
                put_wide(&o, composed);
                i++;
                continue;
            }
        }
        put_wide(&o, c);
    }
    if (o.Overflow) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    return o.Length;
}

int WINAPI FoldStringA(DWORD flags, LPCSTR src, int src_len, LPSTR dst, int dst_len)
{
    if (!src || !src_len || src_len < -1 || dst_len < 0 || (dst_len && !dst) || src == dst) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    int wlen = MultiByteToWideChar(CP_ACP, 0, src, src_len, nullptr, 0);
    if (!wlen)
        return 0;
    PWSTR wsrc = KbAlloc((SIZE_T)wlen * sizeof(WCHAR));
    if (!wsrc) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    MultiByteToWideChar(CP_ACP, 0, src, src_len, wsrc, wlen);
    int result = 0;
    int need = FoldStringW(flags, wsrc, wlen, nullptr, 0);
    PWSTR wdst = need ? KbAlloc((SIZE_T)need * sizeof(WCHAR)) : nullptr;
    if (wdst) {
        FoldStringW(flags, wsrc, wlen, wdst, need);
        result = WideCharToMultiByte(CP_ACP, 0, wdst, need, nullptr, 0, nullptr, nullptr);
        if (dst_len) {
            if (result > dst_len) {
                KbSetError(ERROR_INSUFFICIENT_BUFFER);
                result = 0;
            } else {
                result = WideCharToMultiByte(CP_ACP, 0, wdst, need, dst, dst_len, nullptr, nullptr);
            }
        }
        KbFree(wdst);
    }
    KbFree(wsrc);
    return result;
}

/* ------------------------------------------------------------------ classification */

static BOOL is_rtl(WCHAR c)
{
    return (c >= 0x0590 && c <= 0x08FF) || (c >= 0xFB1D && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF);
}

static WORD char_type2(WCHAR c)
{
    WORD t1 = KbCharType1(c);
    if (c >= '0' && c <= '9')
        return C2_EUROPENUMBER;
    if (c == 0xB2 || c == 0xB3 || c == 0xB9 || (c >= 0xFF10 && c <= 0xFF19))
        return C2_EUROPENUMBER;
    if (c >= 0x0660 && c <= 0x0669)
        return C2_ARABICNUMBER;
    if (c == '+' || c == '-')
        return C2_EUROPESEPARATOR;
    if (c == '#' || c == '$' || c == '%' || c == 0xA2 || c == 0xA3 || c == 0xA4 || c == 0xA5 || c == 0xB0 ||
        c == 0xB1 || (c >= 0x20A0 && c <= 0x20CF) || c == 0x2030)
        return C2_EUROPETERMINATOR;
    if (c == ',' || c == '.' || c == '/' || c == ':' || c == 0xA0)
        return C2_COMMONSEPARATOR;
    if (c == '\n' || c == '\r' || c == 0x1C || c == 0x1D || c == 0x1E || c == 0x85 || c == 0x2029)
        return C2_BLOCKSEPARATOR;
    if (c == '\t' || c == 0x0B || c == 0x1F)
        return C2_SEGMENTSEPARATOR;
    if (c == ' ' || c == '\f' || c == 0x3000 || (c >= 0x2000 && c <= 0x200A) || c == 0x2028)
        return C2_WHITESPACE;
    if (is_rtl(c))
        return C2_RIGHTTOLEFT;
    if (!t1 || (t1 & C1_CNTRL) || is_combining(c))
        return C2_NOTAPPLICABLE;
    if (t1 & C1_ALPHA)
        return C2_LEFTTORIGHT;
    return C2_OTHERNEUTRAL;
}

static WORD char_type3(WCHAR c)
{
    WORD t1 = KbCharType1(c);
    WORD t = 0;
    if (c >= 0xD800 && c <= 0xDBFF)
        return C3_HIGHSURROGATE;
    if (c >= 0xDC00 && c <= 0xDFFF)
        return C3_LOWSURROGATE;
    if (is_combining(c))
        t |= C3_NONSPACING | C3_DIACRITIC;
    if (c == '^' || c == '`' || c == 0xA8 || c == 0xAF || c == 0xB4 || c == 0xB8 || (c >= 0x02B0 && c <= 0x02FF))
        t |= C3_DIACRITIC;
    if ((t1 & C1_PUNCT) && !(t & C3_DIACRITIC))
        t |= C3_SYMBOL;
    if (t1 & C1_ALPHA)
        t |= C3_ALPHA;
    if (is_fullwidth(c) || (c >= 0x3000 && c <= 0x9FFF))
        t |= C3_FULLWIDTH;
    else if ((c >= 0x20 && c <= 0x7E) || (c >= 0xFF61 && c <= 0xFFDC))
        t |= C3_HALFWIDTH;
    if (is_katakana(c) || (c >= 0xFF66 && c <= 0xFF9D))
        t |= C3_KATAKANA | C3_ALPHA;
    if (is_hiragana(c))
        t |= C3_HIRAGANA | C3_ALPHA;
    if ((c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF) || (c >= 0xF900 && c <= 0xFAFF))
        t |= C3_IDEOGRAPH | C3_ALPHA;
    if (c == 0x0640)
        t |= C3_KASHIDA;
    return t;
}

BOOL WINAPI GetStringTypeW(DWORD type, LPCWCH src, int count, LPWORD types)
{
    if (!src || !types || count == 0 || count < -1) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (type != CT_CTYPE1 && type != CT_CTYPE2 && type != CT_CTYPE3) {
        KbSetError(ERROR_INVALID_FLAGS);
        return FALSE;
    }
    if (count < 0)
        count = (int)wcslen(src) + 1;
    for (int i = 0; i < count; i++) {
        WCHAR c = src[i];
        if (type == CT_CTYPE1)
            types[i] = KbCharType1(c);
        else if (type == CT_CTYPE2)
            types[i] = char_type2(c);
        else
            types[i] = char_type3(c);
    }
    return TRUE;
}

BOOL WINAPI GetStringTypeExW(LCID locale, DWORD type, LPCWCH src, int count, LPWORD types)
{
    return GetStringTypeW(type, src, count, types);
}

BOOL WINAPI GetStringTypeA(LCID locale, DWORD type, LPCSTR src, int count, LPWORD types)
{
    if (!src || !types || count == 0 || count < -1) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UINT cp = CP_ACP;
    DWORD value;
    if (GetLocaleInfoW(locale, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER, (LPWSTR)&value,
                       sizeof(value) / sizeof(WCHAR)))
        cp = value;
    if (count < 0)
        count = (int)strlen(src) + 1;
    for (int i = 0; i < count; i++) {
        WCHAR w = 0;
        /* one result per byte; the ansi code pages here are single byte. */
        if (MultiByteToWideChar(cp, 0, src + i, 1, &w, 1) != 1)
            w = 0xFFFF;
        if (!GetStringTypeW(type, &w, 1, &types[i]))
            return FALSE;
    }
    return TRUE;
}

BOOL WINAPI GetStringTypeExA(LCID locale, DWORD type, LPCSTR src, int count, LPWORD types)
{
    return GetStringTypeA(locale, type, src, count, types);
}

#define GSS_ALLOW_INHERITED_COMMON 0x0001

int WINAPI GetStringScripts(DWORD flags, LPCWSTR string, int length, LPWSTR scripts, int size)
{
    static const struct {
        WCHAR First, Last;
        PCWSTR Name;
    } ranges[] = {
        {0x0041, 0x005A, L"Latn"}, {0x0061, 0x007A, L"Latn"}, {0x00AA, 0x00AA, L"Latn"}, {0x00BA, 0x00BA, L"Latn"},
        {0x00C0, 0x024F, L"Latn"}, {0x1E00, 0x1EFF, L"Latn"}, {0xFF21, 0xFF3A, L"Latn"}, {0xFF41, 0xFF5A, L"Latn"},
        {0x0370, 0x03FF, L"Grek"}, {0x1F00, 0x1FFF, L"Grek"}, {0x0400, 0x052F, L"Cyrl"}, {0x0530, 0x058F, L"Armn"},
        {0x0590, 0x05FF, L"Hebr"}, {0x0600, 0x06FF, L"Arab"}, {0x0E00, 0x0E7F, L"Thai"}, {0x10A0, 0x10FF, L"Geor"},
        {0x3040, 0x309F, L"Hira"}, {0x30A0, 0x30FF, L"Kana"}, {0x4E00, 0x9FFF, L"Hani"}, {0xAC00, 0xD7AF, L"Hang"},
    };
    if (!string || length < -1 || size < 0 || (size && !scripts) || (flags & ~(DWORD)GSS_ALLOW_INHERITED_COMMON)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (length < 0)
        length = (int)wcslen(string);
    WCHAR result[128];
    int n = 0;
    BOOL common = FALSE;
    result[0] = 0;
    for (int i = 0; i < length; i++) {
        PCWSTR name = nullptr;
        for (ULONG k = 0; k < RTL_NUMBER_OF(ranges); k++)
            if (string[i] >= ranges[k].First && string[i] <= ranges[k].Last)
                name = ranges[k].Name;
        if (!name) {
            common = TRUE;
            continue;
        }
        if (wcsstr(result, name))
            continue;
        if (n + 5 < (int)RTL_NUMBER_OF(result)) {
            memcpy(result + n, name, 4 * sizeof(WCHAR));
            result[n + 4] = ';';
            n += 5;
            result[n] = 0;
        }
    }
    if (common && (flags & GSS_ALLOW_INHERITED_COMMON) && n + 5 < (int)RTL_NUMBER_OF(result)) {
        memcpy(result + n, L"Zyyy;", 5 * sizeof(WCHAR));
        n += 5;
        result[n] = 0;
    }
    int need = n + 1;
    if (!size)
        return need;
    if (size < need) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    memcpy(scripts, result, (SIZE_T)need * sizeof(WCHAR));
    return need;
}

/* ------------------------------------------------------------------ normalization */

/*
 * the normalization forms of the Unicode standard: the characters are decomposed, as far as their canonical decomposition
 * goes or their compatibility one for the forms of that name, the marks are put in the order of their combining classes,
 * and the composed forms put the characters together again where the standard allows.
 */
static int normalize(NORM_FORM form, LPCWSTR src, int len, LPWSTR dst, int dst_len)
{
    BOOL compat = form == NormalizationKC || form == NormalizationKD;
    BOOL composed = form == NormalizationC || form == NormalizationKC;
    for (int i = 0; i < len; i++) {
        WCHAR c = src[i];
        BOOL bad = FALSE;
        if (c >= 0xD800 && c <= 0xDBFF)
            bad = i + 1 >= len || src[i + 1] < 0xDC00 || src[i + 1] > 0xDFFF;
        else if (c >= 0xDC00 && c <= 0xDFFF)
            bad = i == 0 || src[i - 1] < 0xD800 || src[i - 1] > 0xDBFF;
        if (bad) {
            KbSetError(ERROR_NO_UNICODE_TRANSLATION);
            return -(i + 1);
        }
    }
    ULONG *cps = nullptr;
    SIZE_T count = 0, capacity = 0;
    for (int i = 0; i < len; i++) {
        ULONG c = src[i];
        if (c >= 0xD800 && c <= 0xDBFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (src[i + 1] - 0xDC00);
            i++;
        }
        WCHAR parts[20];
        int n = c >= 0xA0 ? KbUniDecompose(c, compat, parts) : 0;
        ULONG decoded[20];
        int m = 0;
        if (!n) {
            decoded[m++] = c;
        } else {
            for (int k = 0; k < n; k++) {
                ULONG p = parts[k];
                if (p >= 0xD800 && p <= 0xDBFF && k + 1 < n) {
                    p = 0x10000 + ((p - 0xD800) << 10) + (parts[k + 1] - 0xDC00);
                    k++;
                }
                decoded[m++] = p;
            }
        }
        if (count + (SIZE_T)m > capacity) {
            SIZE_T grown = (capacity ? capacity * 2 : 64) + (SIZE_T)m;
            ULONG *bigger = KbAlloc(grown * sizeof(ULONG));
            if (!bigger) {
                KbFree(cps);
                KbSetError(ERROR_NOT_ENOUGH_MEMORY);
                return 0;
            }
            if (cps)
                memcpy(bigger, cps, count * sizeof(ULONG));
            KbFree(cps);
            cps = bigger;
            capacity = grown;
        }
        for (int k = 0; k < m; k++)
            cps[count++] = decoded[k];
    }
    /* the marks of each run in the order of their classes. */
    for (SIZE_T i = 0; i < count;) {
        if (!KbUniCcc(cps[i])) {
            i++;
            continue;
        }
        SIZE_T j = i;
        while (j < count && KbUniCcc(cps[j]))
            j++;
        for (SIZE_T a = i + 1; a < j; a++) {
            ULONG c = cps[a];
            UINT cc = KbUniCcc(c);
            SIZE_T b = a;
            while (b > i && KbUniCcc(cps[b - 1]) > cc) {
                cps[b] = cps[b - 1];
                b--;
            }
            cps[b] = c;
        }
        i = j;
    }
    if (composed && count) {
        SIZE_T out = 0, starter = (SIZE_T)-1;
        UINT last_class = 0;
        for (SIZE_T i = 0; i < count; i++) {
            ULONG c = cps[i];
            UINT cc = KbUniCcc(c);
            if (starter != (SIZE_T)-1 && (out - 1 == starter || last_class < cc)) {
                ULONG pair = KbUniCompose(cps[starter], c, TRUE);
                if (pair) {
                    cps[starter] = pair;
                    continue;
                }
            }
            if (!cc) {
                starter = out;
                last_class = 0;
            } else {
                last_class = cc;
            }
            cps[out++] = c;
        }
        count = out;
    }
    WIDE_OUT o = {dst, dst_len, 0, FALSE};
    for (SIZE_T i = 0; i < count; i++) {
        ULONG c = cps[i];
        if (c >= 0x10000) {
            put_wide(&o, (WCHAR)(0xD800 + ((c - 0x10000) >> 10)));
            put_wide(&o, (WCHAR)(0xDC00 + ((c - 0x10000) & 0x3FF)));
        } else {
            put_wide(&o, (WCHAR)c);
        }
    }
    KbFree(cps);
    if (o.Overflow) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    return o.Length;
}

int WINAPI NormalizeString(NORM_FORM form, LPCWSTR src, int src_len, LPWSTR dst, int dst_len)
{
    if ((form != NormalizationC && form != NormalizationD && form != NormalizationKC && form != NormalizationKD) ||
        !src || src_len < -1 || dst_len < 0 || (dst_len && !dst)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    int len = src_len < 0 ? (int)wcslen(src) + 1 : src_len;
    if (!len) {
        KbSetError(ERROR_SUCCESS);
        return 0;
    }
    int r = normalize(form, src, len, dst, dst_len);
    if (r > 0)
        KbSetError(ERROR_SUCCESS);
    return r;
}

BOOL WINAPI IsNormalizedString(NORM_FORM form, LPCWSTR src, int src_len)
{
    if ((form != NormalizationC && form != NormalizationD && form != NormalizationKC && form != NormalizationKD) ||
        !src || src_len < -1) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    int len = src_len < 0 ? (int)wcslen(src) : src_len;
    if (!len)
        return TRUE;
    int need = normalize(form, src, len, nullptr, 0);
    if (need <= 0)
        return FALSE;
    if (need != len) {
        KbSetError(ERROR_SUCCESS);
        return FALSE;
    }
    PWSTR buffer = KbAlloc((SIZE_T)need * sizeof(WCHAR));
    if (!buffer) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    normalize(form, src, len, buffer, need);
    BOOL same = !memcmp(buffer, src, (SIZE_T)len * sizeof(WCHAR));
    KbFree(buffer);
    KbSetError(ERROR_SUCCESS);
    return same;
}

/* ------------------------------------------------------------------ versions */

#define KB_NLS_VERSION 0x00060305
#define KB_NLS_DEFINED_VERSION 0x00060305

BOOL WINAPI GetNLSVersion(NLS_FUNCTION function, LCID locale, LPNLSVERSIONINFO info)
{
    if (function != COMPARE_STRING || !info) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (info->dwNLSVersionInfoSize < FIELD_OFFSET(NLSVERSIONINFO, dwEffectiveId)) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    if (!KbIsSupportedLcid(locale)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    info->dwNLSVersion = KB_NLS_VERSION;
    info->dwDefinedVersion = KB_NLS_DEFINED_VERSION;
    if (info->dwNLSVersionInfoSize >= sizeof(NLSVERSIONINFO)) {
        info->dwEffectiveId = KbResolveLcid(locale);
        memset(&info->guidCustomVersion, 0, sizeof(info->guidCustomVersion));
    }
    return TRUE;
}

BOOL WINAPI GetNLSVersionEx(NLS_FUNCTION function, LPCWSTR name, LPNLSVERSIONINFOEX info)
{
    if (function != COMPARE_STRING || !info) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (info->dwNLSVersionInfoSize < sizeof(NLSVERSIONINFOEX)) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    LCID lcid = LocaleNameToLCID(name, LOCALE_ALLOW_NEUTRAL_NAMES);
    if (!lcid || !KbIsSupportedLcid(lcid)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    info->dwNLSVersion = KB_NLS_VERSION;
    info->dwDefinedVersion = KB_NLS_DEFINED_VERSION;
    info->dwEffectiveId = lcid;
    memset(&info->guidCustomVersion, 0, sizeof(info->guidCustomVersion));
    return TRUE;
}

BOOL WINAPI IsNLSDefinedString(NLS_FUNCTION function, DWORD flags, LPNLSVERSIONINFO info, LPCWSTR string, INT length)
{
    if (function != COMPARE_STRING || !string || length < -1 || length == 0) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (length < 0)
        length = (int)wcslen(string);
    for (int i = 0; i < length; i++) {
        WCHAR c = string[i];
        if (c >= 0xD800 && c <= 0xDBFF) {
            if (i + 1 >= length || string[i + 1] < 0xDC00 || string[i + 1] > 0xDFFF)
                return FALSE;
            i++;
            continue;
        }
        if ((c >= 0xDC00 && c <= 0xDFFF) || c == 0xFFFE || c == 0xFFFF || !(KbCharType1(c) & C1_DEFINED))
            return FALSE;
    }
    return TRUE;
}
