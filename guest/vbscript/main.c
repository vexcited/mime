/*
 * vbscript.dll: the VBScript active scripting engine and the RegExp object, as in-process servers.
 */
#include "vbs.h"

/* the compiler references this symbol from code that uses floating point. */
int _fltused = 0x9875;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_vbs_module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

MIME_DEFINE_GUID(CLSID_VBScriptEngine, 0xB54F3741, 0x5B07, 0x11CF, 0xA4, 0xB0, 0x00, 0xAA, 0x00, 0x4A, 0x55, 0xE8);
MIME_DEFINE_GUID(CLSID_VBScriptEncode, 0xB54F3743, 0x5B07, 0x11CF, 0xA4, 0xB0, 0x00, 0xAA, 0x00, 0x4A, 0x55, 0xE8);
MIME_DEFINE_GUID(CLSID_VBScriptRegExp, 0x3F4DACA4, 0x160D, 0x11D2, 0xA8, 0xE9, 0x00, 0x10, 0x4B, 0x36, 0x5C, 0x9F);

typedef struct FACTORY {
    IClassFactory Factory;
    int Kind;
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
    if (((FACTORY *)This)->Kind == 0) {
        Engine *e = engine_create();
        if (!e)
            return E_OUTOFMEMORY;
        HRESULT hr = e->Script.lpVtbl->QueryInterface(&e->Script, riid, object);
        e->Script.lpVtbl->Release(&e->Script);
        return hr;
    }
    IDispatch *d = regexp_create(nullptr);
    if (!d)
        return E_OUTOFMEMORY;
    HRESULT hr = d->lpVtbl->QueryInterface(d, riid, object);
    d->lpVtbl->Release(d);
    return hr;
}

static HRESULT STDMETHODCALLTYPE factory_lock(IClassFactory *This, BOOL lock)
{
    return S_OK;
}

static const IClassFactoryVtbl g_factory_vtbl = {factory_qi, factory_addref, factory_release, factory_create, factory_lock};
static FACTORY g_engine_factory = {{&g_factory_vtbl}, 0};
static FACTORY g_regexp_factory = {{&g_factory_vtbl}, 1};

DECLSPEC_EXPORT HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    FACTORY *factory;
    if (IsEqualCLSID(clsid, &CLSID_VBScriptEngine) || IsEqualCLSID(clsid, &CLSID_VBScriptEncode))
        factory = &g_engine_factory;
    else if (IsEqualCLSID(clsid, &CLSID_VBScriptRegExp))
        factory = &g_regexp_factory;
    else
        return CLASS_E_CLASSNOTAVAILABLE;
    return factory->Factory.lpVtbl->QueryInterface(&factory->Factory, riid, object);
}

DECLSPEC_EXPORT HRESULT WINAPI DllCanUnloadNow(void)
{
    return g_vbs_objects ? S_FALSE : S_OK;
}

/* ------------------------------------------------------------------ registration */

static const struct {
    PCWSTR Clsid, Name, ProgId, Version, Description;
    BOOL Script;
} g_classes[] = {
    {L"{B54F3741-5B07-11cf-A4B0-00AA004A55E8}", L"VB Script Language", L"VBScript", L"VBScript.1", L"VB Script Language", TRUE},
    {L"{B54F3743-5B07-11cf-A4B0-00AA004A55E8}", L"VBScript Language Encoding", L"VBScript.Encode", L"VBScript.Encode.1",
     L"VBScript Language Encoding", TRUE},
    {L"{3F4DACA4-160D-11D2-A8E9-00104B365C9F}", L"Regular Expression Object", L"VBScript.RegExp", L"VBScript.RegExp.1",
     L"Regular Expression Object", FALSE},
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

DECLSPEC_EXPORT HRESULT WINAPI DllRegisterServer(void)
{
    WCHAR module[MAX_PATH], path[400];
    GetModuleFileNameW(g_vbs_module, module, MAX_PATH);
    LONG e = 0;
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
        static const PCWSTR categories_script[] = {L"{F0B7A1A1-9847-11CF-8F20-00805F2CD064}",
                                                   L"{F0B7A1A2-9847-11CF-8F20-00805F2CD064}",
                                                   L"{7DD95801-9882-11CF-9FA9-00AA006C42C4}",
                                                   L"{7DD95802-9882-11CF-9FA9-00AA006C42C4}"};
        static const PCWSTR categories_object[] = {L"{7DD95801-9882-11CF-9FA9-00AA006C42C4}",
                                                   L"{7DD95802-9882-11CF-9FA9-00AA006C42C4}"};
        const PCWSTR *categories = g_classes[i].Script ? categories_script : categories_object;
        UINT count = g_classes[i].Script ? 4 : 2;
        for (UINT k = 0; k < count; k++) {
            WCHAR sub[400];
            make_path(sub, 400, L"Software\\Classes\\CLSID\\%s\\Implemented Categories\\%s", g_classes[i].Clsid, categories[k]);
            HKEY key;
            if (!RegCreateKeyExW(HKEY_LOCAL_MACHINE, sub, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr))
                RegCloseKey(key);
        }
        if (g_classes[i].Script) {
            make_path(path, 400, L"Software\\Classes\\CLSID\\%s\\OLEScript%s", g_classes[i].Clsid, nullptr);
            HKEY key;
            if (!RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr))
                RegCloseKey(key);
        }
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
