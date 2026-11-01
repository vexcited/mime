/*
 * the variables a class makes public: the compiled class reads and writes them through these, with the address of the variable
 * as the first argument.
 */
#include "vb.h"

VB_STD LONG vb_GetMem1(BYTE *src, BYTE *dst)
{
    *dst = *src;
    return 0;
}

VB_STD LONG vb_PutMem1(BYTE *dst, BYTE value)
{
    *dst = value;
    return 0;
}

VB_STD LONG vb_GetMem8(LONG *src, LONG *dst)
{
    dst[0] = src[0];
    dst[1] = src[1];
    return 0;
}

VB_STD LONG vb_PutMem8(LONG *dst, LONG low, LONG high)
{
    dst[0] = low;
    dst[1] = high;
    return 0;
}

/* a string comes out as a copy with the same bytes. */
VB_STD LONG vb_GetMemStr(BSTR *src, BSTR *dst)
{
    if (!*src) {
        *dst = nullptr;
        return 0;
    }
    *dst = SysAllocStringByteLen((const char *)*src, SysStringByteLen(*src));
    if (!*dst) {
        VbRaise(VBERR_OUT_OF_MEMORY);
    }
    return 0;
}

VB_STD LONG vb_PutMemStr(BSTR *dst, BSTR value)
{
    BSTR copy = value ? SysAllocStringByteLen((const char *)value, SysStringByteLen(value)) : nullptr;
    if (value && !copy)
        VbRaise(VBERR_OUT_OF_MEMORY);
    SysFreeString(*dst);
    *dst = copy;
    return 0;
}

VB_STD LONG vb_GetMemVar(VARIANT *src, VARIANT *dst)
{
    HRESULT hr = VariantCopy(dst, src);
    if (FAILED(hr))
        VbRaiseHr(hr);
    return 0;
}

VB_STD LONG vb_PutMemVar(VARIANT *dst, VARIANT *value)
{
    HRESULT hr = VbCopyInd(dst, value);
    if (FAILED(hr))
        VbRaiseHr(hr);
    return 0;
}

/* Set of a variant takes an object and keeps it, as a dispatch or unknown variant. */
VB_STD LONG vb_SetMemVar(VARIANT *dst, VARIANT *value)
{
    VARTYPE type = V_VT(value) & ~VT_BYREF;
    if (type != VT_DISPATCH && type != VT_UNKNOWN)
        VbRaise(VBERR_TYPE_MISMATCH);
    IUnknown *object = V_VT(value) & VT_BYREF ? *value->ppunkVal : V_UNKNOWN(value);
    if (object)
        object->lpVtbl->AddRef(object);
    VariantClear(dst);
    V_VT(dst) = type;
    V_UNKNOWN(dst) = object;
    return 0;
}

VB_STD LONG vb_GetMemObj(IUnknown **src, IUnknown **dst)
{
    *dst = *src;
    if (*src)
        (*src)->lpVtbl->AddRef(*src);
    return 0;
}

VB_STD LONG vb_SetMemObj(IUnknown **dst, IUnknown *object)
{
    if (object)
        object->lpVtbl->AddRef(object);
    IUnknown *old = *dst;
    *dst = object;
    if (old)
        old->lpVtbl->Release(old);
    return 0;
}

/* a member that is an object of a class, made when it is first read: New in the declaration. */
VB_STD LONG vb_GetMemNewObj(const void *info, LONG flags, IUnknown **slot, IUnknown **out)
{
    if (!*slot)
        VbNewObject(info, (void **)slot);
    *out = *slot;
    (*slot)->lpVtbl->AddRef(*slot);
    return 0;
}

VB_STD LONG vb_SetMemNewObj(const void *info, LONG flags, IUnknown **slot, IUnknown *object) { return vb_SetMemObj(slot, object); }

VB_STD LONG vb_GetMemEvent(LONG a, LONG b, IUnknown **slot, IUnknown **out) { return vb_GetMemObj(slot, out); }
VB_STD LONG vb_PutMemEvent(LONG a, LONG b, IUnknown **slot, IUnknown *object) { return vb_SetMemObj(slot, object); }
