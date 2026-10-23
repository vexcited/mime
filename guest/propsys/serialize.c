/*
 * serialized property values: the TypedPropertyValue of the property set format,
 * with Unicode strings, as StgSerializePropVariant writes them.
 */
#include "propsys_private.h"

typedef struct {
    BYTE *data;
    SIZE_T size, capacity;
    HRESULT status;
} WRITER;

static void put(WRITER *w, const void *bytes, SIZE_T size)
{
    if (FAILED(w->status))
        return;
    if (w->size + size > w->capacity) {
        SIZE_T capacity = w->capacity ? w->capacity * 2 : 64;
        while (capacity < w->size + size)
            capacity *= 2;
        BYTE *grown = CoTaskMemRealloc(w->data, capacity);
        if (!grown) {
            w->status = E_OUTOFMEMORY;
            return;
        }
        w->data = grown;
        w->capacity = capacity;
    }
    if (bytes)
        memcpy(w->data + w->size, bytes, size);
    else
        memset(w->data + w->size, 0, size);
    w->size += size;
}

static void put_dword(WRITER *w, DWORD value)
{
    put(w, &value, 4);
}

static void pad(WRITER *w)
{
    if (w->size & 3)
        put(w, nullptr, 4 - (w->size & 3));
}

static void put_wide(WRITER *w, LPCWSTR text, BOOL counted_in_bytes)
{
    if (!text)
        text = L"";
    DWORD length = (DWORD)wcslen(text) + 1;
    put_dword(w, counted_in_bytes ? length * 2 : length);
    put(w, text, length * sizeof(WCHAR));
    pad(w);
}

static void put_element(WRITER *w, VARTYPE base, const void *element)
{
    switch (base) {
    case VT_I1: case VT_UI1:
        put(w, element, 1);
        break;
    case VT_I2: case VT_UI2: case VT_BOOL:
        put(w, element, 2);
        break;
    case VT_I4: case VT_UI4: case VT_INT: case VT_UINT: case VT_ERROR: case VT_R4:
        put(w, element, 4);
        break;
    case VT_I8: case VT_UI8: case VT_R8: case VT_CY: case VT_DATE: case VT_FILETIME:
        put(w, element, 8);
        break;
    case VT_CLSID:
        put(w, element, 16);
        break;
    case VT_BSTR:
        put_wide(w, *(BSTR const *)element, TRUE);
        break;
    case VT_LPWSTR:
        put_wide(w, *(LPCWSTR const *)element, FALSE);
        break;
    case VT_LPSTR: {
        LPWSTR text;
        w->status = SUCCEEDED(w->status) ? PsAnsiToWideAlloc(*(LPCSTR const *)element, &text) : w->status;
        if (SUCCEEDED(w->status)) {
            put_wide(w, text, TRUE);
            CoTaskMemFree(text);
        }
        break;
    }
    }
}

static BOOL serializable(VARTYPE base, BOOL vector)
{
    switch (base) {
    case VT_I1: case VT_UI1: case VT_I2: case VT_UI2: case VT_BOOL: case VT_I4: case VT_UI4: case VT_ERROR:
    case VT_R4: case VT_I8: case VT_UI8: case VT_R8: case VT_CY: case VT_DATE: case VT_FILETIME: case VT_CLSID:
    case VT_BSTR: case VT_LPWSTR: case VT_LPSTR:
        return TRUE;
    case VT_INT: case VT_UINT: case VT_EMPTY: case VT_NULL: case VT_BLOB:
        return !vector;
    case VT_VARIANT:
        return vector;
    }
    return FALSE;
}

static void put_value(WRITER *w, const PROPVARIANT *value)
{
    VARTYPE vt = value->vt, base = vt & VT_TYPEMASK;
    BOOL vector = (vt & VT_VECTOR) != 0;
    if ((vt & ~(VT_TYPEMASK | VT_VECTOR)) || !serializable(base, vector)) {
        w->status = SUCCEEDED(w->status) ? HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED) : w->status;
        return;
    }
    put_dword(w, vt);
    if (!vector) {
        if (base == VT_BLOB) {
            put_dword(w, value->blob.cbSize);
            put(w, value->blob.pBlobData, value->blob.cbSize);
        } else if (base == VT_CLSID) {
            put(w, value->puuid, 16);
        } else if (base != VT_EMPTY && base != VT_NULL) {
            put_element(w, base, &value->cVal);
        }
        pad(w);
        return;
    }
    put_dword(w, value->cac.cElems);
    UINT size = PsElementSize(base);
    for (ULONG i = 0; i < value->cac.cElems; i++) {
        const BYTE *element = (const BYTE *)value->cac.pElems + (SIZE_T)i * size;
        if (base == VT_VARIANT)
            put_value(w, (const PROPVARIANT *)element);
        else
            put_element(w, base, element);
    }
    pad(w);
}

HRESULT PsSerializeValue(const PROPVARIANT *value, BYTE **out, ULONG *size)
{
    WRITER w = {0};
    put_value(&w, value);
    if (FAILED(w.status)) {
        CoTaskMemFree(w.data);
        return w.status;
    }
    *out = w.data;
    *size = (ULONG)w.size;
    return S_OK;
}

HRESULT WINAPI StgSerializePropVariant(const PROPVARIANT *value, SERIALIZEDPROPERTYVALUE **out, ULONG *size)
{
    if (!out || !size)
        return E_INVALIDARG;
    *out = nullptr;
    return PsSerializeValue(value, (BYTE **)out, size);
}

/* ------------------------------------------------------------------ reading */

typedef struct {
    const BYTE *data;
    ULONG size, offset;
} READER;

static BOOL take(READER *r, void *out, ULONG size)
{
    if (r->size - r->offset < size)
        return FALSE;
    if (out)
        memcpy(out, r->data + r->offset, size);
    r->offset += size;
    return TRUE;
}

static BOOL align(READER *r)
{
    ULONG skip = (4 - (r->offset & 3)) & 3;
    if (r->size - r->offset < skip) {
        r->offset = r->size;
        return TRUE;
    }
    r->offset += skip;
    return TRUE;
}

static HRESULT take_wide(READER *r, BOOL counted_in_bytes, LPWSTR *out)
{
    DWORD count;
    if (!take(r, &count, 4))
        return E_INVALIDARG;
    DWORD bytes = counted_in_bytes ? count : count * 2;
    if ((bytes & 1) || r->size - r->offset < bytes || (!counted_in_bytes && count > 0x7FFFFFFF))
        return E_INVALIDARG;
    const WCHAR *text = (const WCHAR *)(r->data + r->offset);
    SIZE_T length = bytes / 2;
    while (length && !text[length - 1])
        length--;
    HRESULT hr = PsAllocStringLen(text, length, out);
    r->offset += bytes;
    align(r);
    return hr;
}

static HRESULT take_element(READER *r, VARTYPE base, void *out)
{
    UINT size;
    switch (base) {
    case VT_BSTR: case VT_LPSTR: case VT_LPWSTR: {
        LPWSTR text;
        HRESULT hr = take_wide(r, base != VT_LPWSTR, &text);
        if (FAILED(hr))
            return hr;
        if (base == VT_LPWSTR) {
            *(LPWSTR *)out = text;
        } else if (base == VT_BSTR) {
            *(BSTR *)out = SysAllocString(text);
            CoTaskMemFree(text);
            if (!*(BSTR *)out)
                return E_OUTOFMEMORY;
        } else {
            hr = PsWideToAnsiAlloc(text, out);
            CoTaskMemFree(text);
        }
        return hr;
    }
    case VT_INT:
    case VT_UINT:
        size = 4;
        break;
    default:
        size = PsElementSize(base);
        break;
    }
    return take(r, out, size) ? S_OK : E_INVALIDARG;
}

static HRESULT take_value(READER *r, PROPVARIANT *value)
{
    memset(value, 0, sizeof(*value));
    DWORD type;
    if (!take(r, &type, 4))
        return E_INVALIDARG;
    VARTYPE vt = (VARTYPE)type, base = vt & VT_TYPEMASK;
    BOOL vector = (vt & VT_VECTOR) != 0;
    if (base == VT_STORAGE || base == VT_STORED_OBJECT || base == VT_VERSIONED_STREAM)
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    if ((vt & ~(VT_TYPEMASK | VT_VECTOR)) || !serializable(base, vector))
        return E_INVALIDARG;
    HRESULT hr = S_OK;
    if (!vector) {
        if (base == VT_BLOB) {
            if (!take(r, &value->blob.cbSize, 4) || r->size - r->offset < value->blob.cbSize)
                return E_INVALIDARG;
            value->blob.pBlobData = PsAllocZero(value->blob.cbSize);
            if (!value->blob.pBlobData)
                return E_OUTOFMEMORY;
            take(r, value->blob.pBlobData, value->blob.cbSize);
        } else if (base == VT_CLSID) {
            value->puuid = PsAllocZero(sizeof(GUID));
            if (!value->puuid)
                return E_OUTOFMEMORY;
            if (!take(r, value->puuid, 16)) {
                CoTaskMemFree(value->puuid);
                value->puuid = nullptr;
                return E_INVALIDARG;
            }
        } else if (base != VT_EMPTY && base != VT_NULL) {
            hr = take_element(r, base, &value->cVal);
            if (FAILED(hr))
                return hr;
        }
        align(r);
        value->vt = vt;
        return S_OK;
    }
    DWORD count;
    if (!take(r, &count, 4))
        return E_INVALIDARG;
    UINT size = PsElementSize(base);
    if (count > (r->size - r->offset) || (ULONGLONG)count * size > 0x7FFFFFFF)
        return E_INVALIDARG;
    BYTE *items = PsAllocZero((SIZE_T)count * size);
    if (!items)
        return E_OUTOFMEMORY;
    value->vt = vt;
    value->cac.pElems = (CHAR *)items;
    for (DWORD i = 0; i < count && SUCCEEDED(hr); i++) {
        if (base == VT_VARIANT)
            hr = take_value(r, (PROPVARIANT *)(items + (SIZE_T)i * size));
        else
            hr = take_element(r, base, items + (SIZE_T)i * size);
        if (SUCCEEDED(hr))
            value->cac.cElems++;
    }
    align(r);
    if (FAILED(hr))
        PropVariantClear(value);
    return hr;
}

HRESULT PsDeserializeValue(const BYTE *data, ULONG size, PROPVARIANT *value)
{
    READER r = {data, size, 0};
    return take_value(&r, value);
}

HRESULT WINAPI StgDeserializePropVariant(const SERIALIZEDPROPERTYVALUE *in, ULONG size, PROPVARIANT *value)
{
    memset(value, 0, sizeof(*value));
    if (size < 4)
        return E_INVALIDARG;
    return PsDeserializeValue((const BYTE *)in, size, value);
}
