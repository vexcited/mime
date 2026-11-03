#include "kernelbase_private.h"
#include "nlssort.h"
#include "nls_sort.h"

void *NlsSortAlloc(size_t size)
{
    return KbAlloc(size);
}

void NlsSortFree(void *p)
{
    KbFree(p);
}

/* the sort order of a sort locale name: the registry of Windows keeps the names with an order each, and a name that is not
 * there is tried again without what follows its last hyphen or underscore, until the default order is left. */
const NLS_SORT_ORDER *KbSortOrderByName(PCWSTR name)
{
    char text[88];
    SIZE_T n = 0;
    if (name) {
        for (; name[n] && n < sizeof(text) - 1; n++) {
            WCHAR c = name[n];
            if (c > 0x7F)
                return &g_nls_sort_orders[g_nls_sort_ids[0].Sort];
            text[n] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        if (name[n])
            return &g_nls_sort_orders[g_nls_sort_ids[0].Sort];
    }
    text[n] = 0;
    while (n) {
        int lo = 1, hi = (int)RTL_NUMBER_OF(g_nls_sort_ids) - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            const char *a = g_nls_sort_ids[mid].Name, *b = text;
            while (*a && *a == *b) {
                a++;
                b++;
            }
            int diff = (int)(unsigned char)*b - (int)(unsigned char)*a;
            if (!diff)
                return &g_nls_sort_orders[g_nls_sort_ids[mid].Sort];
            if (diff > 0)
                lo = mid + 1;
            else
                hi = mid - 1;
        }
        SIZE_T cut = n;
        while (cut > 1 && text[cut - 1] != '-' && text[cut - 1] != '_')
            cut--;
        if (cut <= 1)
            break;
        n = cut - 1;
        text[n] = 0;
    }
    return &g_nls_sort_orders[g_nls_sort_ids[0].Sort];
}

int KbSortKey(const NLS_SORT_ORDER *order, DWORD flags, PCWSTR src, int len, PBYTE key, int size)
{
    int need = NlsSortKey(&g_nls_sort_tables, order, flags, src, len, key, size);
    if (need == -1) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    if (need < 0) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    return need;
}

DWORD KbSortHash(const NLS_SORT_ORDER *order, DWORD flags, PCWSTR src, int len)
{
    return NlsSortHash(&g_nls_sort_tables, order, flags, src, len);
}

int KbCompareStrings(const NLS_SORT_ORDER *order, DWORD flags, PCWSTR a, int alen, PCWSTR b, int blen)
{
    int result = NlsSortCompare(&g_nls_sort_tables, order, flags, a, alen, b, blen);
    if (!result)
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
    return result;
}

int NlsSortFindString(const NLS_SORT_ORDER *order, DWORD flags, PCWSTR source, int n, PCWSTR value, int m, int *found)
{
    return NlsSortFind(&g_nls_sort_tables, order, flags, source, n, value, m, found);
}

BOOL KbSortIsDefined(PCWSTR text, int length)
{
    return NlsSortIsDefined(&g_nls_sort_tables, KbSortOrderByName(nullptr), text, length);
}

DWORD KbSortId(const NLS_SORT_ORDER *order)
{
    return g_nls_sort_guids[order - g_nls_sort_orders];
}

DWORD KbSortVersion(void)
{
    return NLS_SORT_VERSION;
}
