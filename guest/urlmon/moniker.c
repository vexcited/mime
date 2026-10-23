/*
 * url monikers and the bind status callbacks bind contexts carry. binding to storage runs the download engine
 * synchronously, so an asynchronous binding gets its data through OnDataAvailable before BindToStorage returns
 * MK_S_ASYNCHRONOUS.
 */
#include "urlmon_private.h"

#define HOLDER L"_BSCB_Holder_"

typedef struct {
    IMoniker Interface;
    LONG volatile Refs;
    PWSTR Url;
} URL_MONIKER;

static URL_MONIKER *moniker_of(IMoniker *iface)
{
    return (URL_MONIKER *)iface;
}

static HRESULT STDMETHODCALLTYPE um_QueryInterface(IMoniker *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IMoniker) || IsEqualIID(riid, &IID_IPersist) ||
        IsEqualIID(riid, &IID_IPersistStream) || IsEqualIID(riid, &IID_IAsyncMoniker)) {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE um_AddRef(IMoniker *iface)
{
    return (ULONG)InterlockedIncrement(&moniker_of(iface)->Refs);
}

static ULONG STDMETHODCALLTYPE um_Release(IMoniker *iface)
{
    URL_MONIKER *moniker = moniker_of(iface);
    LONG refs = InterlockedDecrement(&moniker->Refs);
    if (!refs) {
        UrlFree(moniker->Url);
        UrlFree(moniker);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE um_GetClassID(IMoniker *iface, CLSID *clsid)
{
    if (!clsid)
        return E_POINTER;
    *clsid = CLSID_StdURLMoniker;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE um_IsDirty(IMoniker *iface)
{
    return S_FALSE;
}

/* persisted as the byte length including the terminator, then the url. */
static HRESULT STDMETHODCALLTYPE um_Load(IMoniker *iface, IStream *stream)
{
    URL_MONIKER *moniker = moniker_of(iface);
    ULONG bytes = 0, read;
    HRESULT hr = stream->lpVtbl->Read(stream, &bytes, sizeof(bytes), &read);
    if (FAILED(hr) || read != sizeof(bytes) || bytes < sizeof(WCHAR) || bytes > 0x100000)
        return FAILED(hr) ? hr : E_FAIL;
    PWSTR url = UrlAlloc(bytes);
    if (!url)
        return E_OUTOFMEMORY;
    hr = stream->lpVtbl->Read(stream, url, bytes, &read);
    if (FAILED(hr) || read != bytes) {
        UrlFree(url);
        return FAILED(hr) ? hr : E_FAIL;
    }
    url[bytes / sizeof(WCHAR) - 1] = 0;
    UrlFree(moniker->Url);
    moniker->Url = url;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE um_Save(IMoniker *iface, IStream *stream, BOOL clear_dirty)
{
    URL_MONIKER *moniker = moniker_of(iface);
    ULONG bytes = ((ULONG)lstrlenW(moniker->Url) + 1) * sizeof(WCHAR);
    HRESULT hr = stream->lpVtbl->Write(stream, &bytes, sizeof(bytes), nullptr);
    return FAILED(hr) ? hr : stream->lpVtbl->Write(stream, moniker->Url, bytes, nullptr);
}

static HRESULT STDMETHODCALLTYPE um_GetSizeMax(IMoniker *iface, ULARGE_INTEGER *size)
{
    if (!size)
        return E_INVALIDARG;
    size->QuadPart = sizeof(ULONG) + ((ULONGLONG)lstrlenW(moniker_of(iface)->Url) + 1) * sizeof(WCHAR);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE um_BindToObject(IMoniker *iface, IBindCtx *context, IMoniker *left, REFIID riid,
                                                 void **object)
{
    if (object)
        *object = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE um_BindToStorage(IMoniker *iface, IBindCtx *context, IMoniker *left, REFIID riid,
                                                  void **object)
{
    if (!object)
        return E_INVALIDARG;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IStream) && !IsEqualIID(riid, &IID_IUnknown))
        return E_NOINTERFACE;
    IBindStatusCallback *callback = context ? UrlBoundCallback(context) : nullptr;
    BOOL async = FALSE;
    if (callback) {
        BINDINFO info = {sizeof(info)};
        DWORD bindf = 0;
        if (SUCCEEDED(callback->lpVtbl->GetBindInfo(callback, &bindf, &info))) {
            async = (bindf & BINDF_ASYNCHRONOUS) != 0;
            CoTaskMemFree(info.szExtraInfo);
            CoTaskMemFree(info.szCustomVerb);
            if (info.pUnk)
                info.pUnk->lpVtbl->Release(info.pUnk);
        }
    }
    IStream *stream = nullptr;
    HRESULT hr = UrlDownload(moniker_of(iface)->Url, callback ? URL_SINK_PUSH : URL_SINK_STREAM, 0, callback,
                             nullptr, &stream, nullptr, 0);
    if (callback)
        callback->lpVtbl->Release(callback);
    if (FAILED(hr))
        return hr;
    if (async) {
        if (stream)
            stream->lpVtbl->Release(stream);
        return MK_S_ASYNCHRONOUS;
    }
    if (!stream)
        return E_FAIL;
    *object = stream;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE um_Reduce(IMoniker *iface, IBindCtx *context, DWORD how_far, IMoniker **left,
                                           IMoniker **reduced)
{
    if (!reduced)
        return E_INVALIDARG;
    iface->lpVtbl->AddRef(iface);
    *reduced = iface;
    return MK_S_REDUCED_TO_SELF;
}

static HRESULT STDMETHODCALLTYPE um_ComposeWith(IMoniker *iface, IMoniker *right, BOOL only_if_not_generic,
                                                IMoniker **composite)
{
    if (composite)
        *composite = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE um_Enum(IMoniker *iface, BOOL forward, IEnumMoniker **enumerator)
{
    if (!enumerator)
        return E_INVALIDARG;
    *enumerator = nullptr;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE um_IsEqual(IMoniker *iface, IMoniker *other)
{
    if (!other)
        return E_INVALIDARG;
    DWORD kind = 0;
    LPOLESTR name = nullptr;
    if (FAILED(other->lpVtbl->IsSystemMoniker(other, &kind)) || kind != MKSYS_URLMONIKER)
        return S_FALSE;
    HRESULT hr = S_FALSE;
    if (SUCCEEDED(other->lpVtbl->GetDisplayName(other, nullptr, nullptr, &name)) && name &&
        !lstrcmpW(name, moniker_of(iface)->Url))
        hr = S_OK;
    CoTaskMemFree(name);
    return hr;
}

static HRESULT STDMETHODCALLTYPE um_Hash(IMoniker *iface, DWORD *hash)
{
    if (!hash)
        return E_INVALIDARG;
    DWORD value = 0;
    for (LPCWSTR p = moniker_of(iface)->Url; *p; p++)
        value = value * 37 + *p;
    *hash = value;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE um_IsRunning(IMoniker *iface, IBindCtx *context, IMoniker *left,
                                              IMoniker *newly_running)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE um_GetTimeOfLastChange(IMoniker *iface, IBindCtx *context, IMoniker *left,
                                                        FILETIME *time)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE um_Inverse(IMoniker *iface, IMoniker **inverse)
{
    if (inverse)
        *inverse = nullptr;
    return MK_E_NOINVERSE;
}

static HRESULT STDMETHODCALLTYPE um_CommonPrefixWith(IMoniker *iface, IMoniker *other, IMoniker **prefix)
{
    if (prefix)
        *prefix = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE um_RelativePathTo(IMoniker *iface, IMoniker *other, IMoniker **relative)
{
    if (relative)
        *relative = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE um_GetDisplayName(IMoniker *iface, IBindCtx *context, IMoniker *left, LPOLESTR *name)
{
    if (!name)
        return E_INVALIDARG;
    LPCWSTR url = moniker_of(iface)->Url;
    SIZE_T bytes = ((SIZE_T)lstrlenW(url) + 1) * sizeof(WCHAR);
    *name = CoTaskMemAlloc(bytes);
    if (!*name)
        return E_OUTOFMEMORY;
    memcpy(*name, url, bytes);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE um_ParseDisplayName(IMoniker *iface, IBindCtx *context, IMoniker *left,
                                                     LPOLESTR name, ULONG *eaten, IMoniker **out)
{
    if (out)
        *out = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE um_IsSystemMoniker(IMoniker *iface, DWORD *kind)
{
    if (!kind)
        return E_INVALIDARG;
    *kind = MKSYS_URLMONIKER;
    return S_OK;
}

static const IMonikerVtbl UrlMonikerVtbl = {
    um_QueryInterface, um_AddRef,        um_Release,          um_GetClassID,         um_IsDirty,
    um_Load,           um_Save,          um_GetSizeMax,       um_BindToObject,       um_BindToStorage,
    um_Reduce,         um_ComposeWith,   um_Enum,             um_IsEqual,            um_Hash,
    um_IsRunning,      um_GetTimeOfLastChange, um_Inverse,    um_CommonPrefixWith,   um_RelativePathTo,
    um_GetDisplayName, um_ParseDisplayName, um_IsSystemMoniker,
};

/* resolves against the context moniker's display name, joined like UrlCombine. */
static PWSTR Resolve(IMoniker *context, LPCWSTR url, DWORD flags)
{
    LPOLESTR base = nullptr;
    if (context && SUCCEEDED(context->lpVtbl->GetDisplayName(context, nullptr, nullptr, &base)) && base) {
        WCHAR combined[INTERNET_MAX_URL_LENGTH];
        DWORD length = ARRAYSIZE(combined);
        PWSTR result = SUCCEEDED(UrlCombineW(base, url, combined, &length, flags & URL_MK_NO_CANONICALIZE
                                                                                ? URL_DONT_SIMPLIFY
                                                                                : 0))
                           ? UrlDuplicate(combined)
                           : nullptr;
        CoTaskMemFree(base);
        if (result)
            return result;
    }
    return UrlDuplicate(url);
}

DECLSPEC_EXPORT HRESULT WINAPI CreateURLMonikerEx(LPMONIKER context, LPCWSTR url, LPMONIKER *moniker, DWORD flags)
{
    if (!moniker)
        return E_INVALIDARG;
    *moniker = nullptr;
    if (!url)
        return E_INVALIDARG;
    if (flags & ~(DWORD)(URL_MK_UNIFORM | URL_MK_NO_CANONICALIZE))
        return E_INVALIDARG;
    URL_MONIKER *object = UrlAllocZero(sizeof(*object));
    if (!object)
        return E_OUTOFMEMORY;
    object->Interface.lpVtbl = &UrlMonikerVtbl;
    object->Refs = 1;
    object->Url = Resolve(context, url, flags);
    if (!object->Url) {
        UrlFree(object);
        return E_OUTOFMEMORY;
    }
    *moniker = &object->Interface;
    return S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI CreateURLMoniker(LPMONIKER context, LPCWSTR url, LPMONIKER *moniker)
{
    return CreateURLMonikerEx(context, url, moniker, URL_MK_LEGACY);
}

DECLSPEC_EXPORT HRESULT WINAPI CreateURLMonikerEx2(LPMONIKER context, IUri *uri, LPMONIKER *moniker, DWORD flags)
{
    if (!moniker)
        return E_INVALIDARG;
    *moniker = nullptr;
    if (!uri)
        return E_INVALIDARG;
    BSTR url = nullptr;
    HRESULT hr = uri->lpVtbl->GetDisplayUri(uri, &url);
    if (SUCCEEDED(hr))
        hr = CreateURLMonikerEx(context, url, moniker, flags);
    SysFreeString(url);
    return hr;
}

/* ------------------------------------------------------------------ bind contexts */

IBindStatusCallback *UrlBoundCallback(IBindCtx *context)
{
    IUnknown *holder = nullptr;
    IBindStatusCallback *callback = nullptr;
    if (SUCCEEDED(context->lpVtbl->GetObjectParam(context, (LPOLESTR)HOLDER, &holder)) && holder) {
        holder->lpVtbl->QueryInterface(holder, &IID_IBindStatusCallback, (void **)&callback);
        holder->lpVtbl->Release(holder);
    }
    return callback;
}

DECLSPEC_EXPORT HRESULT WINAPI RegisterBindStatusCallback(LPBC context, IBindStatusCallback *callback,
                                                         IBindStatusCallback **previous, DWORD reserved)
{
    if (previous)
        *previous = nullptr;
    if (!context || !callback)
        return E_INVALIDARG;
    IBindStatusCallback *old = UrlBoundCallback(context);
    if (old) {
        context->lpVtbl->RevokeObjectParam(context, (LPOLESTR)HOLDER);
        if (previous)
            *previous = old;
        else
            old->lpVtbl->Release(old);
    }
    return context->lpVtbl->RegisterObjectParam(context, (LPOLESTR)HOLDER, (IUnknown *)callback);
}

DECLSPEC_EXPORT HRESULT WINAPI RevokeBindStatusCallback(LPBC context, IBindStatusCallback *callback)
{
    if (!context || !callback)
        return E_INVALIDARG;
    IBindStatusCallback *current = UrlBoundCallback(context);
    HRESULT hr = S_OK;
    if (current == callback)
        hr = context->lpVtbl->RevokeObjectParam(context, (LPOLESTR)HOLDER);
    if (current)
        current->lpVtbl->Release(current);
    return SUCCEEDED(hr) ? S_OK : hr;
}

DECLSPEC_EXPORT HRESULT WINAPI CreateAsyncBindCtx(DWORD reserved, IBindStatusCallback *callback,
                                                 IEnumFORMATETC *formats, IBindCtx **context)
{
    if (!context)
        return E_INVALIDARG;
    *context = nullptr;
    if (!callback)
        return E_INVALIDARG;
    IBindCtx *bind = nullptr;
    HRESULT hr = CreateBindCtx(0, &bind);
    if (FAILED(hr))
        return hr;
    hr = RegisterBindStatusCallback(bind, callback, nullptr, 0);
    if (FAILED(hr)) {
        bind->lpVtbl->Release(bind);
        return hr;
    }
    *context = bind;
    return S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI CreateAsyncBindCtxEx(IBindCtx *original, DWORD options, IBindStatusCallback *callback,
                                                   IEnumFORMATETC *formats, IBindCtx **context, DWORD reserved)
{
    if (!context)
        return E_INVALIDARG;
    *context = nullptr;
    if (original) {
        original->lpVtbl->AddRef(original);
        *context = original;
    } else {
        HRESULT hr = CreateBindCtx(0, context);
        if (FAILED(hr))
            return hr;
    }
    if (callback) {
        HRESULT hr = RegisterBindStatusCallback(*context, callback, nullptr, 0);
        if (FAILED(hr)) {
            (*context)->lpVtbl->Release(*context);
            *context = nullptr;
            return hr;
        }
    }
    return S_OK;
}
