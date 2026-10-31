/* values: the variants that properties and qualifiers hold, and the cim types they stand for. */
#include "wbemcore.h"

void wv_init(VARIANT *v)
{
    memset(v, 0, sizeof(*v));
    v->vt = VT_NULL;
}

HRESULT wv_copy(VARIANT *dst, const VARIANT *src)
{
    VariantInit(dst);
    return VariantCopy(dst, src);
}

BOOL wv_is_null(const VARIANT *v)
{
    return v->vt == VT_NULL || v->vt == VT_EMPTY;
}

static CIMTYPE base_of(CIMTYPE type)
{
    return type & ~CIM_FLAG_ARRAY;
}

VARTYPE wv_type_of(CIMTYPE type)
{
    VARTYPE vt;
    switch (base_of(type)) {
    case CIM_SINT8:
    case CIM_SINT16:
    case CIM_CHAR16:
        vt = VT_I2;
        break;
    case CIM_UINT8:
        vt = VT_UI1;
        break;
    case CIM_UINT16:
    case CIM_SINT32:
    case CIM_UINT32:
        vt = VT_I4;
        break;
    case CIM_REAL32:
        vt = VT_R4;
        break;
    case CIM_REAL64:
        vt = VT_R8;
        break;
    case CIM_BOOLEAN:
        vt = VT_BOOL;
        break;
    case CIM_OBJECT:
        vt = VT_UNKNOWN;
        break;
    default:
        vt = VT_BSTR;
        break;
    }
    return (type & CIM_FLAG_ARRAY) ? (VARTYPE)(vt | VT_ARRAY) : vt;
}

static const struct {
    const WCHAR *name;
    CIMTYPE type;
} TYPE_NAMES[] = {
    {L"string", CIM_STRING},     {L"sint8", CIM_SINT8},     {L"uint8", CIM_UINT8},   {L"sint16", CIM_SINT16},
    {L"uint16", CIM_UINT16},     {L"sint32", CIM_SINT32},   {L"uint32", CIM_UINT32}, {L"sint64", CIM_SINT64},
    {L"uint64", CIM_UINT64},     {L"real32", CIM_REAL32},   {L"real64", CIM_REAL64}, {L"boolean", CIM_BOOLEAN},
    {L"datetime", CIM_DATETIME}, {L"char16", CIM_CHAR16},   {L"object", CIM_OBJECT},
};

const WCHAR *wv_cim_name(CIMTYPE type)
{
    type = base_of(type);
    if (type == CIM_REFERENCE)
        return L"ref";
    for (UINT i = 0; i < RTL_NUMBER_OF(TYPE_NAMES); i++)
        if (TYPE_NAMES[i].type == type)
            return TYPE_NAMES[i].name;
    return L"string";
}

CIMTYPE wv_cim_from_name(const WCHAR *name)
{
    if (wb_eq(name, L"ref"))
        return CIM_REFERENCE;
    for (UINT i = 0; i < RTL_NUMBER_OF(TYPE_NAMES); i++)
        if (wb_eq(name, TYPE_NAMES[i].name))
            return TYPE_NAMES[i].type;
    return CIM_ILLEGAL;
}

/* ------------------------------------------------------------------ numbers from variants */

static BOOL parse_integer(const WCHAR *text, BOOL *negative, ULONGLONG *magnitude)
{
    while (*text == ' ')
        text++;
    *negative = FALSE;
    if (*text == '-' || *text == '+') {
        *negative = *text == '-';
        text++;
    }
    int base = 10;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text += 2;
    }
    if (!*text)
        return FALSE;
    ULONGLONG value = 0;
    for (; *text; text++) {
        int digit;
        if (*text >= '0' && *text <= '9')
            digit = *text - '0';
        else if (base == 16 && *text >= 'a' && *text <= 'f')
            digit = *text - 'a' + 10;
        else if (base == 16 && *text >= 'A' && *text <= 'F')
            digit = *text - 'A' + 10;
        else if (*text == ' ' && !text[1])
            break;
        else
            return FALSE;
        ULONGLONG next = value * (ULONGLONG)base + (ULONGLONG)digit;
        if (value > (~0ULL - (ULONGLONG)digit) / (ULONGLONG)base)
            return FALSE;
        value = next;
    }
    *magnitude = value;
    return TRUE;
}

/* the value as a sign and a magnitude, or a failure for text that is not a whole number. */
static HRESULT integer_of(const VARIANT *v, BOOL *negative, ULONGLONG *magnitude)
{
    LONGLONG signed_value;
    *negative = FALSE;
    switch (v->vt) {
    case VT_I1:
        signed_value = v->cVal;
        break;
    case VT_I2:
        signed_value = v->iVal;
        break;
    case VT_I4:
    case VT_INT:
        signed_value = v->lVal;
        break;
    case VT_I8:
        signed_value = v->llVal;
        break;
    case VT_UI1:
        *magnitude = v->bVal;
        return S_OK;
    case VT_UI2:
        *magnitude = v->uiVal;
        return S_OK;
    case VT_UI4:
    case VT_UINT:
        *magnitude = v->ulVal;
        return S_OK;
    case VT_UI8:
        *magnitude = v->ullVal;
        return S_OK;
    case VT_BOOL:
        *magnitude = v->boolVal ? 1 : 0;
        return S_OK;
    case VT_R4:
        signed_value = (LONGLONG)v->fltVal;
        break;
    case VT_R8:
        signed_value = (LONGLONG)v->dblVal;
        break;
    case VT_BSTR:
        return v->bstrVal && parse_integer(v->bstrVal, negative, magnitude) ? S_OK : WBEM_E_TYPE_MISMATCH;
    default:
        return WBEM_E_TYPE_MISMATCH;
    }
    *negative = signed_value < 0;
    *magnitude = signed_value < 0 ? (ULONGLONG)(-(signed_value + 1)) + 1 : (ULONGLONG)signed_value;
    return S_OK;
}

static HRESULT range_check(BOOL negative, ULONGLONG magnitude, LONGLONG low, ULONGLONG high, LONGLONG *out)
{
    if (negative) {
        if (low >= 0 || magnitude > (ULONGLONG)(-(low + 1)) + 1)
            return magnitude ? WBEM_E_VALUE_OUT_OF_RANGE : S_OK;
        *out = -(LONGLONG)magnitude;
        return S_OK;
    }
    if (magnitude > high)
        return WBEM_E_VALUE_OUT_OF_RANGE;
    *out = (LONGLONG)magnitude;
    return S_OK;
}

static HRESULT number_for(const VARIANT *src, CIMTYPE type, VARIANT *dst)
{
    BOOL negative;
    ULONGLONG magnitude;
    LONGLONG value = 0;
    HRESULT hr;
    switch (type) {
    case CIM_REAL32:
    case CIM_REAL64: {
        double d;
        if (src->vt == VT_BSTR) {
            hr = VarR8FromStr(src->bstrVal, 0x409, 0, &d);
            if (FAILED(hr))
                return WBEM_E_TYPE_MISMATCH;
        } else if (src->vt == VT_R4) {
            d = src->fltVal;
        } else if (src->vt == VT_R8) {
            d = src->dblVal;
        } else {
            hr = integer_of(src, &negative, &magnitude);
            if (FAILED(hr))
                return hr;
            d = negative ? -(double)magnitude : (double)magnitude;
        }
        if (type == CIM_REAL32) {
            dst->vt = VT_R4;
            dst->fltVal = (float)d;
        } else {
            dst->vt = VT_R8;
            dst->dblVal = d;
        }
        return S_OK;
    }
    default:
        break;
    }
    hr = integer_of(src, &negative, &magnitude);
    if (FAILED(hr))
        return hr;
    switch (type) {
    case CIM_SINT8:
        hr = range_check(negative, magnitude, -128, 127, &value);
        dst->vt = VT_I2;
        dst->iVal = (SHORT)value;
        break;
    case CIM_SINT16:
    case CIM_CHAR16:
        hr = range_check(negative, magnitude, -32768, 32767, &value);
        dst->vt = VT_I2;
        dst->iVal = (SHORT)value;
        break;
    case CIM_UINT8:
        hr = range_check(negative, magnitude, 0, 255, &value);
        dst->vt = VT_UI1;
        dst->bVal = (BYTE)value;
        break;
    case CIM_UINT16:
        hr = range_check(negative, magnitude, 0, 65535, &value);
        dst->vt = VT_I4;
        dst->lVal = (LONG)value;
        break;
    case CIM_SINT32:
        hr = range_check(negative, magnitude, -2147483647LL - 1, 2147483647, &value);
        dst->vt = VT_I4;
        dst->lVal = (LONG)value;
        break;
    case CIM_UINT32:
        hr = range_check(negative, magnitude, 0, 0xFFFFFFFFULL, &value);
        dst->vt = VT_I4;
        dst->lVal = (LONG)(ULONG)value;
        break;
    case CIM_SINT64: {
        WCHAR text[32];
        hr = range_check(negative, magnitude, -9223372036854775807LL - 1, 9223372036854775807ULL, &value);
        if (FAILED(hr))
            return hr;
        _snwprintf(text, RTL_NUMBER_OF(text) - 1, L"%I64d", value);
        dst->vt = VT_BSTR;
        dst->bstrVal = SysAllocString(text);
        return dst->bstrVal ? S_OK : E_OUTOFMEMORY;
    }
    case CIM_UINT64: {
        WCHAR text[32];
        if (negative && magnitude)
            return WBEM_E_VALUE_OUT_OF_RANGE;
        _snwprintf(text, RTL_NUMBER_OF(text) - 1, L"%I64u", magnitude);
        dst->vt = VT_BSTR;
        dst->bstrVal = SysAllocString(text);
        return dst->bstrVal ? S_OK : E_OUTOFMEMORY;
    }
    default:
        return WBEM_E_TYPE_MISMATCH;
    }
    return hr;
}

static HRESULT scalar_coerce(VARIANT *dst, const VARIANT *src, CIMTYPE type)
{
    HRESULT hr;
    VARIANT value;
    VariantInit(&value);
    switch (type) {
    case CIM_STRING:
    case CIM_DATETIME:
    case CIM_REFERENCE:
        if (src->vt == VT_BSTR) {
            hr = VariantCopy(&value, src);
        } else {
            hr = VariantChangeType(&value, src, 0, VT_BSTR);
            if (FAILED(hr))
                hr = WBEM_E_TYPE_MISMATCH;
        }
        break;
    case CIM_BOOLEAN:
        if (src->vt == VT_BOOL) {
            hr = VariantCopy(&value, src);
        } else if (src->vt == VT_BSTR) {
            hr = WBEM_E_TYPE_MISMATCH;
            if (src->bstrVal && (wb_eq(src->bstrVal, L"true") || wb_eq(src->bstrVal, L"-1") || wb_eq(src->bstrVal, L"1"))) {
                value.vt = VT_BOOL;
                value.boolVal = VARIANT_TRUE;
                hr = S_OK;
            } else if (src->bstrVal && (wb_eq(src->bstrVal, L"false") || wb_eq(src->bstrVal, L"0"))) {
                value.vt = VT_BOOL;
                value.boolVal = VARIANT_FALSE;
                hr = S_OK;
            }
        } else {
            BOOL negative;
            ULONGLONG magnitude;
            hr = integer_of(src, &negative, &magnitude);
            if (SUCCEEDED(hr)) {
                value.vt = VT_BOOL;
                value.boolVal = magnitude ? VARIANT_TRUE : VARIANT_FALSE;
            }
        }
        break;
    case CIM_OBJECT:
        if (src->vt == VT_UNKNOWN || src->vt == VT_DISPATCH) {
            value.vt = VT_UNKNOWN;
            value.punkVal = src->punkVal;
            if (value.punkVal)
                value.punkVal->lpVtbl->AddRef(value.punkVal);
            hr = S_OK;
        } else {
            hr = WBEM_E_TYPE_MISMATCH;
        }
        break;
    default:
        hr = number_for(src, type, &value);
        break;
    }
    if (FAILED(hr)) {
        VariantClear(&value);
        return hr;
    }
    VariantClear(dst);
    *dst = value;
    return S_OK;
}

HRESULT wv_coerce(VARIANT *dst, const VARIANT *src, CIMTYPE type)
{
    VariantClear(dst);
    wv_init(dst);
    if (wv_is_null(src))
        return S_OK;
    if (!(type & CIM_FLAG_ARRAY))
        return scalar_coerce(dst, src, type);
    if (!(src->vt & VT_ARRAY) || (src->vt & VT_BYREF) || !src->parray)
        return WBEM_E_TYPE_MISMATCH;
    SAFEARRAY *in = src->parray;
    if (SafeArrayGetDim(in) != 1)
        return WBEM_E_TYPE_MISMATCH;
    LONG low, high;
    SafeArrayGetLBound(in, 1, &low);
    SafeArrayGetUBound(in, 1, &high);
    VARTYPE element_vt = (VARTYPE)(src->vt & VT_TYPEMASK);
    VARTYPE target = wv_type_of(type) & VT_TYPEMASK;
    SAFEARRAY *out = SafeArrayCreateVector(target, 0, (ULONG)(high - low + 1));
    if (!out)
        return E_OUTOFMEMORY;
    HRESULT hr = S_OK;
    for (LONG i = low; i <= high && SUCCEEDED(hr); i++) {
        VARIANT item, converted;
        VariantInit(&item);
        VariantInit(&converted);
        if (element_vt == VT_VARIANT) {
            hr = SafeArrayGetElement(in, &i, &item);
        } else {
            item.vt = element_vt;
            hr = SafeArrayGetElement(in, &i, &item.lVal);
            if (SUCCEEDED(hr) && (element_vt == VT_I8 || element_vt == VT_UI8 || element_vt == VT_R8))
                hr = SafeArrayGetElement(in, &i, &item.llVal);
        }
        if (SUCCEEDED(hr)) {
            wv_init(&converted);
            hr = scalar_coerce(&converted, &item, base_of(type));
        }
        if (SUCCEEDED(hr)) {
            LONG index = i - low;
            void *pointer = &converted.lVal;
            if (target == VT_BSTR || target == VT_UNKNOWN)
                pointer = converted.bstrVal;
            else if (target == VT_R8)
                pointer = &converted.dblVal;
            hr = SafeArrayPutElement(out, &index, pointer);
        }
        VariantClear(&item);
        VariantClear(&converted);
    }
    if (FAILED(hr)) {
        SafeArrayDestroy(out);
        return hr;
    }
    dst->vt = (VARTYPE)(target | VT_ARRAY);
    dst->parray = out;
    return S_OK;
}

/* ------------------------------------------------------------------ text */

static void add_quoted(WBuf *buf, const WCHAR *text)
{
    wb_buf_addc(buf, '"');
    for (; *text; text++) {
        if (*text == '"' || *text == '\\')
            wb_buf_addc(buf, '\\');
        wb_buf_addc(buf, *text);
    }
    wb_buf_addc(buf, '"');
}

static WCHAR *scalar_text(const VARIANT *v, CIMTYPE type)
{
    WBuf buf = {0};
    switch (base_of(type)) {
    case CIM_STRING:
    case CIM_DATETIME:
    case CIM_REFERENCE:
        if (v->vt == VT_BSTR && v->bstrVal)
            add_quoted(&buf, v->bstrVal);
        else
            wb_buf_add(&buf, L"NULL");
        break;
    case CIM_BOOLEAN:
        wb_buf_add(&buf, v->boolVal ? L"TRUE" : L"FALSE");
        break;
    case CIM_REAL32:
        wb_buf_addf(&buf, L"%g", (double)v->fltVal);
        break;
    case CIM_REAL64:
        wb_buf_addf(&buf, L"%g", v->dblVal);
        break;
    case CIM_SINT64:
    case CIM_UINT64:
        wb_buf_add(&buf, v->vt == VT_BSTR && v->bstrVal ? v->bstrVal : L"0");
        break;
    case CIM_UINT32:
        wb_buf_addf(&buf, L"%u", (ULONG)v->lVal);
        break;
    case CIM_UINT16:
        wb_buf_addf(&buf, L"%u", (ULONG)(USHORT)v->lVal);
        break;
    case CIM_UINT8:
        wb_buf_addf(&buf, L"%u", (ULONG)v->bVal);
        break;
    case CIM_SINT8:
    case CIM_SINT16:
    case CIM_CHAR16:
        wb_buf_addf(&buf, L"%d", (int)v->iVal);
        break;
    case CIM_SINT32:
        wb_buf_addf(&buf, L"%d", (int)v->lVal);
        break;
    default:
        wb_buf_add(&buf, L"NULL");
        break;
    }
    return wb_buf_take(&buf);
}

WCHAR *wv_text(const VARIANT *v, CIMTYPE type)
{
    if (wv_is_null(v))
        return wb_wcsdup(L"NULL");
    if (!(type & CIM_FLAG_ARRAY) && !(v->vt & VT_ARRAY))
        return scalar_text(v, type);
    WBuf buf = {0};
    wb_buf_addc(&buf, '{');
    if ((v->vt & VT_ARRAY) && v->parray) {
        LONG low, high;
        SafeArrayGetLBound(v->parray, 1, &low);
        SafeArrayGetUBound(v->parray, 1, &high);
        VARTYPE vt = (VARTYPE)(v->vt & VT_TYPEMASK);
        for (LONG i = low; i <= high; i++) {
            VARIANT item;
            VariantInit(&item);
            item.vt = vt;
            if (vt == VT_BSTR || vt == VT_UNKNOWN)
                SafeArrayGetElement(v->parray, &i, &item.bstrVal);
            else if (vt == VT_R8 || vt == VT_I8 || vt == VT_UI8)
                SafeArrayGetElement(v->parray, &i, &item.llVal);
            else
                SafeArrayGetElement(v->parray, &i, &item.lVal);
            WCHAR *one = scalar_text(&item, base_of(type));
            if (i > low)
                wb_buf_add(&buf, L", ");
            wb_buf_add(&buf, one);
            wb_free(one);
            if (vt == VT_BSTR)
                SysFreeString(item.bstrVal);
            else if (vt == VT_UNKNOWN && item.punkVal)
                item.punkVal->lpVtbl->Release(item.punkVal);
        }
    }
    wb_buf_addc(&buf, '}');
    return wb_buf_take(&buf);
}

/* ------------------------------------------------------------------ comparison */

static int compare_double(double a, double b)
{
    return a < b ? -1 : a > b ? 1 : 0;
}

BOOL wv_compare(const VARIANT *a, const VARIANT *b, CIMTYPE type, int *order)
{
    if (wv_is_null(a) || wv_is_null(b))
        return FALSE;
    switch (base_of(type)) {
    case CIM_STRING:
    case CIM_DATETIME:
    case CIM_REFERENCE:
    case CIM_SINT64:
    case CIM_UINT64: {
        if (a->vt != VT_BSTR || b->vt != VT_BSTR)
            return FALSE;
        if (base_of(type) == CIM_SINT64 || base_of(type) == CIM_UINT64) {
            BOOL na, nb;
            ULONGLONG ma, mb;
            if (!parse_integer(a->bstrVal, &na, &ma) || !parse_integer(b->bstrVal, &nb, &mb))
                return FALSE;
            if (na != nb && (ma || mb))
                *order = na ? -1 : 1;
            else if (na)
                *order = ma < mb ? 1 : ma > mb ? -1 : 0;
            else
                *order = ma < mb ? -1 : ma > mb ? 1 : 0;
            return TRUE;
        }
        int c = _wcsicmp(a->bstrVal ? a->bstrVal : L"", b->bstrVal ? b->bstrVal : L"");
        *order = c < 0 ? -1 : c > 0 ? 1 : 0;
        return TRUE;
    }
    case CIM_BOOLEAN:
        *order = (a->boolVal != 0) - (b->boolVal != 0);
        return TRUE;
    case CIM_REAL32:
        *order = compare_double(a->fltVal, b->fltVal);
        return TRUE;
    case CIM_REAL64:
        *order = compare_double(a->dblVal, b->dblVal);
        return TRUE;
    case CIM_UINT32: {
        ULONG x = (ULONG)a->lVal, y = (ULONG)b->lVal;
        *order = x < y ? -1 : x > y ? 1 : 0;
        return TRUE;
    }
    case CIM_UINT8:
        *order = (int)a->bVal - (int)b->bVal;
        *order = *order < 0 ? -1 : *order > 0 ? 1 : 0;
        return TRUE;
    case CIM_SINT8:
    case CIM_SINT16:
    case CIM_CHAR16:
        *order = a->iVal < b->iVal ? -1 : a->iVal > b->iVal ? 1 : 0;
        return TRUE;
    case CIM_UINT16:
    case CIM_SINT32:
        *order = a->lVal < b->lVal ? -1 : a->lVal > b->lVal ? 1 : 0;
        return TRUE;
    default:
        return FALSE;
    }
}
