/*
 * connection point helpers: calling the sinks of a connection point with one invocation, and telling the property
 * sinks that a property changed.
 */
#include "shlwapi_private.h"

/* the invocation the helpers hand to every sink, packed as windows lays it out. */
#pragma pack(push, 1)
typedef struct SHINVOKEPARAMS SHINVOKEPARAMS;
struct SHINVOKEPARAMS {
    DWORD flags;
    DISPID dispid;
    const IID *riid;
    LCID lcid;
    WORD wflags;
    DISPPARAMS *params;
    VARIANT *result;
    EXCEPINFO *excep;
    UINT *arg_error;
    HRESULT(WINAPI *check)(IDispatch *sink, SHINVOKEPARAMS *invoke);
    WORD reserved;
    int *cancel;
    int **cancel_pointer;
};
#pragma pack(pop)

/* the flags: the check callback is used, and the call is a plain method call by id. */
#define SHINVOKE_CHECK 1
#define SHINVOKE_SIMPLE 2

SHL_INTERFACE(IPropertyNotifySink);
struct IPropertyNotifySinkVtbl {
    SHL_UNKNOWN(IPropertyNotifySink);
    HRESULT(STDMETHODCALLTYPE *OnChanged)(IPropertyNotifySink *This, DISPID id);
    HRESULT(STDMETHODCALLTYPE *OnRequestEdit)(IPropertyNotifySink *This, DISPID id);
};
MIME_DEFINE_GUID(IID_IPropertyNotifySink, 0x9bfbbc02, 0xeff1, 0x101a, 0x84, 0xed, 0x00, 0xaa, 0x00, 0x34, 0x1d, 0x07);

HRESULT WINAPI SHPackDispParamsV(DISPPARAMS *params, VARIANTARG *args, UINT count, va_list list);

/* calls back each sink of a point that answers one of the two interfaces, until the callback fails. */
static HRESULT enum_sinks(IConnectionPoint *point, const IID *first, const IID *second,
                          HRESULT (*callback)(IUnknown *sink, LONG_PTR context), LONG_PTR context)
{
    if (!point)
        return E_NOINTERFACE;
    IEnumConnections *connections;
    HRESULT hr = point->lpVtbl->EnumConnections(point, &connections);
    if (FAILED(hr))
        return hr;
    CONNECTDATA data = {};
    ULONG fetched = 0;
    while (connections->lpVtbl->Next(connections, 1, &data, &fetched) == S_OK) {
        IUnknown *sink = nullptr;
        BOOL found = first && SUCCEEDED(data.pUnk->lpVtbl->QueryInterface(data.pUnk, first, (void **)&sink)) && sink;
        if (!found) {
            sink = nullptr;
            found = second && SUCCEEDED(data.pUnk->lpVtbl->QueryInterface(data.pUnk, second, (void **)&sink)) && sink;
        }
        if (!found) {
            data.pUnk->lpVtbl->Release(data.pUnk);
            continue;
        }
        hr = callback(sink, context);
        sink->lpVtbl->Release(sink);
        data.pUnk->lpVtbl->Release(data.pUnk);
        if (FAILED(hr))
            break;
    }
    connections->lpVtbl->Release(connections);
    return S_OK;
}

static HRESULT on_changed(IUnknown *sink, LONG_PTR id)
{
    IPropertyNotifySink *notify = (IPropertyNotifySink *)sink;
    notify->lpVtbl->OnChanged(notify, (DISPID)id);
    return S_OK;
}

static HRESULT invoke_sink(IUnknown *sink, LONG_PTR context)
{
    SHINVOKEPARAMS *invoke = (SHINVOKEPARAMS *)context;
    IDispatch *dispatch = (IDispatch *)sink;
    if (invoke->check) {
        HRESULT hr = invoke->check(dispatch, invoke);
        if (hr != S_OK)
            return hr;
    }
    dispatch->lpVtbl->Invoke(dispatch, invoke->dispid, invoke->riid, invoke->lcid, invoke->wflags, invoke->params,
                             invoke->result, invoke->excep, invoke->arg_error);
    return S_OK;
}

/* the interface the sinks are asked for first is the one the point connects, then plain dispatch. */
static HRESULT invoke_sinks(IConnectionPoint *point, SHINVOKEPARAMS *invoke)
{
    DISPPARAMS none = {};
    if (!invoke->params)
        invoke->params = &none;
    if (!(invoke->flags & SHINVOKE_CHECK))
        invoke->check = nullptr;
    if (invoke->flags & SHINVOKE_SIMPLE) {
        invoke->lcid = 0;
        invoke->riid = &GUID_NULL;
        invoke->wflags = DISPATCH_METHOD;
        invoke->result = nullptr;
        invoke->excep = nullptr;
        invoke->arg_error = nullptr;
    }
    IID iid;
    BOOL known = point->lpVtbl->GetConnectionInterface(point, &iid) == S_OK;
    HRESULT hr = enum_sinks(point, known ? &iid : nullptr, &IID_IDispatch, invoke_sink, (LONG_PTR)invoke);
    if (invoke->params == &none)
        invoke->params = nullptr;
    return hr;
}

static HRESULT WINAPI cancel_check(IDispatch *sink, SHINVOKEPARAMS *invoke)
{
    if (invoke->cancel && *invoke->cancel)
        return E_FAIL;
    if (invoke->cancel_pointer)
        return *invoke->cancel_pointer ? E_FAIL : S_OK;
    return S_OK;
}

static HRESULT find_point(IUnknown *unknown, REFIID riid, IConnectionPoint **point)
{
    *point = nullptr;
    if (!unknown)
        return E_NOINTERFACE;
    IConnectionPointContainer *container;
    HRESULT hr = unknown->lpVtbl->QueryInterface(unknown, &IID_IConnectionPointContainer, (void **)&container);
    if (SUCCEEDED(hr)) {
        hr = container->lpVtbl->FindConnectionPoint(container, riid, point);
        container->lpVtbl->Release(container);
    }
    return hr;
}

HRESULT WINAPI IConnectionPoint_SimpleInvoke(IConnectionPoint *point, DISPID dispid, DISPPARAMS *params);
HRESULT WINAPI IConnectionPoint_SimpleInvoke(IConnectionPoint *point, DISPID dispid, DISPPARAMS *params)
{
    SHINVOKEPARAMS invoke = {.flags = SHINVOKE_SIMPLE, .dispid = dispid, .params = params};
    return invoke_sinks(point, &invoke);
}

HRESULT WINAPI IConnectionPoint_InvokeWithCancel(IConnectionPoint *point, DISPID dispid, DISPPARAMS *params,
                                                 int *cancel, int **cancel_pointer);
HRESULT WINAPI IConnectionPoint_InvokeWithCancel(IConnectionPoint *point, DISPID dispid, DISPPARAMS *params,
                                                 int *cancel, int **cancel_pointer)
{
    SHINVOKEPARAMS invoke = {.flags = SHINVOKE_CHECK | SHINVOKE_SIMPLE,
                             .dispid = dispid,
                             .params = params,
                             .check = cancel_check,
                             .cancel = cancel,
                             .cancel_pointer = cancel_pointer};
    return invoke_sinks(point, &invoke);
}

HRESULT WINAPI IConnectionPoint_OnChanged(IConnectionPoint *point, DISPID dispid);
HRESULT WINAPI IConnectionPoint_OnChanged(IConnectionPoint *point, DISPID dispid)
{
    return enum_sinks(point, &IID_IPropertyNotifySink, nullptr, on_changed, dispid);
}

HRESULT WINAPI IUnknown_CPContainerOnChanged(IUnknown *container, DISPID dispid);
HRESULT WINAPI IUnknown_CPContainerOnChanged(IUnknown *container, DISPID dispid)
{
    IConnectionPoint *point;
    HRESULT hr = find_point(container, &IID_IPropertyNotifySink, &point);
    if (SUCCEEDED(hr)) {
        hr = enum_sinks(point, &IID_IPropertyNotifySink, nullptr, on_changed, dispid);
        point->lpVtbl->Release(point);
    }
    return hr;
}

HRESULT WINAPI IUnknown_CPContainerInvokeIndirect(IUnknown *container, REFIID riid, SHINVOKEPARAMS *invoke);
HRESULT WINAPI IUnknown_CPContainerInvokeIndirect(IUnknown *container, REFIID riid, SHINVOKEPARAMS *invoke)
{
    IConnectionPoint *point;
    HRESULT hr = find_point(container, riid, &point);
    if (FAILED(hr))
        return hr;
    hr = invoke_sinks(point, invoke);
    point->lpVtbl->Release(point);
    return hr;
}

HRESULT WINAPIV IUnknown_CPContainerInvokeParam(IUnknown *container, REFIID riid, DISPID dispid, VARIANTARG *args,
                                                UINT count, ...);
HRESULT WINAPIV IUnknown_CPContainerInvokeParam(IUnknown *container, REFIID riid, DISPID dispid, VARIANTARG *args,
                                                UINT count, ...)
{
    IConnectionPoint *point;
    HRESULT hr = find_point(container, riid, &point);
    if (FAILED(hr))
        return hr;
    va_list list;
    va_start(list, count);
    DISPPARAMS params = {};
    hr = SHPackDispParamsV(&params, args, count, list);
    va_end(list);
    if (SUCCEEDED(hr))
        hr = IConnectionPoint_SimpleInvoke(point, dispid, &params);
    point->lpVtbl->Release(point);
    return hr;
}
