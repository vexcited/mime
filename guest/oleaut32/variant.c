/* variant initialization, clearing, copying and type changes. */
#include "oleaut32_private.h"
#include <mime/win32/winnls.h>

/* whether a variant may hold vt, by value or with VT_BYREF or VT_ARRAY. */
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
            if (!info && src->pvRecord)
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
            if (info)
                info->lpVtbl->AddRef(info);
        }
    }
    *dst = copy;
    return S_OK;
}

/* only for the types VT_BYREF can point at. */
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

/* the target of a VT_BYREF variant as a variant that borrows it and owns nothing. fails for types that cannot be pointed at. */
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
    /* borrowed, VariantCopy makes the owned copy. */
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

/* ------------------------------------------------------------------ type changes */

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
 * integers of the same size, or a boolean, convert by their bits unchecked: -1 as an unsigned short is 65535. other
 * numbers go through the checked conversions.
 */
static BOOL copy_integer_bits(const VARIANT *s, VARTYPE dvt, VARIANT *d)
{
    VARTYPE svt = s->vt;
    if (!is_integer_vt(svt) || !is_integer_vt(dvt))
        return FALSE;
    int ssize = value_size(svt), dsize = value_size(dvt);
    BOOL same_size = ssize == dsize && svt != VT_BOOL;
    if (svt == VT_BOOL) {
        /* a boolean is a short: it sign extends into the integers and is truncated for the bytes. */
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
    /* true is -1, so a number becomes a boolean by its value. */
    if (!same_size || dvt == VT_BOOL)
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

/* 0 and LOCALE_USER_DEFAULT mean the user's locale, LOCALE_SYSTEM_DEFAULT the system's. */
static LCID effective_lcid(LCID lcid)
{
    if (!(lcid & ~(LCID)LOCALE_USER_DEFAULT))
        lcid = GetUserDefaultLCID();
    if (lcid == LOCALE_SYSTEM_DEFAULT)
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

/* s is not a reference, d owns the result. */
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
        /* only types with a plain value can become VT_EMPTY or VT_NULL. */
        if (svt == VT_NULL || svt == VT_ERROR || (svt & (VT_ARRAY | VT_VECTOR)) || svt == VT_RECORD)
            return DISP_E_TYPEMISMATCH;
        if (svt != VT_EMPTY && !is_number_vt(svt) && svt != VT_BSTR && svt != VT_DISPATCH && svt != VT_UNKNOWN)
            return DISP_E_TYPEMISMATCH;
        d->vt = dvt;
        return S_OK;
    }
    if (svt & VT_ARRAY) {
        if (svt == (VT_ARRAY | VT_UI1) && dvt == VT_BSTR) {
            /* a NULL array becomes a NULL string. */
            if (!s->parray) {
                d->bstrVal = nullptr;
                d->vt = VT_BSTR;
                return S_OK;
            }
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
    /* windows converts VT_UINT to 64-bit integers but not VT_INT. */
    if (svt == VT_INT && (dvt == VT_I8 || dvt == VT_UI8))
        return DISP_E_TYPEMISMATCH;
    if (copy_integer_bits(s, dvt, d))
        return S_OK;
    AUT_NUM n;
    if (!AutNumFromVariant(s, &n))
        return DISP_E_TYPEMISMATCH;
    return AutNumStore(&n, dvt, d, lcid, nls_flags(flags));
}

/* S_OK for a valid target, S_FALSE when vt is the source type and the source is a by-reference array. */
static HRESULT check_target_type(const VARIANT *src, VARTYPE vt)
{
    if (vt < VT_VARIANT)
        return S_OK;
    VARTYPE base = vt & (VT_ARRAY | VT_BYREF) ? vt & 0x9FFF : vt;
    static const ULONGLONG allowed = 0x1000FF7FFCull;
    if (base > 36 || !(allowed >> base & 1))
        return DISP_E_BADVARTYPE;
    if (vt & (VT_VECTOR | VT_ARRAY | VT_BYREF)) {
        if (vt == src->vt && (vt & VT_ARRAY) && (vt & VT_BYREF))
            return S_FALSE;
        if (vt != (VT_ARRAY | VT_UI1))
            return DISP_E_TYPEMISMATCH;
    }
    return S_OK;
}

static HRESULT borrow_value(const VARIANT *src, VARIANT *value)
{
    VARTYPE base = src->vt & VT_TYPEMASK;
    if (base == VT_INT_PTR || base == VT_UINT_PTR)
        return DISP_E_BADVARTYPE;
    if (src->vt & VT_BYREF) {
        VARTYPE pointee = src->vt & ~VT_BYREF;
        if ((base <= VT_NULL && !(src->vt & VT_ARRAY)) || (pointee != VT_VARIANT && !valid_type(pointee)))
            return DISP_E_BADVARTYPE;
        return deref_variant(src, value);
    }
    if (src->vt == VT_VARIANT)
        return DISP_E_TYPEMISMATCH;
    if (!valid_type(src->vt))
        return DISP_E_BADVARTYPE;
    *value = *src;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VariantChangeTypeEx(VARIANTARG *dest, const VARIANTARG *src, LCID lcid, USHORT flags,
                                                VARTYPE vt)
{
    if (!dest || !src)
        return E_INVALIDARG;
    HRESULT hr = check_target_type(src, vt);
    if (FAILED(hr))
        return hr;
    if (hr == S_FALSE)
        return VariantCopy(dest, src);
    LCID conversion_lcid = effective_lcid(lcid);

    VARIANT value;
    hr = borrow_value(src, &value);
    if (FAILED(hr))
        return hr;

    VARIANT result, inner;
    VariantInit(&inner);
    /* an object stands for its default value, unless the object itself is asked for. */
    if (value.vt == VT_DISPATCH && (is_number_vt(vt) || vt == VT_BSTR) && !(flags & VARIANT_NOVALUEPROP)) {
        hr = AutExtractValueProperty(value.pdispVal, lcid, &inner);
        if (hr != S_OK) {
            VariantClear(&inner);
            return DISP_E_TYPEMISMATCH;
        }
        hr = borrow_value(&inner, &value);
        if (FAILED(hr)) {
            VariantClear(&inner);
            return hr;
        }
    }
    hr = change_value(&value, vt, &result, conversion_lcid, flags);
    VariantClear(&inner);
    if (FAILED(hr))
        return hr;
    hr = VariantClear(dest);
    if (FAILED(hr)) {
        VariantClear(&result);
        return hr;
    }
    *dest = result;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VariantChangeType(VARIANTARG *dest, const VARIANTARG *src, USHORT flags, VARTYPE vt)
{
    return VariantChangeTypeEx(dest, src, LOCALE_USER_DEFAULT, flags, vt);
}

/* ------------------------------------------------------------------ comparison */

typedef enum { CMP_INT, CMP_CY, CMP_REAL, CMP_DEC } CMP_KIND;

typedef struct {
    CMP_KIND Kind;
    LONGLONG Int;
    CY Cy;
    double Real;
    DECIMAL Dec;
} CMP_NUM;

static BOOL is_comparable_type(VARTYPE vt)
{
    return vt <= VT_UNKNOWN + 1 && vt != 15 ? TRUE : vt == VT_I1 || vt == VT_UI1 || vt == VT_I8;
}

static void load_number(const VARIANT *v, CMP_NUM *n)
{
    memset(n, 0, sizeof(*n));
    switch (v->vt) {
    case VT_EMPTY:
        break;
    case VT_I1:
        n->Int = v->cVal;
        break;
    case VT_UI1:
        n->Int = v->bVal;
        break;
    case VT_I2:
        n->Int = v->iVal;
        break;
    case VT_BOOL:
        n->Int = v->boolVal;
        break;
    case VT_I4:
        n->Int = v->lVal;
        break;
    case VT_I8:
        n->Int = v->llVal;
        break;
    case VT_CY:
        n->Kind = CMP_CY;
        n->Cy = v->cyVal;
        break;
    case VT_R4:
        n->Kind = CMP_REAL;
        n->Real = v->fltVal;
        break;
    case VT_R8:
    case VT_DATE:
        n->Kind = CMP_REAL;
        n->Real = v->dblVal;
        break;
    case VT_DECIMAL:
        n->Kind = CMP_DEC;
        n->Dec = v->decVal;
        break;
    }
}

static HRESULT to_decimal(const CMP_NUM *n, DECIMAL *out)
{
    switch (n->Kind) {
    case CMP_INT:
        return VarDecFromI8(n->Int, out);
    case CMP_CY:
        return VarDecFromCy(n->Cy, out);
    case CMP_REAL:
        return VarDecFromR8(n->Real, out);
    default:
        *out = n->Dec;
        return S_OK;
    }
}

static double to_real(const CMP_NUM *n)
{
    switch (n->Kind) {
    case CMP_INT:
        return (double)n->Int;
    case CMP_CY:
        return (double)n->Cy.int64 / 10000.0;
    default:
        return n->Real;
    }
}

static int order(LONGLONG a, LONGLONG b)
{
    return a < b ? VARCMP_LT : a == b ? VARCMP_EQ : VARCMP_GT;
}

/* numbers are compared in the widest of their kinds: exactly for integers and currency, as doubles when one is real. */
static HRESULT compare_numbers(const CMP_NUM *a, const CMP_NUM *b)
{
    if (a->Kind == CMP_DEC || b->Kind == CMP_DEC) {
        DECIMAL x, y;
        HRESULT hr = to_decimal(a, &x);
        if (SUCCEEDED(hr))
            hr = to_decimal(b, &y);
        return FAILED(hr) ? hr : VarDecCmp(&x, &y);
    }
    if (a->Kind == CMP_REAL || b->Kind == CMP_REAL) {
        double x = to_real(a), y = to_real(b);
        return x < y ? VARCMP_LT : x == y ? VARCMP_EQ : VARCMP_GT;
    }
    if (a->Kind == CMP_CY || b->Kind == CMP_CY) {
        LONGLONG x, y;
        if (a->Kind == CMP_CY)
            x = a->Cy.int64;
        else if (a->Int > INT64_MAX / 10000 || a->Int < INT64_MIN / 10000)
            return DISP_E_OVERFLOW;
        else
            x = a->Int * 10000;
        if (b->Kind == CMP_CY)
            y = b->Cy.int64;
        else if (b->Int > INT64_MAX / 10000 || b->Int < INT64_MIN / 10000)
            return DISP_E_OVERFLOW;
        else
            y = b->Int * 10000;
        return order(x, y);
    }
    return order(a->Int, b->Int);
}

/* an object stands for its default value. */
static HRESULT unwrap_object(const VARIANT **side, VARIANT *storage, LCID lcid)
{
    if ((*side)->vt != VT_DISPATCH)
        return S_OK;
    HRESULT hr = (*side)->pdispVal ? AutExtractValueProperty((*side)->pdispVal, lcid, storage) : DISP_E_TYPEMISMATCH;
    if (FAILED(hr))
        return hr;
    *side = storage;
    return S_OK;
}

/*
 * null on either side is null, an error or an unknown object is a mismatch, a string is greater than a number and
 * the two strings are compared as strings. a type of an array, a reference or one the comparison does not know is
 * refused, the right one first.
 */
WINOLEAUTAPI HRESULT WINAPI VarCmp(LPVARIANT left, LPVARIANT right, LCID lcid, ULONG flags)
{
    if (!left || !right)
        return E_INVALIDARG;
    VARIANT left_value, right_value;
    VariantInit(&left_value);
    VariantInit(&right_value);
    const VARIANT *l = left, *r = right;
    HRESULT hr = S_OK;
    const VARIANT *both[2] = {right, left};
    for (int i = 0; i < 2; i++) {
        const VARIANT *side = both[i];
        if (side->vt & VT_ARRAY || side->vt == VT_RECORD) {
            hr = DISP_E_TYPEMISMATCH;
            break;
        }
        if (!is_comparable_type(side->vt)) {
            hr = DISP_E_BADVARTYPE;
            break;
        }
    }
    if (SUCCEEDED(hr))
        hr = unwrap_object(&r, &right_value, lcid);
    if (SUCCEEDED(hr))
        hr = unwrap_object(&l, &left_value, lcid);
    if (SUCCEEDED(hr)) {
        if (l->vt == VT_ERROR || r->vt == VT_ERROR)
            hr = DISP_E_TYPEMISMATCH;
        else if (l->vt == VT_NULL || r->vt == VT_NULL)
            hr = VARCMP_NULL;
        else if (l->vt == VT_UNKNOWN || r->vt == VT_UNKNOWN || l->vt == VT_VARIANT || r->vt == VT_VARIANT ||
                 l->vt == VT_DISPATCH || r->vt == VT_DISPATCH)
            hr = DISP_E_TYPEMISMATCH;
        else if (l->vt == VT_BSTR && r->vt == VT_BSTR)
            hr = VarBstrCmp(l->bstrVal, r->bstrVal, lcid, flags);
        else if (l->vt == VT_BSTR)
            hr = r->vt == VT_EMPTY ? (SysStringLen(l->bstrVal) ? VARCMP_GT : VARCMP_EQ) : VARCMP_GT;
        else if (r->vt == VT_BSTR)
            hr = l->vt == VT_EMPTY ? (SysStringLen(r->bstrVal) ? VARCMP_LT : VARCMP_EQ) : VARCMP_LT;
        else {
            CMP_NUM a, b;
            load_number(l, &a);
            load_number(r, &b);
            hr = compare_numbers(&a, &b);
        }
    }
    VariantClear(&left_value);
    VariantClear(&right_value);
    return hr;
}
