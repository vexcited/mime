/*
 * variants: initialization, clearing, copying and the change of type.
 */
#include "oleaut32_private.h"
#include <mime/win32/winnls.h>

/* whether vt names a type a variant may hold, by value or with VT_BYREF or VT_ARRAY. */
BOOL AutValidVariantType(VARTYPE vt)
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

#define valid_type AutValidVariantType

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
            IRecordInfo *info = variant->pRecInfo;
            if (info) {
                if (variant->pvRecord)
                    info->lpVtbl->RecordDestroy(info, variant->pvRecord);
                info->lpVtbl->Release(info);
            }
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
            IRecordInfo *info = src->pRecInfo;
            if (!info)
                return E_INVALIDARG;
            copy.pvRecord = nullptr;
            if (src->pvRecord) {
                copy.pvRecord = info->lpVtbl->RecordCreate(info);
                if (!copy.pvRecord)
                    return E_OUTOFMEMORY;
                hr = info->lpVtbl->RecordCopy(info, src->pvRecord, copy.pvRecord);
                if (FAILED(hr)) {
                    info->lpVtbl->RecordDestroy(info, copy.pvRecord);
                    return hr;
                }
            }
            info->lpVtbl->AddRef(info);
        }
    }
    *dst = copy;
    return S_OK;
}

/* the size in bytes of a value of vt, for the types that VT_BYREF can point at. */
static SIZE_T value_size(VARTYPE vt)
{
    switch (vt & VT_TYPEMASK) {
    case VT_I1:
    case VT_UI1: return 1;
    case VT_I2:
    case VT_UI2:
    case VT_BOOL: return 2;
    case VT_I4:
    case VT_UI4:
    case VT_R4:
    case VT_INT:
    case VT_UINT:
    case VT_ERROR: return 4;
    case VT_DECIMAL: return sizeof(DECIMAL);
    case VT_VARIANT: return sizeof(VARIANT);
    case VT_I8:
    case VT_UI8:
    case VT_R8:
    case VT_CY:
    case VT_DATE: return 8;
    default: return sizeof(void *);
    }
}

/*
 * the value a VT_BYREF variant points to, as a variant that borrows it: nothing in it is owned. FALSE for a type that
 * cannot be pointed at.
 */
static HRESULT deref_variant(const VARIANT *source, VARIANT *value)
{
    VARIANT in = *source;
    const VARIANT *src = &in;
    VARTYPE vt = src->vt & ~VT_BYREF;
    if (vt == VT_VARIANT) {
        if (!src->pvarVal)
            return DISP_E_TYPEMISMATCH;
        *value = *src->pvarVal;
        if (value->vt & VT_BYREF) {
            if ((value->vt & VT_TYPEMASK) == VT_VARIANT)
                return E_INVALIDARG;
            return deref_variant(value, value);
        }
        return S_OK;
    }
    if (!valid_type(vt) || !src->byref)
        return src->byref ? DISP_E_BADVARTYPE : DISP_E_TYPEMISMATCH;
    memset(value, 0, sizeof(*value));
    value->vt = vt;
    if (vt & VT_ARRAY) {
        value->parray = *src->pparray;
        return S_OK;
    }
    if ((vt & VT_TYPEMASK) == VT_RECORD)
        return DISP_E_TYPEMISMATCH;
    if (vt == VT_DECIMAL) {
        DECIMAL dec = *(DECIMAL *)src->byref;
        value->decVal = dec;
        value->vt = vt;
        return S_OK;
    }
    memcpy(&value->llVal, src->byref, value_size(vt));
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
    if (vt == VT_VARIANT)
        return src->pvarVal ? VariantCopyInd(dst, src->pvarVal) : E_INVALIDARG;
    if ((vt & VT_TYPEMASK) == VT_RECORD) {
        if (!valid_type(vt) || !src->pvRecord || !src->pRecInfo)
            return E_INVALIDARG;
        value = *src;
        value.vt = vt;
    } else {
        HRESULT hr = deref_variant(src, &value);
        if (FAILED(hr))
            return hr == DISP_E_TYPEMISMATCH ? DISP_E_BADVARTYPE : hr;
    }
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

/* ------------------------------------------------------------------ the change of type */

static BOOL is_integer_vt(VARTYPE vt)
{
    return vt == VT_I1 || vt == VT_UI1 || vt == VT_I2 || vt == VT_UI2 || vt == VT_BOOL || vt == VT_I4 ||
           vt == VT_UI4 || vt == VT_INT || vt == VT_UINT || vt == VT_I8 || vt == VT_UI8;
}

static BOOL is_number_vt(VARTYPE vt)
{
    return is_integer_vt(vt) || vt == VT_R4 || vt == VT_R8 || vt == VT_DATE || vt == VT_CY || vt == VT_DECIMAL;
}

/*
 * integers of the same size, or a boolean, change to another by their bits, without a check: -1 as an unsigned short
 * is 65535. Everything else about numbers goes through the checked conversions.
 */
static BOOL copy_integer_bits(const VARIANT *s, VARTYPE dvt, VARIANT *d)
{
    VARTYPE svt = s->vt;
    if (!is_integer_vt(svt) || !is_integer_vt(dvt))
        return FALSE;
    int ssize = value_size(svt), dsize = value_size(dvt);
    BOOL same_size = ssize == dsize && svt != VT_BOOL;
    if (svt == VT_BOOL) {
        /* a boolean is a short, which widens with its sign into the integers and is cut for the bytes. */
        if (dvt == VT_I8 || dvt == VT_UI8)
            d->llVal = s->iVal;
        else if (dsize == 4)
            d->lVal = s->iVal;
        else if (dsize == 1)
            d->bVal = s->bVal;
        else
            d->iVal = s->iVal;
        d->vt = dvt;
        return TRUE;
    }
    if (!same_size && !(svt == VT_I2 && dvt == VT_BOOL))
        return FALSE;
    memcpy(&d->llVal, &s->llVal, ssize);
    d->vt = dvt;
    return TRUE;
}

static ULONG nls_flags(USHORT flags)
{
    ULONG f = 0;
    if (flags & VARIANT_USE_NLS)
        f |= LOCALE_USE_NLS;
    if (flags & VARIANT_NOUSEROVERRIDE)
        f |= LOCALE_NOUSEROVERRIDE;
    return f;
}

static LCID effective_lcid(LCID lcid)
{
    if (!(lcid & 0xFFFFFBFF))
        lcid = GetUserDefaultLCID();
    if (lcid == 0x800)
        lcid = GetSystemDefaultLCID();
    return lcid;
}

static HRESULT convert_from_string(const VARIANT *s, VARTYPE dvt, VARIANT *d, LCID lcid, USHORT flags)
{
    BSTR str = s->bstrVal;
    ULONG f = nls_flags(flags);
    HRESULT hr;
    VARIANT v;
    memset(&v, 0, sizeof(v));
    switch (dvt) {
    case VT_I1: hr = VarI1FromStr(str, lcid, f, &v.cVal); break;
    case VT_UI1: hr = VarUI1FromStr(str, lcid, f, &v.bVal); break;
    case VT_I2: hr = VarI2FromStr(str, lcid, f, &v.iVal); break;
    case VT_UI2: hr = VarUI2FromStr(str, lcid, f, &v.uiVal); break;
    case VT_I4:
    case VT_INT: hr = VarI4FromStr(str, lcid, f, &v.lVal); break;
    case VT_UI4:
    case VT_UINT: hr = VarUI4FromStr(str, lcid, f, &v.ulVal); break;
    case VT_I8: hr = VarI8FromStr(str, lcid, f, &v.llVal); break;
    case VT_UI8: hr = VarUI8FromStr(str, lcid, f, &v.ullVal); break;
    case VT_R4: hr = VarR4FromStr(str, lcid, f, &v.fltVal); break;
    case VT_R8: hr = VarR8FromStr(str, lcid, f, &v.dblVal); break;
    case VT_DATE: hr = VarDateFromStr(str, lcid, f | (flags & VARIANT_CALENDAR_HIJRI), &v.date); break;
    case VT_CY: hr = VarCyFromStr(str, lcid, f, &v.cyVal); break;
    case VT_BOOL: hr = VarBoolFromStr(str, lcid, f | (flags & VARIANT_LOCALBOOL), &v.boolVal); break;
    case VT_DECIMAL: hr = VarDecFromStr(str, lcid, f, &v.decVal); break;
    default: return DISP_E_TYPEMISMATCH;
    }
    if (FAILED(hr))
        return hr;
    /* DECIMAL has no vt field of its own: it shares the first two bytes. */
    VARTYPE vt = dvt;
    DECIMAL dec = v.decVal;
    *d = v;
    if (dvt == VT_DECIMAL)
        d->decVal = dec;
    d->vt = vt;
    return S_OK;
}

static HRESULT convert_to_string(const VARIANT *s, VARIANT *d, LCID lcid, USHORT flags)
{
    ULONG f = nls_flags(flags);
    BSTR str = nullptr;
    HRESULT hr;
    switch (s->vt) {
    case VT_BOOL:
        if (flags & (VARIANT_ALPHABOOL | VARIANT_LOCALBOOL))
            hr = VarBstrFromBool(s->boolVal, lcid, f | (flags & VARIANT_LOCALBOOL), &str);
        else
            hr = VarBstrFromI2(s->iVal, lcid, f, &str);
        break;
    case VT_I1: hr = VarBstrFromI1(s->cVal, lcid, f, &str); break;
    case VT_UI1: hr = VarBstrFromUI1(s->bVal, lcid, f, &str); break;
    case VT_I2: hr = VarBstrFromI2(s->iVal, lcid, f, &str); break;
    case VT_UI2: hr = VarBstrFromUI2(s->uiVal, lcid, f, &str); break;
    case VT_I4:
    case VT_INT: hr = VarBstrFromI4(s->lVal, lcid, f, &str); break;
    case VT_UI4:
    case VT_UINT: hr = VarBstrFromUI4(s->ulVal, lcid, f, &str); break;
    case VT_I8: hr = VarBstrFromI8(s->llVal, lcid, f, &str); break;
    case VT_UI8: hr = VarBstrFromUI8(s->ullVal, lcid, f, &str); break;
    case VT_R4: hr = VarBstrFromR4(s->fltVal, lcid, f, &str); break;
    case VT_R8: hr = VarBstrFromR8(s->dblVal, lcid, f, &str); break;
    case VT_DATE: hr = VarBstrFromDate(s->date, lcid, f | (flags & VARIANT_CALENDAR_HIJRI), &str); break;
    case VT_CY: hr = VarBstrFromCy(s->cyVal, lcid, f, &str); break;
    case VT_DECIMAL: hr = VarBstrFromDec(&s->decVal, lcid, f, &str); break;
    default: return DISP_E_TYPEMISMATCH;
    }
    if (FAILED(hr))
        return hr;
    d->vt = VT_BSTR;
    d->bstrVal = str;
    return S_OK;
}

/* the change of a value that is not a reference, into a variant that owns the result. */
static HRESULT change_value(const VARIANT *s, VARTYPE dvt, VARIANT *d, LCID lcid, USHORT flags)
{
    VARTYPE svt = s->vt;
    memset(d, 0, sizeof(*d));

    if (svt == dvt) {
        d->vt = VT_EMPTY;
        if (svt == VT_BSTR) {
            /* a missing string comes out as an empty one. */
            d->bstrVal = SysAllocStringByteLen((LPCSTR)s->bstrVal, SysStringByteLen(s->bstrVal));
            if (!d->bstrVal)
                return E_OUTOFMEMORY;
            d->vt = VT_BSTR;
            return S_OK;
        }
        return VariantCopy(d, s);
    }
    if (dvt == VT_EMPTY || dvt == VT_NULL) {
        /* anything but the types that have no value of their own turns into the empty or null variant. */
        if ((svt == VT_EMPTY || svt == VT_NULL) || svt == VT_ERROR || (svt & (VT_ARRAY | VT_VECTOR)) ||
            svt == VT_RECORD)
            return DISP_E_TYPEMISMATCH;
        if (!is_number_vt(svt) && svt != VT_BSTR && svt != VT_DISPATCH && svt != VT_UNKNOWN)
            return DISP_E_TYPEMISMATCH;
        d->vt = dvt;
        return S_OK;
    }
    if (svt & VT_ARRAY) {
        if (svt == (VT_ARRAY | VT_UI1) && dvt == VT_BSTR) {
            HRESULT hr = BstrFromVector(s->parray, &d->bstrVal);
            if (SUCCEEDED(hr))
                d->vt = VT_BSTR;
            return hr;
        }
        return DISP_E_TYPEMISMATCH;
    }
    if (dvt == (VT_ARRAY | VT_UI1)) {
        if (svt == VT_BSTR) {
            HRESULT hr = VectorFromBstr(s->bstrVal, &d->parray);
            if (SUCCEEDED(hr))
                d->vt = dvt;
            return hr;
        }
        return DISP_E_TYPEMISMATCH;
    }
    if (svt == VT_NULL || svt == VT_ERROR || svt == VT_RECORD || svt == VT_VARIANT || svt == VT_INT_PTR ||
        svt == VT_UINT_PTR)
        return DISP_E_TYPEMISMATCH;
    if (dvt == VT_ERROR || dvt == VT_RECORD || dvt == VT_VARIANT)
        return DISP_E_TYPEMISMATCH;

    if (svt == VT_UNKNOWN || svt == VT_DISPATCH) {
        if (dvt == VT_UNKNOWN || dvt == VT_DISPATCH) {
            IUnknown *u = s->punkVal;
            IUnknown *out = nullptr;
            if (u) {
                HRESULT hr = u->lpVtbl->QueryInterface(u, dvt == VT_UNKNOWN ? &IID_IUnknown : &IID_IDispatch,
                                                       (void **)&out);
                if (FAILED(hr))
                    return hr;
            }
            d->punkVal = out;
            d->vt = dvt;
            return S_OK;
        }
        return DISP_E_TYPEMISMATCH;
    }
    if (dvt == VT_UNKNOWN || dvt == VT_DISPATCH)
        return DISP_E_TYPEMISMATCH;

    if (svt == VT_EMPTY) {
        if (dvt == VT_BSTR) {
            d->bstrVal = SysAllocStringLen(nullptr, 0);
            if (!d->bstrVal)
                return E_OUTOFMEMORY;
            d->vt = VT_BSTR;
            return S_OK;
        }
        AUT_NUM zero;
        zero.Kind = NK_INT;
        zero.Precision = 0;
        zero.I = 0;
        return AutNumStore(&zero, dvt, d, lcid, 0);
    }
    if (svt == VT_BSTR) {
        if (dvt == VT_BSTR)
            return DISP_E_TYPEMISMATCH;
        return convert_from_string(s, dvt, d, lcid, flags);
    }
    if (!is_number_vt(svt))
        return DISP_E_TYPEMISMATCH;
    if (dvt == VT_BSTR) {
        if (svt == VT_DECIMAL && !AutDecimalValid(&s->decVal))
            return E_INVALIDARG;
        return convert_to_string(s, d, lcid, flags);
    }
    if (!is_number_vt(dvt))
        return DISP_E_TYPEMISMATCH;
    if (copy_integer_bits(s, dvt, d))
        return S_OK;
    AUT_NUM n;
    if (!AutNumFromVariant(s, &n))
        return DISP_E_TYPEMISMATCH;
    return AutNumStore(&n, dvt, d, lcid, nls_flags(flags));
}

WINOLEAUTAPI HRESULT WINAPI VariantChangeTypeEx(VARIANTARG *dest, const VARIANTARG *src, LCID lcid, USHORT flags,
                                                VARTYPE vt)
{
    if (!dest || !src)
        return E_INVALIDARG;
    VARTYPE svt = src->vt;
    if (vt >= VT_VARIANT) {
        VARTYPE base = (vt & 0x6000) ? vt & 0x9FFF : vt;
        if (base > VT_RECORD || !((1ull << base) & 0x1000FF7FFCull))
            return DISP_E_BADVARTYPE;
        if (vt & 0xE000) {
            if (vt == svt && (vt & 0x6000) == 0x6000)
                return VariantCopy(dest, src);
            if (vt != (VT_ARRAY | VT_UI1))
                return DISP_E_TYPEMISMATCH;
        }
    }
    lcid = effective_lcid(lcid);

    VARIANT borrowed, extracted, result;
    const VARIANT *s = src;
    extracted.vt = VT_EMPTY;
    if (!valid_type(svt))
        return DISP_E_BADVARTYPE;
    if (svt & VT_BYREF) {
        HRESULT hr = deref_variant(src, &borrowed);
        if (FAILED(hr))
            return hr == DISP_E_TYPEMISMATCH ? DISP_E_TYPEMISMATCH : hr;
        s = &borrowed;
        if (!valid_type(s->vt))
            return DISP_E_BADVARTYPE;
    }
    HRESULT hr;
    for (;;) {
        if (s->vt == VT_DISPATCH && s->vt != vt && vt != VT_UNKNOWN && vt != VT_EMPTY && vt != VT_NULL) {
            /* a dispatch object stands for its default property. */
            if (flags & VARIANT_NOVALUEPROP)
                return DISP_E_TYPEMISMATCH;
            VARIANT value;
            hr = AutExtractValueProperty(s->pdispVal, lcid, &value);
            if (FAILED(hr))
                return hr;
            VariantClear(&extracted);
            extracted = value;
            s = &extracted;
            if (!valid_type(s->vt)) {
                VariantClear(&extracted);
                return DISP_E_BADVARTYPE;
            }
            if (s->vt & VT_BYREF) {
                hr = deref_variant(&extracted, &borrowed);
                if (FAILED(hr)) {
                    VariantClear(&extracted);
                    return hr;
                }
                s = &borrowed;
            }
            continue;
        }
        break;
    }
    hr = change_value(s, vt, &result, lcid, flags);
    if (FAILED(hr)) {
        VariantClear(&extracted);
        return hr;
    }
    /* the result is ready: the old value goes, and the source may be the destination itself. */
    if (dest->vt >= VT_BSTR || dest == src) {
        HRESULT clear = VariantClear(dest);
        if (FAILED(clear)) {
            VariantClear(&result);
            VariantClear(&extracted);
            return clear;
        }
    }
    *dest = result;
    VariantClear(&extracted);
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VariantChangeType(VARIANTARG *dest, const VARIANTARG *src, USHORT flags, VARTYPE vt)
{
    return VariantChangeTypeEx(dest, src, LOCALE_USER_DEFAULT, flags, vt);
}
