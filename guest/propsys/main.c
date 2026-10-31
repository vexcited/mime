/* propsys.dll's entry point, the classes it serves and its smaller exports. */
#include "propsys_private.h"

#define CLASS_E_CLASSNOTAVAILABLE ((HRESULT)0x80040111L)
#define CLASS_E_NOAGGREGATION ((HRESULT)0x80040110L)

/* the compiler references this symbol from code that uses floating point. */
int _fltused = 0x9875;

HINSTANCE g_module;

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}

/* ------------------------------------------------------------------ the classes */

typedef HRESULT (*CREATE)(REFIID riid, void **object);

static HRESULT create_property_system(REFIID riid, void **object)
{
    return PSGetPropertySystem(riid, object);
}
static HRESULT create_memory_store(REFIID riid, void **object)
{
    return PSCreateMemoryPropertyStore(riid, object);
}
static HRESULT create_window_store(REFIID riid, void **object)
{
    return WindowStoreCreate(nullptr, riid, object);
}
HRESULT MultiplexCreate(REFIID riid, void **object);

MIME_DEFINE_GUID(CLSID_WindowPropertyStore, 0x56ad4c5d, 0xb908, 0x4f85, 0x8f, 0xf1, 0x79, 0x40, 0xc2, 0x9b, 0x3b, 0xcf);
MIME_DEFINE_GUID(CLSID_MultiplexPropertyStore, 0x8d14113b, 0x5715, 0x4714, 0x8a, 0x20, 0x69, 0xfc, 0x34, 0x36, 0x26, 0xde);

static const struct {
    const GUID *clsid;
    CREATE create;
} k_classes[] = {
    {&CLSID_PropertySystem, create_property_system},
    {&CLSID_InMemoryPropertyStore, create_memory_store},
    {&CLSID_InMemoryPropertyStoreMarshalByValue, create_memory_store},
    {&CLSID_MultiplexPropertyStore, MultiplexCreate},
    {&CLSID_WindowPropertyStore, create_window_store},
};

typedef struct {
    IClassFactory factory;
    CREATE create;
} FACTORY;

static HRESULT STDMETHODCALLTYPE f_query_interface(IClassFactory *This, REFIID riid, void **object)
{
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IClassFactory))
        return E_NOINTERFACE;
    *object = This;
    return S_OK;
}
static ULONG STDMETHODCALLTYPE f_add_ref(IClassFactory *This)
{
    return 2;
}
static ULONG STDMETHODCALLTYPE f_release(IClassFactory *This)
{
    return 1;
}
static HRESULT STDMETHODCALLTYPE f_create_instance(IClassFactory *This, IUnknown *outer, REFIID riid, void **object)
{
    *object = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    return ((FACTORY *)This)->create(riid, object);
}
static HRESULT STDMETHODCALLTYPE f_lock_server(IClassFactory *This, BOOL lock)
{
    return S_OK;
}

static const IClassFactoryVtbl factory_vtbl = {f_query_interface, f_add_ref, f_release, f_create_instance,
                                               f_lock_server};
static FACTORY g_factories[sizeof(k_classes) / sizeof(k_classes[0])];

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    for (UINT i = 0; i < sizeof(k_classes) / sizeof(k_classes[0]); i++)
        if (IsEqualGUID(clsid, k_classes[i].clsid)) {
            g_factories[i].factory.lpVtbl = &factory_vtbl;
            g_factories[i].create = k_classes[i].create;
            return f_query_interface(&g_factories[i].factory, riid, object);
        }
    return CLASS_E_CLASSNOTAVAILABLE;
}

/* ------------------------------------------------------------------ the proxies */

/* the interfaces whose proxies propsys registers, with the number of methods each has. */
static const struct {
    GUID iid;
    UINT methods;
} k_interfaces[] = {
    {{0x71604b0f, 0x97b0, 0x4764, {0x85, 0x77, 0x2f, 0x13, 0xe9, 0x8a, 0x14, 0x22}}, 7},
    {{0x9b6e051c, 0x5ddd, 0x4321, {0x90, 0x70, 0xfe, 0x2a, 0xcb, 0x55, 0xe7, 0x94}}, 9},
    {{0x75121952, 0xe0d0, 0x43e5, {0x93, 0x80, 0x1d, 0x80, 0x48, 0x3a, 0xcf, 0x72}}, 4},
    {{0x6f79d558, 0x3e96, 0x4549, {0xa1, 0xd1, 0x7d, 0x75, 0xd2, 0x28, 0x88, 0x14}}, 24},
    {{0xb7d14566, 0x0509, 0x4cce, {0xa7, 0x1f, 0x0a, 0x55, 0x42, 0x33, 0xbd, 0x9b}}, 4},
    {{0xc8e2d566, 0x186e, 0x4d49, {0xbf, 0x41, 0x69, 0x09, 0xea, 0xd5, 0x6a, 0xcc}}, 4},
    {{0x3017056d, 0x9a91, 0x4e90, {0x93, 0x7d, 0x74, 0x6c, 0x72, 0xab, 0xbf, 0x4f}}, 12},
    {{0xbc110b6d, 0x57e8, 0x4148, {0xa9, 0xc6, 0x91, 0x01, 0x5a, 0xb2, 0xf3, 0xa5}}, 5},
    {{0x40d4577f, 0xe237, 0x4bdb, {0xbd, 0x69, 0x58, 0xf0, 0x89, 0x43, 0x1b, 0x6a}}, 6},
    {{0xca724e8a, 0xc3e6, 0x442b, {0x88, 0xa4, 0x6f, 0xb0, 0xdb, 0x80, 0x35, 0xa3}}, 12},
    {{0xf917bc8a, 0x1bba, 0x4478, {0xa2, 0x45, 0x1b, 0xde, 0x03, 0xeb, 0x94, 0x31}}, 6},
    {{0xb824b49d, 0x22ac, 0x4161, {0xac, 0x8a, 0x99, 0x16, 0xe8, 0xfa, 0x3f, 0x7f}}, 4},
    {{0xfc0ca0a7, 0xc316, 0x4fd2, {0x90, 0x31, 0x3e, 0x62, 0x8e, 0x6d, 0x4f, 0x23}}, 5},
    {{0x380f5cad, 0x1b5e, 0x42f2, {0x80, 0x5d, 0x63, 0x7f, 0xd3, 0x92, 0xd3, 0x1e}}, 10},
    {{0x078f91bd, 0x29a2, 0x440f, {0x92, 0x4e, 0x46, 0xa2, 0x91, 0x52, 0x45, 0x20}}, 28},
    {{0x1f9fc1d0, 0xc39b, 0x4b26, {0x81, 0x7f, 0x01, 0x19, 0x67, 0xd3, 0x44, 0x0e}}, 5},
    {{0xfa955fd9, 0x38be, 0x4879, {0xa6, 0xce, 0x82, 0x4c, 0xf5, 0x2d, 0x60, 0x9f}}, 4},
    {{0x886d8eeb, 0x8cf2, 0x4446, {0x8d, 0x02, 0xcd, 0xba, 0x1d, 0xbd, 0xcf, 0x99}}, 8},
    {{0x57d2eded, 0x5062, 0x400e, {0xb1, 0x07, 0x5d, 0xae, 0x79, 0xfe, 0x57, 0xa6}}, 25},
    {{0xa99400f4, 0x3d84, 0x4557, {0x94, 0xba, 0x12, 0x42, 0xfb, 0x2c, 0xc9, 0xa6}}, 7},
    {{0x507393f4, 0x2a3d, 0x4a60, {0xb5, 0x9e, 0xd9, 0xc7, 0x57, 0x16, 0xc2, 0xdd}}, 25},
    {{0x11e1fbf9, 0x2d56, 0x4a6b, {0x8d, 0xb3, 0x7c, 0xd1, 0x93, 0xa4, 0x71, 0xf2}}, 8},
    {{0xf67104fc, 0x2af9, 0x46fd, {0xb3, 0x2d, 0x24, 0x3c, 0x14, 0x04, 0xf3, 0xd1}}, 26},
};

/* the class of the proxies and stubs of these interfaces. */
static const CLSID k_proxy_clsid = {0xf562a2c8, 0xe850, 0x4f05, {0x8e, 0x7a, 0xe7, 0x19, 0x2e, 0x4e, 0x6c, 0x23}};
static const void *k_proxy_files[1];

const CLSID *WINAPI GetProxyDllInfo(const void ***files, const CLSID **clsid)
{
    *files = k_proxy_files;
    *clsid = &k_proxy_clsid;
    return &k_proxy_clsid;
}

static void guid_text(const GUID *guid, WCHAR *text)
{
    StringFromGUID2(guid, text, 40);
}

static HRESULT set_default(LPCWSTR path, LPCWSTR name, LPCWSTR value)
{
    HKEY key;
    LSTATUS status = RegCreateKeyExW(HKEY_CLASSES_ROOT, path, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
    if (status)
        return REGDB_E_WRITEREGDB;
    status = RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)value, (DWORD)((wcslen(value) + 1) * sizeof(WCHAR)));
    RegCloseKey(key);
    return status ? REGDB_E_WRITEREGDB : S_OK;
}

/* the registration a proxy library makes: its class, and each interface with the class and the methods. */
HRESULT WINAPI DllRegisterServer(void)
{
    WCHAR clsid[40], module[MAX_PATH], path[MAX_PATH + 64], text[40];
    guid_text(&k_proxy_clsid, clsid);
    if (!GetModuleFileNameW(g_module, module, MAX_PATH))
        return HRESULT_FROM_WIN32(GetLastError());
    wcscpy(path, L"CLSID\\");
    wcscat(path, clsid);
    HRESULT hr = set_default(path, nullptr, L"PSFactoryBuffer");
    wcscat(path, L"\\InprocServer32");
    if (SUCCEEDED(hr))
        hr = set_default(path, nullptr, module);
    if (SUCCEEDED(hr))
        hr = set_default(path, L"ThreadingModel", L"Both");
    for (UINT i = 0; i < sizeof(k_interfaces) / sizeof(k_interfaces[0]) && SUCCEEDED(hr); i++) {
        WCHAR count[12], base[MAX_PATH + 64], digits[12];
        guid_text(&k_interfaces[i].iid, text);
        wcscpy(base, L"Interface\\");
        wcscat(base, text);
        hr = set_default(base, nullptr, L"");
        UINT n = k_interfaces[i].methods, len = 0;
        do {
            digits[len++] = (WCHAR)('0' + n % 10);
            n /= 10;
        } while (n);
        for (UINT k = 0; k < len; k++)
            count[k] = digits[len - 1 - k];
        count[len] = 0;
        wcscpy(path, base);
        wcscat(path, L"\\ProxyStubClsid32");
        if (SUCCEEDED(hr))
            hr = set_default(path, nullptr, clsid);
        wcscpy(path, base);
        wcscat(path, L"\\NumMethods");
        if (SUCCEEDED(hr))
            hr = set_default(path, nullptr, count);
    }
    return hr;
}

HRESULT WINAPI DllUnregisterServer(void)
{
    WCHAR path[MAX_PATH + 64], text[40];
    for (UINT i = 0; i < sizeof(k_interfaces) / sizeof(k_interfaces[0]); i++) {
        guid_text(&k_interfaces[i].iid, text);
        wcscpy(path, L"Interface\\");
        wcscat(path, text);
        RegDeleteTreeW(HKEY_CLASSES_ROOT, path);
    }
    guid_text(&k_proxy_clsid, text);
    wcscpy(path, L"CLSID\\");
    wcscat(path, text);
    RegDeleteTreeW(HKEY_CLASSES_ROOT, path);
    return S_OK;
}

/* ------------------------------------------------------------------ small exports */

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
