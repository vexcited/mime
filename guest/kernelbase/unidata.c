/*
 * Unicode character data: combining classes, decompositions, composition and simple case mappings, from the tables
 * tools/nls/genunicode.py makes of the Unicode Character Database.
 */
#include "kernelbase_private.h"
#include "nls_unidata.h"

/* ------------------------------------------------------------------ Hangul */

#define HANGUL_S_BASE 0xAC00
#define HANGUL_L_BASE 0x1100
#define HANGUL_V_BASE 0x1161
#define HANGUL_T_BASE 0x11A7
#define HANGUL_L_COUNT 19
#define HANGUL_V_COUNT 21
#define HANGUL_T_COUNT 28
#define HANGUL_N_COUNT (HANGUL_V_COUNT * HANGUL_T_COUNT)
#define HANGUL_S_COUNT (HANGUL_L_COUNT * HANGUL_N_COUNT)

/* ------------------------------------------------------------------ lookups */

/* the position of a key in a sorted array of 32 bit values, or -1. */
static LONG find32(const UINT32 *keys, ULONG count, ULONG key)
{
    LONG lo = 0, hi = (LONG)count - 1;
    while (lo <= hi) {
        LONG mid = (lo + hi) / 2;
        if (keys[mid] == key)
            return mid;
        if (keys[mid] < key)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}

UINT KbUniCcc(ULONG cp)
{
    if (cp < 0x300)
        return 0;
    if (cp >= 0x110000)
        return 0;
    return g_uni_ccc_blocks[g_uni_ccc_index[cp >> 7] * 128 + (cp & 127)];
}

BOOL KbUniIsNonspacing(ULONG cp)
{
    if (cp < 0x300 || cp >= 0x110000)
        return FALSE;
    return g_uni_mark_blocks[g_uni_mark_index[cp >> 7] * 128 + (cp & 127)] != 0;
}

/* the units of the full decomposition of a character, or 0 when it has none. */
int KbUniDecompose(ULONG cp, BOOL compat, WCHAR *out)
{
    if (cp < 0xC0)
        return 0;
    if (cp >= HANGUL_S_BASE && cp < HANGUL_S_BASE + HANGUL_S_COUNT) {
        ULONG s = cp - HANGUL_S_BASE;
        int n = 0;
        out[n++] = (WCHAR)(HANGUL_L_BASE + s / HANGUL_N_COUNT);
        out[n++] = (WCHAR)(HANGUL_V_BASE + (s % HANGUL_N_COUNT) / HANGUL_T_COUNT);
        if (s % HANGUL_T_COUNT)
            out[n++] = (WCHAR)(HANGUL_T_BASE + s % HANGUL_T_COUNT);
        return n;
    }
    LONG i = -1;
    const UINT16 *pool = nullptr;
    const UINT16 *offsets = nullptr;
    const UINT8 *lengths = nullptr;
    if (compat) {
        i = find32(g_uni_compat_cp, RTL_NUMBER_OF(g_uni_compat_cp), cp);
        pool = g_uni_compat_pool;
        offsets = g_uni_compat_off;
        lengths = g_uni_compat_len;
    }
    if (i < 0) {
        i = find32(g_uni_canon_cp, RTL_NUMBER_OF(g_uni_canon_cp), cp);
        pool = g_uni_canon_pool;
        offsets = g_uni_canon_off;
        lengths = g_uni_canon_len;
    }
    if (i < 0)
        return 0;
    int n = lengths[i];
    memcpy(out, pool + offsets[i], n * sizeof(WCHAR));
    return n;
}

/* the character a pair composes to, or 0. */
ULONG KbUniCompose(ULONG first, ULONG second, BOOL hangul)
{
    if (hangul && first >= HANGUL_L_BASE && first < HANGUL_L_BASE + HANGUL_L_COUNT && second >= HANGUL_V_BASE &&
        second < HANGUL_V_BASE + HANGUL_V_COUNT)
        return HANGUL_S_BASE + ((first - HANGUL_L_BASE) * HANGUL_V_COUNT + (second - HANGUL_V_BASE)) * HANGUL_T_COUNT;
    if (hangul && first >= HANGUL_S_BASE && first < HANGUL_S_BASE + HANGUL_S_COUNT && (first - HANGUL_S_BASE) % HANGUL_T_COUNT == 0 &&
        second > HANGUL_T_BASE && second < HANGUL_T_BASE + HANGUL_T_COUNT)
        return first + (second - HANGUL_T_BASE);
    LONG lo = 0, hi = (LONG)RTL_NUMBER_OF(g_uni_comp_first) - 1;
    while (lo <= hi) {
        LONG mid = (lo + hi) / 2;
        if (g_uni_comp_first[mid] == first) {
            /* the entries of one first character are together, sorted by the second. */
            LONG a = mid;
            while (a > 0 && g_uni_comp_first[a - 1] == first)
                a--;
            for (; a < (LONG)RTL_NUMBER_OF(g_uni_comp_first) && g_uni_comp_first[a] == first; a++)
                if (g_uni_comp_second[a] == second)
                    return g_uni_comp_result[a];
            return 0;
        }
        if (g_uni_comp_first[mid] < first)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ case */

static ULONG case_lookup(ULONG cp, const UINT16 *index, const INT32 *blocks, const UINT32 *sup_cp, const UINT32 *sup_to,
                         ULONG sup_count)
{
    if (cp < 0x10000)
        return (ULONG)((LONG)cp + blocks[index[cp >> 6] * 64 + (cp & 63)]);
    LONG i = find32(sup_cp, sup_count, cp);
    return i < 0 ? cp : sup_to[i];
}

ULONG KbUniUpper(ULONG cp)
{
    if (cp < 0x80)
        return (cp >= 'a' && cp <= 'z') ? cp - 32 : cp;
    return case_lookup(cp, g_uni_upper_index, g_uni_upper_blocks, g_uni_case_sup_cp, g_uni_case_sup_upper,
                       RTL_NUMBER_OF(g_uni_case_sup_cp));
}

ULONG KbUniLower(ULONG cp)
{
    if (cp < 0x80)
        return (cp >= 'A' && cp <= 'Z') ? cp + 32 : cp;
    return case_lookup(cp, g_uni_lower_index, g_uni_lower_blocks, g_uni_case_sup_cp, g_uni_case_sup_lower,
                       RTL_NUMBER_OF(g_uni_case_sup_cp));
}

ULONG KbUniFold(ULONG cp)
{
    if (cp < 0x80)
        return (cp >= 'A' && cp <= 'Z') ? cp + 32 : cp;
    return case_lookup(cp, g_uni_fold_index, g_uni_fold_blocks, g_uni_case_sup_cp, g_uni_case_sup_fold,
                       RTL_NUMBER_OF(g_uni_case_sup_cp));
}
