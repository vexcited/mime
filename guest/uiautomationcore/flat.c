/*
 * the flat api: handles of nodes, patterns and text ranges, and the functions on them.
 */
#include "uia.h"

typedef struct ID_GUID {
    int id;
    GUID guid;
} ID_GUID;

static const ID_GUID g_property_guids[] = {
#include "propguids.inc"
};

static const ID_GUID g_pattern_guids[] = {
#include "patguids.inc"
};

static NODE *as_node(HUIANODE h)
{
    NODE *n = (NODE *)h;
    return n && n->magic == UIA_HANDLE_MAGIC ? n : nullptr;
}

static PATTERN *as_pattern(void *h)
{
    PATTERN *p = h;
    return p && p->magic == UIA_HANDLE_MAGIC ? p : nullptr;
}

BOOL WINAPI UiaNodeRelease(HUIANODE node)
{
    NODE *n = as_node(node);
    if (!n)
        return FALSE;
    NodeRelease(n);
    return TRUE;
}

BOOL WINAPI UiaPatternRelease(HUIAPATTERNOBJECT pattern)
{
    PATTERN *p = as_pattern(pattern);
    if (!p)
        return FALSE;
    PatternRelease(p);
    return TRUE;
}

BOOL WINAPI UiaTextRangeRelease(HUIATEXTRANGE range)
{
    PATTERN *p = as_pattern(range);
    if (!p)
        return FALSE;
    PatternRelease(p);
    return TRUE;
}

HRESULT WINAPI UiaNodeFromHandle(HWND hwnd, HUIANODE *node)
{
    if (!node)
        return E_INVALIDARG;
    *node = nullptr;
    if (!IsWindow(hwnd))
        return UIA_E_ELEMENTNOTAVAILABLE;
    return UiaClientNodeFromWindow(hwnd, (NODE **)node);
}

HRESULT WINAPI UiaGetRootNode(HUIANODE *node)
{
    if (!node)
        return E_INVALIDARG;
    *node = nullptr;
    return UiaClientRootNode((NODE **)node);
}

HRESULT WINAPI UiaNodeFromProvider(IRawElementProviderSimple *provider, HUIANODE *node)
{
    if (!node)
        return E_INVALIDARG;
    *node = nullptr;
    if (!provider)
        return E_INVALIDARG;
    return NodeFromProvider(provider, (NODE **)node);
}

HRESULT WINAPI UiaGetPropertyValue(HUIANODE node, PROPERTYID id, VARIANT *value)
{
    if (!value)
        return E_INVALIDARG;
    VariantInit(value);
    NODE *n = as_node(node);
    if (!n)
        return E_INVALIDARG;
    return NodeGetProperty(n, id, TRUE, value);
}

HRESULT WINAPI UiaGetRuntimeId(HUIANODE node, SAFEARRAY **runtime_id)
{
    if (!runtime_id)
        return E_INVALIDARG;
    *runtime_id = nullptr;
    NODE *n = as_node(node);
    if (!n)
        return E_INVALIDARG;
    int *ids;
    int count;
    HRESULT hr = NodeRuntimeId(n, &ids, &count);
    if (FAILED(hr))
        return hr;
    hr = UiaIntArrayToSafeArray(ids, count, runtime_id);
    UiaFree(ids);
    return hr;
}

HRESULT WINAPI UiaGetPatternProvider(HUIANODE node, PATTERNID id, HUIAPATTERNOBJECT *pattern)
{
    if (!pattern)
        return E_INVALIDARG;
    *pattern = nullptr;
    NODE *n = as_node(node);
    if (!n || !UiaPatternName(id))
        return E_INVALIDARG;
    return NodeGetPattern(n, id, (PATTERN **)pattern);
}

HRESULT WINAPI UiaSetFocus(HUIANODE node)
{
    NODE *n = as_node(node);
    if (!n)
        return E_INVALIDARG;
    return n->ops->set_focus(n);
}

BOOL WINAPI UiaHasServerSideProvider(HWND hwnd)
{
    if (!hwnd)
        return FALSE;
    return ProxyHasServerSideProvider(hwnd);
}

HRESULT WINAPI UiaHUiaNodeFromVariant(VARIANT *value, HUIANODE *node)
{
    if (!node)
        return E_INVALIDARG;
    *node = nullptr;
    if (!value)
        return E_INVALIDARG;
    NODE *n = nullptr;
    if (value->vt == VT_I8) {
        n = as_node((HUIANODE)(ULONG_PTR)value->llVal);
        if (n)
            NodeAddRef(n);
    } else if (value->vt == VT_UNKNOWN && value->punkVal) {
        value->punkVal->lpVtbl->QueryInterface(value->punkVal, &IID_IMimeUiaNode, (void **)&n);
    }
    if (!n)
        return E_INVALIDARG;
    *node = (HUIANODE)n;
    return S_OK;
}

static HRESULT handle_from_variant(VARIANT *value, void **handle)
{
    if (!handle)
        return E_INVALIDARG;
    *handle = nullptr;
    if (!value)
        return E_INVALIDARG;
    PATTERN *p = nullptr;
    if (value->vt == VT_I8) {
        p = as_pattern((void *)(ULONG_PTR)value->llVal);
        if (p)
            PatternAddRef(p);
    } else if (value->vt == VT_UNKNOWN && value->punkVal) {
        value->punkVal->lpVtbl->QueryInterface(value->punkVal, &IID_IMimeUiaPattern, (void **)&p);
    }
    if (!p)
        return E_INVALIDARG;
    *handle = p;
    return S_OK;
}

HRESULT WINAPI UiaHPatternObjectFromVariant(VARIANT *value, HUIAPATTERNOBJECT *pattern)
{
    return handle_from_variant(value, (void **)pattern);
}

HRESULT WINAPI UiaHTextRangeFromVariant(VARIANT *value, HUIATEXTRANGE *range)
{
    return handle_from_variant(value, (void **)range);
}

/* ------------------------------------------------------------------ cached data */

static HRESULT data_of_node(NODE *node, UiaCacheRequest *request, SAFEARRAY **data, BSTR *structure)
{
    REQUEST req;
    HRESULT hr = RequestFromStruct(request, &req);
    if (FAILED(hr))
        return hr;
    ELEMENT *e = nullptr;
    hr = ElementCreate(node, &req, &e);
    if (SUCCEEDED(hr)) {
        hr = ElementTreeToData(e, &req, data, structure);
        ElementRelease(e);
    }
    RequestClear(&req);
    return hr;
}

HRESULT WINAPI UiaGetUpdatedCache(HUIANODE node, UiaCacheRequest *request, NormalizeState state, UiaCondition *normalize,
                                  SAFEARRAY **data, BSTR *structure)
{
    if (!data || !structure)
        return E_INVALIDARG;
    *data = nullptr;
    *structure = nullptr;
    NODE *n = as_node(node);
    if (!n || !request)
        return E_INVALIDARG;
    if ((unsigned)state > NormalizeState_Custom)
        return E_INVALIDARG;
    NODE *target = n;
    NodeAddRef(target);
    if (state != NormalizeState_None) {
        COND *cond = nullptr;
        HRESULT hr = S_OK;
        if (state == NormalizeState_Custom) {
            hr = normalize ? CondFromStruct(normalize, &cond) : E_INVALIDARG;
        } else if (request->pViewCondition) {
            hr = CondFromStruct(request->pViewCondition, &cond);
        }
        if (FAILED(hr)) {
            NodeRelease(target);
            return hr;
        }
        if (cond) {
            BOOL match;
            hr = CondMatch(cond, target, &match);
            if (SUCCEEDED(hr) && !match) {
                NODE *up = nullptr;
                hr = ViewParent(target, cond, &up);
                NodeRelease(target);
                target = up;
            }
            CondRelease(cond);
            if (FAILED(hr)) {
                NodeRelease(target);
                return hr;
            }
        }
        if (!target)
            return S_OK;
    }
    HRESULT hr = data_of_node(target, request, data, structure);
    NodeRelease(target);
    return hr;
}

HRESULT WINAPI UiaNavigate(HUIANODE node, NavigateDirection direction, UiaCondition *condition, UiaCacheRequest *request,
                           SAFEARRAY **data, BSTR *structure)
{
    if (!data || !structure)
        return E_INVALIDARG;
    *data = nullptr;
    *structure = nullptr;
    NODE *n = as_node(node);
    if (!n || !request || (unsigned)direction > NavigateDirection_LastChild)
        return E_INVALIDARG;
    COND *view = nullptr;
    HRESULT hr = condition ? CondFromStruct(condition, &view) : S_OK;
    if (FAILED(hr))
        return hr;
    NODE *found = nullptr;
    hr = view ? ViewStep(n, view, direction, &found) : NodeNavigate(n, direction, &found);
    CondRelease(view);
    if (FAILED(hr) || !found)
        return hr;
    hr = data_of_node(found, request, data, structure);
    NodeRelease(found);
    return hr;
}

HRESULT WINAPI UiaFind(HUIANODE node, UiaFindParams *params, UiaCacheRequest *request, SAFEARRAY **data,
                       SAFEARRAY **offsets, SAFEARRAY **structures)
{
    if (!data || !offsets || !structures)
        return E_INVALIDARG;
    *data = *offsets = *structures = nullptr;
    NODE *n = as_node(node);
    if (!n || !params || !request)
        return E_INVALIDARG;
    COND *cond = nullptr;
    HRESULT hr = CondFromStruct(params->pFindCondition, &cond);
    if (FAILED(hr))
        return hr;
    NODE **list;
    int count;
    int scope = TreeScope_Element | TreeScope_Descendants;
    if (params->MaxDepth == 1)
        scope = TreeScope_Element | TreeScope_Children;
    if (params->ExcludeRoot)
        scope &= ~TreeScope_Element;
    hr = NodeFind(n, scope, cond, params->FindFirst, 0, nullptr, &list, &count);
    CondRelease(cond);
    if (FAILED(hr))
        return hr;
    SAFEARRAY *all = nullptr, *texts = nullptr, *positions = nullptr;
    int *starts = count ? UiaAlloc((SIZE_T)count * sizeof(int)) : nullptr;
    BSTR *strings = count ? UiaAlloc((SIZE_T)count * sizeof(BSTR)) : nullptr;
    SAFEARRAY **parts = count ? UiaAlloc((SIZE_T)count * sizeof(SAFEARRAY *)) : nullptr;
    int total = 0;
    for (int i = 0; SUCCEEDED(hr) && i < count; i++) {
        hr = data_of_node(list[i], request, &parts[i], &strings[i]);
        if (SUCCEEDED(hr)) {
            LONG lower, upper;
            SafeArrayGetLBound(parts[i], 1, &lower);
            SafeArrayGetUBound(parts[i], 1, &upper);
            starts[i] = total;
            total += upper - lower + 1;
        }
    }
    if (SUCCEEDED(hr)) {
        all = SafeArrayCreateVector(VT_VARIANT, 0, (ULONG)total);
        texts = SafeArrayCreateVector(VT_BSTR, 0, (ULONG)count);
        VARIANT *items;
        BSTR *tx;
        if (all && texts && (!total || SUCCEEDED(SafeArrayAccessData(all, (void **)&items))) &&
            (!count || SUCCEEDED(SafeArrayAccessData(texts, (void **)&tx)))) {
            for (int i = 0; i < count; i++) {
                VARIANT *part;
                LONG lower, upper;
                SafeArrayGetLBound(parts[i], 1, &lower);
                SafeArrayGetUBound(parts[i], 1, &upper);
                SafeArrayAccessData(parts[i], (void **)&part);
                for (LONG k = 0; k <= upper - lower; k++)
                    VariantCopy(&items[starts[i] + k], &part[k]);
                SafeArrayUnaccessData(parts[i]);
                tx[i] = strings[i];
                strings[i] = nullptr;
            }
            if (total)
                SafeArrayUnaccessData(all);
            if (count)
                SafeArrayUnaccessData(texts);
            UiaIntArrayToSafeArray(starts, count, &positions);
            *data = all;
            *offsets = positions;
            *structures = texts;
        } else {
            hr = E_OUTOFMEMORY;
        }
    }
    for (int i = 0; i < count; i++) {
        if (parts && parts[i])
            SafeArrayDestroy(parts[i]);
        if (strings && strings[i])
            SysFreeString(strings[i]);
    }
    UiaFree(parts);
    UiaFree(strings);
    UiaFree(starts);
    NodeListFree(list, count);
    return hr;
}

HRESULT WINAPI UiaNodeFromPoint(double x, double y, UiaCacheRequest *request, SAFEARRAY **data, BSTR *structure)
{
    if (!data || !structure)
        return E_INVALIDARG;
    *data = nullptr;
    *structure = nullptr;
    if (!request)
        return E_INVALIDARG;
    POINT pt = {(LONG)x, (LONG)y};
    NODE *node = nullptr;
    HRESULT hr = UiaClientNodeFromPoint(pt, &node);
    if (FAILED(hr))
        return hr;
    hr = data_of_node(node, request, data, structure);
    NodeRelease(node);
    return hr;
}

HRESULT WINAPI UiaNodeFromFocus(UiaCacheRequest *request, SAFEARRAY **data, BSTR *structure)
{
    if (!data || !structure)
        return E_INVALIDARG;
    *data = nullptr;
    *structure = nullptr;
    if (!request)
        return E_INVALIDARG;
    NODE *node = nullptr;
    HRESULT hr = UiaClientNodeFromFocus(&node);
    if (FAILED(hr))
        return hr;
    hr = data_of_node(node, request, data, structure);
    NodeRelease(node);
    return hr;
}

/* ------------------------------------------------------------------ identifiers */

int WINAPI UiaLookupId(AutomationIdentifierType type, const GUID *guid)
{
    if (!guid)
        return 0;
    if (type == AutomationIdentifierType_Property) {
        for (int i = 0; i < (int)RTL_NUMBER_OF(g_property_guids); i++)
            if (IsEqualGUID(&g_property_guids[i].guid, guid))
                return g_property_guids[i].id;
    } else if (type == AutomationIdentifierType_Pattern) {
        for (int i = 0; i < (int)RTL_NUMBER_OF(g_pattern_guids); i++)
            if (IsEqualGUID(&g_pattern_guids[i].guid, guid))
                return g_pattern_guids[i].id;
    }
    return 0;
}

/* the description of the last error of the calling thread, as providers and clients report them. */
static volatile LONG g_error_slot = -1;

static DWORD error_slot(void)
{
    if (g_error_slot < 0) {
        DWORD slot = TlsAlloc();
        if (InterlockedCompareExchange(&g_error_slot, (LONG)slot, -1) != -1)
            TlsFree(slot);
    }
    return (DWORD)g_error_slot;
}

void UiaSetErrorDescription(const WCHAR *text)
{
    DWORD slot = error_slot();
    UiaFree(TlsGetValue(slot));
    TlsSetValue(slot, text ? UiaDupString(text) : nullptr);
}

BOOL WINAPI UiaGetErrorDescription(BSTR *description)
{
    if (!description)
        return FALSE;
    *description = nullptr;
    const WCHAR *error = TlsGetValue(error_slot());
    if (!error)
        return FALSE;
    *description = SysAllocString(error);
    return *description != nullptr;
}

UiaProviderCallback *g_provider_callback;

void WINAPI UiaRegisterProviderCallback(UiaProviderCallback *callback)
{
    g_provider_callback = callback;
}

HRESULT WINAPI UpdateErrorLoggingCallback(void *callback)
{
    return S_OK;
}

HRESULT WINAPI IgnoreLeaksInCurrentlyTrackedMemory(void)
{
    return S_OK;
}

HRESULT WINAPI PostTestCheckForLeaks(void *a, void *b)
{
    return S_OK;
}

HRESULT WINAPI StartIgnoringLeaks(void *a)
{
    return S_OK;
}

HRESULT WINAPI StopIgnoringLeaks(void)
{
    return S_OK;
}

BOOL WINAPI IsIgnoringLeaks(void)
{
    return FALSE;
}

HRESULT WINAPI InitializeChannelBasedConnectionForProviderProxy(void *a, DWORD b, DWORD c)
{
    return E_NOTIMPL;
}

/* ------------------------------------------------------------------ events of the flat api */

typedef struct FLATSINK {
    IDispatch iface;
    LONG refs;
    UiaEventCallback *callback;
    REQUEST request;
} FLATSINK;

struct UiaEventObject {
    DWORD magic;
    DWORD cookie;
    FLATSINK *sink;
};

static HRESULT STDMETHODCALLTYPE fs_query(IDispatch *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch)) {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE fs_add_ref(IDispatch *iface)
{
    return (ULONG)InterlockedIncrement(&((FLATSINK *)iface)->refs);
}

static ULONG STDMETHODCALLTYPE fs_release(IDispatch *iface)
{
    FLATSINK *s = (FLATSINK *)iface;
    LONG refs = InterlockedDecrement(&s->refs);
    if (!refs) {
        RequestClear(&s->request);
        UiaFree(s);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE fs_count(IDispatch *iface, UINT *count)
{
    *count = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fs_info(IDispatch *iface, UINT index, LCID lcid, ITypeInfo **info)
{
    *info = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE fs_names(IDispatch *iface, REFIID riid, LPOLESTR *names, UINT count, LCID lcid, DISPID *ids)
{
    return DISP_E_UNKNOWNNAME;
}

static VARIANT *carg(DISPPARAMS *p, ULONG i)
{
    return i < p->cArgs ? &p->rgvarg[p->cArgs - 1 - i] : nullptr;
}

static HRESULT STDMETHODCALLTYPE fs_invoke(IDispatch *iface, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                           DISPPARAMS *params, VARIANT *result, EXCEPINFO *ex, UINT *err)
{
    FLATSINK *s = (FLATSINK *)iface;
    if (id != SINK_DISPID_EVENT || !params || params->cArgs < 3)
        return DISP_E_MEMBERNOTFOUND;
    int type = carg(params, 0)->lVal;
    VARIANT *source = carg(params, 1);
    int event_id = carg(params, 2)->lVal;
    NODE *node = nullptr;
    if (source->vt != VT_DISPATCH || FAILED(RemoteNodeFromDispatch(source->pdispVal, &node)))
        return S_OK;
    ELEMENT *e = nullptr;
    HRESULT hr = ElementCreate(node, &s->request, &e);
    NodeRelease(node);
    if (FAILED(hr))
        return S_OK;
    SAFEARRAY *data = nullptr;
    BSTR structure = nullptr;
    ElementTreeToData(e, &s->request, &data, &structure);
    union {
        UiaEventArgs simple;
        UiaPropertyChangedEventArgs property;
        UiaStructureChangedEventArgs structure;
        UiaAsyncContentLoadedEventArgs loaded;
        UiaTextEditTextChangedEventArgs edit;
    } args = {};
    args.simple.Type = type;
    args.simple.EventId = event_id;
    VARIANT *a0 = carg(params, 3), *a1 = carg(params, 4), *a2 = carg(params, 5);
    int *rid = nullptr;
    if (type == EventArgsType_PropertyChanged) {
        args.property.PropertyId = a0 ? a0->lVal : 0;
        if (a1)
            args.property.OldValue = *a1;
        if (a2)
            args.property.NewValue = *a2;
    } else if (type == EventArgsType_StructureChanged) {
        args.structure.StructureChangeType = a0 ? a0->lVal : 0;
        if (a1 && (a1->vt & VT_ARRAY))
            UiaSafeArrayToIntArray(a1->parray, &rid, &args.structure.cRuntimeIdLen);
        args.structure.pRuntimeId = rid;
    } else if (type == EventArgsType_AsyncContentLoaded) {
        args.loaded.AsyncContentLoadedState = a0 ? a0->lVal : 0;
        args.loaded.percentComplete = a1 ? a1->dblVal : 0;
    } else if (type == EventArgsType_TextEditTextChanged) {
        args.edit.TextEditChangeType = a0 ? a0->lVal : 0;
        args.edit.pTextChange = a1 && (a1->vt & VT_ARRAY) ? a1->parray : nullptr;
    }
    s->callback(&args.simple, data, structure);
    UiaFree(rid);
    if (data)
        SafeArrayDestroy(data);
    SysFreeString(structure);
    ElementRelease(e);
    return S_OK;
}

static const IDispatchVtbl g_flat_sink_vtbl = {fs_query, fs_add_ref, fs_release, fs_count, fs_info, fs_names, fs_invoke};

HRESULT WINAPI UiaAddEvent(HUIANODE node, EVENTID event_id, UiaEventCallback *callback, TreeScope scope,
                           PROPERTYID *props, int nprops, UiaCacheRequest *request, HUIAEVENT *handle)
{
    if (!handle)
        return E_INVALIDARG;
    *handle = nullptr;
    NODE *n = as_node(node);
    if (!n || !callback || !request || (nprops && !props) || nprops < 0 || scope == TreeScope_None ||
        (scope & ~(TreeScope_Element | TreeScope_Children | TreeScope_Descendants)))
        return E_INVALIDARG;
    if (event_id == UIA_AutomationPropertyChangedEventId)
        for (int i = 0; i < nprops; i++)
            if (!UiaPropertyInfo(props[i]))
                return E_INVALIDARG;
    FLATSINK *sink = UiaAlloc(sizeof(*sink));
    struct UiaEventObject *object = UiaAlloc(sizeof(*object));
    if (!sink || !object) {
        UiaFree(sink);
        UiaFree(object);
        return E_OUTOFMEMORY;
    }
    sink->iface.lpVtbl = &g_flat_sink_vtbl;
    sink->refs = 1;
    sink->callback = callback;
    HRESULT hr = RequestFromStruct(request, &sink->request);
    HWND window = NodeOwnerWindow(n);
    int *rid = nullptr, rid_len = 0;
    if (SUCCEEDED(hr) && window != GetDesktopWindow())
        hr = NodeRuntimeId(n, &rid, &rid_len);
    if (SUCCEEDED(hr))
        hr = UiaRegistryAdd(EK_AUTOMATION, event_id, scope, window == GetDesktopWindow() ? window : nullptr,
                            props, nprops, rid, rid_len > 16 ? 16 : rid_len, (IUnknown *)&sink->iface, &object->cookie);
    UiaFree(rid);
    if (FAILED(hr)) {
        fs_release(&sink->iface);
        UiaFree(object);
        return hr;
    }
    object->magic = UIA_HANDLE_MAGIC;
    object->sink = sink;
    *handle = object;
    return S_OK;
}

HRESULT WINAPI UiaRemoveEvent(HUIAEVENT handle)
{
    struct UiaEventObject *object = (struct UiaEventObject *)handle;
    if (!object || object->magic != UIA_HANDLE_MAGIC)
        return E_INVALIDARG;
    UiaRegistryRemove(object->cookie);
    object->magic = 0;
    fs_release(&object->sink->iface);
    UiaFree(object);
    return S_OK;
}

HRESULT WINAPI UiaEventAddWindow(HUIAEVENT handle, HWND hwnd)
{
    struct UiaEventObject *object = (struct UiaEventObject *)handle;
    if (!object || object->magic != UIA_HANDLE_MAGIC || !hwnd)
        return E_INVALIDARG;
    UiaRegistryAddWindow(object->cookie, hwnd);
    return S_OK;
}

HRESULT WINAPI UiaEventRemoveWindow(HUIAEVENT handle, HWND hwnd)
{
    struct UiaEventObject *object = (struct UiaEventObject *)handle;
    if (!object || object->magic != UIA_HANDLE_MAGIC || !hwnd)
        return E_INVALIDARG;
    UiaRegistryRemoveWindow(object->cookie, hwnd);
    return S_OK;
}

/* ------------------------------------------------------------------ patterns by handle */

#define PATTERN_FN(NAME, METHOD, ...)                                                                                  \
    HRESULT WINAPI NAME(HUIAPATTERNOBJECT h, ##__VA_ARGS__)

static HRESULT call_pattern(void *h, int method, VARIANT *args, ULONG count, VARIANT *ret)
{
    PATTERN *p = as_pattern(h);
    if (!p)
        return E_INVALIDARG;
    return PatternCall(p, method, args, count, ret);
}

static VARIANT vi(int v)
{
    VARIANT r;
    VariantInit(&r);
    r.vt = VT_I4;
    r.lVal = v;
    return r;
}

static VARIANT vd(double v)
{
    VARIANT r;
    VariantInit(&r);
    r.vt = VT_R8;
    r.dblVal = v;
    return r;
}

static VARIANT vs(const WCHAR *text)
{
    VARIANT r;
    VariantInit(&r);
    r.vt = VT_BSTR;
    r.bstrVal = SysAllocString(text ? text : L"");
    return r;
}

static HRESULT call0(void *h, int method)
{
    return call_pattern(h, method, nullptr, 0, nullptr);
}

static HRESULT call_with(void *h, int method, VARIANT *args, ULONG count)
{
    HRESULT hr = call_pattern(h, method, args, count, nullptr);
    for (ULONG i = 0; i < count; i++)
        VariantClear(&args[i]);
    return hr;
}

static HRESULT node_out(VARIANT *ret, HUIANODE *node)
{
    *node = nullptr;
    if (ret->vt == VT_UNKNOWN && ret->punkVal)
        ret->punkVal->lpVtbl->QueryInterface(ret->punkVal, &IID_IMimeUiaNode, (void **)node);
    VariantClear(ret);
    return S_OK;
}

static HRESULT range_out(VARIANT *ret, HUIATEXTRANGE *range)
{
    *range = nullptr;
    if (ret->vt == VT_UNKNOWN && ret->punkVal)
        ret->punkVal->lpVtbl->QueryInterface(ret->punkVal, &IID_IMimeUiaPattern, (void **)range);
    VariantClear(ret);
    return S_OK;
}

static HRESULT ranges_out(VARIANT *ret, SAFEARRAY **array)
{
    *array = nullptr;
    if (ret->vt == (VT_ARRAY | VT_UNKNOWN)) {
        *array = ret->parray;
        ret->vt = VT_EMPTY;
    }
    VariantClear(ret);
    return S_OK;
}

HRESULT WINAPI DockPattern_SetDockPosition(HUIAPATTERNOBJECT h, DockPosition position)
{
    VARIANT a = vi(position);
    return call_with(h, PM_Dock_SetDockPosition, &a, 1);
}
HRESULT WINAPI ExpandCollapsePattern_Collapse(HUIAPATTERNOBJECT h) { return call0(h, PM_ExpandCollapse_Collapse); }
HRESULT WINAPI ExpandCollapsePattern_Expand(HUIAPATTERNOBJECT h) { return call0(h, PM_ExpandCollapse_Expand); }
HRESULT WINAPI GridPattern_GetItem(HUIAPATTERNOBJECT h, int row, int column, HUIANODE *item)
{
    if (!item)
        return E_INVALIDARG;
    VARIANT a[2] = {vi(row), vi(column)}, ret;
    HRESULT hr = call_pattern(h, PM_Grid_GetItem, a, 2, &ret);
    return SUCCEEDED(hr) ? node_out(&ret, item) : hr;
}
HRESULT WINAPI InvokePattern_Invoke(HUIAPATTERNOBJECT h) { return call0(h, PM_Invoke_Invoke); }
HRESULT WINAPI ItemContainerPattern_FindItemByProperty(HUIAPATTERNOBJECT h, HUIANODE start, PROPERTYID id, VARIANT value,
                                                       HUIANODE *found)
{
    if (!found)
        return E_INVALIDARG;
    VARIANT a[3], ret;
    VariantInit(&a[0]);
    NODE *s = as_node(start);
    if (s) {
        a[0].vt = VT_UNKNOWN;
        a[0].punkVal = (IUnknown *)s;
    }
    a[1] = vi(id);
    a[2] = value;
    HRESULT hr = call_pattern(h, PM_ItemContainer_FindItemByProperty, a, 3, &ret);
    return SUCCEEDED(hr) ? node_out(&ret, found) : hr;
}
HRESULT WINAPI LegacyIAccessiblePattern_DoDefaultAction(HUIAPATTERNOBJECT h) { return call0(h, PM_Legacy_DoDefaultAction); }
HRESULT WINAPI LegacyIAccessiblePattern_GetIAccessible(HUIAPATTERNOBJECT h, IAccessible **acc)
{
    if (!acc)
        return E_INVALIDARG;
    *acc = nullptr;
    VARIANT ret;
    HRESULT hr = call_pattern(h, PM_Legacy_GetIAccessible, nullptr, 0, &ret);
    if (SUCCEEDED(hr) && ret.vt == VT_UNKNOWN) {
        *acc = (IAccessible *)ret.punkVal;
        ret.vt = VT_EMPTY;
    }
    VariantClear(&ret);
    return hr;
}
HRESULT WINAPI LegacyIAccessiblePattern_Select(HUIAPATTERNOBJECT h, LONG flags)
{
    VARIANT a = vi(flags);
    return call_with(h, PM_Legacy_Select, &a, 1);
}
HRESULT WINAPI LegacyIAccessiblePattern_SetValue(HUIAPATTERNOBJECT h, LPCWSTR value)
{
    VARIANT a = vs(value);
    return call_with(h, PM_Legacy_SetValue, &a, 1);
}
HRESULT WINAPI MultipleViewPattern_GetViewName(HUIAPATTERNOBJECT h, int view, BSTR *name)
{
    if (!name)
        return E_INVALIDARG;
    *name = nullptr;
    VARIANT a = vi(view), ret;
    HRESULT hr = call_pattern(h, PM_MultipleView_GetViewName, &a, 1, &ret);
    if (SUCCEEDED(hr) && ret.vt == VT_BSTR) {
        *name = ret.bstrVal;
        ret.vt = VT_EMPTY;
    }
    VariantClear(&ret);
    return hr;
}
HRESULT WINAPI MultipleViewPattern_SetCurrentView(HUIAPATTERNOBJECT h, int view)
{
    VARIANT a = vi(view);
    return call_with(h, PM_MultipleView_SetCurrentView, &a, 1);
}
HRESULT WINAPI RangeValuePattern_SetValue(HUIAPATTERNOBJECT h, double value)
{
    VARIANT a = vd(value);
    return call_with(h, PM_RangeValue_SetValue, &a, 1);
}
HRESULT WINAPI ScrollItemPattern_ScrollIntoView(HUIAPATTERNOBJECT h) { return call0(h, PM_ScrollItem_ScrollIntoView); }
HRESULT WINAPI ScrollPattern_Scroll(HUIAPATTERNOBJECT h, ScrollAmount horizontal, ScrollAmount vertical)
{
    VARIANT a[2] = {vi(horizontal), vi(vertical)};
    return call_with(h, PM_Scroll_Scroll, a, 2);
}
HRESULT WINAPI ScrollPattern_SetScrollPercent(HUIAPATTERNOBJECT h, double horizontal, double vertical)
{
    VARIANT a[2] = {vd(horizontal), vd(vertical)};
    return call_with(h, PM_Scroll_SetScrollPercent, a, 2);
}
HRESULT WINAPI SelectionItemPattern_AddToSelection(HUIAPATTERNOBJECT h) { return call0(h, PM_SelectionItem_AddToSelection); }
HRESULT WINAPI SelectionItemPattern_RemoveFromSelection(HUIAPATTERNOBJECT h) { return call0(h, PM_SelectionItem_RemoveFromSelection); }
HRESULT WINAPI SelectionItemPattern_Select(HUIAPATTERNOBJECT h) { return call0(h, PM_SelectionItem_Select); }
HRESULT WINAPI SynchronizedInputPattern_Cancel(HUIAPATTERNOBJECT h) { return call0(h, PM_SyncInput_Cancel); }
HRESULT WINAPI SynchronizedInputPattern_StartListening(HUIAPATTERNOBJECT h, SynchronizedInputType type)
{
    VARIANT a = vi(type);
    return call_with(h, PM_SyncInput_StartListening, &a, 1);
}
HRESULT WINAPI TextPattern_GetSelection(HUIAPATTERNOBJECT h, SAFEARRAY **ranges)
{
    if (!ranges)
        return E_INVALIDARG;
    VARIANT ret;
    HRESULT hr = call_pattern(h, PM_Text_GetSelection, nullptr, 0, &ret);
    return SUCCEEDED(hr) ? ranges_out(&ret, ranges) : hr;
}
HRESULT WINAPI TextPattern_GetVisibleRanges(HUIAPATTERNOBJECT h, SAFEARRAY **ranges)
{
    if (!ranges)
        return E_INVALIDARG;
    VARIANT ret;
    HRESULT hr = call_pattern(h, PM_Text_GetVisibleRanges, nullptr, 0, &ret);
    return SUCCEEDED(hr) ? ranges_out(&ret, ranges) : hr;
}
HRESULT WINAPI TextPattern_RangeFromChild(HUIAPATTERNOBJECT h, HUIANODE child, HUIATEXTRANGE *range)
{
    if (!range)
        return E_INVALIDARG;
    VARIANT a, ret;
    VariantInit(&a);
    NODE *c = as_node(child);
    if (!c)
        return E_INVALIDARG;
    a.vt = VT_UNKNOWN;
    a.punkVal = (IUnknown *)c;
    HRESULT hr = call_pattern(h, PM_Text_RangeFromChild, &a, 1, &ret);
    return SUCCEEDED(hr) ? range_out(&ret, range) : hr;
}
HRESULT WINAPI TextPattern_RangeFromPoint(HUIAPATTERNOBJECT h, UiaPoint point, HUIATEXTRANGE *range)
{
    if (!range)
        return E_INVALIDARG;
    VARIANT a[2] = {vd(point.x), vd(point.y)}, ret;
    HRESULT hr = call_pattern(h, PM_Text_RangeFromPoint, a, 2, &ret);
    return SUCCEEDED(hr) ? range_out(&ret, range) : hr;
}
HRESULT WINAPI TextPattern_get_DocumentRange(HUIAPATTERNOBJECT h, HUIATEXTRANGE *range)
{
    if (!range)
        return E_INVALIDARG;
    VARIANT ret;
    HRESULT hr = call_pattern(h, PM_Text_DocumentRange, nullptr, 0, &ret);
    return SUCCEEDED(hr) ? range_out(&ret, range) : hr;
}
HRESULT WINAPI TextPattern_get_SupportedTextSelection(HUIAPATTERNOBJECT h, SupportedTextSelection *selection)
{
    if (!selection)
        return E_INVALIDARG;
    VARIANT ret;
    HRESULT hr = call_pattern(h, PM_Text_SupportedTextSelection, nullptr, 0, &ret);
    *selection = SUCCEEDED(hr) && ret.vt == VT_I4 ? ret.lVal : 0;
    return hr;
}

static VARIANT vrange(HUIATEXTRANGE h)
{
    VARIANT v;
    VariantInit(&v);
    PATTERN *p = as_pattern(h);
    if (p) {
        v.vt = VT_UNKNOWN;
        v.punkVal = (IUnknown *)p;
    }
    return v;
}

HRESULT WINAPI TextRange_AddToSelection(HUIATEXTRANGE h) { return call0(h, PM_Range_AddToSelection); }
HRESULT WINAPI TextRange_Clone(HUIATEXTRANGE h, HUIATEXTRANGE *clone)
{
    if (!clone)
        return E_INVALIDARG;
    VARIANT ret;
    HRESULT hr = call_pattern(h, PM_Range_Clone, nullptr, 0, &ret);
    return SUCCEEDED(hr) ? range_out(&ret, clone) : hr;
}
HRESULT WINAPI TextRange_Compare(HUIATEXTRANGE h, HUIATEXTRANGE other, BOOL *same)
{
    if (!same)
        return E_INVALIDARG;
    VARIANT a = vrange(other), ret;
    HRESULT hr = call_pattern(h, PM_Range_Compare, &a, 1, &ret);
    *same = SUCCEEDED(hr) && ret.vt == VT_BOOL && ret.boolVal;
    return hr;
}
HRESULT WINAPI TextRange_CompareEndpoints(HUIATEXTRANGE h, TextPatternRangeEndpoint ep, HUIATEXTRANGE other,
                                          TextPatternRangeEndpoint other_ep, int *result)
{
    if (!result)
        return E_INVALIDARG;
    VARIANT a[3] = {vi(ep), vrange(other), vi(other_ep)}, ret;
    HRESULT hr = call_pattern(h, PM_Range_CompareEndpoints, a, 3, &ret);
    *result = SUCCEEDED(hr) && ret.vt == VT_I4 ? ret.lVal : 0;
    return hr;
}
HRESULT WINAPI TextRange_ExpandToEnclosingUnit(HUIATEXTRANGE h, TextUnit unit)
{
    VARIANT a = vi(unit);
    return call_with(h, PM_Range_ExpandToEnclosingUnit, &a, 1);
}
HRESULT WINAPI TextRange_FindAttribute(HUIATEXTRANGE h, TEXTATTRIBUTEID attribute, VARIANT value, BOOL backward,
                                       HUIATEXTRANGE *found)
{
    if (!found)
        return E_INVALIDARG;
    VARIANT a[3] = {vi(attribute), value, vi(backward)}, ret;
    HRESULT hr = call_pattern(h, PM_Range_FindAttribute, a, 3, &ret);
    return SUCCEEDED(hr) ? range_out(&ret, found) : hr;
}
HRESULT WINAPI TextRange_FindText(HUIATEXTRANGE h, BSTR text, BOOL backward, BOOL ignore_case, HUIATEXTRANGE *found)
{
    if (!found)
        return E_INVALIDARG;
    VARIANT a[3], ret;
    VariantInit(&a[0]);
    a[0].vt = VT_BSTR;
    a[0].bstrVal = text;
    a[1] = vi(backward);
    a[2] = vi(ignore_case);
    HRESULT hr = call_pattern(h, PM_Range_FindText, a, 3, &ret);
    return SUCCEEDED(hr) ? range_out(&ret, found) : hr;
}
HRESULT WINAPI TextRange_GetAttributeValue(HUIATEXTRANGE h, TEXTATTRIBUTEID attribute, VARIANT *value)
{
    if (!value)
        return E_INVALIDARG;
    VARIANT a = vi(attribute);
    return call_pattern(h, PM_Range_GetAttributeValue, &a, 1, value);
}
HRESULT WINAPI TextRange_GetBoundingRectangles(HUIATEXTRANGE h, SAFEARRAY **rects)
{
    if (!rects)
        return E_INVALIDARG;
    *rects = nullptr;
    VARIANT ret;
    HRESULT hr = call_pattern(h, PM_Range_GetBoundingRectangles, nullptr, 0, &ret);
    if (SUCCEEDED(hr) && (ret.vt & VT_ARRAY)) {
        *rects = ret.parray;
        ret.vt = VT_EMPTY;
    }
    VariantClear(&ret);
    return hr;
}
HRESULT WINAPI TextRange_GetChildren(HUIATEXTRANGE h, SAFEARRAY **children)
{
    if (!children)
        return E_INVALIDARG;
    *children = nullptr;
    VARIANT ret;
    HRESULT hr = call_pattern(h, PM_Range_GetChildren, nullptr, 0, &ret);
    if (SUCCEEDED(hr) && (ret.vt & VT_ARRAY)) {
        *children = ret.parray;
        ret.vt = VT_EMPTY;
    }
    VariantClear(&ret);
    return hr;
}
HRESULT WINAPI TextRange_GetEnclosingElement(HUIATEXTRANGE h, HUIANODE *node)
{
    if (!node)
        return E_INVALIDARG;
    VARIANT ret;
    HRESULT hr = call_pattern(h, PM_Range_GetEnclosingElement, nullptr, 0, &ret);
    return SUCCEEDED(hr) ? node_out(&ret, node) : hr;
}
HRESULT WINAPI TextRange_GetText(HUIATEXTRANGE h, int max, BSTR *text)
{
    if (!text)
        return E_INVALIDARG;
    *text = nullptr;
    VARIANT a = vi(max), ret;
    HRESULT hr = call_pattern(h, PM_Range_GetText, &a, 1, &ret);
    if (SUCCEEDED(hr) && ret.vt == VT_BSTR) {
        *text = ret.bstrVal;
        ret.vt = VT_EMPTY;
    }
    VariantClear(&ret);
    return hr;
}
HRESULT WINAPI TextRange_Move(HUIATEXTRANGE h, TextUnit unit, int count, int *moved)
{
    if (!moved)
        return E_INVALIDARG;
    VARIANT a[2] = {vi(unit), vi(count)}, ret;
    HRESULT hr = call_pattern(h, PM_Range_Move, a, 2, &ret);
    *moved = SUCCEEDED(hr) && ret.vt == VT_I4 ? ret.lVal : 0;
    return hr;
}
HRESULT WINAPI TextRange_MoveEndpointByRange(HUIATEXTRANGE h, TextPatternRangeEndpoint ep, HUIATEXTRANGE other,
                                             TextPatternRangeEndpoint other_ep)
{
    VARIANT a[3] = {vi(ep), vrange(other), vi(other_ep)};
    return call_pattern(h, PM_Range_MoveEndpointByRange, a, 3, nullptr);
}
HRESULT WINAPI TextRange_MoveEndpointByUnit(HUIATEXTRANGE h, TextPatternRangeEndpoint ep, TextUnit unit, int count,
                                            int *moved)
{
    if (!moved)
        return E_INVALIDARG;
    VARIANT a[3] = {vi(ep), vi(unit), vi(count)}, ret;
    HRESULT hr = call_pattern(h, PM_Range_MoveEndpointByUnit, a, 3, &ret);
    *moved = SUCCEEDED(hr) && ret.vt == VT_I4 ? ret.lVal : 0;
    return hr;
}
HRESULT WINAPI TextRange_RemoveFromSelection(HUIATEXTRANGE h) { return call0(h, PM_Range_RemoveFromSelection); }
HRESULT WINAPI TextRange_ScrollIntoView(HUIATEXTRANGE h, BOOL align_to_top)
{
    VARIANT a = vi(align_to_top);
    return call_with(h, PM_Range_ScrollIntoView, &a, 1);
}
HRESULT WINAPI TextRange_Select(HUIATEXTRANGE h) { return call0(h, PM_Range_Select); }
HRESULT WINAPI TogglePattern_Toggle(HUIAPATTERNOBJECT h) { return call0(h, PM_Toggle_Toggle); }
HRESULT WINAPI TransformPattern_Move(HUIAPATTERNOBJECT h, double x, double y)
{
    VARIANT a[2] = {vd(x), vd(y)};
    return call_with(h, PM_Transform_Move, a, 2);
}
HRESULT WINAPI TransformPattern_Resize(HUIAPATTERNOBJECT h, double width, double height)
{
    VARIANT a[2] = {vd(width), vd(height)};
    return call_with(h, PM_Transform_Resize, a, 2);
}
HRESULT WINAPI TransformPattern_Rotate(HUIAPATTERNOBJECT h, double degrees)
{
    VARIANT a = vd(degrees);
    return call_with(h, PM_Transform_Rotate, &a, 1);
}
HRESULT WINAPI ValuePattern_SetValue(HUIAPATTERNOBJECT h, LPCWSTR value)
{
    VARIANT a = vs(value);
    return call_with(h, PM_Value_SetValue, &a, 1);
}
HRESULT WINAPI VirtualizedItemPattern_Realize(HUIAPATTERNOBJECT h) { return call0(h, PM_Virtualized_Realize); }
HRESULT WINAPI WindowPattern_Close(HUIAPATTERNOBJECT h) { return call0(h, PM_Window_Close); }
HRESULT WINAPI WindowPattern_SetWindowVisualState(HUIAPATTERNOBJECT h, WindowVisualState state)
{
    VARIANT a = vi(state);
    return call_with(h, PM_Window_SetVisualState, &a, 1);
}
HRESULT WINAPI WindowPattern_WaitForInputIdle(HUIAPATTERNOBJECT h, int milliseconds, BOOL *timed_out)
{
    if (!timed_out)
        return E_INVALIDARG;
    VARIANT a = vi(milliseconds), ret;
    HRESULT hr = call_pattern(h, PM_Window_WaitForInputIdle, &a, 1, &ret);
    *timed_out = SUCCEEDED(hr) && ret.vt == VT_BOOL && ret.boolVal;
    return hr;
}
