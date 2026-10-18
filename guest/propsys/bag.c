/* PSPropertyBag_*: typed reads and writes over any IPropertyBag. */
#include "propsys_private.h"

typedef struct {
    LONG x, y;
} BAG_POINTL;
typedef struct {
    SHORT x, y;
} BAG_POINTS;
typedef struct {
    LONG left, top, right, bottom;
} BAG_RECTL;

/* the value as type vt, whatever type the bag gave back. */
static HRESULT read_type(IPropertyBag *bag, LPCWSTR name, VARIANT *value, VARTYPE vt)
{
    memset(value, 0, sizeof(*value));
    if (!bag || !name)
        return E_INVALIDARG;
    value->vt = vt;
    HRESULT hr = bag->lpVtbl->Read(bag, name, value, nullptr);
    if (FAILED(hr)) {
        memset(value, 0, sizeof(*value));
        return hr;
    }
    if (vt == VT_EMPTY || value->vt == vt)
        return hr;
    PROPVARIANT converted;
    hr = PropVariantChangeType(&converted, (const PROPVARIANT *)value, PVCHF_DEFAULT, vt);
    VariantClear(value);
    if (FAILED(hr))
        return hr;
    hr = PropVariantToVariant(&converted, value);
    PropVariantClear(&converted);
    return hr;
}

static HRESULT write_variant(IPropertyBag *bag, LPCWSTR name, VARIANT *value)
{
    if (!bag || !name)
        return E_INVALIDARG;
    return bag->lpVtbl->Write(bag, name, value);
}

HRESULT WINAPI PSPropertyBag_ReadType(IPropertyBag *bag, LPCWSTR name, VARIANT *value, VARTYPE vt)
{
    return read_type(bag, name, value, vt);
}

HRESULT WINAPI PSPropertyBag_Delete(IPropertyBag *bag, LPCWSTR name)
{
    VARIANT empty = {0};
    return write_variant(bag, name, &empty);
}

#define BAG_SCALAR(name, ctype, kind, field)                                                                           \
    HRESULT WINAPI PSPropertyBag_Read##name(IPropertyBag *bag, LPCWSTR prop, ctype *out)                               \
    {                                                                                                                  \
        VARIANT value;                                                                                                 \
        HRESULT hr = read_type(bag, prop, &value, kind);                                                               \
        if (SUCCEEDED(hr))                                                                                             \
            *out = (ctype)value.field;                                                                                 \
        return hr;                                                                                                     \
    }                                                                                                                  \
    HRESULT WINAPI PSPropertyBag_Write##name(IPropertyBag *bag, LPCWSTR prop, ctype in)                                \
    {                                                                                                                  \
        VARIANT value = {0};                                                                                           \
        value.vt = kind;                                                                                               \
        value.field = in;                                                                                              \
        return write_variant(bag, prop, &value);                                                                       \
    }
BAG_SCALAR(Int, INT, VT_I4, lVal)
BAG_SCALAR(SHORT, SHORT, VT_I2, iVal)
BAG_SCALAR(LONG, LONG, VT_I4, lVal)
BAG_SCALAR(DWORD, DWORD, VT_UI4, ulVal)
BAG_SCALAR(ULONGLONG, ULONGLONG, VT_UI8, ullVal)

HRESULT WINAPI PSPropertyBag_ReadBOOL(IPropertyBag *bag, LPCWSTR name, BOOL *out)
{
    VARIANT value;
    HRESULT hr = read_type(bag, name, &value, VT_BOOL);
    if (SUCCEEDED(hr))
        *out = value.boolVal != VARIANT_FALSE;
    return hr;
}

HRESULT WINAPI PSPropertyBag_WriteBOOL(IPropertyBag *bag, LPCWSTR name, BOOL in)
{
    VARIANT value = {0};
    value.vt = VT_BOOL;
    value.boolVal = in ? VARIANT_TRUE : VARIANT_FALSE;
    return write_variant(bag, name, &value);
}

HRESULT WINAPI PSPropertyBag_ReadBSTR(IPropertyBag *bag, LPCWSTR name, BSTR *out)
{
    *out = nullptr;
    VARIANT value;
    HRESULT hr = read_type(bag, name, &value, VT_BSTR);
    if (SUCCEEDED(hr))
        *out = value.bstrVal;
    return hr;
}

HRESULT WINAPI PSPropertyBag_WriteBSTR(IPropertyBag *bag, LPCWSTR name, BSTR in)
{
    VARIANT value = {0};
    value.vt = VT_BSTR;
    value.bstrVal = in;
    return write_variant(bag, name, &value);
}

HRESULT WINAPI PSPropertyBag_ReadStr(IPropertyBag *bag, LPCWSTR name, LPWSTR buffer, int length)
{
    VARIANT value;
    HRESULT hr = read_type(bag, name, &value, VT_BSTR);
    if (SUCCEEDED(hr)) {
        hr = PsCopyString(buffer, (UINT)length, value.bstrVal);
        VariantClear(&value);
    }
    return hr;
}

HRESULT WINAPI PSPropertyBag_ReadStrAlloc(IPropertyBag *bag, LPCWSTR name, LPWSTR *out)
{
    *out = nullptr;
    VARIANT value;
    HRESULT hr = read_type(bag, name, &value, VT_BSTR);
    if (SUCCEEDED(hr)) {
        hr = PsAllocString(value.bstrVal, out);
        VariantClear(&value);
    }
    return hr;
}

HRESULT WINAPI PSPropertyBag_WriteStr(IPropertyBag *bag, LPCWSTR name, LPCWSTR in)
{
    VARIANT value = {0};
    value.vt = VT_BSTR;
    value.bstrVal = SysAllocString(in ? in : L"");
    if (!value.bstrVal)
        return E_OUTOFMEMORY;
    HRESULT hr = write_variant(bag, name, &value);
    VariantClear(&value);
    return hr;
}

HRESULT WINAPI PSPropertyBag_ReadGUID(IPropertyBag *bag, LPCWSTR name, GUID *out)
{
    WCHAR text[40];
    memset(out, 0, sizeof(*out));
    HRESULT hr = PSPropertyBag_ReadStr(bag, name, text, 40);
    if (SUCCEEDED(hr) && !PsGuidFromString(text, out))
        hr = E_INVALIDARG;
    return hr;
}

HRESULT WINAPI PSPropertyBag_WriteGUID(IPropertyBag *bag, LPCWSTR name, const GUID *in)
{
    WCHAR text[40];
    StringFromGUID2(in, text, 40);
    return PSPropertyBag_WriteStr(bag, name, text);
}

HRESULT WINAPI PSPropertyBag_ReadPropertyKey(IPropertyBag *bag, LPCWSTR name, PROPERTYKEY *out)
{
    WCHAR text[PKEYSTR_MAX];
    memset(out, 0, sizeof(*out));
    HRESULT hr = PSPropertyBag_ReadStr(bag, name, text, PKEYSTR_MAX);
    if (SUCCEEDED(hr))
        hr = PSPropertyKeyFromString(text, out);
    return hr;
}

HRESULT WINAPI PSPropertyBag_WritePropertyKey(IPropertyBag *bag, LPCWSTR name, REFPROPERTYKEY in)
{
    WCHAR text[PKEYSTR_MAX];
    HRESULT hr = PSStringFromPropertyKey(in, text, PKEYSTR_MAX);
    if (SUCCEEDED(hr))
        hr = PSPropertyBag_WriteStr(bag, name, text);
    return hr;
}

/* compound values are one property per field, "name.field". */
static HRESULT field_name(LPCWSTR name, LPCWSTR field, WCHAR *out)
{
    SIZE_T a = wcslen(name), b = wcslen(field);
    if (a + b + 1 >= 260)
        return E_INVALIDARG;
    memcpy(out, name, a * sizeof(WCHAR));
    memcpy(out + a, field, (b + 1) * sizeof(WCHAR));
    return S_OK;
}

static HRESULT read_fields(IPropertyBag *bag, LPCWSTR name, const LPCWSTR *fields, UINT count, LONG *values)
{
    WCHAR full[260];
    for (UINT i = 0; i < count; i++) {
        HRESULT hr = field_name(name, fields[i], full);
        if (SUCCEEDED(hr))
            hr = PSPropertyBag_ReadLONG(bag, full, &values[i]);
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

static HRESULT write_fields(IPropertyBag *bag, LPCWSTR name, const LPCWSTR *fields, UINT count, const LONG *values)
{
    WCHAR full[260];
    for (UINT i = 0; i < count; i++) {
        HRESULT hr = field_name(name, fields[i], full);
        if (SUCCEEDED(hr))
            hr = PSPropertyBag_WriteLONG(bag, full, values[i]);
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

static const LPCWSTR point_fields[] = {L".x", L".y"};
static const LPCWSTR rect_fields[] = {L".left", L".top", L".right", L".bottom"};

HRESULT WINAPI PSPropertyBag_ReadPOINTL(IPropertyBag *bag, LPCWSTR name, BAG_POINTL *out)
{
    LONG values[2];
    HRESULT hr = read_fields(bag, name, point_fields, 2, values);
    if (SUCCEEDED(hr))
        out->x = values[0], out->y = values[1];
    return hr;
}

HRESULT WINAPI PSPropertyBag_WritePOINTL(IPropertyBag *bag, LPCWSTR name, const BAG_POINTL *in)
{
    LONG values[2] = {in->x, in->y};
    return write_fields(bag, name, point_fields, 2, values);
}

HRESULT WINAPI PSPropertyBag_ReadPOINTS(IPropertyBag *bag, LPCWSTR name, BAG_POINTS *out)
{
    LONG values[2];
    HRESULT hr = read_fields(bag, name, point_fields, 2, values);
    if (SUCCEEDED(hr))
        out->x = (SHORT)values[0], out->y = (SHORT)values[1];
    return hr;
}

HRESULT WINAPI PSPropertyBag_WritePOINTS(IPropertyBag *bag, LPCWSTR name, const BAG_POINTS *in)
{
    LONG values[2] = {in->x, in->y};
    return write_fields(bag, name, point_fields, 2, values);
}

HRESULT WINAPI PSPropertyBag_ReadRECTL(IPropertyBag *bag, LPCWSTR name, BAG_RECTL *out)
{
    LONG values[4];
    HRESULT hr = read_fields(bag, name, rect_fields, 4, values);
    if (SUCCEEDED(hr))
        out->left = values[0], out->top = values[1], out->right = values[2], out->bottom = values[3];
    return hr;
}

HRESULT WINAPI PSPropertyBag_WriteRECTL(IPropertyBag *bag, LPCWSTR name, const BAG_RECTL *in)
{
    LONG values[4] = {in->left, in->top, in->right, in->bottom};
    return write_fields(bag, name, rect_fields, 4, values);
}

HRESULT WINAPI PSPropertyBag_ReadUnknown(IPropertyBag *bag, LPCWSTR name, REFIID riid, void **out)
{
    *out = nullptr;
    VARIANT value;
    HRESULT hr = read_type(bag, name, &value, VT_UNKNOWN);
    if (FAILED(hr))
        return hr;
    hr = value.punkVal ? value.punkVal->lpVtbl->QueryInterface(value.punkVal, riid, out) : E_FAIL;
    VariantClear(&value);
    return hr;
}

HRESULT WINAPI PSPropertyBag_WriteUnknown(IPropertyBag *bag, LPCWSTR name, IUnknown *in)
{
    VARIANT value = {0};
    value.vt = VT_UNKNOWN;
    value.punkVal = in;
    return write_variant(bag, name, &value);
}

HRESULT WINAPI PSPropertyBag_ReadStream(IPropertyBag *bag, LPCWSTR name, IStream **out)
{
    return PSPropertyBag_ReadUnknown(bag, name, &IID_IStream, (void **)out);
}

HRESULT WINAPI PSPropertyBag_WriteStream(IPropertyBag *bag, LPCWSTR name, IStream *in)
{
    return PSPropertyBag_WriteUnknown(bag, name, (IUnknown *)in);
}
