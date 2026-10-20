/*
 * the class of the proxies and stubs of oleautomation, PSOAInterface: the one that every interface of a type library which
 * is dual or for automation is registered with. It finds the type library the interface is registered in and has the
 * proxy or the stub made from it by rpcrt4, as windows does.
 */
#include "oleaut32_private.h"
#include <mime/win32/rpcproxy.h>
#include <mime/win32/shlwapi.h>

/* ------------------------------------------------------------------ finding the type information of an interface */

static BOOL parse_hex(const WCHAR **s, unsigned *value)
{
    unsigned v = 0;
    int n = 0;
    for (; **s; (*s)++, n++) {
        WCHAR ch = **s;
        unsigned digit;
        if (ch >= L'0' && ch <= L'9')
            digit = ch - L'0';
        else if (ch >= L'a' && ch <= L'f')
            digit = ch - L'a' + 10;
        else if (ch >= L'A' && ch <= L'F')
            digit = ch - L'A' + 10;
        else
            break;
        v = v * 16 + digit;
    }
    *value = v;
    return n > 0;
}

/* the type information of an interface, from the library that Interface\{iid}\TypeLib names. */
static HRESULT typeinfo_of(REFIID iid, ITypeInfo **out)
{
    WCHAR g[40], path[100];
    StringFromGUID2(iid, g, ARRAYSIZE(g));
    wsprintfW(path, L"Interface\\%ls\\TypeLib", g);
    WCHAR libid[64], version[32];
    DWORD size = sizeof(libid);
    if (RegGetValueW(HKEY_CLASSES_ROOT, path, nullptr, RRF_RT_REG_SZ, nullptr, libid, &size))
        return REGDB_E_IIDNOTREG;
    size = sizeof(version);
    unsigned major = 1, minor = 0;
    if (!RegGetValueW(HKEY_CLASSES_ROOT, path, L"Version", RRF_RT_REG_SZ, nullptr, version, &size)) {
        const WCHAR *s = version;
        if (parse_hex(&s, &major) && *s == L'.') {
            s++;
            parse_hex(&s, &minor);
        }
    }
    GUID lib_guid;
    if (CLSIDFromString(libid, &lib_guid))
        return REGDB_E_IIDNOTREG;
    ITypeLib *lib = nullptr;
    HRESULT hr = LoadRegTypeLib(&lib_guid, (WORD)major, (WORD)minor, 0, &lib);
    if (FAILED(hr))
        hr = LoadRegTypeLib(&lib_guid, (WORD)major, (WORD)minor, 0x409, &lib);
    if (FAILED(hr))
        return hr;
    ITypeInfo *ti = nullptr;
    hr = lib->lpVtbl->GetTypeInfoOfGuid(lib, iid, &ti);
    lib->lpVtbl->Release(lib);
    if (FAILED(hr))
        return hr;
    *out = ti;
    return S_OK;
}

/* ------------------------------------------------------------------ the factory */

static HRESULT STDMETHODCALLTYPE univ_query(IPSFactoryBuffer *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_IPSFactoryBuffer))
        return E_NOINTERFACE;
    *object = This;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE univ_add_ref(IPSFactoryBuffer *This)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE univ_release(IPSFactoryBuffer *This)
{
    return 1;
}

static HRESULT STDMETHODCALLTYPE univ_create_proxy(IPSFactoryBuffer *This, IUnknown *outer, REFIID riid,
                                                   IRpcProxyBuffer **proxy, void **object)
{
    ITypeInfo *info;
    if (!proxy || !object)
        return E_POINTER;
    *proxy = nullptr;
    *object = nullptr;
    HRESULT hr = typeinfo_of(riid, &info);
    if (FAILED(hr))
        return hr;
    hr = CreateProxyFromTypeInfo(info, outer, riid, proxy, object);
    info->lpVtbl->Release(info);
    return hr;
}

static HRESULT STDMETHODCALLTYPE univ_create_stub(IPSFactoryBuffer *This, REFIID riid, IUnknown *server, IRpcStubBuffer **stub)
{
    ITypeInfo *info;
    if (!stub)
        return E_POINTER;
    *stub = nullptr;
    HRESULT hr = typeinfo_of(riid, &info);
    if (FAILED(hr))
        return hr;
    hr = CreateStubFromTypeInfo(info, riid, server, stub);
    info->lpVtbl->Release(info);
    return hr;
}

static const IPSFactoryBufferVtbl g_univ_vtbl = {univ_query, univ_add_ref, univ_release, univ_create_proxy,
                                                 univ_create_stub};
static IPSFactoryBuffer g_univ_factory = {&g_univ_vtbl};

HRESULT AutUniversalFactory(REFIID iid, void **object)
{
    return univ_query(&g_univ_factory, iid, object);
}
