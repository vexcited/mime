/* the small com objects of the engine: contexts, call results, enumerators, the locator and the class factories. */
#include "wbemcore.h"

/* ------------------------------------------------------------------ the security of an interface */

static HRESULT STDMETHODCALLTYPE sc_QueryInterface(IClientSecurity *This, REFIID riid, void **out)
{
    WSecurity *s = (WSecurity *)This;
    if (IsEqualIID(riid, &IID_IClientSecurity)) {
        *out = This;
        s->owner->lpVtbl->AddRef(s->owner);
        return S_OK;
    }
    return s->owner->lpVtbl->QueryInterface(s->owner, riid, out);
}

static ULONG STDMETHODCALLTYPE sc_AddRef(IClientSecurity *This)
{
    WSecurity *s = (WSecurity *)This;
    return s->owner->lpVtbl->AddRef(s->owner);
}

static ULONG STDMETHODCALLTYPE sc_Release(IClientSecurity *This)
{
    WSecurity *s = (WSecurity *)This;
    return s->owner->lpVtbl->Release(s->owner);
}

static HRESULT STDMETHODCALLTYPE sc_QueryBlanket(IClientSecurity *This, IUnknown *proxy, DWORD *authn_service, DWORD *authz_service,
                                                 OLECHAR **server_principal, DWORD *authn_level, DWORD *imp_level, void **auth_info,
                                                 DWORD *capabilities)
{
    WSecurity *s = (WSecurity *)This;
    if (authn_service)
        *authn_service = s->authn_service;
    if (authz_service)
        *authz_service = s->authz_service;
    if (server_principal)
        *server_principal = nullptr;
    if (authn_level)
        *authn_level = s->authn_level;
    if (imp_level)
        *imp_level = s->imp_level;
    if (auth_info)
        *auth_info = nullptr;
    if (capabilities)
        *capabilities = s->capabilities;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sc_SetBlanket(IClientSecurity *This, IUnknown *proxy, DWORD authn_service, DWORD authz_service,
                                               OLECHAR *server_principal, DWORD authn_level, DWORD imp_level, void *auth_info,
                                               DWORD capabilities)
{
    WSecurity *s = (WSecurity *)This;
    s->authn_service = authn_service;
    s->authz_service = authz_service;
    s->authn_level = authn_level;
    s->imp_level = imp_level;
    s->capabilities = capabilities;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sc_CopyProxy(IClientSecurity *This, IUnknown *proxy, IUnknown **copy)
{
    WSecurity *s = (WSecurity *)This;
    s->owner->lpVtbl->AddRef(s->owner);
    *copy = s->owner;
    return S_OK;
}

static const IClientSecurityVtbl SECURITY_VTBL = {
    sc_QueryInterface, sc_AddRef, sc_Release, sc_QueryBlanket, sc_SetBlanket, sc_CopyProxy,
};

void wc_security_init(WSecurity *security, IUnknown *owner)
{
    security->lpVtbl = &SECURITY_VTBL;
    security->owner = owner;
    security->authn_service = 10;
    security->authz_service = 0;
    security->authn_level = 6;
    security->imp_level = 3;
    security->capabilities = 0;
}

HRESULT wc_security_query(WSecurity *security, REFIID riid, void **out)
{
    if (!IsEqualIID(riid, &IID_IClientSecurity))
        return E_NOINTERFACE;
    *out = security;
    security->owner->lpVtbl->AddRef(security->owner);
    return S_OK;
}

/* ------------------------------------------------------------------ context */

typedef struct CtxValue {
    WCHAR *name;
    VARIANT value;
} CtxValue;

typedef struct ComContext {
    const IWbemContextVtbl *lpVtbl;
    LONG refs;
    CRITICAL_SECTION lock;
    CtxValue *values;
    UINT count, capacity;
    UINT enum_index;
    BOOL enumerating;
} ComContext;

#define CTX(This) ((ComContext *)(This))

static HRESULT STDMETHODCALLTYPE cx_QueryInterface(IWbemContext *This, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IWbemContext)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    return wb_no_interface("cx", riid, out);
}

static ULONG STDMETHODCALLTYPE cx_AddRef(IWbemContext *This)
{
    return (ULONG)InterlockedIncrement(&CTX(This)->refs);
}

static ULONG STDMETHODCALLTYPE cx_Release(IWbemContext *This)
{
    ComContext *c = CTX(This);
    LONG n = InterlockedDecrement(&c->refs);
    if (!n) {
        for (UINT i = 0; i < c->count; i++) {
            wb_free(c->values[i].name);
            VariantClear(&c->values[i].value);
        }
        wb_free(c->values);
        DeleteCriticalSection(&c->lock);
        wb_free(c);
        InterlockedDecrement(&g_wbem_locks);
    }
    return (ULONG)n;
}

static CtxValue *ctx_find(ComContext *c, const WCHAR *name)
{
    for (UINT i = 0; i < c->count; i++)
        if (wb_eq(c->values[i].name, name))
            return &c->values[i];
    return nullptr;
}

static HRESULT STDMETHODCALLTYPE cx_Clone(IWbemContext *This, IWbemContext **copy)
{
    ComContext *c = CTX(This);
    IWbemContext *fresh;
    HRESULT hr = wc_context_new(&fresh);
    if (FAILED(hr))
        return hr;
    EnterCriticalSection(&c->lock);
    for (UINT i = 0; i < c->count; i++)
        fresh->lpVtbl->SetValue(fresh, c->values[i].name, 0, &c->values[i].value);
    LeaveCriticalSection(&c->lock);
    *copy = fresh;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cx_GetNames(IWbemContext *This, LONG flags, SAFEARRAY **names)
{
    ComContext *c = CTX(This);
    if (flags || !names)
        return WBEM_E_INVALID_PARAMETER;
    EnterCriticalSection(&c->lock);
    SAFEARRAY *array = SafeArrayCreateVector(VT_BSTR, 0, c->count);
    for (UINT i = 0; array && i < c->count; i++) {
        LONG index = (LONG)i;
        BSTR s = SysAllocString(c->values[i].name);
        SafeArrayPutElement(array, &index, s);
        SysFreeString(s);
    }
    LeaveCriticalSection(&c->lock);
    *names = array;
    return array ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE cx_BeginEnumeration(IWbemContext *This, LONG flags)
{
    ComContext *c = CTX(This);
    if (flags)
        return WBEM_E_INVALID_PARAMETER;
    c->enum_index = 0;
    c->enumerating = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cx_Next(IWbemContext *This, LONG flags, BSTR *name, VARIANT *value)
{
    ComContext *c = CTX(This);
    if (flags)
        return WBEM_E_INVALID_PARAMETER;
    if (!c->enumerating)
        return WBEM_E_UNEXPECTED;
    EnterCriticalSection(&c->lock);
    HRESULT hr = WBEM_S_NO_MORE_DATA;
    if (c->enum_index < c->count) {
        CtxValue *v = &c->values[c->enum_index++];
        if (name)
            *name = SysAllocString(v->name);
        if (value) {
            VariantInit(value);
            wv_copy(value, &v->value);
        }
        hr = S_OK;
    }
    LeaveCriticalSection(&c->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE cx_EndEnumeration(IWbemContext *This)
{
    CTX(This)->enumerating = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cx_SetValue(IWbemContext *This, LPCWSTR name, LONG flags, VARIANT *value)
{
    ComContext *c = CTX(This);
    if (!name || !value || flags)
        return WBEM_E_INVALID_PARAMETER;
    EnterCriticalSection(&c->lock);
    CtxValue *v = ctx_find(c, name);
    if (!v) {
        WB_GROW(c->values, c->count, c->capacity);
        v = &c->values[c->count++];
        v->name = wb_wcsdup(name);
        VariantInit(&v->value);
    }
    VariantClear(&v->value);
    VariantCopy(&v->value, value);
    LeaveCriticalSection(&c->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cx_GetValue(IWbemContext *This, LPCWSTR name, LONG flags, VARIANT *value)
{
    ComContext *c = CTX(This);
    if (!name || !value || flags)
        return WBEM_E_INVALID_PARAMETER;
    EnterCriticalSection(&c->lock);
    CtxValue *v = ctx_find(c, name);
    HRESULT hr = WBEM_E_NOT_FOUND;
    if (v) {
        VariantInit(value);
        VariantCopy(value, &v->value);
        hr = S_OK;
    }
    LeaveCriticalSection(&c->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE cx_DeleteValue(IWbemContext *This, LPCWSTR name, LONG flags)
{
    ComContext *c = CTX(This);
    if (!name || flags)
        return WBEM_E_INVALID_PARAMETER;
    EnterCriticalSection(&c->lock);
    HRESULT hr = WBEM_E_NOT_FOUND;
    for (UINT i = 0; i < c->count; i++) {
        if (wb_eq(c->values[i].name, name)) {
            wb_free(c->values[i].name);
            VariantClear(&c->values[i].value);
            memmove(&c->values[i], &c->values[i + 1], (c->count - i - 1) * sizeof(CtxValue));
            c->count--;
            hr = S_OK;
            break;
        }
    }
    LeaveCriticalSection(&c->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE cx_DeleteAll(IWbemContext *This)
{
    ComContext *c = CTX(This);
    EnterCriticalSection(&c->lock);
    for (UINT i = 0; i < c->count; i++) {
        wb_free(c->values[i].name);
        VariantClear(&c->values[i].value);
    }
    c->count = 0;
    LeaveCriticalSection(&c->lock);
    return S_OK;
}

static const IWbemContextVtbl CONTEXT_VTBL = {
    cx_QueryInterface, cx_AddRef,    cx_Release,  cx_Clone,    cx_GetNames,   cx_BeginEnumeration,
    cx_Next,           cx_EndEnumeration, cx_SetValue, cx_GetValue, cx_DeleteValue, cx_DeleteAll,
};

HRESULT wc_context_new(IWbemContext **out)
{
    ComContext *c = wb_alloc(sizeof(ComContext));
    if (!c)
        return E_OUTOFMEMORY;
    c->lpVtbl = &CONTEXT_VTBL;
    c->refs = 1;
    InitializeCriticalSection(&c->lock);
    InterlockedIncrement(&g_wbem_locks);
    *out = (IWbemContext *)c;
    return S_OK;
}

/* ------------------------------------------------------------------ call result */

typedef struct ComResult {
    const IWbemCallResultVtbl *lpVtbl;
    LONG refs;
    WSecurity security;
    HRESULT status;
    IWbemClassObject *object;
    BSTR string;
    IWbemServices *services;
} ComResult;

#define RES(This) ((ComResult *)(This))

static HRESULT STDMETHODCALLTYPE cr_QueryInterface(IWbemCallResult *This, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IClientSecurity))
        return wc_security_query(&RES(This)->security, riid, out);
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IWbemCallResult)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    return wb_no_interface("cr", riid, out);
}

static ULONG STDMETHODCALLTYPE cr_AddRef(IWbemCallResult *This)
{
    return (ULONG)InterlockedIncrement(&RES(This)->refs);
}

static ULONG STDMETHODCALLTYPE cr_Release(IWbemCallResult *This)
{
    ComResult *r = RES(This);
    LONG n = InterlockedDecrement(&r->refs);
    if (!n) {
        if (r->object)
            r->object->lpVtbl->Release(r->object);
        if (r->services)
            r->services->lpVtbl->Release(r->services);
        SysFreeString(r->string);
        wb_free(r);
        InterlockedDecrement(&g_wbem_locks);
    }
    return (ULONG)n;
}

static HRESULT STDMETHODCALLTYPE cr_GetResultObject(IWbemCallResult *This, LONG timeout, IWbemClassObject **result)
{
    ComResult *r = RES(This);
    if (!result)
        return WBEM_E_INVALID_PARAMETER;
    if (FAILED(r->status))
        return r->status;
    if (!r->object)
        return WBEM_E_NOT_FOUND;
    *result = r->object;
    r->object->lpVtbl->AddRef(r->object);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cr_GetResultString(IWbemCallResult *This, LONG timeout, BSTR *result)
{
    ComResult *r = RES(This);
    if (!result)
        return WBEM_E_INVALID_PARAMETER;
    if (FAILED(r->status))
        return r->status;
    if (!r->string)
        return WBEM_E_NOT_FOUND;
    *result = SysAllocString(r->string);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cr_GetResultServices(IWbemCallResult *This, LONG timeout, IWbemServices **services)
{
    ComResult *r = RES(This);
    if (!services)
        return WBEM_E_INVALID_PARAMETER;
    if (FAILED(r->status))
        return r->status;
    if (!r->services)
        return WBEM_E_NOT_FOUND;
    *services = r->services;
    r->services->lpVtbl->AddRef(r->services);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cr_GetCallStatus(IWbemCallResult *This, LONG timeout, LONG *status)
{
    if (!status)
        return WBEM_E_INVALID_PARAMETER;
    *status = RES(This)->status;
    return S_OK;
}

static const IWbemCallResultVtbl CALLRESULT_VTBL = {
    cr_QueryInterface, cr_AddRef, cr_Release, cr_GetResultObject, cr_GetResultString, cr_GetResultServices,
    cr_GetCallStatus,
};

HRESULT wc_callresult_new(HRESULT status, IWbemClassObject *object, BSTR string, IWbemServices *services, IWbemCallResult **out)
{
    ComResult *r = wb_alloc(sizeof(ComResult));
    if (!r)
        return E_OUTOFMEMORY;
    r->lpVtbl = &CALLRESULT_VTBL;
    r->refs = 1;
    wc_security_init(&r->security, (IUnknown *)r);
    r->status = status;
    r->object = object;
    if (object)
        object->lpVtbl->AddRef(object);
    r->string = string ? SysAllocString(string) : nullptr;
    r->services = services;
    if (services)
        services->lpVtbl->AddRef(services);
    InterlockedIncrement(&g_wbem_locks);
    *out = (IWbemCallResult *)r;
    return S_OK;
}

/* ------------------------------------------------------------------ enumerators */

void wl_free(WList *list)
{
    for (UINT i = 0; i < list->count; i++)
        wo_free(list->items[i]);
    wb_free(list->items);
    memset(list, 0, sizeof(*list));
}

BOOL wl_emit(void *context, WObj *object)
{
    WList *list = context;
    WB_GROW(list->items, list->count, list->capacity);
    list->items[list->count++] = wo_clone(object);
    return TRUE;
}

typedef struct ComEnum {
    const IEnumWbemClassObjectVtbl *lpVtbl;
    LONG refs;
    WSecurity security;
    WList list;
    UINT position;
} ComEnum;

#define ENUM(This) ((ComEnum *)(This))

static HRESULT STDMETHODCALLTYPE en_QueryInterface(IEnumWbemClassObject *This, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IClientSecurity))
        return wc_security_query(&ENUM(This)->security, riid, out);
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IEnumWbemClassObject)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    return wb_no_interface("en", riid, out);
}

static ULONG STDMETHODCALLTYPE en_AddRef(IEnumWbemClassObject *This)
{
    return (ULONG)InterlockedIncrement(&ENUM(This)->refs);
}

static ULONG STDMETHODCALLTYPE en_Release(IEnumWbemClassObject *This)
{
    ComEnum *e = ENUM(This);
    LONG n = InterlockedDecrement(&e->refs);
    if (!n) {
        wl_free(&e->list);
        wb_free(e);
        InterlockedDecrement(&g_wbem_locks);
    }
    return (ULONG)n;
}

static HRESULT STDMETHODCALLTYPE en_Reset(IEnumWbemClassObject *This)
{
    ENUM(This)->position = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE en_Next(IEnumWbemClassObject *This, LONG timeout, ULONG count, IWbemClassObject **objects,
                                         ULONG *returned)
{
    ComEnum *e = ENUM(This);
    if (!objects || !returned || !count)
        return WBEM_E_INVALID_PARAMETER;
    ULONG got = 0;
    HRESULT hr = S_OK;
    while (got < count && e->position < e->list.count) {
        hr = wc_object(wo_clone(e->list.items[e->position]), &IID_IWbemClassObject, (void **)&objects[got]);
        if (FAILED(hr))
            break;
        e->position++;
        got++;
    }
    *returned = got;
    if (FAILED(hr)) {
        for (ULONG i = 0; i < got; i++)
            objects[i]->lpVtbl->Release(objects[i]);
        *returned = 0;
        return hr;
    }
    return got == count ? S_OK : WBEM_S_FALSE;
}

static HRESULT STDMETHODCALLTYPE en_NextAsync(IEnumWbemClassObject *This, ULONG count, IWbemObjectSink *sink)
{
    ComEnum *e = ENUM(This);
    if (!sink)
        return WBEM_E_INVALID_PARAMETER;
    ULONG sent = 0;
    while (sent < count && e->position < e->list.count) {
        IWbemClassObject *o;
        if (FAILED(wc_object(wo_clone(e->list.items[e->position]), &IID_IWbemClassObject, (void **)&o)))
            break;
        e->position++;
        sent++;
        sink->lpVtbl->Indicate(sink, 1, &o);
        o->lpVtbl->Release(o);
    }
    if (e->position >= e->list.count)
        sink->lpVtbl->SetStatus(sink, WBEM_STATUS_COMPLETE, WBEM_S_NO_ERROR, nullptr, nullptr);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE en_Clone(IEnumWbemClassObject *This, IEnumWbemClassObject **copy)
{
    ComEnum *e = ENUM(This);
    WList list = {0};
    for (UINT i = 0; i < e->list.count; i++)
        wl_emit(&list, e->list.items[i]);
    HRESULT hr = wc_enum_new(&list, copy);
    if (SUCCEEDED(hr))
        ENUM(*copy)->position = e->position;
    else
        wl_free(&list);
    return hr;
}

static HRESULT STDMETHODCALLTYPE en_Skip(IEnumWbemClassObject *This, LONG timeout, ULONG count)
{
    ComEnum *e = ENUM(This);
    if (e->position + count > e->list.count) {
        e->position = e->list.count;
        return WBEM_S_FALSE;
    }
    e->position += count;
    return S_OK;
}

static const IEnumWbemClassObjectVtbl ENUM_VTBL = {
    en_QueryInterface, en_AddRef, en_Release, en_Reset, en_Next, en_NextAsync, en_Clone, en_Skip,
};

HRESULT wc_enum_new(WList *list, IEnumWbemClassObject **out)
{
    ComEnum *e = wb_alloc(sizeof(ComEnum));
    if (!e)
        return E_OUTOFMEMORY;
    e->lpVtbl = &ENUM_VTBL;
    e->refs = 1;
    wc_security_init(&e->security, (IUnknown *)e);
    e->list = *list;
    memset(list, 0, sizeof(*list));
    InterlockedIncrement(&g_wbem_locks);
    *out = (IEnumWbemClassObject *)e;
    return S_OK;
}

/* ------------------------------------------------------------------ the locator */

typedef struct ComSimple {
    const void *lpVtbl;
    LONG refs;
} ComSimple;

static BOOL machine_is_local(const WCHAR *server)
{
    if (!server || !*server || !wcscmp(server, L".") || wb_eq(server, L"localhost") || !wcscmp(server, L"127.0.0.1"))
        return TRUE;
    WCHAR name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = RTL_NUMBER_OF(name);
    return GetComputerNameW(name, &size) && wb_eq(server, name);
}

/* the default namespace, which the scripting key of the registry names. */
static WCHAR *default_namespace(void)
{
    WCHAR value[256];
    DWORD size = sizeof(value), type;
    HKEY key;
    if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\WBEM\\Scripting", 0, KEY_QUERY_VALUE, &key)) {
        LONG error = RegQueryValueExW(key, L"Default Namespace", nullptr, &type, (BYTE *)value, &size);
        RegCloseKey(key);
        if (!error && type == REG_SZ && value[0])
            return wb_wcsdup(value);
    }
    return wb_wcsdup(L"root\\cimv2");
}

HRESULT wm_connect(const WCHAR *resource, const WCHAR *user, const WCHAR *password, LONG flags, IWbemContext *context,
                   IWbemServices **services)
{
    if (!services)
        return WBEM_E_INVALID_PARAMETER;
    *services = nullptr;
    WPath path;
    wp_init(&path);
    WCHAR *ns = nullptr;
    if (resource && *resource) {
        HRESULT hr = wp_parse(&path, resource, WBEMPATH_CREATE_ACCEPT_ALL | WBEMPATH_TREAT_SINGLE_IDENT_AS_NS);
        if (FAILED(hr) || path.has_class) {
            wp_free(&path);
            return FAILED(hr) ? hr : WBEM_E_INVALID_NAMESPACE;
        }
        if (!machine_is_local(path.server)) {
            wp_free(&path);
            return HRESULT_FROM_WIN32(1722);
        }
        ns = wp_text(&path, WBEMPATH_GET_NAMESPACE_ONLY);
    }
    wp_free(&path);
    if (!ns || !*ns) {
        wb_free(ns);
        ns = default_namespace();
    }
    if (user && *user) {
        wb_free(ns);
        return WBEM_E_LOCAL_CREDENTIALS;
    }
    HRESULT hr = wc_services_new(ns, context, services);
    wb_free(ns);
    return hr;
}

static HRESULT STDMETHODCALLTYPE lo_QueryInterface(IWbemLocator *This, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IWbemLocator)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    return wb_no_interface("lo", riid, out);
}

static ULONG STDMETHODCALLTYPE lo_AddRef(IWbemLocator *This)
{
    return (ULONG)InterlockedIncrement(&((ComSimple *)This)->refs);
}

static ULONG STDMETHODCALLTYPE lo_Release(IWbemLocator *This)
{
    ComSimple *c = (ComSimple *)This;
    LONG n = InterlockedDecrement(&c->refs);
    if (!n) {
        wb_free(c);
        InterlockedDecrement(&g_wbem_locks);
    }
    return (ULONG)n;
}

static HRESULT STDMETHODCALLTYPE lo_ConnectServer(IWbemLocator *This, const BSTR resource, const BSTR user, const BSTR password,
                                                  const BSTR locale, LONG flags, const BSTR authority, IWbemContext *context,
                                                  IWbemServices **services)
{
    return wm_connect(resource, user, password, flags, context, services);
}

static const IWbemLocatorVtbl LOCATOR_VTBL = {lo_QueryInterface, lo_AddRef, lo_Release, lo_ConnectServer};

HRESULT wm_locator_new(REFIID riid, void **out)
{
    ComSimple *c = wb_alloc(sizeof(ComSimple));
    if (!c)
        return E_OUTOFMEMORY;
    c->lpVtbl = &LOCATOR_VTBL;
    c->refs = 1;
    InterlockedIncrement(&g_wbem_locks);
    HRESULT hr = lo_QueryInterface((IWbemLocator *)c, riid, out);
    lo_Release((IWbemLocator *)c);
    return hr;
}

/* the login interface the locator would reach the service by. */

static HRESULT STDMETHODCALLTYPE lg_QueryInterface(IWbemLevel1Login *This, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IWbemLevel1Login)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    return wb_no_interface("lg", riid, out);
}

static ULONG STDMETHODCALLTYPE lg_AddRef(IWbemLevel1Login *This)
{
    return (ULONG)InterlockedIncrement(&((ComSimple *)This)->refs);
}

static ULONG STDMETHODCALLTYPE lg_Release(IWbemLevel1Login *This)
{
    ComSimple *c = (ComSimple *)This;
    LONG n = InterlockedDecrement(&c->refs);
    if (!n) {
        wb_free(c);
        InterlockedDecrement(&g_wbem_locks);
    }
    return (ULONG)n;
}

static HRESULT STDMETHODCALLTYPE lg_EstablishPosition(IWbemLevel1Login *This, LPWSTR locales, DWORD flags, DWORD *max_version)
{
    if (max_version)
        *max_version = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE lg_RequestChallenge(IWbemLevel1Login *This, LPWSTR resource, LPWSTR user, UCHAR *nonce)
{
    return WBEM_E_NOT_SUPPORTED;
}

static HRESULT STDMETHODCALLTYPE lg_WBEMLogin(IWbemLevel1Login *This, LPWSTR locale, UCHAR *token, LONG flags,
                                              IWbemContext *context, IWbemServices **services)
{
    return WBEM_E_NOT_SUPPORTED;
}

static HRESULT STDMETHODCALLTYPE lg_NTLMLogin(IWbemLevel1Login *This, LPWSTR resource, LPWSTR locale, LONG flags,
                                              IWbemContext *context, IWbemServices **services)
{
    return wm_connect(resource, nullptr, nullptr, flags, context, services);
}

static const IWbemLevel1LoginVtbl LOGIN_VTBL = {
    lg_QueryInterface, lg_AddRef, lg_Release, lg_EstablishPosition, lg_RequestChallenge, lg_WBEMLogin, lg_NTLMLogin,
};

HRESULT wm_login_new(REFIID riid, void **out)
{
    ComSimple *c = wb_alloc(sizeof(ComSimple));
    if (!c)
        return E_OUTOFMEMORY;
    c->lpVtbl = &LOGIN_VTBL;
    c->refs = 1;
    InterlockedIncrement(&g_wbem_locks);
    HRESULT hr = lg_QueryInterface((IWbemLevel1Login *)c, riid, out);
    lg_Release((IWbemLevel1Login *)c);
    return hr;
}

/* ------------------------------------------------------------------ the text source */

typedef struct IWbemObjectTextSrc IWbemObjectTextSrc;
#define IWbemObjectTextSrc_METHODS(T)                                                                                  \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetText)(T * This, LONG flags, IWbemClassObject *object, ULONG format,                  \
                                        IWbemContext *context, BSTR *text);                                            \
    HRESULT(STDMETHODCALLTYPE *CreateFromText)(T * This, LONG flags, BSTR text, ULONG format, IWbemContext *context,   \
                                               IWbemClassObject **object);
MIME_COM_INTERFACE(IWbemObjectTextSrc)

static HRESULT STDMETHODCALLTYPE ts_QueryInterface(IWbemObjectTextSrc *This, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IWbemObjectTextSrc)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    return wb_no_interface("ts", riid, out);
}

static ULONG STDMETHODCALLTYPE ts_AddRef(IWbemObjectTextSrc *This)
{
    return (ULONG)InterlockedIncrement(&((ComSimple *)This)->refs);
}

static ULONG STDMETHODCALLTYPE ts_Release(IWbemObjectTextSrc *This)
{
    ComSimple *c = (ComSimple *)This;
    LONG n = InterlockedDecrement(&c->refs);
    if (!n) {
        wb_free(c);
        InterlockedDecrement(&g_wbem_locks);
    }
    return (ULONG)n;
}

#define WMI_OBJ_TEXT_CIM_DTD_2_0 1
#define WMI_OBJ_TEXT_WMI_DTD_2_0 2
#define WMI_OBJ_TEXT_WMI_EXT1 3

static HRESULT STDMETHODCALLTYPE ts_GetText(IWbemObjectTextSrc *This, LONG flags, IWbemClassObject *object, ULONG format,
                                            IWbemContext *context, BSTR *text)
{
    WObj *o = wc_unwrap(object);
    if (!o || !text || flags)
        return WBEM_E_INVALID_PARAMETER;
    if (format != 1)
        return WBEM_E_INVALID_PARAMETER;
    WCHAR *t = wo_text(o, 0);
    *text = SysAllocString(t);
    wb_free(t);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ts_CreateFromText(IWbemObjectTextSrc *This, LONG flags, BSTR text, ULONG format,
                                                   IWbemContext *context, IWbemClassObject **object)
{
    return WBEM_E_NOT_SUPPORTED;
}

static const IWbemObjectTextSrcVtbl TEXTSRC_VTBL = {
    ts_QueryInterface, ts_AddRef, ts_Release, ts_GetText, ts_CreateFromText,
};

static HRESULT textsrc_new(REFIID riid, void **out)
{
    ComSimple *c = wb_alloc(sizeof(ComSimple));
    if (!c)
        return E_OUTOFMEMORY;
    c->lpVtbl = &TEXTSRC_VTBL;
    c->refs = 1;
    InterlockedIncrement(&g_wbem_locks);
    HRESULT hr = ts_QueryInterface((IWbemObjectTextSrc *)c, riid, out);
    ts_Release((IWbemObjectTextSrc *)c);
    return hr;
}

/* ------------------------------------------------------------------ class factories */

typedef struct ComFactory {
    const IClassFactoryVtbl *lpVtbl;
    LONG refs;
    int kind;
} ComFactory;

enum { K_LOCATOR, K_OBJECT, K_CONTEXT, K_PATH, K_STATUS, K_COMPILER, K_TEXT, K_LOGIN };

static HRESULT STDMETHODCALLTYPE cf_QueryInterface(IClassFactory *This, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    return wb_no_interface("cf", riid, out);
}

static ULONG STDMETHODCALLTYPE cf_AddRef(IClassFactory *This)
{
    return (ULONG)InterlockedIncrement(&((ComFactory *)This)->refs);
}

static ULONG STDMETHODCALLTYPE cf_Release(IClassFactory *This)
{
    ComFactory *f = (ComFactory *)This;
    LONG n = InterlockedDecrement(&f->refs);
    if (!n)
        wb_free(f);
    return (ULONG)n;
}

static HRESULT STDMETHODCALLTYPE cf_CreateInstance(IClassFactory *This, IUnknown *outer, REFIID riid, void **out)
{
    ComFactory *f = (ComFactory *)This;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    switch (f->kind) {
    case K_LOCATOR:
        return wm_locator_new(riid, out);
    case K_OBJECT: {
        WObj *o = wo_new(TRUE, nullptr, nullptr);
        return wc_object(o, riid, out);
    }
    case K_CONTEXT: {
        IWbemContext *c;
        HRESULT hr = wc_context_new(&c);
        if (SUCCEEDED(hr)) {
            hr = c->lpVtbl->QueryInterface(c, riid, out);
            c->lpVtbl->Release(c);
        }
        return hr;
    }
    case K_PATH:
        return wp_com_path_new(riid, out);
    case K_STATUS:
        return wm_status_text_new(riid, out);
    case K_COMPILER:
        return wm_compiler_new(riid, out);
    case K_TEXT:
        return textsrc_new(riid, out);
    case K_LOGIN:
        return wm_login_new(riid, out);
    }
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE cf_LockServer(IClassFactory *This, BOOL lock)
{
    if (lock)
        InterlockedIncrement(&g_wbem_locks);
    else
        InterlockedDecrement(&g_wbem_locks);
    return S_OK;
}

static const IClassFactoryVtbl FACTORY_VTBL = {cf_QueryInterface, cf_AddRef, cf_Release, cf_CreateInstance, cf_LockServer};

HRESULT wm_class_factory(REFCLSID clsid, REFIID riid, void **out)
{
    int kind;
    if (IsEqualCLSID(clsid, &CLSID_WbemLocator) || IsEqualCLSID(clsid, &CLSID_WbemAdministrativeLocator) ||
        IsEqualCLSID(clsid, &CLSID_WbemAuthenticatedLocator) || IsEqualCLSID(clsid, &CLSID_WbemUnauthenticatedLocator))
        kind = K_LOCATOR;
    else if (IsEqualCLSID(clsid, &CLSID_WbemClassObject))
        kind = K_OBJECT;
    else if (IsEqualCLSID(clsid, &CLSID_WbemContext))
        kind = K_CONTEXT;
    else if (IsEqualCLSID(clsid, &CLSID_WbemDefPath))
        kind = K_PATH;
    else if (IsEqualCLSID(clsid, &CLSID_WbemStatusCodeText))
        kind = K_STATUS;
    else if (IsEqualCLSID(clsid, &CLSID_MofCompiler))
        kind = K_COMPILER;
    else if (IsEqualCLSID(clsid, &CLSID_WbemObjectTextSrc))
        kind = K_TEXT;
    else
        return CLASS_E_CLASSNOTAVAILABLE;
    ComFactory *f = wb_alloc(sizeof(ComFactory));
    if (!f)
        return E_OUTOFMEMORY;
    f->lpVtbl = &FACTORY_VTBL;
    f->refs = 1;
    f->kind = kind;
    HRESULT hr = cf_QueryInterface((IClassFactory *)f, riid, out);
    cf_Release((IClassFactory *)f);
    return hr;
}
