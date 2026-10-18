/* VARIANTs: the automation half of the helpers, and moving values between the two kinds. */
#include "propsys_private.h"

HRESULT PsStrRetToString(STRRET *strret, PCUITEMID_CHILD pidl, LPWSTR *out);

static BOOL automation_scalar(VARTYPE vt)
{
    switch (vt) {
    case VT_EMPTY: case VT_NULL: case VT_I2: case VT_I4: case VT_R4: case VT_R8: case VT_CY: case VT_DATE:
    case VT_BSTR: case VT_DISPATCH: case VT_ERROR: case VT_BOOL: case VT_UNKNOWN: case VT_DECIMAL: case VT_I1:
    case VT_UI1: case VT_UI2: case VT_UI4: case VT_I8: case VT_UI8: case VT_INT: case VT_UINT:
        return TRUE;
    }
    return FALSE;
}

static HRESULT filetime_date(const FILETIME *time, DATE *date)
{
    SYSTEMTIME st;
    if (!FileTimeToSystemTime(time, &st) || !SystemTimeToVariantTime(&st, date))
        return E_INVALIDARG;
    return S_OK;
}

static HRESULT date_filetime(DATE date, FILETIME *time)
{
    SYSTEMTIME st;
    if (!VariantTimeToSystemTime(date, &st) || !SystemTimeToFileTime(&st, time))
        return E_INVALIDARG;
    return S_OK;
}

/* a vector element as the automation type it becomes. */
static VARTYPE automation_base(VARTYPE base)
{
    switch (base) {
    case VT_LPWSTR: case VT_LPSTR: case VT_CLSID:
        return VT_BSTR;
    case VT_FILETIME:
        return VT_DATE;
    }
    return base;
}

static HRESULT vector_to_array(const PROPVARIANT *source, VARIANT *dest)
{
    VARTYPE base = source->vt & VT_TYPEMASK, target = automation_base(base);
    if (!automation_scalar(target) && target != VT_VARIANT)
        return E_INVALIDARG;
    SAFEARRAY *array = SafeArrayCreateVector(target, 0, source->cac.cElems);
    if (!array)
        return E_OUTOFMEMORY;
    HRESULT hr = S_OK;
    UINT size = PsElementSize(base);
    for (ULONG i = 0; i < source->cac.cElems && SUCCEEDED(hr); i++) {
        const BYTE *element = (const BYTE *)source->cac.pElems + (SIZE_T)i * size;
        LONG index = (LONG)i;
        if (base == VT_VARIANT) {
            VARIANT item;
            hr = PropVariantToVariant((const PROPVARIANT *)element, &item);
            if (SUCCEEDED(hr)) {
                hr = SafeArrayPutElement(array, &index, &item);
                VariantClear(&item);
            }
        } else if (target == VT_BSTR) {
            LPWSTR text = nullptr;
            if (base == VT_LPWSTR)
                hr = PsAllocString(*(LPCWSTR const *)element, &text);
            else if (base == VT_LPSTR)
                hr = PsAnsiToWideAlloc(*(LPCSTR const *)element, &text);
            else if (base == VT_CLSID) {
                WCHAR guid[40];
                StringFromGUID2((const GUID *)element, guid, 40);
                hr = PsAllocString(guid, &text);
            } else
                hr = PsAllocString(*(BSTR const *)element, &text);
            if (SUCCEEDED(hr)) {
                BSTR string = SysAllocString(text);
                hr = string ? SafeArrayPutElement(array, &index, string) : E_OUTOFMEMORY;
                SysFreeString(string);
                CoTaskMemFree(text);
            }
        } else if (target == VT_DATE) {
            DATE date;
            hr = filetime_date((const FILETIME *)element, &date);
            if (SUCCEEDED(hr))
                hr = SafeArrayPutElement(array, &index, &date);
        } else {
            hr = SafeArrayPutElement(array, &index, (void *)element);
        }
    }
    if (FAILED(hr)) {
        SafeArrayDestroy(array);
        return hr;
    }
    dest->vt = VT_ARRAY | target;
    dest->parray = array;
    return S_OK;
}

HRESULT WINAPI PropVariantToVariant(const PROPVARIANT *source, VARIANT *dest)
{
    memset(dest, 0, sizeof(*dest));
    VARTYPE vt = source->vt;
    if (automation_scalar(vt) || (vt & (VT_ARRAY | VT_BYREF)))
        return PropVariantCopy((PROPVARIANT *)dest, source);
    if (vt & VT_VECTOR)
        return vector_to_array(source, dest);
    LPWSTR text = nullptr;
    HRESULT hr;
    switch (vt) {
    case VT_LPWSTR: case VT_LPSTR: case VT_CLSID:
        hr = PropVariantToStringAlloc(source, &text);
        if (SUCCEEDED(hr)) {
            dest->bstrVal = SysAllocString(text);
            CoTaskMemFree(text);
            if (!dest->bstrVal)
                return E_OUTOFMEMORY;
            dest->vt = VT_BSTR;
        }
        return hr;
    case VT_FILETIME:
        hr = filetime_date(&source->filetime, &dest->date);
        if (SUCCEEDED(hr))
            dest->vt = VT_DATE;
        return hr;
    case VT_BLOB:
        return InitVariantFromBuffer(source->blob.pBlobData, source->blob.cbSize, dest);
    case VT_STREAM: case VT_STORAGE: case VT_STREAMED_OBJECT: case VT_STORED_OBJECT:
        dest->vt = VT_UNKNOWN;
        dest->punkVal = source->punkVal;
        if (dest->punkVal)
            dest->punkVal->lpVtbl->AddRef(dest->punkVal);
        return S_OK;
    }
    return E_INVALIDARG;
}

HRESULT WINAPI VariantToPropVariant(const VARIANT *source, PROPVARIANT *dest)
{
    memset(dest, 0, sizeof(*dest));
    return PropVariantCopy(dest, (const PROPVARIANT *)source);
}

/* ------------------------------------------------------------------ init */

static HRESULT init_array(const void *values, ULONG count, VARTYPE vt, VARIANT *value)
{
    memset(value, 0, sizeof(*value));
    SAFEARRAY *array = SafeArrayCreateVector(vt, 0, count);
    if (!array)
        return E_OUTOFMEMORY;
    void *data;
    HRESULT hr = SafeArrayAccessData(array, &data);
    if (FAILED(hr)) {
        SafeArrayDestroy(array);
        return hr;
    }
    if (values && count)
        memcpy(data, values, (SIZE_T)count * PsElementSize(vt));
    SafeArrayUnaccessData(array);
    value->vt = VT_ARRAY | vt;
    value->parray = array;
    return S_OK;
}

#define INIT_ARRAY(name, ctype, vt)                                                                                    \
    HRESULT WINAPI InitVariantFrom##name##Array(const ctype *values, ULONG count, VARIANT *value)                      \
    {                                                                                                                  \
        return init_array(values, count, vt, value);                                                                   \
    }
INIT_ARRAY(Int16, SHORT, VT_I2)
INIT_ARRAY(UInt16, USHORT, VT_UI2)
INIT_ARRAY(Int32, LONG, VT_I4)
INIT_ARRAY(UInt32, ULONG, VT_UI4)
INIT_ARRAY(Int64, LONGLONG, VT_I8)
INIT_ARRAY(UInt64, ULONGLONG, VT_UI8)
INIT_ARRAY(Double, DOUBLE, VT_R8)

HRESULT WINAPI InitVariantFromBuffer(const void *buffer, UINT size, VARIANT *value)
{
    return init_array(buffer, size, VT_UI1, value);
}

HRESULT WINAPI InitVariantFromBooleanArray(const BOOL *values, ULONG count, VARIANT *value)
{
    HRESULT hr = init_array(nullptr, count, VT_BOOL, value);
    VARIANT_BOOL *data;
    if (SUCCEEDED(hr) && SUCCEEDED(SafeArrayAccessData(value->parray, (void **)&data))) {
        for (ULONG i = 0; i < count; i++)
            data[i] = values[i] ? VARIANT_TRUE : VARIANT_FALSE;
        SafeArrayUnaccessData(value->parray);
    }
    return hr;
}

HRESULT WINAPI InitVariantFromFileTimeArray(const FILETIME *values, ULONG count, VARIANT *value)
{
    HRESULT hr = init_array(nullptr, count, VT_DATE, value);
    DATE *data;
    if (SUCCEEDED(hr) && SUCCEEDED(SafeArrayAccessData(value->parray, (void **)&data))) {
        for (ULONG i = 0; i < count && SUCCEEDED(hr); i++)
            hr = filetime_date(&values[i], &data[i]);
        SafeArrayUnaccessData(value->parray);
        if (FAILED(hr))
            VariantClear(value);
    }
    return hr;
}

HRESULT WINAPI InitVariantFromStringArray(LPCWSTR *values, ULONG count, VARIANT *value)
{
    HRESULT hr = init_array(nullptr, count, VT_BSTR, value);
    for (ULONG i = 0; SUCCEEDED(hr) && i < count; i++) {
        LONG index = (LONG)i;
        BSTR string = SysAllocString(values[i] ? values[i] : L"");
        hr = string ? SafeArrayPutElement(value->parray, &index, string) : E_OUTOFMEMORY;
        SysFreeString(string);
        if (FAILED(hr))
            VariantClear(value);
    }
    return hr;
}

static HRESULT init_bstr(LPCWSTR text, VARIANT *value)
{
    memset(value, 0, sizeof(*value));
    value->bstrVal = SysAllocString(text);
    if (!value->bstrVal)
        return E_OUTOFMEMORY;
    value->vt = VT_BSTR;
    return S_OK;
}

HRESULT WINAPI InitVariantFromGUIDAsString(REFGUID guid, VARIANT *value)
{
    WCHAR text[40];
    if (!StringFromGUID2(guid, text, 39)) {
        memset(value, 0, sizeof(*value));
        return E_FAIL;
    }
    return init_bstr(text, value);
}

HRESULT WINAPI InitVariantFromFileTime(const FILETIME *time, VARIANT *value)
{
    memset(value, 0, sizeof(*value));
    HRESULT hr = filetime_date(time, &value->date);
    if (SUCCEEDED(hr))
        value->vt = VT_DATE;
    return hr;
}

HRESULT WINAPI InitVariantFromResource(HINSTANCE instance, UINT id, VARIANT *value)
{
    WCHAR text[1024];
    memset(value, 0, sizeof(*value));
    if (!LoadStringW(instance, id, text, 1024)) {
        DWORD error = GetLastError();
        return error ? HRESULT_FROM_WIN32(error) : HRESULT_FROM_WIN32(ERROR_RESOURCE_NAME_NOT_FOUND);
    }
    return init_bstr(text, value);
}

HRESULT WINAPI InitVariantFromStrRet(STRRET *strret, PCUITEMID_CHILD pidl, VARIANT *value)
{
    memset(value, 0, sizeof(*value));
    LPWSTR text;
    HRESULT hr = PsStrRetToString(strret, pidl, &text);
    if (SUCCEEDED(hr)) {
        hr = init_bstr(text, value);
        CoTaskMemFree(text);
    }
    return hr;
}

HRESULT WINAPI InitVariantFromVariantArrayElem(const VARIANT *source, ULONG index, VARIANT *value)
{
    PROPVARIANT element;
    memset(value, 0, sizeof(*value));
    HRESULT hr = InitPropVariantFromPropVariantVectorElem((const PROPVARIANT *)source, index, &element);
    if (SUCCEEDED(hr)) {
        hr = PropVariantToVariant(&element, value);
        PropVariantClear(&element);
    }
    return hr;
}

/* ------------------------------------------------------------------ reading */

#define P(value) ((const PROPVARIANT *)(value))

ULONG WINAPI VariantGetElementCount(const VARIANT *value)
{
    return PsElementCount(P(value));
}

#define VARIANT_TO(name, ctype)                                                                                        \
    HRESULT WINAPI VariantTo##name(const VARIANT *value, ctype *out)                                                   \
    {                                                                                                                  \
        return PropVariantTo##name(P(value), out);                                                                     \
    }                                                                                                                  \
    ctype WINAPI VariantTo##name##WithDefault(const VARIANT *value, ctype fallback)                                    \
    {                                                                                                                  \
        return PropVariantTo##name##WithDefault(P(value), fallback);                                                   \
    }                                                                                                                  \
    HRESULT WINAPI VariantGet##name##Elem(const VARIANT *value, ULONG index, ctype *out)                               \
    {                                                                                                                  \
        return PropVariantGet##name##Elem(P(value), index, out);                                                       \
    }                                                                                                                  \
    HRESULT WINAPI VariantTo##name##Array(const VARIANT *value, ctype *out, ULONG capacity, ULONG *count)              \
    {                                                                                                                  \
        return PropVariantTo##name##Vector(P(value), out, capacity, count);                                            \
    }                                                                                                                  \
    HRESULT WINAPI VariantTo##name##ArrayAlloc(const VARIANT *value, ctype **out, ULONG *count)                        \
    {                                                                                                                  \
        return PropVariantTo##name##VectorAlloc(P(value), out, count);                                                 \
    }

HRESULT WINAPI PropVariantToInt16Vector(const PROPVARIANT *, SHORT *, ULONG, ULONG *);
HRESULT WINAPI PropVariantToInt16VectorAlloc(const PROPVARIANT *, SHORT **, ULONG *);
HRESULT WINAPI PropVariantToUInt16Vector(const PROPVARIANT *, USHORT *, ULONG, ULONG *);
HRESULT WINAPI PropVariantToUInt16VectorAlloc(const PROPVARIANT *, USHORT **, ULONG *);
HRESULT WINAPI PropVariantToUInt32Vector(const PROPVARIANT *, ULONG *, ULONG, ULONG *);
HRESULT WINAPI PropVariantToUInt32VectorAlloc(const PROPVARIANT *, ULONG **, ULONG *);
HRESULT WINAPI PropVariantToInt64Vector(const PROPVARIANT *, LONGLONG *, ULONG, ULONG *);
HRESULT WINAPI PropVariantToInt64VectorAlloc(const PROPVARIANT *, LONGLONG **, ULONG *);
HRESULT WINAPI PropVariantToUInt64Vector(const PROPVARIANT *, ULONGLONG *, ULONG, ULONG *);
HRESULT WINAPI PropVariantToUInt64VectorAlloc(const PROPVARIANT *, ULONGLONG **, ULONG *);
HRESULT WINAPI PropVariantToDoubleVector(const PROPVARIANT *, DOUBLE *, ULONG, ULONG *);
HRESULT WINAPI PropVariantToDoubleVectorAlloc(const PROPVARIANT *, DOUBLE **, ULONG *);
HRESULT WINAPI PropVariantToBooleanVector(const PROPVARIANT *, BOOL *, ULONG, ULONG *);
HRESULT WINAPI PropVariantToBooleanVectorAlloc(const PROPVARIANT *, BOOL **, ULONG *);
HRESULT WINAPI PropVariantToStringVector(const PROPVARIANT *, LPWSTR *, ULONG, ULONG *);
HRESULT WINAPI PropVariantGetInt16Elem(const PROPVARIANT *, ULONG, SHORT *);
HRESULT WINAPI PropVariantGetUInt16Elem(const PROPVARIANT *, ULONG, USHORT *);
HRESULT WINAPI PropVariantGetUInt32Elem(const PROPVARIANT *, ULONG, ULONG *);
HRESULT WINAPI PropVariantGetInt64Elem(const PROPVARIANT *, ULONG, LONGLONG *);
HRESULT WINAPI PropVariantGetUInt64Elem(const PROPVARIANT *, ULONG, ULONGLONG *);
HRESULT WINAPI PropVariantGetDoubleElem(const PROPVARIANT *, ULONG, DOUBLE *);
HRESULT WINAPI PropVariantGetBooleanElem(const PROPVARIANT *, ULONG, BOOL *);

VARIANT_TO(Int16, SHORT)
VARIANT_TO(UInt16, USHORT)
VARIANT_TO(Int32, LONG)
VARIANT_TO(UInt32, ULONG)
VARIANT_TO(Int64, LONGLONG)
VARIANT_TO(UInt64, ULONGLONG)
VARIANT_TO(Double, DOUBLE)
VARIANT_TO(Boolean, BOOL)

HRESULT WINAPI VariantGetStringElem(const VARIANT *value, ULONG index, LPWSTR *out)
{
    return PropVariantGetStringElem(P(value), index, out);
}

HRESULT WINAPI VariantToStringArray(const VARIANT *value, LPWSTR *out, ULONG capacity, ULONG *count)
{
    return PropVariantToStringVector(P(value), out, capacity, count);
}

HRESULT WINAPI VariantToStringArrayAlloc(const VARIANT *value, LPWSTR **out, ULONG *count)
{
    return PropVariantToStringVectorAlloc(P(value), out, count);
}

HRESULT WINAPI VariantToString(const VARIANT *value, LPWSTR buffer, UINT length)
{
    return PropVariantToString(P(value), buffer, length);
}

HRESULT WINAPI VariantToStringAlloc(const VARIANT *value, LPWSTR *out)
{
    return PropVariantToStringAlloc(P(value), out);
}

LPCWSTR WINAPI VariantToStringWithDefault(const VARIANT *value, LPCWSTR fallback)
{
    if (value->vt == VT_BSTR)
        return value->bstrVal ? value->bstrVal : L"";
    return fallback;
}

HRESULT WINAPI VariantToStrRet(const VARIANT *value, STRRET *out)
{
    return PropVariantToStrRet(P(value), out);
}

HRESULT WINAPI VariantToGUID(const VARIANT *value, GUID *out)
{
    memset(out, 0, sizeof(*out));
    switch (value->vt) {
    case VT_EMPTY: case VT_NULL:
        return S_OK;
    case VT_BSTR:
        return PsGuidFromString(value->bstrVal ? value->bstrVal : L"", out) ? S_OK : E_INVALIDARG;
    case VT_ARRAY | VT_UI1:
        return VariantToBuffer(value, out, sizeof(*out));
    }
    return E_INVALIDARG;
}

HRESULT WINAPI VariantToBuffer(const VARIANT *value, void *buffer, UINT size)
{
    if (value->vt != (VT_ARRAY | VT_UI1))
        return E_INVALIDARG;
    if (PsElementCount(P(value)) < size)
        return E_FAIL;
    void *data;
    HRESULT hr = SafeArrayAccessData(value->parray, &data);
    if (FAILED(hr))
        return hr;
    memcpy(buffer, data, size);
    SafeArrayUnaccessData(value->parray);
    return S_OK;
}

HRESULT WINAPI VariantToFileTime(const VARIANT *value, DWORD flags, FILETIME *out)
{
    memset(out, 0, sizeof(*out));
    if (value->vt != VT_DATE)
        return PropVariantToFileTime(P(value), flags, out);
    HRESULT hr = date_filetime(value->date, out);
    if (SUCCEEDED(hr) && (flags & 1)) {
        FILETIME local;
        if (!FileTimeToLocalFileTime(out, &local))
            return HRESULT_FROM_WIN32(GetLastError());
        *out = local;
    }
    return hr;
}

HRESULT WINAPI VariantToDosDateTime(const VARIANT *value, WORD *date, WORD *time)
{
    *date = *time = 0;
    FILETIME ft;
    HRESULT hr = VariantToFileTime(value, 0, &ft);
    if (FAILED(hr))
        return hr;
    return FileTimeToDosDateTime(&ft, date, time) ? S_OK : E_INVALIDARG;
}
