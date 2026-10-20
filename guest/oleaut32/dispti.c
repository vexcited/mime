/*
 * the type information of a dispatch interface that a program describes with tables, CreateDispTypeInfo: what
 * CreateStdDispatch and DispInvoke take when a program has no type library for its object. It is not made of a library:
 * the object holds the tables of the program (which have to stay where they are) and answers from them. What it gives
 * back is a coclass with one interface, and the interface is the type that has the functions, laid out as virtual
 * functions at the offsets 8 times the number of the method, the way windows does; everything it cannot say is
 * E_NOTIMPL.
 */
#include "oleaut32_private.h"

typedef struct {
    const ITypeInfoVtbl *lpVtbl;
    LONG Refs;
    LCID Lcid;
    TYPEKIND Kind;
    INTERFACEDATA *Data;
} DISP_TYPEINFO;

/* what the arguments of a call became to be handed to DispCallFunc. */
typedef struct {
    UINT Count;
    VARTYPE *Types;
    VARIANTARG **Pointers;
    VARIANTARG *Converted;
} INVOKE_ARGS;

static const ITypeInfoVtbl g_disp_typeinfo_vtbl;

static HRESULT disp_create(TYPEKIND kind, INTERFACEDATA *data, LCID lcid, ITypeInfo **out)
{
    DISP_TYPEINFO *info = AutAllocZero(sizeof(*info));
    if (!info)
        return E_OUTOFMEMORY;
    info->lpVtbl = &g_disp_typeinfo_vtbl;
    info->Refs = 1;
    info->Lcid = lcid;
    info->Kind = kind;
    info->Data = data;
    *out = (ITypeInfo *)info;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI CreateDispTypeInfo(INTERFACEDATA *data, LCID lcid, ITypeInfo **info)
{
    ITypeInfo *created;
    HRESULT hr = disp_create(TKIND_COCLASS, data, lcid, &created);
    if (FAILED(hr))
        return hr;
    *info = created;
    return S_OK;
}

/* the method of a dispid that the flags allow. */
static HRESULT method_of_dispid(DISP_TYPEINFO *t, DISPID id, WORD flags, METHODDATA **out)
{
    for (UINT i = 0; i < t->Data->cMembers; i++) {
        METHODDATA *m = &t->Data->pmethdata[i];
        if (m->dispid == id && (flags & m->wFlags)) {
            *out = m;
            return S_OK;
        }
    }
    return DISP_E_MEMBERNOTFOUND;
}

static HRESULT STDMETHODCALLTYPE di_query(ITypeInfo *This, REFIID riid, void **object)
{
    if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_ITypeInfo)) {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    *object = This;
    InterlockedIncrement(&((DISP_TYPEINFO *)This)->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE di_add_ref(ITypeInfo *This)
{
    return InterlockedIncrement(&((DISP_TYPEINFO *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE di_release(ITypeInfo *This)
{
    LONG refs = InterlockedDecrement(&((DISP_TYPEINFO *)This)->Refs);
    if (!refs)
        AutFree(This);
    return refs;
}

static HRESULT STDMETHODCALLTYPE di_get_type_attr(ITypeInfo *This, TYPEATTR **out)
{
    DISP_TYPEINFO *t = (DISP_TYPEINFO *)This;
    TYPEATTR *attr = AutAllocZero(sizeof(*attr));
    if (!attr)
        return E_OUTOFMEMORY;
    attr->typekind = t->Kind;
    attr->lcid = t->Lcid;
    if (t->Kind == TKIND_INTERFACE) {
        attr->cFuncs = (WORD)t->Data->cMembers;
        attr->cImplTypes = 0;
    } else if (t->Kind == TKIND_COCLASS) {
        attr->cImplTypes = 1;
    }
    attr->cbAlignment = 2;
    attr->cbSizeVft = 0xFFFF;
    attr->cbSizeInstance = 0xFFFF;
    attr->memidConstructor = MEMBERID_NIL;
    attr->memidDestructor = MEMBERID_NIL;
    *out = attr;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE di_not_implemented()
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE di_get_func_desc(ITypeInfo *This, UINT index, FUNCDESC **out)
{
    DISP_TYPEINFO *t = (DISP_TYPEINFO *)This;
    if (t->Kind != TKIND_INTERFACE)
        return E_FAIL;
    if (index >= t->Data->cMembers)
        return DISP_E_MEMBERNOTFOUND;
    METHODDATA *m = &t->Data->pmethdata[index];
    INVOKEKIND invoke;
    switch (m->wFlags) {
    case DISPATCH_METHOD: invoke = INVOKE_FUNC; break;
    case DISPATCH_PROPERTYGET: invoke = INVOKE_PROPERTYGET; break;
    case DISPATCH_PROPERTYPUT: invoke = INVOKE_PROPERTYPUT; break;
    case DISPATCH_PROPERTYPUTREF: invoke = INVOKE_PROPERTYPUTREF; break;
    default: return E_FAIL;
    }
    FUNCDESC *fd = AutAllocZero(sizeof(*fd));
    ELEMDESC *params = AutAllocZero((m->cArgs ? m->cArgs : 1) * sizeof(ELEMDESC));
    if (!fd || !params) {
        AutFree(fd);
        AutFree(params);
        return E_OUTOFMEMORY;
    }
    fd->memid = m->dispid;
    fd->funckind = FUNC_VIRTUAL;
    fd->invkind = invoke;
    fd->callconv = m->cc;
    fd->cParams = (SHORT)m->cArgs;
    fd->oVft = (SHORT)(sizeof(void *) * m->iMeth);
    fd->elemdescFunc.tdesc.vt = m->vtReturn;
    for (UINT i = 0; i < m->cArgs; i++)
        params[i].tdesc.vt = m->ppdata[i].vt;
    fd->lprgelemdescParam = params;
    *out = fd;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE di_get_var_desc(ITypeInfo *This, UINT index, VARDESC **out)
{
    return DISP_E_MEMBERNOTFOUND;
}

static HRESULT STDMETHODCALLTYPE di_get_names(ITypeInfo *This, MEMBERID id, BSTR *names, UINT max, UINT *count)
{
    DISP_TYPEINFO *t = (DISP_TYPEINFO *)This;
    METHODDATA *m;
    if (t->Kind != TKIND_INTERFACE || !max) {
        *count = 0;
        return S_OK;
    }
    HRESULT hr = method_of_dispid(t, id, 0xF, &m);
    if (FAILED(hr))
        return hr;
    UINT n = m->cArgs + 1 < max ? m->cArgs + 1 : max;
    memset(names, 0, n * sizeof(BSTR));
    UINT done = 0;
    for (; done < n; done++) {
        const OLECHAR *text = done ? m->ppdata[done - 1].szName : m->szName;
        names[done] = SysAllocString(text);
        if (text && !names[done]) {
            for (UINT k = 0; k < done; k++) {
                SysFreeString(names[k]);
                names[k] = nullptr;
            }
            return E_OUTOFMEMORY;
        }
    }
    *count = n;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE di_get_ref_type_of_impl_type(ITypeInfo *This, UINT index, HREFTYPE *ref)
{
    if (((DISP_TYPEINFO *)This)->Kind != TKIND_COCLASS)
        return E_UNEXPECTED;
    *ref = 0;
    return S_OK;
}

/* names are the same when they differ in case only, and in the width of characters for the languages that have two. */
static BOOL name_matches(DISP_TYPEINFO *t, const OLECHAR *name, int length, const OLECHAR *other)
{
    DWORD flags = NORM_IGNORECASE | 2;
    return CompareStringW(t->Lcid, flags, name, length, other, -1) == CSTR_EQUAL;
}

static HRESULT STDMETHODCALLTYPE di_get_ids_of_names(ITypeInfo *This, LPOLESTR *names, UINT count, MEMBERID *ids)
{
    DISP_TYPEINFO *t = (DISP_TYPEINFO *)This;
    if (!count)
        return E_INVALIDARG;
    int length = (int)wcslen(names[0]);
    UINT found = 0;
    for (; found < t->Data->cMembers; found++)
        if (name_matches(t, names[0], length, t->Data->pmethdata[found].szName))
            break;
    if (found == t->Data->cMembers) {
        memset(ids, 0xFF, count * sizeof(MEMBERID));
        return DISP_E_UNKNOWNNAME;
    }
    METHODDATA *m = &t->Data->pmethdata[found];
    ids[0] = m->dispid;
    HRESULT hr = S_OK;
    for (UINT i = 1; i < count; i++) {
        int param_length = (int)wcslen(names[i]);
        UINT p = 0;
        for (; p < m->cArgs; p++)
            if (name_matches(t, names[i], param_length, m->ppdata[p].szName))
                break;
        if (p == m->cArgs) {
            ids[i] = -1;
            hr = DISP_E_UNKNOWNNAME;
        } else {
            ids[i] = p;
        }
    }
    return hr;
}

static void release_args(INVOKE_ARGS *a)
{
    if (!a)
        return;
    if (a->Converted) {
        for (UINT i = 0; i < a->Count; i++)
            VariantClear(&a->Converted[i]);
        AutFree(a->Converted);
    }
    AutFree(a->Pointers);
    AutFree(a->Types);
    AutFree(a);
}

static HRESULT alloc_args(UINT count, INVOKE_ARGS **out)
{
    INVOKE_ARGS *a = AutAllocZero(sizeof(*a));
    if (!a)
        return E_OUTOFMEMORY;
    a->Count = count;
    if (count) {
        a->Converted = AutAllocZero(count * sizeof(VARIANTARG));
        a->Pointers = AutAllocZero(count * sizeof(VARIANTARG *));
        a->Types = AutAllocZero(count * sizeof(VARTYPE));
        if (!a->Converted || !a->Pointers || !a->Types) {
            release_args(a);
            return E_OUTOFMEMORY;
        }
    }
    *out = a;
    return S_OK;
}

/*
 * the arguments of a call in the order of the function: the last of a property put is the value and has to be the argument
 * named for it, a named argument is the parameter whose number its dispid is, the others come from the end of the list.
 * Types that variants convert to are converted, a variant is handed on as it is, and a reference or an array has to be
 * what the parameter says.
 */
static HRESULT get_invoke_args(METHODDATA *m, WORD flags, DISPPARAMS *params, INVOKE_ARGS **out, UINT *arg_error)
{
    INVOKE_ARGS *a;
    HRESULT hr = alloc_args(params->cArgs, &a);
    if (FAILED(hr))
        return hr;
    for (UINT i = 0; i < a->Count; i++) {
        UINT at = 0;
        if (i == a->Count - 1 && (flags & 0xC)) {
            if (!params->cNamedArgs || params->rgdispidNamedArgs[0] != DISPID_PROPERTYPUT) {
                release_args(a);
                return DISP_E_PARAMNOTOPTIONAL;
            }
        } else {
            BOOL named = FALSE;
            for (UINT k = 0; k < params->cNamedArgs; k++) {
                if (params->rgdispidNamedArgs[k] == (DISPID)i) {
                    named = TRUE;
                    at = k;
                    break;
                }
            }
            if (!named) {
                at = params->cArgs - i - 1;
                if ((int)at < (int)params->cNamedArgs) {
                    release_args(a);
                    return DISP_E_PARAMNOTFOUND;
                }
            }
        }
        VARIANTARG *arg = &params->rgvarg[at];
        VARTYPE vt = m->ppdata[i].vt;
        a->Types[i] = vt;
        BOOL convert = (vt >= VT_I2 && vt <= VT_BOOL) || vt == VT_UNKNOWN || vt == VT_DECIMAL || (vt >= VT_I1 && vt <= VT_UINT);
        if (vt == VT_VARIANT) {
            a->Pointers[i] = arg;
        } else if (convert) {
            hr = VariantChangeTypeEx(&a->Converted[i], arg, LOCALE_USER_DEFAULT, 0, vt);
            if (hr != S_OK) {
                *arg_error = at;
                if (hr == DISP_E_TYPEMISMATCH && arg->vt == VT_ERROR && arg->scode == DISP_E_PARAMNOTFOUND)
                    hr = DISP_E_PARAMNOTOPTIONAL;
                release_args(a);
                return hr;
            }
            a->Pointers[i] = &a->Converted[i];
        } else if (vt & (VT_BYREF | VT_ARRAY)) {
            if (arg->vt != vt) {
                release_args(a);
                *arg_error = at;
                return DISP_E_TYPEMISMATCH;
            }
            a->Pointers[i] = arg;
        } else {
            release_args(a);
            return E_INVALIDARG;
        }
    }
    *out = a;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE di_invoke(ITypeInfo *This, PVOID instance, MEMBERID id, WORD flags, DISPPARAMS *params,
                                           VARIANT *result, EXCEPINFO *excep, UINT *arg_error)
{
    DISP_TYPEINFO *t = (DISP_TYPEINFO *)This;
    VARIANT scratch;
    UINT scratch_error = 0;
    VariantInit(&scratch);
    VARIANT *out = result ? result : &scratch;
    UINT *error = arg_error ? arg_error : &scratch_error;
    if (flags & 0xFFF0)
        return E_INVALIDARG;
    METHODDATA *m = nullptr;
    for (UINT i = 0; i < t->Data->cMembers; i++) {
        METHODDATA *c = &t->Data->pmethdata[i];
        if (c->dispid == id && (c->wFlags & flags)) {
            m = c;
            break;
        }
    }
    BOOL through_getter = FALSE;
    if (!m) {
        if (!(flags & 0xC))
            return DISP_E_MEMBERNOTFOUND;
        /* a put of a property that has a getter and no put: the getter's object is asked to take it. */
        WORD other = flags != 4 ? 4 : 8;
        if (method_of_dispid(t, id, other, &m) != DISP_E_MEMBERNOTFOUND)
            return DISP_E_MEMBERNOTFOUND;
        if (FAILED(method_of_dispid(t, id, 2, &m)))
            return DISP_E_MEMBERNOTFOUND;
        if (m->cArgs)
            return DISP_E_MEMBERNOTFOUND;
        through_getter = TRUE;
    } else if (params->cArgs != m->cArgs) {
        if (params->cArgs > m->cArgs) {
            if (!(flags & 2)) {
                if (!(flags & 0xC) || !(m->wFlags & 0xC) || m->cArgs != 1 || FAILED(method_of_dispid(t, id, 2, &m)) || m->cArgs)
                    return DISP_E_BADPARAMCOUNT;
                through_getter = TRUE;
            } else if (m->wFlags & 2) {
                if (m->cArgs)
                    return DISP_E_BADPARAMCOUNT;
                through_getter = TRUE;
            }
        }
        if (!through_getter)
            return DISP_E_BADPARAMCOUNT;
    }
    if (through_getter) {
        VARIANT object;
        VariantInit(&object);
        HRESULT hr = DispCallFunc(instance, sizeof(void *) * m->iMeth, m->cc, m->vtReturn, 0, nullptr, nullptr, &object);
        if (FAILED(hr))
            return hr;
        HRESULT answer = DISP_E_MEMBERNOTFOUND;
        if (object.vt == VT_DISPATCH) {
            if (object.pdispVal)
                answer = object.pdispVal->lpVtbl->Invoke(object.pdispVal, DISPID_VALUE, &GUID_NULL, t->Lcid, flags, params, out,
                                                          excep, error);
        } else {
            answer = DISP_E_NOTACOLLECTION;
        }
        VariantClear(&object);
        if (scratch.vt)
            VariantClear(&scratch);
        return answer;
    }
    INVOKE_ARGS *args;
    HRESULT hr = get_invoke_args(m, flags, params, &args, error);
    if (FAILED(hr))
        return hr;
    hr = DispCallFunc(instance, sizeof(void *) * m->iMeth, m->cc, m->vtReturn, args->Count, args->Types, args->Pointers, out);
    release_args(args);
    if (scratch.vt)
        VariantClear(&scratch);
    return hr;
}

static HRESULT STDMETHODCALLTYPE di_get_documentation(ITypeInfo *This, MEMBERID id, BSTR *name, BSTR *doc, DWORD *help_context,
                                                      BSTR *help_file)
{
    METHODDATA *m;
    HRESULT hr = method_of_dispid((DISP_TYPEINFO *)This, id, 0xF, &m);
    if (FAILED(hr))
        return hr;
    if (name) {
        *name = SysAllocString(m->szName);
        if (m->szName && !*name)
            return E_OUTOFMEMORY;
    }
    if (doc)
        *doc = nullptr;
    if (help_context)
        *help_context = 0;
    if (help_file)
        *help_file = nullptr;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE di_get_ref_type_info(ITypeInfo *This, HREFTYPE ref, ITypeInfo **out)
{
    DISP_TYPEINFO *t = (DISP_TYPEINFO *)This;
    if (t->Kind != TKIND_COCLASS || ref != 0)
        return E_FAIL;
    return disp_create(TKIND_INTERFACE, t->Data, t->Lcid, out);
}

static void STDMETHODCALLTYPE di_release_type_attr(ITypeInfo *This, TYPEATTR *attr)
{
    AutFree(attr);
}

static void STDMETHODCALLTYPE di_release_func_desc(ITypeInfo *This, FUNCDESC *fd)
{
    AutFree(fd->lprgelemdescParam);
    AutFree(fd);
}

static void STDMETHODCALLTYPE di_release_var_desc(ITypeInfo *This, VARDESC *vd)
{
}

static const ITypeInfoVtbl g_disp_typeinfo_vtbl = {
    di_query,
    di_add_ref,
    di_release,
    di_get_type_attr,
    (void *)di_not_implemented,
    di_get_func_desc,
    di_get_var_desc,
    di_get_names,
    di_get_ref_type_of_impl_type,
    (void *)di_not_implemented,
    di_get_ids_of_names,
    di_invoke,
    di_get_documentation,
    (void *)di_not_implemented,
    di_get_ref_type_info,
    (void *)di_not_implemented,
    (void *)di_not_implemented,
    (void *)di_not_implemented,
    (void *)di_not_implemented,
    di_release_type_attr,
    di_release_func_desc,
    di_release_var_desc,
};
