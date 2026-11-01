/*
 * the pieces every object of scrrun.dll uses: memory, the count of objects that keeps the dll loaded, strings of automation,
 * the translation of win32 errors and the type information of the library.
 */
#include "scrrun_private.h"

void *ScrAlloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

void ScrFree(void *memory)
{
    if (memory)
        HeapFree(GetProcessHeap(), 0, memory);
}

WCHAR *ScrCopy(const WCHAR *text, SIZE_T length)
{
    WCHAR *copy = ScrAlloc((length + 1) * sizeof(WCHAR));
    if (copy && length)
        memcpy(copy, text, length * sizeof(WCHAR));
    return copy;
}

WCHAR *ScrCopyZ(const WCHAR *text)
{
    return ScrCopy(text, wcslen(text));
}

HRESULT AllocateBstr(const WCHAR *text, int length, BSTR *out)
{
    if (length < 0)
        length = (int)wcslen(text);
    *out = SysAllocStringLen(text, (UINT)length);
    return *out ? S_OK : E_OUTOFMEMORY;
}

VARIANT *ScrDeref(VARIANT *value)
{
    while (value && value->vt == (VT_VARIANT | VT_BYREF) && value->pvarVal)
        value = value->pvarVal;
    return value;
}

/* ------------------------------------------------------------------ errors */

HRESULT TranslateWin32Error(DWORD error)
{
    switch (error) {
    case 2:
    case 206:
        return E_VB_FILENOTFOUND;
    case 3:
    case 53:
    case 161:
    case 267:
        return E_VB_PATHNOTFOUND;
    case 5:
    case 19:
    case 32:
    case 50:
    case 145:
        return E_VB_PERMISSION;
    case 15:
    case 21:
        return E_VB_DISKNOTREADY;
    case 67:
    case 123:
        return E_VB_BADFILENAME;
    case 87:
    case 154:
        return E_VB_INVALID;
    case 80:
    case 183:
        return E_VB_FILEEXISTS;
    default:
        break;
    }
    if ((LONG)error > 0)
        return (HRESULT)(0x80070000u | (error & 0xFFFF));
    return (HRESULT)error;
}

/* ------------------------------------------------------------------ locks */

static LONG g_locks;
static SRWLOCK g_library_lock = SRWLOCK_INIT;
static ITypeLib *g_library;
#define INFO_SLOTS 24
static struct {
    GUID Guid;
    ITypeInfo *Info;
} g_infos[INFO_SLOTS];

LONG ScrLockCount(void)
{
    return g_locks;
}

void ScrLockDll(void)
{
    InterlockedIncrement(&g_locks);
}

void ScrUnlockDll(void)
{
    if (InterlockedDecrement(&g_locks))
        return;
    /* the last object is gone: the type information goes with it. */
    AcquireSRWLockExclusive(&g_library_lock);
    for (int i = 0; i < INFO_SLOTS; i++) {
        if (g_infos[i].Info)
            g_infos[i].Info->lpVtbl->Release(g_infos[i].Info);
        g_infos[i].Info = nullptr;
    }
    if (g_library)
        g_library->lpVtbl->Release(g_library);
    g_library = nullptr;
    ReleaseSRWLockExclusive(&g_library_lock);
}

/* ------------------------------------------------------------------ type information */

static HRESULT library_locked(void)
{
    if (g_library)
        return S_OK;
    WCHAR path[MAX_PATH];
    if (!GetModuleFileNameW(g_module, path, MAX_PATH))
        return HRESULT_FROM_WIN32(GetLastError());
    return LoadTypeLibEx(path, REGKIND_NONE, &g_library);
}

HRESULT ScrTypeInfo(REFIID iid, ITypeInfo **info)
{
    *info = nullptr;
    AcquireSRWLockExclusive(&g_library_lock);
    HRESULT hr = library_locked();
    if (SUCCEEDED(hr)) {
        int free_slot = -1;
        for (int i = 0; i < INFO_SLOTS; i++) {
            if (g_infos[i].Info && IsEqualGUID(&g_infos[i].Guid, iid)) {
                *info = g_infos[i].Info;
                break;
            }
            if (!g_infos[i].Info && free_slot < 0)
                free_slot = i;
        }
        if (*info) {
            (*info)->lpVtbl->AddRef(*info);
        } else {
            hr = g_library->lpVtbl->GetTypeInfoOfGuid(g_library, iid, info);
            if (SUCCEEDED(hr) && free_slot >= 0) {
                g_infos[free_slot].Guid = *iid;
                g_infos[free_slot].Info = *info;
                (*info)->lpVtbl->AddRef(*info);
            }
        }
    }
    ReleaseSRWLockExclusive(&g_library_lock);
    return hr;
}

HRESULT ScrClassInfo(REFCLSID clsid, ITypeInfo **info)
{
    return ScrTypeInfo(clsid, info);
}

HRESULT ScrDispatchGetIDs(REFIID iid, LPOLESTR *names, UINT count, DISPID *ids)
{
    ITypeInfo *info;
    HRESULT hr = ScrTypeInfo(iid, &info);
    if (FAILED(hr))
        return hr;
    hr = DispGetIDsOfNames(info, names, count, ids);
    info->lpVtbl->Release(info);
    return hr;
}

HRESULT ScrDispatchInvoke(void *self, REFIID iid, DISPID id, WORD flags, DISPPARAMS *params, VARIANT *result,
                          EXCEPINFO *excep, UINT *arg_error)
{
    ITypeInfo *info;
    HRESULT hr = ScrTypeInfo(iid, &info);
    if (FAILED(hr))
        return hr;
    hr = DispInvoke(self, info, id, flags, params, result, excep, arg_error);
    info->lpVtbl->Release(info);
    return hr;
}
