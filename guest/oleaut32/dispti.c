/*
 * CreateDispTypeInfo: type information for an automation object made of the method tables of the program. The tables
 * stay with the program and are read in place. What comes back is a coclass with one interface, and that interface is
 * the one with the functions, laid out as virtual with the method at iMeth times the size of a pointer.
 */
#include "oleaut32_private.h"

typedef struct {
    const ITypeInfoVtbl *Vtbl;
    LONG Refs;
    LCID Lcid;
    TYPEKIND Kind;
    const INTERFACEDATA *Data;
} DISP_INFO;

static const ITypeInfoVtbl g_disp_info_vtbl;

#define ANY_KIND 0xF
#define PUT_KINDS (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)

static HRESULT new_info(const INTERFACEDATA *data, LCID lcid, TYPEKIND kind, ITypeInfo **out)
{
    DISP_INFO *info = AutAlloc(sizeof(*info));
    if (!info)
        return E_OUTOFMEMORY;
    info->Vtbl = &g_disp_info_vtbl;
    info->Refs = 1;
    info->Lcid = lcid;
    info->Kind = kind;
    info->Data = data;
    *out = (ITypeInfo *)info;
    return S_OK;
}

/* the first method of the table with this member id and one of the kinds in the mask, or -1. */
static int find_method(const DISP_INFO *info, DISPID id, UINT mask)
{
    for (UINT i = 0; i < info->Data->cMembers; i++) {
        const METHODDATA *m = &info->Data->pmethdata[i];
        if (m->dispid == id && (m->wFlags & mask))
            return (int)i;
    }
    return -1;
}

static const METHODDATA *method_at(const DISP_INFO *info, int index)
{
    return &info->Data->pmethdata[index];
}

/* ------------------------------------------------------------------ IUnknown */

static HRESULT STDMETHODCALLTYPE di_query(ITypeInfo *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITypeInfo)) {
        InterlockedIncrement(&((DISP_INFO *)This)->Refs);
        *object = This;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE di_addref(ITypeInfo *This)
{
    return InterlockedIncrement(&((DISP_INFO *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE di_release(ITypeInfo *This)
{
    LONG refs = InterlockedDecrement(&((DISP_INFO *)This)->Refs);
    if (!refs)
        AutFree(This);
    return refs;
}

/* ------------------------------------------------------------------ descriptions */

static HRESULT STDMETHODCALLTYPE di_get_type_attr(ITypeInfo *This, TYPEATTR **attr)
{
    const DISP_INFO *info = (DISP_INFO *)This;
    TYPEATTR *a = AutAllocZero(sizeof(*a));
    if (!a)
        return E_OUTOFMEMORY;
    a->typekind = info->Kind;
    a->lcid = info->Lcid;
    a->cbAlignment = 2;
    a->cbSizeVft = 0xFFFF;
    a->cbSizeInstance = 0xFFFF;
    a->memidConstructor = MEMBERID_NIL;
    a->memidDestructor = MEMBERID_NIL;
    if (info->Kind == TKIND_INTERFACE)
        a->cFuncs = (WORD)info->Data->cMembers;
    else
        a->cImplTypes = 1;
    *attr = a;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE di_get_type_comp(ITypeInfo *This, ITypeComp **comp)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE di_get_impl_type_flags(ITypeInfo *This, UINT index, INT *flags)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE di_get_dll_entry(ITypeInfo *This, MEMBERID id, INVOKEKIND kind, BSTR *dll, BSTR *name,
                                                  WORD *ordinal)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE di_address_of_member(ITypeInfo *This, MEMBERID id, INVOKEKIND kind, PVOID *address)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE di_create_instance(ITypeInfo *This, IUnknown *outer, REFIID riid, PVOID *object)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE di_get_mops(ITypeInfo *This, MEMBERID id, BSTR *mops)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE di_get_containing_type_lib(ITypeInfo *This, ITypeLib **lib, UINT *index)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE di_get_func_desc(ITypeInfo *This, UINT index, FUNCDESC **desc)
{
    const DISP_INFO *info = (DISP_INFO *)This;
    if (info->Kind != TKIND_INTERFACE)
        return E_FAIL;
    if (index >= info->Data->cMembers)
        return DISP_E_MEMBERNOTFOUND;
    const METHODDATA *m = method_at(info, (int)index);
    INVOKEKIND kind;
    switch (m->wFlags) {
    case DISPATCH_METHOD:
        kind = INVOKE_FUNC;
        break;
    case DISPATCH_PROPERTYGET:
        kind = INVOKE_PROPERTYGET;
        break;
    case DISPATCH_PROPERTYPUT:
        kind = INVOKE_PROPERTYPUT;
        break;
    case DISPATCH_PROPERTYPUTREF:
        kind = INVOKE_PROPERTYPUTREF;
        break;
    default:
        return E_FAIL;
    }
    FUNCDESC *f = AutAllocZero(sizeof(*f));
    if (!f)
        return E_OUTOFMEMORY;
    if (m->cArgs) {
        f->lprgelemdescParam = AutAllocZero(m->cArgs * sizeof(ELEMDESC));
        if (!f->lprgelemdescParam) {
            AutFree(f);
            return E_OUTOFMEMORY;
        }
        for (UINT i = 0; i < m->cArgs; i++)
            f->lprgelemdescParam[i].tdesc.vt = m->ppdata[i].vt;
    }
    f->memid = m->dispid;
    f->funckind = FUNC_VIRTUAL;
    f->invkind = kind;
    f->callconv = m->cc;
    f->cParams = (SHORT)m->cArgs;
    f->oVft = (SHORT)(sizeof(void *) * m->iMeth);
    f->elemdescFunc.tdesc.vt = m->vtReturn;
    *desc = f;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE di_get_var_desc(ITypeInfo *This, UINT index, VARDESC **desc)
{
    return DISP_E_MEMBERNOTFOUND;
}

static HRESULT STDMETHODCALLTYPE di_get_names(ITypeInfo *This, MEMBERID id, BSTR *names, UINT max, UINT *count)
{
    const DISP_INFO *info = (DISP_INFO *)This;
    if (info->Kind != TKIND_INTERFACE || !max) {
        *count = 0;
        return S_OK;
    }
    int index = find_method(info, id, ANY_KIND);
    if (index < 0)
        return DISP_E_MEMBERNOTFOUND;
    const METHODDATA *m = method_at(info, index);
    UINT n = m->cArgs + 1 < max ? m->cArgs + 1 : max;
    memset(names, 0, n * sizeof(BSTR));
    for (UINT i = 0; i < n; i++) {
        const WCHAR *source = i ? m->ppdata[i - 1].szName : m->szName;
        names[i] = source ? SysAllocString(source) : nullptr;
        if (source && !names[i]) {
            while (i--) {
                SysFreeString(names[i]);
                names[i] = nullptr;
            }
            return E_OUTOFMEMORY;
        }
    }
    *count = n;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE di_get_ref_type_of_impl_type(ITypeInfo *This, UINT index, HREFTYPE *ref)
{
    if (((DISP_INFO *)This)->Kind != TKIND_COCLASS)
        return E_UNEXPECTED;
    *ref = 0;
    return S_OK;
}

/* the flags that make two names the same in the language of the object. */
static DWORD name_flags(LCID lcid)
{
    DWORD flags = NORM_IGNORECASE | NORM_IGNORENONSPACE;
    switch (PRIMARYLANGID(LANGIDFROMLCID(lcid))) {
    case LANG_JAPANESE:
        flags |= NORM_IGNOREKANATYPE;
        [[fallthrough]];
    case LANG_CHINESE:
    case LANG_KOREAN:
        flags |= NORM_IGNOREWIDTH;
        break;
    }
    return flags;
}

static BOOL same_name(LCID lcid, DWORD flags, const WCHAR *a, const WCHAR *b)
{
    return a && b && CompareStringW(lcid, flags, a, -1, b, -1) == CSTR_EQUAL;
}

static HRESULT STDMETHODCALLTYPE di_get_ids_of_names(ITypeInfo *This, LPOLESTR *names, UINT count, MEMBERID *ids)
{
    const DISP_INFO *info = (DISP_INFO *)This;
    if (!count)
        return E_INVALIDARG;
    DWORD flags = name_flags(info->Lcid);
    const METHODDATA *found = nullptr;
    for (UINT i = 0; i < info->Data->cMembers && !found; i++) {
        const METHODDATA *m = &info->Data->pmethdata[i];
        if (same_name(info->Lcid, flags, names[0], m->szName))
            found = m;
    }
    if (!found) {
        memset(ids, 0xFF, count * sizeof(MEMBERID));
        return DISP_E_UNKNOWNNAME;
    }
    ids[0] = found->dispid;
    HRESULT hr = S_OK;
    for (UINT n = 1; n < count; n++) {
        ids[n] = -1;
        for (UINT p = 0; p < found->cArgs; p++) {
            if (same_name(info->Lcid, flags, names[n], found->ppdata[p].szName)) {
                ids[n] = (MEMBERID)p;
                break;
            }
        }
        if (ids[n] == -1)
            hr = DISP_E_UNKNOWNNAME;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE di_get_documentation(ITypeInfo *This, MEMBERID id, BSTR *name, BSTR *doc,
                                                      DWORD *help_context, BSTR *help_file)
{
    const DISP_INFO *info = (DISP_INFO *)This;
    int index = find_method(info, id, ANY_KIND);
    if (index < 0)
        return DISP_E_MEMBERNOTFOUND;
    if (name) {
        const WCHAR *source = method_at(info, index)->szName;
        *name = source ? SysAllocString(source) : nullptr;
        if (source && !*name)
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
    const DISP_INFO *info = (DISP_INFO *)This;
    if (info->Kind != TKIND_COCLASS || ref != 0)
        return E_FAIL;
    return new_info(info->Data, info->Lcid, TKIND_INTERFACE, out);
}

static void STDMETHODCALLTYPE di_release_type_attr(ITypeInfo *This, TYPEATTR *attr)
{
    AutFree(attr);
}

static void STDMETHODCALLTYPE di_release_func_desc(ITypeInfo *This, FUNCDESC *desc)
{
    AutFree(desc->lprgelemdescParam);
    AutFree(desc);
}

static void STDMETHODCALLTYPE di_release_var_desc(ITypeInfo *This, VARDESC *desc)
{
}

/* ------------------------------------------------------------------ Invoke */

#define SMALL_CALL 8

/* the arguments of one call, as DispCallFunc takes them: a type and a pointer to a variant for each. */
typedef struct {
    UINT Count;
    VARTYPE *Types;
    VARIANT **Values;
    VARIANT *Converted;
    VARTYPE SmallTypes[SMALL_CALL];
    VARIANT *SmallValues[SMALL_CALL];
    VARIANT SmallConverted[SMALL_CALL];
} CALL_ARGS;

static void args_release(CALL_ARGS *args)
{
    for (UINT i = 0; i < args->Count; i++)
        VariantClear(&args->Converted[i]);
    if (args->Types != args->SmallTypes) {
        AutFree(args->Types);
        AutFree(args->Values);
        AutFree(args->Converted);
    }
}

static BOOL args_init(CALL_ARGS *args, UINT count)
{
    args->Count = count;
    if (count <= SMALL_CALL) {
        args->Types = args->SmallTypes;
        args->Values = args->SmallValues;
        args->Converted = args->SmallConverted;
    } else {
        args->Types = AutAlloc(count * sizeof(VARTYPE));
        args->Values = AutAlloc(count * sizeof(VARIANT *));
        args->Converted = AutAlloc(count * sizeof(VARIANT));
        if (!args->Types || !args->Values || !args->Converted) {
            AutFree(args->Types);
            AutFree(args->Values);
            AutFree(args->Converted);
            args->Count = 0;
            return FALSE;
        }
    }
    for (UINT i = 0; i < count; i++) {
        args->Types[i] = VT_EMPTY;
        args->Values[i] = nullptr;
        VariantInit(&args->Converted[i]);
    }
    return TRUE;
}

/* a type of parameter that a variant is converted to before the call. */
static BOOL converts_to(VARTYPE vt)
{
    return (vt >= VT_I2 && vt <= VT_BOOL) || vt == VT_UNKNOWN || vt == VT_DECIMAL || (vt >= VT_I1 && vt <= VT_UINT);
}

static HRESULT args_build(CALL_ARGS *args, const METHODDATA *m, WORD flags, const DISPPARAMS *params, UINT *arg_err)
{
    for (UINT i = 0; i < m->cArgs; i++) {
        const VARIANT *source;
        VARTYPE vt = m->ppdata[i].vt;
        UINT position;
        BOOL named = FALSE;
        if (i == m->cArgs - 1 && (flags & PUT_KINDS)) {
            if (!params->cNamedArgs || params->rgdispidNamedArgs[0] != DISPID_PROPERTYPUT)
                return DISP_E_PARAMNOTOPTIONAL;
            position = 0;
        } else {
            position = params->cArgs - i - 1;
            for (UINT n = 0; n < params->cNamedArgs; n++) {
                if (params->rgdispidNamedArgs[n] == (DISPID)i) {
                    position = n;
                    named = TRUE;
                    break;
                }
            }
            if (!named && position < params->cNamedArgs)
                return DISP_E_PARAMNOTFOUND;
        }
        source = &params->rgvarg[position];
        args->Types[i] = vt;

        if (vt & (VT_BYREF | VT_ARRAY)) {
            if (source->vt != vt)
                return DISP_E_TYPEMISMATCH;
            args->Values[i] = (VARIANT *)source;
        } else if (vt == VT_VARIANT) {
            args->Values[i] = (VARIANT *)source;
        } else if (converts_to(vt)) {
            HRESULT hr = VariantChangeTypeEx(&args->Converted[i], (VARIANT *)source, 0x400, 0, vt);
            if (FAILED(hr)) {
                *arg_err = position;
                if (hr == DISP_E_TYPEMISMATCH && source->vt == VT_ERROR && source->scode == DISP_E_PARAMNOTFOUND)
                    hr = DISP_E_PARAMNOTOPTIONAL;
                return hr;
            }
            args->Values[i] = &args->Converted[i];
        } else {
            return E_INVALIDARG;
        }
    }
    return S_OK;
}

/* the property is read first and the object it gives is what the call goes on to. */
static HRESULT through_getter(const DISP_INFO *info, void *instance, const METHODDATA *getter, WORD flags,
                              DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep, UINT *arg_err)
{
    VARIANT value;
    VariantInit(&value);
    HRESULT hr = DispCallFunc(instance, sizeof(void *) * getter->iMeth, getter->cc, getter->vtReturn, 0, nullptr,
                              nullptr, &value);
    if (FAILED(hr))
        return hr;
    if (value.vt != VT_DISPATCH)
        hr = DISP_E_NOTACOLLECTION;
    else if (!value.pdispVal)
        hr = DISP_E_MEMBERNOTFOUND;
    else
        hr = value.pdispVal->lpVtbl->Invoke(value.pdispVal, DISPID_VALUE, &IID_NULL, info->Lcid, flags, params, result,
                                            excep, arg_err);
    VariantClear(&value);
    return hr;
}

static HRESULT STDMETHODCALLTYPE di_invoke(ITypeInfo *This, PVOID instance, MEMBERID id, WORD flags,
                                           DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep, UINT *arg_err)
{
    const DISP_INFO *info = (DISP_INFO *)This;
    DISPPARAMS none = {nullptr, nullptr, 0, 0};
    VARIANT scratch;
    UINT scratch_error;
    if (!params)
        params = &none;
    if (flags & 0xFFF0)
        return E_INVALIDARG;
    VariantInit(&scratch);
    BOOL own_result = !result;
    if (own_result)
        result = &scratch;
    if (!arg_err)
        arg_err = &scratch_error;

    HRESULT hr;
    int index = find_method(info, id, flags);
    const METHODDATA *getter = nullptr;

    if (index < 0) {
        hr = DISP_E_MEMBERNOTFOUND;
        if (flags & PUT_KINDS) {
            UINT other = flags == DISPATCH_PROPERTYPUTREF ? DISPATCH_PROPERTYPUT : DISPATCH_PROPERTYPUTREF;
            int g = find_method(info, id, DISPATCH_PROPERTYGET);
            if (find_method(info, id, other) < 0 && g >= 0 && !method_at(info, g)->cArgs)
                getter = method_at(info, g);
        }
    } else {
        const METHODDATA *m = method_at(info, index);
        hr = S_OK;
        if (params->cArgs != m->cArgs) {
            hr = DISP_E_BADPARAMCOUNT;
            if (params->cArgs > m->cArgs && !(flags & DISPATCH_PROPERTYGET)) {
                int g = find_method(info, id, DISPATCH_PROPERTYGET);
                if ((flags & PUT_KINDS) && (m->wFlags & PUT_KINDS) && m->cArgs == 1 && g >= 0 &&
                    !method_at(info, g)->cArgs)
                    getter = method_at(info, g);
            } else if ((flags & DISPATCH_PROPERTYGET) && (m->wFlags & DISPATCH_PROPERTYGET) && !m->cArgs) {
                getter = m;
            }
        }
        if (getter)
            hr = S_OK;
        else if (hr == S_OK) {
            CALL_ARGS args;
            if (!args_init(&args, m->cArgs))
                hr = E_OUTOFMEMORY;
            else {
                hr = args_build(&args, m, flags, params, arg_err);
                if (SUCCEEDED(hr))
                    hr = DispCallFunc(instance, sizeof(void *) * m->iMeth, m->cc, m->vtReturn, m->cArgs, args.Types,
                                      args.Values, result);
                args_release(&args);
            }
        }
    }
    if (getter)
        hr = through_getter(info, instance, getter, flags, params, result, excep, arg_err);
    if (own_result)
        VariantClear(&scratch);
    return hr;
}

static const ITypeInfoVtbl g_disp_info_vtbl = {
    di_query, di_addref, di_release, di_get_type_attr, di_get_type_comp, di_get_func_desc, di_get_var_desc,
    di_get_names, di_get_ref_type_of_impl_type, di_get_impl_type_flags, di_get_ids_of_names, di_invoke,
    di_get_documentation, di_get_dll_entry, di_get_ref_type_info, di_address_of_member, di_create_instance,
    di_get_mops, di_get_containing_type_lib, di_release_type_attr, di_release_func_desc, di_release_var_desc,
};

WINOLEAUTAPI HRESULT WINAPI CreateDispTypeInfo(INTERFACEDATA *data, LCID lcid, ITypeInfo **info)
{
    if (!data || !info)
        return E_INVALIDARG;
    return new_info(data, lcid, TKIND_COCLASS, info);
}
