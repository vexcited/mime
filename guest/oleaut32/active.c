/*
 * the active objects of automation: the object that an application makes available to scripts as the one running, under
 * the class it is. They are entries of the running object table, named by an item moniker of the exclamation mark and
 * the class identifier in braces, which is how the table of windows holds them, so that a script in another process of the
 * machine finds the object through the table across processes.
 */
#include "oleaut32_private.h"

#define ACTIVEOBJECT_WEAK 1
#define ROTFLAGS_REGISTRATIONKEEPSALIVE_ 1

static HRESULT active_object_moniker(REFCLSID clsid, IMoniker **moniker)
{
    WCHAR text[40];
    if (!StringFromGUID2(clsid, text, RTL_NUMBER_OF(text)))
        return E_INVALIDARG;
    return CreateItemMoniker(L"!", text, moniker);
}

WINOLEAUTAPI HRESULT WINAPI RegisterActiveObject(IUnknown *object, REFCLSID clsid, DWORD flags, DWORD *cookie)
{
    if (!object || !clsid || !cookie || (flags & ~ACTIVEOBJECT_WEAK))
        return E_INVALIDARG;
    *cookie = 0;
    IMoniker *moniker;
    HRESULT hr = active_object_moniker(clsid, &moniker);
    if (FAILED(hr))
        return hr;
    IRunningObjectTable *table;
    hr = GetRunningObjectTable(0, &table);
    if (SUCCEEDED(hr)) {
        hr = table->lpVtbl->Register(table, flags & ACTIVEOBJECT_WEAK ? 0 : ROTFLAGS_REGISTRATIONKEEPSALIVE_, object,
                                     moniker, cookie);
        table->lpVtbl->Release(table);
    }
    moniker->lpVtbl->Release(moniker);
    return hr;
}

WINOLEAUTAPI HRESULT WINAPI RevokeActiveObject(DWORD cookie, void *reserved)
{
    if (reserved)
        return E_INVALIDARG;
    IRunningObjectTable *table;
    HRESULT hr = GetRunningObjectTable(0, &table);
    if (SUCCEEDED(hr)) {
        hr = table->lpVtbl->Revoke(table, cookie);
        table->lpVtbl->Release(table);
    }
    return hr;
}

WINOLEAUTAPI HRESULT WINAPI GetActiveObject(REFCLSID clsid, void *reserved, IUnknown **object)
{
    if (!object)
        return E_INVALIDARG;
    *object = nullptr;
    if (!clsid || reserved)
        return E_INVALIDARG;
    IMoniker *moniker;
    HRESULT hr = active_object_moniker(clsid, &moniker);
    if (FAILED(hr))
        return hr;
    IRunningObjectTable *table;
    hr = GetRunningObjectTable(0, &table);
    if (SUCCEEDED(hr)) {
        hr = table->lpVtbl->GetObject(table, moniker, object);
        table->lpVtbl->Release(table);
    }
    moniker->lpVtbl->Release(moniker);
    return hr;
}
