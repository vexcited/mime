/*
 * BSTR strings. a BSTR points at the characters, and the DWORD before them holds the byte length without the
 * terminator. on 64-bit windows the block starts 8 bytes before the characters with the length in its upper half, so
 * the string stays 8 byte aligned.
 */
#include "oleaut32_private.h"
#include <mime/win32/winnls.h>

#define BSTR_HEADER 8
/* lengths above this cannot be represented with the header and terminator. */
#define BSTR_MAX_BYTES 0x7FFFFFF0u

/*
 * like windows, each thread keeps the blocks of freed strings in four buckets of six, by size, and hands them out again.
 * a freed string is left as it was, and programs that read one after freeing it work because of that. OANOCACHE, or
 * SetOaNoCache, turns the cache off, and OACACHEPARAMS sets the bucket limits and their length as windows reads it.
 */
#define CACHE_BUCKETS 4
#define CACHE_SLOTS 6

typedef struct CACHE_SLOT {
    DWORD Size;
    BYTE *Block;
} CACHE_SLOT;

typedef struct CACHE {
    CACHE_SLOT Slots[CACHE_BUCKETS][CACHE_SLOTS];
} CACHE;

static DWORD g_bucket_limit[CACHE_BUCKETS] = {32, 64, 256, 32768};
static USHORT g_slots = CACHE_SLOTS;
static volatile LONG g_no_cache = -1;
static DWORD g_cache_index = FLS_OUT_OF_INDEXES;
static INIT_ONCE g_cache_once = INIT_ONCE_STATIC_INIT;

static void WINAPI cache_release(PVOID data)
{
    CACHE *cache = data;
    if (!cache)
        return;
    for (ULONG b = 0; b < CACHE_BUCKETS; b++)
        for (ULONG i = 0; i < CACHE_SLOTS; i++)
            AutFree(cache->Slots[b][i].Block);
    AutFree(cache);
}

static BOOL CALLBACK cache_setup(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    char text[64];
    if (GetEnvironmentVariableA("OANOCACHE", text, sizeof(text)))
        InterlockedCompareExchange(&g_no_cache, 1, -1);
    DWORD n = GetEnvironmentVariableA("OACACHEPARAMS", text, sizeof(text));
    if (n && n < sizeof(text)) {
        /* "largest,next,next,smallest:slots", where 0 keeps the default. */
        DWORD values[CACHE_BUCKETS] = {};
        ULONG count = 0, slots = 0;
        const char *p = text;
        while (*p && count < CACHE_BUCKETS) {
            DWORD v = 0;
            while (*p >= '0' && *p <= '9')
                v = v * 10 + (DWORD)(*p++ - '0');
            values[count++] = v;
            if (*p != ',')
                break;
            p++;
        }
        if (*p == ':')
            for (p++; *p >= '0' && *p <= '9'; p++)
                slots = slots * 10 + (ULONG)(*p - '0');
        for (ULONG i = 0; i < count; i++)
            if (values[i])
                g_bucket_limit[CACHE_BUCKETS - 1 - i] = values[i];
        g_slots = (USHORT)(slots >= 1 && slots <= CACHE_SLOTS ? slots : CACHE_SLOTS);
    }
    InterlockedCompareExchange(&g_no_cache, 0, -1);
    g_cache_index = FlsAlloc(cache_release);
    return TRUE;
}

static CACHE *thread_cache(BOOL create)
{
    InitOnceExecuteOnce(&g_cache_once, cache_setup, nullptr, nullptr);
    if (g_no_cache > 0 || g_cache_index == FLS_OUT_OF_INDEXES)
        return nullptr;
    CACHE *cache = FlsGetValue(g_cache_index);
    if (!cache && create) {
        cache = AutAllocZero(sizeof(*cache));
        if (cache && !FlsSetValue(g_cache_index, cache)) {
            AutFree(cache);
            cache = nullptr;
        }
    }
    return cache;
}

static LONG bucket_of(DWORD size)
{
    for (ULONG b = 0; b < CACHE_BUCKETS; b++)
        if (size <= g_bucket_limit[b])
            return (LONG)b;
    return -1;
}

/* the block sizes windows asks for: the string and its header rounded up to 16 bytes. */
static DWORD block_size(UINT bytes)
{
    DWORD size = (bytes + 25) & ~15u;
    return size < bytes + BSTR_HEADER + 3 ? size + 16 : size;
}

/* the block starts at the header, so a string is a heap block that CoTaskMemFree may free, as on windows. */
static BYTE *cached_alloc(DWORD size)
{
    CACHE *cache = thread_cache(TRUE);
    LONG b = cache ? bucket_of(size) : -1;
    if (b >= 0)
        for (ULONG i = 0; i < g_slots; i++) {
            CACHE_SLOT *slot = &cache->Slots[b][i];
            if (slot->Size >= size) {
                slot->Size = 0;
                return slot->Block;
            }
        }
    return AutAlloc(size);
}

/* a block goes in an empty slot, or in place of a smaller one, which is freed instead. its size is the heap's. */
static void cached_free(BYTE *block)
{
    CACHE *cache = thread_cache(FALSE);
    SIZE_T heap_size = cache ? HeapSize(GetProcessHeap(), 0, block) : (SIZE_T)-1;
    DWORD size = heap_size > 0xFFFFFFFF ? 0xFFFFFFFF : (DWORD)heap_size;
    LONG b = cache ? bucket_of(size) : -1;
    if (b >= 0) {
        CACHE_SLOT *smallest = nullptr;
        for (ULONG i = 0; i < g_slots; i++) {
            CACHE_SLOT *slot = &cache->Slots[b][i];
            if (!slot->Size) {
                slot->Size = size;
                slot->Block = block;
                return;
            }
            if (slot->Block == block)
                return;
            if (!smallest || slot->Size < smallest->Size)
                smallest = slot;
        }
        if (smallest && smallest->Size < size) {
            BYTE *evicted = smallest->Block;
            smallest->Block = block;
            smallest->Size = size;
            block = evicted;
        }
    }
    AutFree(block);
}

static BSTR allocate(UINT bytes)
{
    if (bytes > BSTR_MAX_BYTES)
        return nullptr;
    BYTE *block = cached_alloc(block_size(bytes));
    if (!block)
        return nullptr;
    *(DWORD *)block = 0;
    *(DWORD *)(block + 4) = bytes;
    BYTE *text = block + BSTR_HEADER;
    /* the terminator follows the last whole character, so an odd byte count gets a zero byte then a zero character. */
    text[bytes] = 0;
    text[(bytes + 1) & ~1u] = 0;
    text[((bytes + 1) & ~1u) + 1] = 0;
    return (BSTR)text;
}

WINOLEAUTAPI BSTR WINAPI SysAllocStringByteLen(LPCSTR string, UINT length)
{
    BSTR bstr = allocate(length);
    if (!bstr)
        return nullptr;
    if (string)
        memcpy(bstr, string, length);
    else
        memset(bstr, 0, length);
    return bstr;
}

WINOLEAUTAPI BSTR WINAPI SysAllocStringLen(const OLECHAR *string, UINT length)
{
    if (length > BSTR_MAX_BYTES / sizeof(WCHAR))
        return nullptr;
    return SysAllocStringByteLen((LPCSTR)string, length * sizeof(WCHAR));
}

WINOLEAUTAPI BSTR WINAPI SysAllocString(const OLECHAR *string)
{
    if (!string)
        return nullptr;
    return SysAllocStringLen(string, (UINT)wcslen(string));
}

/*
 * references taken with SysAddRefString beyond the first, by address. freeing such a string only drops a reference,
 * and the string goes with the last one. most programs never take one, so the table is only searched when not empty.
 */
static SRWLOCK g_ref_lock = SRWLOCK_INIT;
static struct {
    BSTR String;
    LONG Extra;
} *g_refs;
static ULONG g_ref_count, g_ref_capacity;
static volatile LONG g_ref_used;

static LONG ref_find(BSTR bstr)
{
    for (ULONG i = 0; i < g_ref_count; i++)
        if (g_refs[i].String == bstr)
            return (LONG)i;
    return -1;
}

/* true when a reference was dropped and the string lives on. */
static BOOL drop_reference(BSTR bstr)
{
    BOOL kept = FALSE;
    if (!g_ref_used)
        return FALSE;
    AcquireSRWLockExclusive(&g_ref_lock);
    LONG i = ref_find(bstr);
    if (i >= 0) {
        kept = TRUE;
        if (--g_refs[i].Extra <= 0) {
            g_refs[i] = g_refs[--g_ref_count];
            g_ref_used = g_ref_count != 0;
        }
    }
    ReleaseSRWLockExclusive(&g_ref_lock);
    return kept;
}

WINOLEAUTAPI void WINAPI SysFreeString(BSTR bstr)
{
    if (bstr && !drop_reference(bstr))
        cached_free((BYTE *)bstr - BSTR_HEADER);
}

/* the string lives until it is freed once per reference. */
WINOLEAUTAPI HRESULT WINAPI SysAddRefString(BSTR bstr)
{
    if (!bstr)
        return S_OK;
    HRESULT hr = S_OK;
    AcquireSRWLockExclusive(&g_ref_lock);
    LONG i = ref_find(bstr);
    if (i < 0 && g_ref_count == g_ref_capacity) {
        ULONG capacity = g_ref_capacity ? g_ref_capacity * 2 : 16;
        void *items = AutAllocZero((SIZE_T)capacity * sizeof(g_refs[0]));
        if (items) {
            if (g_ref_count)
                memcpy(items, g_refs, g_ref_count * sizeof(g_refs[0]));
            AutFree(g_refs);
            g_refs = items;
            g_ref_capacity = capacity;
        } else {
            hr = E_OUTOFMEMORY;
        }
    }
    if (hr == S_OK) {
        if (i < 0) {
            i = (LONG)g_ref_count++;
            g_refs[i].String = bstr;
            g_refs[i].Extra = 0;
            g_ref_used = 1;
        }
        g_refs[i].Extra++;
    }
    ReleaseSRWLockExclusive(&g_ref_lock);
    return hr;
}

WINOLEAUTAPI void WINAPI SysReleaseString(BSTR bstr)
{
    SysFreeString(bstr);
}

WINOLEAUTAPI void WINAPI SetOaNoCache(void)
{
    InitOnceExecuteOnce(&g_cache_once, cache_setup, nullptr, nullptr);
    g_no_cache = 1;
}

WINOLEAUTAPI UINT WINAPI SysStringByteLen(BSTR bstr)
{
    return bstr ? *(const DWORD *)((const BYTE *)bstr - 4) : 0;
}

WINOLEAUTAPI UINT WINAPI SysStringLen(BSTR bstr)
{
    return SysStringByteLen(bstr) / sizeof(WCHAR);
}

/*
 * the new string may point into the old one, so it is copied before the old one is freed. with no source, or the old
 * string as source, the old characters stay as far as the new length reaches.
 */
WINOLEAUTAPI INT WINAPI SysReAllocStringLen(BSTR *bstr, const OLECHAR *string, UINT length)
{
    if (!bstr)
        return FALSE;
    BOOL keep = !string || string == *bstr;
    BSTR fresh = SysAllocStringLen(keep ? nullptr : string, length);
    if (!fresh)
        return FALSE;
    if (keep && *bstr) {
        UINT old = SysStringLen(*bstr);
        memcpy(fresh, *bstr, (old < length ? old : length) * sizeof(WCHAR));
    }
    SysFreeString(*bstr);
    *bstr = fresh;
    return TRUE;
}

WINOLEAUTAPI INT WINAPI SysReAllocString(BSTR *bstr, const OLECHAR *string)
{
    if (!bstr)
        return FALSE;
    if (!string) {
        SysFreeString(*bstr);
        *bstr = nullptr;
        return TRUE;
    }
    return SysReAllocStringLen(bstr, string, (UINT)wcslen(string));
}

WINOLEAUTAPI HRESULT WINAPI VarBstrCat(BSTR left, BSTR right, BSTR *result)
{
    if (!result)
        return E_INVALIDARG;
    UINT left_bytes = SysStringByteLen(left), right_bytes = SysStringByteLen(right);
    ULONGLONG total = (ULONGLONG)left_bytes + right_bytes;
    if (total > 0xFFFFFFFFull)
        return DISP_E_OVERFLOW;
    /* byte lengths are joined, so an odd byte of either string is kept. */
    BSTR joined = SysAllocStringByteLen(nullptr, (UINT)total);
    *result = joined;
    if (!joined)
        return E_OUTOFMEMORY;
    if (left_bytes)
        memcpy(joined, left, left_bytes);
    if (right_bytes)
        memcpy((BYTE *)joined + left_bytes, right, right_bytes);
    return S_OK;
}

/* locale 0 compares the 16-bit units as numbers, then a trailing odd byte. */
static int compare_units(const BYTE *a, const BYTE *b, UINT common)
{
    for (UINT i = 0; i + 1 < common; i += 2) {
        USHORT x = (USHORT)(a[i] | a[i + 1] << 8), y = (USHORT)(b[i] | b[i + 1] << 8);
        if (x != y)
            return x < y ? -1 : 1;
    }
    if (common & 1) {
        BYTE x = a[common - 1], y = b[common - 1];
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}

WINOLEAUTAPI HRESULT WINAPI VarBstrCmp(BSTR left, BSTR right, LCID lcid, ULONG flags)
{
    UINT left_bytes = SysStringByteLen(left), right_bytes = SysStringByteLen(right);
    UINT common = left_bytes < right_bytes ? left_bytes : right_bytes;
    if (!lcid) {
        int order = common ? compare_units((const BYTE *)left, (const BYTE *)right, common) : 0;
        if (!order && left_bytes != right_bytes)
            order = left_bytes < right_bytes ? -1 : 1;
        return order < 0 ? VARCMP_LT : order > 0 ? VARCMP_GT : VARCMP_EQ;
    }
    int left_chars = (int)(left_bytes / sizeof(WCHAR)), right_chars = (int)(right_bytes / sizeof(WCHAR));
    if (!common)
        return left_chars < right_chars ? VARCMP_LT : left_chars > right_chars ? VARCMP_GT : VARCMP_EQ;
    int order = CompareStringW(lcid == 1 ? LOCALE_USER_DEFAULT : lcid, flags, left, left_chars, right, right_chars);
    return order ? (HRESULT)(order - 1) : E_INVALIDARG;
}
