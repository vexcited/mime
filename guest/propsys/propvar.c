/* building PROPVARIANTs and reading them back as one type. */
#include "propsys_private.h"

#define TYPE_E_BUFFERTOOSMALL ((HRESULT)0x80028016L)

HRESULT PsCoerceElement(VARTYPE from, const void *source, VARTYPE to, void *out, PROPVAR_CHANGE_FLAGS flags);

/* ------------------------------------------------------------------ init */

static HRESULT init_vector(const void *values, ULONG count, VARTYPE base, PROPVARIANT *value)
{
    memset(value, 0, sizeof(*value));
    UINT size = PsElementSize(base);
    if ((ULONGLONG)count * size > 0xFFFFFFFFULL)
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    void *items = PsAllocZero((SIZE_T)count * size);
    if (!items)
        return E_OUTOFMEMORY;
    if (values)
        memcpy(items, values, (SIZE_T)count * size);
    value->vt = VT_VECTOR | base;
    value->cac.cElems = count;
    value->cac.pElems = items;
    return S_OK;
}

#define INIT_VECTOR(name, ctype, base)                                                                                 \
    HRESULT WINAPI name(const ctype *values, ULONG count, PROPVARIANT *value)                                          \
    {                                                                                                                  \
        return init_vector(values, count, base, value);                                                                \
    }
INIT_VECTOR(InitPropVariantFromInt16Vector, SHORT, VT_I2)
INIT_VECTOR(InitPropVariantFromUInt16Vector, USHORT, VT_UI2)
INIT_VECTOR(InitPropVariantFromInt32Vector, LONG, VT_I4)
INIT_VECTOR(InitPropVariantFromUInt32Vector, ULONG, VT_UI4)
INIT_VECTOR(InitPropVariantFromInt64Vector, LONGLONG, VT_I8)
INIT_VECTOR(InitPropVariantFromUInt64Vector, ULONGLONG, VT_UI8)
INIT_VECTOR(InitPropVariantFromDoubleVector, DOUBLE, VT_R8)
INIT_VECTOR(InitPropVariantFromFileTimeVector, FILETIME, VT_FILETIME)

HRESULT WINAPI InitPropVariantFromBooleanVector(const BOOL *values, ULONG count, PROPVARIANT *value)
{
    HRESULT hr = init_vector(nullptr, count, VT_BOOL, value);
    if (SUCCEEDED(hr) && values)
        for (ULONG i = 0; i < count; i++)
            value->cabool.pElems[i] = values[i] ? VARIANT_TRUE : VARIANT_FALSE;
    return hr;
}

HRESULT WINAPI InitPropVariantFromBuffer(const void *buffer, UINT size, PROPVARIANT *value)
{
    return init_vector(buffer, size, VT_UI1, value);
}

HRESULT WINAPI InitPropVariantFromCLSID(REFCLSID clsid, PROPVARIANT *value)
{
    memset(value, 0, sizeof(*value));
    value->puuid = PsAllocZero(sizeof(CLSID));
    if (!value->puuid)
        return E_OUTOFMEMORY;
    *value->puuid = *clsid;
    value->vt = VT_CLSID;
    return S_OK;
}

static HRESULT init_string(LPCWSTR string, PROPVARIANT *value)
{
    memset(value, 0, sizeof(*value));
    HRESULT hr = PsAllocString(string, &value->pwszVal);
    if (SUCCEEDED(hr))
        value->vt = VT_LPWSTR;
    return hr;
}

HRESULT WINAPI InitPropVariantFromGUIDAsString(REFGUID guid, PROPVARIANT *value)
{
    WCHAR text[40];
    if (!StringFromGUID2(guid, text, 39)) {
        memset(value, 0, sizeof(*value));
        return E_FAIL;
    }
    return init_string(text, value);
}

HRESULT WINAPI InitPropVariantFromFileTime(const FILETIME *time, PROPVARIANT *value)
{
    memset(value, 0, sizeof(*value));
    value->vt = VT_FILETIME;
    value->filetime = *time;
    return S_OK;
}

HRESULT WINAPI InitPropVariantFromResource(HINSTANCE instance, UINT id, PROPVARIANT *value)
{
    WCHAR text[1024];
    memset(value, 0, sizeof(*value));
    if (!LoadStringW(instance, id, text, 1024)) {
        DWORD error = GetLastError();
        return error ? HRESULT_FROM_WIN32(error) : HRESULT_FROM_WIN32(ERROR_RESOURCE_NAME_NOT_FOUND);
    }
    return init_string(text, value);
}

/* like StrRetToStrW: a wide string is taken over, the others are copied. */
HRESULT PsStrRetToString(STRRET *strret, PCUITEMID_CHILD pidl, LPWSTR *out)
{
    *out = nullptr;
    switch (strret->uType) {
    case STRRET_WSTR:
        *out = strret->pOleStr;
        strret->pOleStr = nullptr;
        return *out ? S_OK : E_INVALIDARG;
    case STRRET_CSTR:
        return PsAnsiToWideAlloc(strret->cStr, out);
    case STRRET_OFFSET:
        if (!pidl)
            return E_INVALIDARG;
        return PsAnsiToWideAlloc((LPCSTR)pidl + strret->uOffset, out);
    }
    return E_FAIL;
}

HRESULT WINAPI InitPropVariantFromStrRet(STRRET *strret, PCUITEMID_CHILD pidl, PROPVARIANT *value)
{
    memset(value, 0, sizeof(*value));
    HRESULT hr = PsStrRetToString(strret, pidl, &value->pwszVal);
    if (SUCCEEDED(hr))
        value->vt = VT_LPWSTR;
    return hr;
}

HRESULT WINAPI InitPropVariantFromStringVector(LPCWSTR *values, ULONG count, PROPVARIANT *value)
{
    HRESULT hr = init_vector(nullptr, count, VT_LPWSTR, value);
    if (FAILED(hr) || !values)
        return hr;
    for (ULONG i = 0; i < count; i++) {
        hr = PsAllocString(values[i], &value->calpwstr.pElems[i]);
        if (FAILED(hr)) {
            PropVariantClear(value);
            return hr;
        }
    }
    return S_OK;
}

/* "a; b;;c" holds a, b and c. */
HRESULT WINAPI InitPropVariantFromStringAsVector(LPCWSTR string, PROPVARIANT *value)
{
    memset(value, 0, sizeof(*value));
    if (!string)
        return E_INVALIDARG;
    ULONG count = 0;
    for (LPCWSTR p = string; *p;) {
        while (*p == ' ')
            p++;
        LPCWSTR start = p;
        while (*p && *p != ';')
            p++;
        if (p > start)
            count++;
        if (*p)
            p++;
    }
    HRESULT hr = init_vector(nullptr, count, VT_LPWSTR, value);
    ULONG n = 0;
    for (LPCWSTR p = string; SUCCEEDED(hr) && *p;) {
        while (*p == ' ')
            p++;
        LPCWSTR start = p;
        while (*p && *p != ';')
            p++;
        SIZE_T length = (SIZE_T)(p - start);
        while (length && start[length - 1] == ' ')
            length--;
        if (p > start)
            hr = PsAllocStringLen(start, length, &value->calpwstr.pElems[n++]);
        if (*p)
            p++;
    }
    if (FAILED(hr))
        PropVariantClear(value);
    return hr;
}

HRESULT WINAPI InitPropVariantFromPropVariantVectorElem(const PROPVARIANT *source, ULONG index, PROPVARIANT *value)
{
    memset(value, 0, sizeof(*value));
    if (!(source->vt & (VT_VECTOR | VT_ARRAY))) {
        if (index)
            return E_INVALIDARG;
        return PropVariantCopy(value, source);
    }
    const void *element;
    HRESULT hr = PsElementPointer(source, index, &element);
    if (FAILED(hr))
        return hr;
    VARTYPE base = source->vt & VT_TYPEMASK;
    if (base == VT_VARIANT)
        return PropVariantCopy(value, element);
    PROPVARIANT borrowed = {0};
    borrowed.vt = base;
    if (base == VT_CLSID || base == VT_CF)
        borrowed.puuid = (CLSID *)element;
    else
        memcpy(&borrowed.cVal, element, PsElementSize(base));
    return PropVariantCopy(value, &borrowed);
}

HRESULT WINAPI InitPropVariantVectorFromPropVariant(const PROPVARIANT *source, PROPVARIANT *value)
{
    memset(value, 0, sizeof(*value));
    if (source->vt & VT_VECTOR)
        return PropVariantCopy(value, source);
    VARTYPE base = source->vt & VT_TYPEMASK;
    if ((source->vt & ~VT_TYPEMASK) || !PsElementSize(base))
        return E_INVALIDARG;
    PROPVARIANT single = {0};
    single.vt = VT_VECTOR | base;
    single.cac.cElems = 1;
    single.cac.pElems = base == VT_CLSID || base == VT_CF ? (CHAR *)source->puuid : (CHAR *)&source->cVal;
    return PropVariantCopy(value, &single);
}

/* ------------------------------------------------------------------ scalars */

/* the value itself when it already has type vt, else what PropVariantChangeType makes. */
static HRESULT to_scalar(const PROPVARIANT *value, VARTYPE vt, void *out, UINT size)
{
    memset(out, 0, size);
    if (value->vt == vt) {
        memcpy(out, &value->cVal, size);
        return S_OK;
    }
    PROPVARIANT converted;
    HRESULT hr = PropVariantChangeType(&converted, value, PVCHF_DEFAULT, vt);
    if (SUCCEEDED(hr)) {
        memcpy(out, &converted.cVal, size);
        PropVariantClear(&converted);
    }
    return hr;
}

#define TO_SCALAR(name, ctype, kind)                                                                                   \
    HRESULT WINAPI PropVariantTo##name(const PROPVARIANT *value, ctype *out)                                           \
    {                                                                                                                  \
        return to_scalar(value, kind, out, sizeof(ctype));                                                             \
    }                                                                                                                  \
    ctype WINAPI PropVariantTo##name##WithDefault(const PROPVARIANT *value, ctype fallback)                            \
    {                                                                                                                  \
        ctype result;                                                                                                  \
        if (value->vt == VT_EMPTY || FAILED(to_scalar(value, kind, &result, sizeof(ctype))))                           \
            return fallback;                                                                                           \
        return result;                                                                                                 \
    }
TO_SCALAR(Int16, SHORT, VT_I2)
TO_SCALAR(UInt16, USHORT, VT_UI2)
TO_SCALAR(Int32, LONG, VT_I4)
TO_SCALAR(UInt32, ULONG, VT_UI4)
TO_SCALAR(Int64, LONGLONG, VT_I8)
TO_SCALAR(UInt64, ULONGLONG, VT_UI8)
TO_SCALAR(Double, DOUBLE, VT_R8)

HRESULT WINAPI PropVariantToBoolean(const PROPVARIANT *value, BOOL *out)
{
    VARIANT_BOOL result;
    HRESULT hr = to_scalar(value, VT_BOOL, &result, sizeof(result));
    *out = result == VARIANT_TRUE;
    return hr;
}

BOOL WINAPI PropVariantToBooleanWithDefault(const PROPVARIANT *value, BOOL fallback)
{
    VARIANT_BOOL result;
    if (value->vt == VT_EMPTY || FAILED(to_scalar(value, VT_BOOL, &result, sizeof(result))))
        return fallback;
    return result == VARIANT_TRUE;
}

HRESULT WINAPI PropVariantToFileTime(const PROPVARIANT *value, DWORD flags, FILETIME *out)
{
    HRESULT hr = to_scalar(value, VT_FILETIME, out, sizeof(*out));
    if (SUCCEEDED(hr) && (flags & 1)) {
        FILETIME local;
        if (!FileTimeToLocalFileTime(out, &local))
            return HRESULT_FROM_WIN32(GetLastError());
        *out = local;
    }
    return hr;
}

HRESULT WINAPI PropVariantToGUID(const PROPVARIANT *value, GUID *out)
{
    memset(out, 0, sizeof(*out));
    switch (value->vt) {
    case VT_CLSID:
        *out = *value->puuid;
        return S_OK;
    case VT_EMPTY: case VT_NULL:
        return S_OK;
    case VT_BSTR:
        return PsGuidFromString(value->bstrVal ? value->bstrVal : L"", out) ? S_OK : E_INVALIDARG;
    case VT_LPWSTR:
        return value->pwszVal && PsGuidFromString(value->pwszVal, out) ? S_OK : E_INVALIDARG;
    case VT_LPSTR: {
        WCHAR text[39];
        if (!value->pszVal)
            return E_INVALIDARG;
        MultiByteToWideChar(CP_ACP, 0, value->pszVal, -1, text, 39);
        text[38] = 0;
        return PsGuidFromString(text, out) ? S_OK : E_INVALIDARG;
    }
    case VT_ARRAY | VT_UI1:
        return VariantToGUID((const VARIANT *)value, out);
    }
    return E_INVALIDARG;
}

HRESULT WINAPI PropVariantToBuffer(const PROPVARIANT *value, void *buffer, UINT size)
{
    if (value->vt == (VT_VECTOR | VT_UI1)) {
        if (value->caub.cElems < size)
            return E_FAIL;
        memcpy(buffer, value->caub.pElems, size);
        return S_OK;
    }
    if (value->vt == (VT_ARRAY | VT_UI1))
        return VariantToBuffer((const VARIANT *)value, buffer, size);
    return E_INVALIDARG;
}

/* ------------------------------------------------------------------ strings */

static HRESULT vector_to_string(const PROPVARIANT *value, LPWSTR *out)
{
    ULONG count = PsElementCount(value);
    LPWSTR result = nullptr;
    HRESULT hr = PsAllocString(L"", &result);
    for (ULONG i = 0; SUCCEEDED(hr) && i < count; i++) {
        PROPVARIANT element;
        LPWSTR text = nullptr;
        hr = InitPropVariantFromPropVariantVectorElem(value, i, &element);
        if (SUCCEEDED(hr)) {
            hr = PropVariantToStringAlloc(&element, &text);
            PropVariantClear(&element);
        }
        if (SUCCEEDED(hr)) {
            SIZE_T a = wcslen(result), b = wcslen(text);
            LPWSTR joined = CoTaskMemAlloc((a + b + 3) * sizeof(WCHAR));
            if (joined) {
                memcpy(joined, result, a * sizeof(WCHAR));
                SIZE_T n = a;
                if (i) {
                    joined[n++] = ';';
                    joined[n++] = ' ';
                }
                memcpy(joined + n, text, b * sizeof(WCHAR));
                joined[n + b] = 0;
                CoTaskMemFree(result);
                result = joined;
            } else {
                hr = E_OUTOFMEMORY;
            }
            CoTaskMemFree(text);
        }
    }
    if (FAILED(hr)) {
        CoTaskMemFree(result);
        return hr;
    }
    *out = result;
    return S_OK;
}

HRESULT WINAPI PropVariantToStringAlloc(const PROPVARIANT *value, LPWSTR *out)
{
    *out = nullptr;
    if (value->vt & (VT_VECTOR | VT_ARRAY))
        return vector_to_string(value, out);
    switch (value->vt) {
    case VT_EMPTY: case VT_NULL:
        return PsAllocString(L"", out);
    case VT_BSTR:
        return PsAllocString(value->bstrVal, out);
    case VT_LPWSTR:
        return value->pwszVal ? PsAllocString(value->pwszVal, out) : E_INVALIDARG;
    }
    PROPVARIANT converted;
    HRESULT hr = PropVariantChangeType(&converted, value, PVCHF_DEFAULT, VT_LPWSTR);
    if (SUCCEEDED(hr))
        *out = converted.pwszVal;
    return hr;
}

HRESULT WINAPI PropVariantToString(const PROPVARIANT *value, LPWSTR buffer, UINT length)
{
    *buffer = 0;
    switch (value->vt) {
    case VT_EMPTY: case VT_NULL:
        return PsCopyString(buffer, length, L"");
    case VT_BSTR:
        return PsCopyString(buffer, length, value->bstrVal);
    case VT_LPWSTR:
        return PsCopyString(buffer, length, value->pwszVal);
    }
    LPWSTR text;
    HRESULT hr = PropVariantToStringAlloc(value, &text);
    if (SUCCEEDED(hr)) {
        hr = PsCopyString(buffer, length, text);
        CoTaskMemFree(text);
    }
    return hr;
}

LPCWSTR WINAPI PropVariantToStringWithDefault(const PROPVARIANT *value, LPCWSTR fallback)
{
    if (value->vt == VT_LPWSTR && value->pwszVal)
        return value->pwszVal;
    if (value->vt == VT_BSTR)
        return value->bstrVal ? value->bstrVal : L"";
    return fallback;
}

HRESULT WINAPI PropVariantToBSTR(const PROPVARIANT *value, BSTR *out)
{
    *out = nullptr;
    LPWSTR text;
    HRESULT hr = PropVariantToStringAlloc(value, &text);
    if (FAILED(hr))
        return hr;
    *out = SysAllocString(text);
    CoTaskMemFree(text);
    return *out ? S_OK : E_OUTOFMEMORY;
}

HRESULT WINAPI PropVariantToStrRet(const PROPVARIANT *value, STRRET *out)
{
    out->uType = STRRET_WSTR;
    out->pOleStr = nullptr;
    return PropVariantToStringAlloc(value, &out->pOleStr);
}

/* ------------------------------------------------------------------ elements and vectors */

ULONG WINAPI PropVariantGetElementCount(const PROPVARIANT *value)
{
    return PsElementCount(value);
}

static HRESULT get_element(const PROPVARIANT *value, ULONG index, VARTYPE vt, void *out, UINT size)
{
    memset(out, 0, size);
    const void *element;
    HRESULT hr = PsElementPointer(value, index, &element);
    if (FAILED(hr))
        return hr;
    VARTYPE base = value->vt & VT_TYPEMASK;
    if (!(value->vt & (VT_VECTOR | VT_ARRAY | VT_BYREF)) && base != vt)
        return to_scalar(value, vt, out, size);
    if (base == vt && vt != VT_LPWSTR) {
        memcpy(out, element, size);
        return S_OK;
    }
    return PsCoerceElement(base, element, vt, out, PVCHF_DEFAULT);
}

#define GET_ELEMENT(name, ctype, kind)                                                                                 \
    HRESULT WINAPI PropVariantGet##name##Elem(const PROPVARIANT *value, ULONG index, ctype *out)                       \
    {                                                                                                                  \
        return get_element(value, index, kind, out, sizeof(ctype));                                                    \
    }
GET_ELEMENT(Int16, SHORT, VT_I2)
GET_ELEMENT(UInt16, USHORT, VT_UI2)
GET_ELEMENT(Int32, LONG, VT_I4)
GET_ELEMENT(UInt32, ULONG, VT_UI4)
GET_ELEMENT(Int64, LONGLONG, VT_I8)
GET_ELEMENT(UInt64, ULONGLONG, VT_UI8)
GET_ELEMENT(Double, DOUBLE, VT_R8)
GET_ELEMENT(FileTime, FILETIME, VT_FILETIME)
GET_ELEMENT(String, LPWSTR, VT_LPWSTR)

HRESULT WINAPI PropVariantGetBooleanElem(const PROPVARIANT *value, ULONG index, BOOL *out)
{
    VARIANT_BOOL result;
    HRESULT hr = get_element(value, index, VT_BOOL, &result, sizeof(result));
    *out = result == VARIANT_TRUE;
    return hr;
}

/* a scalar is accepted only when the caller takes one. */
static HRESULT as_vector(const PROPVARIANT *value, VARTYPE base, PROPVARIANT *vector)
{
    memset(vector, 0, sizeof(*vector));
    if (!(value->vt & (VT_VECTOR | VT_ARRAY)))
        return E_INVALIDARG;
    return PropVariantChangeType(vector, value, PVCHF_DEFAULT, VT_VECTOR | base);
}

static HRESULT to_vector(const PROPVARIANT *value, VARTYPE base, void *out, ULONG capacity, ULONG *count)
{
    *count = 0;
    PROPVARIANT vector;
    HRESULT hr = as_vector(value, base, &vector);
    if (FAILED(hr))
        return hr;
    if (vector.cac.cElems > capacity) {
        PropVariantClear(&vector);
        return TYPE_E_BUFFERTOOSMALL;
    }
    UINT size = PsElementSize(base);
    memcpy(out, vector.cac.pElems, (SIZE_T)vector.cac.cElems * size);
    *count = vector.cac.cElems;
    if (base == VT_LPWSTR)
        vector.cac.cElems = 0;
    PropVariantClear(&vector);
    return S_OK;
}

static HRESULT to_vector_alloc(const PROPVARIANT *value, VARTYPE base, void **out, ULONG *count)
{
    *out = nullptr;
    *count = 0;
    PROPVARIANT vector;
    HRESULT hr = as_vector(value, base, &vector);
    if (FAILED(hr))
        return hr;
    *out = vector.cac.pElems;
    *count = vector.cac.cElems;
    return S_OK;
}

#define TO_VECTOR(name, ctype, base)                                                                                   \
    HRESULT WINAPI PropVariantTo##name##Vector(const PROPVARIANT *value, ctype *out, ULONG capacity, ULONG *count)     \
    {                                                                                                                  \
        return to_vector(value, base, out, capacity, count);                                                           \
    }                                                                                                                  \
    HRESULT WINAPI PropVariantTo##name##VectorAlloc(const PROPVARIANT *value, ctype **out, ULONG *count)               \
    {                                                                                                                  \
        return to_vector_alloc(value, base, (void **)out, count);                                                      \
    }
TO_VECTOR(Int16, SHORT, VT_I2)
TO_VECTOR(UInt16, USHORT, VT_UI2)
TO_VECTOR(Int32, LONG, VT_I4)
TO_VECTOR(UInt32, ULONG, VT_UI4)
TO_VECTOR(Int64, LONGLONG, VT_I8)
TO_VECTOR(UInt64, ULONGLONG, VT_UI8)
TO_VECTOR(Double, DOUBLE, VT_R8)
TO_VECTOR(FileTime, FILETIME, VT_FILETIME)
TO_VECTOR(String, LPWSTR, VT_LPWSTR)

HRESULT WINAPI PropVariantToBooleanVector(const PROPVARIANT *value, BOOL *out, ULONG capacity, ULONG *count)
{
    *count = 0;
    PROPVARIANT vector;
    HRESULT hr = as_vector(value, VT_BOOL, &vector);
    if (FAILED(hr))
        return hr;
    if (vector.cabool.cElems > capacity) {
        PropVariantClear(&vector);
        return TYPE_E_BUFFERTOOSMALL;
    }
    for (ULONG i = 0; i < vector.cabool.cElems; i++)
        out[i] = vector.cabool.pElems[i] == VARIANT_TRUE;
    *count = vector.cabool.cElems;
    PropVariantClear(&vector);
    return S_OK;
}

HRESULT WINAPI PropVariantToBooleanVectorAlloc(const PROPVARIANT *value, BOOL **out, ULONG *count)
{
    *out = nullptr;
    *count = 0;
    PROPVARIANT vector;
    HRESULT hr = as_vector(value, VT_BOOL, &vector);
    if (FAILED(hr))
        return hr;
    BOOL *items = PsAllocZero((SIZE_T)vector.cabool.cElems * sizeof(BOOL));
    if (!items) {
        PropVariantClear(&vector);
        return E_OUTOFMEMORY;
    }
    for (ULONG i = 0; i < vector.cabool.cElems; i++)
        items[i] = vector.cabool.pElems[i] == VARIANT_TRUE;
    *out = items;
    *count = vector.cabool.cElems;
    PropVariantClear(&vector);
    return S_OK;
}
