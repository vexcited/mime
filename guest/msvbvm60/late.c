/*
 * late binding: a call, a read or an assignment on an object by the name of a member, or by its dispatch id when the
 * compiler knows it. the arguments sit on the stack as variants, the last of the call first, which is the order of the
 * arguments of Invoke.
 */
#include "vb.h"

#define LCID_USER 0x0400

/* the runtime error a failed call of Invoke stands for. */
static LONG error_of_invoke(HRESULT hr, const EXCEPINFO *info)
{
    switch ((ULONG)hr) {
    case 0x80020003:
    case 0x80020004:
    case 0x80020006: return VBERR_NO_SUCH_MEMBER;
    case 0x80020005: return VBERR_TYPE_MISMATCH;
    case 0x80020007: return 458;
    case 0x80020008: return VBERR_TYPE_MISMATCH;
    case 0x8002000A: return VBERR_OVERFLOW;
    case 0x8002000B: return VBERR_SUBSCRIPT;
    case 0x8002000E: return 450;
    case 0x8002000F: return 449;
    case 0x80020012: return VBERR_DIVISION_BY_ZERO;
    case 0x80020009: {
        ULONG scode = (ULONG)(info->scode ? info->scode : info->wCode);
        if ((scode & 0xFFFF0000) == 0x800A0000 && (scode & 0xFFFF))
            return (LONG)(scode & 0xFFFF);
        return VBERR_APPLICATION_DEFINED;
    }
    case 0x80004002: return VBERR_NO_SUCH_MEMBER;
    }
    return VbErrorOfHresult(hr);
}

/* the failed call is the error, with the description the object gave. */
static void raise_invoke_error(HRESULT hr, EXCEPINFO *info)
{
    LONG number = error_of_invoke(hr, info);
    VbThread *t = VbThreadState();
    SysFreeString(t->err_description);
    SysFreeString(t->err_source);
    t->err_description = nullptr;
    t->err_source = nullptr;
    if (hr == DISP_E_EXCEPTION) {
        if (info->pfnDeferredFillIn)
            info->pfnDeferredFillIn(info);
        t->err_description = info->bstrDescription;
        t->err_source = info->bstrSource;
        SysFreeString(info->bstrHelpFile);
    }
    VbRaiseKeep(number);
}

static IDispatch *object_of(IDispatch *obj)
{
    if (!obj)
        VbRaise(91);
    return obj;
}

static DISPID dispid_of(IDispatch *obj, LPOLESTR name)
{
    DISPID id;
    object_of(obj);
    HRESULT hr = obj->lpVtbl->GetIDsOfNames(obj, &IID_NULL, &name, 1, LCID_USER, &id);
    if (FAILED(hr))
        raise_invoke_error(hr, &(EXCEPINFO){});
    return id;
}

static void invoke(IDispatch *obj, DISPID id, WORD flags, VARIANT *args, UINT count, VARIANT *result)
{
    DISPPARAMS params = {};
    DISPID named = DISPID_PROPERTYPUT;
    EXCEPINFO info = {};
    UINT bad = 0;
    params.rgvarg = count ? args : nullptr;
    params.cArgs = count;
    if (flags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) {
        params.rgdispidNamedArgs = &named;
        params.cNamedArgs = 1;
    }
    object_of(obj);
    HRESULT hr = obj->lpVtbl->Invoke(obj, id, &IID_NULL, LCID_USER, flags, &params, result, &info, &bad);
    if (FAILED(hr))
        raise_invoke_error(hr, &info);
}

/* the object a variant holds, which is a dispatch interface or a reference to one. */
static IDispatch *object_in(VARIANT *v)
{
    if (!v)
        VbRaise(91);
    VARTYPE vt = V_VT(v) & ~VT_BYREF;
    if (vt == VT_DISPATCH)
        return V_VT(v) & VT_BYREF ? *(IDispatch **)v->byref : v->pdispVal;
    if (vt == VT_VARIANT && (V_VT(v) & VT_BYREF))
        return object_in(v->pvarVal);
    VbRaise(VBERR_OBJECT_REQUIRED);
}

/* the arguments pushed after the count. */
#define VARARGS(count_param) ((VARIANT *)((LONG *)&(count_param) + 1))

VB_C void vb___vbaLateMemCall(IDispatch *obj, LPOLESTR name, SHORT count, ...)
{
    invoke(obj, dispid_of(obj, name), DISPATCH_METHOD, VARARGS(count), (UINT)(USHORT)count, nullptr);
}

VB_C VARIANT *vb___vbaLateMemCallLd(VARIANT *result, IDispatch *obj, LPOLESTR name, SHORT count, ...)
{
    invoke(obj, dispid_of(obj, name), DISPATCH_METHOD | DISPATCH_PROPERTYGET, VARARGS(count), (UINT)(USHORT)count, result);
    return result;
}

VB_C void vb___vbaLateMemCallSt(IDispatch *obj, LPOLESTR name, SHORT count, ...)
{
    invoke(obj, dispid_of(obj, name), DISPATCH_PROPERTYPUT, VARARGS(count), (UINT)(USHORT)count + 1, nullptr);
}

VB_STD void vb___vbaLateMemSt(IDispatch *obj, LPOLESTR name, VARIANT value)
{
    invoke(obj, dispid_of(obj, name), DISPATCH_PROPERTYPUT, &value, 1, nullptr);
}

VB_C void vb___vbaLateMemStAd(IDispatch *obj, LPOLESTR name, SHORT count, ...)
{
    invoke(obj, dispid_of(obj, name), DISPATCH_PROPERTYPUTREF, VARARGS(count), (UINT)(USHORT)count + 1, nullptr);
}

VB_C void vb___vbaLateIdCall(IDispatch *obj, DISPID id, SHORT count, ...)
{
    invoke(obj, id, DISPATCH_METHOD, VARARGS(count), (UINT)(USHORT)count, nullptr);
}

VB_C VARIANT *vb___vbaLateIdCallLd(VARIANT *result, IDispatch *obj, DISPID id, SHORT count, ...)
{
    invoke(obj, id, DISPATCH_METHOD | DISPATCH_PROPERTYGET, VARARGS(count), (UINT)(USHORT)count, result);
    return result;
}

VB_STD void vb___vbaLateIdSt(IDispatch *obj, DISPID id, VARIANT value)
{
    invoke(obj, id, DISPATCH_PROPERTYPUT, &value, 1, nullptr);
}

VB_C void vb___vbaLateIdStAd(IDispatch *obj, DISPID id, SHORT count, ...)
{
    invoke(obj, id, DISPATCH_PROPERTYPUTREF, VARARGS(count), (UINT)(USHORT)count + 1, nullptr);
}

/* the forms that take the object as a variant. */
VB_C VARIANT *vb___vbaVarLateMemCallLd(VARIANT *result, VARIANT *holder, LPOLESTR name, LONG count, ...)
{
    IDispatch *obj = object_in(holder);
    invoke(obj, dispid_of(obj, name), DISPATCH_METHOD | DISPATCH_PROPERTYGET, (VARIANT *)(&count + 1), (UINT)count, result);
    return result;
}

VB_C VARIANT *vb___vbaVarLateMemCallLdRf(VARIANT *result, VARIANT *holder, LPOLESTR name, LONG count, ...)
{
    IDispatch *obj = object_in(holder);
    invoke(obj, dispid_of(obj, name), DISPATCH_METHOD | DISPATCH_PROPERTYGET, (VARIANT *)(&count + 1), (UINT)count, result);
    return result;
}

VB_STD void vb___vbaVarLateMemSt(VARIANT *holder, LPOLESTR name, VARIANT value)
{
    IDispatch *obj = object_in(holder);
    invoke(obj, dispid_of(obj, name), DISPATCH_PROPERTYPUT, &value, 1, nullptr);
}

VB_C void vb___vbaVarLateMemStAd(VARIANT *holder, LPOLESTR name, LONG count, ...)
{
    IDispatch *obj = object_in(holder);
    invoke(obj, dispid_of(obj, name), DISPATCH_PROPERTYPUTREF, (VARIANT *)(&count + 1), (UINT)count + 1, nullptr);
}
