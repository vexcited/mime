/*
 * the automation object clients create, IUIAutomation, with the event handlers it registers and its class factory.
 */
#include "uia.h"

extern const IID IID_IMimeCondition;
extern HRESULT CondToCom(COND *c, IUnknown **out);


/* ------------------------------------------------------------------ handlers */

typedef struct HANDLER {
    struct HANDLER *next;
    DWORD cookie;
    int kind;
    int event_id;
    IUnknown *handler;
    NODE *root;
    int scope;
} HANDLER;

typedef struct SINK {
    IDispatch iface;
    LONG refs;
    int kind;
    IUnknown *handler;
    REQUEST request;
    HANDLER *owner;
} SINK;

typedef struct AUTO {
    const void *const *vtbl;
    LONG refs;
    HANDLER *handlers;
    BOOL auto_set_focus;
    DWORD connection_timeout, transaction_timeout;
    int recovery, coalesce;
    CRITICAL_SECTION lock;
} AUTO;

static const void *const g_auto_vtbl[];
static const IDispatchVtbl g_sink_vtbl;

static HRESULT STDMETHODCALLTYPE sink_query(IDispatch *iface, REFIID riid, void **out)
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

static ULONG STDMETHODCALLTYPE sink_add_ref(IDispatch *iface)
{
    return (ULONG)InterlockedIncrement(&((SINK *)iface)->refs);
}

static ULONG STDMETHODCALLTYPE sink_release(IDispatch *iface)
{
    SINK *s = (SINK *)iface;
    LONG refs = InterlockedDecrement(&s->refs);
    if (!refs) {
        s->handler->lpVtbl->Release(s->handler);
        RequestClear(&s->request);
        UiaFree(s);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE sink_count(IDispatch *iface, UINT *count)
{
    *count = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sink_info(IDispatch *iface, UINT index, LCID lcid, ITypeInfo **info)
{
    *info = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE sink_names(IDispatch *iface, REFIID riid, LPOLESTR *names, UINT count, LCID lcid,
                                            DISPID *ids)
{
    return DISP_E_UNKNOWNNAME;
}

static VARIANT *arg(DISPPARAMS *p, ULONG i)
{
    return i < p->cArgs ? &p->rgvarg[p->cArgs - 1 - i] : nullptr;
}

static int arg_int(DISPPARAMS *p, ULONG i)
{
    VARIANT *v = arg(p, i);
    return v && v->vt == VT_I4 ? v->lVal : 0;
}

typedef HRESULT(STDMETHODCALLTYPE *HANDLE0)(IUnknown *, IUnknown *);

/* an event from a provider: the sender is made an element, with the cache the handler asked for. */
static HRESULT STDMETHODCALLTYPE sink_invoke(IDispatch *iface, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                             DISPPARAMS *params, VARIANT *result, EXCEPINFO *ex, UINT *err)
{
    SINK *s = (SINK *)iface;
    if (id != SINK_DISPID_EVENT || !params || params->cArgs < 3)
        return DISP_E_MEMBERNOTFOUND;
    VARIANT *source = arg(params, 1);
    int event_id = arg_int(params, 2);
    NODE *node = nullptr;
    if (source->vt != VT_DISPATCH || !source->pdispVal || FAILED(RemoteNodeFromDispatch(source->pdispVal, &node)))
        return S_OK;
    ELEMENT *element = nullptr;
    HRESULT hr = ElementCreate(node, s->request.nprops || s->request.npatterns || s->request.scope != 1 ? &s->request
                                                                                                        : &s->request,
                               &element);
    NodeRelease(node);
    if (FAILED(hr))
        return S_OK;
    VARIANT *a0 = arg(params, 3), *a1 = arg(params, 4), *a2 = arg(params, 5), *a3 = arg(params, 6);
    void **vtbl = *(void ***)s->handler;
    IUnknown *sender = (IUnknown *)element;
    switch (s->kind) {
    case EK_AUTOMATION:
        ((HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *, int))vtbl[3])(s->handler, sender, event_id);
        break;
    case EK_PROPERTY: {
        VARIANT value;
        VariantInit(&value);
        if (a2) {
            VariantCopy(&value, a2);
            if (value.vt == VT_DISPATCH || value.vt == VT_UNKNOWN) {
                NODE *n = nullptr;
                IUnknown *wrapped = nullptr;
                if (value.punkVal && SUCCEEDED(NodeFromUnknown(value.punkVal, &n)) && n) {
                    ElementCreate(n, nullptr, (ELEMENT **)&wrapped);
                    NodeRelease(n);
                }
                VariantClear(&value);
                value.vt = VT_UNKNOWN;
                value.punkVal = wrapped;
            }
        }
        ((HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *, int, VARIANT))vtbl[3])(s->handler, sender,
                                                                                      a0 ? a0->lVal : 0, value);
        VariantClear(&value);
        break;
    }
    case EK_STRUCTURE:
        ((HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *, int, SAFEARRAY *))vtbl[3])(
            s->handler, sender, a0 ? a0->lVal : 0, a1 && (a1->vt & VT_ARRAY) ? a1->parray : nullptr);
        break;
    case EK_FOCUS:
        ((HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *))vtbl[3])(s->handler, sender);
        break;
    case EK_TEXT_EDIT:
        ((HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *, int, SAFEARRAY *))vtbl[3])(
            s->handler, sender, a0 ? a0->lVal : 0, a1 && (a1->vt & VT_ARRAY) ? a1->parray : nullptr);
        break;
    case EK_NOTIFICATION:
        ((HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *, int, int, BSTR, BSTR))vtbl[3])(
            s->handler, sender, a0 ? a0->lVal : 0, a1 ? a1->lVal : 0, a2 ? a2->bstrVal : nullptr,
            a3 ? a3->bstrVal : nullptr);
        break;
    case EK_CHANGES: {
        int count = 0;
        UiaChangesEventArgs unused = {};
        (void)unused;
        if (a0 && a0->vt == (VT_ARRAY | VT_VARIANT) && a0->parray) {
            LONG lower, upper;
            VARIANT *items;
            SafeArrayGetLBound(a0->parray, 1, &lower);
            SafeArrayGetUBound(a0->parray, 1, &upper);
            count = (upper - lower + 1) / 3;
            UiaChangeInfo *changes = count ? UiaAlloc((SIZE_T)count * sizeof(*changes)) : nullptr;
            if (changes && SUCCEEDED(SafeArrayAccessData(a0->parray, (void **)&items))) {
                for (int i = 0; i < count; i++) {
                    changes[i].uiaId = items[i * 3].lVal;
                    VariantCopy(&changes[i].payload, &items[i * 3 + 1]);
                    VariantCopy(&changes[i].extraInfo, &items[i * 3 + 2]);
                }
                SafeArrayUnaccessData(a0->parray);
                ((HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *, UiaChangeInfo *, int))vtbl[3])(
                    s->handler, sender, changes, count);
                for (int i = 0; i < count; i++) {
                    VariantClear(&changes[i].payload);
                    VariantClear(&changes[i].extraInfo);
                }
            }
            UiaFree(changes);
        }
        break;
    }
    case EK_ACTIVE_TEXT:
        ((HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *, IUnknown *))vtbl[3])(s->handler, sender, nullptr);
        break;
    }
    ElementRelease(element);
    return S_OK;
}

static const IDispatchVtbl g_sink_vtbl = {sink_query, sink_add_ref, sink_release, sink_count,
                                          sink_info,  sink_names,   sink_invoke};

static HRESULT register_handler(AUTO *a, int kind, int event_id, IUnknown *element_unknown, int scope,
                                IUnknown *request, IUnknown *handler, const int *props, int nprops)
{
    if (!element_unknown || !handler)
        return E_POINTER;
    if (scope == TreeScope_None || (scope & ~(TreeScope_Element | TreeScope_Children | TreeScope_Descendants)))
        return E_INVALIDARG;
    ELEMENT *element = nullptr;
    if (FAILED(ElementFromCom(element_unknown, &element)) || !element)
        return E_INVALIDARG;
    SINK *sink = UiaAlloc(sizeof(*sink));
    if (!sink) {
        ElementRelease(element);
        return E_OUTOFMEMORY;
    }
    sink->iface.lpVtbl = &g_sink_vtbl;
    sink->refs = 1;
    sink->kind = kind;
    handler->lpVtbl->AddRef(handler);
    sink->handler = handler;
    HRESULT hr = S_OK;
    if (request)
        hr = RequestFromCom(request, &sink->request);
    else
        RequestInit(&sink->request);
    NODE *root = ElementNode(element);
    int *rid = nullptr, rid_len = 0;
    HWND window = NodeOwnerWindow(root);
    VARIANT v;
    VariantInit(&v);
    BOOL desktop = FALSE;
    if (SUCCEEDED(NodeGetProperty(root, UIA_ClassNamePropertyId, TRUE, &v)) && v.vt == VT_BSTR && v.bstrVal &&
        window == GetDesktopWindow())
        desktop = TRUE;
    VariantClear(&v);
    if (window == GetDesktopWindow())
        desktop = TRUE;
    if (SUCCEEDED(hr) && !desktop)
        hr = NodeRuntimeId(root, &rid, &rid_len);
    DWORD cookie = 0;
    if (SUCCEEDED(hr))
        hr = UiaRegistryAdd(kind, event_id, scope, desktop ? GetDesktopWindow() : (rid_len ? nullptr : window), props,
                            nprops, rid, rid_len > 16 ? 16 : rid_len, (IUnknown *)&sink->iface, &cookie);
    UiaFree(rid);
    if (SUCCEEDED(hr)) {
        HANDLER *h = UiaAlloc(sizeof(*h));
        if (h) {
            h->cookie = cookie;
            h->kind = kind;
            h->event_id = event_id;
            h->handler = handler;
            handler->lpVtbl->AddRef(handler);
            h->root = root;
            NodeAddRef(root);
            h->scope = scope;
            EnterCriticalSection(&a->lock);
            h->next = a->handlers;
            a->handlers = h;
            LeaveCriticalSection(&a->lock);
        }
    }
    sink_release(&sink->iface);
    ElementRelease(element);
    return hr;
}

static BOOL same_object(IUnknown *a, IUnknown *b)
{
    IUnknown *ia = nullptr, *ib = nullptr;
    a->lpVtbl->QueryInterface(a, &IID_IUnknown, (void **)&ia);
    b->lpVtbl->QueryInterface(b, &IID_IUnknown, (void **)&ib);
    BOOL same = ia && ia == ib;
    if (ia)
        ia->lpVtbl->Release(ia);
    if (ib)
        ib->lpVtbl->Release(ib);
    return same;
}

static void drop_handler(HANDLER *h)
{
    UiaRegistryRemove(h->cookie);
    h->handler->lpVtbl->Release(h->handler);
    NodeRelease(h->root);
    UiaFree(h);
}

static HRESULT unregister_handler(AUTO *a, int kind, int event_id, IUnknown *element_unknown, IUnknown *handler)
{
    if (!handler || (element_unknown == nullptr && kind != EK_FOCUS))
        return E_POINTER;
    ELEMENT *element = nullptr;
    if (element_unknown && (FAILED(ElementFromCom(element_unknown, &element)) || !element))
        return E_INVALIDARG;
    EnterCriticalSection(&a->lock);
    for (HANDLER **link = &a->handlers; *link;) {
        HANDLER *h = *link;
        if (h->kind == kind && (kind != EK_AUTOMATION || h->event_id == event_id) && same_object(h->handler, handler) &&
            (!element || NodeSame(h->root, ElementNode(element)))) {
            *link = h->next;
            LeaveCriticalSection(&a->lock);
            drop_handler(h);
            EnterCriticalSection(&a->lock);
            link = &a->handlers;
            continue;
        }
        link = &h->next;
    }
    LeaveCriticalSection(&a->lock);
    ElementRelease(element);
    return S_OK;
}

/* ------------------------------------------------------------------ the object */

static AUTO *me(void *p)
{
    return p;
}

static HRESULT STDMETHODCALLTYPE auto_query(void *p, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IUIAutomation) || IsEqualIID(riid, &IID_IUIAutomation2) ||
        IsEqualIID(riid, &IID_IUIAutomation3) || IsEqualIID(riid, &IID_IUIAutomation4) ||
        IsEqualIID(riid, &IID_IUIAutomation5) || IsEqualIID(riid, &IID_IUIAutomation6)) {
        *out = p;
        InterlockedIncrement(&me(p)->refs);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE auto_add_ref(void *p)
{
    return (ULONG)InterlockedIncrement(&me(p)->refs);
}

static void remove_all(AUTO *a)
{
    EnterCriticalSection(&a->lock);
    HANDLER *list = a->handlers;
    a->handlers = nullptr;
    LeaveCriticalSection(&a->lock);
    while (list) {
        HANDLER *next = list->next;
        drop_handler(list);
        list = next;
    }
}

static ULONG STDMETHODCALLTYPE auto_release(void *p)
{
    AUTO *a = p;
    LONG refs = InterlockedDecrement(&a->refs);
    if (!refs) {
        remove_all(a);
        DeleteCriticalSection(&a->lock);
        UiaFree(a);
    }
    return (ULONG)refs;
}

static HRESULT element_out(NODE *node, IUnknown *request, void **out)
{
    REQUEST req;
    BOOL have = FALSE;
    HRESULT hr = S_OK;
    if (request) {
        hr = RequestFromCom(request, &req);
        have = SUCCEEDED(hr);
    }
    if (SUCCEEDED(hr))
        hr = ElementCreate(node, have ? &req : nullptr, (ELEMENT **)out);
    if (have)
        RequestClear(&req);
    return hr;
}

static HRESULT STDMETHODCALLTYPE A_CompareElements(void *p, IUnknown *a, IUnknown *b, BOOL *same)
{
    if (!same || !a || !b)
        return E_POINTER;
    ELEMENT *ea = nullptr, *eb = nullptr;
    if (FAILED(ElementFromCom(a, &ea)) || FAILED(ElementFromCom(b, &eb)) || !ea || !eb) {
        ElementRelease(ea);
        ElementRelease(eb);
        return E_INVALIDARG;
    }
    *same = NodeSame(ElementNode(ea), ElementNode(eb));
    ElementRelease(ea);
    ElementRelease(eb);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE A_CompareRuntimeIds(void *p, SAFEARRAY *a, SAFEARRAY *b, BOOL *same)
{
    if (!same || !a || !b)
        return E_POINTER;
    int *ia, *ib, na, nb;
    HRESULT hr = UiaSafeArrayToIntArray(a, &ia, &na);
    if (SUCCEEDED(hr))
        hr = UiaSafeArrayToIntArray(b, &ib, &nb);
    if (SUCCEEDED(hr)) {
        *same = !UiaCompareRuntimeIds(ia, na, ib, nb);
        UiaFree(ib);
    }
    UiaFree(ia);
    return hr;
}

static HRESULT root_impl(IUnknown *request, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    NODE *root = nullptr;
    HRESULT hr = UiaClientRootNode(&root);
    if (FAILED(hr))
        return hr;
    hr = element_out(root, request, out);
    NodeRelease(root);
    return hr;
}

static HRESULT STDMETHODCALLTYPE A_GetRootElement(void *p, void **out)
{
    return root_impl(nullptr, out);
}

static HRESULT STDMETHODCALLTYPE A_GetRootElementBuildCache(void *p, IUnknown *r, void **out)
{
    return root_impl(r, out);
}

static HRESULT handle_impl(HWND hwnd, IUnknown *request, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!hwnd || !IsWindow(hwnd))
        return UIA_E_ELEMENTNOTAVAILABLE;
    NODE *node = nullptr;
    HRESULT hr = UiaClientNodeFromWindow(hwnd, &node);
    if (FAILED(hr))
        return hr;
    hr = element_out(node, request, out);
    NodeRelease(node);
    return hr;
}

static HRESULT STDMETHODCALLTYPE A_ElementFromHandle(void *p, HWND hwnd, void **out)
{
    return handle_impl(hwnd, nullptr, out);
}

static HRESULT STDMETHODCALLTYPE A_ElementFromHandleBuildCache(void *p, HWND hwnd, IUnknown *r, void **out)
{
    return handle_impl(hwnd, r, out);
}

extern HRESULT UiaClientNodeFromPoint(POINT pt, NODE **out);
extern HRESULT UiaClientNodeFromFocus(NODE **out);

static HRESULT STDMETHODCALLTYPE A_ElementFromPoint(void *p, POINT pt, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    NODE *node = nullptr;
    HRESULT hr = UiaClientNodeFromPoint(pt, &node);
    if (FAILED(hr))
        return hr;
    hr = ElementCreate(node, nullptr, (ELEMENT **)out);
    NodeRelease(node);
    return hr;
}

static HRESULT STDMETHODCALLTYPE A_ElementFromPointBuildCache(void *p, POINT pt, IUnknown *r, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    NODE *node = nullptr;
    HRESULT hr = UiaClientNodeFromPoint(pt, &node);
    if (FAILED(hr))
        return hr;
    hr = element_out(node, r, out);
    NodeRelease(node);
    return hr;
}

static HRESULT STDMETHODCALLTYPE A_GetFocusedElement(void *p, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    NODE *node = nullptr;
    HRESULT hr = UiaClientNodeFromFocus(&node);
    if (FAILED(hr))
        return hr;
    hr = ElementCreate(node, nullptr, (ELEMENT **)out);
    NodeRelease(node);
    return hr;
}

static HRESULT STDMETHODCALLTYPE A_GetFocusedElementBuildCache(void *p, IUnknown *r, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    NODE *node = nullptr;
    HRESULT hr = UiaClientNodeFromFocus(&node);
    if (FAILED(hr))
        return hr;
    hr = element_out(node, r, out);
    NodeRelease(node);
    return hr;
}

static HRESULT STDMETHODCALLTYPE A_CreateTreeWalker(void *p, IUnknown *condition, IUnknown **out)
{
    if (!out || !condition)
        return E_POINTER;
    *out = nullptr;
    COND *c;
    HRESULT hr = CondFromCom(condition, &c);
    if (FAILED(hr))
        return hr;
    hr = WalkerCreate(c, out);
    CondRelease(c);
    return hr;
}

static HRESULT walker_of(HRESULT (*make)(COND **), IUnknown **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    COND *c = nullptr;
    HRESULT hr = make(&c);
    if (SUCCEEDED(hr)) {
        hr = WalkerCreate(c, out);
        CondRelease(c);
    }
    return hr;
}

static HRESULT raw_view(COND **out)
{
    *out = CondNew(ConditionType_True);
    return *out ? S_OK : E_OUTOFMEMORY;
}

static HRESULT cond_of_view(HRESULT (*make)(COND **), IUnknown **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    COND *c = nullptr;
    HRESULT hr = make(&c);
    if (SUCCEEDED(hr)) {
        hr = CondToCom(c, out);
        CondRelease(c);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE A_get_ControlViewWalker(void *p, IUnknown **out) { return walker_of(ControlViewCondition, out); }
static HRESULT STDMETHODCALLTYPE A_get_ContentViewWalker(void *p, IUnknown **out) { return walker_of(ContentViewCondition, out); }
static HRESULT STDMETHODCALLTYPE A_get_RawViewWalker(void *p, IUnknown **out) { return walker_of(raw_view, out); }
static HRESULT STDMETHODCALLTYPE A_get_RawViewCondition(void *p, IUnknown **out) { return cond_of_view(raw_view, out); }
static HRESULT STDMETHODCALLTYPE A_get_ControlViewCondition(void *p, IUnknown **out) { return cond_of_view(ControlViewCondition, out); }
static HRESULT STDMETHODCALLTYPE A_get_ContentViewCondition(void *p, IUnknown **out) { return cond_of_view(ContentViewCondition, out); }

static HRESULT STDMETHODCALLTYPE A_CreateCacheRequest(void *p, IUnknown **out)
{
    if (!out)
        return E_POINTER;
    return RequestCreate(out);
}

static HRESULT bool_condition(int type, IUnknown **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    COND *c = CondNew(type);
    if (!c)
        return E_OUTOFMEMORY;
    HRESULT hr = CondToCom(c, out);
    CondRelease(c);
    return hr;
}

static HRESULT STDMETHODCALLTYPE A_CreateTrueCondition(void *p, IUnknown **out) { return bool_condition(ConditionType_True, out); }
static HRESULT STDMETHODCALLTYPE A_CreateFalseCondition(void *p, IUnknown **out) { return bool_condition(ConditionType_False, out); }

static HRESULT STDMETHODCALLTYPE A_CreatePropertyConditionEx(void *p, PROPERTYID id, VARIANT value, int flags,
                                                             IUnknown **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    const PROPERTY_INFO *info = UiaPropertyInfo(id);
    if (!info)
        return E_INVALIDARG;
    if (value.vt != info->vt)
        return E_INVALIDARG;
    if (flags & ~(PropertyConditionFlags_IgnoreCase | PropertyConditionFlags_MatchSubstring))
        return E_INVALIDARG;
    COND *c;
    HRESULT hr = CondProperty_(id, &value, flags, &c);
    if (SUCCEEDED(hr)) {
        hr = CondToCom(c, out);
        CondRelease(c);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE A_CreatePropertyCondition(void *p, PROPERTYID id, VARIANT value, IUnknown **out)
{
    return A_CreatePropertyConditionEx(p, id, value, PropertyConditionFlags_None, out);
}

static HRESULT group_of(int type, IUnknown **conditions, int count, IUnknown **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    COND **items = count > 0 ? UiaAlloc((SIZE_T)count * sizeof(*items)) : nullptr;
    HRESULT hr = S_OK;
    int made = 0;
    for (int i = 0; i < count && SUCCEEDED(hr); i++, made++)
        hr = conditions[i] ? CondFromCom(conditions[i], &items[i]) : E_POINTER;
    if (SUCCEEDED(hr)) {
        COND *g;
        hr = CondGroup(type, items, made, &g);
        if (SUCCEEDED(hr)) {
            hr = CondToCom(g, out);
            CondRelease(g);
        }
    }
    for (int i = 0; i < made; i++)
        if (items && items[i])
            CondRelease(items[i]);
    UiaFree(items);
    return hr;
}

static HRESULT group_of_array(int type, SAFEARRAY *array, IUnknown **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!array)
        return E_POINTER;
    LONG lower, upper;
    IUnknown **items;
    if (SafeArrayGetDim(array) != 1 || FAILED(SafeArrayGetLBound(array, 1, &lower)) ||
        FAILED(SafeArrayGetUBound(array, 1, &upper)))
        return E_INVALIDARG;
    int count = upper - lower + 1;
    if (count <= 0)
        return group_of(type, nullptr, 0, out);
    if (FAILED(SafeArrayAccessData(array, (void **)&items)))
        return E_INVALIDARG;
    HRESULT hr = group_of(type, items, count, out);
    SafeArrayUnaccessData(array);
    return hr;
}

static HRESULT STDMETHODCALLTYPE A_CreateAndCondition(void *p, IUnknown *a, IUnknown *b, IUnknown **out)
{
    IUnknown *items[2] = {a, b};
    return group_of(ConditionType_And, items, 2, out);
}
static HRESULT STDMETHODCALLTYPE A_CreateAndConditionFromArray(void *p, SAFEARRAY *array, IUnknown **out)
{
    return group_of_array(ConditionType_And, array, out);
}
static HRESULT STDMETHODCALLTYPE A_CreateAndConditionFromNativeArray(void *p, IUnknown **items, int count, IUnknown **out)
{
    return group_of(ConditionType_And, items, count, out);
}
static HRESULT STDMETHODCALLTYPE A_CreateOrCondition(void *p, IUnknown *a, IUnknown *b, IUnknown **out)
{
    IUnknown *items[2] = {a, b};
    return group_of(ConditionType_Or, items, 2, out);
}
static HRESULT STDMETHODCALLTYPE A_CreateOrConditionFromArray(void *p, SAFEARRAY *array, IUnknown **out)
{
    return group_of_array(ConditionType_Or, array, out);
}
static HRESULT STDMETHODCALLTYPE A_CreateOrConditionFromNativeArray(void *p, IUnknown **items, int count, IUnknown **out)
{
    return group_of(ConditionType_Or, items, count, out);
}

static HRESULT STDMETHODCALLTYPE A_CreateNotCondition(void *p, IUnknown *condition, IUnknown **out)
{
    if (!out || !condition)
        return E_POINTER;
    *out = nullptr;
    COND *child, *n;
    HRESULT hr = CondFromCom(condition, &child);
    if (FAILED(hr))
        return hr;
    hr = CondNot(child, &n);
    CondRelease(child);
    if (SUCCEEDED(hr)) {
        hr = CondToCom(n, out);
        CondRelease(n);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE A_AddAutomationEventHandler(void *p, int event, IUnknown *element, int scope,
                                                             IUnknown *request, IUnknown *handler)
{
    return register_handler(p, EK_AUTOMATION, event, element, scope, request, handler, nullptr, 0);
}

static HRESULT STDMETHODCALLTYPE A_RemoveAutomationEventHandler(void *p, int event, IUnknown *element, IUnknown *handler)
{
    return unregister_handler(p, EK_AUTOMATION, event, element, handler);
}

static HRESULT STDMETHODCALLTYPE A_AddPropertyChangedEventHandlerNativeArray(void *p, IUnknown *element, int scope,
                                                                             IUnknown *request, IUnknown *handler,
                                                                             PROPERTYID *props, int count)
{
    for (int i = 0; i < count; i++)
        if (!UiaPropertyInfo(props[i]))
            return E_INVALIDARG;
    return register_handler(p, EK_PROPERTY, UIA_AutomationPropertyChangedEventId, element, scope, request, handler,
                            props, count);
}

static HRESULT STDMETHODCALLTYPE A_AddPropertyChangedEventHandler(void *p, IUnknown *element, int scope,
                                                                  IUnknown *request, IUnknown *handler, SAFEARRAY *props)
{
    int *ids = nullptr, count = 0;
    if (props && FAILED(UiaSafeArrayToIntArray(props, &ids, &count)))
        return E_INVALIDARG;
    HRESULT hr = A_AddPropertyChangedEventHandlerNativeArray(p, element, scope, request, handler, ids, count);
    UiaFree(ids);
    return hr;
}

static HRESULT STDMETHODCALLTYPE A_RemovePropertyChangedEventHandler(void *p, IUnknown *element, IUnknown *handler)
{
    return unregister_handler(p, EK_PROPERTY, UIA_AutomationPropertyChangedEventId, element, handler);
}

static HRESULT STDMETHODCALLTYPE A_AddStructureChangedEventHandler(void *p, IUnknown *element, int scope,
                                                                   IUnknown *request, IUnknown *handler)
{
    return register_handler(p, EK_STRUCTURE, UIA_StructureChangedEventId, element, scope, request, handler, nullptr, 0);
}

static HRESULT STDMETHODCALLTYPE A_RemoveStructureChangedEventHandler(void *p, IUnknown *element, IUnknown *handler)
{
    return unregister_handler(p, EK_STRUCTURE, UIA_StructureChangedEventId, element, handler);
}

static HRESULT STDMETHODCALLTYPE A_AddFocusChangedEventHandler(void *p, IUnknown *request, IUnknown *handler)
{
    NODE *root = nullptr;
    HRESULT hr = UiaClientRootNode(&root);
    if (FAILED(hr))
        return hr;
    ELEMENT *e;
    hr = ElementCreate(root, nullptr, &e);
    NodeRelease(root);
    if (SUCCEEDED(hr)) {
        hr = register_handler(p, EK_FOCUS, UIA_AutomationFocusChangedEventId, (IUnknown *)e,
                              TreeScope_Subtree, request, handler, nullptr, 0);
        ElementRelease(e);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE A_RemoveFocusChangedEventHandler(void *p, IUnknown *handler)
{
    return unregister_handler(p, EK_FOCUS, UIA_AutomationFocusChangedEventId, nullptr, handler);
}

static HRESULT STDMETHODCALLTYPE A_RemoveAllEventHandlers(void *p)
{
    remove_all(p);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE A_IntNativeArrayToSafeArray(void *p, int *array, int count, SAFEARRAY **out)
{
    if (!out || (!array && count))
        return E_POINTER;
    return UiaIntArrayToSafeArray(array, count, out);
}

static HRESULT STDMETHODCALLTYPE A_IntSafeArrayToNativeArray(void *p, SAFEARRAY *array, int **out, int *count)
{
    if (!out || !count || !array)
        return E_POINTER;
    int *values;
    int n;
    HRESULT hr = UiaSafeArrayToIntArray(array, &values, &n);
    if (FAILED(hr))
        return hr;
    int *copy = CoTaskMemAlloc((SIZE_T)(n ? n : 1) * sizeof(int));
    if (!copy) {
        UiaFree(values);
        return E_OUTOFMEMORY;
    }
    memcpy(copy, values, (SIZE_T)n * sizeof(int));
    UiaFree(values);
    *out = copy;
    *count = n;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE A_RectToVariant(void *p, RECT rect, VARIANT *out)
{
    if (!out)
        return E_POINTER;
    VariantInit(out);
    double d[4] = {rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
    HRESULT hr = UiaDoubleArrayToSafeArray(d, 4, &out->parray);
    if (SUCCEEDED(hr))
        out->vt = VT_ARRAY | VT_R8;
    return hr;
}

static HRESULT STDMETHODCALLTYPE A_VariantToRect(void *p, VARIANT v, RECT *out)
{
    if (!out)
        return E_POINTER;
    if (v.vt != (VT_ARRAY | VT_R8) || !v.parray)
        return E_INVALIDARG;
    LONG lower, upper;
    double *d;
    SafeArrayGetLBound(v.parray, 1, &lower);
    SafeArrayGetUBound(v.parray, 1, &upper);
    if (upper - lower + 1 != 4 || FAILED(SafeArrayAccessData(v.parray, (void **)&d)))
        return E_INVALIDARG;
    out->left = (LONG)d[0];
    out->top = (LONG)d[1];
    out->right = (LONG)(d[0] + d[2]);
    out->bottom = (LONG)(d[1] + d[3]);
    SafeArrayUnaccessData(v.parray);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE A_SafeArrayToRectNativeArray(void *p, SAFEARRAY *array, RECT **out, int *count)
{
    if (!out || !count || !array)
        return E_POINTER;
    LONG lower, upper;
    double *d;
    SafeArrayGetLBound(array, 1, &lower);
    SafeArrayGetUBound(array, 1, &upper);
    int total = upper - lower + 1;
    if (total % 4 || FAILED(SafeArrayAccessData(array, (void **)&d)))
        return E_INVALIDARG;
    int n = total / 4;
    RECT *rects = CoTaskMemAlloc((SIZE_T)(n ? n : 1) * sizeof(RECT));
    if (!rects) {
        SafeArrayUnaccessData(array);
        return E_OUTOFMEMORY;
    }
    for (int i = 0; i < n; i++) {
        rects[i].left = (LONG)d[i * 4];
        rects[i].top = (LONG)d[i * 4 + 1];
        rects[i].right = (LONG)(d[i * 4] + d[i * 4 + 2]);
        rects[i].bottom = (LONG)(d[i * 4 + 1] + d[i * 4 + 3]);
    }
    SafeArrayUnaccessData(array);
    *out = rects;
    *count = n;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE A_CreateProxyFactoryEntry(void *p, IUnknown *factory, IUnknown **out)
{
    if (out)
        *out = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE A_get_ProxyFactoryMapping(void *p, IUnknown **out)
{
    if (out)
        *out = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE A_GetPropertyProgrammaticName(void *p, PROPERTYID id, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    const PROPERTY_INFO *info = UiaPropertyInfo(id);
    if (!info)
        return E_INVALIDARG;
    *out = SysAllocString(info->name);
    return *out ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE A_GetPatternProgrammaticName(void *p, PATTERNID id, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    const WCHAR *name = UiaPatternName(id);
    if (!name)
        return E_INVALIDARG;
    *out = SysAllocString(name);
    return *out ? S_OK : E_OUTOFMEMORY;
}

static HRESULT names_array(const int *ids, const WCHAR *const *names, int count, SAFEARRAY **id_array,
                           SAFEARRAY **name_array)
{
    HRESULT hr = UiaIntArrayToSafeArray(ids, count, id_array);
    if (FAILED(hr))
        return hr;
    SAFEARRAY *strings = SafeArrayCreateVector(VT_BSTR, 0, (ULONG)count);
    BSTR *items;
    if (!strings || (count && FAILED(SafeArrayAccessData(strings, (void **)&items)))) {
        SafeArrayDestroy(*id_array);
        *id_array = nullptr;
        return E_OUTOFMEMORY;
    }
    for (int i = 0; i < count; i++)
        items[i] = SysAllocString(names[i]);
    if (count)
        SafeArrayUnaccessData(strings);
    *name_array = strings;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE A_PollForPotentialSupportedPatterns(void *p, IUnknown *element, SAFEARRAY **ids,
                                                                     SAFEARRAY **names)
{
    if (!element || !ids || !names)
        return E_POINTER;
    *ids = *names = nullptr;
    ELEMENT *e = nullptr;
    if (FAILED(ElementFromCom(element, &e)) || !e)
        return E_INVALIDARG;
    int found[64];
    const WCHAR *words[64];
    int n = 0;
    for (int id = 10000; id < 10036; id++) {
        PATTERN *pattern = nullptr;
        if (SUCCEEDED(NodeGetPattern(ElementNode(e), id, &pattern)) && pattern) {
            found[n] = id;
            words[n++] = UiaPatternName(id);
            PatternRelease(pattern);
        }
    }
    ElementRelease(e);
    return names_array(found, words, n, ids, names);
}

static HRESULT STDMETHODCALLTYPE A_PollForPotentialSupportedProperties(void *p, IUnknown *element, SAFEARRAY **ids,
                                                                       SAFEARRAY **names)
{
    if (!element || !ids || !names)
        return E_POINTER;
    *ids = *names = nullptr;
    ELEMENT *e = nullptr;
    if (FAILED(ElementFromCom(element, &e)) || !e)
        return E_INVALIDARG;
    int found[256];
    const WCHAR *words[256];
    int n = 0;
    for (int id = 30000; id <= 30175; id++) {
        const PROPERTY_INFO *info = UiaPropertyInfo(id);
        VARIANT v;
        if (!info)
            continue;
        if (SUCCEEDED(NodeGetProperty(ElementNode(e), id, TRUE, &v))) {
            if (!(v.vt == VT_UNKNOWN && v.punkVal == UiaNotSupportedValue())) {
                found[n] = id;
                words[n++] = info->name;
            }
            VariantClear(&v);
        }
    }
    ElementRelease(e);
    return names_array(found, words, n, ids, names);
}

static HRESULT STDMETHODCALLTYPE A_CheckNotSupported(void *p, VARIANT value, BOOL *out)
{
    if (!out)
        return E_POINTER;
    *out = (value.vt == VT_UNKNOWN || value.vt == VT_DISPATCH) && value.punkVal == UiaNotSupportedValue();
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE A_get_ReservedNotSupportedValue(void *p, IUnknown **out)
{
    if (!out)
        return E_POINTER;
    *out = UiaNotSupportedValue();
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE A_get_ReservedMixedAttributeValue(void *p, IUnknown **out)
{
    if (!out)
        return E_POINTER;
    *out = UiaMixedValue();
    return S_OK;
}

static HRESULT accessible_impl(IAccessible *acc, int child, IUnknown *request, void **out)
{
    if (!out || !acc)
        return E_POINTER;
    *out = nullptr;
    NODE *node = nullptr;
    HRESULT hr = ProxyNodeFromAccessible(acc, child, &node);
    if (FAILED(hr))
        return hr;
    hr = element_out(node, request, out);
    NodeRelease(node);
    return hr;
}

static HRESULT STDMETHODCALLTYPE A_ElementFromIAccessible(void *p, IAccessible *acc, int child, void **out)
{
    return accessible_impl(acc, child, nullptr, out);
}

static HRESULT STDMETHODCALLTYPE A_ElementFromIAccessibleBuildCache(void *p, IAccessible *acc, int child,
                                                                    IUnknown *request, void **out)
{
    return accessible_impl(acc, child, request, out);
}

static HRESULT STDMETHODCALLTYPE A_get_AutoSetFocus(void *p, BOOL *out)
{
    if (!out)
        return E_POINTER;
    *out = me(p)->auto_set_focus;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE A_put_AutoSetFocus(void *p, BOOL v)
{
    me(p)->auto_set_focus = v != 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE A_get_ConnectionTimeout(void *p, DWORD *out)
{
    if (!out)
        return E_POINTER;
    *out = me(p)->connection_timeout;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE A_put_ConnectionTimeout(void *p, DWORD v)
{
    me(p)->connection_timeout = v;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE A_get_TransactionTimeout(void *p, DWORD *out)
{
    if (!out)
        return E_POINTER;
    *out = me(p)->transaction_timeout;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE A_put_TransactionTimeout(void *p, DWORD v)
{
    me(p)->transaction_timeout = v;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE A_get_ConnectionRecoveryBehavior(void *p, int *out)
{
    if (!out)
        return E_POINTER;
    *out = me(p)->recovery;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE A_put_ConnectionRecoveryBehavior(void *p, int v)
{
    if ((unsigned)v > 1)
        return E_INVALIDARG;
    me(p)->recovery = v;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE A_get_CoalesceEvents(void *p, int *out)
{
    if (!out)
        return E_POINTER;
    *out = me(p)->coalesce;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE A_put_CoalesceEvents(void *p, int v)
{
    if ((unsigned)v > 1)
        return E_INVALIDARG;
    me(p)->coalesce = v;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE A_AddTextEditTextChangedEventHandler(void *p, IUnknown *element, int scope, int type,
                                                                      IUnknown *request, IUnknown *handler)
{
    if ((unsigned)(type - 1) > 3)
        return E_INVALIDARG;
    return register_handler(p, EK_TEXT_EDIT, UIA_TextEdit_TextChangedEventId, element, scope, request, handler,
                            nullptr, 0);
}
static HRESULT STDMETHODCALLTYPE A_RemoveTextEditTextChangedEventHandler(void *p, IUnknown *element, IUnknown *handler)
{
    return unregister_handler(p, EK_TEXT_EDIT, UIA_TextEdit_TextChangedEventId, element, handler);
}
static HRESULT STDMETHODCALLTYPE A_AddChangesEventHandler(void *p, IUnknown *element, int scope, int *types, int count,
                                                          IUnknown *request, IUnknown *handler)
{
    return register_handler(p, EK_CHANGES, UIA_ChangesEventId, element, scope, request, handler, nullptr, 0);
}
static HRESULT STDMETHODCALLTYPE A_RemoveChangesEventHandler(void *p, IUnknown *element, IUnknown *handler)
{
    return unregister_handler(p, EK_CHANGES, UIA_ChangesEventId, element, handler);
}
static HRESULT STDMETHODCALLTYPE A_AddNotificationEventHandler(void *p, IUnknown *element, int scope, IUnknown *request,
                                                               IUnknown *handler)
{
    return register_handler(p, EK_NOTIFICATION, UIA_NotificationEventId, element, scope, request, handler, nullptr, 0);
}
static HRESULT STDMETHODCALLTYPE A_RemoveNotificationEventHandler(void *p, IUnknown *element, IUnknown *handler)
{
    return unregister_handler(p, EK_NOTIFICATION, UIA_NotificationEventId, element, handler);
}
static HRESULT STDMETHODCALLTYPE A_AddActiveTextPositionChangedEventHandler(void *p, IUnknown *element, int scope,
                                                                            IUnknown *request, IUnknown *handler)
{
    return register_handler(p, EK_ACTIVE_TEXT, UIA_ActiveTextPositionChangedEventId, element, scope, request, handler,
                            nullptr, 0);
}
static HRESULT STDMETHODCALLTYPE A_RemoveActiveTextPositionChangedEventHandler(void *p, IUnknown *element,
                                                                               IUnknown *handler)
{
    return unregister_handler(p, EK_ACTIVE_TEXT, UIA_ActiveTextPositionChangedEventId, element, handler);
}

/* ------------------------------------------------------------------ event handler groups */

typedef struct GROUP_ITEM {
    struct GROUP_ITEM *next;
    int kind;
    int event_id;
    int scope;
    IUnknown *request;
    IUnknown *handler;
    int *props;
    int nprops;
} GROUP_ITEM;

typedef struct GROUP {
    const void *const *vtbl;
    LONG refs;
    GROUP_ITEM *items;
} GROUP;

static const void *const g_group_vtbl[];

static HRESULT STDMETHODCALLTYPE group_query(void *p, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IUIAutomationEventHandlerGroup))
        return E_NOINTERFACE;
    *out = p;
    InterlockedIncrement(&((GROUP *)p)->refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE group_add_ref(void *p)
{
    return (ULONG)InterlockedIncrement(&((GROUP *)p)->refs);
}

static ULONG STDMETHODCALLTYPE group_release(void *p)
{
    GROUP *g = p;
    LONG refs = InterlockedDecrement(&g->refs);
    if (!refs) {
        while (g->items) {
            GROUP_ITEM *i = g->items;
            g->items = i->next;
            if (i->request)
                i->request->lpVtbl->Release(i->request);
            i->handler->lpVtbl->Release(i->handler);
            UiaFree(i->props);
            UiaFree(i);
        }
        UiaFree(g);
    }
    return (ULONG)refs;
}

static HRESULT group_add(void *p, int kind, int event_id, int scope, IUnknown *request, IUnknown *handler,
                         const int *props, int nprops)
{
    if (!handler)
        return E_POINTER;
    GROUP *g = p;
    GROUP_ITEM *item = UiaAlloc(sizeof(*item));
    if (!item)
        return E_OUTOFMEMORY;
    item->kind = kind;
    item->event_id = event_id;
    item->scope = scope;
    item->request = request;
    if (request)
        request->lpVtbl->AddRef(request);
    item->handler = handler;
    handler->lpVtbl->AddRef(handler);
    if (nprops) {
        item->props = UiaAlloc((SIZE_T)nprops * sizeof(int));
        memcpy(item->props, props, (SIZE_T)nprops * sizeof(int));
        item->nprops = nprops;
    }
    item->next = g->items;
    g->items = item;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE G_AddActiveTextPositionChangedEventHandler(void *p, int scope, IUnknown *r, IUnknown *h)
{
    return group_add(p, EK_ACTIVE_TEXT, UIA_ActiveTextPositionChangedEventId, scope, r, h, nullptr, 0);
}
static HRESULT STDMETHODCALLTYPE G_AddAutomationEventHandler(void *p, int event, int scope, IUnknown *r, IUnknown *h)
{
    return group_add(p, EK_AUTOMATION, event, scope, r, h, nullptr, 0);
}
static HRESULT STDMETHODCALLTYPE G_AddChangesEventHandler(void *p, int scope, int *types, int count, IUnknown *r, IUnknown *h)
{
    return group_add(p, EK_CHANGES, UIA_ChangesEventId, scope, r, h, nullptr, 0);
}
static HRESULT STDMETHODCALLTYPE G_AddNotificationEventHandler(void *p, int scope, IUnknown *r, IUnknown *h)
{
    return group_add(p, EK_NOTIFICATION, UIA_NotificationEventId, scope, r, h, nullptr, 0);
}
static HRESULT STDMETHODCALLTYPE G_AddPropertyChangedEventHandler(void *p, int scope, IUnknown *r, IUnknown *h,
                                                                  PROPERTYID *props, int count)
{
    return group_add(p, EK_PROPERTY, UIA_AutomationPropertyChangedEventId, scope, r, h, props, count);
}
static HRESULT STDMETHODCALLTYPE G_AddStructureChangedEventHandler(void *p, int scope, IUnknown *r, IUnknown *h)
{
    return group_add(p, EK_STRUCTURE, UIA_StructureChangedEventId, scope, r, h, nullptr, 0);
}
static HRESULT STDMETHODCALLTYPE G_AddTextEditTextChangedEventHandler(void *p, int scope, int type, IUnknown *r, IUnknown *h)
{
    return group_add(p, EK_TEXT_EDIT, UIA_TextEdit_TextChangedEventId, scope, r, h, nullptr, 0);
}

#define GSLOT(name) (const void *)G_##name,
static const void *const g_group_vtbl[] = {(const void *)group_query, (const void *)group_add_ref,
                                           (const void *)group_release,
                                           IUIAutomationEventHandlerGroup_METHODS(GSLOT)};

static HRESULT STDMETHODCALLTYPE A_CreateEventHandlerGroup(void *p, IUnknown **out)
{
    if (!out)
        return E_POINTER;
    GROUP *g = UiaAlloc(sizeof(*g));
    if (!g)
        return E_OUTOFMEMORY;
    g->vtbl = g_group_vtbl;
    g->refs = 1;
    *out = (IUnknown *)g;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE A_AddEventHandlerGroup(void *p, IUnknown *element, IUnknown *group)
{
    if (!element || !group)
        return E_POINTER;
    GROUP *g = nullptr;
    if (FAILED(group->lpVtbl->QueryInterface(group, &IID_IUIAutomationEventHandlerGroup, (void **)&g)))
        return E_INVALIDARG;
    HRESULT hr = S_OK;
    for (GROUP_ITEM *i = g->items; i && SUCCEEDED(hr); i = i->next)
        hr = register_handler(p, i->kind, i->event_id, element, i->scope, i->request, i->handler, i->props, i->nprops);
    group_release(g);
    return hr;
}

static HRESULT STDMETHODCALLTYPE A_RemoveEventHandlerGroup(void *p, IUnknown *element, IUnknown *group)
{
    if (!element || !group)
        return E_POINTER;
    GROUP *g = nullptr;
    if (FAILED(group->lpVtbl->QueryInterface(group, &IID_IUIAutomationEventHandlerGroup, (void **)&g)))
        return E_INVALIDARG;
    for (GROUP_ITEM *i = g->items; i; i = i->next)
        unregister_handler(p, i->kind, i->event_id, element, i->handler);
    group_release(g);
    return S_OK;
}

#define ASLOT(name) (const void *)A_##name,
static const void *const g_auto_vtbl[] = {(const void *)auto_query, (const void *)auto_add_ref,
                                          (const void *)auto_release, IUIAutomation6_METHODS(ASLOT)};

/* ------------------------------------------------------------------ the class */

static HRESULT automation_create(REFIID riid, void **out)
{
    AUTO *a = UiaAlloc(sizeof(*a));
    if (!a)
        return E_OUTOFMEMORY;
    a->vtbl = g_auto_vtbl;
    a->refs = 1;
    a->auto_set_focus = TRUE;
    a->connection_timeout = 2000;
    a->transaction_timeout = 20000;
    InitializeCriticalSection(&a->lock);
    HRESULT hr = auto_query(a, riid, out);
    auto_release(a);
    return hr;
}

typedef struct FACTORY {
    IClassFactory iface;
} FACTORY;

static HRESULT STDMETHODCALLTYPE factory_query(IClassFactory *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory)) {
        *out = iface;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE factory_add_ref(IClassFactory *iface)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE factory_release(IClassFactory *iface)
{
    return 1;
}

static HRESULT STDMETHODCALLTYPE factory_create(IClassFactory *iface, IUnknown *outer, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    return automation_create(riid, out);
}

static HRESULT STDMETHODCALLTYPE factory_lock(IClassFactory *iface, BOOL lock)
{
    return S_OK;
}

static const IClassFactoryVtbl g_factory_vtbl = {factory_query, factory_add_ref, factory_release, factory_create,
                                                 factory_lock};
static IClassFactory g_factory = {&g_factory_vtbl};

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!IsEqualIID(clsid, &CLSID_CUIAutomation) && !IsEqualIID(clsid, &CLSID_CUIAutomation8))
        return CLASS_E_CLASSNOTAVAILABLE;
    return factory_query(&g_factory, riid, out);
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}

HRESULT WINAPI DllRegisterServer(void)
{
    return S_OK;
}

HRESULT WINAPI DllUnregisterServer(void)
{
    return S_OK;
}

HRESULT WINAPI DllGetActivationFactory(void *class_id, void **factory)
{
    if (factory)
        *factory = nullptr;
    return CLASS_E_CLASSNOTAVAILABLE;
}
