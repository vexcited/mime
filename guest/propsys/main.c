/* propsys.dll's entry point and its smaller exports. */
#include "propsys_private.h"

/* the compiler references this symbol from code that uses floating point. */
int _fltused = 0x9875;

#define CLASS_E_CLASSNOTAVAILABLE ((HRESULT)0x80040111L)

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **object)
{
    if (object)
        *object = nullptr;
    return CLASS_E_CLASSNOTAVAILABLE;
}

void WINAPI ClearPropVariantArray(PROPVARIANT *values, UINT count)
{
    for (UINT i = 0; values && i < count; i++)
        PropVariantClear(&values[i]);
}

void WINAPI ClearVariantArray(VARIANT *values, UINT count)
{
    for (UINT i = 0; values && i < count; i++)
        VariantClear(&values[i]);
}

/* reads one value by loading the storage into a memory store. */
static HRESULT from_storage(PCUSERIALIZEDPROPSTORAGE storage, DWORD size, REFPROPERTYKEY key, LPCWSTR name,
                            PROPVARIANT *value)
{
    memset(value, 0, sizeof(*value));
    IPersistSerializedPropStorage *persist;
    HRESULT hr = PSCreateMemoryPropertyStore(&IID_IPersistSerializedPropStorage, (void **)&persist);
    if (FAILED(hr))
        return hr;
    hr = persist->lpVtbl->SetPropertyStorage(persist, storage, size);
    if (SUCCEEDED(hr) && key) {
        IPropertyStore *store;
        hr = persist->lpVtbl->QueryInterface(persist, &IID_IPropertyStore, (void **)&store);
        if (SUCCEEDED(hr)) {
            hr = store->lpVtbl->GetValue(store, key, value);
            store->lpVtbl->Release(store);
        }
    } else if (SUCCEEDED(hr)) {
        INamedPropertyStore *named;
        hr = persist->lpVtbl->QueryInterface(persist, &IID_INamedPropertyStore, (void **)&named);
        if (SUCCEEDED(hr)) {
            hr = named->lpVtbl->GetNamedValue(named, name, value);
            named->lpVtbl->Release(named);
        }
    }
    persist->lpVtbl->Release(persist);
    return hr;
}

HRESULT WINAPI PSGetPropertyFromPropertyStorage(PCUSERIALIZEDPROPSTORAGE storage, DWORD size, REFPROPERTYKEY key,
                                                PROPVARIANT *value)
{
    if (!key)
        return E_INVALIDARG;
    return from_storage(storage, size, key, nullptr, value);
}

HRESULT WINAPI PSGetNamedPropertyFromPropertyStorage(PCUSERIALIZEDPROPSTORAGE storage, DWORD size, LPCWSTR name,
                                                     PROPVARIANT *value)
{
    if (!name)
        return E_INVALIDARG;
    return from_storage(storage, size, nullptr, name, value);
}

/* without the schema's descriptions a value displays as its string form. */
HRESULT WINAPI PSFormatForDisplayAlloc(REFPROPERTYKEY key, const PROPVARIANT *value, DWORD flags, LPWSTR *text)
{
    *text = nullptr;
    return PropVariantToStringAlloc(value, text);
}

HRESULT WINAPI PSFormatForDisplay(REFPROPERTYKEY key, const PROPVARIANT *value, DWORD flags, LPWSTR buffer,
                                  DWORD length)
{
    if (buffer && length)
        *buffer = 0;
    LPWSTR text;
    HRESULT hr = PSFormatForDisplayAlloc(key, value, flags, &text);
    if (SUCCEEDED(hr)) {
        hr = PsCopyString(buffer, length, text);
        CoTaskMemFree(text);
    }
    return hr;
}

/* there is no property schema, so no descriptions. */
HRESULT WINAPI PSGetPropertyDescription(REFPROPERTYKEY key, REFIID riid, void **object)
{
    if (object)
        *object = nullptr;
    return TYPE_E_ELEMENTNOTFOUND;
}

HRESULT WINAPI PSGetPropertyDescriptionByName(LPCWSTR name, REFIID riid, void **object)
{
    if (object)
        *object = nullptr;
    return TYPE_E_ELEMENTNOTFOUND;
}
