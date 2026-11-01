/*
 * the bridge between providers and active accessibility. a provider seen as an accessible object is what a client of
 * MSAA gets from a window that answers WM_GETOBJECT with a provider, and an accessible object seen as a provider is
 * what UiaProviderFromIAccessible gives. both wrappers answer the service that tells them from plain objects, and give
 * the object they wrap through the service of wrapped objects.
 */
#include "uia.h"

typedef struct IServiceProvider IServiceProvider;
typedef struct IServiceProviderVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IServiceProvider *, REFIID, void **);
    ULONG(STDMETHODCALLTYPE *AddRef)(IServiceProvider *);
    ULONG(STDMETHODCALLTYPE *Release)(IServiceProvider *);
    HRESULT(STDMETHODCALLTYPE *QueryService)(IServiceProvider *, REFGUID, REFIID, void **);
} IServiceProviderVtbl;
struct IServiceProvider {
    const IServiceProviderVtbl *lpVtbl;
};

static const IID IID_IServiceProvider_ = {0x6d5140c1, 0x7436, 0x11ce, {0x80, 0x34, 0x00, 0xaa, 0x00, 0x60, 0x09, 0xfa}};
static const GUID SID_WrapsObject = {0xb96fdb85, 0x7204, 0x4724, {0x84, 0x2b, 0xc7, 0x05, 0x9d, 0xed, 0xb9, 0xd0}};
static const GUID SID_WrappedObject_ = {0xad6af07f, 0x2bca, 0x4858, {0x9a, 0x1a, 0xc8, 0xd0, 0x99, 0xb8, 0x40, 0xe7}};

#define ROLE_PUSHBUTTON 0x2b

static int role_of_control_type(int type)
{
    switch (type) {
    case UIA_ButtonControlTypeId: return ROLE_PUSHBUTTON;
    case UIA_CalendarControlTypeId: return 0x1;
    case UIA_CheckBoxControlTypeId: return 0x2c;
    case UIA_ComboBoxControlTypeId: return 0x2e;
    case UIA_EditControlTypeId: return 0x2a;
    case UIA_HyperlinkControlTypeId: return 0x1e;
    case UIA_ImageControlTypeId: return 0x28;
    case UIA_ListItemControlTypeId: return 0x22;
    case UIA_ListControlTypeId: return 0x21;
    case UIA_MenuControlTypeId: return 0xb;
    case UIA_MenuBarControlTypeId: return 0x2;
    case UIA_MenuItemControlTypeId: return 0xc;
    case UIA_ProgressBarControlTypeId: return 0x30;
    case UIA_RadioButtonControlTypeId: return 0x2d;
    case UIA_ScrollBarControlTypeId: return 0x3;
    case UIA_SliderControlTypeId: return 0x33;
    case UIA_SpinnerControlTypeId: return 0x34;
    case UIA_StatusBarControlTypeId: return 0x17;
    case UIA_TabControlTypeId: return 0x3c;
    case UIA_TabItemControlTypeId: return 0x25;
    case UIA_TextControlTypeId: return 0x29;
    case UIA_ToolBarControlTypeId: return 0x16;
    case UIA_ToolTipControlTypeId: return 0xd;
    case UIA_TreeControlTypeId: return 0x23;
    case UIA_TreeItemControlTypeId: return 0x24;
    case UIA_GroupControlTypeId: return 0x14;
    case UIA_ThumbControlTypeId: return 0x4;
    case UIA_DataGridControlTypeId: return 0x18;
    case UIA_DataItemControlTypeId: return 0x1d;
    case UIA_DocumentControlTypeId: return 0xf;
    case UIA_SplitButtonControlTypeId: return 0x3e;
    case UIA_WindowControlTypeId: return 0x9;
    case UIA_PaneControlTypeId: return 0x10;
    case UIA_HeaderControlTypeId: return 0x19;
    case UIA_HeaderItemControlTypeId: return 0x19;
    case UIA_TableControlTypeId: return 0x18;
    case UIA_TitleBarControlTypeId: return 0x1;
    case UIA_SeparatorControlTypeId: return 0x15;
    }
    return 0xa;
}

/* ------------------------------------------------------------------ an accessible object as a provider */

typedef struct NPROV {
    IRawElementProviderSimple simple;
    IRawElementProviderFragment fragment;
    IRawElementProviderFragmentRoot root;
    ILegacyIAccessibleProvider legacy;
    IServiceProvider service;
    LONG refs;
    NODE *node;
} NPROV;

#define NP(p, field) ((NPROV *)((char *)(p) - offsetof(NPROV, field)))

static HRESULT provider_of_node(NODE *node, NPROV **out);

static HRESULT WINAPI np_query(NPROV *n, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IRawElementProviderSimple))
        *out = &n->simple;
    else if (IsEqualIID(riid, &IID_IRawElementProviderFragment))
        *out = &n->fragment;
    else if (IsEqualIID(riid, &IID_IRawElementProviderFragmentRoot))
        *out = &n->root;
    else if (IsEqualIID(riid, &IID_ILegacyIAccessibleProvider))
        *out = &n->legacy;
    else if (IsEqualIID(riid, &IID_IServiceProvider_))
        *out = &n->service;
    if (!*out)
        return E_NOINTERFACE;
    InterlockedIncrement(&n->refs);
    return S_OK;
}

static ULONG WINAPI np_addref(NPROV *n)
{
    return (ULONG)InterlockedIncrement(&n->refs);
}

static ULONG WINAPI np_release(NPROV *n)
{
    LONG refs = InterlockedDecrement(&n->refs);
    if (!refs) {
        NodeRelease(n->node);
        UiaFree(n);
    }
    return (ULONG)refs;
}

#define NP_UNKNOWN(prefix, Type, field)                                                                                \
    static HRESULT WINAPI prefix##_query(Type *p, REFIID riid, void **out) { return np_query(NP(p, field), riid, out); } \
    static ULONG WINAPI prefix##_addref(Type *p) { return np_addref(NP(p, field)); }                                   \
    static ULONG WINAPI prefix##_release(Type *p) { return np_release(NP(p, field)); }

NP_UNKNOWN(ps, IRawElementProviderSimple, simple)
NP_UNKNOWN(pf, IRawElementProviderFragment, fragment)
NP_UNKNOWN(pr, IRawElementProviderFragmentRoot, root)
NP_UNKNOWN(pl, ILegacyIAccessibleProvider, legacy)

static HRESULT WINAPI pv_query(IServiceProvider *p, REFIID riid, void **out)
{
    return np_query(NP(p, service), riid, out);
}
static ULONG WINAPI pv_addref(IServiceProvider *p)
{
    return np_addref(NP(p, service));
}
static ULONG WINAPI pv_release(IServiceProvider *p)
{
    return np_release(NP(p, service));
}

/* the accessible object a provider of a proxy stands for, when it stands for one. */
static IAccessible *wrapped_accessible(NODE *n, VARIANT *child)
{
    PATTERN *legacy = nullptr;
    IAccessible *acc = nullptr;
    VariantInit(child);
    child->vt = VT_I4;
    if (FAILED(NodeGetPattern(n, UIA_LegacyIAccessiblePatternId, &legacy)) || !legacy)
        return nullptr;
    VARIANT ret;
    if (SUCCEEDED(PatternCall(legacy, PM_Legacy_GetIAccessible, nullptr, 0, &ret)) && ret.vt == VT_UNKNOWN)
        acc = (IAccessible *)ret.punkVal;
    if (SUCCEEDED(PatternCall(legacy, PM_Legacy_ChildId, nullptr, 0, &ret)) && ret.vt == VT_I4)
        child->lVal = ret.lVal;
    PatternRelease(legacy);
    return acc;
}

static HRESULT WINAPI pv_service(IServiceProvider *p, REFGUID service, REFIID riid, void **out)
{
    NPROV *n = NP(p, service);
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!IsEqualGUID(service, &SID_WrapsObject) && !IsEqualGUID(service, &SID_WrappedObject_))
        return E_NOINTERFACE;
    VARIANT child;
    IAccessible *acc = wrapped_accessible(n->node, &child);
    if (!acc)
        return E_NOINTERFACE;
    if (IsEqualGUID(service, &SID_WrapsObject)) {
        acc->lpVtbl->Release(acc);
        return np_query(n, riid, out);
    }
    HRESULT hr = acc->lpVtbl->QueryInterface(acc, riid, out);
    acc->lpVtbl->Release(acc);
    return hr;
}

static const IServiceProviderVtbl g_service_vtbl = {pv_query, pv_addref, pv_release, pv_service};

static HRESULT WINAPI ps_options(IRawElementProviderSimple *p, ProviderOptions *options)
{
    *options = ProviderOptions_ClientSideProvider;
    return S_OK;
}

static HRESULT WINAPI ps_pattern(IRawElementProviderSimple *p, PATTERNID id, IUnknown **out)
{
    NPROV *n = NP(p, simple);
    *out = nullptr;
    if (id == UIA_LegacyIAccessiblePatternId) {
        PATTERN *legacy = nullptr;
        if (SUCCEEDED(NodeGetPattern(n->node, id, &legacy)) && legacy) {
            PatternRelease(legacy);
            *out = (IUnknown *)&n->legacy;
            np_addref(n);
        }
    }
    return S_OK;
}

static HRESULT WINAPI ps_property(IRawElementProviderSimple *p, PROPERTYID id, VARIANT *v)
{
    NPROV *n = NP(p, simple);
    return NodeGetProperty(n->node, id, TRUE, v);
}

static HRESULT WINAPI ps_host(IRawElementProviderSimple *p, IRawElementProviderSimple **host)
{
    *host = nullptr;
    return S_OK;
}

static const IRawElementProviderSimpleVtbl g_simple_vtbl = {ps_query, ps_addref, ps_release, ps_options, ps_pattern,
                                                            ps_property, ps_host};

static HRESULT fragment_out(NODE *node, IRawElementProviderFragment **out)
{
    *out = nullptr;
    if (!node)
        return S_OK;
    NPROV *n;
    HRESULT hr = provider_of_node(node, &n);
    if (SUCCEEDED(hr))
        *out = &n->fragment;
    return hr;
}

static HRESULT WINAPI pf_navigate(IRawElementProviderFragment *p, NavigateDirection dir, IRawElementProviderFragment **out)
{
    NODE *found = nullptr;
    HRESULT hr = NodeNavigate(NP(p, fragment)->node, dir, &found);
    if (FAILED(hr)) {
        *out = nullptr;
        return hr;
    }
    hr = fragment_out(found, out);
    NodeRelease(found);
    return hr;
}

static HRESULT WINAPI pf_runtime_id(IRawElementProviderFragment *p, SAFEARRAY **out)
{
    int *ids;
    int count;
    *out = nullptr;
    HRESULT hr = NodeRuntimeId(NP(p, fragment)->node, &ids, &count);
    if (FAILED(hr))
        return hr;
    if (count)
        hr = UiaIntArrayToSafeArray(ids, count, out);
    UiaFree(ids);
    return hr;
}

static HRESULT WINAPI pf_rect(IRawElementProviderFragment *p, UiaRect *rect)
{
    BOOL has;
    return NodeBoundingRect(NP(p, fragment)->node, rect, &has);
}

static HRESULT WINAPI pf_embedded(IRawElementProviderFragment *p, SAFEARRAY **out)
{
    *out = nullptr;
    return S_OK;
}

static HRESULT WINAPI pf_focus(IRawElementProviderFragment *p)
{
    NODE *n = NP(p, fragment)->node;
    return n->ops->set_focus(n);
}

static HRESULT WINAPI pf_root(IRawElementProviderFragment *p, IRawElementProviderFragmentRoot **out)
{
    NODE *root = nullptr;
    *out = nullptr;
    NODE *n = NP(p, fragment)->node;
    HRESULT hr = n->ops->fragment_root(n, &root);
    if (FAILED(hr) || !root)
        return hr;
    NPROV *r;
    hr = provider_of_node(root, &r);
    NodeRelease(root);
    if (SUCCEEDED(hr))
        *out = &r->root;
    return hr;
}

static const IRawElementProviderFragmentVtbl g_fragment_vtbl = {pf_query,  pf_addref,  pf_release, pf_navigate,
                                                                pf_runtime_id, pf_rect, pf_embedded, pf_focus, pf_root};

static HRESULT WINAPI pr_from_point(IRawElementProviderFragmentRoot *p, double x, double y, IRawElementProviderFragment **out)
{
    NODE *found = nullptr;
    NODE *n = NP(p, root)->node;
    *out = nullptr;
    HRESULT hr = n->ops->root_from_point(n, x, y, &found);
    if (FAILED(hr))
        return hr;
    hr = fragment_out(found, out);
    NodeRelease(found);
    return hr;
}

static HRESULT WINAPI pr_focus(IRawElementProviderFragmentRoot *p, IRawElementProviderFragment **out)
{
    NODE *found = nullptr;
    NODE *n = NP(p, root)->node;
    *out = nullptr;
    HRESULT hr = n->ops->root_focus(n, &found);
    if (FAILED(hr))
        return hr;
    hr = fragment_out(found, out);
    NodeRelease(found);
    return hr;
}

static const IRawElementProviderFragmentRootVtbl g_root_vtbl = {pr_query, pr_addref, pr_release, pr_from_point, pr_focus};

static HRESULT legacy_value(NPROV *n, int method, VARIANT *ret)
{
    PATTERN *legacy = nullptr;
    HRESULT hr = NodeGetPattern(n->node, UIA_LegacyIAccessiblePatternId, &legacy);
    if (FAILED(hr))
        return hr;
    if (!legacy)
        return UIA_E_NOTSUPPORTED;
    hr = PatternCall(legacy, method, nullptr, 0, ret);
    PatternRelease(legacy);
    return hr;
}

static HRESULT legacy_action(NPROV *n, int method, VARIANT *args, ULONG count)
{
    PATTERN *legacy = nullptr;
    HRESULT hr = NodeGetPattern(n->node, UIA_LegacyIAccessiblePatternId, &legacy);
    if (FAILED(hr))
        return hr;
    if (!legacy)
        return UIA_E_NOTSUPPORTED;
    hr = PatternCall(legacy, method, args, count, nullptr);
    PatternRelease(legacy);
    return hr;
}

static HRESULT WINAPI pl_select(ILegacyIAccessibleProvider *p, LONG flags)
{
    VARIANT a;
    VariantInit(&a);
    a.vt = VT_I4;
    a.lVal = flags;
    return legacy_action(NP(p, legacy), PM_Legacy_Select, &a, 1);
}
static HRESULT WINAPI pl_default(ILegacyIAccessibleProvider *p)
{
    return legacy_action(NP(p, legacy), PM_Legacy_DoDefaultAction, nullptr, 0);
}
static HRESULT WINAPI pl_set_value(ILegacyIAccessibleProvider *p, LPCWSTR value)
{
    VARIANT a;
    VariantInit(&a);
    a.vt = VT_BSTR;
    a.bstrVal = SysAllocString(value);
    HRESULT hr = legacy_action(NP(p, legacy), PM_Legacy_SetValue, &a, 1);
    VariantClear(&a);
    return hr;
}
static HRESULT WINAPI pl_accessible(ILegacyIAccessibleProvider *p, IAccessible **out)
{
    VARIANT ret;
    *out = nullptr;
    HRESULT hr = legacy_value(NP(p, legacy), PM_Legacy_GetIAccessible, &ret);
    if (SUCCEEDED(hr) && ret.vt == VT_UNKNOWN) {
        *out = (IAccessible *)ret.punkVal;
        ret.vt = VT_EMPTY;
    }
    VariantClear(&ret);
    return hr;
}
static HRESULT WINAPI pl_child(ILegacyIAccessibleProvider *p, int *out)
{
    VARIANT ret;
    *out = 0;
    HRESULT hr = legacy_value(NP(p, legacy), PM_Legacy_ChildId, &ret);
    if (SUCCEEDED(hr) && ret.vt == VT_I4)
        *out = ret.lVal;
    return hr;
}

#define PL_STRING(NAME, METHOD)                                                                                        \
    static HRESULT WINAPI pl_##NAME(ILegacyIAccessibleProvider *p, BSTR *out)                                          \
    {                                                                                                                  \
        VARIANT ret;                                                                                                   \
        *out = nullptr;                                                                                                \
        HRESULT hr = legacy_value(NP(p, legacy), METHOD, &ret);                                                        \
        if (SUCCEEDED(hr) && ret.vt == VT_BSTR) {                                                                      \
            *out = ret.bstrVal;                                                                                        \
            ret.vt = VT_EMPTY;                                                                                         \
        }                                                                                                              \
        VariantClear(&ret);                                                                                            \
        return hr;                                                                                                     \
    }
PL_STRING(name, PM_Legacy_Name)
PL_STRING(value, PM_Legacy_Value)
PL_STRING(description, PM_Legacy_Description)
PL_STRING(help, PM_Legacy_Help)
PL_STRING(shortcut, PM_Legacy_KeyboardShortcut)
PL_STRING(default_action, PM_Legacy_DefaultAction)

static HRESULT WINAPI pl_role(ILegacyIAccessibleProvider *p, DWORD *out)
{
    VARIANT ret;
    *out = 0;
    HRESULT hr = legacy_value(NP(p, legacy), PM_Legacy_Role, &ret);
    if (SUCCEEDED(hr) && ret.vt == VT_I4)
        *out = (DWORD)ret.lVal;
    return hr;
}
static HRESULT WINAPI pl_state(ILegacyIAccessibleProvider *p, DWORD *out)
{
    VARIANT ret;
    *out = 0;
    HRESULT hr = legacy_value(NP(p, legacy), PM_Legacy_State, &ret);
    if (SUCCEEDED(hr) && ret.vt == VT_I4)
        *out = (DWORD)ret.lVal;
    return hr;
}
static HRESULT WINAPI pl_selection(ILegacyIAccessibleProvider *p, SAFEARRAY **out)
{
    *out = SafeArrayCreateVector(VT_UNKNOWN, 0, 0);
    return *out ? S_OK : E_OUTOFMEMORY;
}

static const ILegacyIAccessibleProviderVtbl g_legacy_vtbl = {
    pl_query,  pl_addref, pl_release, pl_select, pl_default,    pl_set_value, pl_accessible,
    pl_child,  pl_name,   pl_value,   pl_description, pl_role,  pl_state,     pl_help,
    pl_shortcut, pl_selection, pl_default_action,
};

static HRESULT provider_of_node(NODE *node, NPROV **out)
{
    *out = nullptr;
    NPROV *n = UiaAlloc(sizeof(*n));
    if (!n)
        return E_OUTOFMEMORY;
    n->simple.lpVtbl = &g_simple_vtbl;
    n->fragment.lpVtbl = &g_fragment_vtbl;
    n->root.lpVtbl = &g_root_vtbl;
    n->legacy.lpVtbl = &g_legacy_vtbl;
    n->service.lpVtbl = &g_service_vtbl;
    n->refs = 1;
    n->node = node;
    NodeAddRef(node);
    *out = n;
    return S_OK;
}

/* a node as a provider, with the node's own provider when it has one. */
HRESULT ProviderOfNode(NODE *node, IRawElementProviderSimple **out)
{
    *out = nullptr;
    IRawElementProviderSimple *own = node->ops->provider(node);
    if (own) {
        own->lpVtbl->AddRef(own);
        *out = own;
        return S_OK;
    }
    NPROV *n;
    HRESULT hr = provider_of_node(node, &n);
    if (SUCCEEDED(hr))
        *out = &n->simple;
    return hr;
}

HRESULT WINAPI UiaProviderFromIAccessible(IAccessible *accessible, LONG child, DWORD flags,
                                          IRawElementProviderSimple **provider)
{
    if (!provider)
        return E_POINTER;
    *provider = nullptr;
    if (!accessible || (flags & 0xFFFFFFFE))
        return E_INVALIDARG;
    IServiceProvider *service = nullptr;
    if (SUCCEEDED(accessible->lpVtbl->QueryInterface(accessible, &IID_IServiceProvider_, (void **)&service))) {
        void *wrapper = nullptr;
        BOOL wraps = SUCCEEDED(service->lpVtbl->QueryService(service, &SID_WrapsObject, &IID_IUnknown, &wrapper));
        if (wrapper)
            ((IUnknown *)wrapper)->lpVtbl->Release((IUnknown *)wrapper);
        if (wraps) {
            if (!(flags & 1)) {
                service->lpVtbl->Release(service);
                return E_INVALIDARG;
            }
            HRESULT hr = service->lpVtbl->QueryService(service, &SID_WrappedObject_, &IID_IRawElementProviderSimple,
                                                       (void **)provider);
            service->lpVtbl->Release(service);
            return hr == S_OK && *provider ? S_OK : E_INVALIDARG;
        }
        service->lpVtbl->Release(service);
    }
    HWND hwnd = nullptr;
    HRESULT hr = WindowFromAccessibleObject(accessible, &hwnd);
    if (FAILED(hr))
        return hr;
    if (!hwnd)
        return E_FAIL;
    NODE *node = nullptr;
    hr = ProxyNodeFromAccessible(accessible, child, &node);
    if (FAILED(hr))
        return hr;
    hr = ProviderOfNode(node, provider);
    NodeRelease(node);
    return hr;
}

HRESULT WINAPI UiaProviderForNonClient(HWND hwnd, int object, int child, IRawElementProviderSimple **provider)
{
    if (!provider)
        return E_INVALIDARG;
    *provider = nullptr;
    if (!hwnd)
        return E_INVALIDARG;
    if (!IsWindow(hwnd))
        return UIA_E_ELEMENTNOTAVAILABLE;
    IAccessible *acc = nullptr;
    HRESULT hr = AccessibleObjectFromWindow(hwnd, (DWORD)object, &IID_IAccessible, (void **)&acc);
    if (FAILED(hr))
        return hr;
    NODE *node = nullptr;
    hr = ProxyNodeFromAccessible(acc, child, &node);
    acc->lpVtbl->Release(acc);
    if (FAILED(hr))
        return hr;
    hr = ProviderOfNode(node, provider);
    NodeRelease(node);
    return hr;
}

/* ------------------------------------------------------------------ a provider as an accessible object */

typedef struct PACC {
    const IAccessibleVtbl *accessible;
    IServiceProvider service;
    LONG refs;
    NODE *node;
    HWND hwnd;
} PACC;

#define PA(p) ((PACC *)(p))

static const IAccessibleVtbl g_accessible_vtbl;

static HRESULT paccessible_make(NODE *node, HWND hwnd, PACC **out)
{
    *out = nullptr;
    PACC *a = UiaAlloc(sizeof(*a));
    if (!a)
        return E_OUTOFMEMORY;
    a->accessible = &g_accessible_vtbl;
    a->refs = 1;
    a->node = node;
    NodeAddRef(node);
    a->hwnd = hwnd;
    *out = a;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE a_query(IAccessible *p, REFIID riid, void **out)
{
    PACC *a = PA(p);
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch) || IsEqualIID(riid, &IID_IAccessible))
        *out = a;
    else if (IsEqualIID(riid, &IID_IServiceProvider_))
        *out = &a->service;
    if (!*out)
        return E_NOINTERFACE;
    InterlockedIncrement(&a->refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE a_addref(IAccessible *p)
{
    return (ULONG)InterlockedIncrement(&PA(p)->refs);
}

static ULONG STDMETHODCALLTYPE a_release(IAccessible *p)
{
    PACC *a = PA(p);
    LONG refs = InterlockedDecrement(&a->refs);
    if (!refs) {
        NodeRelease(a->node);
        UiaFree(a);
    }
    return (ULONG)refs;
}

#define SERVICE_PACC(p) ((PACC *)((char *)(p) - offsetof(PACC, service)))

static HRESULT WINAPI as_query(IServiceProvider *p, REFIID riid, void **out)
{
    return a_query((IAccessible *)SERVICE_PACC(p), riid, out);
}
static ULONG WINAPI as_addref(IServiceProvider *p)
{
    return a_addref((IAccessible *)SERVICE_PACC(p));
}
static ULONG WINAPI as_release(IServiceProvider *p)
{
    return a_release((IAccessible *)SERVICE_PACC(p));
}

static HRESULT WINAPI as_service(IServiceProvider *p, REFGUID service, REFIID riid, void **out)
{
    PACC *a = SERVICE_PACC(p);
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (IsEqualGUID(service, &SID_WrapsObject))
        return a_query((IAccessible *)a, riid, out);
    if (IsEqualGUID(service, &SID_WrappedObject_)) {
        IRawElementProviderSimple *provider = a->node->ops->provider(a->node);
        if (!provider)
            return E_NOINTERFACE;
        return provider->lpVtbl->QueryInterface(provider, riid, out);
    }
    return E_NOINTERFACE;
}

static const IServiceProviderVtbl g_pacc_service_vtbl = {as_query, as_addref, as_release, as_service};

static HRESULT STDMETHODCALLTYPE a_count(IAccessible *p, UINT *count)
{
    *count = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE a_info(IAccessible *p, UINT index, LCID lcid, void **info)
{
    *info = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE a_names(IAccessible *p, REFIID riid, LPOLESTR *names, UINT count, LCID lcid, DISPID *ids)
{
    return DISP_E_UNKNOWNNAME;
}

static HRESULT STDMETHODCALLTYPE a_invoke(IAccessible *p, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                          DISPPARAMS *params, VARIANT *result, EXCEPINFO *ex, UINT *err)
{
    return DISP_E_MEMBERNOTFOUND;
}

/* the node a child number or the object itself names. */
static HRESULT target_of(PACC *a, VARIANT child, NODE **node)
{
    *node = nullptr;
    if (child.vt != VT_I4)
        return E_INVALIDARG;
    if (child.lVal == 0) {
        *node = a->node;
        NodeAddRef(*node);
        return S_OK;
    }
    if (child.lVal < 0)
        return E_INVALIDARG;
    NODE *current = nullptr;
    HRESULT hr = NodeNavigate(a->node, NavigateDirection_FirstChild, &current);
    for (LONG i = 1; SUCCEEDED(hr) && current && i < child.lVal; i++) {
        NODE *next = nullptr;
        hr = NodeNavigate(current, NavigateDirection_NextSibling, &next);
        NodeRelease(current);
        current = next;
    }
    if (FAILED(hr)) {
        NodeRelease(current);
        return hr;
    }
    if (!current)
        return E_INVALIDARG;
    *node = current;
    return S_OK;
}

static HRESULT accessible_out(NODE *node, HWND hwnd, IDispatch **out)
{
    PACC *a;
    HRESULT hr = paccessible_make(node, hwnd, &a);
    if (SUCCEEDED(hr))
        *out = (IDispatch *)a;
    return hr;
}

static HRESULT STDMETHODCALLTYPE a_parent(IAccessible *p, IDispatch **out)
{
    PACC *a = PA(p);
    *out = nullptr;
    NODE *parent = nullptr;
    HRESULT hr = NodeNavigate(a->node, NavigateDirection_Parent, &parent);
    if (FAILED(hr))
        return hr;
    if (parent) {
        hr = accessible_out(parent, a->hwnd, out);
        NodeRelease(parent);
        return hr;
    }
    /* a root has the window that hosts it above. */
    HWND hwnd = a->hwnd ? a->hwnd : NodeOwnerWindow(a->node);
    if (!hwnd)
        return S_FALSE;
    IAccessible *window = nullptr;
    hr = AccessibleObjectFromWindow(hwnd, OBJID_WINDOW, &IID_IAccessible, (void **)&window);
    if (SUCCEEDED(hr))
        *out = (IDispatch *)window;
    return hr;
}

static HRESULT STDMETHODCALLTYPE a_child_count(IAccessible *p, LONG *count)
{
    PACC *a = PA(p);
    *count = 0;
    NODE *child = nullptr;
    HRESULT hr = NodeNavigate(a->node, NavigateDirection_FirstChild, &child);
    while (SUCCEEDED(hr) && child && *count < 100000) {
        NODE *next = nullptr;
        (*count)++;
        hr = NodeNavigate(child, NavigateDirection_NextSibling, &next);
        NodeRelease(child);
        child = next;
    }
    NodeRelease(child);
    return hr;
}

static HRESULT STDMETHODCALLTYPE a_child(IAccessible *p, VARIANT var, IDispatch **out)
{
    PACC *a = PA(p);
    *out = nullptr;
    if (var.vt == VT_I4 && var.lVal == 0) {
        a_addref(p);
        *out = (IDispatch *)p;
        return S_OK;
    }
    NODE *node = nullptr;
    HRESULT hr = target_of(a, var, &node);
    if (FAILED(hr))
        return hr;
    hr = accessible_out(node, a->hwnd, out);
    NodeRelease(node);
    return hr;
}

static HRESULT text_property(NODE *n, PROPERTYID id, BSTR *out)
{
    VARIANT v;
    *out = nullptr;
    HRESULT hr = NodeGetProperty(n, id, TRUE, &v);
    if (FAILED(hr))
        return hr;
    if (v.vt == VT_BSTR && v.bstrVal && *v.bstrVal) {
        *out = v.bstrVal;
        return S_OK;
    }
    VariantClear(&v);
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE a_name(IAccessible *p, VARIANT var, BSTR *out)
{
    NODE *n;
    *out = nullptr;
    HRESULT hr = target_of(PA(p), var, &n);
    if (FAILED(hr))
        return hr;
    hr = text_property(n, UIA_NamePropertyId, out);
    NodeRelease(n);
    return hr;
}

static HRESULT STDMETHODCALLTYPE a_value(IAccessible *p, VARIANT var, BSTR *out)
{
    NODE *n;
    *out = nullptr;
    HRESULT hr = target_of(PA(p), var, &n);
    if (FAILED(hr))
        return hr;
    hr = text_property(n, UIA_ValueValuePropertyId, out);
    if (hr == S_FALSE) {
        VARIANT v;
        if (SUCCEEDED(NodeGetProperty(n, UIA_RangeValueValuePropertyId, TRUE, &v)) && v.vt == VT_R8) {
            WCHAR text[32];
            _snwprintf(text, RTL_NUMBER_OF(text), L"%d", (int)v.dblVal);
            *out = SysAllocString(text);
            hr = S_OK;
        }
    }
    NodeRelease(n);
    return hr;
}

static HRESULT STDMETHODCALLTYPE a_description(IAccessible *p, VARIANT var, BSTR *out)
{
    NODE *n;
    *out = nullptr;
    HRESULT hr = target_of(PA(p), var, &n);
    if (FAILED(hr))
        return hr;
    hr = text_property(n, UIA_LegacyIAccessibleDescriptionPropertyId, out);
    NodeRelease(n);
    return hr;
}

static HRESULT STDMETHODCALLTYPE a_role(IAccessible *p, VARIANT var, VARIANT *out)
{
    NODE *n;
    VariantInit(out);
    HRESULT hr = target_of(PA(p), var, &n);
    if (FAILED(hr))
        return hr;
    VARIANT v;
    int role = 0xa;
    if (SUCCEEDED(NodeGetProperty(n, UIA_LegacyIAccessibleRolePropertyId, TRUE, &v)) && v.vt == VT_I4 && v.lVal) {
        role = v.lVal;
    } else if (SUCCEEDED(NodeGetProperty(n, UIA_ControlTypePropertyId, FALSE, &v)) && v.vt == VT_I4) {
        role = role_of_control_type(v.lVal);
    }
    out->vt = VT_I4;
    out->lVal = role;
    NodeRelease(n);
    return S_OK;
}

static BOOL flag_property(NODE *n, PROPERTYID id, BOOL fallback)
{
    VARIANT v;
    BOOL result = fallback;
    if (SUCCEEDED(NodeGetProperty(n, id, FALSE, &v)) && v.vt == VT_BOOL)
        result = v.boolVal != 0;
    VariantClear(&v);
    return result;
}

static HRESULT STDMETHODCALLTYPE a_state(IAccessible *p, VARIANT var, VARIANT *out)
{
    NODE *n;
    VariantInit(out);
    HRESULT hr = target_of(PA(p), var, &n);
    if (FAILED(hr))
        return hr;
    VARIANT v;
    LONG state = 0;
    if (SUCCEEDED(NodeGetProperty(n, UIA_LegacyIAccessibleStatePropertyId, TRUE, &v)) && v.vt == VT_I4 && v.lVal) {
        state = v.lVal;
    } else {
        if (!flag_property(n, UIA_IsEnabledPropertyId, TRUE))
            state |= 0x1;
        if (flag_property(n, UIA_IsOffscreenPropertyId, FALSE))
            state |= 0x10000;
        if (flag_property(n, UIA_HasKeyboardFocusPropertyId, FALSE))
            state |= 0x4;
        if (flag_property(n, UIA_IsKeyboardFocusablePropertyId, FALSE))
            state |= 0x100000;
        if (flag_property(n, UIA_IsPasswordPropertyId, FALSE))
            state |= 0x20000000;
        if (SUCCEEDED(NodeGetProperty(n, UIA_ToggleToggleStatePropertyId, TRUE, &v)) && v.vt == VT_I4)
            state |= v.lVal == ToggleState_On ? 0x10 : v.lVal == ToggleState_Indeterminate ? 0x20 : 0;
        if (SUCCEEDED(NodeGetProperty(n, UIA_ExpandCollapseExpandCollapseStatePropertyId, TRUE, &v)) && v.vt == VT_I4)
            state |= v.lVal == ExpandCollapseState_Expanded ? 0x200 : v.lVal == ExpandCollapseState_Collapsed ? 0x400 : 0;
        if (flag_property(n, UIA_SelectionItemIsSelectedPropertyId, FALSE))
            state |= 0x2;
    }
    out->vt = VT_I4;
    out->lVal = state;
    NodeRelease(n);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE a_help(IAccessible *p, VARIANT var, BSTR *out)
{
    NODE *n;
    *out = nullptr;
    HRESULT hr = target_of(PA(p), var, &n);
    if (FAILED(hr))
        return hr;
    hr = text_property(n, UIA_HelpTextPropertyId, out);
    NodeRelease(n);
    return hr;
}

static HRESULT STDMETHODCALLTYPE a_help_topic(IAccessible *p, BSTR *file, VARIANT var, LONG *topic)
{
    *file = nullptr;
    *topic = 0;
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE a_shortcut(IAccessible *p, VARIANT var, BSTR *out)
{
    NODE *n;
    *out = nullptr;
    HRESULT hr = target_of(PA(p), var, &n);
    if (FAILED(hr))
        return hr;
    hr = text_property(n, UIA_AccessKeyPropertyId, out);
    if (hr == S_FALSE)
        hr = text_property(n, UIA_AcceleratorKeyPropertyId, out);
    NodeRelease(n);
    return hr;
}

static HRESULT STDMETHODCALLTYPE a_focus(IAccessible *p, VARIANT *out)
{
    PACC *a = PA(p);
    VariantInit(out);
    NODE *focus = nullptr;
    if (FAILED(a->node->ops->root_focus(a->node, &focus)) || !focus)
        return S_FALSE;
    if (NodeSame(focus, a->node)) {
        out->vt = VT_I4;
        out->lVal = 0;
    } else {
        IDispatch *d;
        if (SUCCEEDED(accessible_out(focus, a->hwnd, &d))) {
            out->vt = VT_DISPATCH;
            out->pdispVal = d;
        }
    }
    NodeRelease(focus);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE a_selection(IAccessible *p, VARIANT *out)
{
    VariantInit(out);
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE a_default_action(IAccessible *p, VARIANT var, BSTR *out)
{
    NODE *n;
    *out = nullptr;
    HRESULT hr = target_of(PA(p), var, &n);
    if (FAILED(hr))
        return hr;
    hr = text_property(n, UIA_LegacyIAccessibleDefaultActionPropertyId, out);
    NodeRelease(n);
    return hr;
}

static HRESULT STDMETHODCALLTYPE a_select(IAccessible *p, LONG flags, VARIANT var)
{
    NODE *n;
    HRESULT hr = target_of(PA(p), var, &n);
    if (FAILED(hr))
        return hr;
    hr = S_FALSE;
    if (flags & SELFLAG_TAKEFOCUS)
        hr = n->ops->set_focus(n);
    if (SUCCEEDED(hr) && (flags & (SELFLAG_TAKESELECTION | SELFLAG_ADDSELECTION | SELFLAG_REMOVESELECTION))) {
        PATTERN *item = nullptr;
        if (SUCCEEDED(NodeGetPattern(n, UIA_SelectionItemPatternId, &item)) && item) {
            hr = PatternCall(item, flags & SELFLAG_REMOVESELECTION ? PM_SelectionItem_RemoveFromSelection
                                    : flags & SELFLAG_ADDSELECTION ? PM_SelectionItem_AddToSelection
                                                                    : PM_SelectionItem_Select,
                             nullptr, 0, nullptr);
            PatternRelease(item);
        }
    }
    NodeRelease(n);
    return hr;
}

static HRESULT STDMETHODCALLTYPE a_location(IAccessible *p, LONG *x, LONG *y, LONG *w, LONG *h, VARIANT var)
{
    NODE *n;
    *x = *y = *w = *h = 0;
    HRESULT hr = target_of(PA(p), var, &n);
    if (FAILED(hr))
        return hr;
    UiaRect rect;
    BOOL has;
    hr = NodeBoundingRect(n, &rect, &has);
    if (SUCCEEDED(hr) && has) {
        *x = (LONG)rect.left;
        *y = (LONG)rect.top;
        *w = (LONG)rect.width;
        *h = (LONG)rect.height;
    }
    NodeRelease(n);
    return hr;
}

static HRESULT STDMETHODCALLTYPE a_navigate(IAccessible *p, LONG direction, VARIANT start, VARIANT *out)
{
    PACC *a = PA(p);
    VariantInit(out);
    NODE *from;
    HRESULT hr = target_of(a, start, &from);
    if (FAILED(hr))
        return hr;
    NavigateDirection dir;
    switch (direction) {
    case NAVDIR_NEXT: dir = NavigateDirection_NextSibling; break;
    case NAVDIR_PREVIOUS: dir = NavigateDirection_PreviousSibling; break;
    case NAVDIR_FIRSTCHILD: dir = NavigateDirection_FirstChild; break;
    case NAVDIR_LASTCHILD: dir = NavigateDirection_LastChild; break;
    default:
        NodeRelease(from);
        return S_FALSE;
    }
    NODE *found = nullptr;
    hr = NodeNavigate(from, dir, &found);
    NodeRelease(from);
    if (FAILED(hr))
        return hr;
    if (!found)
        return S_FALSE;
    IDispatch *d;
    hr = accessible_out(found, a->hwnd, &d);
    NodeRelease(found);
    if (SUCCEEDED(hr)) {
        out->vt = VT_DISPATCH;
        out->pdispVal = d;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE a_hit_test(IAccessible *p, LONG x, LONG y, VARIANT *out)
{
    PACC *a = PA(p);
    VariantInit(out);
    NODE *found = nullptr;
    HRESULT hr = a->node->ops->root_from_point(a->node, x, y, &found);
    if (FAILED(hr)) {
        UiaRect rect;
        BOOL has;
        if (SUCCEEDED(NodeBoundingRect(a->node, &rect, &has)) && has && x >= rect.left && y >= rect.top &&
            x < rect.left + rect.width && y < rect.top + rect.height) {
            out->vt = VT_I4;
            out->lVal = 0;
            return S_OK;
        }
        return S_FALSE;
    }
    if (!found)
        return S_FALSE;
    if (NodeSame(found, a->node)) {
        out->vt = VT_I4;
        out->lVal = 0;
    } else {
        IDispatch *d;
        if (SUCCEEDED(accessible_out(found, a->hwnd, &d))) {
            out->vt = VT_DISPATCH;
            out->pdispVal = d;
        }
    }
    NodeRelease(found);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE a_do_default(IAccessible *p, VARIANT var)
{
    NODE *n;
    HRESULT hr = target_of(PA(p), var, &n);
    if (FAILED(hr))
        return hr;
    static const struct {
        PATTERNID pattern;
        int method;
    } actions[] = {
        {UIA_InvokePatternId, PM_Invoke_Invoke},
        {UIA_TogglePatternId, PM_Toggle_Toggle},
        {UIA_SelectionItemPatternId, PM_SelectionItem_Select},
        {UIA_ExpandCollapsePatternId, PM_ExpandCollapse_Expand},
    };
    hr = S_FALSE;
    for (int i = 0; i < (int)RTL_NUMBER_OF(actions) && hr == S_FALSE; i++) {
        PATTERN *pattern = nullptr;
        if (SUCCEEDED(NodeGetPattern(n, actions[i].pattern, &pattern)) && pattern) {
            hr = PatternCall(pattern, actions[i].method, nullptr, 0, nullptr);
            PatternRelease(pattern);
        }
    }
    NodeRelease(n);
    return hr;
}

static HRESULT STDMETHODCALLTYPE a_put_name(IAccessible *p, VARIANT var, BSTR name)
{
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE a_put_value(IAccessible *p, VARIANT var, BSTR value)
{
    NODE *n;
    HRESULT hr = target_of(PA(p), var, &n);
    if (FAILED(hr))
        return hr;
    hr = S_FALSE;
    PATTERN *pattern = nullptr;
    if (SUCCEEDED(NodeGetPattern(n, UIA_ValuePatternId, &pattern)) && pattern) {
        VARIANT a;
        VariantInit(&a);
        a.vt = VT_BSTR;
        a.bstrVal = value;
        hr = PatternCall(pattern, PM_Value_SetValue, &a, 1, nullptr);
        PatternRelease(pattern);
    }
    NodeRelease(n);
    return hr;
}

static const IAccessibleVtbl g_accessible_vtbl = {
    a_query,  a_addref, a_release, a_count, a_info, a_names, a_invoke, a_parent, a_child_count, a_child,
    a_name,   a_value,  a_description, a_role, a_state, a_help, a_help_topic, a_shortcut, a_focus, a_selection,
    a_default_action, a_select, a_location, a_navigate, a_hit_test, a_do_default, a_put_name, a_put_value,
};

HRESULT AccessibleWrapProvider(IRawElementProviderSimple *provider, IAccessible **out, LONG *child, HWND hwnd)
{
    *out = nullptr;
    *child = 0;
    NODE *node = nullptr;
    HRESULT hr = NodeFromProvider(provider, &node);
    if (FAILED(hr))
        return hr;
    PACC *a;
    hr = paccessible_make(node, hwnd, &a);
    NodeRelease(node);
    if (SUCCEEDED(hr)) {
        a->service.lpVtbl = &g_pacc_service_vtbl;
        *out = (IAccessible *)a;
    }
    return hr;
}

HRESULT WINAPI UiaIAccessibleFromProvider(IRawElementProviderSimple *provider, DWORD flags, IAccessible **accessible,
                                          VARIANT *child)
{
    if (!accessible || !child)
        return E_POINTER;
    *accessible = nullptr;
    VariantInit(child);
    if (!provider || (flags & 0xFFFFFFFE))
        return E_INVALIDARG;
    IServiceProvider *service = nullptr;
    if (SUCCEEDED(provider->lpVtbl->QueryInterface(provider, &IID_IServiceProvider_, (void **)&service))) {
        void *marker = nullptr;
        BOOL wraps = SUCCEEDED(service->lpVtbl->QueryService(service, &SID_WrapsObject, &IID_IUnknown, &marker));
        if (marker)
            ((IUnknown *)marker)->lpVtbl->Release((IUnknown *)marker);
        service->lpVtbl->Release(service);
        if (wraps) {
            if (!(flags & 1))
                return E_INVALIDARG;
            ILegacyIAccessibleProvider *legacy = nullptr;
            if (FAILED(provider->lpVtbl->QueryInterface(provider, &IID_ILegacyIAccessibleProvider, (void **)&legacy)))
                return E_FAIL;
            int id = 0;
            HRESULT hr = legacy->lpVtbl->get_ChildId(legacy, &id);
            if (SUCCEEDED(hr)) {
                child->vt = VT_I4;
                child->lVal = id;
                hr = legacy->lpVtbl->GetIAccessible(legacy, accessible);
            }
            legacy->lpVtbl->Release(legacy);
            return hr;
        }
    }
    LONG zero;
    HRESULT hr = AccessibleWrapProvider(provider, accessible, &zero, nullptr);
    if (SUCCEEDED(hr)) {
        child->vt = VT_I4;
        child->lVal = 0;
    }
    return hr;
}
