/*
 * variants: initialization, clearing and copying. type conversions are
 * not implemented yet.
 */
#include "oleaut32_private.h"

/* whether vt names a type a variant may hold, by value or with VT_BYREF or VT_ARRAY. */
static BOOL valid_type(VARTYPE vt)
{
    VARTYPE base = vt & VT_TYPEMASK;
    if (vt & (VT_VECTOR | VT_RESERVED))
        return FALSE;
    if ((vt & VT_ARRAY) && (base == VT_EMPTY || base == VT_NULL))
        return FALSE;
    if (base == VT_VARIANT && !(vt & (VT_BYREF | VT_ARRAY)))
        return FALSE;
    return base <= VT_DECIMAL || (base >= VT_I1 && base <= VT_UINT) || base == VT_RECORD || base == VT_INT_PTR ||
           base == VT_UINT_PTR;
}

WINOLEAUTAPI void WINAPI VariantInit(VARIANTARG *variant)
{
    if (variant)
        variant->vt = VT_EMPTY;
}

WINOLEAUTAPI HRESULT WINAPI VariantClear(VARIANTARG *variant)
{
    if (!variant)
        return E_INVALIDARG;
    VARTYPE vt = variant->vt;
    if (!valid_type(vt))
        return DISP_E_BADVARTYPE;
    if (!(vt & VT_BYREF)) {
        if (vt & VT_ARRAY) {
            HRESULT hr = SafeArrayDestroy(variant->parray);
            if (FAILED(hr))
                return hr;
        } else if (vt == VT_BSTR) {
            SysFreeString(variant->bstrVal);
        } else if (vt == VT_UNKNOWN || vt == VT_DISPATCH) {
            AutRelease(variant->punkVal);
        } else if (vt == VT_RECORD) {
            AutRelease(variant->pRecInfo);
        }
    }
    variant->vt = VT_EMPTY;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VariantCopy(VARIANTARG *dst, const VARIANTARG *src)
{
    if (!dst || !src)
        return E_INVALIDARG;
    if (dst == src)
        return S_OK;
    if (!valid_type(src->vt))
        return DISP_E_BADVARTYPE;
    HRESULT hr = VariantClear(dst);
    if (FAILED(hr))
        return hr;
    VARIANT copy = *src;
    if (!(src->vt & VT_BYREF)) {
        if (src->vt & VT_ARRAY) {
            hr = src->parray ? SafeArrayCopy(src->parray, &copy.parray) : S_OK;
            if (FAILED(hr))
                return hr;
        } else if (src->vt == VT_BSTR) {
            if (src->bstrVal) {
                copy.bstrVal = SysAllocStringByteLen((LPCSTR)src->bstrVal, SysStringByteLen(src->bstrVal));
                if (!copy.bstrVal)
                    return E_OUTOFMEMORY;
            }
        } else if (src->vt == VT_UNKNOWN || src->vt == VT_DISPATCH) {
            AutAddRef(src->punkVal);
        } else if (src->vt == VT_RECORD) {
            /* copying record data needs IRecordInfo::RecordCopy, not supported yet. */
            return E_NOTIMPL;
        }
    }
    *dst = copy;
    return S_OK;
}

/* copies the variant a VT_BYREF variant points to, or the variant itself. */
WINOLEAUTAPI HRESULT WINAPI VariantCopyInd(VARIANT *dst, const VARIANTARG *src)
{
    if (!dst || !src)
        return E_INVALIDARG;
    if (!(src->vt & VT_BYREF))
        return VariantCopy(dst, src);
    VARTYPE vt = src->vt & ~VT_BYREF;
    VARIANT value;
    value.vt = vt;
    value.wReserved1 = value.wReserved2 = value.wReserved3 = 0;
    value.llVal = 0;
    if (vt == VT_VARIANT)
        return VariantCopyInd(dst, src->pvarVal);
    if (!valid_type(vt) || !src->byref)
        return DISP_E_BADVARTYPE;
    switch (vt & VT_TYPEMASK) {
    case VT_I1:
    case VT_UI1:
        value.bVal = *(BYTE *)src->byref;
        break;
    case VT_I2:
    case VT_UI2:
    case VT_BOOL:
        value.iVal = *(SHORT *)src->byref;
        break;
    case VT_I4:
    case VT_UI4:
    case VT_R4:
    case VT_INT:
    case VT_UINT:
    case VT_ERROR:
        value.lVal = *(LONG *)src->byref;
        break;
    case VT_DECIMAL:
        value.decVal = *(DECIMAL *)src->byref;
        value.vt = vt;
        break;
    default:
        value.llVal = *(LONGLONG *)src->byref;
        break;
    }
    if (vt & VT_ARRAY)
        value.parray = *(SAFEARRAY **)src->byref;
    /* the value is borrowed; VariantCopy makes the owned copy. */
    VARIANT temp;
    temp.vt = VT_EMPTY;
    HRESULT hr = VariantCopy(&temp, &value);
    if (FAILED(hr))
        return hr;
    hr = VariantClear(dst);
    if (FAILED(hr)) {
        VariantClear(&temp);
        return hr;
    }
    *dst = temp;
    return S_OK;
}
