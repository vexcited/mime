/* the com side of objects: IWbemClassObject with IWbemObjectAccess, and the qualifier sets of a class, property or method. */
#include "wbemcore.h"

typedef struct ComObj {
    const IWbemObjectAccessVtbl *lpVtbl;
    LONG refs;
    CRITICAL_SECTION lock;
    WObj *o;
    UINT enum_index;
    LONG enum_flags;
    BOOL enumerating;
    UINT meth_index;
    BOOL meth_enumerating;
} ComObj;

#define OBJ(This) ((ComObj *)(This))

typedef enum QualTarget { QT_OBJECT, QT_PROPERTY, QT_METHOD } QualTarget;

typedef struct ComQuals {
    const IWbemQualifierSetVtbl *lpVtbl;
    LONG refs;
    ComObj *owner;
    QualTarget target;
    WCHAR *name;
    UINT enum_index;
    LONG enum_flags;
    BOOL enumerating;
} ComQuals;

#define QUALS(This) ((ComQuals *)(This))

static const IWbemObjectAccessVtbl OBJECT_VTBL;
static const IWbemQualifierSetVtbl QUALS_VTBL;

WObj *wc_unwrap(IWbemClassObject *object)
{
    if (!object || (const void *)object->lpVtbl != (const void *)&OBJECT_VTBL)
        return nullptr;
    return OBJ(object)->o;
}

static HRESULT new_com(WObj *o, ComObj **out)
{
    ComObj *c = wb_alloc(sizeof(ComObj));
    if (!c) {
        wo_free(o);
        return E_OUTOFMEMORY;
    }
    c->lpVtbl = &OBJECT_VTBL;
    c->refs = 1;
    InitializeCriticalSection(&c->lock);
    c->o = o;
    InterlockedIncrement(&g_wbem_locks);
    *out = c;
    return S_OK;
}

HRESULT wc_object(WObj *o, REFIID riid, void **out)
{
    ComObj *c;
    HRESULT hr = new_com(o, &c);
    if (FAILED(hr))
        return hr;
    hr = OBJ(c)->lpVtbl->QueryInterface((IWbemObjectAccess *)c, riid, out);
    ((IWbemObjectAccess *)c)->lpVtbl->Release((IWbemObjectAccess *)c);
    return hr;
}

static HRESULT wrap_clone(const WObj *o, IWbemClassObject **out)
{
    if (!o) {
        *out = nullptr;
        return S_OK;
    }
    return wc_object(wo_clone(o), &IID_IWbemClassObject, (void **)out);
}

/* ------------------------------------------------------------------ unknown */

static HRESULT STDMETHODCALLTYPE co_QueryInterface(IWbemObjectAccess *This, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IWbemClassObject) ||
        IsEqualIID(riid, &IID_IWbemObjectAccess)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    return wb_no_interface("co", riid, out);
}

static ULONG STDMETHODCALLTYPE co_AddRef(IWbemObjectAccess *This)
{
    return (ULONG)InterlockedIncrement(&OBJ(This)->refs);
}

static ULONG STDMETHODCALLTYPE co_Release(IWbemObjectAccess *This)
{
    ComObj *c = OBJ(This);
    LONG n = InterlockedDecrement(&c->refs);
    if (!n) {
        DeleteCriticalSection(&c->lock);
        wo_free(c->o);
        wb_free(c);
        InterlockedDecrement(&g_wbem_locks);
    }
    return (ULONG)n;
}

/* ------------------------------------------------------------------ properties */

static LONG flavor_of(const WProp *p)
{
    return p->local ? WBEM_FLAVOR_ORIGIN_LOCAL : WBEM_FLAVOR_ORIGIN_PROPAGATED;
}

static BOOL is_system_name(const WCHAR *name)
{
    return name[0] == '_' && name[1] == '_';
}

static HRESULT STDMETHODCALLTYPE co_GetQualifierSet(IWbemObjectAccess *This, IWbemQualifierSet **set)
{
    if (!set)
        return WBEM_E_INVALID_PARAMETER;
    ComQuals *q = wb_alloc(sizeof(ComQuals));
    q->lpVtbl = &QUALS_VTBL;
    q->refs = 1;
    q->owner = OBJ(This);
    q->target = QT_OBJECT;
    co_AddRef(This);
    *set = (IWbemQualifierSet *)q;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE co_Get(IWbemObjectAccess *This, LPCWSTR name, LONG flags, VARIANT *value, CIMTYPE *type,
                                        LONG *flavor)
{
    ComObj *c = OBJ(This);
    if (!name || flags)
        return WBEM_E_INVALID_PARAMETER;
    EnterCriticalSection(&c->lock);
    HRESULT hr = WBEM_E_NOT_FOUND;
    VARIANT v;
    wv_init(&v);
    if (is_system_name(name) && wo_system_prop(c->o, name, &v, type)) {
        if (flavor)
            *flavor = WBEM_FLAVOR_ORIGIN_SYSTEM;
        hr = S_OK;
    } else {
        WProp *p = wo_prop(c->o, name);
        if (p) {
            VariantClear(&v);
            wv_copy(&v, &p->value);
            if (type)
                *type = p->type;
            if (flavor)
                *flavor = flavor_of(p);
            hr = S_OK;
        }
    }
    LeaveCriticalSection(&c->lock);
    if (SUCCEEDED(hr)) {
        if (value) {
            VariantInit(value);
            *value = v;
        } else {
            VariantClear(&v);
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE co_Put(IWbemObjectAccess *This, LPCWSTR name, LONG flags, VARIANT *value, CIMTYPE type)
{
    ComObj *c = OBJ(This);
    if (!name || flags || !value)
        return WBEM_E_INVALID_PARAMETER;
    EnterCriticalSection(&c->lock);
    HRESULT hr;
    WProp *p = wo_prop(c->o, name);
    if (is_system_name(name)) {
        if (wb_eq(name, L"__CLASS") && value->vt == VT_BSTR && value->bstrVal && !c->o->name) {
            c->o->name = wb_wcsdup(value->bstrVal);
            hr = S_OK;
        } else if (wb_eq(name, L"__SUPERCLASS") && c->o->is_class && value->vt == VT_BSTR) {
            hr = WBEM_E_INVALID_OPERATION;
        } else {
            hr = WBEM_E_READ_ONLY;
        }
    } else if (p) {
        if (type && type != p->type && !(c->o->is_class && !p->origin))
            hr = WBEM_E_TYPE_MISMATCH;
        else if (!c->o->is_class && !p->local && FALSE)
            hr = WBEM_E_INVALID_OPERATION;
        else {
            VARIANT v;
            wv_init(&v);
            hr = wv_coerce(&v, value, p->type);
            if (SUCCEEDED(hr)) {
                VariantClear(&p->value);
                p->value = v;
            }
        }
    } else if (c->o->is_class) {
        if (!type || (type & ~(CIM_FLAG_ARRAY | 0xFF)) || (type & 0xFF) == 0) {
            hr = WBEM_E_INVALID_PARAMETER;
        } else {
            VARIANT v;
            wv_init(&v);
            hr = wv_coerce(&v, value, type);
            if (SUCCEEDED(hr)) {
                WProp *n = wo_add_prop(c->o, name, type, &v, nullptr);
                (void)n;
            }
            VariantClear(&v);
        }
    } else {
        hr = WBEM_E_NOT_FOUND;
    }
    LeaveCriticalSection(&c->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE co_Delete(IWbemObjectAccess *This, LPCWSTR name)
{
    ComObj *c = OBJ(This);
    if (!name)
        return WBEM_E_INVALID_PARAMETER;
    if (is_system_name(name))
        return WBEM_E_SYSTEM_PROPERTY;
    EnterCriticalSection(&c->lock);
    HRESULT hr = WBEM_E_NOT_FOUND;
    WProp *p = wo_prop(c->o, name);
    if (p) {
        if (c->o->is_class) {
            hr = p->local ? (wo_remove_prop(c->o, name), S_OK) : WBEM_E_PROPAGATED_PROPERTY;
        } else {
            VariantClear(&p->value);
            wv_init(&p->value);
            hr = WBEM_S_RESET_TO_DEFAULT;
        }
    }
    LeaveCriticalSection(&c->lock);
    return hr;
}

/* does the property pass the filters of a names or enumeration call. */
static BOOL passes(const WObj *o, const WProp *p, LONG flags)
{
    if ((flags & WBEM_FLAG_KEYS_ONLY) && !wq_true(&p->quals, L"key"))
        return FALSE;
    if (flags & WBEM_FLAG_REFS_ONLY) {
        if ((p->type & ~CIM_FLAG_ARRAY) != CIM_REFERENCE)
            return FALSE;
    }
    switch (flags & WBEM_MASK_CONDITION_ORIGIN) {
    case WBEM_FLAG_LOCAL_ONLY:
        return p->local;
    case WBEM_FLAG_PROPAGATED_ONLY:
        return !p->local;
    case WBEM_FLAG_SYSTEM_ONLY:
        return FALSE;
    default:
        return TRUE;
    }
}

static BOOL system_wanted(LONG flags)
{
    LONG origin = flags & WBEM_MASK_CONDITION_ORIGIN;
    return (origin == 0 || origin == WBEM_FLAG_SYSTEM_ONLY) && !(flags & (WBEM_FLAG_KEYS_ONLY | WBEM_FLAG_REFS_ONLY));
}

static HRESULT STDMETHODCALLTYPE co_GetNames(IWbemObjectAccess *This, LPCWSTR qualifier, LONG flags, VARIANT *qvalue,
                                             SAFEARRAY **names)
{
    ComObj *c = OBJ(This);
    if (!names)
        return WBEM_E_INVALID_PARAMETER;
    EnterCriticalSection(&c->lock);
    WObj *o = c->o;
    LONG mode = flags & WBEM_MASK_PRIMARY_CONDITION;
    WCHAR **list = wb_alloc((o->prop_count + wo_system_count() + 1) * sizeof(WCHAR *));
    UINT n = 0;
    if (system_wanted(flags) && (mode == WBEM_FLAG_ALWAYS || !qualifier))
        for (UINT i = 0; i < wo_system_count(); i++)
            list[n++] = (WCHAR *)wo_system_name(i);
    for (UINT i = 0; i < o->prop_count; i++) {
        const WProp *p = &o->props[i];
        if (!passes(o, p, flags))
            continue;
        if (mode != WBEM_FLAG_ALWAYS && qualifier) {
            WQual *q = wq_find(&p->quals, qualifier);
            BOOL have = q != nullptr;
            if (mode == WBEM_FLAG_ONLY_IF_TRUE && !(have && (q->value.vt != VT_BOOL || q->value.boolVal)))
                continue;
            if (mode == WBEM_FLAG_ONLY_IF_FALSE && have && (q->value.vt != VT_BOOL || q->value.boolVal))
                continue;
            if (mode == WBEM_FLAG_ONLY_IF_FALSE && !have)
                continue;
            if (mode == WBEM_FLAG_ONLY_IF_IDENTICAL) {
                if (!have || !qvalue)
                    continue;
                WCHAR *a = wv_text(&q->value, CIM_STRING), *b = wv_text(qvalue, CIM_STRING);
                BOOL same = !wcscmp(a, b);
                wb_free(a);
                wb_free(b);
                if (!same)
                    continue;
            }
        }
        list[n++] = p->name;
    }
    SAFEARRAY *array = SafeArrayCreateVector(VT_BSTR, 0, n);
    for (UINT i = 0; array && i < n; i++) {
        LONG index = (LONG)i;
        BSTR s = SysAllocString(list[i]);
        SafeArrayPutElement(array, &index, s);
        SysFreeString(s);
    }
    LeaveCriticalSection(&c->lock);
    wb_free(list);
    *names = array;
    return array ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE co_BeginEnumeration(IWbemObjectAccess *This, LONG flags)
{
    ComObj *c = OBJ(This);
    if (flags & ~(WBEM_FLAG_KEYS_ONLY | WBEM_FLAG_REFS_ONLY | WBEM_MASK_CONDITION_ORIGIN))
        return WBEM_E_INVALID_PARAMETER;
    EnterCriticalSection(&c->lock);
    c->enum_flags = flags;
    c->enum_index = 0;
    c->enumerating = TRUE;
    LeaveCriticalSection(&c->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE co_Next(IWbemObjectAccess *This, LONG flags, BSTR *name, VARIANT *value, CIMTYPE *type,
                                         LONG *flavor)
{
    ComObj *c = OBJ(This);
    if (flags)
        return WBEM_E_INVALID_PARAMETER;
    EnterCriticalSection(&c->lock);
    HRESULT hr = WBEM_E_INVALID_OPERATION;
    if (c->enumerating) {
        UINT sys = system_wanted(c->enum_flags) ? wo_system_count() : 0;
        hr = WBEM_S_NO_MORE_DATA;
        while (c->enum_index < sys + c->o->prop_count) {
            UINT i = c->enum_index++;
            if (i < sys) {
                VARIANT v;
                wv_init(&v);
                wo_system_prop(c->o, wo_system_name(i), &v, type);
                if (name)
                    *name = SysAllocString(wo_system_name(i));
                if (value) {
                    VariantInit(value);
                    *value = v;
                } else {
                    VariantClear(&v);
                }
                if (flavor)
                    *flavor = WBEM_FLAVOR_ORIGIN_SYSTEM;
                hr = S_OK;
                break;
            }
            WProp *p = &c->o->props[i - sys];
            if (!passes(c->o, p, c->enum_flags))
                continue;
            if (name)
                *name = SysAllocString(p->name);
            if (value) {
                VariantInit(value);
                wv_copy(value, &p->value);
            }
            if (type)
                *type = p->type;
            if (flavor)
                *flavor = flavor_of(p);
            hr = S_OK;
            break;
        }
    }
    LeaveCriticalSection(&c->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE co_EndEnumeration(IWbemObjectAccess *This)
{
    ComObj *c = OBJ(This);
    EnterCriticalSection(&c->lock);
    c->enumerating = FALSE;
    LeaveCriticalSection(&c->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE co_GetPropertyQualifierSet(IWbemObjectAccess *This, LPCWSTR property, IWbemQualifierSet **set)
{
    ComObj *c = OBJ(This);
    if (!property || !set)
        return WBEM_E_INVALID_PARAMETER;
    EnterCriticalSection(&c->lock);
    WProp *p = wo_prop(c->o, property);
    LeaveCriticalSection(&c->lock);
    if (!p)
        return is_system_name(property) ? WBEM_E_SYSTEM_PROPERTY : WBEM_E_NOT_FOUND;
    ComQuals *q = wb_alloc(sizeof(ComQuals));
    q->lpVtbl = &QUALS_VTBL;
    q->refs = 1;
    q->owner = c;
    q->target = QT_PROPERTY;
    q->name = wb_wcsdup(p->name);
    co_AddRef(This);
    *set = (IWbemQualifierSet *)q;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE co_Clone(IWbemObjectAccess *This, IWbemClassObject **copy)
{
    ComObj *c = OBJ(This);
    if (!copy)
        return WBEM_E_INVALID_PARAMETER;
    EnterCriticalSection(&c->lock);
    HRESULT hr = wrap_clone(c->o, copy);
    LeaveCriticalSection(&c->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE co_GetObjectText(IWbemObjectAccess *This, LONG flags, BSTR *text)
{
    ComObj *c = OBJ(This);
    if (!text)
        return WBEM_E_INVALID_PARAMETER;
    EnterCriticalSection(&c->lock);
    WCHAR *t = wo_text(c->o, flags);
    LeaveCriticalSection(&c->lock);
    *text = SysAllocString(t);
    wb_free(t);
    return *text ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE co_SpawnDerivedClass(IWbemObjectAccess *This, LONG flags, IWbemClassObject **derived)
{
    ComObj *c = OBJ(This);
    if (flags || !derived)
        return WBEM_E_INVALID_PARAMETER;
    EnterCriticalSection(&c->lock);
    HRESULT hr;
    if (!c->o->is_class || !c->o->name) {
        hr = WBEM_E_INVALID_OPERATION;
    } else {
        WObj *d = wo_derive(c->o, nullptr);
        hr = wc_object(d, &IID_IWbemClassObject, (void **)derived);
    }
    LeaveCriticalSection(&c->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE co_SpawnInstance(IWbemObjectAccess *This, LONG flags, IWbemClassObject **instance)
{
    ComObj *c = OBJ(This);
    if (flags || !instance)
        return WBEM_E_INVALID_PARAMETER;
    EnterCriticalSection(&c->lock);
    HRESULT hr;
    if (!c->o->is_class || !c->o->name) {
        hr = WBEM_E_INVALID_OPERATION;
    } else {
        WObj *d = wo_spawn(c->o);
        hr = wc_object(d, &IID_IWbemClassObject, (void **)instance);
    }
    LeaveCriticalSection(&c->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE co_CompareTo(IWbemObjectAccess *This, LONG flags, IWbemClassObject *other)
{
    WObj *b = wc_unwrap(other);
    if (!b)
        return WBEM_E_INVALID_PARAMETER;
    ComObj *c = OBJ(This);
    EnterCriticalSection(&c->lock);
    BOOL same = wo_equal(c->o, b, flags);
    LeaveCriticalSection(&c->lock);
    return same ? S_OK : WBEM_S_DIFFERENT;
}

static HRESULT STDMETHODCALLTYPE co_GetPropertyOrigin(IWbemObjectAccess *This, LPCWSTR name, BSTR *class_name)
{
    ComObj *c = OBJ(This);
    if (!name || !class_name)
        return WBEM_E_INVALID_PARAMETER;
    if (is_system_name(name)) {
        *class_name = SysAllocString(L"___SYSTEM");
        return S_OK;
    }
    EnterCriticalSection(&c->lock);
    WProp *p = wo_prop(c->o, name);
    *class_name = p ? SysAllocString(p->origin ? p->origin : c->o->name) : nullptr;
    LeaveCriticalSection(&c->lock);
    return p ? S_OK : WBEM_E_NOT_FOUND;
}

static HRESULT STDMETHODCALLTYPE co_InheritsFrom(IWbemObjectAccess *This, LPCWSTR ancestor)
{
    ComObj *c = OBJ(This);
    if (!ancestor)
        return WBEM_E_INVALID_PARAMETER;
    EnterCriticalSection(&c->lock);
    BOOL yes = c->o->name && wo_inherits(c->o, ancestor) && !wb_eq(c->o->name, ancestor);
    if (!c->o->is_class && c->o->name && wb_eq(c->o->name, ancestor))
        yes = TRUE;
    LeaveCriticalSection(&c->lock);
    return yes ? S_OK : WBEM_S_FALSE;
}

/* ------------------------------------------------------------------ methods */

static HRESULT STDMETHODCALLTYPE co_GetMethod(IWbemObjectAccess *This, LPCWSTR name, LONG flags, IWbemClassObject **in,
                                              IWbemClassObject **out)
{
    ComObj *c = OBJ(This);
    if (!name || flags)
        return WBEM_E_INVALID_PARAMETER;
    EnterCriticalSection(&c->lock);
    WMeth *m = wo_meth(c->o, name);
    HRESULT hr = WBEM_E_NOT_FOUND;
    if (m) {
        hr = S_OK;
        if (in)
            hr = wrap_clone(m->in, in);
        if (SUCCEEDED(hr) && out)
            hr = wrap_clone(m->out, out);
    }
    LeaveCriticalSection(&c->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE co_PutMethod(IWbemObjectAccess *This, LPCWSTR name, LONG flags, IWbemClassObject *in,
                                              IWbemClassObject *out)
{
    ComObj *c = OBJ(This);
    if (!name || flags || !c->o->is_class)
        return WBEM_E_INVALID_PARAMETER;
    WObj *i = in ? wc_unwrap(in) : nullptr, *o = out ? wc_unwrap(out) : nullptr;
    if ((in && !i) || (out && !o))
        return WBEM_E_INVALID_PARAMETER;
    EnterCriticalSection(&c->lock);
    wo_add_meth(c->o, name, wo_clone(i), wo_clone(o), nullptr);
    LeaveCriticalSection(&c->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE co_DeleteMethod(IWbemObjectAccess *This, LPCWSTR name)
{
    ComObj *c = OBJ(This);
    if (!name)
        return WBEM_E_INVALID_PARAMETER;
    EnterCriticalSection(&c->lock);
    WMeth *m = wo_meth(c->o, name);
    HRESULT hr = !m ? WBEM_E_NOT_FOUND : !m->local ? WBEM_E_PROPAGATED_METHOD : (wo_remove_meth(c->o, name), S_OK);
    LeaveCriticalSection(&c->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE co_BeginMethodEnumeration(IWbemObjectAccess *This, LONG flags)
{
    ComObj *c = OBJ(This);
    EnterCriticalSection(&c->lock);
    c->meth_index = 0;
    c->meth_enumerating = TRUE;
    LeaveCriticalSection(&c->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE co_NextMethod(IWbemObjectAccess *This, LONG flags, BSTR *name, IWbemClassObject **in,
                                               IWbemClassObject **out)
{
    ComObj *c = OBJ(This);
    EnterCriticalSection(&c->lock);
    HRESULT hr = WBEM_E_INVALID_OPERATION;
    if (c->meth_enumerating) {
        hr = WBEM_S_NO_MORE_DATA;
        if (c->meth_index < c->o->meth_count) {
            WMeth *m = &c->o->meths[c->meth_index++];
            if (name)
                *name = SysAllocString(m->name);
            hr = S_OK;
            if (in)
                hr = wrap_clone(m->in, in);
            if (SUCCEEDED(hr) && out)
                hr = wrap_clone(m->out, out);
        }
    }
    LeaveCriticalSection(&c->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE co_EndMethodEnumeration(IWbemObjectAccess *This)
{
    ComObj *c = OBJ(This);
    EnterCriticalSection(&c->lock);
    c->meth_enumerating = FALSE;
    LeaveCriticalSection(&c->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE co_GetMethodQualifierSet(IWbemObjectAccess *This, LPCWSTR method, IWbemQualifierSet **set)
{
    ComObj *c = OBJ(This);
    if (!method || !set)
        return WBEM_E_INVALID_PARAMETER;
    EnterCriticalSection(&c->lock);
    WMeth *m = wo_meth(c->o, method);
    LeaveCriticalSection(&c->lock);
    if (!m)
        return WBEM_E_NOT_FOUND;
    ComQuals *q = wb_alloc(sizeof(ComQuals));
    q->lpVtbl = &QUALS_VTBL;
    q->refs = 1;
    q->owner = c;
    q->target = QT_METHOD;
    q->name = wb_wcsdup(m->name);
    co_AddRef(This);
    *set = (IWbemQualifierSet *)q;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE co_GetMethodOrigin(IWbemObjectAccess *This, LPCWSTR method, BSTR *class_name)
{
    ComObj *c = OBJ(This);
    if (!method || !class_name)
        return WBEM_E_INVALID_PARAMETER;
    EnterCriticalSection(&c->lock);
    WMeth *m = wo_meth(c->o, method);
    *class_name = m ? SysAllocString(m->origin ? m->origin : c->o->name) : nullptr;
    LeaveCriticalSection(&c->lock);
    return m ? S_OK : WBEM_E_NOT_FOUND;
}

/* ------------------------------------------------------------------ access by handle */

/* a handle is the type in the high half and the position of the property plus one in the low half. */
static HRESULT STDMETHODCALLTYPE co_GetPropertyType(IWbemObjectAccess *This, LPCWSTR name, CIMTYPE *type, LONG *flavor)
{
    return co_Get(This, name, 0, nullptr, type, flavor);
}

static HRESULT STDMETHODCALLTYPE co_GetPropertyHandle(IWbemObjectAccess *This, LPCWSTR name, CIMTYPE *type, LONG *handle)
{
    ComObj *c = OBJ(This);
    if (!name || !handle)
        return WBEM_E_INVALID_PARAMETER;
    EnterCriticalSection(&c->lock);
    HRESULT hr = WBEM_E_NOT_FOUND;
    for (UINT i = 0; i < c->o->prop_count; i++) {
        if (wb_eq(c->o->props[i].name, name)) {
            if (type)
                *type = c->o->props[i].type;
            *handle = (LONG)(((ULONG)c->o->props[i].type << 16) | (i + 1));
            hr = S_OK;
            break;
        }
    }
    LeaveCriticalSection(&c->lock);
    return hr;
}

static WProp *by_handle(ComObj *c, LONG handle)
{
    UINT index = (UINT)(handle & 0xFFFF);
    if (!index || index > c->o->prop_count)
        return nullptr;
    WProp *p = &c->o->props[index - 1];
    return (LONG)(((ULONG)p->type << 16) | index) == handle ? p : nullptr;
}

static HRESULT STDMETHODCALLTYPE co_WritePropertyValue(IWbemObjectAccess *This, LONG handle, LONG size, const BYTE *data)
{
    ComObj *c = OBJ(This);
    if (!data)
        return WBEM_E_INVALID_PARAMETER;
    EnterCriticalSection(&c->lock);
    WProp *p = by_handle(c, handle);
    HRESULT hr = p ? S_OK : WBEM_E_INVALID_PARAMETER;
    if (p) {
        VARIANT v;
        VariantInit(&v);
        switch (p->type & ~CIM_FLAG_ARRAY) {
        case CIM_STRING:
        case CIM_DATETIME:
        case CIM_REFERENCE:
            v.vt = VT_BSTR;
            v.bstrVal = SysAllocStringLen((const WCHAR *)data, (UINT)(size / (LONG)sizeof(WCHAR)));
            break;
        case CIM_BOOLEAN:
            v.vt = VT_BOOL;
            v.boolVal = *(const VARIANT_BOOL *)data ? VARIANT_TRUE : VARIANT_FALSE;
            break;
        case CIM_UINT8:
        case CIM_SINT8:
            v.vt = VT_UI1;
            v.bVal = *data;
            break;
        case CIM_SINT16:
        case CIM_CHAR16:
        case CIM_UINT16:
            v.vt = VT_I4;
            v.lVal = (p->type == CIM_UINT16) ? (LONG)*(const USHORT *)data : (LONG)*(const SHORT *)data;
            break;
        case CIM_REAL32:
            v.vt = VT_R4;
            v.fltVal = *(const float *)data;
            break;
        case CIM_REAL64:
            v.vt = VT_R8;
            v.dblVal = *(const double *)data;
            break;
        case CIM_SINT64:
        case CIM_UINT64: {
            WCHAR text[32];
            if ((p->type & ~CIM_FLAG_ARRAY) == CIM_SINT64)
                _snwprintf(text, RTL_NUMBER_OF(text) - 1, L"%I64d", *(const LONGLONG *)data);
            else
                _snwprintf(text, RTL_NUMBER_OF(text) - 1, L"%I64u", *(const ULONGLONG *)data);
            v.vt = VT_BSTR;
            v.bstrVal = SysAllocString(text);
            break;
        }
        default:
            v.vt = VT_I4;
            v.lVal = *(const LONG *)data;
            break;
        }
        VARIANT stored;
        wv_init(&stored);
        hr = wv_coerce(&stored, &v, p->type);
        VariantClear(&v);
        if (SUCCEEDED(hr)) {
            VariantClear(&p->value);
            p->value = stored;
        }
    }
    LeaveCriticalSection(&c->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE co_ReadPropertyValue(IWbemObjectAccess *This, LONG handle, LONG size, LONG *needed, BYTE *data)
{
    ComObj *c = OBJ(This);
    if (!needed)
        return WBEM_E_INVALID_PARAMETER;
    EnterCriticalSection(&c->lock);
    WProp *p = by_handle(c, handle);
    HRESULT hr = p ? S_OK : WBEM_E_INVALID_PARAMETER;
    if (p) {
        BYTE raw[16];
        const void *source = raw;
        LONG length = 0;
        const VARIANT *v = &p->value;
        switch (p->type & ~CIM_FLAG_ARRAY) {
        case CIM_STRING:
        case CIM_DATETIME:
        case CIM_REFERENCE:
            source = v->vt == VT_BSTR && v->bstrVal ? (const void *)v->bstrVal : (const void *)L"";
            length = (LONG)((wcslen(source) + 1) * sizeof(WCHAR));
            break;
        case CIM_BOOLEAN:
            length = sizeof(VARIANT_BOOL);
            *(VARIANT_BOOL *)raw = v->boolVal;
            break;
        case CIM_UINT8:
        case CIM_SINT8:
            length = 1;
            raw[0] = v->bVal;
            break;
        case CIM_SINT16:
        case CIM_CHAR16:
        case CIM_UINT16:
            length = 2;
            *(USHORT *)raw = (USHORT)v->lVal;
            break;
        case CIM_REAL32:
            length = 4;
            *(float *)raw = v->fltVal;
            break;
        case CIM_REAL64:
            length = 8;
            *(double *)raw = v->dblVal;
            break;
        case CIM_SINT64:
        case CIM_UINT64: {
            BOOL negative;
            ULONGLONG magnitude = 0;
            VARIANT tmp;
            (void)negative;
            VariantInit(&tmp);
            if (v->vt == VT_BSTR && v->bstrVal)
                VarUI8FromStr(v->bstrVal, 0x409, 0, &magnitude);
            *(ULONGLONG *)raw = magnitude;
            length = 8;
            break;
        }
        default:
            length = 4;
            *(LONG *)raw = v->lVal;
            break;
        }
        *needed = length;
        if (data) {
            if (size < length)
                hr = WBEM_E_BUFFER_TOO_SMALL;
            else
                memcpy(data, source, (SIZE_T)length);
        }
    }
    LeaveCriticalSection(&c->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE co_ReadDWORD(IWbemObjectAccess *This, LONG handle, DWORD *value)
{
    LONG needed;
    BYTE raw[8];
    HRESULT hr = co_ReadPropertyValue(This, handle, sizeof(raw), &needed, raw);
    if (FAILED(hr))
        return hr;
    if (needed != 4)
        return WBEM_E_TYPE_MISMATCH;
    *value = *(DWORD *)raw;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE co_WriteDWORD(IWbemObjectAccess *This, LONG handle, DWORD value)
{
    return co_WritePropertyValue(This, handle, sizeof(value), (const BYTE *)&value);
}

static HRESULT STDMETHODCALLTYPE co_ReadQWORD(IWbemObjectAccess *This, LONG handle, ULONGLONG *value)
{
    LONG needed;
    BYTE raw[8];
    HRESULT hr = co_ReadPropertyValue(This, handle, sizeof(raw), &needed, raw);
    if (FAILED(hr))
        return hr;
    if (needed != 8)
        return WBEM_E_TYPE_MISMATCH;
    *value = *(ULONGLONG *)raw;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE co_WriteQWORD(IWbemObjectAccess *This, LONG handle, ULONGLONG value)
{
    return co_WritePropertyValue(This, handle, sizeof(value), (const BYTE *)&value);
}

static HRESULT STDMETHODCALLTYPE co_GetPropertyInfoByHandle(IWbemObjectAccess *This, LONG handle, BSTR *name, CIMTYPE *type)
{
    ComObj *c = OBJ(This);
    EnterCriticalSection(&c->lock);
    WProp *p = by_handle(c, handle);
    if (p) {
        if (name)
            *name = SysAllocString(p->name);
        if (type)
            *type = p->type;
    }
    LeaveCriticalSection(&c->lock);
    return p ? S_OK : WBEM_E_INVALID_PARAMETER;
}

static HRESULT STDMETHODCALLTYPE co_Lock(IWbemObjectAccess *This, LONG flags)
{
    EnterCriticalSection(&OBJ(This)->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE co_Unlock(IWbemObjectAccess *This, LONG flags)
{
    LeaveCriticalSection(&OBJ(This)->lock);
    return S_OK;
}

static const IWbemObjectAccessVtbl OBJECT_VTBL = {
    co_QueryInterface, co_AddRef, co_Release, co_GetQualifierSet, co_Get, co_Put, co_Delete, co_GetNames,
    co_BeginEnumeration, co_Next, co_EndEnumeration, co_GetPropertyQualifierSet, co_Clone, co_GetObjectText,
    co_SpawnDerivedClass, co_SpawnInstance, co_CompareTo, co_GetPropertyOrigin, co_InheritsFrom, co_GetMethod,
    co_PutMethod, co_DeleteMethod, co_BeginMethodEnumeration, co_NextMethod, co_EndMethodEnumeration,
    co_GetMethodQualifierSet, co_GetMethodOrigin, co_GetPropertyType, co_GetPropertyHandle, co_WritePropertyValue,
    co_ReadPropertyValue, co_ReadDWORD, co_WriteDWORD, co_ReadQWORD, co_WriteQWORD, co_GetPropertyInfoByHandle,
    co_Lock, co_Unlock,
};

/* ------------------------------------------------------------------ qualifier sets */

static WQuals *quals_of(ComQuals *q)
{
    WObj *o = q->owner->o;
    if (q->target == QT_OBJECT)
        return &o->quals;
    if (q->target == QT_PROPERTY) {
        WProp *p = wo_prop(o, q->name);
        return p ? &p->quals : nullptr;
    }
    WMeth *m = wo_meth(o, q->name);
    return m ? &m->quals : nullptr;
}

static HRESULT STDMETHODCALLTYPE cq_QueryInterface(IWbemQualifierSet *This, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IWbemQualifierSet)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    return wb_no_interface("cq", riid, out);
}

static ULONG STDMETHODCALLTYPE cq_AddRef(IWbemQualifierSet *This)
{
    return (ULONG)InterlockedIncrement(&QUALS(This)->refs);
}

static ULONG STDMETHODCALLTYPE cq_Release(IWbemQualifierSet *This)
{
    ComQuals *q = QUALS(This);
    LONG n = InterlockedDecrement(&q->refs);
    if (!n) {
        co_Release((IWbemObjectAccess *)q->owner);
        wb_free(q->name);
        wb_free(q);
    }
    return (ULONG)n;
}

#define QLOCK(q) EnterCriticalSection(&(q)->owner->lock)
#define QUNLOCK(q) LeaveCriticalSection(&(q)->owner->lock)

static HRESULT STDMETHODCALLTYPE cq_Get(IWbemQualifierSet *This, LPCWSTR name, LONG flags, VARIANT *value, LONG *flavor)
{
    ComQuals *q = QUALS(This);
    if (!name || flags)
        return WBEM_E_INVALID_PARAMETER;
    QLOCK(q);
    WQuals *list = quals_of(q);
    WQual *found = list ? wq_find(list, name) : nullptr;
    HRESULT hr = WBEM_E_NOT_FOUND;
    if (found) {
        if (value) {
            VariantInit(value);
            wv_copy(value, &found->value);
        }
        if (flavor)
            *flavor = found->flavor;
        hr = S_OK;
    }
    QUNLOCK(q);
    return hr;
}

static HRESULT STDMETHODCALLTYPE cq_Put(IWbemQualifierSet *This, LPCWSTR name, VARIANT *value, LONG flavor)
{
    ComQuals *q = QUALS(This);
    if (!name || !value)
        return WBEM_E_INVALID_PARAMETER;
    VARTYPE base = (VARTYPE)(value->vt & VT_TYPEMASK);
    if (wv_is_null(value) || (base != VT_BOOL && base != VT_BSTR && base != VT_I4 && base != VT_I2 && base != VT_R8 &&
                              base != VT_R4 && base != VT_UI1))
        return WBEM_E_INVALID_QUALIFIER_TYPE;
    if (flavor & ~(WBEM_FLAVOR_MASK_PROPAGATION | WBEM_FLAVOR_MASK_PERMISSIONS | WBEM_FLAVOR_MASK_AMENDED))
        return WBEM_E_INVALID_PARAMETER;
    QLOCK(q);
    WQuals *list = quals_of(q);
    HRESULT hr = WBEM_E_NOT_FOUND;
    if (list) {
        WQual *old = wq_find(list, name);
        if (old && (old->flavor & WBEM_FLAVOR_ORIGIN_PROPAGATED) && (old->flavor & WBEM_FLAVOR_NOT_OVERRIDABLE)) {
            hr = WBEM_E_OVERRIDE_NOT_ALLOWED;
        } else {
            VARIANT v;
            VariantInit(&v);
            if (base == VT_I2 || base == VT_UI1) {
                hr = VariantChangeType(&v, value, 0, VT_I4);
            } else {
                hr = VariantCopy(&v, value);
            }
            if (SUCCEEDED(hr)) {
                wq_set(list, name, &v, flavor);
                hr = S_OK;
            }
            VariantClear(&v);
        }
    }
    QUNLOCK(q);
    return hr;
}

static HRESULT STDMETHODCALLTYPE cq_Delete(IWbemQualifierSet *This, LPCWSTR name)
{
    ComQuals *q = QUALS(This);
    if (!name)
        return WBEM_E_INVALID_PARAMETER;
    QLOCK(q);
    WQuals *list = quals_of(q);
    WQual *found = list ? wq_find(list, name) : nullptr;
    HRESULT hr = !found ? WBEM_E_NOT_FOUND
                 : (found->flavor & WBEM_FLAVOR_ORIGIN_PROPAGATED) ? WBEM_E_PROPAGATED_QUALIFIER
                                                                   : (wq_remove(list, name), S_OK);
    QUNLOCK(q);
    return hr;
}

static BOOL qual_passes(const WQual *item, LONG flags)
{
    switch (flags & WBEM_MASK_CONDITION_ORIGIN) {
    case WBEM_FLAG_LOCAL_ONLY:
        return !(item->flavor & WBEM_FLAVOR_ORIGIN_PROPAGATED);
    case WBEM_FLAG_PROPAGATED_ONLY:
        return (item->flavor & WBEM_FLAVOR_ORIGIN_PROPAGATED) != 0;
    default:
        return TRUE;
    }
}

static HRESULT STDMETHODCALLTYPE cq_GetNames(IWbemQualifierSet *This, LONG flags, SAFEARRAY **names)
{
    ComQuals *q = QUALS(This);
    if (!names)
        return WBEM_E_INVALID_PARAMETER;
    QLOCK(q);
    WQuals *list = quals_of(q);
    UINT n = 0;
    for (UINT i = 0; list && i < list->count; i++)
        if (qual_passes(&list->items[i], flags))
            n++;
    SAFEARRAY *array = SafeArrayCreateVector(VT_BSTR, 0, n);
    UINT at = 0;
    for (UINT i = 0; array && list && i < list->count; i++) {
        if (!qual_passes(&list->items[i], flags))
            continue;
        LONG index = (LONG)at++;
        BSTR s = SysAllocString(list->items[i].name);
        SafeArrayPutElement(array, &index, s);
        SysFreeString(s);
    }
    QUNLOCK(q);
    *names = array;
    return array ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE cq_BeginEnumeration(IWbemQualifierSet *This, LONG flags)
{
    ComQuals *q = QUALS(This);
    q->enum_flags = flags;
    q->enum_index = 0;
    q->enumerating = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cq_Next(IWbemQualifierSet *This, LONG flags, BSTR *name, VARIANT *value, LONG *flavor)
{
    ComQuals *q = QUALS(This);
    if (!q->enumerating)
        return WBEM_E_INVALID_OPERATION;
    QLOCK(q);
    WQuals *list = quals_of(q);
    HRESULT hr = WBEM_S_NO_MORE_DATA;
    while (list && q->enum_index < list->count) {
        WQual *item = &list->items[q->enum_index++];
        if (!qual_passes(item, q->enum_flags))
            continue;
        if (name)
            *name = SysAllocString(item->name);
        if (value) {
            VariantInit(value);
            wv_copy(value, &item->value);
        }
        if (flavor)
            *flavor = item->flavor;
        hr = S_OK;
        break;
    }
    QUNLOCK(q);
    return hr;
}

static HRESULT STDMETHODCALLTYPE cq_EndEnumeration(IWbemQualifierSet *This)
{
    QUALS(This)->enumerating = FALSE;
    return S_OK;
}

static const IWbemQualifierSetVtbl QUALS_VTBL = {
    cq_QueryInterface, cq_AddRef, cq_Release, cq_Get, cq_Put, cq_Delete, cq_GetNames, cq_BeginEnumeration, cq_Next,
    cq_EndEnumeration,
};
