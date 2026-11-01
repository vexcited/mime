/*
 * events of objects: ConnectObject and the prefix of CreateObject and GetObject make a sink for the default source
 * interface of the object. An event calls the function of the script that is named by the prefix and the event.
 */
#include "wsh.h"

typedef struct SINK {
    IDispatch Dispatch;
    LONG Refs;
    IUnknown *Source;
    IConnectionPoint *Point;
    DWORD Cookie;
    ITypeInfo *Info;
    GUID Iid;
    PWSTR Prefix;
    SCRIPT_ENGINE *Engine;
    struct SINK *Next;
} SINK;

static SINK *g_sinks;

static HRESULT STDMETHODCALLTYPE sink_query_interface(IDispatch *This, REFIID riid, void **out)
{
    SINK *sink = (SINK *)This;
    if (!out)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch) || IsEqualIID(riid, &sink->Iid)) {
        *out = This;
        InterlockedIncrement(&sink->Refs);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE sink_add_ref(IDispatch *This)
{
    return (ULONG)InterlockedIncrement(&((SINK *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE sink_release(IDispatch *This)
{
    SINK *sink = (SINK *)This;
    LONG refs = InterlockedDecrement(&sink->Refs);
    if (!refs) {
        if (sink->Info)
            sink->Info->lpVtbl->Release(sink->Info);
        HeapFree(GetProcessHeap(), 0, sink->Prefix);
        HeapFree(GetProcessHeap(), 0, sink);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE sink_get_type_info_count(IDispatch *This, UINT *count)
{
    *count = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sink_get_type_info(IDispatch *This, UINT index, LCID lcid, ITypeInfo **info)
{
    *info = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE sink_get_ids_of_names(IDispatch *This, REFIID riid, LPOLESTR *names, UINT count, LCID lcid, DISPID *ids)
{
    return E_NOTIMPL;
}

/* the event is a call of the function Prefix_Event, when the script has one. */
static HRESULT STDMETHODCALLTYPE sink_invoke(IDispatch *This, DISPID id, REFIID riid, LCID lcid, WORD flags, DISPPARAMS *params,
                                             VARIANT *result, EXCEPINFO *excep, UINT *arg_err)
{
    SINK *sink = (SINK *)This;
    BSTR name = nullptr;
    UINT count = 0;
    if (!sink->Info || FAILED(sink->Info->lpVtbl->GetNames(sink->Info, id, &name, 1, &count)) || !count)
        return S_OK;
    SIZE_T size = wcslen(sink->Prefix) + SysStringLen(name) + 1;
    PWSTR function = HeapAlloc(GetProcessHeap(), 0, size * sizeof(WCHAR));
    if (!function) {
        SysFreeString(name);
        return E_OUTOFMEMORY;
    }
    wcscpy(function, sink->Prefix);
    wcscat(function, name);
    SysFreeString(name);
    IDispatch *script = nullptr;
    HRESULT hr = sink->Engine->Script->lpVtbl->GetScriptDispatch(sink->Engine->Script, nullptr, &script);
    if (SUCCEEDED(hr)) {
        DISPID function_id;
        PWSTR names[1] = {function};
        hr = script->lpVtbl->GetIDsOfNames(script, &IID_NULL, names, 1, LOCALE_USER_DEFAULT, &function_id);
        if (SUCCEEDED(hr)) {
            DISPPARAMS empty = {};
            hr = script->lpVtbl->Invoke(script, function_id, &IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, params ? params : &empty,
                                        result, excep, arg_err);
        } else
            hr = S_OK;
        script->lpVtbl->Release(script);
    }
    HeapFree(GetProcessHeap(), 0, function);
    return hr;
}

static const IDispatchVtbl g_sink_vtbl = {
    sink_query_interface, sink_add_ref, sink_release, sink_get_type_info_count, sink_get_type_info, sink_get_ids_of_names, sink_invoke,
};

/* the default source interface of the class of an object: the type information of the interface, from the coclass. */
static HRESULT source_interface(IDispatch *object, ITypeInfo **info, GUID *iid)
{
    IProvideClassInfo *provider;
    ITypeInfo *class_info;
    HRESULT hr = object->lpVtbl->QueryInterface(object, &IID_IProvideClassInfo, (void **)&provider);
    if (FAILED(hr))
        return hr;
    hr = provider->lpVtbl->GetClassInfo(provider, &class_info);
    provider->lpVtbl->Release(provider);
    if (FAILED(hr))
        return hr;
    TYPEATTR *attr;
    hr = class_info->lpVtbl->GetTypeAttr(class_info, &attr);
    if (SUCCEEDED(hr)) {
        hr = E_FAIL;
        if (attr->typekind == TKIND_COCLASS) {
            for (UINT i = 0; i < attr->cImplTypes; i++) {
                INT flags;
                HREFTYPE ref;
                if (FAILED(class_info->lpVtbl->GetImplTypeFlags(class_info, i, &flags)) ||
                    (flags & (IMPLTYPEFLAG_FDEFAULT | IMPLTYPEFLAG_FSOURCE)) != (IMPLTYPEFLAG_FDEFAULT | IMPLTYPEFLAG_FSOURCE))
                    continue;
                if (SUCCEEDED(class_info->lpVtbl->GetRefTypeOfImplType(class_info, i, &ref)) &&
                    SUCCEEDED(class_info->lpVtbl->GetRefTypeInfo(class_info, ref, info))) {
                    TYPEATTR *source;
                    if (SUCCEEDED((*info)->lpVtbl->GetTypeAttr(*info, &source))) {
                        *iid = source->guid;
                        (*info)->lpVtbl->ReleaseTypeAttr(*info, source);
                        hr = S_OK;
                    }
                    break;
                }
            }
        }
        class_info->lpVtbl->ReleaseTypeAttr(class_info, attr);
    }
    class_info->lpVtbl->Release(class_info);
    return hr;
}

static void drop(SINK *sink)
{
    if (sink->Point) {
        sink->Point->lpVtbl->Unadvise(sink->Point, sink->Cookie);
        sink->Point->lpVtbl->Release(sink->Point);
        sink->Point = nullptr;
    }
    if (sink->Source) {
        sink->Source->lpVtbl->Release(sink->Source);
        sink->Source = nullptr;
    }
}

HRESULT WshConnectObject(const GUID *clsid, IDispatch *object, PCWSTR prefix)
{
    ITypeInfo *info = nullptr;
    GUID iid = {};
    IConnectionPointContainer *container;
    IConnectionPoint *point;
    if (!prefix || !*prefix)
        return E_INVALIDARG;
    HRESULT hr = source_interface(object, &info, &iid);
    if (FAILED(hr))
        return hr;
    hr = object->lpVtbl->QueryInterface(object, &IID_IConnectionPointContainer, (void **)&container);
    if (SUCCEEDED(hr)) {
        hr = container->lpVtbl->FindConnectionPoint(container, &iid, &point);
        container->lpVtbl->Release(container);
    }
    if (FAILED(hr)) {
        info->lpVtbl->Release(info);
        return hr;
    }
    SINK *sink = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*sink));
    PWSTR copy = HeapAlloc(GetProcessHeap(), 0, (wcslen(prefix) + 1) * sizeof(WCHAR));
    if (!sink || !copy) {
        point->lpVtbl->Release(point);
        info->lpVtbl->Release(info);
        HeapFree(GetProcessHeap(), 0, sink);
        HeapFree(GetProcessHeap(), 0, copy);
        return E_OUTOFMEMORY;
    }
    wcscpy(copy, prefix);
    sink->Dispatch.lpVtbl = &g_sink_vtbl;
    sink->Refs = 1;
    sink->Info = info;
    sink->Iid = iid;
    sink->Prefix = copy;
    sink->Engine = g_host.Engine;
    sink->Point = point;
    hr = object->lpVtbl->QueryInterface(object, &IID_IUnknown, (void **)&sink->Source);
    if (SUCCEEDED(hr))
        hr = point->lpVtbl->Advise(point, (IUnknown *)&sink->Dispatch, &sink->Cookie);
    if (FAILED(hr)) {
        sink->Point = nullptr;
        point->lpVtbl->Release(point);
        drop(sink);
        sink_release(&sink->Dispatch);
        return hr;
    }
    sink->Next = g_sinks;
    g_sinks = sink;
    return S_OK;
}

/* the sinks of an object stop and go. */
HRESULT WshDisconnectObject(IDispatch *object)
{
    IUnknown *identity;
    if (FAILED(object->lpVtbl->QueryInterface(object, &IID_IUnknown, (void **)&identity)))
        return E_NOINTERFACE;
    SINK **link = &g_sinks;
    while (*link) {
        SINK *sink = *link;
        if (sink->Source == identity) {
            *link = sink->Next;
            drop(sink);
            sink_release(&sink->Dispatch);
            continue;
        }
        link = &sink->Next;
    }
    identity->lpVtbl->Release(identity);
    return S_OK;
}

void WshDisconnectAll(void)
{
    while (g_sinks) {
        SINK *sink = g_sinks;
        g_sinks = sink->Next;
        drop(sink);
        sink_release(&sink->Dispatch);
    }
}
