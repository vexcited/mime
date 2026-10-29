/*
 * the session of the process (CoInternetGetSession), the check for monikers that bind asynchronously, and navigation to
 * an address. the session keeps the options of urlmon and takes no protocol handlers or filters, since binding goes
 * to the protocols of wininet only. navigation is done by the shell, which opens the address in its default program.
 */
#include "urlmon_private.h"

HRESULT WINAPI UrlMkGetSessionOption(DWORD option, LPVOID buffer, DWORD length, DWORD *length_out, DWORD reserved);
HRESULT WINAPI UrlMkSetSessionOption(DWORD option, LPVOID buffer, DWORD length, DWORD reserved);

/* ------------------------------------------------------------------ the session */

typedef struct IInternetSessionVtbl IInternetSessionVtbl;
typedef struct IInternetSession {
    const IInternetSessionVtbl *lpVtbl;
} IInternetSession;

struct IInternetSessionVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IInternetSession *This, REFIID iid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IInternetSession *This);
    ULONG(STDMETHODCALLTYPE *Release)(IInternetSession *This);
    HRESULT(STDMETHODCALLTYPE *RegisterNameSpace)(IInternetSession *This, IClassFactory *factory, REFCLSID clsid,
                                                  LPCWSTR protocol, ULONG count, const LPCWSTR *patterns,
                                                  DWORD reserved);
    HRESULT(STDMETHODCALLTYPE *UnregisterNameSpace)(IInternetSession *This, IClassFactory *factory, LPCWSTR protocol);
    HRESULT(STDMETHODCALLTYPE *RegisterMimeFilter)(IInternetSession *This, IClassFactory *factory, REFCLSID clsid,
                                                   LPCWSTR mime);
    HRESULT(STDMETHODCALLTYPE *UnregisterMimeFilter)(IInternetSession *This, IClassFactory *factory, LPCWSTR mime);
    HRESULT(STDMETHODCALLTYPE *CreateBinding)(IInternetSession *This, IBindCtx *context, LPCWSTR url, IUnknown *outer,
                                              IUnknown **unknown, void **protocol, DWORD options);
    HRESULT(STDMETHODCALLTYPE *SetSessionOption)(IInternetSession *This, DWORD option, LPVOID buffer, DWORD length,
                                                 DWORD reserved);
    HRESULT(STDMETHODCALLTYPE *GetSessionOption)(IInternetSession *This, DWORD option, LPVOID buffer, DWORD *length,
                                                 DWORD reserved);
};

MIME_DEFINE_GUID(IID_IInternetSession_, 0x79eac9e7, 0xbaf9, 0x11ce, 0x8c, 0x82, 0x00, 0xaa, 0x00, 0x4b, 0xa9, 0x0b);

static HRESULT STDMETHODCALLTYPE session_query(IInternetSession *self, REFIID iid, void **object)
{
    if (!object)
        return E_POINTER;
    if (!memcmp(iid, &IID_IUnknown, sizeof(GUID)) || !memcmp(iid, &IID_IInternetSession_, sizeof(GUID))) {
        *object = self;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

/* one session lives as long as the process does. */
static ULONG STDMETHODCALLTYPE session_addref(IInternetSession *self)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE session_release(IInternetSession *self)
{
    return 1;
}

static HRESULT STDMETHODCALLTYPE session_register_namespace(IInternetSession *self, IClassFactory *factory,
                                                            REFCLSID clsid, LPCWSTR protocol, ULONG count,
                                                            const LPCWSTR *patterns, DWORD reserved)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE session_unregister_namespace(IInternetSession *self, IClassFactory *factory,
                                                              LPCWSTR protocol)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE session_register_filter(IInternetSession *self, IClassFactory *factory,
                                                         REFCLSID clsid, LPCWSTR mime)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE session_unregister_filter(IInternetSession *self, IClassFactory *factory,
                                                           LPCWSTR mime)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE session_create_binding(IInternetSession *self, IBindCtx *context, LPCWSTR url,
                                                        IUnknown *outer, IUnknown **unknown, void **protocol,
                                                        DWORD options)
{
    if (unknown)
        *unknown = nullptr;
    if (protocol)
        *protocol = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE session_set_option(IInternetSession *self, DWORD option, LPVOID buffer, DWORD length,
                                                    DWORD reserved)
{
    return UrlMkSetSessionOption(option, buffer, length, reserved);
}

static HRESULT STDMETHODCALLTYPE session_get_option(IInternetSession *self, DWORD option, LPVOID buffer, DWORD *length,
                                                    DWORD reserved)
{
    if (!length)
        return E_INVALIDARG;
    DWORD returned = 0;
    HRESULT hr = UrlMkGetSessionOption(option, buffer, *length, &returned, reserved);
    *length = returned;
    return hr;
}

static const IInternetSessionVtbl session_vtable = {
    session_query,
    session_addref,
    session_release,
    session_register_namespace,
    session_unregister_namespace,
    session_register_filter,
    session_unregister_filter,
    session_create_binding,
    session_set_option,
    session_get_option,
};

static IInternetSession g_session = {&session_vtable};

DECLSPEC_EXPORT HRESULT WINAPI CoInternetGetSession(DWORD mode, IInternetSession **session, DWORD reserved)
{
    if (!session)
        return E_INVALIDARG;
    *session = &g_session;
    return S_OK;
}

/* ------------------------------------------------------------------ monikers */

DECLSPEC_EXPORT HRESULT WINAPI IsAsyncMoniker(IMoniker *moniker)
{
    if (!moniker)
        return E_INVALIDARG;
    IUnknown *async = nullptr;
    if (FAILED(moniker->lpVtbl->QueryInterface(moniker, &IID_IAsyncMoniker, (void **)&async)))
        return S_FALSE;
    async->lpVtbl->Release(async);
    return S_OK;
}

/* ------------------------------------------------------------------ navigation */

static HRESULT navigate(LPCWSTR address, LPCWSTR location)
{
    if (!address || !*address)
        return E_FAIL;
    SIZE_T length = lstrlenW(address) + (location && *location ? lstrlenW(location) + 1 : 0);
    PWSTR target = UrlAlloc((length + 1) * sizeof(WCHAR));
    if (!target)
        return E_OUTOFMEMORY;
    lstrcpyW(target, address);
    if (location && *location) {
        lstrcatW(target, L"#");
        lstrcatW(target, location);
    }
    HMODULE shell = LoadLibraryW(L"shell32.dll");
    typedef HINSTANCE(WINAPI * EXECUTE)(HWND, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, INT);
    EXECUTE execute = shell ? (EXECUTE)(void *)GetProcAddress(shell, "ShellExecuteW") : nullptr;
    HRESULT hr = E_FAIL;
    if (execute) {
        ULONG_PTR result = (ULONG_PTR)execute(nullptr, L"open", target, nullptr, nullptr, SW_SHOWNORMAL);
        hr = result > 32 ? S_OK : HRESULT_FROM_WIN32((DWORD)result ? (DWORD)result : ERROR_FILE_NOT_FOUND);
    }
    UrlFree(target);
    return hr;
}

DECLSPEC_EXPORT HRESULT WINAPI HlinkSimpleNavigateToString(LPCWSTR target, LPCWSTR location, LPCWSTR frame,
                                                           IUnknown *site, IBindCtx *context,
                                                           IBindStatusCallback *callback, DWORD flags, DWORD reserved)
{
    if (reserved)
        return E_INVALIDARG;
    return navigate(target, location);
}

DECLSPEC_EXPORT HRESULT WINAPI HlinkSimpleNavigateToMoniker(IMoniker *target, LPCWSTR location, LPCWSTR frame,
                                                            IUnknown *site, IBindCtx *context,
                                                            IBindStatusCallback *callback, DWORD flags, DWORD reserved)
{
    if (reserved)
        return E_INVALIDARG;
    LPOLESTR name = nullptr;
    if (!target || FAILED(target->lpVtbl->GetDisplayName(target, context, nullptr, &name)))
        return E_FAIL;
    HRESULT hr = navigate(name, location);
    CoTaskMemFree(name);
    return hr;
}
