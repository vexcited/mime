/*
 * DispCallFunc, and ITypeInfo::Invoke: find the function for a member id and call kind, map the automation arguments,
 * which come reversed, partly named and partly missing, onto its parameters, call it through the object's vtable and
 * return its result. DispInvoke, DispGetIDsOfNames, DispGetParam and CreateStdDispatch sit on top.
 */
#include "typelib.h"

#define MAX_FORMALS 64

#ifndef DISP_E_NOTCOLLECTION
#define DISP_E_NOTCOLLECTION ((HRESULT)0x80020011L)
#endif

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

/* on 64-bit, a value that does not fit a register is passed as a pointer to a copy. */
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

/* ------------------------------------------------------------------ parameter types */

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
        /* resolve the alias within its own library. */
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
            /* a pointer to an interface is the interface itself; to anything else, a reference. */
            f->Vt = g.Vt;
            f->Size = g.Size;
            return S_OK;
        }
        FORMAL g;
        HRESULT hr = vt_of(scope, inner, depth + 1, &g);
        if (FAILED(hr))
            return hr;
        /* no variant type can express a reference to a reference. */
        if (g.Vt & VT_BYREF)
            return DISP_E_BADVARTYPE;
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

/* ------------------------------------------------------------------ argument mapping */

/* the lcid and retval parameters are supplied by the call, not the caller. */
static BOOL from_caller(const TL_PARAM *p)
{
    return !(p->Flags & (PARAMFLAG_FLCID | PARAMFLAG_FRETVAL));
}

static UINT caller_params(const TL_FUNC *f)
{
    UINT n = 0;
    for (USHORT i = 0; i < f->ParamCount; i++)
        n += from_caller(&f->Params[i]);
    return n;
}

/*
 * checks in the order windows does: a restricted function is invisible to a nonnegative member id, a vararg function
 * takes no named arguments, and too many arguments may target a collection property. DISP_E_NOTCOLLECTION means the
 * call goes to the object the returned getter yields.
 */
static HRESULT judge_arguments(const TL_TYPE *type, const TL_FUNC *f, WORD flags, const DISPPARAMS *params,
                               const TL_FUNC **getter)
{
    *getter = nullptr;
    UINT v = caller_params(f);
    BOOL hidden = f->Id >= 0 && (f->Flags & FUNCFLAG_FRESTRICTED);
    if (f->OptCount == -1) {
        if (params->cNamedArgs && !(params->cNamedArgs == 1 && params->rgdispidNamedArgs[0] == DISPID_PROPERTYPUT))
            return DISP_E_NONAMEDARGS;
    } else if (params->cArgs > v) {
        if ((flags & DISPATCH_PROPERTYGET) && !v) {
            *getter = f;
        } else if ((flags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) && !(flags & DISPATCH_PROPERTYGET)) {
            for (UINT i = 0; i < type->FuncCount; i++) {
                const TL_FUNC *g = &type->Funcs[i];
                if (g->Id != f->Id || g->Invoke != INVOKE_PROPERTYGET)
                    continue;
                UINT gv = caller_params(g);
                if (!gv || gv == params->cArgs - 1)
                    *getter = g;
                break;
            }
        }
        if (!*getter)
            return DISP_E_BADPARAMCOUNT;
        return hidden ? DISP_E_MEMBERNOTFOUND : DISP_E_NOTCOLLECTION;
    }
    return hidden ? DISP_E_MEMBERNOTFOUND : S_OK;
}

/*
 * the rgvarg index for a parameter: the argument named for its position, else the one at its position counted from
 * the end, unless that one is named.
 */
static int find_argument(const DISPPARAMS *params, int position)
{
    for (UINT k = 0; k < params->cNamedArgs; k++)
        if (params->rgdispidNamedArgs[k] == position)
            return (int)k;
    int index = (int)params->cArgs - position - 1;
    return index >= (int)params->cNamedArgs ? index : -1;
}

/* one argument as the function takes it. */
typedef struct {
    VARTYPE Vt;
    /* the value, in the variant field Vt names. */
    VARIANT Pass;
    /* what a by reference parameter points at when the argument is not a reference of the right kind. */
    VARIANT Box;
    VARTYPE BoxVt;
    /* Box holds a copy cleared after the call, as a full variant when it stands for one. */
    BOOL BoxOwned, BoxIsVariant;
    /* a converted value and a copy of the default, both cleared after the call. */
    VARIANT Own;
    VARIANT Default;
} SLOT;

static void release_slot(SLOT *slot)
{
    VariantClear(&slot->Own);
    VariantClear(&slot->Default);
    if (slot->BoxOwned) {
        if (!slot->BoxIsVariant)
            slot->Box.vt = slot->BoxVt;
        VariantClear(&slot->Box);
    }
}

/* follows a VT_VARIANT reference to its target. */
static const VARIANT *target_of(const VARIANT *arg)
{
    return arg->vt == (VT_BYREF | VT_VARIANT) ? arg->pvarVal : arg;
}

static void set_missing_error(VARIANT *v)
{
    VariantInit(v);
    v->vt = VT_ERROR;
    v->scode = DISP_E_PARAMNOTFOUND;
}

/* a NULL BSTR is copied as an empty string. */
static HRESULT box_copy(SLOT *slot, const VARIANT *value, VARTYPE as)
{
    HRESULT hr = VariantCopy(&slot->Box, value);
    if (FAILED(hr))
        return hr;
    if (slot->Box.vt == VT_BSTR && !slot->Box.bstrVal && !(value->vt & VT_BYREF)) {
        slot->Box.bstrVal = SysAllocString(L"");
        if (!slot->Box.bstrVal)
            return E_OUTOFMEMORY;
    }
    slot->BoxOwned = TRUE;
    slot->BoxVt = as;
    slot->BoxIsVariant = as == VT_VARIANT;
    return S_OK;
}

/*
 * a value already of the parameter type is passed as is, others are converted. A by reference parameter gets the
 * argument's address when it is a reference of the same kind, or a box with a converted copy when the argument is
 * not a reference. A reference of another kind does not fit.
 */
static HRESULT fill_slot(const TL_LIB *lib, const FORMAL *formal, const VARIANT *arg, SLOT *slot)
{
    VARTYPE vt = formal->Vt;
    slot->Vt = vt;
    if (vt == VT_VARIANT) {
        slot->Pass = *arg;
        return S_OK;
    }
    if (vt == (VT_BYREF | VT_VARIANT)) {
        if (arg->vt == vt && arg->pvarVal) {
            slot->Pass.byref = arg->pvarVal;
            return S_OK;
        }
        HRESULT hr = box_copy(slot, arg, VT_VARIANT);
        slot->Pass.byref = &slot->Box;
        return hr;
    }
    BOOL by_ref = (vt & VT_BYREF) != 0;
    VARTYPE base = vt & ~VT_BYREF;
    if (by_ref && (arg->vt & VT_BYREF)) {
        if (arg->vt != vt)
            return DISP_E_TYPEMISMATCH;
        slot->Pass.byref = arg->byref;
        return S_OK;
    }
    const VARIANT *src = target_of(arg);
    if (!src)
        return DISP_E_TYPEMISMATCH;
    if (base & VT_ARRAY) {
        if (!(src->vt & VT_ARRAY) || (src->vt & VT_TYPEMASK) != (base & VT_TYPEMASK))
            return DISP_E_TYPEMISMATCH;
        SAFEARRAY *array = (src->vt & VT_BYREF) ? *src->pparray : src->parray;
        if (!by_ref) {
            slot->Pass.parray = array;
            return S_OK;
        }
        slot->Box.vt = base;
        slot->Box.parray = array;
        slot->Pass.byref = &slot->Box.byref;
        return S_OK;
    }
    if (src->vt == (VT_BYREF | VT_VARIANT))
        return E_INVALIDARG;
    if (base == VT_RECORD) {
        if (src->vt != VT_RECORD)
            return DISP_E_TYPEMISMATCH;
        slot->Pass.byref = src->pvRecord;
        return S_OK;
    }
    if (by_ref) {
        VARIANT converted;
        VariantInit(&converted);
        const VARIANT *value = src;
        if (src->vt != base) {
            HRESULT hr = VariantChangeTypeEx(&converted, src, LOCALE_USER_DEFAULT, 0, base);
            if (FAILED(hr))
                return hr;
            value = &converted;
        }
        HRESULT hr = box_copy(slot, value, base);
        VariantClear(&converted);
        slot->Pass.byref = &slot->Box.byref;
        return hr;
    }
    const VARIANT *value = src;
    if (src->vt != base) {
        HRESULT hr = VariantChangeTypeEx(&slot->Own, src, LOCALE_USER_DEFAULT, VARIANT_ALPHABOOL, base);
        if (FAILED(hr))
            return hr;
        value = &slot->Own;
    }
    if (base == VT_DECIMAL)
        slot->Pass = *value;
    else
        memcpy(&slot->Pass.byref, &value->byref, sizeof(value->llVal));
    return S_OK;
}

/* the arguments from a position on as a safe array of variants in source order, for a vararg function's last parameter. */
static HRESULT collect_rest(const DISPPARAMS *params, int position, BYTE *used, UINT *matched, SAFEARRAY **out)
{
    int first = (int)params->cArgs - 1 - position;
    int count = first - (int)params->cNamedArgs + 1;
    if (count < 0)
        count = 0;
    SAFEARRAYBOUND bound = {(ULONG)count, 0};
    SAFEARRAY *array = SafeArrayCreate(VT_VARIANT, 1, &bound);
    if (!array)
        return E_OUTOFMEMORY;
    for (int e = 0; e < count; e++) {
        int index = first - e;
        LONG at = e;
        const VARIANT *value = target_of(&params->rgvarg[index]);
        HRESULT hr = value ? SafeArrayPutElement(array, &at, (void *)value) : DISP_E_TYPEMISMATCH;
        if (FAILED(hr)) {
            SafeArrayDestroy(array);
            return hr;
        }
        if (!used[index]) {
            used[index] = 1;
            (*matched)++;
        }
    }
    *out = array;
    return S_OK;
}

/* ------------------------------------------------------------------ calling */

/* reads back a value the function wrote through a pointer. */
static HRESULT variant_from_storage(VARTYPE vt, const void *storage, VARIANT *out)
{
    memset(out, 0, sizeof(*out));
    if (vt == VT_VARIANT) {
        *out = *(const VARIANT *)storage;
        return S_OK;
    }
    if (vt == VT_DECIMAL) {
        out->decVal = *(const DECIMAL *)storage;
        out->vt = VT_DECIMAL;
        return S_OK;
    }
    if (vt == VT_HRESULT)
        vt = VT_ERROR;
    out->vt = vt;
    if (vt & (VT_BYREF | VT_ARRAY)) {
        out->byref = *(void *const *)storage;
        return S_OK;
    }
    switch (vt) {
    case VT_I1: out->cVal = *(const CHAR *)storage; break;
    case VT_UI1: out->bVal = *(const BYTE *)storage; break;
    case VT_I2:
    case VT_UI2:
    case VT_BOOL: out->uiVal = *(const USHORT *)storage; break;
    case VT_I4:
    case VT_UI4:
    case VT_INT:
    case VT_UINT:
    case VT_ERROR:
    case VT_R4: out->ulVal = *(const ULONG *)storage; break;
    case VT_I8:
    case VT_UI8:
    case VT_R8:
    case VT_DATE:
    case VT_CY: out->ullVal = *(const ULONGLONG *)storage; break;
    case VT_BSTR:
    case VT_DISPATCH:
    case VT_UNKNOWN: out->byref = *(void *const *)storage; break;
    default: out->vt = VT_EMPTY; return DISP_E_BADVARTYPE;
    }
    return S_OK;
}

static HRESULT exception_from_failure(EXCEPINFO *excep, HRESULT failure)
{
    if (!excep)
        return failure;
    memset(excep, 0, sizeof(*excep));
    excep->scode = failure;
    IErrorInfo *info;
    if (GetErrorInfo(0, &info) == S_OK) {
        info->lpVtbl->GetSource(info, &excep->bstrSource);
        info->lpVtbl->GetDescription(info, &excep->bstrDescription);
        info->lpVtbl->GetHelpFile(info, &excep->bstrHelpFile);
        info->lpVtbl->GetHelpContext(info, &excep->dwHelpContext);
        info->lpVtbl->Release(info);
    }
    return DISP_E_EXCEPTION;
}

/* the object's vtable slot, or a module function. */
static HRESULT call_target(ITypeInfo *self, const TL_TYPE *type, const TL_FUNC *f, PVOID instance, PVOID *object,
                           ULONG_PTR *where)
{
    if (f->Kind == FUNC_STATIC || type->Kind == TKIND_MODULE) {
        PVOID address;
        HRESULT hr = self->lpVtbl->AddressOfMember(self, f->Id, f->Invoke, &address);
        if (FAILED(hr))
            return hr;
        *object = nullptr;
        *where = (ULONG_PTR)address;
        return S_OK;
    }
    if (f->Kind != FUNC_PUREVIRTUAL && f->Kind != FUNC_VIRTUAL)
        return DISP_E_BADCALLEE;
    *object = instance;
    *where = (ULONG_PTR)(USHORT)TlNativeVtOffset(type->Lib, f->VtOffset);
    return S_OK;
}

/*
 * each parameter takes its argument or, without one, the value its flags and type give it. The result is the retval
 * parameter or the return value.
 */
static HRESULT call_function(ITypeInfo *self, TL_TYPE *type, const TL_FUNC *f, PVOID instance, WORD flags,
                             const DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep, UINT *arg_err)
{
    TL_LIB *lib = type->Lib;
    UINT count = f->ParamCount;
    if (count > MAX_FORMALS)
        return DISP_E_BADPARAMCOUNT;
    SIZE_T bytes = (SIZE_T)(count + 1) * (sizeof(SLOT) + sizeof(VARTYPE) + sizeof(VARIANT *) + sizeof(FORMAL)) + params->cArgs;
    BYTE *memory = AutAllocZero(bytes);
    if (!memory)
        return E_OUTOFMEMORY;
    SLOT *slots = (SLOT *)memory;
    VARTYPE *vts = (VARTYPE *)(slots + count + 1);
    VARIANT **pointers = (VARIANT **)(vts + count + 1);
    FORMAL *formals = (FORMAL *)(pointers + count + 1);
    BYTE *used = (BYTE *)(formals + count + 1);
    UINT matched = 0;
    VARIANT retval_box;
    VariantInit(&retval_box);
    const TL_PARAM *retval = nullptr;
    FORMAL retval_formal = {0, 0};
    HRESULT hr = S_OK;
    /* the last caller parameter takes a property put value, or the remaining varargs. */
    int value_last = -1;
    for (UINT i = 0; i < count; i++)
        if (from_caller(&f->Params[i]))
            value_last = (int)i;
    int value_position = f->Invoke == INVOKE_PROPERTYPUT || f->Invoke == INVOKE_PROPERTYPUTREF ? value_last : -1;
    if (f->OptCount == -1 && params->cNamedArgs && value_position < 0)
        hr = DISP_E_PARAMNOTFOUND;
    for (UINT i = 0; i < count && SUCCEEDED(hr); i++) {
        const TL_PARAM *p = &f->Params[i];
        SLOT *slot = &slots[i];
        FORMAL *formal = &formals[i];
        VariantInit(&slot->Own);
        VariantInit(&slot->Default);
        hr = vt_of(type, &p->Type, 0, formal);
        if (FAILED(hr))
            break;
        slot->Vt = formal->Vt;
        if (p->Flags & PARAMFLAG_FLCID) {
            slot->Pass.lVal = (LONG)lib->Lcid;
            continue;
        }
        if (p->Flags & PARAMFLAG_FRETVAL) {
            retval = p;
            if (p->Type.vt == VT_PTR)
                hr = vt_of(type, p->Type.lptdesc, 0, &retval_formal);
            else
                hr = DISP_E_BADVARTYPE;
            slot->Vt = VT_BYREF | VT_I4;
            slot->Pass.byref = &retval_box;
            continue;
        }
        if (f->OptCount == -1 && (int)i == value_last && (formal->Vt & VT_ARRAY)) {
            SAFEARRAY *rest;
            hr = collect_rest(params, (int)i, used, &matched, &rest);
            if (SUCCEEDED(hr)) {
                slot->Own.vt = formal->Vt & ~VT_BYREF;
                slot->Own.parray = rest;
                slot->Pass.parray = rest;
                if (formal->Vt & VT_BYREF) {
                    slot->Box.parray = rest;
                    slot->Pass.byref = &slot->Box.byref;
                }
            }
            continue;
        }
        int index;
        if ((int)i == value_position) {
            /* a put value can only come as the DISPID_PROPERTYPUT named argument. */
            if (!params->cNamedArgs || params->rgdispidNamedArgs[0] != DISPID_PROPERTYPUT) {
                hr = DISP_E_PARAMNOTFOUND;
                continue;
            }
            index = 0;
        } else {
            index = find_argument(params, (int)i);
        }
        const VARIANT *arg = index >= 0 ? &params->rgvarg[index] : nullptr;
        BOOL optional = (p->Flags & PARAMFLAG_FOPT) != 0;
        BOOL has_default = (p->Flags & PARAMFLAG_FHASDEFAULT) != 0;
        BOOL is_variant = formal->Vt == VT_VARIANT || formal->Vt == (VT_BYREF | VT_VARIANT);
        if (arg && !used[index]) {
            used[index] = 1;
            matched++;
        }
        /* DISP_E_PARAMNOTFOUND marks a missing argument, accepted when the parameter can do without. */
        if (arg && arg->vt == VT_ERROR && arg->scode == (SCODE)DISP_E_PARAMNOTFOUND) {
            if (optional && (!is_variant || has_default)) {
                arg = nullptr;
            } else if (!optional && !is_variant) {
                hr = DISP_E_PARAMNOTOPTIONAL;
                if (arg_err)
                    *arg_err = (UINT)index;
                continue;
            }
        }
        if (arg) {
            hr = fill_slot(lib, formal, arg, slot);
            if (FAILED(hr) && arg_err)
                *arg_err = (UINT)index;
            continue;
        }
        VARIANT missing;
        if (f->OptCount > 0 && i >= (UINT)count - (UINT)f->OptCount) {
            set_missing_error(&missing);
            hr = fill_slot(lib, formal, &missing, slot);
        } else if (!optional) {
            hr = DISP_E_BADPARAMCOUNT;
        } else if (has_default) {
            hr = VariantCopy(&slot->Default, &p->Default);
            if (SUCCEEDED(hr))
                hr = fill_slot(lib, formal, &slot->Default, slot);
        } else if (is_variant) {
            set_missing_error(&missing);
            hr = fill_slot(lib, formal, &missing, slot);
        } else {
            memset(&slot->Pass, 0, sizeof(slot->Pass));
            if (formal->Vt & VT_BYREF)
                slot->Pass.byref = &slot->Box.byref;
        }
    }
    /* leftover arguments next to matched ones mean the call does not fit this function. */
    if (SUCCEEDED(hr) && f->OptCount != -1 && matched && matched < params->cArgs)
        hr = DISP_E_PARAMNOTFOUND;
    VARTYPE return_vt = VT_EMPTY;
    FORMAL return_formal = {0, 0};
    if (SUCCEEDED(hr) && f->Ret.vt != VT_VOID && f->Ret.vt != VT_EMPTY) {
        if (f->Ret.vt == VT_HRESULT) {
            return_vt = VT_ERROR;
        } else {
            hr = vt_of(type, &f->Ret, 0, &return_formal);
            return_vt = return_formal.Vt;
        }
    }
    PVOID object = nullptr;
    ULONG_PTR where = 0;
    if (SUCCEEDED(hr))
        hr = call_target(self, type, f, instance, &object, &where);
    VARIANT raw;
    VariantInit(&raw);
    BOOL failed_inside = FALSE;
    if (SUCCEEDED(hr)) {
        for (UINT i = 0; i < count; i++) {
            vts[i] = slots[i].Vt;
            pointers[i] = &slots[i].Pass;
        }
        /* keep only error info the function itself sets. */
        SetErrorInfo(0, nullptr);
        hr = DispCallFunc(object, where, f->Conv, return_vt, count, vts, pointers, &raw);
    }
    VARIANT value;
    VariantInit(&value);
    if (SUCCEEDED(hr)) {
        if (return_vt == VT_ERROR) {
            hr = raw.scode;
            failed_inside = FAILED(hr);
        } else if (return_vt != VT_EMPTY) {
            value = raw;
        }
        if (SUCCEEDED(hr) && retval)
            hr = variant_from_storage(retval_formal.Vt, &retval_box, &value);
        else if (FAILED(hr))
            VariantInit(&value);
    }
    for (UINT i = 0; i < count; i++)
        release_slot(&slots[i]);
    AutFree(memory);
    if (FAILED(hr)) {
        VariantClear(&value);
        return failed_inside ? exception_from_failure(excep, hr) : hr;
    }
    if (result)
        *result = value;
    else
        VariantClear(&value);
    return hr;
}

/* calls the object a property returns, with the arguments the property could not take. */
static HRESULT forward_to_default(ITypeInfo *self, TL_TYPE *type, const TL_FUNC *getter, PVOID instance, WORD flags,
                                  const DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep, UINT *arg_err)
{
    DISPPARAMS getter_params = {nullptr, nullptr, 0, 0};
    DISPPARAMS forwarded = *params;
    DISPID put = DISPID_PROPERTYPUT;
    if (!(flags & DISPATCH_PROPERTYGET)) {
        UINT v = caller_params(getter);
        if (v) {
            getter_params.rgvarg = params->rgvarg + 1;
            getter_params.cArgs = params->cArgs - 1;
        }
        if (v == params->cArgs - 1) {
            forwarded.cArgs = 1;
            forwarded.cNamedArgs = 1;
            forwarded.rgdispidNamedArgs = &put;
        }
    }
    VARIANT held;
    VariantInit(&held);
    HRESULT hr = call_function(self, type, getter, instance, DISPATCH_PROPERTYGET, &getter_params, &held, excep, arg_err);
    if (FAILED(hr))
        return hr;
    if (held.vt != VT_DISPATCH) {
        VariantClear(&held);
        return DISP_E_NOTCOLLECTION;
    }
    IDispatch *object = held.pdispVal;
    if (!object)
        return DISP_E_MEMBERNOTFOUND;
    hr = object->lpVtbl->Invoke(object, 0, &IID_NULL, type->Lib->Lcid, flags, &forwarded, result, excep, arg_err);
    object->lpVtbl->Release(object);
    return hr;
}

static HRESULT invoke_function(ITypeInfo *self, TL_TYPE *type, const TL_FUNC *f, PVOID instance, WORD flags,
                               DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep, UINT *arg_err)
{
    if (!params)
        return E_INVALIDARG;
    const TL_FUNC *getter;
    HRESULT hr = judge_arguments(type, f, flags, params, &getter);
    if (hr == DISP_E_NOTCOLLECTION)
        return forward_to_default(self, type, getter, instance, flags, params, result, excep, arg_err);
    if (FAILED(hr))
        return hr;
    return call_function(self, type, f, instance, flags, params, result, excep, arg_err);
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
    /* only call kind flags are allowed, and at least one. */
    if (!flags || (flags & ~(WORD)(DISPATCH_METHOD | DISPATCH_PROPERTYGET | DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)))
        return E_INVALIDARG;
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
        return invoke_function(self, type, found, instance, flags, params, result, excep, arg_err);
    /* not found here: pass the same call to the base. */
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
