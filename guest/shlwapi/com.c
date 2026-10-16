/* small com helpers shared by shell clients. */
#include "shlwapi_private.h"
#include <mime/win32/objbase.h>

typedef struct { const IID *piid; int dwOffset; } QITAB;

LWSTDAPI HRESULT WINAPI QISearch(void *base, const QITAB *table, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!base || !table || !iid)
        return E_INVALIDARG;
    const QITAB *entry = table;
    if (!IsEqualIID(iid, &IID_IUnknown)) {
        while (entry->piid && !IsEqualIID(iid, entry->piid))
            entry++;
    }
    if (!entry->piid)
        return E_NOINTERFACE;
    IUnknown *object = (IUnknown *)((BYTE *)base + entry->dwOffset);
    object->lpVtbl->AddRef(object);
    *out = object;
    return S_OK;
}

LWSTDAPI HRESULT WINAPI SHStrDupW(LPCWSTR string, LPWSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!string)
        return E_INVALIDARG;
    SIZE_T bytes = (wcslen(string) + 1) * sizeof(WCHAR);
    *out = HeapAlloc(GetProcessHeap(), 0, bytes);
    if (!*out)
        return E_OUTOFMEMORY;
    memcpy(*out, string, bytes);
    return S_OK;
}
