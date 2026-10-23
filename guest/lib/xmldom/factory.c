/* the class factory and class registration. */
#include <xmldom/xmldom.h>

static LONG g_locks;

typedef struct Factory {
    const void *vtbl;
    LONG refs;
    const XClass *cls;
} Factory;

typedef struct FactoryVtbl {
    MIME_IUNKNOWN_METHODS(Factory)
    HRESULT(STDMETHODCALLTYPE *CreateInstance)(Factory *This, IUnknown *outer, REFIID riid, void **object);
    HRESULT(STDMETHODCALLTYPE *LockServer)(Factory *This, BOOL lock);
} FactoryVtbl;

static HRESULT STDMETHODCALLTYPE f_qi(Factory *f, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IClassFactory))
        return E_NOINTERFACE;
    InterlockedIncrement(&f->refs);
    *out = f;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE f_addref(Factory *f)
{
    return (ULONG)InterlockedIncrement(&f->refs);
}

static ULONG STDMETHODCALLTYPE f_release(Factory *f)
{
    LONG refs = InterlockedDecrement(&f->refs);
    if (refs == 0) {
        xd_free(f);
        InterlockedDecrement(&xd_objects);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE f_create(Factory *f, IUnknown *outer, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    XDoc *doc = xd_doc_new(f->cls->flags);
    if (!doc)
        return E_OUTOFMEMORY;
    HRESULT hr = xn_query(&doc->node, riid, out);
    xn_release(&doc->node);
    return hr;
}

static HRESULT STDMETHODCALLTYPE f_lock(Factory *f, BOOL lock)
{
    if (lock)
        InterlockedIncrement(&g_locks);
    else
        InterlockedDecrement(&g_locks);
    return S_OK;
}

static const FactoryVtbl factory_vtbl = {f_qi, f_addref, f_release, f_create, f_lock};

HRESULT xd_get_class_object(const XClass *classes, UINT count, REFCLSID clsid, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    for (UINT i = 0; i < count; i++) {
        if (!IsEqualGUID(clsid, classes[i].clsid))
            continue;
        Factory *f = xd_alloc(sizeof(Factory));
        if (!f)
            return E_OUTOFMEMORY;
        f->vtbl = &factory_vtbl;
        f->refs = 1;
        f->cls = &classes[i];
        InterlockedIncrement(&xd_objects);
        HRESULT hr = f_qi(f, riid, out);
        f_release(f);
        return hr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT xd_can_unload(void)
{
    return xd_objects == 0 && g_locks == 0 ? S_OK : S_FALSE;
}


static void guid_string(const GUID *g, WCHAR *out)
{
    static const WCHAR hex[] = L"0123456789ABCDEF";
    UINT n = 0;
    out[n++] = '{';
    for (int i = 7; i >= 0; i--)
        out[n++] = hex[(g->Data1 >> (i * 4)) & 15];
    out[n++] = '-';
    for (int i = 3; i >= 0; i--)
        out[n++] = hex[(g->Data2 >> (i * 4)) & 15];
    out[n++] = '-';
    for (int i = 3; i >= 0; i--)
        out[n++] = hex[(g->Data3 >> (i * 4)) & 15];
    out[n++] = '-';
    for (int i = 0; i < 2; i++) {
        out[n++] = hex[g->Data4[i] >> 4];
        out[n++] = hex[g->Data4[i] & 15];
    }
    out[n++] = '-';
    for (int i = 2; i < 8; i++) {
        out[n++] = hex[g->Data4[i] >> 4];
        out[n++] = hex[g->Data4[i] & 15];
    }
    out[n++] = '}';
    out[n] = 0;
}

static LONG put_value_typed(const WCHAR *path, const WCHAR *name, const WCHAR *value, DWORD type)
{
    HKEY key;
    LONG r = RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
    if (r)
        return r;
    r = RegSetValueExW(key, name, 0, type, (const BYTE *)value, (xd_wlen(value) + 1) * sizeof(WCHAR));
    RegCloseKey(key);
    return r;
}

static LONG put_value(const WCHAR *path, const WCHAR *name, const WCHAR *value)
{
    return put_value_typed(path, name, value, REG_SZ);
}

static void compose(WCHAR *out, UINT cap, const WCHAR *a, const WCHAR *b, const WCHAR *c)
{
    UINT n = 0;
    const WCHAR *parts[3] = {a, b, c};
    for (UINT i = 0; i < 3; i++)
        for (const WCHAR *p = parts[i]; p && *p && n + 1 < cap; p++)
            out[n++] = *p;
    out[n] = 0;
}

HRESULT xd_register_server(const XClass *classes, UINT count, HMODULE module, BOOL unregister)
{
    /* always registered under System32, which each architecture resolves to its own copy. */
    WCHAR full[MAX_PATH], file[MAX_PATH];
    if (!GetModuleFileNameW(module, full, MAX_PATH))
        return E_FAIL;
    const WCHAR *base = full;
    for (const WCHAR *p = full; *p; p++)
        if (*p == '\\' || *p == '/')
            base = p + 1;
    compose(file, MAX_PATH, L"%SystemRoot%\\System32\\", base, nullptr);
    HRESULT hr = S_OK;
    for (UINT i = 0; i < count; i++) {
        WCHAR id[40], path[160], prog[160];
        guid_string(classes[i].clsid, id);
        compose(path, 160, L"SOFTWARE\\Classes\\CLSID\\", id, nullptr);
        if (unregister) {
            RegDeleteTreeW(HKEY_LOCAL_MACHINE, path);
            if (classes[i].progid) {
                compose(prog, 160, L"SOFTWARE\\Classes\\", classes[i].progid, nullptr);
                RegDeleteTreeW(HKEY_LOCAL_MACHINE, prog);
            }
            continue;
        }
        LONG r = put_value(path, nullptr, classes[i].name ? classes[i].name : L"XML DOM Document");
        compose(prog, 160, path, L"\\InprocServer32", nullptr);
        if (!r)
            r = put_value_typed(prog, nullptr, file, REG_EXPAND_SZ);
        if (!r)
            r = put_value(prog, L"ThreadingModel", L"Both");
        if (!r && classes[i].progid) {
            compose(prog, 160, path, L"\\ProgID", nullptr);
            r = put_value(prog, nullptr, classes[i].progid);
        }
        if (!r && classes[i].version_independent) {
            compose(prog, 160, path, L"\\VersionIndependentProgID", nullptr);
            r = put_value(prog, nullptr, classes[i].version_independent);
        }
        if (!r && classes[i].progid) {
            compose(prog, 160, L"SOFTWARE\\Classes\\", classes[i].progid, L"\\CLSID");
            r = put_value(prog, nullptr, id);
        }
        if (r)
            hr = (HRESULT)(0x80070000 | (r & 0xFFFF));
    }
    return hr;
}
