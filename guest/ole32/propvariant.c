/* property variants own task memory, automation values and interface references. */
#include "ole32_private.h"
#include <mime/win32/propidl.h>

static BOOL automation_type(VARTYPE type)
{
    return (type >= VT_I2 && type <= VT_DECIMAL) ||
           (type >= VT_I1 && type <= VT_UI4) || type == VT_INT || type == VT_UINT;
}

static BOOL vector_type(VARTYPE type)
{
    return (type >= VT_I2 && type <= VT_BSTR) || type == VT_ERROR || type == VT_BOOL ||
           type == VT_VARIANT || (type >= VT_I1 && type <= VT_UI8) || type == VT_LPSTR ||
           type == VT_LPWSTR || type == VT_FILETIME || type == VT_CLSID || type == VT_CF ||
           type == VT_BSTR_BLOB;
}

static HRESULT clear_automation(PROPVARIANT *value)
{
    HMODULE library = LoadLibraryW(L"oleaut32.dll");
    if (!library)
        return CO_E_DLLNOTFOUND;
    typedef HRESULT (WINAPI *CLEAR)(VARIANT *);
    CLEAR clear = (CLEAR)GetProcAddress(library, "VariantClear");
    VARIANT automation;
    memcpy(&automation, value, sizeof(automation));
    HRESULT hr = clear ? clear(&automation) : E_NOTIMPL;
    FreeLibrary(library);
    return hr;
}

static HRESULT clear_vector(PROPVARIANT *value)
{
    VARTYPE type = value->vt & VT_TYPEMASK;
    if (!vector_type(type))
        return DISP_E_BADVARTYPE;
    for (ULONG i = 0; i < value->caub.cElems; i++) {
        HRESULT hr = S_OK;
        switch (type) {
        case VT_VARIANT:
            hr = PropVariantClear(&value->capropvar.pElems[i]);
            break;
        case VT_BSTR: {
            PROPVARIANT item = {};
            item.vt = VT_BSTR;
            item.bstrVal = value->cabstr.pElems[i];
            hr = clear_automation(&item);
            if (SUCCEEDED(hr))
                value->cabstr.pElems[i] = nullptr;
            break;
        }
        case VT_LPSTR:
        case VT_LPWSTR:
            CoTaskMemFree(value->calpstr.pElems[i]);
            value->calpstr.pElems[i] = nullptr;
            break;
        case VT_CF:
            CoTaskMemFree(value->caclipdata.pElems[i].pClipData);
            value->caclipdata.pElems[i].pClipData = nullptr;
            break;
        case VT_BSTR_BLOB:
            CoTaskMemFree(value->cabstrblob.pElems[i].pData);
            value->cabstrblob.pElems[i].pData = nullptr;
            break;
        default:
            break;
        }
        if (FAILED(hr))
            return hr;
    }
    CoTaskMemFree(value->caub.pElems);
    return S_OK;
}

WINOLEAPI HRESULT WINAPI PropVariantClear(PROPVARIANT *value)
{
    if (!value)
        return E_INVALIDARG;
    VARTYPE type = value->vt & VT_TYPEMASK;
    VARTYPE flags = value->vt & ~VT_TYPEMASK;
    HRESULT hr = S_OK;
    if (flags & VT_RESERVED)
        return DISP_E_BADVARTYPE;
    if (flags & VT_VECTOR) {
        if (flags != VT_VECTOR)
            return DISP_E_BADVARTYPE;
        hr = clear_vector(value);
    } else if (flags & (VT_ARRAY | VT_BYREF)) {
        if (!automation_type(type))
            return DISP_E_BADVARTYPE;
        if (!(flags & VT_BYREF))
            hr = clear_automation(value);
    } else {
        switch (type) {
        case VT_EMPTY: case VT_NULL:
        case VT_I1: case VT_UI1: case VT_I2: case VT_UI2:
        case VT_I4: case VT_UI4: case VT_I8: case VT_UI8:
        case VT_INT: case VT_UINT: case VT_R4: case VT_R8:
        case VT_CY: case VT_DATE: case VT_BOOL: case VT_ERROR:
        case VT_DECIMAL: case VT_FILETIME:
            break;
        case VT_LPSTR: case VT_LPWSTR: case VT_CLSID:
            CoTaskMemFree(value->pszVal);
            break;
        case VT_BLOB: case VT_BLOBOBJECT: case VT_BSTR_BLOB:
            CoTaskMemFree(value->blob.pBlobData);
            break;
        case VT_BSTR:
            hr = clear_automation(value);
            break;
        case VT_UNKNOWN: case VT_DISPATCH: case VT_STREAM: case VT_STORAGE:
        case VT_STREAMED_OBJECT: case VT_STORED_OBJECT:
            if (value->punkVal)
                value->punkVal->lpVtbl->Release(value->punkVal);
            break;
        case VT_CF:
            if (value->pclipdata) {
                CoTaskMemFree(value->pclipdata->pClipData);
                CoTaskMemFree(value->pclipdata);
            }
            break;
        case VT_VERSIONED_STREAM:
            if (value->pVersionedStream) {
                IUnknown *stream = (IUnknown *)value->pVersionedStream->pStream;
                if (stream)
                    stream->lpVtbl->Release(stream);
                CoTaskMemFree(value->pVersionedStream);
            }
            break;
        default:
            return DISP_E_BADVARTYPE;
        }
    }
    if (SUCCEEDED(hr))
        memset(value, 0, sizeof(*value));
    return hr;
}

/* ------------------------------------------------------------------ copying */

static FARPROC automation_function(const char *name)
{
    HMODULE library = GetModuleHandleW(L"oleaut32.dll");
    if (!library)
        library = LoadLibraryW(L"oleaut32.dll");
    return library ? GetProcAddress(library, name) : nullptr;
}

static HRESULT copy_bstr(BSTR source, BSTR *dest)
{
    typedef UINT(WINAPI * BYTE_LEN)(BSTR);
    typedef BSTR(WINAPI * ALLOC_BYTE_LEN)(LPCSTR, UINT);
    *dest = nullptr;
    if (!source)
        return S_OK;
    BYTE_LEN length = (BYTE_LEN)automation_function("SysStringByteLen");
    ALLOC_BYTE_LEN alloc = (ALLOC_BYTE_LEN)automation_function("SysAllocStringByteLen");
    if (!length || !alloc)
        return CO_E_DLLNOTFOUND;
    *dest = alloc((LPCSTR)source, length(source));
    return *dest ? S_OK : E_OUTOFMEMORY;
}

static void *copy_memory(const void *source, SIZE_T size)
{
    void *copy = CoTaskMemAlloc(size ? size : 1);
    if (copy && size)
        memcpy(copy, source, size);
    return copy;
}

static HRESULT copy_string(const char *source, char **dest)
{
    *dest = source ? copy_memory(source, strlen(source) + 1) : nullptr;
    return !source || *dest ? S_OK : E_OUTOFMEMORY;
}

static HRESULT copy_wide(const WCHAR *source, WCHAR **dest)
{
    *dest = source ? copy_memory(source, (wcslen(source) + 1) * sizeof(WCHAR)) : nullptr;
    return !source || *dest ? S_OK : E_OUTOFMEMORY;
}

static HRESULT copy_clipdata(const CLIPDATA *source, CLIPDATA *dest)
{
    *dest = *source;
    dest->pClipData = nullptr;
    if (!source->pClipData || source->cbSize <= sizeof(source->ulClipFmt))
        return S_OK;
    SIZE_T size = source->cbSize - sizeof(source->ulClipFmt);
    dest->pClipData = copy_memory(source->pClipData, size);
    return dest->pClipData ? S_OK : E_OUTOFMEMORY;
}

static SIZE_T vector_element_size(VARTYPE type)
{
    switch (type) {
    case VT_I1: case VT_UI1:
        return 1;
    case VT_I2: case VT_UI2: case VT_BOOL:
        return 2;
    case VT_I4: case VT_UI4: case VT_R4: case VT_ERROR:
        return 4;
    case VT_I8: case VT_UI8: case VT_R8: case VT_CY: case VT_DATE: case VT_FILETIME:
        return 8;
    case VT_CLSID:
        return sizeof(CLSID);
    case VT_CF:
        return sizeof(CLIPDATA);
    case VT_BSTR: case VT_LPSTR: case VT_LPWSTR:
        return sizeof(void *);
    case VT_VARIANT:
        return sizeof(PROPVARIANT);
    case VT_BSTR_BLOB:
        return sizeof(BSTRBLOB);
    default:
        return 0;
    }
}

/* copies the elements of a vector whose array was already duplicated byte for byte. */
static HRESULT copy_vector_items(VARTYPE type, PROPVARIANT *dest, const PROPVARIANT *source)
{
    for (ULONG i = 0; i < source->caub.cElems; i++) {
        HRESULT hr = S_OK;
        switch (type) {
        case VT_VARIANT:
            hr = PropVariantCopy(&dest->capropvar.pElems[i], &source->capropvar.pElems[i]);
            break;
        case VT_BSTR:
            hr = copy_bstr(source->cabstr.pElems[i], &dest->cabstr.pElems[i]);
            break;
        case VT_LPSTR:
            hr = copy_string(source->calpstr.pElems[i], &dest->calpstr.pElems[i]);
            break;
        case VT_LPWSTR:
            hr = copy_wide(source->calpwstr.pElems[i], &dest->calpwstr.pElems[i]);
            break;
        case VT_CF:
            hr = copy_clipdata(&source->caclipdata.pElems[i], &dest->caclipdata.pElems[i]);
            break;
        case VT_BSTR_BLOB:
            dest->cabstrblob.pElems[i].pData = source->cabstrblob.pElems[i].pData ?
                copy_memory(source->cabstrblob.pElems[i].pData, source->cabstrblob.pElems[i].cbSize) : nullptr;
            if (source->cabstrblob.pElems[i].pData && !dest->cabstrblob.pElems[i].pData)
                hr = E_OUTOFMEMORY;
            break;
        default:
            break;
        }
        if (FAILED(hr)) {
            /* the copied prefix is owned, the rest still borrows the source and is cut off. */
            dest->caub.cElems = i;
            return hr;
        }
    }
    return S_OK;
}

static HRESULT copy_value(PROPVARIANT *dest, const PROPVARIANT *source)
{
    VARTYPE type = source->vt & VT_TYPEMASK;
    VARTYPE flags = source->vt & ~VT_TYPEMASK;
    if (flags & VT_RESERVED)
        return DISP_E_BADVARTYPE;
    if (flags & VT_VECTOR) {
        SIZE_T size = vector_element_size(type);
        if (flags != VT_VECTOR || !vector_type(type) || !size)
            return DISP_E_BADVARTYPE;
        dest->caub.pElems = nullptr;
        if (!source->caub.cElems)
            return S_OK;
        dest->caub.pElems = copy_memory(source->caub.pElems, size * source->caub.cElems);
        if (!dest->caub.pElems) {
            dest->caub.cElems = 0;
            return E_OUTOFMEMORY;
        }
        return copy_vector_items(type, dest, source);
    }
    if (flags & (VT_ARRAY | VT_BYREF)) {
        if (!automation_type(type))
            return DISP_E_BADVARTYPE;
        if (flags & VT_BYREF)
            return S_OK;
        typedef HRESULT(WINAPI * ARRAY_COPY)(SAFEARRAY *, SAFEARRAY **);
        ARRAY_COPY copy = (ARRAY_COPY)automation_function("SafeArrayCopy");
        dest->parray = nullptr;
        return copy ? copy(source->parray, &dest->parray) : CO_E_DLLNOTFOUND;
    }
    switch (type) {
    case VT_EMPTY: case VT_NULL:
    case VT_I1: case VT_UI1: case VT_I2: case VT_UI2:
    case VT_I4: case VT_UI4: case VT_I8: case VT_UI8:
    case VT_INT: case VT_UINT: case VT_R4: case VT_R8:
    case VT_CY: case VT_DATE: case VT_BOOL: case VT_ERROR:
    case VT_DECIMAL: case VT_FILETIME:
        return S_OK;
    case VT_LPSTR:
        return copy_string(source->pszVal, &dest->pszVal);
    case VT_LPWSTR:
        return copy_wide(source->pwszVal, &dest->pwszVal);
    case VT_CLSID:
        dest->puuid = source->puuid ? copy_memory(source->puuid, sizeof(CLSID)) : nullptr;
        return !source->puuid || dest->puuid ? S_OK : E_OUTOFMEMORY;
    case VT_BLOB: case VT_BLOBOBJECT: case VT_BSTR_BLOB:
        dest->blob.pBlobData = source->blob.pBlobData ?
            copy_memory(source->blob.pBlobData, source->blob.cbSize) : nullptr;
        return !source->blob.pBlobData || dest->blob.pBlobData ? S_OK : E_OUTOFMEMORY;
    case VT_BSTR:
        return copy_bstr(source->bstrVal, &dest->bstrVal);
    case VT_UNKNOWN: case VT_DISPATCH: case VT_STREAM: case VT_STORAGE:
    case VT_STREAMED_OBJECT: case VT_STORED_OBJECT:
        if (dest->punkVal)
            dest->punkVal->lpVtbl->AddRef(dest->punkVal);
        return S_OK;
    case VT_CF:
        if (!source->pclipdata)
            return S_OK;
        dest->pclipdata = CoTaskMemAlloc(sizeof(CLIPDATA));
        if (!dest->pclipdata)
            return E_OUTOFMEMORY;
        return copy_clipdata(source->pclipdata, dest->pclipdata);
    case VT_VERSIONED_STREAM:
        if (!source->pVersionedStream)
            return S_OK;
        dest->pVersionedStream = copy_memory(source->pVersionedStream, sizeof(VERSIONEDSTREAM));
        if (!dest->pVersionedStream)
            return E_OUTOFMEMORY;
        if (dest->pVersionedStream->pStream) {
            IUnknown *stream = (IUnknown *)dest->pVersionedStream->pStream;
            stream->lpVtbl->AddRef(stream);
        }
        return S_OK;
    default:
        return DISP_E_BADVARTYPE;
    }
}

WINOLEAPI HRESULT WINAPI PropVariantCopy(PROPVARIANT *dest, const PROPVARIANT *source)
{
    if (!dest || !source)
        return E_INVALIDARG;
    memcpy(dest, source, sizeof(*dest));
    HRESULT hr = copy_value(dest, source);
    if (FAILED(hr)) {
        /* free what the copy owns so far; borrowed pointers were replaced or cut off. */
        VARTYPE type = source->vt & VT_TYPEMASK;
        if (hr != DISP_E_BADVARTYPE && (source->vt & VT_VECTOR))
            PropVariantClear(dest);
        else if (hr != DISP_E_BADVARTYPE && type == VT_CF && dest->pclipdata)
            CoTaskMemFree(dest->pclipdata);
        memset(dest, 0, sizeof(*dest));
    }
    return hr;
}
