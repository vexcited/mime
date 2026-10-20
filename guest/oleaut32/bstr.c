/*
 * BSTR strings. a BSTR points to the characters; the DWORD before them
 * holds the length in bytes without the terminating null. on 64 bit
 * windows the block starts 8 bytes before the characters, the length
 * taking the upper half, so the string stays 8 byte aligned.
 */
#include "oleaut32_private.h"

#define BSTR_HEADER 8
#define VARCMP_LT 0
#define VARCMP_EQ 1
#define VARCMP_GT 2
/* lengths above this cannot be represented with the header and terminator. */
#define BSTR_MAX_BYTES 0x7FFFFFF0u

static BSTR allocate(UINT bytes)
{
    if (bytes > BSTR_MAX_BYTES)
        return nullptr;
    BYTE *block = AutAlloc(BSTR_HEADER + bytes + 1 + sizeof(WCHAR));
    if (!block)
        return nullptr;
    *(DWORD *)(block + 4) = bytes;
    BYTE *text = block + BSTR_HEADER;
    /* the terminator is the character after the last whole one, so an odd byte count is followed by a zero byte and a zero character. */
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
 * strings that something took a reference to with SysAddRefString: the references beyond the first, kept by address. A
 * free of one of them only takes a reference away, and the string goes with the last. Most programs never take one, so
 * the table is only looked in when it is not empty.
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
        AutFree((BYTE *)bstr - BSTR_HEADER);
}

/* a string with a reference taken lives until it has been freed as many times as it has references. */
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

/* strings were not cached by this system to begin with, so there is nothing to turn off. */
WINOLEAUTAPI void WINAPI SetOaNoCache(void)
{
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
 * the new string may point into the old one, so it is copied before the old one is freed. Without a source, or with the old
 * string as the source, the characters of the old one stay, as far as the new one reaches.
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

/* byte lengths are joined, so an odd byte of either string is kept. */
WINOLEAUTAPI HRESULT WINAPI VarBstrCat(BSTR left, BSTR right, BSTR *result)
{
    if (!result)
        return E_INVALIDARG;
    UINT a = left ? SysStringByteLen(left) : 0, b = right ? SysStringByteLen(right) : 0;
    if (~b < a)
        return DISP_E_OVERFLOW;
    BSTR joined = SysAllocStringByteLen(nullptr, a + b);
    *result = joined;
    if (!joined)
        return E_OUTOFMEMORY;
    memcpy(joined, left, a);
    memcpy((BYTE *)joined + a, right, b);
    return S_OK;
}

/*
 * locale 0 compares the bytes; any other locale compares characters with
 * CompareStringW, locale 1 meaning the user's. an empty side compares by
 * length alone.
 */
WINOLEAUTAPI HRESULT WINAPI VarBstrCmp(BSTR left, BSTR right, LCID lcid, ULONG flags)
{
    UINT a = left ? SysStringByteLen(left) : 0, b = right ? SysStringByteLen(right) : 0;
    UINT shortest = a < b ? a : b;
    if (!lcid) {
        if (shortest) {
            int c = memcmp(left, right, shortest);
            if (c)
                return c < 0 ? VARCMP_LT : VARCMP_GT;
        }
        return a < b ? VARCMP_LT : a == b ? VARCMP_EQ : VARCMP_GT;
    }
    a /= sizeof(WCHAR);
    b /= sizeof(WCHAR);
    if (!shortest)
        return a < b ? VARCMP_LT : a == b ? VARCMP_EQ : VARCMP_GT;
    int c = CompareStringW(lcid == 1 ? LOCALE_USER_DEFAULT : lcid, flags, left, (int)a, right, (int)b);
    return c ? c - 1 : E_INVALIDARG;
}
