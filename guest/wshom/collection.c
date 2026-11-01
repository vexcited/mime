/*
 * the collections of the object model: the generic collection of strings, the environment variables of the process or of the
 * registry, and the enumerator over the items of a collection.
 */
#include "wshom_private.h"

/* ------------------------------------------------------------------ the enumerator over a safe array */

typedef struct ENUM_ARRAY {
    IEnumVARIANT Enum;
    LONG Refs;
    SAFEARRAY *Array;
    LONG Position, Lower, Upper;
} ENUM_ARRAY;

static const IEnumVARIANTVtbl g_enum_vtbl;

static ENUM_ARRAY *enum_of(IEnumVARIANT *This)
{
    return (ENUM_ARRAY *)This;
}

static HRESULT STDMETHODCALLTYPE enum_query_interface(IEnumVARIANT *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IEnumVARIANT)) {
        *object = This;
        InterlockedIncrement(&enum_of(This)->Refs);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE enum_add_ref(IEnumVARIANT *This)
{
    return (ULONG)InterlockedIncrement(&enum_of(This)->Refs);
}

static ULONG STDMETHODCALLTYPE enum_release(IEnumVARIANT *This)
{
    ENUM_ARRAY *e = enum_of(This);
    LONG refs = InterlockedDecrement(&e->Refs);
    if (!refs) {
        SafeArrayDestroy(e->Array);
        HeapFree(GetProcessHeap(), 0, e);
        WshUnlockDll();
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE enum_next(IEnumVARIANT *This, ULONG count, VARIANT *values, ULONG *fetched)
{
    ENUM_ARRAY *e = enum_of(This);
    for (ULONG i = 0; i < count; i++)
        VariantInit(&values[i]);
    ULONG got = 0;
    while (got < count && e->Position <= e->Upper) {
        HRESULT hr = SafeArrayGetElement(e->Array, &e->Position, &values[got]);
        if (FAILED(hr)) {
            for (ULONG i = 0; i < count; i++)
                VariantClear(&values[i]);
            return hr;
        }
        e->Position++;
        got++;
    }
    if (fetched)
        *fetched = got;
    return got != count ? S_FALSE : S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_skip(IEnumVARIANT *This, ULONG count)
{
    ENUM_ARRAY *e = enum_of(This);
    while (count && e->Position <= e->Upper) {
        e->Position++;
        count--;
    }
    return count ? S_FALSE : S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_reset(IEnumVARIANT *This)
{
    ENUM_ARRAY *e = enum_of(This);
    e->Position = e->Lower;
    return S_OK;
}

static HRESULT enum_create(SAFEARRAY *array, IEnumVARIANT **out)
{
    ENUM_ARRAY *e = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*e));
    if (!e)
        return E_OUTOFMEMORY;
    e->Enum.lpVtbl = &g_enum_vtbl;
    e->Refs = 1;
    HRESULT hr = SafeArrayGetLBound(array, 1, &e->Lower);
    if (SUCCEEDED(hr))
        hr = SafeArrayGetUBound(array, 1, &e->Upper);
    if (SUCCEEDED(hr)) {
        e->Position = e->Lower;
        hr = SafeArrayCopy(array, &e->Array);
    }
    if (FAILED(hr)) {
        HeapFree(GetProcessHeap(), 0, e);
        return hr;
    }
    WshLockDll();
    *out = &e->Enum;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_clone(IEnumVARIANT *This, IEnumVARIANT **out)
{
    if (!out)
        return E_POINTER;
    ENUM_ARRAY *e = enum_of(This);
    HRESULT hr = enum_create(e->Array, out);
    if (SUCCEEDED(hr))
        enum_of(*out)->Position = e->Position;
    return hr;
}

static const IEnumVARIANTVtbl g_enum_vtbl = {
    enum_query_interface, enum_add_ref, enum_release, enum_next, enum_skip, enum_reset, enum_clone,
};

/* the enumerator of an array as the IUnknown that _NewEnum gives. */
static HRESULT new_enum(SAFEARRAY *array, IUnknown **out)
{
    IEnumVARIANT *e;
    HRESULT hr = enum_create(array, &e);
    if (SUCCEEDED(hr))
        *out = (IUnknown *)e;
    return hr;
}

/* ------------------------------------------------------------------ the collection */

static const WSH_CLASS g_collection_class = {&IID_IWshCollection, &CLSID_WshCollection, {&IID_IWshCollection, nullptr}};

typedef struct WSH_COLLECTION {
    WSH_OBJECT Object;
    SAFEARRAY *Items, *Names;
} WSH_COLLECTION;

WSH_COMMON_METHODS(collection, IWshCollection)

static void collection_destroy(WSH_OBJECT *object)
{
    WSH_COLLECTION *collection = (WSH_COLLECTION *)object;
    if (collection->Items)
        SafeArrayDestroy(collection->Items);
    if (collection->Names)
        SafeArrayDestroy(collection->Names);
}

static HRESULT STDMETHODCALLTYPE collection_Count(IWshCollection *This, LONG *count)
{
    if (!count)
        return E_POINTER;
    *count = 0;
    LONG upper = 0;
    HRESULT hr = SafeArrayGetUBound(((WSH_COLLECTION *)This)->Items, 1, &upper);
    if (SUCCEEDED(hr))
        *count = upper + 1;
    return hr;
}

static HRESULT STDMETHODCALLTYPE collection_get_length(IWshCollection *This, LONG *length)
{
    if (!length)
        return E_POINTER;
    return collection_Count(This, length);
}

static HRESULT STDMETHODCALLTYPE collection_Item(IWshCollection *This, VARIANT *index, VARIANT *value)
{
    if (!index || !value)
        return E_POINTER;
    WSH_COLLECTION *collection = (WSH_COLLECTION *)This;
    VariantInit(value);
    LONG position;
    if (collection->Names && index->vt == VT_BSTR) {
        LONG lower, upper;
        HRESULT hr = SafeArrayGetLBound(collection->Names, 1, &lower);
        if (SUCCEEDED(hr))
            hr = SafeArrayGetUBound(collection->Names, 1, &upper);
        if (FAILED(hr))
            return hr;
        LONG i;
        for (i = lower; i <= upper; i++) {
            VARIANT name;
            VariantInit(&name);
            hr = SafeArrayGetElement(collection->Names, &i, &name);
            if (FAILED(hr))
                return hr;
            BOOL match = !_wcsicmp(index->bstrVal ? index->bstrVal : L"", name.bstrVal ? name.bstrVal : L"");
            VariantClear(&name);
            if (match)
                break;
        }
        if (i > upper) {
            /* a name that is not there gives an empty string. */
            value->vt = VT_BSTR;
            value->bstrVal = SysAllocString(L"");
            return value->bstrVal ? S_OK : E_OUTOFMEMORY;
        }
        position = i - lower;
    } else {
        position = 0;
        WshVariantLong(index, &position);
    }
    HRESULT hr = SafeArrayGetElement(collection->Items, &position, value);
    if (FAILED(hr))
        return hr;
    if (value->vt != VT_BSTR) {
        VariantClear(value);
        return E_UNEXPECTED;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE collection_NewEnum(IWshCollection *This, IUnknown **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    return new_enum(((WSH_COLLECTION *)This)->Items, out);
}

static const IWshCollectionVtbl g_collection_vtbl = {
    collection_QueryInterface, collection_AddRef, collection_Release, collection_GetTypeInfoCount, collection_GetTypeInfo,
    collection_GetIDsOfNames, collection_Invoke, collection_Item, collection_Count, collection_get_length,
    collection_NewEnum,
};

HRESULT WshCollectionCreate(SAFEARRAY *items, SAFEARRAY *names, IWshCollection **out)
{
    if (!items)
        return E_POINTER;
    WSH_COLLECTION *collection = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*collection));
    if (!collection)
        return E_OUTOFMEMORY;
    WshObjectInit(&collection->Object, &g_collection_vtbl, &g_collection_class, collection_destroy);
    HRESULT hr = SafeArrayCopy(items, &collection->Items);
    if (SUCCEEDED(hr) && names)
        hr = SafeArrayCopy(names, &collection->Names);
    if (FAILED(hr)) {
        WshRelease(collection);
        return hr;
    }
    *out = (IWshCollection *)collection;
    return S_OK;
}

/* ------------------------------------------------------------------ environment variables */

static const WSH_CLASS g_environment_class = {&IID_IWshEnvironment, &CLSID_WshEnvironment, {&IID_IWshEnvironment, nullptr}};

typedef struct WSH_ENVIRONMENT {
    WSH_OBJECT Object;
    /* the key of the variables, nothing for those of the process. */
    HKEY Key;
} WSH_ENVIRONMENT;

WSH_COMMON_METHODS(environment, IWshEnvironment)

static void environment_destroy(WSH_OBJECT *object)
{
    WSH_ENVIRONMENT *environment = (WSH_ENVIRONMENT *)object;
    if (environment->Key)
        RegCloseKey(environment->Key);
}

static HRESULT win32_hr(LONG status)
{
    return status > 0 ? (HRESULT)(0x80070000u | (status & 0xFFFF)) : (HRESULT)status;
}

static void broadcast_change(void)
{
    DWORD_PTR result = 0;
    SendMessageTimeoutA(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM) "Environment", 0, 1000, &result);
}

static HRESULT STDMETHODCALLTYPE environment_get_Item(IWshEnvironment *This, BSTR name, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    WSH_ENVIRONMENT *environment = (WSH_ENVIRONMENT *)This;
    const WCHAR *key_name = name ? name : L"";
    if (!environment->Key) {
        WCHAR small[0x100];
        WCHAR *big = nullptr, *text = small;
        DWORD size = GetEnvironmentVariableW(key_name, small, 0x100);
        if (size >= 0x100) {
            big = HeapAlloc(GetProcessHeap(), 0, size * sizeof(WCHAR));
            if (!big)
                return E_OUTOFMEMORY;
            DWORD got = GetEnvironmentVariableW(key_name, big, size);
            if (got >= size) {
                HeapFree(GetProcessHeap(), 0, big);
                return E_OUTOFMEMORY;
            }
            size = got;
            text = big;
        }
        text[size] = 0;
        *out = SysAllocString(text);
        if (big)
            HeapFree(GetProcessHeap(), 0, big);
        return *out ? S_OK : E_OUTOFMEMORY;
    }
    DWORD longest = 0;
    LONG status = RegQueryInfoKeyW(environment->Key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                   nullptr, &longest, nullptr, nullptr);
    if (status)
        return win32_hr(status);
    DWORD capacity = longest + 2;
    BYTE *data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, capacity + 4);
    if (!data)
        return E_OUTOFMEMORY;
    DWORD type = 0;
    status = RegQueryValueExW(environment->Key, key_name, nullptr, &type, data, &capacity);
    HRESULT hr;
    if (status == ERROR_FILE_NOT_FOUND) {
        *(WCHAR *)data = 0;
        type = REG_SZ;
        hr = S_OK;
    } else {
        hr = win32_hr(status);
    }
    if (SUCCEEDED(hr)) {
        if (type == REG_SZ || type == REG_EXPAND_SZ) {
            *out = SysAllocString((const WCHAR *)data);
            hr = *out ? S_OK : E_OUTOFMEMORY;
        } else {
            hr = E_FAIL;
        }
    }
    HeapFree(GetProcessHeap(), 0, data);
    return hr;
}

static HRESULT STDMETHODCALLTYPE environment_put_Item(IWshEnvironment *This, BSTR name, BSTR value)
{
    WSH_ENVIRONMENT *environment = (WSH_ENVIRONMENT *)This;
    const WCHAR *key_name = name ? name : L"";
    if (!environment->Key)
        return SetEnvironmentVariableW(key_name, value) ? S_OK : HrFromLastError();
    const WCHAR *text = value ? value : L"";
    LONG status = RegSetValueExW(environment->Key, key_name, 0, REG_EXPAND_SZ, (const BYTE *)text,
                                 (DWORD)(wcslen(text) + 1) * sizeof(WCHAR));
    HRESULT hr = win32_hr(status);
    if (SUCCEEDED(hr))
        broadcast_change();
    return hr;
}

static HRESULT STDMETHODCALLTYPE environment_Count(IWshEnvironment *This, LONG *count)
{
    if (!count)
        return E_POINTER;
    *count = 0;
    WSH_ENVIRONMENT *environment = (WSH_ENVIRONMENT *)This;
    if (environment->Key) {
        DWORD values = 0;
        LONG status = RegQueryInfoKeyW(environment->Key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &values,
                                       nullptr, nullptr, nullptr, nullptr);
        if (status)
            return win32_hr(status);
        *count = (LONG)values;
        return S_OK;
    }
    WCHAR *strings = GetEnvironmentStringsW();
    if (!strings)
        return E_FAIL;
    LONG n = 0;
    for (const WCHAR *p = strings; *p; p += wcslen(p) + 1)
        n++;
    FreeEnvironmentStringsW(strings);
    *count = n;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE environment_get_length(IWshEnvironment *This, LONG *length)
{
    return environment_Count(This, length);
}

/* the variable at an index of the key, as the text name=value. */
static HRESULT registry_string(HKEY key, DWORD index, BSTR *out)
{
    DWORD name_max = 0, data_max = 0;
    LONG status = RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &name_max, &data_max,
                                   nullptr, nullptr);
    if (status)
        return win32_hr(status);
    if (name_max > 0x400 || data_max > 0x2000)
        return E_OUTOFMEMORY;
    SIZE_T units = (SIZE_T)data_max / 2 + name_max + 4;
    WCHAR *buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, units * sizeof(WCHAR));
    if (!buffer)
        return E_OUTOFMEMORY;
    DWORD name_length = name_max + 1;
    HRESULT hr = win32_hr(RegEnumValueW(key, index, buffer, &name_length, nullptr, nullptr, nullptr, nullptr));
    if (SUCCEEDED(hr)) {
        DWORD type = 0, size = data_max;
        name_length = name_max + 1;
        hr = win32_hr(RegEnumValueW(key, index, buffer, &name_length, nullptr, &type, (BYTE *)&buffer[name_length + 1],
                                    &size));
        if (SUCCEEDED(hr)) {
            buffer[name_length] = '=';
            if (type != REG_SZ && type != REG_EXPAND_SZ)
                buffer[name_length + 1] = 0;
            *out = SysAllocString(buffer);
            hr = *out ? S_OK : E_OUTOFMEMORY;
        }
    }
    HeapFree(GetProcessHeap(), 0, buffer);
    return hr;
}

static HRESULT STDMETHODCALLTYPE environment_NewEnum(IWshEnvironment *This, IUnknown **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    WSH_ENVIRONMENT *environment = (WSH_ENVIRONMENT *)This;
    LONG count = 0;
    HRESULT hr = environment_Count(This, &count);
    if (FAILED(hr))
        return hr;
    SAFEARRAY *array = SafeArrayCreateVector(VT_VARIANT, 0, (ULONG)count);
    if (!array)
        return E_OUTOFMEMORY;
    WCHAR *strings = environment->Key ? nullptr : GetEnvironmentStringsW();
    const WCHAR *p = strings;
    LONG filled = 0;
    for (LONG i = 0; i < count; i++) {
        VARIANT item;
        VariantInit(&item);
        item.vt = VT_BSTR;
        if (environment->Key) {
            hr = registry_string(environment->Key, (DWORD)i, &item.bstrVal);
            if (hr == HRESULT_FROM_WIN32(ERROR_NO_MORE_ITEMS))
                break;
        } else {
            item.bstrVal = SysAllocString(p);
            hr = item.bstrVal ? S_OK : E_OUTOFMEMORY;
            p += wcslen(p) + 1;
        }
        if (FAILED(hr))
            goto done;
        hr = SafeArrayPutElement(array, &i, &item);
        VariantClear(&item);
        if (FAILED(hr))
            goto done;
        filled++;
    }
    if (filled != count) {
        SAFEARRAYBOUND bound = {(ULONG)filled, 0};
        SafeArrayRedim(array, &bound);
    }
    hr = new_enum(array, out);
done:
    if (strings)
        FreeEnvironmentStringsW(strings);
    SafeArrayDestroy(array);
    return hr;
}

static HRESULT STDMETHODCALLTYPE environment_Remove(IWshEnvironment *This, BSTR name)
{
    WSH_ENVIRONMENT *environment = (WSH_ENVIRONMENT *)This;
    const WCHAR *key_name = name ? name : L"";
    HRESULT hr;
    if (!environment->Key)
        hr = SetEnvironmentVariableW(key_name, nullptr) ? S_OK : HrFromLastError();
    else
        hr = win32_hr(RegDeleteValueW(environment->Key, key_name));
    if (FAILED(hr)) {
        WshSignal(&IID_IWshEnvironment, L"WshEnvironment.Remove", MSG_ENV_REMOVE, key_name);
        return hr;
    }
    if (environment->Key)
        broadcast_change();
    return S_OK;
}

static const IWshEnvironmentVtbl g_environment_vtbl = {
    environment_QueryInterface, environment_AddRef, environment_Release, environment_GetTypeInfoCount,
    environment_GetTypeInfo, environment_GetIDsOfNames, environment_Invoke, environment_get_Item, environment_put_Item,
    environment_Count, environment_get_length, environment_NewEnum, environment_Remove,
};

HRESULT WshEnvironmentCreate(int type, IWshEnvironment **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    HKEY key = nullptr;
    if (type != 3) {
        HKEY root = type == 0 ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
        const WCHAR *path = type == 0   ? L"System\\CurrentControlSet\\Control\\Session Manager\\Environment"
                            : type == 1 ? L"Environment"
                                        : L"Volatile Environment";
        LONG status = RegCreateKeyExW(root, path, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, nullptr);
        if (status)
            status = RegCreateKeyExW(root, path, 0, nullptr, 0, KEY_READ, nullptr, &key, nullptr);
        if (status)
            return win32_hr(status);
    }
    WSH_ENVIRONMENT *environment = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*environment));
    if (!environment) {
        if (key)
            RegCloseKey(key);
        return E_OUTOFMEMORY;
    }
    WshObjectInit(&environment->Object, &g_environment_vtbl, &g_environment_class, environment_destroy);
    environment->Key = key;
    *out = (IWshEnvironment *)environment;
    return S_OK;
}
