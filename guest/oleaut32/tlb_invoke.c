/*
 * calls: DispCallFunc, which calls a function with arguments of the types of variants, and ITypeInfo::Invoke, which
 * finds the function a member id and the kind of call name in a type, turns the arguments of an automation call (in
 * reverse order, some named, some missing) into the arguments of that function, calls it through the virtual table of
 * the object, and returns its result. DispInvoke, DispGetIDsOfNames, DispGetParam and CreateStdDispatch sit on top.
 */
#include "typelib.h"

#define MAX_FORMALS 64

/* ------------------------------------------------------------------ slots */

typedef struct {
    ULONG_PTR Slot[4 * MAX_FORMALS + 16];
    ULONG Count;
} CALL;

static void put_slot(CALL *c, ULONG_PTR value)
{
    if (c->Count < sizeof(c->Slot) / sizeof(c->Slot[0]))
        c->Slot[c->Count++] = value;
}

static void put_u64(CALL *c, ULONGLONG value)
{
    if (sizeof(void *) == 8) {
        put_slot(c, (ULONG_PTR)value);
    } else {
        put_slot(c, (ULONG_PTR)(ULONG)value);
        put_slot(c, (ULONG_PTR)(ULONG)(value >> 32));
    }
}

/* the bytes of a value that is passed inline: on 64 bits as a pointer to a copy when it does not fit a register. */
static void put_block(CALL *c, const void *data, SIZE_T size, void *copy)
{
    if (sizeof(void *) == 8) {
        if (size == 1 || size == 2 || size == 4 || size == 8) {
            ULONGLONG v = 0;
            memcpy(&v, data, size);
            put_slot(c, (ULONG_PTR)v);
        } else {
            memcpy(copy, data, size);
            put_slot(c, (ULONG_PTR)copy);
        }
    } else {
        const ULONG *words = data;
        for (SIZE_T i = 0; i < (size + 3) / 4; i++) {
            ULONG w = 0;
            memcpy(&w, (const BYTE *)data + i * 4, size - i * 4 < 4 ? size - i * 4 : 4);
            put_slot(c, w);
        }
        (void)words;
    }
}

/* a value of vt from a variant's storage into the slots. */
static HRESULT put_value(CALL *c, VARTYPE vt, const VARIANT *v, VARIANT *copy)
{
    if (vt & (VT_BYREF | VT_ARRAY)) {
        put_slot(c, (ULONG_PTR)((vt & VT_BYREF) ? v->byref : (PVOID)v->parray));
        return S_OK;
    }
    switch (vt) {
    case VT_I1:
    case VT_UI1: put_slot(c, v->bVal); break;
    case VT_I2:
    case VT_UI2:
    case VT_BOOL: put_slot(c, v->uiVal); break;
    case VT_I4:
    case VT_UI4:
    case VT_INT:
    case VT_UINT:
    case VT_ERROR:
    case VT_HRESULT: put_slot(c, v->ulVal); break;
    case VT_R4: put_slot(c, *(const ULONG *)&v->fltVal); break;
    case VT_R8:
    case VT_DATE:
    case VT_CY:
    case VT_I8:
    case VT_UI8: put_u64(c, v->ullVal); break;
    case VT_BSTR:
    case VT_UNKNOWN:
    case VT_DISPATCH:
    case VT_PTR:
    case VT_LPSTR:
    case VT_LPWSTR:
    case VT_INT_PTR:
    case VT_UINT_PTR:
    case VT_USERDEFINED:
    case VT_SAFEARRAY:
    case VT_CARRAY: put_slot(c, (ULONG_PTR)v->byref); break;
    case VT_VARIANT: put_block(c, v, sizeof(VARIANT), copy); break;
    case VT_DECIMAL: put_block(c, &v->decVal, sizeof(DECIMAL), copy); break;
    default: return DISP_E_BADVARTYPE;
    }
    return S_OK;
}

/* the value a function returned, in a variant. */
static void take_result(VARTYPE vt, ULONGLONG raw, ULONGLONG fp, VARIANT *out)
{
    memset(out, 0, sizeof(*out));
    out->vt = vt;
    switch (vt) {
    case VT_I1:
    case VT_UI1: out->bVal = (BYTE)raw; break;
    case VT_I2:
    case VT_UI2:
    case VT_BOOL: out->uiVal = (USHORT)raw; break;
    case VT_I4:
    case VT_UI4:
    case VT_INT:
    case VT_UINT:
    case VT_ERROR:
    case VT_HRESULT: out->ulVal = (ULONG)raw; break;
    case VT_R4: {
        ULONG bits = (ULONG)fp;
        memcpy(&out->fltVal, &bits, 4);
        break;
    }
    case VT_R8:
    case VT_DATE: out->ullVal = fp; break;
    case VT_CY:
    case VT_I8:
    case VT_UI8: out->ullVal = raw; break;
    default: out->ullVal = raw; break;
    }
}

static ULONG fp_kind_of(VARTYPE vt)
{
    return vt == VT_R4 ? 1 : (vt == VT_R8 || vt == VT_DATE) ? 2 : 0;
}

WINOLEAUTAPI HRESULT WINAPI DispCallFunc(PVOID instance, ULONG_PTR vtable_offset, CALLCONV callconv, VARTYPE return_vt,
                                         UINT count, VARTYPE *vts, VARIANTARG **args, VARIANT *result)
{
    if (count && (!vts || !args))
        return E_INVALIDARG;
    if (count > MAX_FORMALS)
        return E_INVALIDARG;
    if (callconv != CC_STDCALL && callconv != CC_CDECL)
        return DISP_E_BADCALLEE;
    CALL *call = AutAllocZero(sizeof(CALL) + (SIZE_T)(count + 1) * sizeof(VARIANT));
    if (!call)
        return E_OUTOFMEMORY;
    VARIANT *copies = (VARIANT *)(call + 1);
    void *function;
    if (instance) {
        function = (*(void ***)instance)[vtable_offset / sizeof(void *)];
        put_slot(call, (ULONG_PTR)instance);
    } else {
        function = (void *)vtable_offset;
    }
    HRESULT hr = S_OK;
    if (return_vt == VT_VARIANT || return_vt == VT_DECIMAL) {
        AutFree(call);
        return DISP_E_BADVARTYPE;
    }
    for (UINT i = 0; i < count && SUCCEEDED(hr); i++) {
        if (!args[i]) {
            hr = E_INVALIDARG;
            break;
        }
        hr = put_value(call, vts[i], args[i], &copies[i]);
    }
    if (FAILED(hr)) {
        AutFree(call);
        return hr;
    }
    ULONG kind = return_vt & VT_BYREF ? 0 : fp_kind_of(return_vt);
    ULONGLONG fp = 0;
    ULONGLONG raw = AutCallEx(function, call->Slot, call->Count, kind, &fp);
    if (result) {
        if (return_vt != VT_EMPTY && return_vt != VT_VOID) {
            take_result(return_vt, raw, fp, result);
        } else {
            VariantInit(result);
        }
    }
    AutFree(call);
    return S_OK;
}

/* ------------------------------------------------------------------ types of arguments */

typedef struct {
    VARTYPE Vt;
    ULONG Size;
} FORMAL;

static HRESULT vt_of(TL_TYPE *scope, const TYPEDESC *td, int depth, FORMAL *f);

static HRESULT user_defined(TL_TYPE *scope, HREFTYPE ref, BOOL via_pointer, int depth, FORMAL *f)
{
    ITypeInfo *info;
    HRESULT hr = TlResolveHref(scope, ref & 0xFFFFFF, &info);
    if (FAILED(hr))
        return hr;
    TYPEATTR *attr;
    hr = info->lpVtbl->GetTypeAttr(info, &attr);
    if (FAILED(hr)) {
        info->lpVtbl->Release(info);
        return hr;
    }
    switch (attr->typekind) {
    case TKIND_ENUM:
        f->Vt = via_pointer ? (VT_BYREF | VT_I4) : VT_I4;
        break;
    case TKIND_ALIAS: {
        TYPEDESC alias = attr->tdescAlias;
        /* an alias is followed in the library it belongs to. */
        ITypeLib *lib;
        UINT index;
        hr = info->lpVtbl->GetContainingTypeLib(info, &lib, &index);
        if (SUCCEEDED(hr)) {
            TL_TYPE *t = ((TL_LIB *)lib)->Types[index];
            if (via_pointer) {
                TYPEDESC pointer;
                pointer.vt = VT_PTR;
                pointer.lptdesc = &alias;
                hr = vt_of(t, &pointer, depth + 1, f);
            } else {
                hr = vt_of(t, &alias, depth + 1, f);
            }
            lib->lpVtbl->Release(lib);
        }
        break;
    }
    case TKIND_INTERFACE:
    case TKIND_COCLASS:
        f->Vt = VT_UNKNOWN;
        if (via_pointer)
            break;
        break;
    case TKIND_DISPATCH:
        f->Vt = VT_DISPATCH;
        break;
    case TKIND_RECORD:
    case TKIND_UNION:
        f->Vt = via_pointer ? (VT_BYREF | VT_RECORD) : VT_RECORD;
        f->Size = attr->cbSizeInstance;
        break;
    default:
        hr = DISP_E_BADVARTYPE;
        break;
    }
    info->lpVtbl->ReleaseTypeAttr(info, attr);
    info->lpVtbl->Release(info);
    return hr;
}

static HRESULT vt_of(TL_TYPE *scope, const TYPEDESC *td, int depth, FORMAL *f)
{
    f->Vt = VT_EMPTY;
    f->Size = 0;
    if (depth > 16)
        return DISP_E_BADVARTYPE;
    switch (td->vt) {
    case VT_PTR: {
        const TYPEDESC *inner = td->lptdesc;
        if (inner->vt == VT_USERDEFINED) {
            FORMAL g;
            HRESULT hr = user_defined(scope, inner->hreftype, TRUE, depth + 1, &g);
            if (FAILED(hr))
                return hr;
            /* the pointer to an interface is the interface; to anything else, a reference to it. */
            f->Vt = g.Vt;
            f->Size = g.Size;
            return S_OK;
        }
        FORMAL g;
        HRESULT hr = vt_of(scope, inner, depth + 1, &g);
        if (FAILED(hr))
            return hr;
        f->Vt = g.Vt | VT_BYREF;
        f->Size = g.Size;
        return S_OK;
    }
    case VT_SAFEARRAY: {
        FORMAL g;
        HRESULT hr;
        if (td->lptdesc->vt == VT_PTR || td->lptdesc->vt == VT_USERDEFINED) {
            hr = vt_of(scope, td->lptdesc, depth + 1, &g);
            if (SUCCEEDED(hr) && td->lptdesc->vt == VT_PTR && !(g.Vt & VT_BYREF))
                ;
        } else {
            hr = vt_of(scope, td->lptdesc, depth + 1, &g);
        }
        if (FAILED(hr))
            return hr;
        f->Vt = VT_ARRAY | (g.Vt & VT_TYPEMASK);
        return S_OK;
    }
    case VT_USERDEFINED: return user_defined(scope, td->hreftype, FALSE, depth + 1, f);
    case VT_CARRAY: return DISP_E_BADVARTYPE;
    case VT_HRESULT: f->Vt = VT_ERROR; return S_OK;
    default:
        f->Vt = td->vt;
        return S_OK;
    }
}

/* ------------------------------------------------------------------ the arguments of one call */

typedef struct {
    /* what was converted for the formal, to be given back after the call. */
    VARIANT Temp;
    VARIANT *Source;
    BOOL WriteBack;
    BOOL Owned;
} ARG;

static BOOL is_lcid(const TL_PARAM *p)
{
    return (p->Flags & PARAMFLAG_FLCID) != 0;
}

static BOOL is_retval(const TL_PARAM *p)
{
    return (p->Flags & PARAMFLAG_FRETVAL) != 0;
}

/* the variant an actual argument stands for: a reference to a variant is the variant. */
static const VARIANT *actual_value(const VARIANT *a, VARIANT *scratch)
{
    if ((a->vt & VT_BYREF) && (a->vt & VT_TYPEMASK) == VT_VARIANT && !(a->vt & VT_ARRAY)) {
        if (!a->pvarVal)
            return nullptr;
        a = a->pvarVal;
    }
    return a;
}

static HRESULT fill_exception(IUnknown *instance, EXCEPINFO *excep, HRESULT failure, REFIID iid)
{
    ISupportErrorInfo *support;
    memset(excep, 0, sizeof(*excep));
    excep->scode = failure;
    if (FAILED(instance->lpVtbl->QueryInterface(instance, &IID_ISupportErrorInfo, (void **)&support)))
        return failure;
    HRESULT hr = support->lpVtbl->InterfaceSupportsErrorInfo(support, iid);
    support->lpVtbl->Release(support);
    if (hr != S_OK)
        return failure;
    IErrorInfo *info;
    if (GetErrorInfo(0, &info) != S_OK)
        return failure;
    info->lpVtbl->GetSource(info, &excep->bstrSource);
    info->lpVtbl->GetDescription(info, &excep->bstrDescription);
    info->lpVtbl->GetHelpFile(info, &excep->bstrHelpFile);
    info->lpVtbl->GetHelpContext(info, &excep->dwHelpContext);
    info->lpVtbl->Release(info);
    return DISP_E_EXCEPTION;
}

static HRESULT invoke_function(TL_TYPE *type, TL_FUNC *f, PVOID instance, WORD flags, DISPPARAMS *dp,
                               VARIANT *result, EXCEPINFO *excep, UINT *arg_err)
{
    UINT n = f->ParamCount;
    if (n > MAX_FORMALS)
        return DISP_E_BADPARAMCOUNT;
    if (f->Kind == FUNC_STATIC || f->Kind == FUNC_NONVIRTUAL)
        return DISP_E_BADCALLEE;
    LCID lcid = GetUserDefaultLCID();
    SIZE_T bytes = sizeof(CALL) + (SIZE_T)(n + 2) * (sizeof(ARG) + sizeof(FORMAL) + sizeof(VARIANT) * 2);
    BYTE *memory = AutAllocZero(bytes);
    if (!memory)
        return E_OUTOFMEMORY;
    CALL *call = (CALL *)memory;
    ARG *args = (ARG *)(call + 1);
    FORMAL *formals = (FORMAL *)(args + n + 2);
    VARIANT *copies = (VARIANT *)(formals + n + 2);
    VARIANT *retstore = copies + n + 2;
    HRESULT hr = S_OK;
    UINT visible[MAX_FORMALS], nvisible = 0;
    int retval_index = -1;
    for (UINT i = 0; i < n && SUCCEEDED(hr); i++) {
        hr = vt_of(type, &f->Params[i].Type, 0, &formals[i]);
        if (is_retval(&f->Params[i]) && retval_index < 0)
            retval_index = (int)i;
        else if (!is_lcid(&f->Params[i]))
            visible[nvisible++] = i;
    }
    if (FAILED(hr))
        goto done;

    UINT cargs = dp ? dp->cArgs : 0, cnamed = dp ? dp->cNamedArgs : 0;
    VARIANT *rg = dp ? dp->rgvarg : nullptr;
    if ((cargs && !rg) || cnamed > cargs) {
        hr = DISP_E_BADPARAMCOUNT;
        goto done;
    }
    BOOL put = (flags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) != 0;
    VARIANT *put_value_arg = nullptr;
    UINT first_named = 0;
    if (put) {
        if (!cnamed || dp->rgdispidNamedArgs[0] != DISPID_PROPERTYPUT) {
            hr = DISP_E_PARAMNOTOPTIONAL;
            goto done;
        }
        put_value_arg = &rg[0];
        first_named = 1;
    }
    UINT positional = cargs - cnamed;
    /* the actual for each visible formal. */
    const VARIANT *actual[MAX_FORMALS];
    UINT actual_index[MAX_FORMALS];
    for (UINT i = 0; i < nvisible; i++) {
        actual[i] = nullptr;
        actual_index[i] = 0;
    }
    UINT vararg_count = 0;
    BOOL varargs = f->OptCount == -1 && nvisible > 0;
    UINT fixed_visible = varargs ? nvisible - 1 : nvisible;
    UINT value_slots = nvisible - (put && nvisible ? 1 : 0);
    for (UINT p = 0; p < positional; p++) {
        if (p < (put ? value_slots : fixed_visible)) {
            actual[p] = &rg[cargs - 1 - p];
            actual_index[p] = cargs - 1 - p;
        } else if (varargs && !put) {
            vararg_count++;
        } else {
            hr = DISP_E_BADPARAMCOUNT;
            goto done;
        }
    }
    if (put) {
        if (!nvisible) {
            hr = DISP_E_BADPARAMCOUNT;
            goto done;
        }
        actual[nvisible - 1] = put_value_arg;
        actual_index[nvisible - 1] = 0;
    }
    for (UINT k = first_named; k < cnamed; k++) {
        DISPID id = dp->rgdispidNamedArgs[k];
        if (id < 0 || (UINT)id >= nvisible) {
            if (arg_err)
                *arg_err = k;
            hr = DISP_E_PARAMNOTFOUND;
            goto done;
        }
        actual[id] = &rg[k];
        actual_index[id] = k;
    }

    /* the formals in order, converted. */
    UINT vi = 0;
    VARIANT *vararray_holder = nullptr;
    for (UINT i = 0; i < n && SUCCEEDED(hr); i++) {
        TL_PARAM *prm = &f->Params[i];
        FORMAL *fm = &formals[i];
        ARG *arg = &args[i];
        VARIANT *tmp = &arg->Temp;
        VARTYPE value_vt = fm->Vt & ~VT_BYREF;
        BOOL byref = (fm->Vt & VT_BYREF) != 0;
        if (is_lcid(prm)) {
            VARIANT v;
            memset(&v, 0, sizeof(v));
            v.ulVal = lcid;
            hr = put_value(call, VT_I4, &v, nullptr);
            continue;
        }
        if ((int)i == retval_index) {
            memset(retstore, 0, sizeof(*retstore));
            VARTYPE rv = value_vt;
            VARIANT v;
            memset(&v, 0, sizeof(v));
            v.byref = rv == VT_DECIMAL ? (PVOID)&retstore->decVal : rv == VT_VARIANT ? (PVOID)retstore : (PVOID)&retstore->llVal;
            put_slot(call, (ULONG_PTR)v.byref);
            retstore->vt = rv;
            continue;
        }
        const VARIANT *a = actual[vi];
        UINT index = actual_index[vi];
        BOOL last_vararg = varargs && vi == nvisible - 1;
        vi++;
        if (last_vararg) {
            /* the arguments past the others are one array of variants. */
            SAFEARRAYBOUND bound = {vararg_count, 0};
            SAFEARRAY *array = SafeArrayCreate(VT_VARIANT, 1, &bound);
            if (!array) {
                hr = E_OUTOFMEMORY;
                break;
            }
            for (UINT e = 0; e < vararg_count; e++) {
                LONG at = (LONG)e;
                const VARIANT *src = &rg[cargs - 1 - (fixed_visible + e)];
                VARIANT inner;
                VariantInit(&inner);
                const VARIANT *value = actual_value(src, nullptr);
                if (value) {
                    VariantCopyInd(&inner, value->vt & VT_BYREF ? value : value);
                }
                SafeArrayPutElement(array, &at, &inner);
                VariantClear(&inner);
            }
            tmp->vt = VT_ARRAY | VT_VARIANT;
            tmp->parray = array;
            arg->Owned = TRUE;
            vararray_holder = tmp;
            put_slot(call, (ULONG_PTR)array);
            continue;
        }
        const VARIANT *value = a ? actual_value(a, nullptr) : nullptr;
        if (a && !value) {
            hr = DISP_E_TYPEMISMATCH;
            if (arg_err)
                *arg_err = index;
            break;
        }
        /* an argument left out: the default, a variant that says it is missing, or nothing. */
        if (!a || ((value->vt == VT_ERROR && value->scode == DISP_E_PARAMNOTFOUND) && (prm->Flags & PARAMFLAG_FOPT))) {
            if (prm->Flags & PARAMFLAG_FHASDEFAULT) {
                hr = VariantCopy(tmp, &prm->Default);
                arg->Owned = TRUE;
                value = tmp;
            } else if (prm->Flags & PARAMFLAG_FOPT) {
                if (value_vt == VT_VARIANT) {
                    tmp->vt = VT_ERROR;
                    tmp->scode = DISP_E_PARAMNOTFOUND;
                    value = tmp;
                } else {
                    hr = DISP_E_PARAMNOTOPTIONAL;
                    if (arg_err)
                        *arg_err = index;
                    break;
                }
            } else {
                hr = DISP_E_PARAMNOTOPTIONAL;
                if (arg_err)
                    *arg_err = i;
                break;
            }
            if (FAILED(hr))
                break;
        }
        if (value_vt == VT_VARIANT) {
            VARIANT *target;
            if (byref) {
                /* a variant by reference: the actual itself when it is one, or the copy of its value. */
                if (a && (a->vt == (VT_BYREF | VT_VARIANT)) && a->pvarVal) {
                    target = a->pvarVal;
                } else if (a && a == value && a->vt != (VT_BYREF | VT_VARIANT)) {
                    target = (VARIANT *)a;
                } else {
                    *tmp = *value;
                    target = tmp;
                }
                put_slot(call, (ULONG_PTR)target);
            } else {
                hr = put_value(call, VT_VARIANT, value, &copies[i]);
            }
            continue;
        }
        if (value_vt == VT_RECORD) {
            if (byref) {
                put_slot(call, (ULONG_PTR)value->pvRecord);
            } else {
                put_block(call, value->pvRecord, fm->Size, &copies[i]);
            }
            continue;
        }
        if (value_vt == VT_UNKNOWN || value_vt == VT_DISPATCH) {
            /* an interface pointer: the actual's own, asked for the interface of the formal. */
            VARIANT converted;
            VariantInit(&converted);
            const VARIANT *source = value;
            if (byref) {
                if (a && (a->vt & VT_BYREF) && (a->vt & VT_TYPEMASK) == value_vt) {
                    put_slot(call, (ULONG_PTR)a->byref);
                    continue;
                }
            }
            if ((value->vt & VT_BYREF) && !(value->vt & VT_ARRAY)) {
                VariantCopyInd(&converted, value);
                source = &converted;
            }
            if (source->vt == VT_NULL || source->vt == VT_EMPTY || ((source->vt == VT_UNKNOWN || source->vt == VT_DISPATCH) && !source->punkVal)) {
                tmp->vt = value_vt;
                tmp->punkVal = nullptr;
            } else {
                hr = VariantChangeTypeEx(tmp, source, lcid, 0, value_vt);
                arg->Owned = TRUE;
            }
            VariantClear(&converted);
            if (FAILED(hr)) {
                hr = DISP_E_TYPEMISMATCH;
                if (arg_err)
                    *arg_err = index;
                break;
            }
            if (byref) {
                arg->WriteBack = a && (a->vt & VT_BYREF);
                arg->Source = (VARIANT *)a;
                put_slot(call, (ULONG_PTR)&tmp->punkVal);
            } else {
                put_slot(call, (ULONG_PTR)tmp->punkVal);
            }
            continue;
        }
        if (value_vt & VT_ARRAY) {
            if (byref) {
                if (a && (a->vt & VT_BYREF) && (a->vt & VT_ARRAY)) {
                    put_slot(call, (ULONG_PTR)a->byref);
                } else if (value->vt & VT_ARRAY) {
                    tmp->vt = value->vt;
                    tmp->parray = value->parray;
                    put_slot(call, (ULONG_PTR)&tmp->parray);
                } else {
                    hr = DISP_E_TYPEMISMATCH;
                    if (arg_err)
                        *arg_err = index;
                }
            } else {
                const VARIANT *src = value;
                VARIANT in;
                if ((src->vt & VT_BYREF) && (src->vt & VT_ARRAY)) {
                    in.vt = src->vt & ~VT_BYREF;
                    in.parray = src->pparray ? *src->pparray : nullptr;
                    src = &in;
                }
                if (!(src->vt & VT_ARRAY)) {
                    hr = DISP_E_TYPEMISMATCH;
                    if (arg_err)
                        *arg_err = index;
                } else {
                    put_slot(call, (ULONG_PTR)src->parray);
                }
            }
            continue;
        }
        /* plain values: a pointer to a value of the type, or the value. */
        if (byref) {
            if (a && (a->vt & VT_BYREF) && (a->vt & VT_TYPEMASK) == value_vt && !(a->vt & VT_ARRAY)) {
                put_slot(call, (ULONG_PTR)a->byref);
                continue;
            }
            if (a && (a->vt & VT_BYREF) && (a->vt & VT_TYPEMASK) != VT_VARIANT && !(prm->Flags & PARAMFLAG_FOUT)) {
                /* a reference to another type, only read. */
            }
            const VARIANT *inner = value;
            if (!(value_vt == VT_PTR) && (value->vt != value_vt || (value->vt & VT_BYREF))) {
                VARIANT converted;
                VariantInit(&converted);
                const VARIANT *source = value;
                if (value->vt & VT_BYREF) {
                    hr = VariantCopyInd(&converted, value);
                    source = &converted;
                }
                if (SUCCEEDED(hr))
                    hr = VariantChangeTypeEx(tmp, source, lcid, 0, value_vt);
                VariantClear(&converted);
                arg->Owned = TRUE;
                inner = tmp;
            } else {
                *tmp = *value;
                inner = tmp;
            }
            if (FAILED(hr)) {
                hr = DISP_E_TYPEMISMATCH;
                if (arg_err)
                    *arg_err = index;
                break;
            }
            arg->WriteBack = (prm->Flags & PARAMFLAG_FOUT) && a && ((a->vt & VT_BYREF) != 0);
            arg->Source = (VARIANT *)a;
            put_slot(call, (ULONG_PTR)(value_vt == VT_DECIMAL ? (PVOID)&tmp->decVal : (PVOID)&tmp->llVal));
            (void)inner;
            continue;
        }
        if (value_vt == VT_PTR || value_vt == VT_LPSTR || value_vt == VT_LPWSTR) {
            put_slot(call, (ULONG_PTR)(value->vt & VT_BYREF ? value->byref : (PVOID)value->byref));
            continue;
        }
        {
            VARIANT converted;
            VariantInit(&converted);
            const VARIANT *source = value;
            if (value->vt & VT_BYREF) {
                hr = VariantCopyInd(&converted, value);
                source = &converted;
            }
            if (SUCCEEDED(hr)) {
                if (source->vt == value_vt || (value_vt == VT_INT && source->vt == VT_I4) ||
                    (value_vt == VT_UINT && source->vt == VT_UI4)) {
                    hr = VariantCopy(tmp, source);
                } else {
                    hr = VariantChangeTypeEx(tmp, source, lcid, 0, value_vt);
                }
                arg->Owned = TRUE;
            }
            VariantClear(&converted);
            if (FAILED(hr)) {
                hr = hr == DISP_E_OVERFLOW ? hr : DISP_E_TYPEMISMATCH;
                if (arg_err)
                    *arg_err = index;
                break;
            }
            hr = put_value(call, value_vt, tmp, &copies[i]);
        }
    }
    (void)vararray_holder;
    if (FAILED(hr))
        goto cleanup;

    {
        void *function = (*(void ***)instance)[TlNativeVtOffset(type->Lib, f->VtOffset) / sizeof(void *)];
        /* the function comes before everything: this first. */
        ULONG_PTR slots[sizeof(call->Slot) / sizeof(call->Slot[0])];
        slots[0] = (ULONG_PTR)instance;
        memcpy(slots + 1, call->Slot, call->Count * sizeof(ULONG_PTR));
        VARTYPE rv_vt = VT_EMPTY;
        FORMAL ret_formal;
        ret_formal.Vt = VT_EMPTY;
        ret_formal.Size = 0;
        if (f->Ret.vt != VT_HRESULT && f->Ret.vt != VT_VOID)
            vt_of(type, &f->Ret, 0, &ret_formal);
        rv_vt = ret_formal.Vt;
        ULONGLONG fp = 0;
        ULONGLONG raw = AutCallEx(function, slots, call->Count + 1, fp_kind_of(rv_vt), &fp);
        HRESULT callee = S_OK;
        if (f->Ret.vt == VT_HRESULT)
            callee = (HRESULT)raw;
        if (FAILED(callee)) {
            hr = callee;
            if (excep && callee != DISP_E_EXCEPTION)
                hr = fill_exception((IUnknown *)instance, excep, callee, &type->Guid);
            goto cleanup;
        }
        /* the outputs: references to converted values go back, and the result is taken. */
        for (UINT i = 0; i < n; i++) {
            ARG *arg = &args[i];
            if (arg->WriteBack && arg->Source) {
                VARIANT *dst = arg->Source;
                VARTYPE vt = formals[i].Vt & ~VT_BYREF;
                if ((dst->vt & VT_TYPEMASK) == VT_VARIANT && (dst->vt & VT_BYREF) && dst->pvarVal) {
                    VariantClear(dst->pvarVal);
                    *dst->pvarVal = arg->Temp;
                    arg->Owned = FALSE;
                } else if (dst->vt & VT_BYREF) {
                    (void)vt;
                }
            }
        }
        if (result) {
            VariantInit(result);
            if (retval_index >= 0) {
                if (retstore->vt == VT_VARIANT) {
                    *result = *retstore;
                } else {
                    *result = *retstore;
                    if (retstore->vt == VT_DECIMAL)
                        result->vt = VT_DECIMAL;
                }
                retstore->vt = VT_EMPTY;
            } else if (rv_vt != VT_EMPTY && rv_vt != VT_VOID) {
                take_result(rv_vt & ~VT_BYREF, raw, fp, result);
            }
        } else if (retval_index >= 0) {
            VariantClear(retstore);
        }
    }
cleanup:
    for (UINT i = 0; i < n; i++) {
        if (args[i].Owned && !args[i].WriteBack)
            VariantClear(&args[i].Temp);
        else if (args[i].Owned && args[i].WriteBack)
            ;
    }
    if (FAILED(hr) && retval_index >= 0)
        VariantClear(retstore);
done:
    AutFree(memory);
    return hr;
}

/* ------------------------------------------------------------------ ITypeInfo::Invoke */

static BOOL kind_allowed(INVOKEKIND kind, WORD flags)
{
    return (flags & (WORD)kind) != 0;
}

HRESULT TlInvokeOnType(ITypeInfo *self, TL_TYPE *type, BOOL dual_dispatch_view, PVOID instance, MEMBERID id, WORD flags,
                       DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep, UINT *arg_err)
{
    if (!instance && type->Kind != TKIND_MODULE)
        return E_INVALIDARG;
    if (arg_err)
        *arg_err = 0;
    HRESULT hr = TlLoadMembers(type);
    if (FAILED(hr))
        return hr;
    if (type->Kind == TKIND_DISPATCH && !(type->Flags & TYPEFLAG_FDUAL)) {
        IDispatch *dispatch;
        hr = ((IUnknown *)instance)->lpVtbl->QueryInterface((IUnknown *)instance, &IID_IDispatch, (void **)&dispatch);
        if (FAILED(hr))
            return hr;
        hr = dispatch->lpVtbl->Invoke(dispatch, id, &IID_NULL, GetUserDefaultLCID(), flags, params, result, excep,
                                      arg_err);
        dispatch->lpVtbl->Release(dispatch);
        return hr;
    }
    TL_FUNC *found = nullptr;
    for (UINT i = 0; i < type->FuncCount; i++)
        if (type->Funcs[i].Id == id && kind_allowed(type->Funcs[i].Invoke, flags)) {
            found = &type->Funcs[i];
            break;
        }
    if (found)
        return invoke_function(type, found, instance, flags, params, result, excep, arg_err);
    /* the base: asked with the same call. */
    HREFTYPE ref;
    ITypeInfo2 *info2 = (ITypeInfo2 *)self;
    if (type->Kind == TKIND_MODULE || FAILED(info2->lpVtbl->GetRefTypeOfImplType(info2, 0, &ref)))
        return DISP_E_MEMBERNOTFOUND;
    ITypeInfo *base;
    if (FAILED(info2->lpVtbl->GetRefTypeInfo(info2, ref, &base)))
        return DISP_E_MEMBERNOTFOUND;
    hr = base->lpVtbl->Invoke(base, instance, id, flags, params, result, excep, arg_err);
    base->lpVtbl->Release(base);
    return hr;
}

/* ------------------------------------------------------------------ the dispatch helpers */

WINOLEAUTAPI HRESULT WINAPI DispGetIDsOfNames(ITypeInfo *info, LPOLESTR *names, UINT count, DISPID *ids)
{
    if (!info)
        return E_INVALIDARG;
    return info->lpVtbl->GetIDsOfNames(info, names, count, ids);
}

WINOLEAUTAPI HRESULT WINAPI DispInvoke(PVOID this_, ITypeInfo *info, DISPID id, WORD flags, DISPPARAMS *params,
                                       VARIANT *result, EXCEPINFO *excep, UINT *arg_err)
{
    if (!this_ || !info || !params)
        return E_INVALIDARG;
    return info->lpVtbl->Invoke(info, this_, id, flags, params, result, excep, arg_err);
}

WINOLEAUTAPI HRESULT WINAPI DispGetParam(DISPPARAMS *params, UINT position, VARTYPE vt, VARIANT *value, UINT *arg_err)
{
    if (!params || !value)
        return E_INVALIDARG;
    VariantInit(value);
    const VARIANT *arg = nullptr;
    for (UINT i = 0; i < params->cNamedArgs; i++)
        if (params->rgdispidNamedArgs[i] == (DISPID)position) {
            arg = &params->rgvarg[i];
            break;
        }
    if (!arg) {
        UINT positional = params->cArgs - params->cNamedArgs;
        if (position >= positional)
            return DISP_E_PARAMNOTFOUND;
        arg = &params->rgvarg[params->cArgs - 1 - position];
    }
    HRESULT hr;
    if (arg->vt == vt) {
        hr = VariantCopy(value, arg);
    } else if ((arg->vt & VT_BYREF) || vt != VT_VARIANT) {
        VARIANT in;
        hr = VariantCopyInd(&in, arg);
        if (SUCCEEDED(hr)) {
            hr = vt == in.vt ? VariantCopy(value, &in) : VariantChangeType(value, &in, 0, vt);
            VariantClear(&in);
        }
    } else {
        hr = VariantCopy(value, arg);
    }
    if (FAILED(hr)) {
        if (arg_err)
            *arg_err = position;
        VariantInit(value);
        return hr == DISP_E_OVERFLOW ? hr : DISP_E_TYPEMISMATCH;
    }
    return S_OK;
}

/* ------------------------------------------------------------------ the standard dispatch object */

typedef struct {
    const IUnknownVtbl *lpUnknownVtbl;
    const IDispatchVtbl *lpVtbl;
    LONG Refs;
    IUnknown *Outer;
    PVOID Object;
    ITypeInfo *Info;
} STD_DISPATCH;

static STD_DISPATCH *std_from_unknown(IUnknown *u)
{
    return CONTAINING_RECORD(u, STD_DISPATCH, lpUnknownVtbl);
}

static STD_DISPATCH *std_from_dispatch(IDispatch *d)
{
    return CONTAINING_RECORD(d, STD_DISPATCH, lpVtbl);
}

static HRESULT WINAPI std_unknown_query(IUnknown *self, REFIID iid, void **out)
{
    STD_DISPATCH *s = std_from_unknown(self);
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (IsEqualIID(iid, &IID_IUnknown))
        *out = &s->lpUnknownVtbl;
    else if (IsEqualIID(iid, &IID_IDispatch))
        *out = &s->lpVtbl;
    else
        return E_NOINTERFACE;
    InterlockedIncrement(&s->Refs);
    return S_OK;
}

static ULONG WINAPI std_unknown_addref(IUnknown *self)
{
    return InterlockedIncrement(&std_from_unknown(self)->Refs);
}

static ULONG WINAPI std_unknown_release(IUnknown *self)
{
    STD_DISPATCH *s = std_from_unknown(self);
    LONG refs = InterlockedDecrement(&s->Refs);
    if (!refs) {
        AutRelease(s->Info);
        AutFree(s);
    }
    return refs;
}

static const IUnknownVtbl g_std_unknown_vtbl = {std_unknown_query, std_unknown_addref, std_unknown_release};

static IUnknown *std_controller(STD_DISPATCH *s)
{
    return s->Outer ? s->Outer : (IUnknown *)&s->lpUnknownVtbl;
}

static HRESULT WINAPI std_query(IDispatch *self, REFIID iid, void **out)
{
    IUnknown *c = std_controller(std_from_dispatch(self));
    return c->lpVtbl->QueryInterface(c, iid, out);
}
static ULONG WINAPI std_addref(IDispatch *self)
{
    IUnknown *c = std_controller(std_from_dispatch(self));
    return c->lpVtbl->AddRef(c);
}
static ULONG WINAPI std_release(IDispatch *self)
{
    IUnknown *c = std_controller(std_from_dispatch(self));
    return c->lpVtbl->Release(c);
}

static HRESULT WINAPI std_get_type_info_count(IDispatch *self, UINT *count)
{
    if (!count)
        return E_POINTER;
    *count = 1;
    return S_OK;
}

static HRESULT WINAPI std_get_type_info(IDispatch *self, UINT index, LCID lcid, ITypeInfo **info)
{
    STD_DISPATCH *s = std_from_dispatch(self);
    if (!info)
        return E_POINTER;
    *info = nullptr;
    if (index)
        return DISP_E_BADINDEX;
    s->Info->lpVtbl->AddRef(s->Info);
    *info = s->Info;
    return S_OK;
}

static HRESULT WINAPI std_get_ids_of_names(IDispatch *self, REFIID riid, LPOLESTR *names, UINT count, LCID lcid,
                                           DISPID *ids)
{
    if (!IsEqualIID(riid, &IID_NULL))
        return DISP_E_UNKNOWNINTERFACE;
    return DispGetIDsOfNames(std_from_dispatch(self)->Info, names, count, ids);
}

static HRESULT WINAPI std_invoke(IDispatch *self, DISPID id, REFIID riid, LCID lcid, WORD flags, DISPPARAMS *params,
                                 VARIANT *result, EXCEPINFO *excep, UINT *arg_err)
{
    STD_DISPATCH *s = std_from_dispatch(self);
    if (!IsEqualIID(riid, &IID_NULL))
        return DISP_E_UNKNOWNINTERFACE;
    return DispInvoke(s->Object, s->Info, id, flags, params, result, excep, arg_err);
}

static const IDispatchVtbl g_std_dispatch_vtbl = {std_query,         std_addref,       std_release,
                                                  std_get_type_info_count, std_get_type_info, std_get_ids_of_names,
                                                  std_invoke};

WINOLEAUTAPI HRESULT WINAPI CreateStdDispatch(IUnknown *outer, PVOID object, ITypeInfo *info, IUnknown **unknown)
{
    if (!object || !info || !unknown)
        return E_INVALIDARG;
    *unknown = nullptr;
    STD_DISPATCH *s = AutAllocZero(sizeof(*s));
    if (!s)
        return E_OUTOFMEMORY;
    s->lpUnknownVtbl = &g_std_unknown_vtbl;
    s->lpVtbl = &g_std_dispatch_vtbl;
    s->Refs = 1;
    s->Outer = outer;
    s->Object = object;
    s->Info = info;
    info->lpVtbl->AddRef(info);
    *unknown = (IUnknown *)&s->lpUnknownVtbl;
    return S_OK;
}
