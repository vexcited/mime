/*
 * scrrun.dll: the scripting runtime. The file system object, its drives, folders, files and text streams, and the dictionary.
 */
#define SCRRUNAPI DECLSPEC_EXPORT
#include "scrrun_private.h"

/* the compiler references this symbol from code that uses floating point. */
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
    const CLSID *Clsid;
} FACTORY;

static HRESULT STDMETHODCALLTYPE factory_query_interface(IClassFactory *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IClassFactory))
        return E_NOINTERFACE;
    *object = This;
    This->lpVtbl->AddRef(This);
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

HRESULT FsCreate(REFIID riid, void **object);

static HRESULT STDMETHODCALLTYPE factory_create_instance(IClassFactory *This, IUnknown *outer, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    const CLSID *clsid = ((FACTORY *)This)->Clsid;
    if (IsEqualCLSID(clsid, &CLSID_Dictionary))
        return DictionaryCreate(riid, object);
    return FsCreate(riid, object);
}

static HRESULT STDMETHODCALLTYPE factory_lock_server(IClassFactory *This, BOOL lock)
{
    if (lock)
        ScrLockDll();
    else
        ScrUnlockDll();
    return S_OK;
}

static const IClassFactoryVtbl g_factory_vtbl = {
    factory_query_interface, factory_add_ref, factory_release, factory_create_instance, factory_lock_server,
};
static FACTORY g_dictionary_factory = {{&g_factory_vtbl}, &CLSID_Dictionary};
static FACTORY g_filesystem_factory = {{&g_factory_vtbl}, &CLSID_FileSystemObject};

DECLSPEC_EXPORT HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    FACTORY *factory;
    if (IsEqualCLSID(clsid, &CLSID_Dictionary))
        factory = &g_dictionary_factory;
    else if (IsEqualCLSID(clsid, &CLSID_FileSystemObject))
        factory = &g_filesystem_factory;
    else
        return CLASS_E_CLASSNOTAVAILABLE;
    return factory->Factory.lpVtbl->QueryInterface(&factory->Factory, riid, object);
}

DECLSPEC_EXPORT HRESULT WINAPI DllCanUnloadNow(void)
{
    return ScrLockCount() ? S_FALSE : S_OK;
}

/* the text of a string of the table, the help strings of the type library. */
DECLSPEC_EXPORT HRESULT WINAPI DLLGetDocumentation(ITypeLib *library, ITypeInfo *info, LCID lcid, DWORD context,
                                                   BSTR *text)
{
    if (!text)
        return E_POINTER;
    *text = nullptr;
    WCHAR buffer[256];
    int length = LoadStringW(g_module, (UINT)(context & 0xFFFF), buffer, 256);
    if (!length)
        return E_FAIL;
    return AllocateBstr(buffer, length, text);
}

HRESULT WINAPI DoOpenPipeStream(HANDLE pipe, IOMode mode, ITextStream **stream)
{
    BYTE_STREAM *bytes;
    HRESULT hr = ByteStreamFromHandle(pipe, STREAM_PIPE, FALSE, &bytes);
    if (FAILED(hr))
        return hr;
    hr = TextStreamCreate(bytes, mode, TristateFalse, STREAM_PIPE, stream);
    ByteStreamRelease(bytes);
    return hr;
}

/* ------------------------------------------------------------------ registration */

static void reg_value(HKEY root, const WCHAR *key, const WCHAR *name, const WCHAR *value)
{
    HKEY handle;
    if (RegCreateKeyExW(root, key, 0, nullptr, 0, KEY_WRITE, nullptr, &handle, nullptr))
        return;
    RegSetValueExW(handle, name, 0, REG_SZ, (const BYTE *)value, (DWORD)((wcslen(value) + 1) * sizeof(WCHAR)));
    RegCloseKey(handle);
}

static void reg_delete_tree(HKEY root, const WCHAR *key)
{
    HKEY handle;
    if (RegOpenKeyExW(root, key, 0, KEY_ALL_ACCESS, &handle))
        return;
    WCHAR name[260];
    while (TRUE) {
        DWORD size = 260;
        if (RegEnumKeyExW(handle, 0, name, &size, nullptr, nullptr, nullptr, nullptr))
            break;
        reg_delete_tree(handle, name);
    }
    RegCloseKey(handle);
    RegDeleteKeyW(root, key);
}

static BOOL is_registered_class(const TYPEATTR *attr)
{
    /* the encoder needs the script engines, which this system does not have. */
    return attr->typekind == TKIND_COCLASS && (attr->wTypeFlags & TYPEFLAG_FCANCREATE) &&
           !IsEqualGUID(&attr->guid, &CLSID_Encoder);
}

static void guid_text(const GUID *guid, WCHAR *text)
{
    StringFromGUID2(guid, text, 40);
}

DECLSPEC_EXPORT HRESULT WINAPI DllRegisterServer(void)
{
    WCHAR path[MAX_PATH];
    if (!GetModuleFileNameW(g_module, path, MAX_PATH))
        wcscpy(path, L"scrrun.dll");
    ITypeLib *library;
    HRESULT hr = LoadTypeLibEx(path, REGKIND_REGISTER, &library);
    if (FAILED(hr))
        return hr;
    TLIBATTR *lib_attr;
    hr = library->lpVtbl->GetLibAttr(library, &lib_attr);
    if (SUCCEEDED(hr)) {
        WCHAR lib_text[40], version[32], key[160];
        guid_text(&lib_attr->guid, lib_text);
        _snwprintf(version, 32, L"%d.%d", lib_attr->wMajorVerNum, lib_attr->wMinorVerNum);
        UINT count = library->lpVtbl->GetTypeInfoCount(library);
        for (UINT i = 0; i < count; i++) {
            ITypeInfo *info;
            if (FAILED(library->lpVtbl->GetTypeInfo(library, i, &info)))
                continue;
            TYPEATTR *attr;
            if (SUCCEEDED(info->lpVtbl->GetTypeAttr(info, &attr))) {
                BSTR name = nullptr, doc = nullptr;
                if (is_registered_class(attr) &&
                    SUCCEEDED(library->lpVtbl->GetDocumentation(library, (INT)i, &name, &doc, nullptr, nullptr))) {
                    WCHAR clsid[40], prog[96];
                    guid_text(&attr->guid, clsid);
                    _snwprintf(prog, 96, L"Scripting.%s", name);
                    BOOL both = !wcscmp(name, L"FileSystemObject");
                    _snwprintf(key, 160, L"CLSID\\%s", clsid);
                    reg_value(HKEY_CLASSES_ROOT, key, nullptr, doc ? doc : L"");
                    _snwprintf(key, 160, L"CLSID\\%s\\InprocServer32", clsid);
                    reg_value(HKEY_CLASSES_ROOT, key, nullptr, path);
                    reg_value(HKEY_CLASSES_ROOT, key, L"ThreadingModel", both ? L"Both" : L"Apartment");
                    if (!both) {
                        _snwprintf(key, 160, L"CLSID\\%s\\Implemented Categories\\{7DD95801-9882-11CF-9FA9-00AA006C42C4}",
                                   clsid);
                        reg_value(HKEY_CLASSES_ROOT, key, nullptr, L"");
                    }
                    _snwprintf(key, 160, L"CLSID\\%s\\TypeLib", clsid);
                    reg_value(HKEY_CLASSES_ROOT, key, nullptr, lib_text);
                    _snwprintf(key, 160, L"CLSID\\%s\\Version", clsid);
                    reg_value(HKEY_CLASSES_ROOT, key, nullptr, version);
                    _snwprintf(key, 160, L"CLSID\\%s\\ProgID", clsid);
                    reg_value(HKEY_CLASSES_ROOT, key, nullptr, prog);
                    reg_value(HKEY_CLASSES_ROOT, prog, nullptr, doc ? doc : L"");
                    _snwprintf(key, 160, L"%s\\CLSID", prog);
                    reg_value(HKEY_CLASSES_ROOT, key, nullptr, clsid);
                }
                SysFreeString(name);
                SysFreeString(doc);
                info->lpVtbl->ReleaseTypeAttr(info, attr);
            }
            info->lpVtbl->Release(info);
        }
        library->lpVtbl->ReleaseTLibAttr(library, lib_attr);
    }
    library->lpVtbl->Release(library);
    return hr;
}

DECLSPEC_EXPORT HRESULT WINAPI DllUnregisterServer(void)
{
    WCHAR path[MAX_PATH];
    if (!GetModuleFileNameW(g_module, path, MAX_PATH))
        wcscpy(path, L"scrrun.dll");
    ITypeLib *library;
    HRESULT hr = LoadTypeLibEx(path, REGKIND_NONE, &library);
    if (FAILED(hr))
        return hr;
    TLIBATTR *lib_attr;
    hr = library->lpVtbl->GetLibAttr(library, &lib_attr);
    if (SUCCEEDED(hr)) {
        UINT count = library->lpVtbl->GetTypeInfoCount(library);
        for (UINT i = 0; i < count; i++) {
            ITypeInfo *info;
            if (FAILED(library->lpVtbl->GetTypeInfo(library, i, &info)))
                continue;
            TYPEATTR *attr;
            if (SUCCEEDED(info->lpVtbl->GetTypeAttr(info, &attr))) {
                BSTR name = nullptr;
                if (attr->typekind == TKIND_COCLASS &&
                    SUCCEEDED(library->lpVtbl->GetDocumentation(library, (INT)i, &name, nullptr, nullptr, nullptr))) {
                    WCHAR clsid[40], key[160], prog[96];
                    guid_text(&attr->guid, clsid);
                    _snwprintf(prog, 96, L"Scripting.%s", name);
                    _snwprintf(key, 160, L"CLSID\\%s", clsid);
                    reg_delete_tree(HKEY_CLASSES_ROOT, key);
                    reg_delete_tree(HKEY_CLASSES_ROOT, prog);
                }
                SysFreeString(name);
                info->lpVtbl->ReleaseTypeAttr(info, attr);
            }
            info->lpVtbl->Release(info);
        }
        library->lpVtbl->ReleaseTLibAttr(library, lib_attr);
    }
    library->lpVtbl->Release(library);
    UnRegisterTypeLib(&LIBID_Scripting, 1, 0, 0, sizeof(void *) == 8 ? SYS_WIN64 : SYS_WIN32);
    return S_OK;
}
