/*
 * nodes between threads and processes. a provider is served by a wrapper, an automation object whose methods are the
 * operations of a node. the wrapper runs every call on the thread it was made on, where the provider belongs, and a
 * remote node is the other end: a node whose operations call the wrapper. a wrapper is marshaled like any object, so
 * it reaches other processes the way objects of COM do.
 */
#include "uia.h"

extern IUnknown *UiaNotSupportedValue(void);
extern IUnknown *UiaMixedValue(void);

const IID IID_IMimeUiaServer = {0x4d494d45, 0x5541, 0x5352, {0x56, 0x52, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01}};

enum {
    D_KIND = 1,
    D_PROP = 10,
    D_PATTERN,
    D_NAVIGATE,
    D_RUNTIME_ID,
    D_OPTIONS,
    D_HOST,
    D_FOCUS,
    D_ROOT_POINT,
    D_ROOT_FOCUS,
    D_FRAGMENT_ROOT,
    D_EMBEDDED,
    D_PROCESS,
    D_ADVISE,
    D_IS_FRAGMENT,
    D_CALL = 30,
};

#define KIND_NODE 1
#define KIND_PATTERN 2

#define MARK_MIXED ((SCODE)0x8004E001)
#define MARK_UNSUPPORTED ((SCODE)0x8004E002)

#define CALL_TIMEOUT 20000
#define WM_UIA_RUN (WM_APP + 0x5A1)

/* ------------------------------------------------------------------ running on a thread */

typedef struct RUN {
    UIA_THREAD_FN fn;
    void *context;
    HRESULT hr;
} RUN;

typedef struct THREAD_WINDOW {
    DWORD thread;
    HWND hwnd;
} THREAD_WINDOW;

static CRITICAL_SECTION g_lock;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
static THREAD_WINDOW g_windows[64];
static ATOM g_class;

static LRESULT CALLBACK thread_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_UIA_RUN) {
        RUN *run = (RUN *)lparam;
        run->hr = run->fn(run->context);
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static BOOL CALLBACK once_init(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    InitializeCriticalSection(&g_lock);
    return TRUE;
}

static void lock(void)
{
    InitOnceExecuteOnce(&g_once, once_init, nullptr, nullptr);
    EnterCriticalSection(&g_lock);
}

/* a window on the calling thread that runs requests for the thread, made when a wrapper is. */
static void ensure_thread_window(void)
{
    DWORD thread = GetCurrentThreadId();
    lock();
    for (int i = 0; i < (int)RTL_NUMBER_OF(g_windows); i++)
        if (g_windows[i].thread == thread && IsWindow(g_windows[i].hwnd)) {
            LeaveCriticalSection(&g_lock);
            return;
        }
    if (!g_class) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = thread_proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"UiaThreadWindow";
        g_class = RegisterClassW(&wc);
        if (!g_class && GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
            g_class = 1;
    }
    HWND hwnd = CreateWindowExW(0, L"UiaThreadWindow", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    if (hwnd) {
        int slot = -1;
        for (int i = 0; i < (int)RTL_NUMBER_OF(g_windows); i++)
            if (!g_windows[i].hwnd || !IsWindow(g_windows[i].hwnd)) {
                slot = i;
                break;
            }
        if (slot >= 0) {
            g_windows[slot].thread = thread;
            g_windows[slot].hwnd = hwnd;
        }
    }
    LeaveCriticalSection(&g_lock);
}

static HWND thread_window(DWORD thread)
{
    HWND found = nullptr;
    lock();
    for (int i = 0; i < (int)RTL_NUMBER_OF(g_windows); i++)
        if (g_windows[i].thread == thread && g_windows[i].hwnd && IsWindow(g_windows[i].hwnd)) {
            found = g_windows[i].hwnd;
            break;
        }
    LeaveCriticalSection(&g_lock);
    return found;
}

HRESULT UiaRunOnThread(DWORD thread_id, UIA_THREAD_FN fn, void *context)
{
    if (thread_id == GetCurrentThreadId())
        return fn(context);
    HWND hwnd = thread_window(thread_id);
    if (!hwnd)
        return UIA_E_ELEMENTNOTAVAILABLE;
    RUN run = {fn, context, E_FAIL};
    DWORD_PTR result = 0;
    if (!SendMessageTimeoutW(hwnd, WM_UIA_RUN, 0, (LPARAM)&run, SMTO_ABORTIFHUNG, CALL_TIMEOUT, &result))
        return UIA_E_TIMEOUT;
    return run.hr;
}

/* ------------------------------------------------------------------ the wrapper */

typedef struct SERVER {
    IDispatch iface;
    LONG refs;
    int kind;
    NODE *node;
    PATTERN *pattern;
    DWORD thread;
    volatile LONG gone;
    struct SERVER *next, *previous;
} SERVER;

static SERVER *g_servers;
static const IDispatchVtbl g_server_vtbl;

static void server_link(SERVER *s)
{
    lock();
    s->next = g_servers;
    if (g_servers)
        g_servers->previous = s;
    g_servers = s;
    LeaveCriticalSection(&g_lock);
}

static HRESULT STDMETHODCALLTYPE server_query(IDispatch *iface, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch) || IsEqualIID(riid, &IID_IMimeUiaServer)) {
        *object = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE server_add_ref(IDispatch *iface)
{
    return (ULONG)InterlockedIncrement(&((SERVER *)iface)->refs);
}

static ULONG STDMETHODCALLTYPE server_release(IDispatch *iface)
{
    SERVER *s = (SERVER *)iface;
    LONG refs = InterlockedDecrement(&s->refs);
    if (!refs) {
        lock();
        if (s->previous)
            s->previous->next = s->next;
        else
            g_servers = s->next;
        if (s->next)
            s->next->previous = s->previous;
        LeaveCriticalSection(&g_lock);
        NodeRelease(s->node);
        PatternRelease(s->pattern);
        UiaFree(s);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE server_type_count(IDispatch *iface, UINT *count)
{
    *count = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE server_type_info(IDispatch *iface, UINT index, LCID lcid, ITypeInfo **info)
{
    *info = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE server_names(IDispatch *iface, REFIID riid, LPOLESTR *names, UINT count, LCID lcid,
                                              DISPID *ids)
{
    for (UINT i = 0; i < count; i++)
        ids[i] = DISPID_UNKNOWN;
    return DISP_E_UNKNOWNNAME;
}

/* what leaves the process of a provider is a wrapper, never the provider's own object. */
static HRESULT wrap_node_here(NODE *node, IDispatch **out);
static HRESULT wrap_pattern_here(PATTERN *pattern, IDispatch **out);

static HRESULT export_value(VARIANT *v)
{
    if (v->vt == VT_UNKNOWN && v->punkVal) {
        IUnknown *u = v->punkVal;
        if (u == UiaMixedValue() || u == UiaNotSupportedValue()) {
            v->vt = VT_ERROR;
            v->scode = u == UiaMixedValue() ? MARK_MIXED : MARK_UNSUPPORTED;
            return S_OK;
        }
        NODE *node = nullptr;
        PATTERN *pattern = nullptr;
        IDispatch *wrapper = nullptr;
        HRESULT hr = E_NOINTERFACE;
        if (SUCCEEDED(u->lpVtbl->QueryInterface(u, &IID_IMimeUiaNode, (void **)&node))) {
            hr = wrap_node_here(node, &wrapper);
            NodeRelease(node);
        } else if (SUCCEEDED(u->lpVtbl->QueryInterface(u, &IID_IMimeUiaPattern, (void **)&pattern))) {
            hr = wrap_pattern_here(pattern, &wrapper);
            PatternRelease(pattern);
        } else {
            NODE *made = nullptr;
            if (SUCCEEDED(NodeFromUnknown(u, &made)) && made) {
                hr = wrap_node_here(made, &wrapper);
                NodeRelease(made);
            }
        }
        u->lpVtbl->Release(u);
        v->vt = VT_EMPTY;
        if (FAILED(hr))
            return hr;
        v->vt = VT_DISPATCH;
        v->pdispVal = wrapper;
        return S_OK;
    }
    if (v->vt == (VT_ARRAY | VT_UNKNOWN) && v->parray) {
        SAFEARRAY *source = v->parray;
        LONG lower, upper;
        SafeArrayGetLBound(source, 1, &lower);
        SafeArrayGetUBound(source, 1, &upper);
        int total = upper - lower + 1;
        SAFEARRAY *result = SafeArrayCreateVector(VT_DISPATCH, 0, total > 0 ? (ULONG)total : 0);
        if (!result)
            return E_OUTOFMEMORY;
        if (total > 0) {
            IUnknown **in;
            IDispatch **outv;
            SafeArrayAccessData(source, (void **)&in);
            SafeArrayAccessData(result, (void **)&outv);
            for (int i = 0; i < total; i++) {
                VARIANT item;
                item.vt = VT_UNKNOWN;
                item.punkVal = in[i];
                if (in[i])
                    in[i]->lpVtbl->AddRef(in[i]);
                if (SUCCEEDED(export_value(&item)) && item.vt == VT_DISPATCH)
                    outv[i] = item.pdispVal;
            }
            SafeArrayUnaccessData(result);
            SafeArrayUnaccessData(source);
        }
        SafeArrayDestroy(source);
        v->vt = VT_ARRAY | VT_DISPATCH;
        v->parray = result;
        return S_OK;
    }
    return S_OK;
}

HRESULT UiaExportValue(VARIANT *value)
{
    return export_value(value);
}

static IUnknown *server_unwrap_unknown(IUnknown *u)
{
    return u;
}

/* a value from a client: wrappers that are ours turn back into the objects they wrap. */
static HRESULT import_value(VARIANT *v)
{
    if (v->vt == VT_DISPATCH && v->pdispVal) {
        SERVER *s = nullptr;
        IDispatch *d = v->pdispVal;
        if (SUCCEEDED(d->lpVtbl->QueryInterface(d, &IID_IMimeUiaServer, (void **)&s))) {
            IUnknown *inner = (IUnknown *)(s->kind == KIND_NODE ? (void *)s->node : (void *)s->pattern);
            inner->lpVtbl->AddRef(inner);
            server_release((IDispatch *)s);
            d->lpVtbl->Release(d);
            v->vt = VT_UNKNOWN;
            v->punkVal = server_unwrap_unknown(inner);
        }
    }
    return S_OK;
}

typedef struct SERVER_CALL {
    SERVER *server;
    DISPID id;
    DISPPARAMS *params;
    VARIANT *result;
} SERVER_CALL;

static VARIANT *param(DISPPARAMS *p, ULONG i)
{
    return i < p->cArgs ? &p->rgvarg[p->cArgs - 1 - i] : nullptr;
}

static int param_int(DISPPARAMS *p, ULONG i)
{
    VARIANT *v = param(p, i);
    if (!v)
        return 0;
    if (v->vt == VT_I4)
        return v->lVal;
    VARIANT t;
    VariantInit(&t);
    int r = 0;
    if (SUCCEEDED(VariantChangeType(&t, v, 0, VT_I4)))
        r = t.lVal;
    VariantClear(&t);
    return r;
}

static double param_double(DISPPARAMS *p, ULONG i)
{
    VARIANT *v = param(p, i);
    if (!v)
        return 0;
    if (v->vt == VT_R8)
        return v->dblVal;
    VARIANT t;
    VariantInit(&t);
    double r = 0;
    if (SUCCEEDED(VariantChangeType(&t, v, 0, VT_R8)))
        r = t.dblVal;
    VariantClear(&t);
    return r;
}

static void result_node(VARIANT *result, NODE *node, HRESULT *hr)
{
    if (!node || !result)
        return;
    IDispatch *wrapper = nullptr;
    *hr = wrap_node_here(node, &wrapper);
    NodeRelease(node);
    if (SUCCEEDED(*hr)) {
        result->vt = VT_DISPATCH;
        result->pdispVal = wrapper;
    }
}

static HRESULT server_node_call(SERVER_CALL *c)
{
    NODE *n = c->server->node;
    DISPPARAMS *p = c->params;
    VARIANT *result = c->result;
    HRESULT hr = S_OK;
    NODE *other = nullptr;
    switch (c->id) {
    case D_PROP: {
        VARIANT v;
        hr = n->ops->get_prop(n, param_int(p, 0), &v);
        if (SUCCEEDED(hr)) {
            hr = export_value(&v);
            if (result)
                *result = v;
            else
                VariantClear(&v);
        }
        return hr;
    }
    case D_PATTERN: {
        PATTERN *pattern = nullptr;
        hr = n->ops->get_pattern(n, param_int(p, 0), &pattern);
        if (SUCCEEDED(hr) && pattern) {
            IDispatch *wrapper = nullptr;
            hr = wrap_pattern_here(pattern, &wrapper);
            PatternRelease(pattern);
            if (SUCCEEDED(hr) && result) {
                result->vt = VT_DISPATCH;
                result->pdispVal = wrapper;
            } else if (wrapper) {
                wrapper->lpVtbl->Release(wrapper);
            }
        }
        return hr;
    }
    case D_NAVIGATE:
        hr = NodeNavigate(n, (NavigateDirection)param_int(p, 0), &other);
        if (SUCCEEDED(hr))
            result_node(result, other, &hr);
        return hr;
    case D_RUNTIME_ID: {
        int *ids = nullptr, count = 0;
        hr = n->ops->runtime_id(n, &ids, &count);
        if (SUCCEEDED(hr) && count && result) {
            result->vt = VT_ARRAY | VT_I4;
            hr = UiaIntArrayToSafeArray(ids, count, &result->parray);
        }
        UiaFree(ids);
        return hr;
    }
    case D_OPTIONS: {
        int options = 0;
        hr = n->ops->options(n, &options);
        if (SUCCEEDED(hr) && result) {
            result->vt = VT_I4;
            result->lVal = options;
        }
        return hr;
    }
    case D_HOST: {
        HWND hwnd = nullptr;
        hr = n->ops->host_hwnd(n, &hwnd);
        if (SUCCEEDED(hr) && result) {
            result->vt = VT_I4;
            result->lVal = (LONG)(LONG_PTR)hwnd;
        }
        return hr;
    }
    case D_FOCUS:
        return n->ops->set_focus(n);
    case D_ROOT_POINT:
        hr = n->ops->root_from_point(n, param_double(p, 0), param_double(p, 1), &other);
        if (SUCCEEDED(hr))
            result_node(result, other, &hr);
        return hr;
    case D_ROOT_FOCUS:
        hr = n->ops->root_focus(n, &other);
        if (SUCCEEDED(hr))
            result_node(result, other, &hr);
        return hr;
    case D_FRAGMENT_ROOT:
        hr = n->ops->fragment_root(n, &other);
        if (SUCCEEDED(hr))
            result_node(result, other, &hr);
        return hr;
    case D_EMBEDDED: {
        NODE **list = nullptr;
        int count = 0;
        hr = n->ops->embedded_roots(n, &list, &count);
        if (SUCCEEDED(hr) && count && result) {
            SAFEARRAY *array = SafeArrayCreateVector(VT_DISPATCH, 0, (ULONG)count);
            IDispatch **items;
            if (array && SUCCEEDED(SafeArrayAccessData(array, (void **)&items))) {
                for (int i = 0; i < count; i++)
                    wrap_node_here(list[i], &items[i]);
                SafeArrayUnaccessData(array);
                result->vt = VT_ARRAY | VT_DISPATCH;
                result->parray = array;
            }
        }
        for (int i = 0; i < count; i++)
            NodeRelease(list[i]);
        UiaFree(list);
        return hr;
    }
    case D_PROCESS: {
        DWORD pid = 0;
        hr = n->ops->process_id(n, &pid);
        if (SUCCEEDED(hr) && result) {
            result->vt = VT_I4;
            result->lVal = (LONG)pid;
        }
        return hr;
    }
    case D_ADVISE: {
        VARIANT *props = param(p, 2);
        return n->ops->advise(n, param_int(p, 0), param_int(p, 1),
                              props && props->vt == (VT_ARRAY | VT_I4) ? props->parray : nullptr);
    }
    case D_IS_FRAGMENT:
        if (result) {
            result->vt = VT_BOOL;
            result->boolVal = n->ops->is_fragment(n) ? VARIANT_TRUE : VARIANT_FALSE;
        }
        return S_OK;
    }
    return DISP_E_MEMBERNOTFOUND;
}

static HRESULT server_pattern_call(SERVER_CALL *c)
{
    DISPPARAMS *p = c->params;
    if (c->id != D_CALL || p->cArgs < 1)
        return DISP_E_MEMBERNOTFOUND;
    int method = param_int(p, 0);
    ULONG count = p->cArgs - 1;
    VARIANT *args = count ? UiaAlloc(count * sizeof(VARIANT)) : nullptr;
    if (count && !args)
        return E_OUTOFMEMORY;
    for (ULONG i = 0; i < count; i++) {
        VariantInit(&args[i]);
        VariantCopy(&args[i], param(p, i + 1));
        import_value(&args[i]);
    }
    VARIANT ret;
    VariantInit(&ret);
    HRESULT hr = PatternCall(c->server->pattern, method, args, count, &ret);
    for (ULONG i = 0; i < count; i++)
        VariantClear(&args[i]);
    UiaFree(args);
    if (SUCCEEDED(hr)) {
        hr = export_value(&ret);
        if (c->result)
            *c->result = ret;
        else
            VariantClear(&ret);
    } else {
        VariantClear(&ret);
    }
    return hr;
}

static HRESULT server_do(void *context)
{
    SERVER_CALL *c = context;
    SERVER *s = c->server;
    if (s->gone || (s->node && s->node->disconnected))
        return UIA_E_ELEMENTNOTAVAILABLE;
    if (c->id == D_KIND) {
        if (c->result) {
            c->result->vt = VT_I4;
            c->result->lVal = s->kind;
        }
        return S_OK;
    }
    return s->kind == KIND_NODE ? server_node_call(c) : server_pattern_call(c);
}

static HRESULT STDMETHODCALLTYPE server_invoke(IDispatch *iface, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                               DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep, UINT *argerr)
{
    SERVER *s = (SERVER *)iface;
    DISPPARAMS none = {};
    SERVER_CALL call = {s, id, params ? params : &none, result};
    if (result)
        VariantInit(result);
    if (id == D_KIND)
        return server_do(&call);
    return UiaRunOnThread(s->thread, server_do, &call);
}

static const IDispatchVtbl g_server_vtbl = {
    server_query, server_add_ref, server_release, server_type_count, server_type_info, server_names, server_invoke,
};

static HRESULT wrap(int kind, NODE *node, PATTERN *pattern, IDispatch **out)
{
    UiaEnsureCom();
    SERVER *s = UiaAlloc(sizeof(*s));
    if (!s)
        return E_OUTOFMEMORY;
    s->iface.lpVtbl = &g_server_vtbl;
    s->refs = 1;
    s->kind = kind;
    s->node = node;
    s->pattern = pattern;
    s->thread = GetCurrentThreadId();
    if (node)
        NodeAddRef(node);
    if (pattern)
        PatternAddRef(pattern);
    ensure_thread_window();
    server_link(s);
    *out = &s->iface;
    return S_OK;
}

static HRESULT wrap_node_here(NODE *node, IDispatch **out)
{
    return wrap(KIND_NODE, node, nullptr, out);
}

static HRESULT wrap_pattern_here(PATTERN *pattern, IDispatch **out)
{
    return wrap(KIND_PATTERN, nullptr, pattern, out);
}

HRESULT ServerWrapNode(NODE *node, IDispatch **out)
{
    return wrap_node_here(node, out);
}

void ServerDisconnectProvider(IRawElementProviderSimple *provider, const int *rid, int rid_len)
{
    lock();
    for (SERVER *s = g_servers; s; s = s->next) {
        if (s->kind != KIND_NODE)
            continue;
        BOOL match = s->node->ops->provider(s->node) == provider;
        if (!match && rid_len) {
            int *ids = nullptr, n = 0;
            if (SUCCEEDED(NodeRuntimeId(s->node, &ids, &n)))
                match = n && !UiaCompareRuntimeIds(ids, n, rid, rid_len);
            UiaFree(ids);
        }
        if (match) {
            s->gone = 1;
            s->node->disconnected = 1;
        }
    }
    LeaveCriticalSection(&g_lock);
}

void ServerDisconnectAll(void)
{
    lock();
    for (SERVER *s = g_servers; s; s = s->next) {
        s->gone = 1;
        if (s->node)
            s->node->disconnected = 1;
    }
    LeaveCriticalSection(&g_lock);
}

/* ------------------------------------------------------------------ the client end */

static HRESULT remote_error(HRESULT hr)
{
    return UIA_RPC_FAILED(hr) ? UIA_E_ELEMENTNOTAVAILABLE : hr;
}

/* calls a method of a wrapper with arguments given in order. */
static HRESULT remote_invoke(IDispatch *d, DISPID id, VARIANT *args, ULONG count, VARIANT *result)
{
    VARIANT stack[8];
    DISPPARAMS params = {};
    params.cArgs = count;
    if (count > RTL_NUMBER_OF(stack))
        return E_INVALIDARG;
    for (ULONG i = 0; i < count; i++)
        stack[count - 1 - i] = args[i];
    params.rgvarg = count ? stack : nullptr;
    VARIANT local;
    if (!result) {
        VariantInit(&local);
        result = &local;
    }
    VariantInit(result);
    EXCEPINFO ex = {};
    UINT err = 0;
    HRESULT hr = d->lpVtbl->Invoke(d, id, &IID_NULL, 0, DISPATCH_METHOD, &params, result, &ex, &err);
    if (hr == DISP_E_EXCEPTION)
        hr = ex.scode ? ex.scode : E_FAIL;
    if (result == &local)
        VariantClear(&local);
    return remote_error(hr);
}

static HRESULT remote_int_call(IDispatch *d, DISPID id, VARIANT *args, ULONG count, int *out)
{
    VARIANT r;
    HRESULT hr = remote_invoke(d, id, args, count, &r);
    if (SUCCEEDED(hr)) {
        *out = r.vt == VT_I4 ? r.lVal : 0;
        VariantClear(&r);
    }
    return hr;
}

typedef struct REMNODE {
    NODE base;
    IDispatch *dispatch;
    int fragment; /* -1 until asked. */
} REMNODE;

#define REM(n) ((REMNODE *)(n))

static const NODE_OPS g_remote_ops;
static const PATTERN_OPS g_remote_pattern_ops;

typedef struct REMPAT {
    PATTERN base;
    IDispatch *dispatch;
    NODE *owner;
} REMPAT;

static HRESULT kind_of(IDispatch *d, int *kind)
{
    SERVER *s = nullptr;
    if (SUCCEEDED(d->lpVtbl->QueryInterface(d, &IID_IMimeUiaServer, (void **)&s))) {
        *kind = s->kind;
        server_release((IDispatch *)s);
        return S_OK;
    }
    return remote_int_call(d, D_KIND, nullptr, 0, kind);
}

static HRESULT remote_node_make(IDispatch *d, NODE **out)
{
    REMNODE *r = NodeAlloc(sizeof(*r), &g_remote_ops);
    if (!r)
        return E_OUTOFMEMORY;
    d->lpVtbl->AddRef(d);
    r->dispatch = d;
    r->fragment = -1;
    *out = &r->base;
    return S_OK;
}

HRESULT RemoteNodeFromDispatch(IDispatch *dispatch, NODE **out)
{
    *out = nullptr;
    int kind = 0;
    HRESULT hr = kind_of(dispatch, &kind);
    if (FAILED(hr))
        return hr;
    if (kind != KIND_NODE)
        return E_NOINTERFACE;
    return remote_node_make(dispatch, out);
}

static HRESULT remote_pattern_make(IDispatch *d, int id, NODE *owner, PATTERN **out)
{
    REMPAT *r = PatternAlloc(sizeof(*r), &g_remote_pattern_ops, id);
    if (!r)
        return E_OUTOFMEMORY;
    d->lpVtbl->AddRef(d);
    r->dispatch = d;
    r->owner = owner;
    if (owner)
        NodeAddRef(owner);
    *out = &r->base;
    return S_OK;
}

HRESULT RemotePatternFromDispatch(IDispatch *dispatch, int id, PATTERN **out)
{
    *out = nullptr;
    int kind = 0;
    HRESULT hr = kind_of(dispatch, &kind);
    if (FAILED(hr))
        return hr;
    if (kind != KIND_PATTERN)
        return E_NOINTERFACE;
    return remote_pattern_make(dispatch, id, nullptr, out);
}

/* values from a wrapper, with its objects turned into nodes and patterns. */
static HRESULT remote_import(VARIANT *v, NODE *owner)
{
    if (v->vt == VT_ERROR && (v->scode == MARK_MIXED || v->scode == MARK_UNSUPPORTED)) {
        IUnknown *u = v->scode == MARK_MIXED ? UiaMixedValue() : UiaNotSupportedValue();
        v->vt = VT_UNKNOWN;
        v->punkVal = u;
        return S_OK;
    }
    if (v->vt == VT_DISPATCH && v->pdispVal) {
        IDispatch *d = v->pdispVal;
        int kind = 0;
        HRESULT hr = kind_of(d, &kind);
        IUnknown *object = nullptr;
        if (SUCCEEDED(hr)) {
            if (kind == KIND_NODE) {
                NODE *node = nullptr;
                hr = remote_node_make(d, &node);
                object = (IUnknown *)node;
            } else {
                PATTERN *pattern = nullptr;
                hr = remote_pattern_make(d, PATTERN_ID_TEXTRANGE, owner, &pattern);
                object = (IUnknown *)pattern;
            }
        }
        d->lpVtbl->Release(d);
        v->vt = VT_UNKNOWN;
        v->punkVal = object;
        return hr;
    }
    if (v->vt == (VT_ARRAY | VT_DISPATCH) && v->parray) {
        SAFEARRAY *source = v->parray;
        LONG lower, upper;
        SafeArrayGetLBound(source, 1, &lower);
        SafeArrayGetUBound(source, 1, &upper);
        int total = upper - lower + 1;
        SAFEARRAY *result = SafeArrayCreateVector(VT_UNKNOWN, 0, total > 0 ? (ULONG)total : 0);
        if (!result)
            return E_OUTOFMEMORY;
        if (total > 0) {
            IDispatch **in;
            IUnknown **outv;
            SafeArrayAccessData(source, (void **)&in);
            SafeArrayAccessData(result, (void **)&outv);
            for (int i = 0; i < total; i++) {
                VARIANT item;
                item.vt = VT_DISPATCH;
                item.pdispVal = in[i];
                if (in[i])
                    in[i]->lpVtbl->AddRef(in[i]);
                if (SUCCEEDED(remote_import(&item, owner)) && item.vt == VT_UNKNOWN)
                    outv[i] = item.punkVal;
            }
            SafeArrayUnaccessData(result);
            SafeArrayUnaccessData(source);
        }
        SafeArrayDestroy(source);
        v->vt = VT_ARRAY | VT_UNKNOWN;
        v->parray = result;
    }
    return S_OK;
}

static void rem_destroy(NODE *n)
{
    REM(n)->dispatch->lpVtbl->Release(REM(n)->dispatch);
}

static HRESULT rem_get_prop(NODE *n, PROPERTYID id, VARIANT *out)
{
    VARIANT arg;
    arg.vt = VT_I4;
    arg.lVal = id;
    HRESULT hr = remote_invoke(REM(n)->dispatch, D_PROP, &arg, 1, out);
    if (SUCCEEDED(hr))
        hr = remote_import(out, n);
    if (FAILED(hr))
        VariantClear(out);
    return hr;
}

static HRESULT rem_get_pattern(NODE *n, PATTERNID id, PATTERN **out)
{
    *out = nullptr;
    VARIANT arg, r;
    arg.vt = VT_I4;
    arg.lVal = id;
    HRESULT hr = remote_invoke(REM(n)->dispatch, D_PATTERN, &arg, 1, &r);
    if (FAILED(hr))
        return hr;
    if (r.vt == VT_DISPATCH && r.pdispVal) {
        hr = remote_pattern_make(r.pdispVal, id, n, out);
    }
    VariantClear(&r);
    return hr;
}

static HRESULT rem_node_result(VARIANT *r, NODE **out)
{
    *out = nullptr;
    HRESULT hr = S_OK;
    if (r->vt == VT_DISPATCH && r->pdispVal)
        hr = remote_node_make(r->pdispVal, out);
    VariantClear(r);
    return hr;
}

static HRESULT rem_navigate(NODE *n, NavigateDirection dir, NODE **out)
{
    *out = nullptr;
    VARIANT arg, r;
    arg.vt = VT_I4;
    arg.lVal = dir;
    HRESULT hr = remote_invoke(REM(n)->dispatch, D_NAVIGATE, &arg, 1, &r);
    return FAILED(hr) ? hr : rem_node_result(&r, out);
}

static HRESULT rem_runtime_id(NODE *n, int **ids, int *count)
{
    *ids = nullptr;
    *count = 0;
    VARIANT r;
    HRESULT hr = remote_invoke(REM(n)->dispatch, D_RUNTIME_ID, nullptr, 0, &r);
    if (SUCCEEDED(hr) && r.vt == (VT_ARRAY | VT_I4))
        UiaSafeArrayToIntArray(r.parray, ids, count);
    VariantClear(&r);
    return hr;
}

static HRESULT rem_options(NODE *n, int *options)
{
    *options = 0;
    return remote_int_call(REM(n)->dispatch, D_OPTIONS, nullptr, 0, options);
}

static HRESULT rem_host_hwnd(NODE *n, HWND *hwnd)
{
    int value = 0;
    HRESULT hr = remote_int_call(REM(n)->dispatch, D_HOST, nullptr, 0, &value);
    *hwnd = (HWND)(LONG_PTR)value;
    return hr;
}

static HRESULT rem_set_focus(NODE *n)
{
    return remote_invoke(REM(n)->dispatch, D_FOCUS, nullptr, 0, nullptr);
}

static HRESULT rem_root_from_point(NODE *n, double x, double y, NODE **out)
{
    *out = nullptr;
    VARIANT args[2], r;
    args[0].vt = VT_R8;
    args[0].dblVal = x;
    args[1].vt = VT_R8;
    args[1].dblVal = y;
    HRESULT hr = remote_invoke(REM(n)->dispatch, D_ROOT_POINT, args, 2, &r);
    return FAILED(hr) ? hr : rem_node_result(&r, out);
}

static HRESULT rem_root_focus(NODE *n, NODE **out)
{
    *out = nullptr;
    VARIANT r;
    HRESULT hr = remote_invoke(REM(n)->dispatch, D_ROOT_FOCUS, nullptr, 0, &r);
    return FAILED(hr) ? hr : rem_node_result(&r, out);
}

static HRESULT rem_fragment_root(NODE *n, NODE **out)
{
    *out = nullptr;
    VARIANT r;
    HRESULT hr = remote_invoke(REM(n)->dispatch, D_FRAGMENT_ROOT, nullptr, 0, &r);
    return FAILED(hr) ? hr : rem_node_result(&r, out);
}

static HRESULT rem_embedded_roots(NODE *n, NODE ***list, int *count)
{
    *list = nullptr;
    *count = 0;
    VARIANT r;
    HRESULT hr = remote_invoke(REM(n)->dispatch, D_EMBEDDED, nullptr, 0, &r);
    if (SUCCEEDED(hr) && r.vt == (VT_ARRAY | VT_DISPATCH) && r.parray) {
        LONG lower, upper;
        IDispatch **items;
        SafeArrayGetLBound(r.parray, 1, &lower);
        SafeArrayGetUBound(r.parray, 1, &upper);
        int total = upper - lower + 1;
        NODE **nodes = total > 0 ? UiaAlloc((SIZE_T)total * sizeof(*nodes)) : nullptr;
        if (nodes && SUCCEEDED(SafeArrayAccessData(r.parray, (void **)&items))) {
            int made = 0;
            for (int i = 0; i < total; i++)
                if (items[i] && SUCCEEDED(remote_node_make(items[i], &nodes[made])))
                    made++;
            SafeArrayUnaccessData(r.parray);
            *list = nodes;
            *count = made;
        } else {
            UiaFree(nodes);
        }
    }
    VariantClear(&r);
    return hr;
}

static HRESULT rem_process_id(NODE *n, DWORD *pid)
{
    int value = 0;
    HRESULT hr = remote_int_call(REM(n)->dispatch, D_PROCESS, nullptr, 0, &value);
    *pid = (DWORD)value;
    return hr;
}

static HRESULT rem_advise(NODE *n, BOOL added, int event, SAFEARRAY *props)
{
    VARIANT args[3];
    args[0].vt = VT_I4;
    args[0].lVal = added;
    args[1].vt = VT_I4;
    args[1].lVal = event;
    VariantInit(&args[2]);
    if (props) {
        args[2].vt = VT_ARRAY | VT_I4;
        args[2].parray = props;
    }
    return remote_invoke(REM(n)->dispatch, D_ADVISE, args, 3, nullptr);
}

static IRawElementProviderSimple *rem_provider(NODE *n)
{
    return nullptr;
}

static BOOL rem_is_fragment(NODE *n)
{
    if (REM(n)->fragment < 0) {
        VARIANT r;
        REM(n)->fragment = 0;
        if (SUCCEEDED(remote_invoke(REM(n)->dispatch, D_IS_FRAGMENT, nullptr, 0, &r)) && r.vt == VT_BOOL)
            REM(n)->fragment = r.boolVal ? 1 : 0;
    }
    return REM(n)->fragment > 0;
}

static const NODE_OPS g_remote_ops = {
    rem_destroy,     rem_get_prop,    rem_get_pattern,  rem_navigate,       rem_runtime_id,
    rem_options,     rem_host_hwnd,   rem_set_focus,    rem_root_from_point, rem_root_focus,
    rem_fragment_root, rem_embedded_roots, rem_process_id, rem_advise,      rem_provider,
    rem_is_fragment,
};

static void rempat_destroy(PATTERN *p)
{
    ((REMPAT *)p)->dispatch->lpVtbl->Release(((REMPAT *)p)->dispatch);
    NodeRelease(((REMPAT *)p)->owner);
}

/* an argument for a call of the other end: only objects of that end can go. */
static HRESULT export_argument(const VARIANT *in, VARIANT *out)
{
    VariantInit(out);
    if ((in->vt == VT_UNKNOWN || in->vt == VT_DISPATCH) && in->punkVal) {
        IUnknown *u = in->punkVal;
        NODE *node = nullptr;
        PATTERN *pattern = nullptr;
        IDispatch *d = nullptr;
        if (SUCCEEDED(u->lpVtbl->QueryInterface(u, &IID_IMimeUiaNode, (void **)&node))) {
            if (node->ops == &g_remote_ops)
                d = REM(node)->dispatch;
            NodeRelease(node);
        } else if (SUCCEEDED(u->lpVtbl->QueryInterface(u, &IID_IMimeUiaPattern, (void **)&pattern))) {
            if (pattern->ops == &g_remote_pattern_ops)
                d = ((REMPAT *)pattern)->dispatch;
            PatternRelease(pattern);
        }
        if (!d)
            return E_INVALIDARG;
        d->lpVtbl->AddRef(d);
        out->vt = VT_DISPATCH;
        out->pdispVal = d;
        return S_OK;
    }
    return VariantCopy(out, (VARIANT *)in);
}

static HRESULT rempat_call(PATTERN *p, int method, VARIANT *args, ULONG count, VARIANT *ret)
{
    REMPAT *r = (REMPAT *)p;
    VARIANT sent[8];
    if (count + 1 > RTL_NUMBER_OF(sent))
        return E_INVALIDARG;
    sent[0].vt = VT_I4;
    sent[0].lVal = method;
    HRESULT hr = S_OK;
    ULONG made = 0;
    for (ULONG i = 0; i < count && SUCCEEDED(hr); i++, made++)
        hr = export_argument(&args[i], &sent[1 + i]);
    if (SUCCEEDED(hr)) {
        hr = remote_invoke(r->dispatch, D_CALL, sent, count + 1, ret);
        if (SUCCEEDED(hr) && ret)
            hr = remote_import(ret, r->owner);
        if (FAILED(hr) && ret)
            VariantClear(ret);
    }
    for (ULONG i = 0; i < made; i++)
        VariantClear(&sent[1 + i]);
    return hr;
}

static NODE *rempat_owner(PATTERN *p)
{
    return ((REMPAT *)p)->owner;
}

static const PATTERN_OPS g_remote_pattern_ops = {rempat_destroy, rempat_call, rempat_owner};
