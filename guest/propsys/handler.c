/* property handlers of items: the store a shell item makes, and the handler a file name selects. */
#include "propsys_private.h"

#define HANDLER_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\PropertySystem\\PropertyHandlers\\"
#define SYSTEM_HANDLERS L"Software\\Microsoft\\Windows\\CurrentVersion\\PropertySystem\\SystemPropertyHandlers"

MIME_DEFINE_GUID(IID_IPsShellItem2, 0x7e9fb0d3, 0x919f, 0x4307, 0xab, 0x2e, 0x9b, 0x18, 0x60, 0x31, 0x0c, 0x93);

/* the methods of IShellItem2 after the five of IShellItem and the three of IUnknown. */
typedef HRESULT(STDMETHODCALLTYPE *GET_STORE)(void *This, DWORD flags, REFIID riid, void **object);
typedef HRESULT(STDMETHODCALLTYPE *GET_STORE_WITH_OBJECT)(void *This, DWORD flags, IUnknown *create, REFIID riid,
                                                          void **object);

HRESULT WINAPI PSGetItemPropertyHandlerWithCreateObject(IUnknown *item, BOOL readwrite, IUnknown *create, REFIID riid,
                                                        void **object)
{
    if (!item || !object)
        return E_POINTER;
    *object = nullptr;
    IUnknown *item2;
    HRESULT hr = item->lpVtbl->QueryInterface(item, &IID_IPsShellItem2, (void **)&item2);
    if (FAILED(hr))
        return hr;
    void **vtable = *(void ***)item2;
    DWORD flags = readwrite ? GPS_HANDLERPROPERTIESONLY | GPS_READWRITE : GPS_HANDLERPROPERTIESONLY;
    if (create)
        hr = ((GET_STORE_WITH_OBJECT)vtable[9])(item2, flags, create, riid, object);
    else
        hr = ((GET_STORE)vtable[8])(item2, flags, riid, object);
    item2->lpVtbl->Release(item2);
    return hr;
}

HRESULT WINAPI PSGetItemPropertyHandler(IUnknown *item, BOOL readwrite, REFIID riid, void **object)
{
    return PSGetItemPropertyHandlerWithCreateObject(item, readwrite, nullptr, riid, object);
}

static HRESULT clsid_from_value(HKEY root, LPCWSTR key, LPCWSTR value, CLSID *clsid)
{
    WCHAR text[40];
    DWORD bytes = sizeof(text);
    LSTATUS status = RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, text, &bytes);
    if (status)
        return HRESULT_FROM_WIN32(status);
    return CLSIDFromString(text, clsid);
}

/* the handler registered for the extension of a path, a system one first. */
HRESULT WINAPI PSLookupPropertyHandlerCLSID(LPCWSTR path, CLSID *clsid)
{
    if (!path || !clsid)
        return E_POINTER;
    *clsid = GUID_NULL;
    LPCWSTR extension = PathFindExtensionW(path);
    if (!*extension)
        return E_POINTER;
    HRESULT hr = clsid_from_value(HKEY_LOCAL_MACHINE, SYSTEM_HANDLERS, extension, clsid);
    if (FAILED(hr)) {
        WCHAR key[MAX_PATH];
        if (wcslen(extension) + wcslen(HANDLER_KEY) >= MAX_PATH)
            return STRSAFE_E_INSUFFICIENT_BUFFER;
        wcscpy(key, HANDLER_KEY);
        wcscat(key, extension);
        hr = clsid_from_value(HKEY_LOCAL_MACHINE, key, nullptr, clsid);
    }
    return hr;
}
