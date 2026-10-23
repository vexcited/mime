/*
 * automation active objects: the object an application offers to scripts as the running one for its class. like on
 * windows they are running object table entries named by an item moniker of "!" and the braced class id, so scripts in
 * other processes find them through the machine wide table.
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
