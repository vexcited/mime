/*
 * the elements clients hold: a node with what was fetched with it, and the interfaces of IUIAutomationElement.
 */
#include "uia.h"

const IID IID_IMimeElement = {0x4d494d45, 0x5541, 0x454c, {0x45, 0x4d, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01}};

typedef struct CACHE {
    int nprops;
    int *prop_ids;
    VARIANT *props;
    HRESULT *prop_hr;
    int npatterns;
    int *pattern_ids;
    PATTERN **patterns;
    ELEMENT *parent;
    ELEMENT **children;
    int nchildren;
    BOOL has_children;
} CACHE;

struct ELEMENT {
    const void *const *vtbl;
    LONG refs;
    NODE *node;
    CACHE *cache;
    int mode;
};

typedef struct ELEMENT_ARRAY {
    const void *const *vtbl;
    LONG refs;
    ELEMENT **items;
    int count;
} ELEMENT_ARRAY;

static const void *const g_element_vtbl[];
static const void *const g_array_vtbl[];

/* ------------------------------------------------------------------ requests */

void RequestInit(REQUEST *r)
{
    memset(r, 0, sizeof(*r));
    r->scope = TreeScope_Element;
    r->mode = AutomationElementMode_Full;
}

void RequestClear(REQUEST *r)
{
    CondRelease(r->view);
    UiaFree(r->props);
    UiaFree(r->patterns);
    RequestInit(r);
}

HRESULT RequestCopy(REQUEST *to, const REQUEST *from)
{
    RequestInit(to);
    to->scope = from->scope;
    to->mode = from->mode;
    to->view = from->view;
    CondAddRef(to->view);
    if (from->nprops) {
        to->props = UiaAlloc((SIZE_T)from->nprops * sizeof(int));
        if (!to->props)
            return E_OUTOFMEMORY;
        memcpy(to->props, from->props, (SIZE_T)from->nprops * sizeof(int));
        to->nprops = from->nprops;
    }
    if (from->npatterns) {
        to->patterns = UiaAlloc((SIZE_T)from->npatterns * sizeof(int));
        if (!to->patterns)
            return E_OUTOFMEMORY;
        memcpy(to->patterns, from->patterns, (SIZE_T)from->npatterns * sizeof(int));
        to->npatterns = from->npatterns;
    }
    return S_OK;
}

HRESULT RequestFromStruct(const UiaCacheRequest *in, REQUEST *out)
{
    RequestInit(out);
    if (!in)
        return E_INVALIDARG;
    out->scope = in->Scope;
    out->mode = in->automationElementMode;
    if (in->pViewCondition) {
        HRESULT hr = CondFromStruct(in->pViewCondition, &out->view);
        if (FAILED(hr))
            return hr;
    }
    if (in->cProperties > 0) {
        out->props = UiaAlloc((SIZE_T)in->cProperties * sizeof(int));
        if (!out->props)
            return E_OUTOFMEMORY;
        memcpy(out->props, in->pProperties, (SIZE_T)in->cProperties * sizeof(int));
        out->nprops = in->cProperties;
    }
    if (in->cPatterns > 0) {
        out->patterns = UiaAlloc((SIZE_T)in->cPatterns * sizeof(int));
        if (!out->patterns)
            return E_OUTOFMEMORY;
        memcpy(out->patterns, in->pPatterns, (SIZE_T)in->cPatterns * sizeof(int));
        out->npatterns = in->cPatterns;
    }
    return S_OK;
}

/* ------------------------------------------------------------------ the cache */

static void cache_free(CACHE *c)
{
    if (!c)
        return;
    for (int i = 0; i < c->nprops; i++)
        VariantClear(&c->props[i]);
    UiaFree(c->prop_ids);
    UiaFree(c->props);
    UiaFree(c->prop_hr);
    for (int i = 0; i < c->npatterns; i++)
        PatternRelease(c->patterns[i]);
    UiaFree(c->pattern_ids);
    UiaFree(c->patterns);
    ElementRelease(c->parent);
    for (int i = 0; i < c->nchildren; i++)
        ElementRelease(c->children[i]);
    UiaFree(c->children);
    UiaFree(c);
}

static HRESULT element_make(NODE *node, ELEMENT **out)
{
    ELEMENT *e = UiaAlloc(sizeof(*e));
    if (!e)
        return E_OUTOFMEMORY;
    e->vtbl = g_element_vtbl;
    e->refs = 1;
    e->node = node;
    NodeAddRef(node);
    e->mode = AutomationElementMode_Full;
    *out = e;
    return S_OK;
}

static HRESULT fill_cache(ELEMENT *e, const REQUEST *r)
{
    CACHE *c = UiaAlloc(sizeof(*c));
    if (!c)
        return E_OUTOFMEMORY;
    e->cache = c;
    e->mode = r->mode;
    if (r->nprops) {
        c->prop_ids = UiaAlloc((SIZE_T)r->nprops * sizeof(int));
        c->props = UiaAlloc((SIZE_T)r->nprops * sizeof(VARIANT));
        c->prop_hr = UiaAlloc((SIZE_T)r->nprops * sizeof(HRESULT));
        if (!c->prop_ids || !c->props || !c->prop_hr)
            return E_OUTOFMEMORY;
        c->nprops = r->nprops;
        for (int i = 0; i < r->nprops; i++) {
            c->prop_ids[i] = r->props[i];
            VariantInit(&c->props[i]);
            c->prop_hr[i] = UiaPropertyInfo(r->props[i]) ? NodeGetProperty(e->node, r->props[i], TRUE, &c->props[i])
                                                          : E_INVALIDARG;
            if (c->prop_hr[i] == E_INVALIDARG)
                return E_INVALIDARG;
        }
    }
    if (r->npatterns) {
        c->pattern_ids = UiaAlloc((SIZE_T)r->npatterns * sizeof(int));
        c->patterns = UiaAlloc((SIZE_T)r->npatterns * sizeof(PATTERN *));
        if (!c->pattern_ids || !c->patterns)
            return E_OUTOFMEMORY;
        c->npatterns = r->npatterns;
        for (int i = 0; i < r->npatterns; i++) {
            c->pattern_ids[i] = r->patterns[i];
            NodeGetPattern(e->node, r->patterns[i], &c->patterns[i]);
        }
    }
    return S_OK;
}

static HRESULT cache_tree(ELEMENT *e, const REQUEST *r, int scope_left)
{
    HRESULT hr = fill_cache(e, r);
    if (FAILED(hr))
        return hr;
    if (scope_left & (TreeScope_Children | TreeScope_Descendants)) {
        NODE **list;
        int count;
        hr = ViewChildren(e->node, r->view, &list, &count);
        if (FAILED(hr))
            return hr;
        e->cache->children = count ? UiaAlloc((SIZE_T)count * sizeof(ELEMENT *)) : nullptr;
        e->cache->has_children = TRUE;
        for (int i = 0; SUCCEEDED(hr) && i < count; i++) {
            ELEMENT *child;
            hr = element_make(list[i], &child);
            if (SUCCEEDED(hr)) {
                e->cache->children[e->cache->nchildren++] = child;
                hr = cache_tree(child, r, scope_left & TreeScope_Descendants ? TreeScope_Children | TreeScope_Descendants : 0);
            }
        }
        NodeListFree(list, count);
    }
    return hr;
}

HRESULT ElementCreate(NODE *node, const REQUEST *request, ELEMENT **out)
{
    *out = nullptr;
    ELEMENT *e;
    HRESULT hr = element_make(node, &e);
    if (FAILED(hr))
        return hr;
    if (request) {
        hr = cache_tree(e, request, request->scope);
        if (SUCCEEDED(hr) && (request->scope & TreeScope_Parent)) {
            NODE *parent = nullptr;
            hr = ViewParent(node, request->view, &parent);
            if (SUCCEEDED(hr) && parent) {
                ELEMENT *pe;
                hr = element_make(parent, &pe);
                if (SUCCEEDED(hr)) {
                    hr = fill_cache(pe, request);
                    e->cache->parent = pe;
                }
                NodeRelease(parent);
            }
        }
    }
    if (FAILED(hr)) {
        ElementRelease(e);
        return hr;
    }
    *out = e;
    return S_OK;
}

NODE *ElementNode(ELEMENT *e)
{
    return e->node;
}

void ElementAddRef(ELEMENT *e)
{
    if (e)
        InterlockedIncrement(&e->refs);
}

void ElementRelease(ELEMENT *e)
{
    if (e && !InterlockedDecrement(&e->refs)) {
        cache_free(e->cache);
        NodeRelease(e->node);
        UiaFree(e);
    }
}

HRESULT ElementsFromNodes(NODE **nodes, int count, const REQUEST *request, ELEMENT ***items)
{
    *items = count ? UiaAlloc((SIZE_T)count * sizeof(ELEMENT *)) : nullptr;
    if (count && !*items)
        return E_OUTOFMEMORY;
    for (int i = 0; i < count; i++) {
        HRESULT hr = ElementCreate(nodes[i], request, &(*items)[i]);
        if (FAILED(hr)) {
            for (int j = 0; j < i; j++)
                ElementRelease((*items)[j]);
            UiaFree(*items);
            *items = nullptr;
            return hr;
        }
    }
    return S_OK;
}

HRESULT ElementCachedValue(ELEMENT *e, PROPERTYID id, VARIANT *out)
{
    VariantInit(out);
    CACHE *c = e->cache;
    if (c)
        for (int i = 0; i < c->nprops; i++)
            if (c->prop_ids[i] == id) {
                if (FAILED(c->prop_hr[i]))
                    return c->prop_hr[i];
                return VariantCopy(out, &c->props[i]);
            }
    return E_INVALIDARG;
}

HRESULT ElementCachedPattern(ELEMENT *e, PATTERNID id, PATTERN **out, BOOL *cached)
{
    *out = nullptr;
    *cached = FALSE;
    CACHE *c = e->cache;
    if (c)
        for (int i = 0; i < c->npatterns; i++)
            if (c->pattern_ids[i] == id) {
                *cached = TRUE;
                *out = c->patterns[i];
                PatternAddRef(*out);
                return S_OK;
            }
    return S_OK;
}

HRESULT ElementFromCom(IUnknown *unknown, ELEMENT **out)
{
    *out = nullptr;
    if (!unknown)
        return S_OK;
    return unknown->lpVtbl->QueryInterface(unknown, &IID_IMimeElement, (void **)out);
}

/* ------------------------------------------------------------------ values for clients */

static HRESULT wrap_node(IUnknown *unknown, const REQUEST *request, IUnknown **out)
{
    *out = nullptr;
    if (!unknown)
        return S_OK;
    NODE *node = nullptr;
    HRESULT hr = NodeFromUnknown(unknown, &node);
    if (FAILED(hr) || !node)
        return hr;
    ELEMENT *e;
    hr = ElementCreate(node, request, &e);
    NodeRelease(node);
    *out = (IUnknown *)e;
    return hr;
}

HRESULT ValueToElements(VARIANT *v, const REQUEST *request)
{
    if (v->vt == VT_UNKNOWN && v->punkVal) {
        IUnknown *e = nullptr;
        if (v->punkVal == UiaNotSupportedValue() || v->punkVal == UiaMixedValue())
            return S_OK;
        HRESULT hr = wrap_node(v->punkVal, request, &e);
        v->punkVal->lpVtbl->Release(v->punkVal);
        v->punkVal = e;
        return hr;
    }
    if (v->vt == (VT_ARRAY | VT_UNKNOWN) && v->parray) {
        LONG lower, upper;
        SafeArrayGetLBound(v->parray, 1, &lower);
        SafeArrayGetUBound(v->parray, 1, &upper);
        IUnknown **items;
        if (upper >= lower && SUCCEEDED(SafeArrayAccessData(v->parray, (void **)&items))) {
            for (int i = 0; i <= upper - lower; i++) {
                IUnknown *e = nullptr;
                wrap_node(items[i], request, &e);
                if (items[i])
                    items[i]->lpVtbl->Release(items[i]);
                items[i] = e;
            }
            SafeArrayUnaccessData(v->parray);
        }
    }
    return S_OK;
}

/* ------------------------------------------------------------------ the element */

static ELEMENT *self(void *p)
{
    return (ELEMENT *)p;
}

static HRESULT STDMETHODCALLTYPE elem_query(void *p, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IMimeElement) ||
        IsEqualIID(riid, &IID_IUIAutomationElement) || IsEqualIID(riid, &IID_IUIAutomationElement2) ||
        IsEqualIID(riid, &IID_IUIAutomationElement3) || IsEqualIID(riid, &IID_IUIAutomationElement4) ||
        IsEqualIID(riid, &IID_IUIAutomationElement5) || IsEqualIID(riid, &IID_IUIAutomationElement6) ||
        IsEqualIID(riid, &IID_IUIAutomationElement7) || IsEqualIID(riid, &IID_IUIAutomationElement8) ||
        IsEqualIID(riid, &IID_IUIAutomationElement9)) {
        *out = p;
        ElementAddRef(self(p));
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE elem_add_ref(void *p)
{
    return (ULONG)InterlockedIncrement(&self(p)->refs);
}

static ULONG STDMETHODCALLTYPE elem_release(void *p)
{
    LONG refs = self(p)->refs - 1;
    ElementRelease(self(p));
    return (ULONG)refs;
}

/* a property now: the node's answer with the defaults of the schema. */
static HRESULT current_value(ELEMENT *e, PROPERTYID id, BOOL ignore_default, VARIANT *out)
{
    VariantInit(out);
    if (!UiaPropertyInfo(id) && id < 30000)
        return E_INVALIDARG;
    return NodeGetProperty(e->node, id, ignore_default, out);
}

static HRESULT cached_value(ELEMENT *e, PROPERTYID id, BOOL ignore_default, VARIANT *out)
{
    HRESULT hr = ElementCachedValue(e, id, out);
    if (FAILED(hr))
        return hr;
    const PROPERTY_INFO *info = UiaPropertyInfo(id);
    if (!ignore_default && out->vt == VT_UNKNOWN && out->punkVal == UiaNotSupportedValue() && info) {
        VariantClear(out);
        return UiaDefaultValue(info->default_kind, out);
    }
    return S_OK;
}

HRESULT ElementValueEx(ELEMENT *e, PROPERTYID id, BOOL current, BOOL ignore_default, VARIANT *out);
static HRESULT value_of(ELEMENT *e, PROPERTYID id, BOOL current, BOOL ignore_default, VARIANT *out)
{
    return ElementValueEx(e, id, current, ignore_default, out);
}

HRESULT ElementValueEx(ELEMENT *e, PROPERTYID id, BOOL current, BOOL ignore_default, VARIANT *out)
{
    HRESULT hr = current ? current_value(e, id, ignore_default, out) : cached_value(e, id, ignore_default, out);
    if (SUCCEEDED(hr)) {
        hr = ValueToElements(out, nullptr);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE E_SetFocus(void *p)
{
    return self(p)->node->ops->set_focus(self(p)->node);
}

static HRESULT STDMETHODCALLTYPE E_GetRuntimeId(void *p, SAFEARRAY **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    int *ids = nullptr, count = 0;
    HRESULT hr = NodeRuntimeId(self(p)->node, &ids, &count);
    if (FAILED(hr))
        return hr;
    if (!count) {
        ids = UiaAlloc(2 * sizeof(int));
        ids[0] = 0x7FFFFFFF;
        ids[1] = (int)(ULONG_PTR)self(p)->node;
        count = 2;
    }
    hr = UiaIntArrayToSafeArray(ids, count, out);
    UiaFree(ids);
    return hr;
}


static BOOL scope_ok(int scope)
{
    return scope > 0 && scope <= 0x1F;
}

static HRESULT find_impl(ELEMENT *e, int scope, IUnknown *condition, IUnknown *request, BOOL first, int options,
                         IUnknown *root, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!condition)
        return E_POINTER;
    if (!scope_ok(scope))
        return E_INVALIDARG;
    COND *cond = nullptr;
    HRESULT hr = CondFromCom(condition, &cond);
    if (FAILED(hr))
        return hr;
    REQUEST req;
    BOOL have_request = FALSE;
    if (request) {
        hr = RequestFromCom(request, &req);
        if (FAILED(hr)) {
            CondRelease(cond);
            return hr;
        }
        have_request = TRUE;
    }
    ELEMENT *root_element = nullptr;
    if (root)
        ElementFromCom(root, &root_element);
    NODE **list;
    int count;
    hr = NodeFind(e->node, scope, cond, first, options, root_element ? root_element->node : nullptr, &list, &count);
    CondRelease(cond);
    ElementRelease(root_element);
    if (SUCCEEDED(hr)) {
        ELEMENT **items;
        hr = ElementsFromNodes(list, count, have_request ? &req : nullptr, &items);
        NodeListFree(list, count);
        if (SUCCEEDED(hr)) {
            if (first) {
                *out = count ? items[0] : nullptr;
                for (int i = 1; i < count; i++)
                    ElementRelease(items[i]);
                UiaFree(items);
            } else {
                hr = ElementArrayCreate(items, count, (IUnknown **)out);
            }
        }
    }
    if (have_request)
        RequestClear(&req);
    return hr;
}

static HRESULT STDMETHODCALLTYPE E_FindFirst(void *p, int scope, IUnknown *c, void **out)
{
    return find_impl(self(p), scope, c, nullptr, TRUE, 0, nullptr, out);
}

static HRESULT STDMETHODCALLTYPE E_FindAll(void *p, int scope, IUnknown *c, void **out)
{
    return find_impl(self(p), scope, c, nullptr, FALSE, 0, nullptr, out);
}

static HRESULT STDMETHODCALLTYPE E_FindFirstBuildCache(void *p, int scope, IUnknown *c, IUnknown *r, void **out)
{
    return find_impl(self(p), scope, c, r, TRUE, 0, nullptr, out);
}

static HRESULT STDMETHODCALLTYPE E_FindAllBuildCache(void *p, int scope, IUnknown *c, IUnknown *r, void **out)
{
    return find_impl(self(p), scope, c, r, FALSE, 0, nullptr, out);
}

static HRESULT STDMETHODCALLTYPE E_FindFirstWithOptions(void *p, int scope, IUnknown *c, int options, IUnknown *root,
                                                        void **out)
{
    return find_impl(self(p), scope, c, nullptr, TRUE, options, root, out);
}

static HRESULT STDMETHODCALLTYPE E_FindAllWithOptions(void *p, int scope, IUnknown *c, int options, IUnknown *root,
                                                      void **out)
{
    return find_impl(self(p), scope, c, nullptr, FALSE, options, root, out);
}

static HRESULT STDMETHODCALLTYPE E_FindFirstWithOptionsBuildCache(void *p, int scope, IUnknown *c, IUnknown *r,
                                                                  int options, IUnknown *root, void **out)
{
    return find_impl(self(p), scope, c, r, TRUE, options, root, out);
}

static HRESULT STDMETHODCALLTYPE E_FindAllWithOptionsBuildCache(void *p, int scope, IUnknown *c, IUnknown *r,
                                                                int options, IUnknown *root, void **out)
{
    return find_impl(self(p), scope, c, r, FALSE, options, root, out);
}

static HRESULT STDMETHODCALLTYPE E_BuildUpdatedCache(void *p, IUnknown *request, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!request)
        return E_POINTER;
    REQUEST req;
    HRESULT hr = RequestFromCom(request, &req);
    if (FAILED(hr))
        return hr;
    hr = ElementCreate(self(p)->node, &req, (ELEMENT **)out);
    RequestClear(&req);
    return hr;
}

static HRESULT STDMETHODCALLTYPE E_GetCurrentPropertyValue(void *p, PROPERTYID id, VARIANT *out)
{
    if (!out)
        return E_POINTER;
    return value_of(self(p), id, TRUE, FALSE, out);
}

static HRESULT STDMETHODCALLTYPE E_GetCurrentPropertyValueEx(void *p, PROPERTYID id, BOOL ignore, VARIANT *out)
{
    if (!out)
        return E_POINTER;
    return value_of(self(p), id, TRUE, ignore, out);
}

static HRESULT STDMETHODCALLTYPE E_GetCachedPropertyValue(void *p, PROPERTYID id, VARIANT *out)
{
    if (!out)
        return E_POINTER;
    return value_of(self(p), id, FALSE, FALSE, out);
}

static HRESULT STDMETHODCALLTYPE E_GetCachedPropertyValueEx(void *p, PROPERTYID id, BOOL ignore, VARIANT *out)
{
    if (!out)
        return E_POINTER;
    return value_of(self(p), id, FALSE, ignore, out);
}

static HRESULT pattern_as(ELEMENT *e, PATTERNID id, REFIID riid, BOOL current, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!UiaPatternName(id))
        return E_INVALIDARG;
    PATTERN *pattern = nullptr;
    BOOL cached = FALSE;
    HRESULT hr;
    if (current) {
        hr = NodeGetPattern(e->node, id, &pattern);
    } else {
        hr = ElementCachedPattern(e, id, &pattern, &cached);
        if (SUCCEEDED(hr) && !cached)
            return E_INVALIDARG;
    }
    if (FAILED(hr) || !pattern)
        return hr;
    hr = ClientPatternCreate(e, id, pattern, !current, riid, out);
    PatternRelease(pattern);
    return hr;
}

static HRESULT STDMETHODCALLTYPE E_GetCurrentPatternAs(void *p, PATTERNID id, REFIID riid, void **out)
{
    return pattern_as(self(p), id, riid, TRUE, out);
}

static HRESULT STDMETHODCALLTYPE E_GetCachedPatternAs(void *p, PATTERNID id, REFIID riid, void **out)
{
    return pattern_as(self(p), id, riid, FALSE, out);
}

static HRESULT STDMETHODCALLTYPE E_GetCurrentPattern(void *p, PATTERNID id, IUnknown **out)
{
    return pattern_as(self(p), id, &IID_IUnknown, TRUE, (void **)out);
}

static HRESULT STDMETHODCALLTYPE E_GetCachedPattern(void *p, PATTERNID id, IUnknown **out)
{
    return pattern_as(self(p), id, &IID_IUnknown, FALSE, (void **)out);
}

static HRESULT STDMETHODCALLTYPE E_GetCachedParent(void *p, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    CACHE *c = self(p)->cache;
    if (c && c->parent) {
        *out = c->parent;
        ElementAddRef(c->parent);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE E_GetCachedChildren(void *p, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    CACHE *c = self(p)->cache;
    if (!c || !c->has_children)
        return S_OK;
    ELEMENT **items = c->nchildren ? UiaAlloc((SIZE_T)c->nchildren * sizeof(ELEMENT *)) : nullptr;
    for (int i = 0; i < c->nchildren; i++) {
        items[i] = c->children[i];
        ElementAddRef(items[i]);
    }
    return ElementArrayCreate(items, c->nchildren, (IUnknown **)out);
}

/* ------------------------------------------------------------------ typed properties */

static HRESULT get_int(ELEMENT *e, PROPERTYID id, BOOL current, int *out)
{
    if (!out)
        return E_POINTER;
    *out = 0;
    VARIANT v;
    HRESULT hr = current ? current_value(e, id, FALSE, &v) : cached_value(e, id, FALSE, &v);
    if (FAILED(hr))
        return hr;
    VARIANT t;
    VariantInit(&t);
    if (v.vt == VT_BOOL)
        *out = v.boolVal ? 1 : 0;
    else if (SUCCEEDED(VariantChangeType(&t, &v, 0, VT_I4)))
        *out = t.lVal;
    VariantClear(&t);
    VariantClear(&v);
    return S_OK;
}

static HRESULT get_bstr(ELEMENT *e, PROPERTYID id, BOOL current, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    VARIANT v;
    HRESULT hr = current ? current_value(e, id, FALSE, &v) : cached_value(e, id, FALSE, &v);
    if (FAILED(hr))
        return hr;
    if (v.vt == VT_BSTR) {
        *out = v.bstrVal;
        v.bstrVal = nullptr;
    } else {
        *out = SysAllocString(L"");
    }
    VariantClear(&v);
    return *out ? S_OK : E_OUTOFMEMORY;
}

static HRESULT get_element(ELEMENT *e, PROPERTYID id, BOOL current, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    VARIANT v;
    HRESULT hr = value_of(e, id, current, FALSE, &v);
    if (FAILED(hr))
        return hr;
    if (v.vt == VT_UNKNOWN && v.punkVal) {
        v.punkVal->lpVtbl->QueryInterface(v.punkVal, &IID_IMimeElement, out);
    }
    VariantClear(&v);
    return S_OK;
}

static HRESULT get_array(ELEMENT *e, PROPERTYID id, BOOL current, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    VARIANT v;
    HRESULT hr = value_of(e, id, current, FALSE, &v);
    if (FAILED(hr))
        return hr;
    ELEMENT **items = nullptr;
    int count = 0;
    if (v.vt == (VT_ARRAY | VT_UNKNOWN) && v.parray) {
        LONG lower, upper;
        IUnknown **raw;
        SafeArrayGetLBound(v.parray, 1, &lower);
        SafeArrayGetUBound(v.parray, 1, &upper);
        int total = upper - lower + 1;
        items = total > 0 ? UiaAlloc((SIZE_T)total * sizeof(ELEMENT *)) : nullptr;
        if (items && SUCCEEDED(SafeArrayAccessData(v.parray, (void **)&raw))) {
            for (int i = 0; i < total; i++) {
                ELEMENT *el = nullptr;
                if (raw[i] && SUCCEEDED(ElementFromCom(raw[i], &el)) && el)
                    items[count++] = el;
            }
            SafeArrayUnaccessData(v.parray);
        }
    }
    VariantClear(&v);
    return ElementArrayCreate(items, count, (IUnknown **)out);
}

static HRESULT get_safearray(ELEMENT *e, PROPERTYID id, BOOL current, SAFEARRAY **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    VARIANT v;
    HRESULT hr = current ? current_value(e, id, FALSE, &v) : cached_value(e, id, FALSE, &v);
    if (FAILED(hr))
        return hr;
    if (v.vt & VT_ARRAY) {
        *out = v.parray;
        v.vt = VT_EMPTY;
    }
    VariantClear(&v);
    return S_OK;
}

static HRESULT get_rect(ELEMENT *e, BOOL current, RECT *out)
{
    if (!out)
        return E_POINTER;
    memset(out, 0, sizeof(*out));
    VARIANT v;
    HRESULT hr = current ? current_value(e, UIA_BoundingRectanglePropertyId, FALSE, &v)
                         : cached_value(e, UIA_BoundingRectanglePropertyId, FALSE, &v);
    if (FAILED(hr))
        return hr;
    double *d;
    if (v.vt == (VT_ARRAY | VT_R8) && v.parray && SUCCEEDED(SafeArrayAccessData(v.parray, (void **)&d))) {
        out->left = (LONG)d[0];
        out->top = (LONG)d[1];
        out->right = (LONG)(d[0] + d[2]);
        out->bottom = (LONG)(d[1] + d[3]);
        SafeArrayUnaccessData(v.parray);
    }
    VariantClear(&v);
    return S_OK;
}

#define P_INT(NAME, ID)                                                                                                \
    static HRESULT STDMETHODCALLTYPE E_get_Current##NAME(void *p, int *out) { return get_int(self(p), ID, TRUE, out); } \
    static HRESULT STDMETHODCALLTYPE E_get_Cached##NAME(void *p, int *out) { return get_int(self(p), ID, FALSE, out); }
#define P_BSTR(NAME, ID)                                                                                               \
    static HRESULT STDMETHODCALLTYPE E_get_Current##NAME(void *p, BSTR *out)                                           \
    {                                                                                                                  \
        return get_bstr(self(p), ID, TRUE, out);                                                                       \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE E_get_Cached##NAME(void *p, BSTR *out)                                            \
    {                                                                                                                  \
        return get_bstr(self(p), ID, FALSE, out);                                                                      \
    }
#define P_ELEMENT(NAME, ID)                                                                                            \
    static HRESULT STDMETHODCALLTYPE E_get_Current##NAME(void *p, void **out)                                          \
    {                                                                                                                  \
        return get_element(self(p), ID, TRUE, out);                                                                    \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE E_get_Cached##NAME(void *p, void **out)                                           \
    {                                                                                                                  \
        return get_element(self(p), ID, FALSE, out);                                                                   \
    }
#define P_ARRAY(NAME, ID)                                                                                              \
    static HRESULT STDMETHODCALLTYPE E_get_Current##NAME(void *p, void **out)                                          \
    {                                                                                                                  \
        return get_array(self(p), ID, TRUE, out);                                                                      \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE E_get_Cached##NAME(void *p, void **out)                                           \
    {                                                                                                                  \
        return get_array(self(p), ID, FALSE, out);                                                                     \
    }
#define P_SAFEARRAY(NAME, ID)                                                                                          \
    static HRESULT STDMETHODCALLTYPE E_get_Current##NAME(void *p, SAFEARRAY **out)                                     \
    {                                                                                                                  \
        return get_safearray(self(p), ID, TRUE, out);                                                                  \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE E_get_Cached##NAME(void *p, SAFEARRAY **out)                                      \
    {                                                                                                                  \
        return get_safearray(self(p), ID, FALSE, out);                                                                 \
    }

P_INT(ProcessId, UIA_ProcessIdPropertyId)
P_INT(ControlType, UIA_ControlTypePropertyId)
P_BSTR(LocalizedControlType, UIA_LocalizedControlTypePropertyId)
P_BSTR(Name, UIA_NamePropertyId)
P_BSTR(AcceleratorKey, UIA_AcceleratorKeyPropertyId)
P_BSTR(AccessKey, UIA_AccessKeyPropertyId)
P_INT(HasKeyboardFocus, UIA_HasKeyboardFocusPropertyId)
P_INT(IsKeyboardFocusable, UIA_IsKeyboardFocusablePropertyId)
P_INT(IsEnabled, UIA_IsEnabledPropertyId)
P_BSTR(AutomationId, UIA_AutomationIdPropertyId)
P_BSTR(ClassName, UIA_ClassNamePropertyId)
P_BSTR(HelpText, UIA_HelpTextPropertyId)
P_INT(Culture, UIA_CulturePropertyId)
P_INT(IsControlElement, UIA_IsControlElementPropertyId)
P_INT(IsContentElement, UIA_IsContentElementPropertyId)
P_INT(IsPassword, UIA_IsPasswordPropertyId)
P_BSTR(ItemType, UIA_ItemTypePropertyId)
P_INT(IsOffscreen, UIA_IsOffscreenPropertyId)
P_INT(Orientation, UIA_OrientationPropertyId)
P_BSTR(FrameworkId, UIA_FrameworkIdPropertyId)
P_INT(IsRequiredForForm, UIA_IsRequiredForFormPropertyId)
P_BSTR(ItemStatus, UIA_ItemStatusPropertyId)
P_ELEMENT(LabeledBy, UIA_LabeledByPropertyId)
P_BSTR(AriaRole, UIA_AriaRolePropertyId)
P_BSTR(AriaProperties, UIA_AriaPropertiesPropertyId)
P_INT(IsDataValidForForm, UIA_IsDataValidForFormPropertyId)
P_ARRAY(ControllerFor, UIA_ControllerForPropertyId)
P_ARRAY(DescribedBy, UIA_DescribedByPropertyId)
P_ARRAY(FlowsTo, UIA_FlowsToPropertyId)
P_BSTR(ProviderDescription, UIA_ProviderDescriptionPropertyId)
P_INT(OptimizeForVisualContent, UIA_OptimizeForVisualContentPropertyId)
P_INT(LiveSetting, UIA_LiveSettingPropertyId)
P_ARRAY(FlowsFrom, UIA_FlowsFromPropertyId)
P_INT(IsPeripheral, UIA_IsPeripheralPropertyId)
P_INT(PositionInSet, UIA_PositionInSetPropertyId)
P_INT(SizeOfSet, UIA_SizeOfSetPropertyId)
P_INT(Level, UIA_LevelPropertyId)
P_SAFEARRAY(AnnotationTypes, UIA_AnnotationTypesPropertyId)
P_ARRAY(AnnotationObjects, UIA_AnnotationObjectsPropertyId)
P_INT(LandmarkType, UIA_LandmarkTypePropertyId)
P_BSTR(LocalizedLandmarkType, UIA_LocalizedLandmarkTypePropertyId)
P_BSTR(FullDescription, UIA_FullDescriptionPropertyId)
P_INT(HeadingLevel, UIA_HeadingLevelPropertyId)
P_INT(IsDialog, UIA_IsDialogPropertyId)

static HRESULT STDMETHODCALLTYPE E_get_CurrentNativeWindowHandle(void *p, HWND *out)
{
    int v;
    HRESULT hr = get_int(self(p), UIA_NativeWindowHandlePropertyId, TRUE, &v);
    if (out)
        *out = (HWND)(LONG_PTR)v;
    return hr;
}

static HRESULT STDMETHODCALLTYPE E_get_CachedNativeWindowHandle(void *p, HWND *out)
{
    int v;
    HRESULT hr = get_int(self(p), UIA_NativeWindowHandlePropertyId, FALSE, &v);
    if (out)
        *out = (HWND)(LONG_PTR)v;
    return hr;
}

static HRESULT STDMETHODCALLTYPE E_get_CurrentBoundingRectangle(void *p, RECT *out)
{
    return get_rect(self(p), TRUE, out);
}

static HRESULT STDMETHODCALLTYPE E_get_CachedBoundingRectangle(void *p, RECT *out)
{
    return get_rect(self(p), FALSE, out);
}

static HRESULT STDMETHODCALLTYPE E_GetClickablePoint(void *p, POINT *point, BOOL *got)
{
    if (!got)
        return E_POINTER;
    *got = FALSE;
    if (!point)
        return E_POINTER;
    point->x = point->y = 0;
    return NodeClickablePoint(self(p)->node, point, got);
}

static HRESULT STDMETHODCALLTYPE E_ShowContextMenu(void *p)
{
    NODE *n = self(p)->node;
    VARIANT v;
    IRawElementProviderSimple *simple = n->ops->provider(n);
    IRawElementProviderSimple2 *two = nullptr;
    (void)v;
    if (simple && SUCCEEDED(simple->lpVtbl->QueryInterface(simple, &IID_IRawElementProviderSimple2, (void **)&two))) {
        HRESULT hr = two->lpVtbl->ShowContextMenu(two);
        two->lpVtbl->Release(two);
        return hr;
    }
    return UIA_E_NOTSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE E_GetCurrentMetadataValue(void *p, int target, METADATAID id, VARIANT *out)
{
    if (!out)
        return E_POINTER;
    VariantInit(out);
    NODE *n = self(p)->node;
    IRawElementProviderSimple *simple = n->ops->provider(n);
    IRawElementProviderSimple3 *three = nullptr;
    if (simple && SUCCEEDED(simple->lpVtbl->QueryInterface(simple, &IID_IRawElementProviderSimple3, (void **)&three))) {
        HRESULT hr = three->lpVtbl->GetMetadataValue(three, target, id, out);
        three->lpVtbl->Release(three);
        return hr;
    }
    return UIA_E_NOTSUPPORTED;
}

#define SLOT(name) (const void *)E_##name,
static const void *const g_element_vtbl[] = {
    (const void *)elem_query,
    (const void *)elem_add_ref,
    (const void *)elem_release,
    IUIAutomationElement9_METHODS(SLOT)
};

/* ------------------------------------------------------------------ element arrays */

static HRESULT STDMETHODCALLTYPE arr_query(void *p, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IUIAutomationElementArray)) {
        *out = p;
        ((ELEMENT_ARRAY *)p)->refs++;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE arr_add_ref(void *p)
{
    return (ULONG)InterlockedIncrement(&((ELEMENT_ARRAY *)p)->refs);
}

static ULONG STDMETHODCALLTYPE arr_release(void *p)
{
    ELEMENT_ARRAY *a = p;
    LONG refs = InterlockedDecrement(&a->refs);
    if (!refs) {
        for (int i = 0; i < a->count; i++)
            ElementRelease(a->items[i]);
        UiaFree(a->items);
        UiaFree(a);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE A_get_Length(void *p, int *out)
{
    if (!out)
        return E_POINTER;
    *out = ((ELEMENT_ARRAY *)p)->count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE A_GetElement(void *p, int index, void **out)
{
    ELEMENT_ARRAY *a = p;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (index < 0 || index >= a->count)
        return E_INVALIDARG;
    *out = a->items[index];
    ElementAddRef(a->items[index]);
    return S_OK;
}

static const void *const g_array_vtbl[] = {
    (const void *)arr_query,
    (const void *)arr_add_ref,
    (const void *)arr_release,
    (const void *)A_get_Length,
    (const void *)A_GetElement,
};

HRESULT ElementArrayCreate(ELEMENT **items, int count, IUnknown **out)
{
    ELEMENT_ARRAY *a = UiaAlloc(sizeof(*a));
    if (!a) {
        for (int i = 0; i < count; i++)
            ElementRelease(items[i]);
        UiaFree(items);
        return E_OUTOFMEMORY;
    }
    a->vtbl = g_array_vtbl;
    a->refs = 1;
    a->items = items;
    a->count = count;
    *out = (IUnknown *)a;
    return S_OK;
}

/* ------------------------------------------------------------------ the flat api's data */

static HRESULT push(VARIANT **list, int *count, int *capacity, const VARIANT *value)
{
    if (*count == *capacity) {
        int grown = *capacity ? *capacity * 2 : 16;
        VARIANT *bigger = UiaAlloc((SIZE_T)grown * sizeof(VARIANT));
        if (!bigger)
            return E_OUTOFMEMORY;
        if (*count)
            memcpy(bigger, *list, (SIZE_T)*count * sizeof(VARIANT));
        UiaFree(*list);
        *list = bigger;
        *capacity = grown;
    }
    VariantInit(&(*list)[*count]);
    HRESULT hr = VariantCopy(&(*list)[*count], (VARIANT *)value);
    if (SUCCEEDED(hr))
        (*count)++;
    return hr;
}

static HRESULT serialize(ELEMENT *e, const REQUEST *r, VARIANT **list, int *count, int *capacity, WCHAR **text,
                         SIZE_T *used)
{
    VARIANT v;
    v.vt = VT_I8;
    v.llVal = (LONGLONG)(ULONG_PTR)e->node;
    NodeAddRef(e->node);
    HRESULT hr = push(list, count, capacity, &v);
    for (int i = 0; SUCCEEDED(hr) && i < r->nprops; i++) {
        VARIANT value;
        VariantInit(&value);
        if (FAILED(ElementCachedValue(e, r->props[i], &value))) {
            value.vt = VT_UNKNOWN;
            value.punkVal = UiaNotSupportedValue();
        }
        hr = push(list, count, capacity, &value);
        VariantClear(&value);
    }
    for (int i = 0; SUCCEEDED(hr) && i < r->npatterns; i++) {
        PATTERN *pattern;
        BOOL cached;
        ElementCachedPattern(e, r->patterns[i], &pattern, &cached);
        VariantInit(&v);
        if (pattern) {
            v.vt = VT_I8;
            v.llVal = (LONGLONG)(ULONG_PTR)pattern;
        }
        hr = push(list, count, capacity, &v);
    }
    int kids = e->cache ? e->cache->nchildren : 0;
    WCHAR piece[16];
    int n = _snwprintf(piece, RTL_NUMBER_OF(piece), L"<%d", kids);
    wcscpy(*text + *used, piece);
    *used += n;
    for (int i = 0; SUCCEEDED(hr) && i < kids; i++)
        hr = serialize(e->cache->children[i], r, list, count, capacity, text, used);
    wcscpy(*text + *used, L">");
    *used += 1;
    return hr;
}

HRESULT ElementTreeToData(ELEMENT *root, const REQUEST *request, SAFEARRAY **data, BSTR *structure)
{
    VARIANT *list = nullptr;
    int count = 0, capacity = 0;
    WCHAR *text = UiaAlloc(65536 * sizeof(WCHAR));
    SIZE_T used = 0;
    if (!text)
        return E_OUTOFMEMORY;
    HRESULT hr = serialize(root, request, &list, &count, &capacity, &text, &used);
    if (SUCCEEDED(hr)) {
        SAFEARRAY *array = SafeArrayCreateVector(VT_VARIANT, 0, (ULONG)count);
        VARIANT *items;
        if (array && SUCCEEDED(SafeArrayAccessData(array, (void **)&items))) {
            memcpy(items, list, (SIZE_T)count * sizeof(VARIANT));
            memset(list, 0, (SIZE_T)count * sizeof(VARIANT));
            SafeArrayUnaccessData(array);
            *data = array;
            *structure = SysAllocString(text);
        } else {
            hr = E_OUTOFMEMORY;
        }
    }
    for (int i = 0; i < count; i++)
        VariantClear(&list[i]);
    UiaFree(list);
    UiaFree(text);
    return hr;
}
