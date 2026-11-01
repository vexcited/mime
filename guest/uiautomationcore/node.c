/*
 * nodes: the elements clients see. a node is a provider of this process, a wrapper of a provider of another thread or
 * process, or a proxy for a window. this file holds what they share, the answers a node gives with the schema's
 * defaults applied, and the node of a provider of this process.
 */
#include "uia.h"

const IID IID_IMimeUiaNode = {0x4d494d45, 0x5541, 0x4e4f, {0x44, 0x45, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01}};
const IID IID_IMimeUiaPattern = {0x4d494d45, 0x5541, 0x5041, {0x54, 0x4e, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01}};

extern IUnknown *UiaNotSupportedValue(void);
extern IUnknown *UiaMixedValue(void);

/* ------------------------------------------------------------------ base objects */

static HRESULT STDMETHODCALLTYPE node_query(IUnknown *self, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IMimeUiaNode)) {
        *out = self;
        self->lpVtbl->AddRef(self);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE node_add_ref(IUnknown *self)
{
    return (ULONG)InterlockedIncrement(&((NODE *)self)->refs);
}

static ULONG STDMETHODCALLTYPE node_release(IUnknown *self)
{
    NODE *n = (NODE *)self;
    LONG refs = InterlockedDecrement(&n->refs);
    if (!refs) {
        n->ops->destroy(n);
        UiaFree(n);
    }
    return (ULONG)refs;
}

static const IUnknownVtbl g_node_vtbl = {node_query, node_add_ref, node_release};

void *NodeAlloc(SIZE_T size, const NODE_OPS *ops)
{
    NODE *n = UiaAlloc(size);
    if (!n)
        return nullptr;
    n->lpVtbl = &g_node_vtbl;
    n->magic = UIA_HANDLE_MAGIC;
    n->refs = 1;
    n->ops = ops;
    return n;
}

void NodeAddRef(NODE *n)
{
    node_add_ref((IUnknown *)n);
}

void NodeRelease(NODE *n)
{
    if (n)
        node_release((IUnknown *)n);
}

static HRESULT STDMETHODCALLTYPE pattern_query(IUnknown *self, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IMimeUiaPattern)) {
        *out = self;
        self->lpVtbl->AddRef(self);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE pattern_add_ref(IUnknown *self)
{
    return (ULONG)InterlockedIncrement(&((PATTERN *)self)->refs);
}

static ULONG STDMETHODCALLTYPE pattern_release(IUnknown *self)
{
    PATTERN *p = (PATTERN *)self;
    LONG refs = InterlockedDecrement(&p->refs);
    if (!refs) {
        p->ops->destroy(p);
        UiaFree(p);
    }
    return (ULONG)refs;
}

static const IUnknownVtbl g_pattern_vtbl = {pattern_query, pattern_add_ref, pattern_release};

void *PatternAlloc(SIZE_T size, const PATTERN_OPS *ops, PATTERNID id)
{
    PATTERN *p = UiaAlloc(size);
    if (!p)
        return nullptr;
    p->lpVtbl = &g_pattern_vtbl;
    p->magic = UIA_HANDLE_MAGIC;
    p->refs = 1;
    p->ops = ops;
    p->id = id;
    return p;
}

void PatternAddRef(PATTERN *p)
{
    pattern_add_ref((IUnknown *)p);
}

void PatternRelease(PATTERN *p)
{
    if (p)
        pattern_release((IUnknown *)p);
}

/* ------------------------------------------------------------------ a provider of this process */

typedef struct PROVNODE {
    NODE base;
    IRawElementProviderSimple *simple;
    IRawElementProviderFragment *fragment;
} PROVNODE;

#define PROV(n) ((PROVNODE *)(n))

static const NODE_OPS g_provider_ops;

static BOOL is_provider_node(NODE *n)
{
    return n->ops == &g_provider_ops;
}

static void prov_destroy(NODE *n)
{
    PROVNODE *p = PROV(n);
    if (p->fragment)
        p->fragment->lpVtbl->Release(p->fragment);
    if (p->simple)
        p->simple->lpVtbl->Release(p->simple);
}

static HRESULT prov_get_prop(NODE *n, PROPERTYID id, VARIANT *out)
{
    VariantInit(out);
    if (n->disconnected)
        return UIA_E_ELEMENTNOTAVAILABLE;
    /* the rectangle of a fragment is the one its own method gives. */
    IRawElementProviderFragment *fragment = PROV(n)->fragment;
    if (id == UIA_BoundingRectanglePropertyId && fragment) {
        UiaRect rect = {};
        HRESULT hr = fragment->lpVtbl->get_BoundingRectangle(fragment, &rect);
        if (SUCCEEDED(hr)) {
            double d[4] = {rect.left, rect.top, rect.width, rect.height};
            hr = UiaDoubleArrayToSafeArray(d, 4, &out->parray);
            if (SUCCEEDED(hr))
                out->vt = VT_ARRAY | VT_R8;
            return hr;
        }
    }
    HRESULT hr = PROV(n)->simple->lpVtbl->GetPropertyValue(PROV(n)->simple, id, out);
    if (FAILED(hr)) {
        VariantClear(out);
        return hr;
    }
    return S_OK;
}

static HRESULT prov_get_pattern(NODE *n, PATTERNID id, PATTERN **out)
{
    *out = nullptr;
    if (n->disconnected)
        return UIA_E_ELEMENTNOTAVAILABLE;
    IUnknown *unknown = nullptr;
    HRESULT hr = PROV(n)->simple->lpVtbl->GetPatternProvider(PROV(n)->simple, id, &unknown);
    if (FAILED(hr))
        return hr;
    if (!unknown)
        return S_OK;
    hr = PatternFromProvider(unknown, id, n, out);
    unknown->lpVtbl->Release(unknown);
    return hr;
}

/* a fragment handed over by a provider, as a node. */
static HRESULT node_of_fragment(IRawElementProviderFragment *fragment, NODE **out)
{
    *out = nullptr;
    if (!fragment)
        return S_OK;
    IRawElementProviderSimple *simple = nullptr;
    HRESULT hr = fragment->lpVtbl->QueryInterface(fragment, &IID_IRawElementProviderSimple, (void **)&simple);
    if (FAILED(hr))
        return hr;
    hr = NodeFromProvider(simple, out);
    simple->lpVtbl->Release(simple);
    return hr;
}

static HRESULT prov_navigate(NODE *n, NavigateDirection dir, NODE **out)
{
    *out = nullptr;
    if (n->disconnected)
        return UIA_E_ELEMENTNOTAVAILABLE;
    IRawElementProviderFragment *fragment = PROV(n)->fragment;
    if (!fragment)
        return S_OK;
    IRawElementProviderFragment *target = nullptr;
    HRESULT hr = fragment->lpVtbl->Navigate(fragment, dir, &target);
    if (FAILED(hr))
        return hr;
    hr = node_of_fragment(target, out);
    if (target)
        target->lpVtbl->Release(target);
    return hr;
}

static HRESULT prov_runtime_id(NODE *n, int **ids, int *count)
{
    *ids = nullptr;
    *count = 0;
    if (n->disconnected)
        return UIA_E_ELEMENTNOTAVAILABLE;
    IRawElementProviderFragment *fragment = PROV(n)->fragment;
    if (!fragment)
        return S_OK;
    SAFEARRAY *array = nullptr;
    HRESULT hr = fragment->lpVtbl->GetRuntimeId(fragment, &array);
    if (FAILED(hr))
        return hr;
    if (array) {
        UiaSafeArrayToIntArray(array, ids, count);
        SafeArrayDestroy(array);
    }
    return S_OK;
}

static HRESULT prov_options(NODE *n, int *options)
{
    ProviderOptions value = 0;
    HRESULT hr = PROV(n)->simple->lpVtbl->get_ProviderOptions(PROV(n)->simple, &value);
    *options = FAILED(hr) ? 0 : (int)value;
    return hr;
}

static HRESULT prov_host_hwnd(NODE *n, HWND *hwnd)
{
    *hwnd = nullptr;
    IRawElementProviderSimple *host = nullptr;
    HRESULT hr = PROV(n)->simple->lpVtbl->get_HostRawElementProvider(PROV(n)->simple, &host);
    if (FAILED(hr) || !host)
        return S_OK;
    VARIANT v;
    VariantInit(&v);
    if (SUCCEEDED(host->lpVtbl->GetPropertyValue(host, UIA_NativeWindowHandlePropertyId, &v)) &&
        (v.vt == VT_I4 || v.vt == VT_INT))
        *hwnd = (HWND)(LONG_PTR)v.lVal;
    VariantClear(&v);
    host->lpVtbl->Release(host);
    return S_OK;
}

static HRESULT prov_set_focus(NODE *n)
{
    IRawElementProviderFragment *fragment = PROV(n)->fragment;
    if (!fragment)
        return UIA_E_NOTSUPPORTED;
    return fragment->lpVtbl->SetFocus(fragment);
}

static HRESULT prov_root_from_point(NODE *n, double x, double y, NODE **out)
{
    *out = nullptr;
    IRawElementProviderFragmentRoot *root = nullptr;
    if (!PROV(n)->fragment ||
        FAILED(PROV(n)->fragment->lpVtbl->QueryInterface(PROV(n)->fragment, &IID_IRawElementProviderFragmentRoot,
                                                          (void **)&root)))
        return UIA_E_NOTSUPPORTED;
    IRawElementProviderFragment *found = nullptr;
    HRESULT hr = root->lpVtbl->ElementProviderFromPoint(root, x, y, &found);
    if (SUCCEEDED(hr))
        hr = node_of_fragment(found, out);
    if (found)
        found->lpVtbl->Release(found);
    root->lpVtbl->Release(root);
    return hr;
}

static HRESULT prov_root_focus(NODE *n, NODE **out)
{
    *out = nullptr;
    IRawElementProviderFragmentRoot *root = nullptr;
    if (!PROV(n)->fragment ||
        FAILED(PROV(n)->fragment->lpVtbl->QueryInterface(PROV(n)->fragment, &IID_IRawElementProviderFragmentRoot,
                                                          (void **)&root)))
        return UIA_E_NOTSUPPORTED;
    IRawElementProviderFragment *found = nullptr;
    HRESULT hr = root->lpVtbl->GetFocus(root, &found);
    if (SUCCEEDED(hr))
        hr = node_of_fragment(found, out);
    if (found)
        found->lpVtbl->Release(found);
    root->lpVtbl->Release(root);
    return hr;
}

static HRESULT prov_fragment_root(NODE *n, NODE **out)
{
    *out = nullptr;
    IRawElementProviderFragment *fragment = PROV(n)->fragment;
    if (!fragment)
        return S_OK;
    IRawElementProviderFragmentRoot *root = nullptr;
    HRESULT hr = fragment->lpVtbl->get_FragmentRoot(fragment, &root);
    if (FAILED(hr) || !root)
        return hr;
    IRawElementProviderFragment *as_fragment = nullptr;
    hr = root->lpVtbl->QueryInterface(root, &IID_IRawElementProviderFragment, (void **)&as_fragment);
    if (SUCCEEDED(hr)) {
        hr = node_of_fragment(as_fragment, out);
        as_fragment->lpVtbl->Release(as_fragment);
    }
    root->lpVtbl->Release(root);
    return hr;
}

static HRESULT prov_embedded_roots(NODE *n, NODE ***list, int *count)
{
    *list = nullptr;
    *count = 0;
    IRawElementProviderFragment *fragment = PROV(n)->fragment;
    if (!fragment)
        return S_OK;
    SAFEARRAY *array = nullptr;
    HRESULT hr = fragment->lpVtbl->GetEmbeddedFragmentRoots(fragment, &array);
    if (FAILED(hr) || !array)
        return hr;
    LONG lower, upper;
    SafeArrayGetLBound(array, 1, &lower);
    SafeArrayGetUBound(array, 1, &upper);
    int total = upper - lower + 1;
    if (total > 0) {
        NODE **nodes = UiaAlloc((SIZE_T)total * sizeof(*nodes));
        IUnknown **items;
        if (nodes && SUCCEEDED(SafeArrayAccessData(array, (void **)&items))) {
            int made = 0;
            for (int i = 0; i < total; i++)
                if (items[i] && SUCCEEDED(NodeFromUnknown(items[i], &nodes[made])) && nodes[made])
                    made++;
            SafeArrayUnaccessData(array);
            *list = nodes;
            *count = made;
        } else {
            UiaFree(nodes);
        }
    }
    SafeArrayDestroy(array);
    return S_OK;
}

static HRESULT prov_process_id(NODE *n, DWORD *pid)
{
    *pid = GetCurrentProcessId();
    return S_OK;
}

static HRESULT prov_advise(NODE *n, BOOL added, int event, SAFEARRAY *props)
{
    IRawElementProviderAdviseEvents *advise = nullptr;
    if (FAILED(PROV(n)->simple->lpVtbl->QueryInterface(PROV(n)->simple, &IID_IRawElementProviderAdviseEvents,
                                                        (void **)&advise)))
        return S_OK;
    HRESULT hr = added ? advise->lpVtbl->AdviseEventAdded(advise, event, props)
                       : advise->lpVtbl->AdviseEventRemoved(advise, event, props);
    advise->lpVtbl->Release(advise);
    return hr;
}

static IRawElementProviderSimple *prov_provider(NODE *n)
{
    return PROV(n)->simple;
}

static BOOL prov_is_fragment(NODE *n)
{
    return PROV(n)->fragment != nullptr;
}

static const NODE_OPS g_provider_ops = {
    prov_destroy,     prov_get_prop,   prov_get_pattern,  prov_navigate,       prov_runtime_id,
    prov_options,     prov_host_hwnd,  prov_set_focus,    prov_root_from_point, prov_root_focus,
    prov_fragment_root, prov_embedded_roots, prov_process_id, prov_advise,      prov_provider,
    prov_is_fragment,
};

HRESULT NodeFromProvider(IRawElementProviderSimple *provider, NODE **out)
{
    *out = nullptr;
    if (!provider)
        return E_INVALIDARG;
    PROVNODE *p = NodeAlloc(sizeof(*p), &g_provider_ops);
    if (!p)
        return E_OUTOFMEMORY;
    provider->lpVtbl->AddRef(provider);
    p->simple = provider;
    provider->lpVtbl->QueryInterface(provider, &IID_IRawElementProviderFragment, (void **)&p->fragment);
    *out = &p->base;
    return S_OK;
}

HRESULT NodeFromUnknown(IUnknown *unknown, NODE **out)
{
    *out = nullptr;
    if (!unknown)
        return S_OK;
    if (SUCCEEDED(unknown->lpVtbl->QueryInterface(unknown, &IID_IMimeUiaNode, (void **)out)))
        return S_OK;
    IRawElementProviderSimple *simple = nullptr;
    if (SUCCEEDED(unknown->lpVtbl->QueryInterface(unknown, &IID_IRawElementProviderSimple, (void **)&simple))) {
        HRESULT hr = NodeFromProvider(simple, out);
        simple->lpVtbl->Release(simple);
        return hr;
    }
    IDispatch *dispatch = nullptr;
    if (SUCCEEDED(unknown->lpVtbl->QueryInterface(unknown, &IID_IDispatch, (void **)&dispatch))) {
        HRESULT hr = RemoteNodeFromDispatch(dispatch, out);
        dispatch->lpVtbl->Release(dispatch);
        return hr;
    }
    return E_NOINTERFACE;
}

/* ------------------------------------------------------------------ values */

/* the elements of an array of providers, as nodes in an array of the same shape. */
static HRESULT normalize_array(SAFEARRAY **array)
{
    SAFEARRAY *source = *array;
    if (!source)
        return S_OK;
    LONG lower, upper;
    if (SafeArrayGetDim(source) != 1 || FAILED(SafeArrayGetLBound(source, 1, &lower)) ||
        FAILED(SafeArrayGetUBound(source, 1, &upper)))
        return E_INVALIDARG;
    int total = upper - lower + 1;
    SAFEARRAY *result = SafeArrayCreateVector(VT_UNKNOWN, 0, total > 0 ? (ULONG)total : 0);
    if (!result)
        return E_OUTOFMEMORY;
    if (total > 0) {
        IUnknown **in, **outv;
        HRESULT hr = SafeArrayAccessData(source, (void **)&in);
        if (FAILED(hr)) {
            SafeArrayDestroy(result);
            return hr;
        }
        SafeArrayAccessData(result, (void **)&outv);
        for (int i = 0; i < total; i++) {
            NODE *node = nullptr;
            if (in[i] && SUCCEEDED(NodeFromUnknown(in[i], &node)))
                outv[i] = (IUnknown *)node;
        }
        SafeArrayUnaccessData(result);
        SafeArrayUnaccessData(source);
    }
    SafeArrayDestroy(source);
    *array = result;
    return S_OK;
}

HRESULT NormalizeValue(VARIANT *value)
{
    if (value->vt == VT_UNKNOWN || value->vt == VT_DISPATCH) {
        IUnknown *unknown = value->punkVal;
        if (!unknown || unknown == UiaNotSupportedValue() || unknown == UiaMixedValue()) {
            value->vt = VT_UNKNOWN;
            return S_OK;
        }
        NODE *node = nullptr;
        HRESULT hr = NodeFromUnknown(unknown, &node);
        unknown->lpVtbl->Release(unknown);
        value->vt = VT_UNKNOWN;
        value->punkVal = (IUnknown *)node;
        return hr == E_NOINTERFACE ? S_OK : hr;
    }
    if (value->vt == (VT_ARRAY | VT_UNKNOWN) || value->vt == (VT_ARRAY | VT_DISPATCH)) {
        HRESULT hr = normalize_array(&value->parray);
        value->vt = VT_ARRAY | VT_UNKNOWN;
        return hr;
    }
    return S_OK;
}

/* an answer must have the type the schema gives its property. */
HRESULT UiaCoerceProperty(int id, VARIANT *value)
{
    const PROPERTY_INFO *info = UiaPropertyInfo(id);
    if (!info || value->vt == VT_EMPTY)
        return S_OK;
    VARTYPE want = info->vt;
    if (value->vt == want)
        return S_OK;
    if (want == VT_UNKNOWN && value->vt == VT_DISPATCH)
        return S_OK;
    if ((want & VT_ARRAY) || (value->vt & VT_ARRAY) || want == VT_UNKNOWN || value->vt == VT_UNKNOWN)
        return value->vt == (VT_ARRAY | VT_DISPATCH) && want == (VT_ARRAY | VT_UNKNOWN) ? S_OK : E_INVALIDARG;
    if (want == VT_BSTR)
        return E_INVALIDARG;
    VARIANT converted;
    VariantInit(&converted);
    HRESULT hr = VariantChangeType(&converted, value, 0, want);
    if (FAILED(hr))
        return E_INVALIDARG;
    VariantClear(value);
    *value = converted;
    return S_OK;
}

/* ------------------------------------------------------------------ what a node says */

HRESULT NodeRuntimeId(NODE *n, int **ids, int *count)
{
    *ids = nullptr;
    *count = 0;
    int *raw = nullptr, raw_count = 0;
    HRESULT hr = n->ops->runtime_id(n, &raw, &raw_count);
    if (FAILED(hr))
        return hr;
    HWND hwnd = nullptr;
    if (raw_count && raw[0] != UiaAppendRuntimeId) {
        *ids = raw;
        *count = raw_count;
        return S_OK;
    }
    /* ids that ask to be appended continue the id of the window that hosts their fragment root. */
    NODE *root = nullptr;
    if (n->ops->is_fragment(n))
        n->ops->fragment_root(n, &root);
    if (root) {
        root->ops->host_hwnd(root, &hwnd);
        NodeRelease(root);
    }
    if (!hwnd)
        n->ops->host_hwnd(n, &hwnd);
    if (!hwnd && !raw_count) {
        VARIANT v;
        VariantInit(&v);
        if (SUCCEEDED(n->ops->get_prop(n, UIA_NativeWindowHandlePropertyId, &v)) && v.vt == VT_I4 && v.lVal)
            hwnd = (HWND)(LONG_PTR)v.lVal;
        VariantClear(&v);
    }
    if (!raw_count) {
        if (!hwnd)
            return S_OK;
        int *made = UiaAlloc(2 * sizeof(int));
        if (!made)
            return E_OUTOFMEMORY;
        made[0] = 42;
        made[1] = (int)(LONG_PTR)hwnd;
        *ids = made;
        *count = 2;
        return S_OK;
    }
    int *made = UiaAlloc(((SIZE_T)raw_count + 1) * sizeof(int));
    if (!made) {
        UiaFree(raw);
        return E_OUTOFMEMORY;
    }
    int at = 0;
    if (hwnd) {
        made[at++] = 42;
        made[at++] = (int)(LONG_PTR)hwnd;
    }
    for (int i = 1; i < raw_count; i++)
        made[at++] = raw[i];
    UiaFree(raw);
    *ids = made;
    *count = at;
    return S_OK;
}

HRESULT NodeProcessId(NODE *n, DWORD *pid)
{
    return n->ops->process_id(n, pid);
}

HRESULT NodeGetPattern(NODE *n, PATTERNID id, PATTERN **out)
{
    *out = nullptr;
    return n->ops->get_pattern(n, id, out);
}

static HRESULT value_of_runtime_id(NODE *n, VARIANT *out)
{
    int *ids = nullptr, count = 0;
    HRESULT hr = NodeRuntimeId(n, &ids, &count);
    if (FAILED(hr))
        return hr;
    if (count) {
        hr = UiaIntArrayToSafeArray(ids, count, &out->parray);
        if (SUCCEEDED(hr))
            out->vt = VT_ARRAY | VT_I4;
    }
    UiaFree(ids);
    return hr;
}

static HRESULT answer_from_pattern(NODE *n, const PROPERTY_INFO *info, PROPERTYID id, VARIANT *out)
{
    int method = PatternMethodOfProperty(id);
    if (method < 0)
        return S_OK;
    PATTERN *pattern = nullptr;
    HRESULT hr = n->ops->get_pattern(n, info->pattern, &pattern);
    if (FAILED(hr) || !pattern)
        return hr;
    VARIANT ret;
    VariantInit(&ret);
    hr = PatternCall(pattern, method, nullptr, 0, &ret);
    PatternRelease(pattern);
    if (SUCCEEDED(hr)) {
        *out = ret;
        return S_OK;
    }
    VariantClear(&ret);
    /* a pattern that fails to answer leaves the property to its default. */
    return S_OK;
}

HRESULT NodeGetProperty(NODE *n, PROPERTYID id, BOOL ignore_default, VARIANT *out)
{
    VariantInit(out);
    const PROPERTY_INFO *info = UiaPropertyInfo(id);
    HRESULT hr;
    if (id == UIA_RuntimeIdPropertyId) {
        hr = value_of_runtime_id(n, out);
        if (FAILED(hr) || out->vt != VT_EMPTY)
            return hr;
        return ignore_default ? S_OK : UiaDefaultValue(info->default_kind, out);
    }
    hr = n->ops->get_prop(n, id, out);
    if (FAILED(hr))
        return hr;
    if (out->vt != VT_EMPTY && FAILED(UiaCoerceProperty(id, out)))
        VariantClear(out);
    if (out->vt == VT_EMPTY && info) {
        if (id == UIA_ProcessIdPropertyId) {
            DWORD pid = 0;
            if (SUCCEEDED(n->ops->process_id(n, &pid))) {
                out->vt = VT_I4;
                out->lVal = (LONG)pid;
            }
        } else if (info->availability) {
            PATTERN *pattern = nullptr;
            hr = n->ops->get_pattern(n, info->pattern, &pattern);
            if (FAILED(hr))
                return hr;
            out->vt = VT_BOOL;
            out->boolVal = pattern ? VARIANT_TRUE : VARIANT_FALSE;
            PatternRelease(pattern);
            return S_OK;
        } else if (info->pattern) {
            hr = answer_from_pattern(n, info, id, out);
            if (FAILED(hr))
                return hr;
        } else if (id == UIA_LocalizedControlTypePropertyId) {
            VARIANT type;
            hr = NodeGetProperty(n, UIA_ControlTypePropertyId, FALSE, &type);
            if (SUCCEEDED(hr) && type.vt == VT_I4) {
                const WCHAR *name = UiaLocalizedControlType(type.lVal);
                if (name) {
                    out->vt = VT_BSTR;
                    out->bstrVal = SysAllocString(name);
                }
            }
            VariantClear(&type);
        }
    }
    if (out->vt != VT_EMPTY && FAILED(UiaCoerceProperty(id, out)))
        VariantClear(out);
    if (out->vt != VT_EMPTY) {
        NormalizeValue(out);
        return S_OK;
    }
    if (ignore_default) {
        out->vt = VT_UNKNOWN;
        out->punkVal = UiaNotSupportedValue();
        out->punkVal->lpVtbl->AddRef(out->punkVal);
        return S_OK;
    }
    if (!info)
        return S_OK;
    return UiaDefaultValue(info->default_kind, out);
}

HRESULT NodeBoundingRect(NODE *n, UiaRect *rect, BOOL *has)
{
    memset(rect, 0, sizeof(*rect));
    *has = FALSE;
    VARIANT v;
    HRESULT hr = NodeGetProperty(n, UIA_BoundingRectanglePropertyId, TRUE, &v);
    if (FAILED(hr))
        return hr;
    if (v.vt == (VT_ARRAY | VT_R8) && v.parray) {
        LONG lower, upper;
        double *data;
        SafeArrayGetLBound(v.parray, 1, &lower);
        SafeArrayGetUBound(v.parray, 1, &upper);
        if (upper - lower + 1 >= 4 && SUCCEEDED(SafeArrayAccessData(v.parray, (void **)&data))) {
            rect->left = data[0];
            rect->top = data[1];
            rect->width = data[2];
            rect->height = data[3];
            SafeArrayUnaccessData(v.parray);
            *has = rect->width > 0 && rect->height > 0;
        }
    }
    VariantClear(&v);
    return S_OK;
}

HRESULT NodeClickablePoint(NODE *n, POINT *point, BOOL *has)
{
    point->x = point->y = 0;
    *has = FALSE;
    VARIANT v;
    HRESULT hr = NodeGetProperty(n, UIA_ClickablePointPropertyId, TRUE, &v);
    if (FAILED(hr))
        return hr;
    if (v.vt == (VT_ARRAY | VT_R8) && v.parray) {
        LONG lower, upper;
        double *data;
        SafeArrayGetLBound(v.parray, 1, &lower);
        SafeArrayGetUBound(v.parray, 1, &upper);
        if (upper - lower + 1 >= 2 && SUCCEEDED(SafeArrayAccessData(v.parray, (void **)&data))) {
            point->x = (LONG)data[0];
            point->y = (LONG)data[1];
            SafeArrayUnaccessData(v.parray);
            *has = TRUE;
        }
    }
    VariantClear(&v);
    if (*has)
        return S_OK;
    UiaRect rect;
    BOOL got;
    hr = NodeBoundingRect(n, &rect, &got);
    if (FAILED(hr) || !got)
        return hr;
    VARIANT offscreen;
    if (SUCCEEDED(NodeGetProperty(n, UIA_IsOffscreenPropertyId, FALSE, &offscreen)) && offscreen.vt == VT_BOOL &&
        offscreen.boolVal)
        return S_OK;
    point->x = (LONG)(rect.left + rect.width / 2);
    point->y = (LONG)(rect.top + rect.height / 2);
    *has = TRUE;
    return S_OK;
}

HWND NodeOwnerWindow(NODE *n)
{
    VARIANT v;
    HWND hwnd = nullptr;
    if (SUCCEEDED(NodeGetProperty(n, UIA_NativeWindowHandlePropertyId, TRUE, &v)) && v.vt == VT_I4)
        hwnd = (HWND)(LONG_PTR)v.lVal;
    VariantClear(&v);
    if (hwnd)
        return hwnd;
    NODE *root = nullptr;
    if (n->ops->is_fragment(n) && SUCCEEDED(n->ops->fragment_root(n, &root)) && root) {
        root->ops->host_hwnd(root, &hwnd);
        NodeRelease(root);
    }
    if (!hwnd)
        n->ops->host_hwnd(n, &hwnd);
    return hwnd;
}

BOOL NodeSame(NODE *a, NODE *b)
{
    if (a == b)
        return TRUE;
    if (is_provider_node(a) && is_provider_node(b) && PROV(a)->simple == PROV(b)->simple)
        return TRUE;
    int *ra, *rb, na, nb;
    BOOL same = FALSE;
    if (SUCCEEDED(NodeRuntimeId(a, &ra, &na)) && SUCCEEDED(NodeRuntimeId(b, &rb, &nb))) {
        same = na && !UiaCompareRuntimeIds(ra, na, rb, nb);
        UiaFree(rb);
        UiaFree(ra);
    }
    return same;
}

/*
 * moving through the tree. a fragment root that a window hosts has the place of the window: nothing above or beside it
 * in its own tree, so the window's proxy says what is.
 */
HRESULT NodeNavigate(NODE *n, NavigateDirection dir, NODE **out)
{
    HRESULT hr = n->ops->navigate(n, dir, out);
    if (FAILED(hr) || *out)
        return hr;
    if (dir != NavigateDirection_Parent && dir != NavigateDirection_NextSibling &&
        dir != NavigateDirection_PreviousSibling)
        return hr;
    if (!is_provider_node(n))
        return hr;
    HWND host = nullptr;
    n->ops->host_hwnd(n, &host);
    if (!host || !IsWindow(host))
        return hr;
    NODE *proxy = nullptr;
    hr = ProxyNodeFromWindow(host, &proxy);
    if (FAILED(hr))
        return hr;
    hr = proxy->ops->navigate(proxy, dir, out);
    NodeRelease(proxy);
    return hr;
}
