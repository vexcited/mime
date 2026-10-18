/*
 * the dynamic arrays of pointers and structures, DPA and DSA, and the
 * Str_ string slot helpers. the item count is the first field of both
 * arrays and the pointer table the second field of a DPA, as the
 * DPA_GetPtrCount and DPA_GetPtrPtr macros read them.
 */
#include "comctl32_private.h"

struct _DPA {
    int Count;
    void **Ptrs;
    HANDLE Heap;
    int Grow;
    int Capacity;
};

struct _DSA {
    int Count;
    BYTE *Data;
    int Grow;
    int Capacity;
    int ItemSize;
};

/* ------------------------------------------------------------------ DPA */

WINCOMMCTRLAPI HDPA WINAPI DPA_CreateEx(int grow, HANDLE heap)
{
    if (!heap)
        heap = GetProcessHeap();
    HDPA hdpa = HeapAlloc(heap, HEAP_ZERO_MEMORY, sizeof(*hdpa));
    if (!hdpa)
        return nullptr;
    hdpa->Heap = heap;
    hdpa->Grow = grow < 8 ? 8 : grow;
    return hdpa;
}

WINCOMMCTRLAPI HDPA WINAPI DPA_Create(int grow)
{
    return DPA_CreateEx(grow, nullptr);
}

WINCOMMCTRLAPI BOOL WINAPI DPA_Destroy(HDPA hdpa)
{
    if (!hdpa)
        return TRUE;
    if (hdpa->Ptrs)
        HeapFree(hdpa->Heap, 0, hdpa->Ptrs);
    return HeapFree(hdpa->Heap, 0, hdpa);
}

WINCOMMCTRLAPI BOOL WINAPI DPA_Grow(HDPA hdpa, int count)
{
    if (!hdpa)
        return FALSE;
    if (count <= hdpa->Capacity)
        return TRUE;
    int capacity = (count + hdpa->Grow - 1) / hdpa->Grow * hdpa->Grow;
    void **ptrs = hdpa->Ptrs ? HeapReAlloc(hdpa->Heap, HEAP_ZERO_MEMORY, hdpa->Ptrs, (SIZE_T)capacity * sizeof(void *))
                             : HeapAlloc(hdpa->Heap, HEAP_ZERO_MEMORY, (SIZE_T)capacity * sizeof(void *));
    if (!ptrs)
        return FALSE;
    hdpa->Ptrs = ptrs;
    hdpa->Capacity = capacity;
    return TRUE;
}

WINCOMMCTRLAPI HDPA WINAPI DPA_Clone(HDPA hdpa, HDPA into)
{
    if (!hdpa)
        return nullptr;
    HDPA copy = into ? into : DPA_CreateEx(hdpa->Grow, hdpa->Heap);
    if (!copy)
        return nullptr;
    copy->Count = 0;
    if (!DPA_Grow(copy, hdpa->Count)) {
        if (!into)
            DPA_Destroy(copy);
        return nullptr;
    }
    if (hdpa->Count)
        memcpy(copy->Ptrs, hdpa->Ptrs, (SIZE_T)hdpa->Count * sizeof(void *));
    copy->Count = hdpa->Count;
    return copy;
}

WINCOMMCTRLAPI void *WINAPI DPA_GetPtr(HDPA hdpa, INT_PTR i)
{
    if (!hdpa || i < 0 || i >= hdpa->Count)
        return nullptr;
    return hdpa->Ptrs[i];
}

WINCOMMCTRLAPI int WINAPI DPA_GetPtrIndex(HDPA hdpa, const void *p)
{
    if (!hdpa)
        return -1;
    for (int i = 0; i < hdpa->Count; i++)
        if (hdpa->Ptrs[i] == p)
            return i;
    return -1;
}

WINCOMMCTRLAPI int WINAPI DPA_InsertPtr(HDPA hdpa, int i, void *p)
{
    if (!hdpa || i < 0)
        return -1;
    if (i > hdpa->Count)
        i = hdpa->Count;
    if (!DPA_Grow(hdpa, hdpa->Count + 1))
        return -1;
    memmove(hdpa->Ptrs + i + 1, hdpa->Ptrs + i, (SIZE_T)(hdpa->Count - i) * sizeof(void *));
    hdpa->Ptrs[i] = p;
    hdpa->Count++;
    return i;
}

WINCOMMCTRLAPI BOOL WINAPI DPA_SetPtr(HDPA hdpa, int i, void *p)
{
    if (!hdpa || i < 0)
        return FALSE;
    if (i >= hdpa->Count) {
        if (!DPA_Grow(hdpa, i + 1))
            return FALSE;
        hdpa->Count = i + 1;
    }
    hdpa->Ptrs[i] = p;
    return TRUE;
}

WINCOMMCTRLAPI void *WINAPI DPA_DeletePtr(HDPA hdpa, int i)
{
    if (!hdpa || i < 0 || i >= hdpa->Count)
        return nullptr;
    void *p = hdpa->Ptrs[i];
    memmove(hdpa->Ptrs + i, hdpa->Ptrs + i + 1, (SIZE_T)(hdpa->Count - i - 1) * sizeof(void *));
    hdpa->Count--;
    return p;
}

WINCOMMCTRLAPI BOOL WINAPI DPA_DeleteAllPtrs(HDPA hdpa)
{
    if (!hdpa)
        return FALSE;
    hdpa->Count = 0;
    return TRUE;
}

WINCOMMCTRLAPI void WINAPI DPA_EnumCallback(HDPA hdpa, PFNDAENUMCALLBACK callback, void *data)
{
    if (!hdpa || !callback)
        return;
    for (int i = 0; i < hdpa->Count; i++)
        if (!callback(hdpa->Ptrs[i], data))
            break;
}

WINCOMMCTRLAPI void WINAPI DPA_DestroyCallback(HDPA hdpa, PFNDAENUMCALLBACK callback, void *data)
{
    DPA_EnumCallback(hdpa, callback, data);
    DPA_Destroy(hdpa);
}

/* a stable merge sort, so equal items keep their order. */
static void merge_sort(void **items, void **tmp, int n, PFNDACOMPARE compare, LPARAM param)
{
    if (n < 2)
        return;
    int half = n / 2;
    merge_sort(items, tmp, half, compare, param);
    merge_sort(items + half, tmp, n - half, compare, param);
    int a = 0, b = half, k = 0;
    while (a < half && b < n)
        tmp[k++] = compare(items[a], items[b], param) <= 0 ? items[a++] : items[b++];
    while (a < half)
        tmp[k++] = items[a++];
    while (b < n)
        tmp[k++] = items[b++];
    memcpy(items, tmp, (SIZE_T)n * sizeof(void *));
}

WINCOMMCTRLAPI BOOL WINAPI DPA_Sort(HDPA hdpa, PFNDACOMPARE compare, LPARAM param)
{
    if (!hdpa || !compare)
        return FALSE;
    if (hdpa->Count < 2)
        return TRUE;
    void **tmp = CcAlloc((SIZE_T)hdpa->Count * sizeof(void *));
    if (!tmp)
        return FALSE;
    merge_sort(hdpa->Ptrs, tmp, hdpa->Count, compare, param);
    CcFree(tmp);
    return TRUE;
}

WINCOMMCTRLAPI int WINAPI DPA_Search(HDPA hdpa, void *find, int start, PFNDACOMPARE compare, LPARAM param,
                                     UINT options)
{
    if (!hdpa || !compare)
        return -1;
    if (!(options & DPAS_SORTED)) {
        for (int i = start < 0 ? 0 : start; i < hdpa->Count; i++)
            if (!compare(find, hdpa->Ptrs[i], param))
                return i;
        return -1;
    }
    int lo = 0, hi = hdpa->Count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = compare(find, hdpa->Ptrs[mid], param);
        if (!c)
            return mid;
        if (c < 0)
            hi = mid - 1;
        else
            lo = mid + 1;
    }
    if (options & DPAS_INSERTBEFORE)
        return lo;
    if (options & DPAS_INSERTAFTER)
        return lo;
    return -1;
}

WINCOMMCTRLAPI ULONGLONG WINAPI DPA_GetSize(HDPA hdpa)
{
    return hdpa ? sizeof(*hdpa) + (ULONGLONG)hdpa->Capacity * sizeof(void *) : 0;
}

/* ------------------------------------------------------------------ DSA */

WINCOMMCTRLAPI HDSA WINAPI DSA_Create(int item_size, int grow)
{
    if (item_size <= 0)
        return nullptr;
    HDSA hdsa = CcAlloc(sizeof(*hdsa));
    if (!hdsa)
        return nullptr;
    hdsa->ItemSize = item_size;
    hdsa->Grow = grow < 1 ? 1 : grow;
    return hdsa;
}

WINCOMMCTRLAPI BOOL WINAPI DSA_Destroy(HDSA hdsa)
{
    if (!hdsa)
        return TRUE;
    CcFree(hdsa->Data);
    CcFree(hdsa);
    return TRUE;
}

static BOOL dsa_grow(HDSA hdsa, int count)
{
    if (count <= hdsa->Capacity)
        return TRUE;
    int capacity = (count + hdsa->Grow - 1) / hdsa->Grow * hdsa->Grow;
    BYTE *data = CcReAlloc(hdsa->Data, (SIZE_T)capacity * (SIZE_T)hdsa->ItemSize);
    if (!data)
        return FALSE;
    hdsa->Data = data;
    hdsa->Capacity = capacity;
    return TRUE;
}

WINCOMMCTRLAPI void *WINAPI DSA_GetItemPtr(HDSA hdsa, int i)
{
    if (!hdsa || i < 0 || i >= hdsa->Count)
        return nullptr;
    return hdsa->Data + (SIZE_T)i * (SIZE_T)hdsa->ItemSize;
}

WINCOMMCTRLAPI BOOL WINAPI DSA_GetItem(HDSA hdsa, int i, void *item)
{
    void *p = DSA_GetItemPtr(hdsa, i);
    if (!p || !item)
        return FALSE;
    memcpy(item, p, (SIZE_T)hdsa->ItemSize);
    return TRUE;
}

WINCOMMCTRLAPI int WINAPI DSA_InsertItem(HDSA hdsa, int i, const void *item)
{
    if (!hdsa || i < 0 || !item)
        return -1;
    if (i > hdsa->Count)
        i = hdsa->Count;
    if (!dsa_grow(hdsa, hdsa->Count + 1))
        return -1;
    BYTE *at = hdsa->Data + (SIZE_T)i * (SIZE_T)hdsa->ItemSize;
    memmove(at + hdsa->ItemSize, at, (SIZE_T)(hdsa->Count - i) * (SIZE_T)hdsa->ItemSize);
    memcpy(at, item, (SIZE_T)hdsa->ItemSize);
    hdsa->Count++;
    return i;
}

WINCOMMCTRLAPI BOOL WINAPI DSA_SetItem(HDSA hdsa, int i, const void *item)
{
    if (!hdsa || i < 0 || !item)
        return FALSE;
    if (i >= hdsa->Count) {
        if (!dsa_grow(hdsa, i + 1))
            return FALSE;
        hdsa->Count = i + 1;
    }
    memcpy(hdsa->Data + (SIZE_T)i * (SIZE_T)hdsa->ItemSize, item, (SIZE_T)hdsa->ItemSize);
    return TRUE;
}

WINCOMMCTRLAPI BOOL WINAPI DSA_DeleteItem(HDSA hdsa, int i)
{
    if (!hdsa || i < 0 || i >= hdsa->Count)
        return FALSE;
    BYTE *at = hdsa->Data + (SIZE_T)i * (SIZE_T)hdsa->ItemSize;
    memmove(at, at + hdsa->ItemSize, (SIZE_T)(hdsa->Count - i - 1) * (SIZE_T)hdsa->ItemSize);
    hdsa->Count--;
    return TRUE;
}

WINCOMMCTRLAPI BOOL WINAPI DSA_DeleteAllItems(HDSA hdsa)
{
    if (!hdsa)
        return FALSE;
    hdsa->Count = 0;
    return TRUE;
}

WINCOMMCTRLAPI HDSA WINAPI DSA_Clone(HDSA hdsa)
{
    if (!hdsa)
        return nullptr;
    HDSA copy = DSA_Create(hdsa->ItemSize, hdsa->Grow);
    if (!copy || !dsa_grow(copy, hdsa->Count)) {
        DSA_Destroy(copy);
        return nullptr;
    }
    if (hdsa->Count)
        memcpy(copy->Data, hdsa->Data, (SIZE_T)hdsa->Count * (SIZE_T)hdsa->ItemSize);
    copy->Count = hdsa->Count;
    return copy;
}

WINCOMMCTRLAPI void WINAPI DSA_EnumCallback(HDSA hdsa, PFNDAENUMCALLBACK callback, void *data)
{
    if (!hdsa || !callback)
        return;
    for (int i = 0; i < hdsa->Count; i++)
        if (!callback(hdsa->Data + (SIZE_T)i * (SIZE_T)hdsa->ItemSize, data))
            break;
}

WINCOMMCTRLAPI void WINAPI DSA_DestroyCallback(HDSA hdsa, PFNDAENUMCALLBACK callback, void *data)
{
    DSA_EnumCallback(hdsa, callback, data);
    DSA_Destroy(hdsa);
}

WINCOMMCTRLAPI BOOL WINAPI DSA_Sort(HDSA hdsa, PFNDACOMPARE compare, LPARAM param)
{
    if (!hdsa || !compare)
        return FALSE;
    if (hdsa->Count < 2)
        return TRUE;
    void **ptrs = CcAlloc((SIZE_T)hdsa->Count * 2 * sizeof(void *));
    BYTE *sorted = CcAlloc((SIZE_T)hdsa->Count * (SIZE_T)hdsa->ItemSize);
    if (!ptrs || !sorted) {
        CcFree(ptrs);
        CcFree(sorted);
        return FALSE;
    }
    for (int i = 0; i < hdsa->Count; i++)
        ptrs[i] = hdsa->Data + (SIZE_T)i * (SIZE_T)hdsa->ItemSize;
    merge_sort(ptrs, ptrs + hdsa->Count, hdsa->Count, compare, param);
    for (int i = 0; i < hdsa->Count; i++)
        memcpy(sorted + (SIZE_T)i * (SIZE_T)hdsa->ItemSize, ptrs[i], (SIZE_T)hdsa->ItemSize);
    memcpy(hdsa->Data, sorted, (SIZE_T)hdsa->Count * (SIZE_T)hdsa->ItemSize);
    CcFree(ptrs);
    CcFree(sorted);
    return TRUE;
}

WINCOMMCTRLAPI ULONGLONG WINAPI DSA_GetSize(HDSA hdsa)
{
    return hdsa ? sizeof(*hdsa) + (ULONGLONG)hdsa->Capacity * (ULONGLONG)hdsa->ItemSize : 0;
}

/* ------------------------------------------------------------------ string slots */

WINCOMMCTRLAPI BOOL WINAPI Str_SetPtrW(LPWSTR *slot, LPCWSTR text)
{
    if (!slot)
        return FALSE;
    LPWSTR copy = nullptr;
    if (text && !(copy = CcStrDupW(text)))
        return FALSE;
    CcFree(*slot);
    *slot = copy;
    return TRUE;
}

/* ordinal 234. */
WINCOMMCTRLAPI BOOL WINAPI Str_SetPtrA(LPSTR *slot, LPCSTR text)
{
    if (!slot)
        return FALSE;
    LPSTR copy = nullptr;
    if (text) {
        SIZE_T n = strlen(text) + 1;
        if (!(copy = CcAlloc(n)))
            return FALSE;
        memcpy(copy, text, n);
    }
    CcFree(*slot);
    *slot = copy;
    return TRUE;
}

/* ordinal 235: copies into the buffer, returns the characters copied. */
WINCOMMCTRLAPI int WINAPI Str_GetPtrW(LPCWSTR text, LPWSTR buffer, int max)
{
    if (!buffer || max <= 0)
        return text ? (int)wcslen(text) + 1 : 1;
    return CcCopyText(buffer, max, text);
}

/* ordinal 233. */
WINCOMMCTRLAPI int WINAPI Str_GetPtrA(LPCSTR text, LPSTR buffer, int max)
{
    int len = text ? (int)strlen(text) : 0;
    if (!buffer || max <= 0)
        return len + 1;
    int n = len < max - 1 ? len : max - 1;
    if (n > 0)
        memcpy(buffer, text, (SIZE_T)n);
    buffer[n] = 0;
    return n;
}
