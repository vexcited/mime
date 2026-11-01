/*
 * the objects of conditions, cache requests and tree walkers that clients create.
 */
#include "uia.h"

const IID IID_IMimeCondition = {0x4d494d45, 0x5541, 0x434f, {0x4e, 0x44, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01}};


typedef struct CONDOBJ {
    const void *const *vtbl;
    LONG refs;
    COND *cond;
} CONDOBJ;

static const void *const g_bool_vtbl[], *const g_property_vtbl[], *const g_and_vtbl[], *const g_or_vtbl[],
    *const g_not_vtbl[];

static HRESULT STDMETHODCALLTYPE cond_query(void *p, REFIID riid, void **out)
{
    CONDOBJ *o = p;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    int type = CondType(o->cond);
    BOOL ok = IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IUIAutomationCondition) ||
              IsEqualIID(riid, &IID_IMimeCondition);
    ok = ok || ((type == ConditionType_True || type == ConditionType_False) &&
                IsEqualIID(riid, &IID_IUIAutomationBoolCondition));
    ok = ok || (type == ConditionType_Property && IsEqualIID(riid, &IID_IUIAutomationPropertyCondition));
    ok = ok || (type == ConditionType_And && IsEqualIID(riid, &IID_IUIAutomationAndCondition));
    ok = ok || (type == ConditionType_Or && IsEqualIID(riid, &IID_IUIAutomationOrCondition));
    ok = ok || (type == ConditionType_Not && IsEqualIID(riid, &IID_IUIAutomationNotCondition));
    if (!ok)
        return E_NOINTERFACE;
    *out = p;
    InterlockedIncrement(&o->refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE cond_add_ref(void *p)
{
    return (ULONG)InterlockedIncrement(&((CONDOBJ *)p)->refs);
}

static ULONG STDMETHODCALLTYPE cond_release(void *p)
{
    CONDOBJ *o = p;
    LONG refs = InterlockedDecrement(&o->refs);
    if (!refs) {
        CondRelease(o->cond);
        UiaFree(o);
    }
    return (ULONG)refs;
}

HRESULT CondToCom(COND *c, IUnknown **out)
{
    const void *const *vtbl;
    switch (CondType(c)) {
    case ConditionType_True:
    case ConditionType_False: vtbl = g_bool_vtbl; break;
    case ConditionType_Property: vtbl = g_property_vtbl; break;
    case ConditionType_And: vtbl = g_and_vtbl; break;
    case ConditionType_Or: vtbl = g_or_vtbl; break;
    default: vtbl = g_not_vtbl; break;
    }
    CONDOBJ *o = UiaAlloc(sizeof(*o));
    if (!o)
        return E_OUTOFMEMORY;
    o->vtbl = vtbl;
    o->refs = 1;
    o->cond = c;
    CondAddRef(c);
    *out = (IUnknown *)o;
    return S_OK;
}

HRESULT CondFromCom(IUnknown *unknown, COND **out)
{
    *out = nullptr;
    CONDOBJ *o = nullptr;
    if (!unknown || FAILED(unknown->lpVtbl->QueryInterface(unknown, &IID_IMimeCondition, (void **)&o)))
        return E_INVALIDARG;
    *out = o->cond;
    CondAddRef(o->cond);
    cond_release(o);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE B_get_BooleanValue(void *p, BOOL *out)
{
    if (!out)
        return E_POINTER;
    *out = CondType(((CONDOBJ *)p)->cond) == ConditionType_True;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE P_get_PropertyId(void *p, int *out)
{
    if (!out)
        return E_POINTER;
    *out = CondProperty(((CONDOBJ *)p)->cond);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE P_get_PropertyValue(void *p, VARIANT *out)
{
    if (!out)
        return E_POINTER;
    VariantInit(out);
    return VariantCopy(out, (VARIANT *)CondValue(((CONDOBJ *)p)->cond));
}

static HRESULT STDMETHODCALLTYPE P_get_PropertyConditionFlags(void *p, int *out)
{
    if (!out)
        return E_POINTER;
    *out = CondFlags(((CONDOBJ *)p)->cond);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE G_get_ChildCount(void *p, int *out)
{
    if (!out)
        return E_POINTER;
    *out = CondCount(((CONDOBJ *)p)->cond);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE G_GetChildrenAsNativeArray(void *p, IUnknown ***out, int *count)
{
    if (!out || !count)
        return E_POINTER;
    *out = nullptr;
    COND *c = ((CONDOBJ *)p)->cond;
    int n = CondCount(c);
    *count = n;
    if (!n)
        return S_OK;
    IUnknown **items = CoTaskMemAlloc((SIZE_T)n * sizeof(*items));
    if (!items)
        return E_OUTOFMEMORY;
    for (int i = 0; i < n; i++) {
        HRESULT hr = CondToCom(CondChild(c, i), &items[i]);
        if (FAILED(hr)) {
            for (int j = 0; j < i; j++)
                items[j]->lpVtbl->Release(items[j]);
            CoTaskMemFree(items);
            return hr;
        }
    }
    *out = items;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE G_GetChildren(void *p, SAFEARRAY **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    COND *c = ((CONDOBJ *)p)->cond;
    int n = CondCount(c);
    SAFEARRAY *array = SafeArrayCreateVector(VT_UNKNOWN, 0, (ULONG)n);
    if (!array)
        return E_OUTOFMEMORY;
    IUnknown **items;
    if (n && SUCCEEDED(SafeArrayAccessData(array, (void **)&items))) {
        for (int i = 0; i < n; i++)
            CondToCom(CondChild(c, i), &items[i]);
        SafeArrayUnaccessData(array);
    }
    *out = array;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE N_GetChild(void *p, IUnknown **out)
{
    if (!out)
        return E_POINTER;
    return CondToCom(CondChild(((CONDOBJ *)p)->cond, 0), out);
}

#define CSLOT(name) (const void *)name,
static const void *const g_bool_vtbl[] = {(const void *)cond_query, (const void *)cond_add_ref,
                                          (const void *)cond_release, (const void *)B_get_BooleanValue};
static const void *const g_property_vtbl[] = {(const void *)cond_query,
                                              (const void *)cond_add_ref,
                                              (const void *)cond_release,
                                              (const void *)P_get_PropertyId,
                                              (const void *)P_get_PropertyValue,
                                              (const void *)P_get_PropertyConditionFlags};
static const void *const g_and_vtbl[] = {(const void *)cond_query,
                                         (const void *)cond_add_ref,
                                         (const void *)cond_release,
                                         (const void *)G_get_ChildCount,
                                         (const void *)G_GetChildrenAsNativeArray,
                                         (const void *)G_GetChildren};
static const void *const g_or_vtbl[] = {(const void *)cond_query,
                                        (const void *)cond_add_ref,
                                        (const void *)cond_release,
                                        (const void *)G_get_ChildCount,
                                        (const void *)G_GetChildrenAsNativeArray,
                                        (const void *)G_GetChildren};
static const void *const g_not_vtbl[] = {(const void *)cond_query, (const void *)cond_add_ref,
                                         (const void *)cond_release, (const void *)N_GetChild};

/* ------------------------------------------------------------------ cache requests */

typedef struct REQUESTOBJ {
    const void *const *vtbl;
    LONG refs;
    REQUEST request;
} REQUESTOBJ;

static const void *const g_request_vtbl[];
const IID IID_IMimeRequest = {0x4d494d45, 0x5541, 0x5251, {0x52, 0x45, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01}};

static HRESULT STDMETHODCALLTYPE req_query(void *p, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IUIAutomationCacheRequest) &&
        !IsEqualIID(riid, &IID_IMimeRequest))
        return E_NOINTERFACE;
    *out = p;
    InterlockedIncrement(&((REQUESTOBJ *)p)->refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE req_add_ref(void *p)
{
    return (ULONG)InterlockedIncrement(&((REQUESTOBJ *)p)->refs);
}

static ULONG STDMETHODCALLTYPE req_release(void *p)
{
    REQUESTOBJ *o = p;
    LONG refs = InterlockedDecrement(&o->refs);
    if (!refs) {
        RequestClear(&o->request);
        UiaFree(o);
    }
    return (ULONG)refs;
}

static HRESULT request_object(REQUESTOBJ **out)
{
    REQUESTOBJ *o = UiaAlloc(sizeof(*o));
    if (!o)
        return E_OUTOFMEMORY;
    o->vtbl = g_request_vtbl;
    o->refs = 1;
    RequestInit(&o->request);
    *out = o;
    return S_OK;
}

/* the control view is what a request filters by until told otherwise. */
HRESULT ControlViewCondition(COND **out)
{
    VARIANT yes;
    yes.vt = VT_BOOL;
    yes.boolVal = VARIANT_TRUE;
    return CondProperty_(UIA_IsControlElementPropertyId, &yes, 0, out);
}

HRESULT ContentViewCondition(COND **out)
{
    VARIANT yes;
    yes.vt = VT_BOOL;
    yes.boolVal = VARIANT_TRUE;
    COND *items[2] = {};
    HRESULT hr = CondProperty_(UIA_IsControlElementPropertyId, &yes, 0, &items[0]);
    if (SUCCEEDED(hr))
        hr = CondProperty_(UIA_IsContentElementPropertyId, &yes, 0, &items[1]);
    if (SUCCEEDED(hr))
        hr = CondGroup(ConditionType_And, items, 2, out);
    CondRelease(items[0]);
    CondRelease(items[1]);
    return hr;
}

HRESULT RequestCreate(IUnknown **out)
{
    REQUESTOBJ *o;
    HRESULT hr = request_object(&o);
    if (FAILED(hr))
        return hr;
    hr = ControlViewCondition(&o->request.view);
    if (FAILED(hr)) {
        req_release(o);
        return hr;
    }
    *out = (IUnknown *)o;
    return S_OK;
}

HRESULT RequestFromCom(IUnknown *unknown, REQUEST *out)
{
    REQUESTOBJ *o = nullptr;
    if (!unknown || FAILED(unknown->lpVtbl->QueryInterface(unknown, &IID_IMimeRequest, (void **)&o)))
        return E_INVALIDARG;
    HRESULT hr = RequestCopy(out, &o->request);
    req_release(o);
    return hr;
}

static HRESULT STDMETHODCALLTYPE R_AddProperty(void *p, int id)
{
    REQUESTOBJ *o = p;
    if (!UiaPropertyInfo(id))
        return E_INVALIDARG;
    for (int i = 0; i < o->request.nprops; i++)
        if (o->request.props[i] == id)
            return S_OK;
    int *bigger = UiaAlloc(((SIZE_T)o->request.nprops + 1) * sizeof(int));
    if (!bigger)
        return E_OUTOFMEMORY;
    memcpy(bigger, o->request.props, (SIZE_T)o->request.nprops * sizeof(int));
    bigger[o->request.nprops] = id;
    UiaFree(o->request.props);
    o->request.props = bigger;
    o->request.nprops++;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE R_AddPattern(void *p, int id)
{
    REQUESTOBJ *o = p;
    if (!UiaPatternName(id))
        return E_INVALIDARG;
    for (int i = 0; i < o->request.npatterns; i++)
        if (o->request.patterns[i] == id)
            return S_OK;
    int *bigger = UiaAlloc(((SIZE_T)o->request.npatterns + 1) * sizeof(int));
    if (!bigger)
        return E_OUTOFMEMORY;
    memcpy(bigger, o->request.patterns, (SIZE_T)o->request.npatterns * sizeof(int));
    bigger[o->request.npatterns] = id;
    UiaFree(o->request.patterns);
    o->request.patterns = bigger;
    o->request.npatterns++;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE R_Clone(void *p, IUnknown **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    REQUESTOBJ *o;
    HRESULT hr = request_object(&o);
    if (FAILED(hr))
        return hr;
    RequestClear(&o->request);
    hr = RequestCopy(&o->request, &((REQUESTOBJ *)p)->request);
    if (FAILED(hr)) {
        req_release(o);
        return hr;
    }
    *out = (IUnknown *)o;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE R_get_TreeScope(void *p, int *out)
{
    if (!out)
        return E_POINTER;
    *out = ((REQUESTOBJ *)p)->request.scope;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE R_put_TreeScope(void *p, int scope)
{
    if (scope == TreeScope_None || (scope & 0xFFFFFFF8) != 0)
        return E_INVALIDARG;
    ((REQUESTOBJ *)p)->request.scope = scope;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE R_get_TreeFilter(void *p, IUnknown **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    COND *view = ((REQUESTOBJ *)p)->request.view;
    if (!view)
        return S_OK;
    return CondToCom(view, out);
}

static HRESULT STDMETHODCALLTYPE R_put_TreeFilter(void *p, IUnknown *filter)
{
    if (!filter)
        return E_POINTER;
    COND *c;
    HRESULT hr = CondFromCom(filter, &c);
    if (FAILED(hr))
        return hr;
    REQUESTOBJ *o = p;
    CondRelease(o->request.view);
    o->request.view = c;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE R_get_AutomationElementMode(void *p, int *out)
{
    if (!out)
        return E_POINTER;
    *out = ((REQUESTOBJ *)p)->request.mode;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE R_put_AutomationElementMode(void *p, int mode)
{
    if ((unsigned)mode > 1)
        return E_INVALIDARG;
    ((REQUESTOBJ *)p)->request.mode = mode;
    return S_OK;
}

#define RSLOT(name) (const void *)R_##name,
static const void *const g_request_vtbl[] = {(const void *)req_query, (const void *)req_add_ref,
                                             (const void *)req_release, IUIAutomationCacheRequest_METHODS(RSLOT)};

/* ------------------------------------------------------------------ tree walkers */

typedef struct WALKER {
    const void *const *vtbl;
    LONG refs;
    COND *cond;
} WALKER;

static const void *const g_walker_vtbl[];

static HRESULT STDMETHODCALLTYPE walker_query(void *p, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IUIAutomationTreeWalker))
        return E_NOINTERFACE;
    *out = p;
    InterlockedIncrement(&((WALKER *)p)->refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE walker_add_ref(void *p)
{
    return (ULONG)InterlockedIncrement(&((WALKER *)p)->refs);
}

static ULONG STDMETHODCALLTYPE walker_release(void *p)
{
    WALKER *w = p;
    LONG refs = InterlockedDecrement(&w->refs);
    if (!refs) {
        CondRelease(w->cond);
        UiaFree(w);
    }
    return (ULONG)refs;
}

HRESULT WalkerCreate(COND *cond, IUnknown **out)
{
    WALKER *w = UiaAlloc(sizeof(*w));
    if (!w)
        return E_OUTOFMEMORY;
    w->vtbl = g_walker_vtbl;
    w->refs = 1;
    w->cond = cond;
    CondAddRef(cond);
    *out = (IUnknown *)w;
    return S_OK;
}

static HRESULT walk(void *p, IUnknown *element, IUnknown *request, int direction, BOOL normalize, void **out)
{
    WALKER *w = p;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    ELEMENT *e = nullptr;
    if (!element)
        return E_POINTER;
    if (FAILED(ElementFromCom(element, &e)) || !e)
        return E_INVALIDARG;
    REQUEST req;
    BOOL have = FALSE;
    HRESULT hr = S_OK;
    if (request) {
        hr = RequestFromCom(request, &req);
        have = SUCCEEDED(hr);
    }
    NODE *found = nullptr;
    if (SUCCEEDED(hr)) {
        if (normalize) {
            BOOL match;
            hr = CondMatch(w->cond, ElementNode(e), &match);
            if (SUCCEEDED(hr)) {
                if (match) {
                    found = ElementNode(e);
                    NodeAddRef(found);
                } else {
                    hr = ViewParent(ElementNode(e), w->cond, &found);
                }
            }
        } else {
            hr = ViewStep(ElementNode(e), w->cond, direction, &found);
        }
    }
    if (SUCCEEDED(hr) && found) {
        hr = ElementCreate(found, have ? &req : nullptr, (ELEMENT **)out);
        NodeRelease(found);
    }
    if (have)
        RequestClear(&req);
    ElementRelease(e);
    return hr;
}

static HRESULT STDMETHODCALLTYPE W_GetParentElement(void *p, IUnknown *e, void **out)
{
    return walk(p, e, nullptr, NavigateDirection_Parent, FALSE, out);
}

static HRESULT STDMETHODCALLTYPE W_GetFirstChildElement(void *p, IUnknown *e, void **out)
{
    return walk(p, e, nullptr, NavigateDirection_FirstChild, FALSE, out);
}

static HRESULT STDMETHODCALLTYPE W_GetLastChildElement(void *p, IUnknown *e, void **out)
{
    return walk(p, e, nullptr, NavigateDirection_LastChild, FALSE, out);
}

static HRESULT STDMETHODCALLTYPE W_GetNextSiblingElement(void *p, IUnknown *e, void **out)
{
    return walk(p, e, nullptr, NavigateDirection_NextSibling, FALSE, out);
}

static HRESULT STDMETHODCALLTYPE W_GetPreviousSiblingElement(void *p, IUnknown *e, void **out)
{
    return walk(p, e, nullptr, NavigateDirection_PreviousSibling, FALSE, out);
}

static HRESULT STDMETHODCALLTYPE W_NormalizeElement(void *p, IUnknown *e, void **out)
{
    return walk(p, e, nullptr, 0, TRUE, out);
}

static HRESULT STDMETHODCALLTYPE W_GetParentElementBuildCache(void *p, IUnknown *e, IUnknown *r, void **out)
{
    return walk(p, e, r, NavigateDirection_Parent, FALSE, out);
}

static HRESULT STDMETHODCALLTYPE W_GetFirstChildElementBuildCache(void *p, IUnknown *e, IUnknown *r, void **out)
{
    return walk(p, e, r, NavigateDirection_FirstChild, FALSE, out);
}

static HRESULT STDMETHODCALLTYPE W_GetLastChildElementBuildCache(void *p, IUnknown *e, IUnknown *r, void **out)
{
    return walk(p, e, r, NavigateDirection_LastChild, FALSE, out);
}

static HRESULT STDMETHODCALLTYPE W_GetNextSiblingElementBuildCache(void *p, IUnknown *e, IUnknown *r, void **out)
{
    return walk(p, e, r, NavigateDirection_NextSibling, FALSE, out);
}

static HRESULT STDMETHODCALLTYPE W_GetPreviousSiblingElementBuildCache(void *p, IUnknown *e, IUnknown *r, void **out)
{
    return walk(p, e, r, NavigateDirection_PreviousSibling, FALSE, out);
}

static HRESULT STDMETHODCALLTYPE W_NormalizeElementBuildCache(void *p, IUnknown *e, IUnknown *r, void **out)
{
    return walk(p, e, r, 0, TRUE, out);
}

static HRESULT STDMETHODCALLTYPE W_get_Condition(void *p, IUnknown **out)
{
    if (!out)
        return E_POINTER;
    return CondToCom(((WALKER *)p)->cond, out);
}

#define WSLOT(name) (const void *)W_##name,
static const void *const g_walker_vtbl[] = {(const void *)walker_query, (const void *)walker_add_ref,
                                            (const void *)walker_release, IUIAutomationTreeWalker_METHODS(WSLOT)};
