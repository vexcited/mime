/*
 * string comparison, searching, mapping, folding, classification and
 * normalization.
 */
#include "kernelbase_private.h"

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

/* letters without a decomposition that still sort with a base letter. */
static WCHAR base_letter(WCHAR c, BOOL *accented)
{
    const DECOMPOSITION *d = find_decomposition(c);
    *accented = TRUE;
    if (d)
        return d->Base;
    switch (c) {
    case 0xD8:
        return 'O';
    case 0xF8:
        return 'o';
    case 0xD0:
    case 0x110:
        return 'D';
    case 0xF0:
    case 0x111:
        return 'd';
    case 0x126:
        return 'H';
    case 0x127:
        return 'h';
    case 0x131:
        return 'i';
    case 0x138:
        return 'k';
    case 0x13F:
    case 0x141:
        return 'L';
    case 0x140:
    case 0x142:
        return 'l';
    case 0x166:
        return 'T';
    case 0x167:
        return 't';
    case 0x17F:
        return 's';
    }
    *accented = FALSE;
    return c;
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

/* ------------------------------------------------------------------ sort weights */

enum { CLASS_IGNORE, CLASS_SPECIAL, CLASS_SYMBOL, CLASS_DIGIT, CLASS_LETTER, CLASS_OTHER };

typedef struct SORT_ELEMENT {
    ULONG Primary;   /* class in the top bits. */
    USHORT Secondary;
    USHORT Tertiary;
    BYTE Class;
    USHORT Position; /* index in the source, for word sort specials. */
} SORT_ELEMENT;

#define TERTIARY_UPPER 1
#define TERTIARY_WIDE 2
#define TERTIARY_KATAKANA 4

typedef struct SORT_LIST {
    SORT_ELEMENT *Items;
    int Count;
    int Capacity;
    SORT_ELEMENT Inline[128];
} SORT_LIST;

static BOOL list_push(SORT_LIST *l, const SORT_ELEMENT *e)
{
    if (l->Count == l->Capacity) {
        int capacity = l->Capacity * 2;
        SORT_ELEMENT *items = KbAlloc((SIZE_T)capacity * sizeof(SORT_ELEMENT));
        if (!items)
            return FALSE;
        memcpy(items, l->Items, (SIZE_T)l->Count * sizeof(SORT_ELEMENT));
        if (l->Items != l->Inline)
            KbFree(l->Items);
        l->Items = items;
        l->Capacity = capacity;
    }
    l->Items[l->Count++] = *e;
    return TRUE;
}

static void list_free(SORT_LIST *l)
{
    if (l->Items != l->Inline)
        KbFree(l->Items);
}

static int digit_value(WCHAR c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 0xFF10 && c <= 0xFF19)
        return c - 0xFF10;
    return -1;
}

/* builds the sort elements of a string under the given flags. */
static BOOL build_elements(LPCWSTR s, int len, DWORD flags, SORT_LIST *out)
{
    out->Items = out->Inline;
    out->Count = 0;
    out->Capacity = RTL_NUMBER_OF(out->Inline);
    for (int i = 0; i < len; i++) {
        WCHAR c = s[i];
        SORT_ELEMENT e = {};
        e.Position = (USHORT)(i < 0xFFFF ? i : 0xFFFF);
        if (!c)
            continue;
        if (flags & NORM_IGNOREWIDTH)
            c = narrow(c);
        else if (is_fullwidth(c)) {
            e.Tertiary |= TERTIARY_WIDE;
            c = narrow(c);
        }
        if (is_katakana(c)) {
            if (!(flags & NORM_IGNOREKANATYPE))
                e.Tertiary |= TERTIARY_KATAKANA;
            c = (WCHAR)(c - 0x60);
        }
        if (is_combining(c)) {
            /* a mark after a plain letter weighs like the precomposed letter. */
            if (out->Count) {
                SORT_ELEMENT *prev = &out->Items[out->Count - 1];
                WCHAR composed = prev->Class == CLASS_LETTER && !prev->Secondary
                                     ? compose((WCHAR)(prev->Primary & 0xFFFF), c)
                                     : 0;
                if (composed) {
                    prev->Secondary = composed;
                    continue;
                }
            }
            e.Class = CLASS_IGNORE;
            e.Secondary = c;
            if (!list_push(out, &e))
                return FALSE;
            continue;
        }
        WORD type = KbCharType1(c);
        if (!(flags & SORT_STRINGSORT) && (c == '-' || c == '\'' || c == 0x2010 || c == 0x2011 || c == 0xAD)) {
            e.Class = CLASS_SPECIAL;
            e.Primary = c;
            if (!(flags & NORM_IGNORESYMBOLS) && !list_push(out, &e))
                return FALSE;
            continue;
        }
        int digit = digit_value(c);
        if (digit >= 0) {
            e.Class = CLASS_DIGIT;
            if (flags & SORT_DIGITSASNUMBERS) {
                /* a run of digits compares by numeric value: its length first, then its digits. */
                int j = i;
                while (j < len && digit_value(s[j]) == 0 && j + 1 < len && digit_value(s[j + 1]) >= 0)
                    j++;
                int start = j;
                while (j < len && digit_value(s[j]) >= 0)
                    j++;
                SORT_ELEMENT lengthe = e;
                lengthe.Primary = ((ULONG)CLASS_DIGIT << 24) | (ULONG)(j - start);
                if (!list_push(out, &lengthe))
                    return FALSE;
                for (int k = start; k < j; k++) {
                    SORT_ELEMENT d = e;
                    d.Primary = ((ULONG)CLASS_DIGIT << 24) | 0x10000 | (ULONG)digit_value(s[k]);
                    if (!list_push(out, &d))
                        return FALSE;
                }
                i = j - 1;
                continue;
            }
            e.Primary = ((ULONG)CLASS_DIGIT << 24) | (ULONG)digit;
            if (!list_push(out, &e))
                return FALSE;
            continue;
        }
        if (type & C1_ALPHA) {
            WCHAR pair[2];
            BOOL upper = (type & C1_UPPER) != 0;
            if (upper)
                e.Tertiary |= TERTIARY_UPPER;
            e.Class = CLASS_LETTER;
            if (expand_ligature(c, pair)) {
                for (int k = 0; k < 2; k++) {
                    SORT_ELEMENT x = e;
                    x.Primary = ((ULONG)CLASS_LETTER << 24) | KbDowncase(pair[k]);
                    x.Secondary = k == 0 ? 1 : 0;
                    if (!list_push(out, &x))
                        return FALSE;
                }
                continue;
            }
            BOOL accented;
            WCHAR base = base_letter(c, &accented);
            e.Primary = ((ULONG)CLASS_LETTER << 24) | KbDowncase(base);
            if (accented)
                e.Secondary = KbDowncase(c);
            if (!list_push(out, &e))
                return FALSE;
            continue;
        }
        if (type & (C1_PUNCT | C1_SPACE | C1_CNTRL)) {
            if (flags & NORM_IGNORESYMBOLS)
                continue;
            e.Class = CLASS_SYMBOL;
            e.Primary = ((ULONG)CLASS_SYMBOL << 24) | c;
            if (!list_push(out, &e))
                return FALSE;
            continue;
        }
        e.Class = CLASS_OTHER;
        e.Primary = ((ULONG)CLASS_OTHER << 24) | c;
        if (!list_push(out, &e))
            return FALSE;
    }
    return TRUE;
}

static BOOL counts_at(const SORT_ELEMENT *e, int level)
{
    if (level == 3)
        return e->Class == CLASS_SPECIAL;
    if (e->Class == CLASS_SPECIAL)
        return FALSE;
    if (e->Class == CLASS_IGNORE)
        return level == 1;
    return TRUE;
}

static ULONG level_weight(const SORT_ELEMENT *e, int level, DWORD flags)
{
    switch (level) {
    case 0:
        return e->Primary;
    case 1:
        return (flags & (NORM_IGNORENONSPACE | LINGUISTIC_IGNOREDIACRITIC)) ? 0 : e->Secondary;
    case 2: {
        USHORT t = e->Tertiary;
        if (flags & (NORM_IGNORECASE | LINGUISTIC_IGNORECASE))
            t &= (USHORT)~TERTIARY_UPPER;
        return t;
    }
    default:
        return ((ULONG)e->Position << 16) | (e->Primary & 0xFFFF);
    }
}

static int compare_level(const SORT_LIST *a, const SORT_LIST *b, int level, DWORD flags)
{
    int i = 0, j = 0;
    for (;;) {
        while (i < a->Count && (!counts_at(&a->Items[i], level) || (level == 0 && a->Items[i].Class == CLASS_IGNORE)))
            i++;
        while (j < b->Count && (!counts_at(&b->Items[j], level) || (level == 0 && b->Items[j].Class == CLASS_IGNORE)))
            j++;
        if (i >= a->Count || j >= b->Count)
            break;
        ULONG wa = level_weight(&a->Items[i], level, flags);
        ULONG wb = level_weight(&b->Items[j], level, flags);
        /* an element without an accent weight still takes part at the accent level. */
        if (wa != wb)
            return wa < wb ? CSTR_LESS_THAN : CSTR_GREATER_THAN;
        i++;
        j++;
    }
    /* trailing elements weigh nothing when ignored at this level. */
    BOOL more_a = FALSE, more_b = FALSE;
    for (; i < a->Count; i++)
        if (counts_at(&a->Items[i], level) && !(level == 0 && a->Items[i].Class == CLASS_IGNORE) &&
            (level == 0 || level == 3 || level_weight(&a->Items[i], level, flags)))
            more_a = TRUE;
    for (; j < b->Count; j++)
        if (counts_at(&b->Items[j], level) && !(level == 0 && b->Items[j].Class == CLASS_IGNORE) &&
            (level == 0 || level == 3 || level_weight(&b->Items[j], level, flags)))
            more_b = TRUE;
    if (more_a != more_b)
        return more_a ? CSTR_GREATER_THAN : CSTR_LESS_THAN;
    return CSTR_EQUAL;
}

static int compare_strings(DWORD flags, LPCWSTR a, int alen, LPCWSTR b, int blen)
{
    SORT_LIST la, lb;
    int result = 0;
    if (!build_elements(a, alen, flags, &la)) {
        list_free(&la);
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    if (!build_elements(b, blen, flags, &lb)) {
        list_free(&la);
        list_free(&lb);
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    for (int level = 0; level < 4; level++) {
        result = compare_level(&la, &lb, level, flags);
        if (result != CSTR_EQUAL)
            break;
    }
    list_free(&la);
    list_free(&lb);
    return result;
}

#define COMPARE_FLAGS                                                                                             \
    (NORM_IGNORECASE | NORM_IGNORENONSPACE | NORM_IGNORESYMBOLS | SORT_DIGITSASNUMBERS | LINGUISTIC_IGNORECASE | \
     LINGUISTIC_IGNOREDIACRITIC | SORT_STRINGSORT | NORM_IGNOREKANATYPE | NORM_IGNOREWIDTH | NORM_LINGUISTIC_CASING)

static int compare_checked(DWORD flags, LPCWSTR a, int alen, LPCWSTR b, int blen)
{
    if (!a || !b) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (flags & ~(DWORD)(COMPARE_FLAGS | LOCALE_USE_CP_ACP)) {
        KbSetError(ERROR_INVALID_FLAGS);
        return 0;
    }
    return compare_strings(flags, a, string_length(a, alen), b, string_length(b, blen));
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
    return compare_checked(flags, a, alen, b, blen);
}

int WINAPI CompareStringW(LCID locale, DWORD flags, PCNZWCH a, int alen, PCNZWCH b, int blen)
{
    /* every locale shares the default sort table. */
    return compare_checked(flags, a, alen, b, blen);
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
        result = compare_checked(flags & ~(DWORD)LOCALE_USE_CP_ACP, wa, wa_len, wb, wb_len);
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

static int find_nls(DWORD flags, LPCWSTR source, int source_len, LPCWSTR value, int value_len, LPINT found_len)
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
            if (compare_strings(compare, source + start, end - start, value, m) == CSTR_EQUAL) {
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
    return find_nls(flags, source, source_len, value, value_len, found_len);
}

int WINAPI FindNLSStringEx(LPCWSTR name, DWORD flags, LPCWSTR source, int source_len, LPCWSTR value, int value_len,
                           LPINT found_len, LPNLSVERSIONINFO version, LPVOID reserved, LPARAM param)
{
    if (reserved || param) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    return find_nls(flags, source, source_len, value, value_len, found_len);
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

static void put_byte(BYTE_OUT *o, BYTE b)
{
    if (o->Size && o->Length < o->Size)
        o->Buffer[o->Length] = b;
    o->Length++;
}

/* a sort key holds the primary, accent, case and special weights, each list closed by 1 and the key by 0. */
static int sort_key(DWORD flags, LPCWSTR src, int len, PBYTE key, int size)
{
    SORT_LIST l;
    BYTE_OUT o = {key, size, 0};
    if (!build_elements(src, len, flags, &l)) {
        list_free(&l);
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    for (int i = 0; i < l.Count; i++) {
        const SORT_ELEMENT *e = &l.Items[i];
        if (e->Class == CLASS_IGNORE || e->Class == CLASS_SPECIAL)
            continue;
        /* class and a sixteen bit weight; digit runs keep their length marker apart. */
        put_byte(&o, (BYTE)(e->Class + 1 + ((e->Primary >> 16) & 1)));
        put_byte(&o, (BYTE)(e->Primary >> 8));
        put_byte(&o, (BYTE)e->Primary);
    }
    put_byte(&o, 1);
    if (!(flags & (NORM_IGNORENONSPACE | LINGUISTIC_IGNOREDIACRITIC))) {
        /* accent weights are trimmed when all trailing ones are empty. */
        int last = -1, k = 0;
        for (int i = 0; i < l.Count; i++)
            if (l.Items[i].Class != CLASS_SPECIAL && l.Items[i].Secondary)
                last = i;
        for (int i = 0; i <= last; i++, k++) {
            const SORT_ELEMENT *e = &l.Items[i];
            if (e->Class == CLASS_SPECIAL)
                continue;
            USHORT s = e->Secondary;
            put_byte(&o, (BYTE)(2 + (s >> 8)));
            put_byte(&o, (BYTE)s);
        }
    }
    put_byte(&o, 1);
    {
        int last = -1;
        for (int i = 0; i < l.Count; i++)
            if (l.Items[i].Class != CLASS_SPECIAL && l.Items[i].Class != CLASS_IGNORE &&
                level_weight(&l.Items[i], 2, flags))
                last = i;
        for (int i = 0; i <= last; i++) {
            const SORT_ELEMENT *e = &l.Items[i];
            if (e->Class == CLASS_SPECIAL || e->Class == CLASS_IGNORE)
                continue;
            put_byte(&o, (BYTE)(2 + level_weight(e, 2, flags)));
        }
    }
    put_byte(&o, 1);
    for (int i = 0; i < l.Count; i++) {
        const SORT_ELEMENT *e = &l.Items[i];
        if (e->Class != CLASS_SPECIAL)
            continue;
        put_byte(&o, (BYTE)(e->Position >> 8));
        put_byte(&o, (BYTE)e->Position);
        put_byte(&o, (BYTE)(e->Primary >> 8));
        put_byte(&o, (BYTE)e->Primary);
    }
    put_byte(&o, 0);
    list_free(&l);
    if (o.Size && o.Length > o.Size) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    return o.Length;
}

static BOOL is_word_start(LPCWSTR s, int i)
{
    if (i == 0)
        return TRUE;
    WORD prev = KbCharType1(s[i - 1]);
    return !(prev & C1_ALPHA) && s[i - 1] != '\'';
}

#define LCMAP_CASE_FLAGS (LCMAP_LOWERCASE | LCMAP_UPPERCASE)
#define LCMAP_VALID                                                                                               \
    (LCMAP_LOWERCASE | LCMAP_UPPERCASE | LCMAP_SORTKEY | LCMAP_BYTEREV | LCMAP_HASH | LCMAP_HIRAGANA |            \
     LCMAP_KATAKANA | LCMAP_HALFWIDTH | LCMAP_FULLWIDTH | LCMAP_LINGUISTIC_CASING | LCMAP_SIMPLIFIED_CHINESE |   \
     LCMAP_TRADITIONAL_CHINESE | NORM_IGNORECASE | NORM_IGNORENONSPACE | NORM_IGNORESYMBOLS | NORM_IGNOREKANATYPE | \
     NORM_IGNOREWIDTH | SORT_STRINGSORT | SORT_DIGITSASNUMBERS | LINGUISTIC_IGNORECASE |                         \
     LINGUISTIC_IGNOREDIACRITIC | NORM_LINGUISTIC_CASING)

static int map_string(DWORD flags, LPCWSTR src, int src_len, LPWSTR dst, int dst_len)
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
        return sort_key(flags & ~(DWORD)LCMAP_SORTKEY, src, key_len, (PBYTE)dst, dst_len);
    }
    if (hash) {
        int key_len = src_len < 0 ? len - 1 : len;
        int need = sort_key(flags & ~(DWORD)LCMAP_HASH, src, key_len, nullptr, 0);
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
        sort_key(flags & ~(DWORD)LCMAP_HASH, src, key_len, key, need);
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
    for (int i = 0; i < len; i++) {
        WCHAR c = src[i];
        if ((flags & NORM_IGNORESYMBOLS) && (KbCharType1(c) & (C1_PUNCT | C1_SPACE)))
            continue;
        if ((flags & NORM_IGNORENONSPACE) && is_combining(c))
            continue;
        if (flags & NORM_IGNORENONSPACE) {
            const DECOMPOSITION *d = find_decomposition(c);
            if (d)
                c = d->Base;
        }
        if (casing == LCMAP_TITLECASE)
            c = is_word_start(src, i) ? KbUpcase(c) : KbDowncase(c);
        else if (casing == LCMAP_LOWERCASE)
            c = KbDowncase(c);
        else if (casing == LCMAP_UPPERCASE)
            c = KbUpcase(c);
        if ((flags & LCMAP_HIRAGANA) && is_katakana(c))
            c = (WCHAR)(c - 0x60);
        else if ((flags & LCMAP_KATAKANA) && is_hiragana(c))
            c = (WCHAR)(c + 0x60);
        if (flags & (LCMAP_HALFWIDTH | NORM_IGNOREWIDTH))
            c = narrow(c);
        else if (flags & LCMAP_FULLWIDTH)
            c = widen(c);
        if (flags & LCMAP_BYTEREV)
            c = (WCHAR)((c >> 8) | (c << 8));
        if (dst_len) {
            if (out >= dst_len) {
                KbSetError(ERROR_INSUFFICIENT_BUFFER);
                return 0;
            }
            dst[out] = c;
        }
        out++;
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
    return map_string(flags, src, src_len, dst, dst_len);
}

int WINAPI LCMapStringW(LCID locale, DWORD flags, LPCWSTR src, int src_len, LPWSTR dst, int dst_len)
{
    return map_string(flags, src, src_len, dst, dst_len);
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
        result = map_string(flags, wsrc, src_len < 0 ? -1 : wlen, (LPWSTR)dst, dst_len);
    } else {
        int need = map_string(flags, wsrc, wlen, nullptr, 0);
        PWSTR wdst = need ? KbAlloc((SIZE_T)need * sizeof(WCHAR)) : nullptr;
        if (wdst) {
            map_string(flags, wsrc, wlen, wdst, need);
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

/* compatibility mappings used by the kc and kd forms; values may hold two characters. */
static int compatibility(WCHAR c, WCHAR out[3])
{
    if (c >= 0xFF01 && c <= 0xFF5E) {
        out[0] = (WCHAR)(c - 0xFF01 + 0x21);
        return 1;
    }
    switch (c) {
    case 0x00A0:
    case 0x3000:
    case 0x2002:
    case 0x2003:
        out[0] = ' ';
        return 1;
    case 0x00A8:
        out[0] = ' ', out[1] = DIAERESIS;
        return 2;
    case 0x00AA:
        out[0] = 'a';
        return 1;
    case 0x00AF:
        out[0] = ' ', out[1] = MACRON;
        return 2;
    case 0x00B2:
    case 0x00B3:
        out[0] = (WCHAR)('2' + c - 0xB2);
        return 1;
    case 0x00B4:
        out[0] = ' ', out[1] = ACUTE;
        return 2;
    case 0x00B5:
        out[0] = 0x03BC;
        return 1;
    case 0x00B8:
        out[0] = ' ', out[1] = CEDILLA;
        return 2;
    case 0x00B9:
        out[0] = '1';
        return 1;
    case 0x00BA:
        out[0] = 'o';
        return 1;
    case 0x00BC:
        out[0] = '1', out[1] = 0x2044, out[2] = '4';
        return 3;
    case 0x00BD:
        out[0] = '1', out[1] = 0x2044, out[2] = '2';
        return 3;
    case 0x00BE:
        out[0] = '3', out[1] = 0x2044, out[2] = '4';
        return 3;
    case 0x0132:
        out[0] = 'I', out[1] = 'J';
        return 2;
    case 0x0133:
        out[0] = 'i', out[1] = 'j';
        return 2;
    case 0x013F:
        out[0] = 'L', out[1] = 0x00B7;
        return 2;
    case 0x0140:
        out[0] = 'l', out[1] = 0x00B7;
        return 2;
    case 0x0149:
        out[0] = 0x02BC, out[1] = 'n';
        return 2;
    case 0x017F:
        out[0] = 's';
        return 1;
    case 0x2026:
        out[0] = '.', out[1] = '.', out[2] = '.';
        return 3;
    case 0x2122:
        out[0] = 'T', out[1] = 'M';
        return 2;
    case 0xFB00:
        out[0] = 'f', out[1] = 'f';
        return 2;
    case 0xFB01:
        out[0] = 'f', out[1] = 'i';
        return 2;
    case 0xFB02:
        out[0] = 'f', out[1] = 'l';
        return 2;
    }
    return 0;
}

static int normalize(NORM_FORM form, LPCWSTR src, int len, LPWSTR dst, int dst_len)
{
    BOOL compat = form == NormalizationKC || form == NormalizationKD;
    BOOL composed = form == NormalizationC || form == NormalizationKC;
    WIDE_OUT o = {dst, dst_len, 0, FALSE};
    WCHAR pending = 0; /* a starter waiting for a combining mark, composed forms only. */
    BOOL have_pending = FALSE;
    for (int i = 0; i < len; i++) {
        WCHAR c = src[i];
        if (c >= 0xD800 && c <= 0xDBFF) {
            if (i + 1 >= len || src[i + 1] < 0xDC00 || src[i + 1] > 0xDFFF) {
                KbSetError(ERROR_NO_UNICODE_TRANSLATION);
                return -(i + 1);
            }
        } else if (c >= 0xDC00 && c <= 0xDFFF && (i == 0 || src[i - 1] < 0xD800 || src[i - 1] > 0xDBFF)) {
            KbSetError(ERROR_NO_UNICODE_TRANSLATION);
            return -(i + 1);
        }
        WCHAR seq[4];
        int count = 0;
        WCHAR extra[3];
        int e = compat ? compatibility(c, extra) : 0;
        if (e) {
            for (int k = 0; k < e; k++)
                seq[count++] = extra[k];
        } else {
            const DECOMPOSITION *d = find_decomposition(c);
            if (d) {
                seq[count++] = d->Base;
                seq[count++] = d->Mark;
            } else {
                seq[count++] = c;
            }
        }
        for (int k = 0; k < count; k++) {
            WCHAR x = seq[k];
            if (!composed) {
                put_wide(&o, x);
                continue;
            }
            if (have_pending && is_combining(x)) {
                WCHAR combined = compose(pending, x);
                if (combined) {
                    pending = combined;
                    continue;
                }
            }
            if (have_pending)
                put_wide(&o, pending);
            pending = x;
            have_pending = TRUE;
        }
    }
    if (have_pending)
        put_wide(&o, pending);
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
