/* the services object a client holds for a namespace: classes, instances, queries and methods. */
#include "wbemcore.h"

DECLSPEC_IMPORT BOOL WINAPI ConvertStringSecurityDescriptorToSecurityDescriptorW(LPCWSTR text, DWORD revision,
                                                                                 PSECURITY_DESCRIPTOR *descriptor, PULONG size);
DECLSPEC_IMPORT BOOL WINAPI ConvertSecurityDescriptorToStringSecurityDescriptorW(PSECURITY_DESCRIPTOR descriptor, DWORD revision,
                                                                                 DWORD information, LPWSTR *text, PULONG size);
DECLSPEC_IMPORT DWORD WINAPI GetSecurityDescriptorLength(PSECURITY_DESCRIPTOR descriptor);

#define DEFAULT_NAMESPACE_SD L"O:BAG:BAD:(A;CIID;0x1fffff;;;BA)(A;CIID;0x13;;;NS)(A;CIID;0x13;;;LS)(A;CIID;0x13;;;AU)"

typedef struct ComSvc {
    const IWbemServicesVtbl *lpVtbl;
    LONG refs;
    WSecurity security;
    WCHAR *ns;
    IWbemContext *context;
} ComSvc;

#define SVC(This) ((ComSvc *)(This))

static HRESULT STDMETHODCALLTYPE sv_QueryInterface(IWbemServices *This, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IClientSecurity))
        return wc_security_query(&SVC(This)->security, riid, out);
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IWbemServices)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    return wb_no_interface("sv", riid, out);
}

static ULONG STDMETHODCALLTYPE sv_AddRef(IWbemServices *This)
{
    return (ULONG)InterlockedIncrement(&SVC(This)->refs);
}

static ULONG STDMETHODCALLTYPE sv_Release(IWbemServices *This)
{
    ComSvc *s = SVC(This);
    LONG n = InterlockedDecrement(&s->refs);
    if (!n) {
        if (s->context)
            s->context->lpVtbl->Release(s->context);
        wb_free(s->ns);
        wb_free(s);
        InterlockedDecrement(&g_wbem_locks);
    }
    return (ULONG)n;
}

/* the namespace and the relative path of an object path, which may name another namespace. */
static HRESULT resolve(ComSvc *s, const WCHAR *text, WNs **ns, WCHAR **relative)
{
    WPath path;
    wp_init(&path);
    HRESULT hr = wp_parse(&path, text, WBEMPATH_CREATE_ACCEPT_ALL);
    if (FAILED(hr)) {
        wp_free(&path);
        return hr;
    }
    if (path.ns_count) {
        WCHAR *name = wp_text(&path, WBEMPATH_GET_NAMESPACE_ONLY);
        *ns = wr_find_ns(name);
        wb_free(name);
    } else {
        *ns = wr_find_ns(s->ns);
    }
    if (!*ns)
        hr = WBEM_E_INVALID_NAMESPACE;
    else
        *relative = wp_text(&path, WBEMPATH_GET_RELATIVE_ONLY);
    wp_free(&path);
    return hr;
}

static HRESULT result_for(HRESULT status, IWbemClassObject *object, const WCHAR *string, IWbemCallResult **out)
{
    if (!out)
        return S_OK;
    BSTR text = string ? SysAllocString(string) : nullptr;
    HRESULT hr = wc_callresult_new(status, object, text, nullptr, out);
    SysFreeString(text);
    return hr;
}

/* ------------------------------------------------------------------ namespaces and objects */

static HRESULT STDMETHODCALLTYPE sv_OpenNamespace(IWbemServices *This, const BSTR name, LONG flags, IWbemContext *context,
                                                  IWbemServices **services, IWbemCallResult **result)
{
    ComSvc *s = SVC(This);
    if (!name || !services)
        return WBEM_E_INVALID_PARAMETER;
    wr_lock();
    WCHAR *full = wb_format(L"%s\\%s", s->ns, name);
    WNs *ns = wr_find_ns(full);
    HRESULT hr = ns ? S_OK : WBEM_E_NOT_FOUND;
    if (SUCCEEDED(hr))
        hr = wc_services_new(ns->path, context ? context : s->context, services);
    wb_free(full);
    wr_unlock();
    if (SUCCEEDED(hr) && result)
        hr = wc_callresult_new(S_OK, nullptr, nullptr, *services, result);
    return hr;
}

static HRESULT STDMETHODCALLTYPE sv_CancelAsyncCall(IWbemServices *This, IWbemObjectSink *sink)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sv_QueryObjectSink(IWbemServices *This, LONG flags, IWbemObjectSink **sink)
{
    return WBEM_E_NOT_SUPPORTED;
}

static HRESULT get_object(ComSvc *s, const WCHAR *path, IWbemClassObject **object)
{
    if (!path || !*path)
        return wc_object(wo_new(TRUE, nullptr, nullptr), &IID_IWbemClassObject, (void **)object);
    wr_lock();
    WNs *ns;
    WCHAR *relative = nullptr;
    HRESULT hr = resolve(s, path, &ns, &relative);
    WObj *found = nullptr;
    if (SUCCEEDED(hr)) {
        if (!relative || !*relative)
            hr = WBEM_E_INVALID_OBJECT_PATH;
        else
            hr = wprov_get(ns, relative, &found);
    }
    wb_free(relative);
    wr_unlock();
    if (SUCCEEDED(hr))
        hr = wc_object(found, &IID_IWbemClassObject, (void **)object);
    return hr;
}

static HRESULT STDMETHODCALLTYPE sv_GetObject(IWbemServices *This, const BSTR path, LONG flags, IWbemContext *context,
                                              IWbemClassObject **object, IWbemCallResult **result)
{
    IWbemClassObject *o = nullptr;
    HRESULT hr = get_object(SVC(This), path, &o);
    if (result) {
        HRESULT made = result_for(hr, o, nullptr, result);
        if (FAILED(made)) {
            if (o)
                o->lpVtbl->Release(o);
            return made;
        }
        if (!object) {
            if (o)
                o->lpVtbl->Release(o);
            return hr;
        }
    }
    if (object)
        *object = o;
    else if (o)
        o->lpVtbl->Release(o);
    return hr;
}

static void finish(IWbemObjectSink *sink, HRESULT hr, IWbemClassObject *object)
{
    if (sink)
        sink->lpVtbl->SetStatus(sink, WBEM_STATUS_COMPLETE, hr, nullptr, object);
}

static HRESULT STDMETHODCALLTYPE sv_GetObjectAsync(IWbemServices *This, const BSTR path, LONG flags, IWbemContext *context,
                                                   IWbemObjectSink *sink)
{
    if (!sink)
        return WBEM_E_INVALID_PARAMETER;
    IWbemClassObject *o = nullptr;
    HRESULT hr = get_object(SVC(This), path, &o);
    if (o) {
        sink->lpVtbl->Indicate(sink, 1, &o);
        o->lpVtbl->Release(o);
    }
    finish(sink, hr, nullptr);
    return S_OK;
}

static HRESULT put_class(ComSvc *s, IWbemClassObject *object, LONG flags, WCHAR **path)
{
    WObj *o = wc_unwrap(object);
    if (!o || !o->is_class)
        return WBEM_E_INVALID_PARAMETER;
    wr_lock();
    wr_write_begin();
    WNs *ns = wr_find_ns(s->ns);
    HRESULT hr = ns ? wr_put_class(ns, o, flags) : WBEM_E_INVALID_NAMESPACE;
    if (SUCCEEDED(hr) && path)
        *path = wb_wcsdup(o->name);
    wr_write_end();
    wr_unlock();
    return hr;
}

static HRESULT STDMETHODCALLTYPE sv_PutClass(IWbemServices *This, IWbemClassObject *object, LONG flags, IWbemContext *context,
                                             IWbemCallResult **result)
{
    WCHAR *path = nullptr;
    HRESULT hr = put_class(SVC(This), object, flags, &path);
    HRESULT made = result_for(hr, nullptr, path, result);
    wb_free(path);
    return SUCCEEDED(hr) ? made : hr;
}

static HRESULT STDMETHODCALLTYPE sv_PutClassAsync(IWbemServices *This, IWbemClassObject *object, LONG flags,
                                                  IWbemContext *context, IWbemObjectSink *sink)
{
    HRESULT hr = put_class(SVC(This), object, flags, nullptr);
    finish(sink, hr, nullptr);
    return S_OK;
}

static HRESULT delete_class(ComSvc *s, const WCHAR *name)
{
    if (!name)
        return WBEM_E_INVALID_PARAMETER;
    wr_lock();
    wr_write_begin();
    WNs *ns = wr_find_ns(s->ns);
    HRESULT hr = ns ? wr_delete_class(ns, name) : WBEM_E_INVALID_NAMESPACE;
    wr_write_end();
    wr_unlock();
    return hr;
}

static HRESULT STDMETHODCALLTYPE sv_DeleteClass(IWbemServices *This, const BSTR name, LONG flags, IWbemContext *context,
                                                IWbemCallResult **result)
{
    HRESULT hr = delete_class(SVC(This), name);
    HRESULT made = result_for(hr, nullptr, nullptr, result);
    return SUCCEEDED(hr) ? made : hr;
}

static HRESULT STDMETHODCALLTYPE sv_DeleteClassAsync(IWbemServices *This, const BSTR name, LONG flags, IWbemContext *context,
                                                     IWbemObjectSink *sink)
{
    finish(sink, delete_class(SVC(This), name), nullptr);
    return S_OK;
}

/* ------------------------------------------------------------------ enumerations */

static HRESULT class_list(ComSvc *s, const WCHAR *super, LONG flags, WList *list)
{
    wr_lock();
    WNs *ns = wr_find_ns(s->ns);
    HRESULT hr = ns ? S_OK : WBEM_E_INVALID_NAMESPACE;
    if (SUCCEEDED(hr) && super && *super && !wr_find_class(ns, super))
        hr = WBEM_E_INVALID_CLASS;
    if (SUCCEEDED(hr)) {
        WObj **subs;
        UINT n = wr_subclasses(ns, super, !(flags & WBEM_FLAG_SHALLOW), &subs);
        for (UINT i = 0; i < n; i++)
            wl_emit(list, subs[i]);
        wb_free(subs);
    }
    wr_unlock();
    return hr;
}

static HRESULT STDMETHODCALLTYPE sv_CreateClassEnum(IWbemServices *This, const BSTR super, LONG flags, IWbemContext *context,
                                                    IEnumWbemClassObject **enumerator)
{
    if (!enumerator)
        return WBEM_E_INVALID_PARAMETER;
    WList list = {0};
    HRESULT hr = class_list(SVC(This), super, flags, &list);
    if (SUCCEEDED(hr))
        hr = wc_enum_new(&list, enumerator);
    wl_free(&list);
    return hr;
}

static void indicate_list(IWbemObjectSink *sink, WList *list)
{
    for (UINT i = 0; i < list->count; i++) {
        IWbemClassObject *o;
        if (SUCCEEDED(wc_object(wo_clone(list->items[i]), &IID_IWbemClassObject, (void **)&o))) {
            sink->lpVtbl->Indicate(sink, 1, &o);
            o->lpVtbl->Release(o);
        }
    }
}

static HRESULT STDMETHODCALLTYPE sv_CreateClassEnumAsync(IWbemServices *This, const BSTR super, LONG flags,
                                                         IWbemContext *context, IWbemObjectSink *sink)
{
    if (!sink)
        return WBEM_E_INVALID_PARAMETER;
    WList list = {0};
    HRESULT hr = class_list(SVC(This), super, flags, &list);
    indicate_list(sink, &list);
    wl_free(&list);
    finish(sink, hr, nullptr);
    return S_OK;
}

static HRESULT put_instance(ComSvc *s, IWbemClassObject *object, LONG flags, WCHAR **path)
{
    WObj *o = wc_unwrap(object);
    if (!o || o->is_class)
        return WBEM_E_INVALID_PARAMETER;
    wr_lock();
    wr_write_begin();
    WNs *ns = wr_find_ns(s->ns);
    HRESULT hr = ns ? wprov_put(ns, o, flags) : WBEM_E_INVALID_NAMESPACE;
    if (SUCCEEDED(hr) && path) {
        WCHAR *rel = wo_relpath(o);
        *path = rel ? rel : wb_wcsdup(o->name);
    }
    wr_write_end();
    wr_unlock();
    return hr;
}

static HRESULT STDMETHODCALLTYPE sv_PutInstance(IWbemServices *This, IWbemClassObject *object, LONG flags, IWbemContext *context,
                                                IWbemCallResult **result)
{
    WCHAR *path = nullptr;
    HRESULT hr = put_instance(SVC(This), object, flags, &path);
    HRESULT made = result_for(hr, nullptr, path, result);
    wb_free(path);
    return SUCCEEDED(hr) ? made : hr;
}

static HRESULT STDMETHODCALLTYPE sv_PutInstanceAsync(IWbemServices *This, IWbemClassObject *object, LONG flags,
                                                     IWbemContext *context, IWbemObjectSink *sink)
{
    finish(sink, put_instance(SVC(This), object, flags, nullptr), nullptr);
    return S_OK;
}

static HRESULT delete_instance(ComSvc *s, const WCHAR *path)
{
    if (!path)
        return WBEM_E_INVALID_PARAMETER;
    wr_lock();
    wr_write_begin();
    WNs *ns;
    WCHAR *relative = nullptr;
    HRESULT hr = resolve(s, path, &ns, &relative);
    if (SUCCEEDED(hr))
        hr = wprov_delete(ns, relative);
    wb_free(relative);
    wr_write_end();
    wr_unlock();
    return hr;
}

static HRESULT STDMETHODCALLTYPE sv_DeleteInstance(IWbemServices *This, const BSTR path, LONG flags, IWbemContext *context,
                                                   IWbemCallResult **result)
{
    HRESULT hr = delete_instance(SVC(This), path);
    HRESULT made = result_for(hr, nullptr, nullptr, result);
    return SUCCEEDED(hr) ? made : hr;
}

static HRESULT STDMETHODCALLTYPE sv_DeleteInstanceAsync(IWbemServices *This, const BSTR path, LONG flags,
                                                        IWbemContext *context, IWbemObjectSink *sink)
{
    finish(sink, delete_instance(SVC(This), path), nullptr);
    return S_OK;
}

static HRESULT instance_list(ComSvc *s, const WCHAR *class_name, LONG flags, WList *list)
{
    if (!class_name || !*class_name)
        return WBEM_E_INVALID_PARAMETER;
    wr_lock();
    WNs *ns = wr_find_ns(s->ns);
    HRESULT hr = ns ? wprov_enum(ns, class_name, !(flags & WBEM_FLAG_SHALLOW), wl_emit, list) : WBEM_E_INVALID_NAMESPACE;
    wr_unlock();
    return hr;
}

static HRESULT STDMETHODCALLTYPE sv_CreateInstanceEnum(IWbemServices *This, const BSTR class_name, LONG flags,
                                                       IWbemContext *context, IEnumWbemClassObject **enumerator)
{
    if (!enumerator)
        return WBEM_E_INVALID_PARAMETER;
    WList list = {0};
    HRESULT hr = instance_list(SVC(This), class_name, flags, &list);
    if (SUCCEEDED(hr))
        hr = wc_enum_new(&list, enumerator);
    wl_free(&list);
    return hr;
}

static HRESULT STDMETHODCALLTYPE sv_CreateInstanceEnumAsync(IWbemServices *This, const BSTR class_name, LONG flags,
                                                            IWbemContext *context, IWbemObjectSink *sink)
{
    if (!sink)
        return WBEM_E_INVALID_PARAMETER;
    WList list = {0};
    HRESULT hr = instance_list(SVC(This), class_name, flags, &list);
    indicate_list(sink, &list);
    wl_free(&list);
    finish(sink, hr, nullptr);
    return S_OK;
}

static HRESULT query_list(ComSvc *s, const WCHAR *language, const WCHAR *query, LONG flags, WList *list)
{
    if (!language || !query)
        return WBEM_E_INVALID_PARAMETER;
    if (!wb_eq(language, L"WQL"))
        return WBEM_E_INVALID_QUERY_TYPE;
    wr_lock();
    WNs *ns = wr_find_ns(s->ns);
    HRESULT hr = ns ? wq_execute(ns, query, flags, wl_emit, list) : WBEM_E_INVALID_NAMESPACE;
    wr_unlock();
    return hr;
}

static HRESULT STDMETHODCALLTYPE sv_ExecQuery(IWbemServices *This, const BSTR language, const BSTR query, LONG flags,
                                              IWbemContext *context, IEnumWbemClassObject **enumerator)
{
    if (!enumerator)
        return WBEM_E_INVALID_PARAMETER;
    WList list = {0};
    HRESULT hr = query_list(SVC(This), language, query, flags, &list);
    if (SUCCEEDED(hr))
        hr = wc_enum_new(&list, enumerator);
    wl_free(&list);
    return hr;
}

static HRESULT STDMETHODCALLTYPE sv_ExecQueryAsync(IWbemServices *This, const BSTR language, const BSTR query, LONG flags,
                                                   IWbemContext *context, IWbemObjectSink *sink)
{
    if (!sink)
        return WBEM_E_INVALID_PARAMETER;
    WList list = {0};
    HRESULT hr = query_list(SVC(This), language, query, flags, &list);
    indicate_list(sink, &list);
    wl_free(&list);
    finish(sink, hr, nullptr);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sv_ExecNotificationQuery(IWbemServices *This, const BSTR language, const BSTR query, LONG flags,
                                                          IWbemContext *context, IEnumWbemClassObject **enumerator)
{
    return WBEM_E_NOT_SUPPORTED;
}

static HRESULT STDMETHODCALLTYPE sv_ExecNotificationQueryAsync(IWbemServices *This, const BSTR language, const BSTR query,
                                                               LONG flags, IWbemContext *context, IWbemObjectSink *sink)
{
    return WBEM_E_NOT_SUPPORTED;
}

/* ------------------------------------------------------------------ methods */

static HRESULT system_security(WNs *ns, const WCHAR *method, WObj *in, WObj **out)
{
    WObj *class_object = wr_find_class(ns, L"__SystemSecurity");
    WMeth *m = class_object ? wo_meth(class_object, method) : nullptr;
    if (!m)
        return WBEM_E_METHOD_NOT_IMPLEMENTED;
    WObj *result = m->out ? wo_spawn(m->out) : wo_new(FALSE, L"__PARAMETERS", nullptr);
    DWORD status = 0;
    if (wb_eq(method, L"GetSD")) {
        PSECURITY_DESCRIPTOR sd;
        ULONG size;
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(ns->sddl ? ns->sddl : DEFAULT_NAMESPACE_SD, 1, &sd, &size)) {
            DWORD length = GetSecurityDescriptorLength(sd);
            SAFEARRAY *array = SafeArrayCreateVector(VT_UI1, 0, length);
            void *data;
            if (array && SUCCEEDED(SafeArrayAccessData(array, &data))) {
                memcpy(data, sd, length);
                SafeArrayUnaccessData(array);
            }
            WProp *p = wo_prop(result, L"SD");
            if (p && array) {
                VariantClear(&p->value);
                p->value.vt = VT_ARRAY | VT_UI1;
                p->value.parray = array;
            }
            LocalFree(sd);
        } else {
            status = GetLastError();
        }
    } else if (wb_eq(method, L"SetSD")) {
        WProp *p = in ? wo_prop(in, L"SD") : nullptr;
        if (!p || !(p->value.vt & VT_ARRAY) || !p->value.parray) {
            wo_free(result);
            return WBEM_E_INVALID_METHOD_PARAMETERS;
        }
        void *data;
        LONG low, high;
        SafeArrayGetLBound(p->value.parray, 1, &low);
        SafeArrayGetUBound(p->value.parray, 1, &high);
        if (SUCCEEDED(SafeArrayAccessData(p->value.parray, &data))) {
            LPWSTR text;
            if (ConvertSecurityDescriptorToStringSecurityDescriptorW(data, 1, 0xF, &text, nullptr)) {
                wb_free(ns->sddl);
                ns->sddl = wb_wcsdup(text);
                wr_save();
                LocalFree(text);
            } else {
                status = GetLastError();
            }
            SafeArrayUnaccessData(p->value.parray);
        }
    }
    WProp *rv = wo_prop(result, L"ReturnValue");
    if (rv) {
        VariantClear(&rv->value);
        rv->value.vt = VT_I4;
        rv->value.lVal = (LONG)status;
    }
    *out = result;
    return S_OK;
}

static HRESULT exec_method(ComSvc *s, const WCHAR *object, const WCHAR *method, IWbemClassObject *in, IWbemClassObject **out)
{
    if (!object || !method)
        return WBEM_E_INVALID_PARAMETER;
    wr_lock();
    wr_write_begin();
    WNs *ns;
    WCHAR *relative = nullptr;
    HRESULT hr = resolve(s, object, &ns, &relative);
    WObj *result = nullptr;
    WObj *input = in ? wc_unwrap(in) : nullptr;
    if (SUCCEEDED(hr)) {
        WPath path;
        wp_init(&path);
        if (SUCCEEDED(wp_parse(&path, relative, WBEMPATH_CREATE_ACCEPT_ALL)) && path.class_name &&
            wb_eq(path.class_name, L"__SystemSecurity"))
            hr = system_security(ns, method, input, &result);
        else
            hr = wprov_exec(ns, relative, method, input, &result);
        wp_free(&path);
    }
    wb_free(relative);
    wr_write_end();
    wr_unlock();
    if (SUCCEEDED(hr)) {
        if (out)
            hr = wc_object(result, &IID_IWbemClassObject, (void **)out);
        else
            wo_free(result);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE sv_ExecMethod(IWbemServices *This, const BSTR object, const BSTR method, LONG flags,
                                               IWbemContext *context, IWbemClassObject *in, IWbemClassObject **out,
                                               IWbemCallResult **result)
{
    IWbemClassObject *o = nullptr;
    HRESULT hr = exec_method(SVC(This), object, method, in, &o);
    if (result) {
        HRESULT made = result_for(hr, o, nullptr, result);
        if (FAILED(made)) {
            if (o)
                o->lpVtbl->Release(o);
            return made;
        }
    }
    if (out)
        *out = o;
    else if (o)
        o->lpVtbl->Release(o);
    return hr;
}

static HRESULT STDMETHODCALLTYPE sv_ExecMethodAsync(IWbemServices *This, const BSTR object, const BSTR method, LONG flags,
                                                    IWbemContext *context, IWbemClassObject *in, IWbemObjectSink *sink)
{
    IWbemClassObject *o = nullptr;
    HRESULT hr = exec_method(SVC(This), object, method, in, &o);
    if (sink) {
        if (o)
            sink->lpVtbl->Indicate(sink, 1, &o);
        finish(sink, hr, nullptr);
    }
    if (o)
        o->lpVtbl->Release(o);
    return S_OK;
}

static const IWbemServicesVtbl SERVICES_VTBL = {
    sv_QueryInterface,     sv_AddRef,           sv_Release,         sv_OpenNamespace,      sv_CancelAsyncCall,
    sv_QueryObjectSink,    sv_GetObject,        sv_GetObjectAsync,  sv_PutClass,           sv_PutClassAsync,
    sv_DeleteClass,        sv_DeleteClassAsync, sv_CreateClassEnum, sv_CreateClassEnumAsync, sv_PutInstance,
    sv_PutInstanceAsync,   sv_DeleteInstance,   sv_DeleteInstanceAsync, sv_CreateInstanceEnum, sv_CreateInstanceEnumAsync,
    sv_ExecQuery,          sv_ExecQueryAsync,   sv_ExecNotificationQuery, sv_ExecNotificationQueryAsync, sv_ExecMethod,
    sv_ExecMethodAsync,
};

HRESULT wc_services_new(const WCHAR *ns, IWbemContext *context, IWbemServices **out)
{
    wr_lock();
    WNs *found = wr_find_ns(ns);
    WCHAR *path = found ? wb_wcsdup(found->path) : nullptr;
    wr_unlock();
    if (!path)
        return WBEM_E_INVALID_NAMESPACE;
    ComSvc *s = wb_alloc(sizeof(ComSvc));
    if (!s) {
        wb_free(path);
        return E_OUTOFMEMORY;
    }
    s->lpVtbl = &SERVICES_VTBL;
    s->refs = 1;
    wc_security_init(&s->security, (IUnknown *)s);
    s->ns = path;
    s->context = context;
    if (context)
        context->lpVtbl->AddRef(context);
    InterlockedIncrement(&g_wbem_locks);
    *out = (IWbemServices *)s;
    return S_OK;
}
