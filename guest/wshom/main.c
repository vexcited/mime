/*
 * wshom.ocx: the object model of the Windows Script Host, WScript.Shell and WScript.Network.
 */
#include "wshom_private.h"

int _fltused = 0x9875;

HINSTANCE g_module;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

/* ------------------------------------------------------------------ class factories */

typedef struct FACTORY {
    IClassFactory Factory;
    BOOL Shell;
} FACTORY;

static HRESULT STDMETHODCALLTYPE factory_query_interface(IClassFactory *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IClassFactory))
        return E_NOINTERFACE;
    *object = This;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE factory_add_ref(IClassFactory *This)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE factory_release(IClassFactory *This)
{
    return 1;
}

static HRESULT STDMETHODCALLTYPE factory_create_instance(IClassFactory *This, IUnknown *outer, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    return ((FACTORY *)This)->Shell ? WshShellCreate(riid, object) : WshNetworkCreate(riid, object);
}

static HRESULT STDMETHODCALLTYPE factory_lock_server(IClassFactory *This, BOOL lock)
{
    if (lock)
        WshLockDll();
    else
        WshUnlockDll();
    return S_OK;
}

static const IClassFactoryVtbl g_factory_vtbl = {
    factory_query_interface, factory_add_ref, factory_release, factory_create_instance, factory_lock_server,
};
static FACTORY g_shell_factory = {{&g_factory_vtbl}, TRUE};
static FACTORY g_network_factory = {{&g_factory_vtbl}, FALSE};

DECLSPEC_EXPORT HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    FACTORY *factory;
    if (IsEqualCLSID(clsid, &CLSID_WshShell) || IsEqualCLSID(clsid, &CLSID_WshShellLegacy))
        factory = &g_shell_factory;
    else if (IsEqualCLSID(clsid, &CLSID_WshNetwork) || IsEqualCLSID(clsid, &CLSID_WshNetworkLegacy))
        factory = &g_network_factory;
    else
        return CLASS_E_CLASSNOTAVAILABLE;
    return factory->Factory.lpVtbl->QueryInterface(&factory->Factory, riid, object);
}

DECLSPEC_EXPORT HRESULT WINAPI DllCanUnloadNow(void)
{
    return WshLockCount() ? S_FALSE : S_OK;
}

/* ------------------------------------------------------------------ registration */

static void reg_value(const WCHAR *key, const WCHAR *name, const WCHAR *value)
{
    HKEY handle;
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, key, 0, nullptr, 0, KEY_WRITE, nullptr, &handle, nullptr))
        return;
    if (value)
        RegSetValueExW(handle, name, 0, REG_SZ, (const BYTE *)value, (DWORD)((wcslen(value) + 1) * sizeof(WCHAR)));
    RegCloseKey(handle);
}

static void reg_delete_tree(HKEY root, const WCHAR *key)
{
    HKEY handle;
    if (RegOpenKeyExW(root, key, 0, KEY_ALL_ACCESS, &handle))
        return;
    WCHAR name[260];
    for (;;) {
        DWORD size = 260;
        if (RegEnumKeyExW(handle, 0, name, &size, nullptr, nullptr, nullptr, nullptr))
            break;
        reg_delete_tree(handle, name);
    }
    RegCloseKey(handle);
    RegDeleteKeyW(root, key);
}

static const struct {
    const GUID *Clsid;
    const WCHAR *Name, *Prog;
} g_classes[] = {
    {&CLSID_WshShell, L"Windows Script Host Shell Object", L"WScript.Shell"},
    {&CLSID_WshShellLegacy, L"Windows Script Host Shell Object", L"WScript.Shell"},
    {&CLSID_WshNetwork, L"Windows Script Host Network Object", L"WScript.Network"},
    {&CLSID_WshNetworkLegacy, L"Windows Script Host Network Object", L"WScript.Network"},
};

DECLSPEC_EXPORT HRESULT WINAPI DllRegisterServer(void)
{
    WCHAR path[MAX_PATH];
    if (!GetModuleFileNameW(g_module, path, MAX_PATH))
        wcscpy(path, L"wshom.ocx");
    ITypeLib *library;
    HRESULT hr = LoadTypeLibEx(path, REGKIND_REGISTER, &library);
    if (FAILED(hr))
        return hr;
    library->lpVtbl->Release(library);
    WCHAR lib_text[40], clsid[40], key[200], version_independent[64], versioned[64];
    StringFromGUID2(&LIBID_IWshRuntimeLibrary, lib_text, 40);
    for (int i = 0; i < (int)(sizeof(g_classes) / sizeof(g_classes[0])); i++) {
        StringFromGUID2(g_classes[i].Clsid, clsid, 40);
        _snwprintf(versioned, 64, L"%s.1", g_classes[i].Prog);
        wcscpy(version_independent, g_classes[i].Prog);
        _snwprintf(key, 200, L"CLSID\\%s", clsid);
        reg_value(key, nullptr, g_classes[i].Name);
        _snwprintf(key, 200, L"CLSID\\%s\\InProcServer32", clsid);
        reg_value(key, nullptr, path);
        reg_value(key, L"ThreadingModel", L"Apartment");
        _snwprintf(key, 200, L"CLSID\\%s\\Implemented Categories\\{40FC6ED5-2438-11CF-A3DB-080036F12502}", clsid);
        reg_value(key, nullptr, nullptr);
        _snwprintf(key, 200, L"CLSID\\%s\\Programmable", clsid);
        reg_value(key, nullptr, nullptr);
        _snwprintf(key, 200, L"CLSID\\%s\\TypeLib", clsid);
        reg_value(key, nullptr, lib_text);
        _snwprintf(key, 200, L"CLSID\\%s\\ProgID", clsid);
        reg_value(key, nullptr, versioned);
        _snwprintf(key, 200, L"CLSID\\%s\\VersionIndependentProgID", clsid);
        reg_value(key, nullptr, version_independent);
        if (i == 0 || i == 2) {
            reg_value(version_independent, nullptr, g_classes[i].Name);
            _snwprintf(key, 200, L"%s\\CLSID", version_independent);
            reg_value(key, nullptr, clsid);
            _snwprintf(key, 200, L"%s\\CurVer", version_independent);
            reg_value(key, nullptr, versioned);
            reg_value(versioned, nullptr, g_classes[i].Name);
            _snwprintf(key, 200, L"%s\\CLSID", versioned);
            reg_value(key, nullptr, clsid);
        }
    }
    return S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI DllUnregisterServer(void)
{
    WCHAR clsid[40], key[100];
    for (int i = 0; i < (int)(sizeof(g_classes) / sizeof(g_classes[0])); i++) {
        StringFromGUID2(g_classes[i].Clsid, clsid, 40);
        _snwprintf(key, 100, L"CLSID\\%s", clsid);
        reg_delete_tree(HKEY_CLASSES_ROOT, key);
    }
    reg_delete_tree(HKEY_CLASSES_ROOT, L"WScript.Shell");
    reg_delete_tree(HKEY_CLASSES_ROOT, L"WScript.Shell.1");
    reg_delete_tree(HKEY_CLASSES_ROOT, L"WScript.Network");
    reg_delete_tree(HKEY_CLASSES_ROOT, L"WScript.Network.1");
    UnRegisterTypeLib(&LIBID_IWshRuntimeLibrary, 1, 0, 0, sizeof(void *) == 8 ? SYS_WIN64 : SYS_WIN32);
    return S_OK;
}
