/*
 * jscript.dll: the JScript active scripting engine, as an in-process server.
 */
#include "js.h"

/* the compiler references this symbol from code that uses floating point. */
int _fltused = 0x9875;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_js_module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

MIME_DEFINE_GUID(CLSID_JScriptEngine, 0xF414C260, 0x6AC0, 0x11CF, 0xB6, 0xD1, 0x00, 0xAA, 0x00, 0xBB, 0xBB, 0x58);
MIME_DEFINE_GUID(CLSID_JScriptAuthor, 0xF414C261, 0x6AC0, 0x11CF, 0xB6, 0xD1, 0x00, 0xAA, 0x00, 0xBB, 0xBB, 0x58);
MIME_DEFINE_GUID(CLSID_JScriptEncode, 0xF414C262, 0x6AC0, 0x11CF, 0xB6, 0xD1, 0x00, 0xAA, 0x00, 0xBB, 0xBB, 0x58);

typedef struct FACTORY {
    IClassFactory Factory;
} FACTORY;

static HRESULT STDMETHODCALLTYPE factory_qi(IClassFactory *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IClassFactory))
        return E_NOINTERFACE;
    *object = This;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE factory_addref(IClassFactory *This)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE factory_release(IClassFactory *This)
{
    return 1;
}

static HRESULT STDMETHODCALLTYPE factory_create(IClassFactory *This, IUnknown *outer, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    Engine *e = engine_create();
    if (!e)
        return E_OUTOFMEMORY;
    HRESULT hr = e->Script.lpVtbl->QueryInterface(&e->Script, riid, object);
    e->Script.lpVtbl->Release(&e->Script);
    return hr;
}

static HRESULT STDMETHODCALLTYPE factory_lock(IClassFactory *This, BOOL lock)
{
    return S_OK;
}

static const IClassFactoryVtbl g_factory_vtbl = {factory_qi, factory_addref, factory_release, factory_create, factory_lock};
static FACTORY g_engine_factory = {{&g_factory_vtbl}};

DECLSPEC_EXPORT HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualCLSID(clsid, &CLSID_JScriptEngine) && !IsEqualCLSID(clsid, &CLSID_JScriptEncode) &&
        !IsEqualCLSID(clsid, &CLSID_JScriptAuthor))
        return CLASS_E_CLASSNOTAVAILABLE;
    return g_engine_factory.Factory.lpVtbl->QueryInterface(&g_engine_factory.Factory, riid, object);
}

DECLSPEC_EXPORT HRESULT WINAPI DllCanUnloadNow(void)
{
    return g_js_objects ? S_FALSE : S_OK;
}

/* ------------------------------------------------------------------ registration */

static const struct {
    PCWSTR Clsid, Name, ProgId, Version, Description;
} g_classes[] = {
    {L"{F414C260-6AC0-11CF-B6D1-00AA00BBBB58}", L"JScript Language", L"JScript", L"JScript.1", L"JScript Language"},
    {L"{F414C262-6AC0-11CF-B6D1-00AA00BBBB58}", L"JScript Language Encoding", L"JScript.Encode", L"JScript.Encode.1",
     L"JScript Language Encoding"},
};

static LONG set_value(HKEY root, PCWSTR path, PCWSTR name, PCWSTR value)
{
    HKEY key;
    LONG e = RegCreateKeyExW(root, path, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
    if (e)
        return e;
    e = RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)value, (DWORD)((wcslen(value) + 1) * sizeof(WCHAR)));
    RegCloseKey(key);
    return e;
}

static void make_path(WCHAR *out, SIZE_T size, PCWSTR format, PCWSTR a, PCWSTR b)
{
    _snwprintf(out, size, format, a, b ? b : L"");
}

static LONG make_key(PCWSTR path)
{
    HKEY key;
    LONG e = RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
    if (!e)
        RegCloseKey(key);
    return e;
}

DECLSPEC_EXPORT HRESULT WINAPI DllRegisterServer(void)
{
    WCHAR module[MAX_PATH], path[400];
    GetModuleFileNameW(g_js_module, module, MAX_PATH);
    LONG e = 0;
    static const PCWSTR categories[] = {L"{F0B7A1A1-9847-11CF-8F20-00805F2CD064}", L"{F0B7A1A2-9847-11CF-8F20-00805F2CD064}",
                                        L"{7DD95801-9882-11CF-9FA9-00AA006C42C4}", L"{7DD95802-9882-11CF-9FA9-00AA006C42C4}"};
    for (UINT i = 0; i < sizeof(g_classes) / sizeof(g_classes[0]); i++) {
        make_path(path, 400, L"Software\\Classes\\CLSID\\%s%s", g_classes[i].Clsid, nullptr);
        e |= set_value(HKEY_LOCAL_MACHINE, path, nullptr, g_classes[i].Name);
        make_path(path, 400, L"Software\\Classes\\CLSID\\%s\\InprocServer32%s", g_classes[i].Clsid, nullptr);
        e |= set_value(HKEY_LOCAL_MACHINE, path, nullptr, module);
        e |= set_value(HKEY_LOCAL_MACHINE, path, L"ThreadingModel", L"Both");
        make_path(path, 400, L"Software\\Classes\\CLSID\\%s\\ProgID%s", g_classes[i].Clsid, nullptr);
        e |= set_value(HKEY_LOCAL_MACHINE, path, nullptr, g_classes[i].Version);
        make_path(path, 400, L"Software\\Classes\\CLSID\\%s\\VersionIndependentProgID%s", g_classes[i].Clsid, nullptr);
        e |= set_value(HKEY_LOCAL_MACHINE, path, nullptr, g_classes[i].ProgId);
        make_path(path, 400, L"Software\\Classes\\%s%s", g_classes[i].ProgId, nullptr);
        e |= set_value(HKEY_LOCAL_MACHINE, path, nullptr, g_classes[i].Description);
        make_path(path, 400, L"Software\\Classes\\%s\\CLSID%s", g_classes[i].ProgId, nullptr);
        e |= set_value(HKEY_LOCAL_MACHINE, path, nullptr, g_classes[i].Clsid);
        make_path(path, 400, L"Software\\Classes\\%s\\CurVer%s", g_classes[i].ProgId, nullptr);
        e |= set_value(HKEY_LOCAL_MACHINE, path, nullptr, g_classes[i].Version);
        make_path(path, 400, L"Software\\Classes\\%s%s", g_classes[i].Version, nullptr);
        e |= set_value(HKEY_LOCAL_MACHINE, path, nullptr, g_classes[i].Description);
        make_path(path, 400, L"Software\\Classes\\%s\\CLSID%s", g_classes[i].Version, nullptr);
        e |= set_value(HKEY_LOCAL_MACHINE, path, nullptr, g_classes[i].Clsid);
        for (UINT k = 0; k < 4; k++) {
            make_path(path, 400, L"Software\\Classes\\CLSID\\%s\\Implemented Categories\\%s", g_classes[i].Clsid, categories[k]);
            e |= make_key(path);
        }
        make_path(path, 400, L"Software\\Classes\\CLSID\\%s\\OLEScript%s", g_classes[i].Clsid, nullptr);
        e |= make_key(path);
        make_path(path, 400, L"Software\\Classes\\%s\\OLEScript%s", g_classes[i].ProgId, nullptr);
        e |= make_key(path);
    }
    return e ? ((HRESULT)0x80040201L) : S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI DllUnregisterServer(void)
{
    WCHAR path[400];
    for (UINT i = 0; i < sizeof(g_classes) / sizeof(g_classes[0]); i++) {
        make_path(path, 400, L"Software\\Classes\\CLSID\\%s%s", g_classes[i].Clsid, nullptr);
        RegDeleteTreeW(HKEY_LOCAL_MACHINE, path);
        make_path(path, 400, L"Software\\Classes\\%s%s", g_classes[i].ProgId, nullptr);
        RegDeleteTreeW(HKEY_LOCAL_MACHINE, path);
        make_path(path, 400, L"Software\\Classes\\%s%s", g_classes[i].Version, nullptr);
        RegDeleteTreeW(HKEY_LOCAL_MACHINE, path);
    }
    return S_OK;
}
