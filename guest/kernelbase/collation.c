/*
 * string sort order: Unicode collation algorithm elements with the CLDR root order and per language tailoring,
 * compared level by level.
 *
 * a string is canonically decomposed and each position yields the elements of the longest matching entry. elements
 * compare by primary, then secondary, then tertiary weight. the hyphen and apostrophe of a word sort have no weight at
 * those levels and are compared last.
 */
#include "kernelbase_private.h"
#include "nls_coll.h"

typedef NLS_COLLCE CE;

#define F_VARIABLE 1
#define F_SYMBOL 2
#define F_SPECIAL 4 /* a word sort hyphen or apostrophe. */
#define F_DIGITS 8  /* holds the length of a number. */

#define P_COMMON ((ULONG)0x2000 * NLS_COLL_PRIMARY_SCALE)

typedef struct CE_LIST {
    CE *Items;
    int Count;
    int Capacity;
    CE Inline[160];
} CE_LIST;

static BOOL ce_push(CE_LIST *l, CE ce)
{
    if (l->Count == l->Capacity) {
        int capacity = l->Capacity * 2;
        CE *items = KbAlloc((SIZE_T)capacity * sizeof(CE));
        if (!items)
            return FALSE;
        memcpy(items, l->Items, (SIZE_T)l->Count * sizeof(CE));
        if (l->Items != l->Inline)
            KbFree(l->Items);
        l->Items = items;
        l->Capacity = capacity;
    }
    l->Items[l->Count++] = ce;
    return TRUE;
}

static void ce_init(CE_LIST *l)
{
    l->Items = l->Inline;
    l->Count = 0;
    l->Capacity = RTL_NUMBER_OF(l->Inline);
}

static void ce_free(CE_LIST *l)
{
    if (l->Items != l->Inline)
        KbFree(l->Items);
}

/* ------------------------------------------------------------------ characters */

typedef struct CP_LIST {
    ULONG *Items;
    int Count;
    int Capacity;
    ULONG Inline[160];
} CP_LIST;

static BOOL cp_push(CP_LIST *l, ULONG cp)
{
    if (l->Count == l->Capacity) {
        int capacity = l->Capacity * 2;
        ULONG *items = KbAlloc((SIZE_T)capacity * sizeof(ULONG));
        if (!items)
            return FALSE;
        memcpy(items, l->Items, (SIZE_T)l->Count * sizeof(ULONG));
        if (l->Items != l->Inline)
            KbFree(l->Items);
        l->Items = items;
        l->Capacity = capacity;
    }
    l->Items[l->Count++] = cp;
    return TRUE;
}

static void cp_init(CP_LIST *l)
{
    l->Items = l->Inline;
    l->Count = 0;
    l->Capacity = RTL_NUMBER_OF(l->Inline);
}

static void cp_free(CP_LIST *l)
{
    if (l->Items != l->Inline)
        KbFree(l->Items);
}

/* the narrow form of a wide character, for comparisons that ignore width. */
static ULONG narrow_form(ULONG c)
{
    if (c >= 0xFF01 && c <= 0xFF5E)
        return c - 0xFF01 + 0x21;
    if (c == 0x3000)
        return ' ';
    switch (c) {
    case 0xFFE0:
        return 0xA2;
    case 0xFFE1:
        return 0xA3;
    case 0xFFE2:
        return 0xAC;
    case 0xFFE3:
        return 0xAF;
    case 0xFFE4:
        return 0xA6;
    case 0xFFE5:
        return 0xA5;
    case 0xFFE6:
        return 0x20A9;
    }
    /* half width katakana and hangul marks map to their full width forms. */
    WCHAR out[20];
    if (c >= 0xFF61 && c <= 0xFFDC) {
        int n = KbUniDecompose(c, TRUE, out);
        if (n == 1)
            return out[0];
    }
    return c;
}

static ULONG kana_fold(ULONG c)
{
    if (c >= 0x30A1 && c <= 0x30F6)
        return c - 0x60;
    if (c == 0x30FD || c == 0x30FE)
        return c - 0x60;
    return c;
}

/* decomposes to code points, mapped as the flags ask, with marks in canonical order. */
static BOOL decompose_string(PCWSTR s, int len, DWORD flags, BOOL turkic, CP_LIST *out)
{
    cp_init(out);
    int start = 0;
    for (int i = 0; i < len; i++) {
        ULONG c = s[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < len && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            i++;
        }
        if (flags & NORM_IGNOREWIDTH)
            c = narrow_form(c);
        if (flags & NORM_IGNOREKANATYPE)
            c = kana_fold(c);
        if (flags & (NORM_IGNORECASE | LINGUISTIC_IGNORECASE)) {
            if (turkic && (flags & (LINGUISTIC_IGNORECASE | NORM_LINGUISTIC_CASING)) && c == 'I')
                c = 0x131;
            else if (turkic && (flags & (LINGUISTIC_IGNORECASE | NORM_LINGUISTIC_CASING)) && c == 0x130)
                c = 'i';
            else
                c = KbUniFold(c);
        }
        WCHAR parts[20];
        int n = c >= 0xC0 ? KbUniDecompose(c, FALSE, parts) : 0;
        if (!n) {
            if (!cp_push(out, c))
                return FALSE;
            continue;
        }
        for (int k = 0; k < n; k++) {
            ULONG p = parts[k];
            if (p >= 0xD800 && p < 0xDC00 && k + 1 < n) {
                p = 0x10000 + ((p - 0xD800) << 10) + (parts[k + 1] - 0xDC00);
                k++;
            }
            if (!cp_push(out, p))
                return FALSE;
        }
    }
    /* canonical ordering: a stable sort of each run of characters with a combining class. */
    for (int i = start; i < out->Count;) {
        if (!KbUniCcc(out->Items[i])) {
            i++;
            continue;
        }
        int j = i;
        while (j < out->Count && KbUniCcc(out->Items[j]))
            j++;
        for (int a = i + 1; a < j; a++) {
            ULONG c = out->Items[a];
            UINT cc = KbUniCcc(c);
            int b = a - 1;
            while (b >= i && KbUniCcc(out->Items[b]) > cc) {
                out->Items[b + 1] = out->Items[b];
                b--;
            }
            out->Items[b + 1] = c;
        }
        i = j;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ collation elements */

static CE make_ce(ULONG p, UINT s, UINT t, UINT f)
{
    CE ce = {p, (UINT16)s, (UINT8)t, (UINT8)f};
    return ce;
}

/* implicit weights for characters missing from the tables: Han and unassigned ones. */
static BOOL implicit_ces(ULONG cp, CE_LIST *out)
{
    ULONG base = 0xFBC0;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_coll_ideographs); i++) {
        if (cp >= g_coll_ideographs[i][0] && cp <= g_coll_ideographs[i][1]) {
            /* the two common ideograph blocks weigh before the rest. */
            base = (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xFA0E && cp <= 0xFA29) ? 0xFB40 : 0xFB80;
            break;
        }
    }
    if (base == 0xFBC0) {
        if ((cp >= 0x17000 && cp <= 0x18AFF) || (cp >= 0x18D00 && cp <= 0x18DFF))
            base = 0xFB00;
        else if (cp >= 0x1B170 && cp <= 0x1B2FF)
            base = 0xFB01;
        else if (cp >= 0x18B00 && cp <= 0x18CFF)
            base = 0xFB02;
    }
    ULONG first = base + (base < 0xFB40 ? 0 : (cp >> 15));
    ULONG second = (cp & 0x7FFF) | 0x8000;
    if (base == 0xFB00) {
        /* Tangut ideographs, then components, each in code point order. */
        ULONG index;
        if (cp <= 0x187FF)
            index = cp - 0x17000;
        else if (cp >= 0x18D00 && cp <= 0x18D7F)
            index = 0x1800 + (cp - 0x18D00);
        else if (cp <= 0x18AFF)
            index = 0x1880 + (cp - 0x18800);
        else
            index = 0x1880 + 0x300 + (cp - 0x18D80);
        second = index | 0x8000;
    } else if (base == 0xFB01) {
        second = (cp - 0x1B170) | 0x8000;
    } else if (base == 0xFB02) {
        second = (cp - 0x18B00) | 0x8000;
    }
    return ce_push(out, make_ce(first * NLS_COLL_PRIMARY_SCALE, 0x20 * NLS_COLL_SECONDARY_SCALE,
                                0x02 * NLS_COLL_TERTIARY_SCALE, 0)) &&
           ce_push(out, make_ce(second * NLS_COLL_PRIMARY_SCALE, 0, 0, 0));
}

static BOOL cps_equal(const ULONG *a, const UINT32 *b, int n)
{
    for (int i = 0; i < n; i++)
        if (a[i] != b[i])
            return FALSE;
    return TRUE;
}

/* the longest entry of a sorted string table that is a prefix of the characters. */
static const NLS_COLLENTRY *longest_entry(const NLS_COLLENTRY *table, ULONG count, const ULONG *cps, int available)
{
    LONG lo = 0, hi = (LONG)count - 1, found = -1;
    while (lo <= hi) {
        LONG mid = (lo + hi) / 2;
        UINT32 first = g_coll_cps[table[mid].CpOffset];
        if (first < cps[0]) {
            lo = mid + 1;
        } else {
            if (first == cps[0])
                found = mid;
            hi = mid - 1;
        }
    }
    if (found < 0)
        return nullptr;
    const NLS_COLLENTRY *best = nullptr;
    for (LONG i = found; i < (LONG)count && g_coll_cps[table[i].CpOffset] == cps[0]; i++) {
        const NLS_COLLENTRY *e = &table[i];
        if (e->CpCount <= available && (!best || e->CpCount > best->CpCount) &&
            cps_equal(cps, &g_coll_cps[e->CpOffset], e->CpCount))
            best = e;
    }
    return best;
}

static BOOL push_entry(CE_LIST *out, const NLS_COLLENTRY *e)
{
    for (int k = 0; k < e->CeCount; k++)
        if (!ce_push(out, g_coll_ces[e->CeOffset + k]))
            return FALSE;
    return TRUE;
}

static BOOL is_special_char(ULONG c)
{
    return c == '-' || c == '\'' || c == 0x2010 || c == 0x2011 || c == 0xFF0D || c == 0xFF07;
}

static BOOL is_digit_char(ULONG c, int *value)
{
    if (c >= '0' && c <= '9') {
        *value = (int)(c - '0');
        return TRUE;
    }
    if (c >= 0xFF10 && c <= 0xFF19) {
        *value = (int)(c - 0xFF10);
        return TRUE;
    }
    if (c >= 0x0660 && c <= 0x0669) {
        *value = (int)(c - 0x0660);
        return TRUE;
    }
    if (c >= 0x06F0 && c <= 0x06F9) {
        *value = (int)(c - 0x06F0);
        return TRUE;
    }
    return FALSE;
}

static const NLS_COLLENTRY *exact_entry(const NLS_COLLENTRY *table, ULONG count, const ULONG *cps, int n)
{
    const NLS_COLLENTRY *e = longest_entry(table, count, cps, n);
    return e && e->CpCount == n ? e : nullptr;
}

/* an entry of the collator's tailoring, or of the root contractions, for these characters. */

static const NLS_COLLENTRY *find_exact(const NLS_COLLATOR *tail, const ULONG *cps, int n)
{
    const NLS_COLLENTRY *e = nullptr;
    if (tail->Count)
        e = exact_entry(&g_coll_tailored[tail->First], tail->Count, cps, n);
    if (!e)
        e = exact_entry(g_coll_root_contractions, RTL_NUMBER_OF(g_coll_root_contractions), cps, n);
    return e;
}

/* pushes the elements at the position, returns the characters used or 0 on failure. */
static int append_string_ces(UINT collator, CP_LIST *cps, int i, CE_LIST *out)
{
    ULONG c = cps->Items[i];
    int available = cps->Count - i;
    const NLS_COLLATOR *tail = &g_coll_collators[collator];
    const NLS_COLLENTRY *e = nullptr;
    if (tail->Count)
        e = longest_entry(&g_coll_tailored[tail->First], tail->Count, &cps->Items[i], available);
    UINT32 v = c < 0x110000 ? g_coll_blocks[g_coll_index[c >> 7] * 128 + (c & 127)] : 0;
    if (!e && (v & 0x80000000u))
        e = longest_entry(g_coll_root_contractions, RTL_NUMBER_OF(g_coll_root_contractions), &cps->Items[i], available);
    int length = e ? e->CpCount : 1;
    if (e || (v & 0x80000000u) || tail->Count) {
        /*
         * a later mark separated only by marks of lower class joins the preceding characters when they form a
         * contraction. the other marks stay where they are.
         */
        for (int j = i + length; j < cps->Count; j++) {
            UINT cc = KbUniCcc(cps->Items[j]);
            if (!cc)
                break;
            BOOL blocked = FALSE;
            for (int k = i + length; k < j; k++)
                if (KbUniCcc(cps->Items[k]) >= cc)
                    blocked = TRUE;
            if (blocked)
                continue;
            ULONG sequence[8];
            if (length + 1 > 8)
                break;
            memcpy(sequence, &cps->Items[i], (SIZE_T)length * sizeof(ULONG));
            sequence[length] = cps->Items[j];
            const NLS_COLLENTRY *longer = find_exact(tail, sequence, length + 1);
            if (!longer)
                continue;
            ULONG mark = cps->Items[j];
            memmove(&cps->Items[i + length + 1], &cps->Items[i + length], (SIZE_T)(j - (i + length)) * sizeof(ULONG));
            cps->Items[i + length] = mark;
            length++;
            e = longer;
        }
    }
    if (e)
        return push_entry(out, e) ? e->CpCount : 0;
    if (v & 0xFF) {
        for (UINT k = 0; k < (v & 0xFF); k++)
            if (!ce_push(out, g_coll_ces[((v & 0x7FFFFFFFu) >> 8) + k]))
                return 0;
        return 1;
    }
    return implicit_ces(c, out) ? 1 : 0;
}

/* elements of a decomposed string: word sort hyphens and apostrophes are marked, digit runs become numbers. */
static BOOL build_ces(UINT collator, DWORD flags, CP_LIST *cps, CE_LIST *out)
{
    ce_init(out);
    BOOL word_sort = !(flags & SORT_STRINGSORT);
    for (int i = 0; i < cps->Count;) {
        int value;
        if ((flags & SORT_DIGITSASNUMBERS) && is_digit_char(cps->Items[i], &value)) {
            int j = i;
            while (j < cps->Count && is_digit_char(cps->Items[j], &value))
                j++;
            int first = i;
            while (first + 1 < j && is_digit_char(cps->Items[first], &value) && value == 0)
                first++;
            /* length first, so a longer number is greater. */
            int length = j - first;
            CE_LIST zero;
            ce_init(&zero);
            ULONG zero_cp = '0';
            CP_LIST one = {};
            cp_init(&one);
            cp_push(&one, zero_cp);
            append_string_ces(0, &one, 0, &zero);
            cp_free(&one);
            ULONG base = zero.Count ? zero.Items[0].P : P_COMMON;
            ce_free(&zero);
            if (!ce_push(out, make_ce(base + (ULONG)(length < NLS_COLL_PRIMARY_SCALE - 1 ? length : NLS_COLL_PRIMARY_SCALE - 1),
                                      0x20 * NLS_COLL_SECONDARY_SCALE, 0x02 * NLS_COLL_TERTIARY_SCALE, F_DIGITS)))
                return FALSE;
            for (int k = first; k < j; k++)
                if (!append_string_ces(0, cps, k, out))
                    return FALSE;
            i = j;
            continue;
        }
        int before = out->Count;
        int used = append_string_ces(collator, cps, i, out);
        if (!used)
            return FALSE;
        if (word_sort && used == 1 && is_special_char(cps->Items[i]))
            for (int k = before; k < out->Count; k++)
                out->Items[k].F |= F_SPECIAL;
        i += used;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ comparison */

/* a string's elements after the flags: ignored symbols and empty elements are dropped. */
static BOOL prepare(UINT collator, DWORD flags, PCWSTR s, int len, BOOL turkic, CE_LIST *out)
{
    CP_LIST cps;
    CE_LIST raw;
    if (!decompose_string(s, len, flags, turkic, &cps))
        return FALSE;
    if (!build_ces(collator, flags, &cps, &raw)) {
        cp_free(&cps);
        return FALSE;
    }
    cp_free(&cps);
    ce_init(out);
    BOOL ok = TRUE;
    for (int i = 0; i < raw.Count && ok; i++) {
        CE ce = raw.Items[i];
        if (!ce.P && !ce.S && !ce.T)
            continue;
        if ((flags & NORM_IGNORESYMBOLS) && (ce.F & F_SYMBOL))
            continue;
        /* an ignored accent's mark weighs nothing. */
        if ((flags & (NORM_IGNORENONSPACE | LINGUISTIC_IGNOREDIACRITIC)) && !ce.P)
            continue;
        ok = ce_push(out, ce);
    }
    ce_free(&raw);
    if (!ok)
        ce_free(out);
    return ok;
}

static ULONG weight_at(const CE *ce, int level)
{
    if (ce->F & F_SPECIAL)
        return 0;
    return level == 0 ? ce->P : level == 1 ? ce->S : ce->T;
}

static int compare_streams(const CE_LIST *a, const CE_LIST *b, int level, BOOL backwards)
{
    int ia = backwards ? a->Count - 1 : 0, ib = backwards ? b->Count - 1 : 0;
    int step = backwards ? -1 : 1;
    for (;;) {
        ULONG wa = 0, wb = 0;
        while (ia >= 0 && ia < a->Count) {
            wa = weight_at(&a->Items[ia], level);
            ia += step;
            if (wa)
                break;
        }
        while (ib >= 0 && ib < b->Count) {
            wb = weight_at(&b->Items[ib], level);
            ib += step;
            if (wb)
                break;
        }
        if (!wa || !wb) {
            if (wa == wb)
                return 0;
            return wa ? 1 : -1;
        }
        if (wa != wb)
            return wa < wb ? -1 : 1;
    }
}

/* specials compare last: first by position, then the string with more of them is greater. */
static int compare_specials(const CE_LIST *a, const CE_LIST *b)
{
    int i = 0, j = 0;
    for (;;) {
        while (i < a->Count && !(a->Items[i].F & F_SPECIAL))
            i++;
        while (j < b->Count && !(b->Items[j].F & F_SPECIAL))
            j++;
        if (i >= a->Count || j >= b->Count)
            break;
        if (i != j)
            return i < j ? -1 : 1;
        i++;
        j++;
    }
    BOOL more_a = i < a->Count, more_b = j < b->Count;
    if (more_a != more_b)
        return more_a ? 1 : -1;
    return 0;
}

int KbCompareStrings(UINT collator, BOOL turkic, DWORD flags, PCWSTR a, int alen, PCWSTR b, int blen)
{
    CE_LIST la, lb;
    if (!prepare(collator, flags, a, alen, turkic, &la)) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    if (!prepare(collator, flags, b, blen, turkic, &lb)) {
        ce_free(&la);
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    BOOL backwards = (g_coll_collators[collator].Flags & 1) != 0;
    int r = compare_streams(&la, &lb, 0, FALSE);
    if (!r && !(flags & (NORM_IGNORENONSPACE | LINGUISTIC_IGNOREDIACRITIC)))
        r = compare_streams(&la, &lb, 1, backwards);
    if (!r)
        r = compare_streams(&la, &lb, 2, FALSE);
    if (!r)
        r = compare_specials(&la, &lb);
    ce_free(&la);
    ce_free(&lb);
    return r < 0 ? CSTR_LESS_THAN : r > 0 ? CSTR_GREATER_THAN : CSTR_EQUAL;
}

/* ------------------------------------------------------------------ sort keys */

typedef struct KEY_OUT {
    PBYTE Buffer;
    int Size;
    int Length;
} KEY_OUT;

static void key_byte(KEY_OUT *o, BYTE b)
{
    if (o->Size && o->Length < o->Size)
        o->Buffer[o->Length] = b;
    o->Length++;
}

/* a weight as bytes 2 to 255, most significant first, so keys compare like the weights. */
static void key_weight(KEY_OUT *o, ULONG w, int digits)
{
    BYTE bytes[5];
    for (int i = digits - 1; i >= 0; i--) {
        bytes[i] = (BYTE)(2 + w % 254);
        w /= 254;
    }
    for (int i = 0; i < digits; i++)
        key_byte(o, bytes[i]);
}

/* levels one after another, each ended by 1 and the key by 0, so memcmp gives the sort order. */
int KbSortKey(UINT collator, BOOL turkic, DWORD flags, PCWSTR src, int len, PBYTE key, int size)
{
    CE_LIST l;
    if (!prepare(collator, flags, src, len, turkic, &l)) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    KEY_OUT o = {key, size, 0};
    for (int i = 0; i < l.Count; i++) {
        ULONG w = weight_at(&l.Items[i], 0);
        if (w)
            key_weight(&o, w, 4);
    }
    key_byte(&o, 1);
    if (!(flags & (NORM_IGNORENONSPACE | LINGUISTIC_IGNOREDIACRITIC))) {
        BOOL backwards = (g_coll_collators[collator].Flags & 1) != 0;
        for (int n = 0; n < l.Count; n++) {
            int i = backwards ? l.Count - 1 - n : n;
            ULONG w = weight_at(&l.Items[i], 1);
            if (w)
                key_weight(&o, w, 3);
        }
    }
    key_byte(&o, 1);
    for (int i = 0; i < l.Count; i++) {
        ULONG w = weight_at(&l.Items[i], 2);
        if (w)
            key_weight(&o, w, 1);
    }
    key_byte(&o, 1);
    for (int i = 0; i < l.Count; i++) {
        if (!(l.Items[i].F & F_SPECIAL))
            continue;
        key_weight(&o, (ULONG)i, 2);
    }
    key_byte(&o, 0);
    ce_free(&l);
    if (o.Size && o.Length > o.Size) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    return o.Length;
}

/* a collator number by name, 0 for the root order when there is none. */
UINT KbCollatorByName(const char *name)
{
    for (UINT i = 1; i < NLS_COLL_COLLATORS; i++)
        if (!strcmp(g_coll_names[i], name))
            return i;
    return 0;
}
