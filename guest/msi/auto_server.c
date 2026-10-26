/*
 * what makes the scripting objects a component: the class factory of WindowsInstaller.Installer, the exports a
 * host of components calls, and the registration of the class.
 */
#include "automation.h"

/* ------------------------------------------------------------------ the class factory */

static LONG factory_references;

static HRESULT WINAPI factory_query_interface(IClassFactory *self, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IClassFactory)) {
        *out = self;
        self->lpVtbl->AddRef(self);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

/* the factory holds the dll while anyone holds the factory, as one instance. */
static ULONG WINAPI factory_add_ref(IClassFactory *self)
{
    LONG references = InterlockedIncrement(&factory_references);
    if (references == 1)
        InterlockedIncrement(&MimeMsiAutoInstances);
    return (ULONG)references;
}

static ULONG WINAPI factory_release(IClassFactory *self)
{
    LONG references = InterlockedDecrement(&factory_references);
    if (!references)
        InterlockedDecrement(&MimeMsiAutoInstances);
    return (ULONG)references;
}

static HRESULT WINAPI factory_create_instance(IClassFactory *self, IUnknown *outer, REFIID iid, void **out)
{
    *out = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    if (!IsEqualGUID(iid, &IID_IUnknown) && !IsEqualGUID(iid, &IID_AutoInstaller) && !IsEqualGUID(iid, &IID_IDispatch))
        return E_NOINTERFACE;
    AUTO_OBJECT *installer = AutoNewInstaller();
    if (!installer)
        return E_OUTOFMEMORY;
    *out = installer;
    return S_OK;
}

static HRESULT WINAPI factory_lock_server(IClassFactory *self, BOOL lock)
{
    if (lock)
        factory_add_ref(self);
    else
        factory_release(self);
    return S_OK;
}

static const IClassFactoryVtbl factory_vtbl = {
    factory_query_interface, factory_add_ref, factory_release, factory_create_instance, factory_lock_server,
};

static IClassFactory factory = {&factory_vtbl};

MSIAPI HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!IsEqualGUID(iid, &IID_IUnknown) && !IsEqualGUID(iid, &IID_IClassFactory))
        return E_NOINTERFACE;
    if (!IsEqualGUID(clsid, &IID_AutoInstaller))
        return CLASS_E_CLASSNOTAVAILABLE;
    factory_add_ref(&factory);
    *out = &factory;
    return S_OK;
}

MSIAPI HRESULT WINAPI DllCanUnloadNow(void)
{
    return MimeMsiAutoInstances ? S_FALSE : S_OK;
}

MSIAPI int WINAPI QueryInstanceCount(void)
{
    return (int)MimeMsiAutoInstances;
}

/* the version a shell asks of a dll that is not its file version. */
typedef struct DLLVERSIONINFO {
    DWORD cbSize, dwMajorVersion, dwMinorVersion, dwBuildNumber, dwPlatformID;
} DLLVERSIONINFO;

MSIAPI HRESULT WINAPI DllGetVersion(DLLVERSIONINFO *info)
{
    if (info->cbSize < sizeof(*info))
        return E_FAIL;
    info->dwMajorVersion = 5;
    info->dwMinorVersion = 0;
    info->dwBuildNumber = 10011;
    info->dwPlatformID = 2;
    return S_OK;
}

/* ------------------------------------------------------------------ registration */

static void guid_text(const GUID *guid, WCHAR text[39])
{
    StringFromGUID2(guid, text, 39);
}

static LSTATUS put(const WCHAR *path, const WCHAR *name, DWORD type, const void *data, DWORD size, REGSAM view)
{
    HKEY key;
    LSTATUS status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, nullptr, 0, KEY_SET_VALUE | view, nullptr, &key,
                                     nullptr);
    if (status)
        return status;
    status = RegSetValueExW(key, name, 0, type, data, size);
    RegCloseKey(key);
    return status;
}

static LSTATUS put_text(const WCHAR *path, const WCHAR *name, const WCHAR *value)
{
    return put(path, name, REG_SZ, value, (DWORD)((wcslen(value) + 1) * sizeof(WCHAR)), 0);
}

static void join(WCHAR *out, size_t count, const WCHAR *a, const WCHAR *b, const WCHAR *c)
{
    _snwprintf(out, count, L"%s%s%s", a, b, c ? c : L"");
}

static const struct {
    const GUID *Iid;
    PCWSTR Name;
} interfaces[] = {
    {&IID_AutoInstaller, L"Installer"}, {&IID_AutoRecord, L"Record"}, {&IID_AutoSession, L"Session"},
    {&IID_AutoDatabase, L"Database"}, {&IID_AutoSummaryInfo, L"SummaryInfo"}, {&IID_AutoView, L"View"},
    {&IID_AutoUIPreview, L"UIPreview"}, {&IID_AutoFeatureInfo, L"FeatureInfo"}, {&IID_AutoRecordList, L"RecordList"},
    {&IID_AutoStringList, L"StringList"}, {&IID_AutoProduct, L"Product"}, {&IID_AutoPatch, L"Patch"},
    {&IID_AutoComponentPath, L"ComponentPath"}, {&IID_AutoComponent, L"Component"},
    {&IID_AutoComponentClient, L"ComponentClient"},
};

#define EVENT_LOG_KEY L"System\\CurrentControlSet\\Services\\EventLog\\Application\\MsiInstaller"

MSIAPI HRESULT WINAPI DllRegisterServer(void)
{
    WCHAR module[MAX_PATH], clsid[39], typelib[39], path[160];
    if (!GetModuleFileNameW(MimeMsiModule, module, MAX_PATH))
        return E_FAIL;
    guid_text(&IID_AutoInstaller, clsid);
    guid_text(&IID_AutoTypeLib, typelib);
    LSTATUS status = ERROR_SUCCESS, failed = 0;
#define CHECK_PUT(expression) \
    do { \
        status = (expression); \
        if (status) \
            failed = status; \
    } while (0)
    DWORD types = 7;
    CHECK_PUT(put(EVENT_LOG_KEY, L"EventMessageFile", REG_EXPAND_SZ, L"%SystemRoot%\\system32\\msimsg.dll",
                  sizeof(L"%SystemRoot%\\system32\\msimsg.dll"), KEY_WOW64_64KEY));
    CHECK_PUT(put(EVENT_LOG_KEY, L"TypesSupported", REG_DWORD, &types, sizeof(types), KEY_WOW64_64KEY));
    failed = 0;
    join(path, 160, L"SOFTWARE\\Classes\\CLSID\\", clsid, nullptr);
    CHECK_PUT(put_text(path, nullptr, L"Microsoft Windows Installer"));
    join(path, 160, L"SOFTWARE\\Classes\\CLSID\\", clsid, L"\\InprocServer32");
    CHECK_PUT(put_text(path, nullptr, module));
    CHECK_PUT(put_text(path, L"ThreadingModel", L"Apartment"));
    join(path, 160, L"SOFTWARE\\Classes\\CLSID\\", clsid, L"\\InprocHandler32");
    CHECK_PUT(put_text(path, nullptr, L"ole32.dll"));
    join(path, 160, L"SOFTWARE\\Classes\\CLSID\\", clsid, L"\\ProgId");
    CHECK_PUT(put_text(path, nullptr, L"WindowsInstaller.Installer"));
    join(path, 160, L"SOFTWARE\\Classes\\CLSID\\", clsid, L"\\TypeLib");
    CHECK_PUT(put_text(path, nullptr, typelib));
    join(path, 160, L"SOFTWARE\\Classes\\CLSID\\", clsid, L"\\Version");
    CHECK_PUT(put_text(path, nullptr, L"1.0"));
    CHECK_PUT(put_text(L"SOFTWARE\\Classes\\WindowsInstaller.Installer", nullptr, L"Microsoft Windows Installer"));
    CHECK_PUT(put_text(L"SOFTWARE\\Classes\\WindowsInstaller.Installer\\CLSID", nullptr, clsid));
    /* the type library of the classes is the one registration names; the interfaces point at it. */
    join(path, 160, L"SOFTWARE\\Classes\\TypeLib\\", typelib, L"\\1.0");
    CHECK_PUT(put_text(path, nullptr, L"Microsoft Windows Installer Object Library"));
    join(path, 160, L"SOFTWARE\\Classes\\TypeLib\\", typelib, sizeof(void *) == 8 ? L"\\1.0\\409\\win64" : L"\\1.0\\409\\win32");
    CHECK_PUT(put_text(path, nullptr, module));
    join(path, 160, L"SOFTWARE\\Classes\\TypeLib\\", typelib, L"\\1.0\\FLAGS");
    CHECK_PUT(put_text(path, nullptr, L"0"));
    join(path, 160, L"SOFTWARE\\Classes\\TypeLib\\", typelib, L"\\1.0\\HELPDIR");
    WCHAR directory[MAX_PATH];
    wcscpy(directory, module);
    WCHAR *slash = wcsrchr(directory, L'\\');
    if (slash)
        *slash = 0;
    CHECK_PUT(put_text(path, nullptr, directory));
    for (ULONG i = 0; i < RTL_NUMBER_OF(interfaces); i++) {
        WCHAR iid[39], stub[39];
        static const GUID dispatch_stub = {0x00020420, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
        guid_text(interfaces[i].Iid, iid);
        guid_text(&dispatch_stub, stub);
        join(path, 160, L"SOFTWARE\\Classes\\Interface\\", iid, nullptr);
        CHECK_PUT(put_text(path, nullptr, interfaces[i].Name));
        join(path, 160, L"SOFTWARE\\Classes\\Interface\\", iid, L"\\ProxyStubClsid");
        CHECK_PUT(put_text(path, nullptr, stub));
        join(path, 160, L"SOFTWARE\\Classes\\Interface\\", iid, L"\\ProxyStubClsid32");
        CHECK_PUT(put_text(path, nullptr, stub));
        join(path, 160, L"SOFTWARE\\Classes\\Interface\\", iid, L"\\TypeLib");
        CHECK_PUT(put_text(path, nullptr, typelib));
        CHECK_PUT(put_text(path, L"Version", L"1.0"));
    }
    return failed ? (HRESULT)0x80040201 : S_OK;
}

MSIAPI HRESULT WINAPI DllUnregisterServer(void)
{
    WCHAR clsid[39], typelib[39], path[160];
    guid_text(&IID_AutoInstaller, clsid);
    guid_text(&IID_AutoTypeLib, typelib);
    LSTATUS failed = 0;
    HKEY machine = nullptr;
    RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"System\\CurrentControlSet\\Services\\EventLog\\Application", 0, DELETE | KEY_WOW64_64KEY,
                  &machine);
    if (machine) {
        RegDeleteTreeW(machine, L"MsiInstaller");
        RegCloseKey(machine);
    }
    join(path, 160, L"SOFTWARE\\Classes\\CLSID\\", clsid, nullptr);
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, path);
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\WindowsInstaller.Installer");
    join(path, 160, L"SOFTWARE\\Classes\\TypeLib\\", typelib, nullptr);
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, path);
    for (ULONG i = 0; i < RTL_NUMBER_OF(interfaces); i++) {
        WCHAR iid[39];
        guid_text(interfaces[i].Iid, iid);
        join(path, 160, L"SOFTWARE\\Classes\\Interface\\", iid, nullptr);
        LSTATUS status = RegDeleteTreeW(HKEY_LOCAL_MACHINE, path);
        if (status && status != ERROR_FILE_NOT_FOUND && status != ERROR_PATH_NOT_FOUND)
            failed = status;
    }
    return failed ? (HRESULT)0x80040201 : S_OK;
}
