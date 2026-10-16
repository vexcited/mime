/*
 * safe arrays. like windows, every descriptor is allocated with 16 bytes in
 * front of it: the element interface id for FADF_HAVEIID, or the element
 * type in the last 4 bytes for FADF_HAVEVARTYPE. the bounds are stored in
 * reverse order of the dimensions, and the first index varies fastest.
 */
#include "oleaut32_private.h"

#define SA_PREFIX 16
#define FADF_NOT_OWNED (FADF_AUTO | FADF_STATIC | FADF_EMBEDDED)

MIME_DEFINE_GUID(IID_IDispatch, 0x00020400, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

static ULONG element_size(VARTYPE vt)
{
    switch (vt) {
    case VT_I1:
    case VT_UI1:
        return 1;
    case VT_I2:
    case VT_UI2:
    case VT_BOOL:
        return 2;
    case VT_I4:
    case VT_UI4:
    case VT_R4:
    case VT_INT:
    case VT_UINT:
    case VT_ERROR:
        return 4;
    case VT_I8:
    case VT_UI8:
    case VT_R8:
    case VT_CY:
    case VT_DATE:
        return 8;
    case VT_BSTR:
    case VT_UNKNOWN:
    case VT_DISPATCH:
    case VT_INT_PTR:
    case VT_UINT_PTR:
        return sizeof(void *);
    case VT_VARIANT:
        return sizeof(VARIANT);
    case VT_DECIMAL:
        return sizeof(DECIMAL);
    default:
        return 0;
    }
}

static BYTE *prefix(SAFEARRAY *array)
{
    return (BYTE *)array - SA_PREFIX;
}

static SIZE_T element_count(SAFEARRAY *array)
{
    SIZE_T count = 1;
    for (USHORT i = 0; i < array->cDims; i++) {
        ULONG64 next = (ULONG64)count * array->rgsabound[i].cElements;
        if (next > 0xFFFFFFFFull)
            return (SIZE_T)-1;
        count = (SIZE_T)next;
    }
    return count;
}

WINOLEAUTAPI HRESULT WINAPI SafeArrayAllocDescriptor(UINT dims, SAFEARRAY **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!dims || dims > 0xFFFF)
        return E_INVALIDARG;
    SIZE_T size = SA_PREFIX + sizeof(SAFEARRAY) + (dims - 1) * sizeof(SAFEARRAYBOUND);
    BYTE *block = AutAllocZero(size);
    if (!block)
        return E_OUTOFMEMORY;
    SAFEARRAY *array = (SAFEARRAY *)(block + SA_PREFIX);
    array->cDims = (USHORT)dims;
    *out = array;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI SafeArrayAllocDescriptorEx(VARTYPE vt, UINT dims, SAFEARRAY **out)
{
    if (vt == VT_RECORD || !element_size(vt))
        return E_INVALIDARG;
    HRESULT hr = SafeArrayAllocDescriptor(dims, out);
    if (FAILED(hr))
        return hr;
    SAFEARRAY *array = *out;
    array->cbElements = element_size(vt);
    switch (vt) {
    case VT_BSTR:
        array->fFeatures = FADF_BSTR | FADF_HAVEVARTYPE;
        break;
    case VT_VARIANT:
        array->fFeatures = FADF_VARIANT | FADF_HAVEVARTYPE;
        break;
    case VT_UNKNOWN:
        array->fFeatures = FADF_UNKNOWN | FADF_HAVEIID;
        memcpy(prefix(array), &IID_IUnknown, sizeof(GUID));
        return S_OK;
    case VT_DISPATCH:
        array->fFeatures = FADF_DISPATCH | FADF_HAVEIID;
        memcpy(prefix(array), &IID_IDispatch, sizeof(GUID));
        return S_OK;
    default:
        array->fFeatures = FADF_HAVEVARTYPE;
        break;
    }
    ((DWORD *)array)[-1] = vt;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI SafeArrayAllocData(SAFEARRAY *array)
{
    if (!array)
        return E_INVALIDARG;
    SIZE_T count = element_count(array);
    if (count == (SIZE_T)-1 || (ULONG64)count * array->cbElements > 0x7FFFFFFFull)
        return E_OUTOFMEMORY;
    SIZE_T bytes = count * array->cbElements;
    array->pvData = AutAllocZero(bytes ? bytes : 1);
    return array->pvData ? S_OK : E_OUTOFMEMORY;
}

WINOLEAUTAPI SAFEARRAY *WINAPI SafeArrayCreate(VARTYPE vt, UINT dims, SAFEARRAYBOUND *bounds)
{
    if (!bounds || !dims)
        return nullptr;
    SAFEARRAY *array;
    if (FAILED(SafeArrayAllocDescriptorEx(vt, dims, &array)))
        return nullptr;
    for (UINT i = 0; i < dims; i++)
        array->rgsabound[dims - 1 - i] = bounds[i];
    if (FAILED(SafeArrayAllocData(array))) {
        SafeArrayDestroyDescriptor(array);
        return nullptr;
    }
    return array;
}

WINOLEAUTAPI SAFEARRAY *WINAPI SafeArrayCreateVector(VARTYPE vt, LONG lower, ULONG count)
{
    SAFEARRAYBOUND bound = {count, lower};
    return SafeArrayCreate(vt, 1, &bound);
}

/* releases what the elements own and zeroes them. */
static void clear_elements(SAFEARRAY *array)
{
    if (!array->pvData)
        return;
    SIZE_T count = element_count(array);
    if (count == (SIZE_T)-1)
        return;
    if (array->fFeatures & FADF_BSTR) {
        BSTR *items = array->pvData;
        for (SIZE_T i = 0; i < count; i++) {
            SysFreeString(items[i]);
            items[i] = nullptr;
        }
    } else if (array->fFeatures & (FADF_UNKNOWN | FADF_DISPATCH)) {
        IUnknown **items = array->pvData;
        for (SIZE_T i = 0; i < count; i++) {
            AutRelease(items[i]);
            items[i] = nullptr;
        }
    } else if (array->fFeatures & FADF_VARIANT) {
        VARIANT *items = array->pvData;
        for (SIZE_T i = 0; i < count; i++)
            VariantClear(&items[i]);
    }
}

WINOLEAUTAPI HRESULT WINAPI SafeArrayDestroyData(SAFEARRAY *array)
{
    if (!array)
        return E_INVALIDARG;
    if (array->cLocks)
        return DISP_E_ARRAYISLOCKED;
    clear_elements(array);
    if (!(array->fFeatures & FADF_NOT_OWNED)) {
        AutFree(array->pvData);
        array->pvData = nullptr;
    }
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI SafeArrayDestroyDescriptor(SAFEARRAY *array)
{
    if (!array)
        return E_INVALIDARG;
    if (array->cLocks)
        return DISP_E_ARRAYISLOCKED;
    if (!(array->fFeatures & FADF_NOT_OWNED))
        AutFree(prefix(array));
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI SafeArrayDestroy(SAFEARRAY *array)
{
    if (!array)
        return S_OK;
    if (array->cLocks)
        return DISP_E_ARRAYISLOCKED;
    HRESULT hr = SafeArrayDestroyData(array);
    return FAILED(hr) ? hr : SafeArrayDestroyDescriptor(array);
}

WINOLEAUTAPI HRESULT WINAPI SafeArrayLock(SAFEARRAY *array)
{
    if (!array)
        return E_INVALIDARG;
    if (InterlockedIncrement((LONG volatile *)&array->cLocks) > 0xFFFF) {
        InterlockedDecrement((LONG volatile *)&array->cLocks);
        return E_UNEXPECTED;
    }
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI SafeArrayUnlock(SAFEARRAY *array)
{
    if (!array)
        return E_INVALIDARG;
    if ((LONG)InterlockedDecrement((LONG volatile *)&array->cLocks) < 0) {
        InterlockedIncrement((LONG volatile *)&array->cLocks);
        return E_UNEXPECTED;
    }
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI SafeArrayAccessData(SAFEARRAY *array, void **data)
{
    if (!array || !data)
        return E_INVALIDARG;
    HRESULT hr = SafeArrayLock(array);
    *data = SUCCEEDED(hr) ? array->pvData : nullptr;
    return hr;
}

WINOLEAUTAPI HRESULT WINAPI SafeArrayUnaccessData(SAFEARRAY *array)
{
    return SafeArrayUnlock(array);
}

WINOLEAUTAPI UINT WINAPI SafeArrayGetDim(SAFEARRAY *array)
{
    return array ? array->cDims : 0;
}

WINOLEAUTAPI UINT WINAPI SafeArrayGetElemsize(SAFEARRAY *array)
{
    return array ? array->cbElements : 0;
}

static HRESULT get_bound(SAFEARRAY *array, UINT dim, SAFEARRAYBOUND **bound)
{
    if (!array)
        return E_INVALIDARG;
    if (!dim || dim > array->cDims)
        return DISP_E_BADINDEX;
    *bound = &array->rgsabound[array->cDims - dim];
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI SafeArrayGetLBound(SAFEARRAY *array, UINT dim, LONG *value)
{
    SAFEARRAYBOUND *bound;
    if (!value)
        return E_INVALIDARG;
    HRESULT hr = get_bound(array, dim, &bound);
    if (SUCCEEDED(hr))
        *value = bound->lLbound;
    return hr;
}

WINOLEAUTAPI HRESULT WINAPI SafeArrayGetUBound(SAFEARRAY *array, UINT dim, LONG *value)
{
    SAFEARRAYBOUND *bound;
    if (!value)
        return E_INVALIDARG;
    HRESULT hr = get_bound(array, dim, &bound);
    if (SUCCEEDED(hr))
        *value = (LONG)(bound->lLbound + (LONG64)bound->cElements - 1);
    return hr;
}

WINOLEAUTAPI HRESULT WINAPI SafeArrayGetVartype(SAFEARRAY *array, VARTYPE *vt)
{
    if (!array || !vt)
        return E_INVALIDARG;
    if (array->fFeatures & FADF_HAVEVARTYPE)
        *vt = (VARTYPE)((DWORD *)array)[-1];
    else if (array->fFeatures & FADF_RECORD)
        *vt = VT_RECORD;
    else if (array->fFeatures & FADF_BSTR)
        *vt = VT_BSTR;
    else if (array->fFeatures & FADF_UNKNOWN)
        *vt = VT_UNKNOWN;
    else if (array->fFeatures & FADF_DISPATCH)
        *vt = VT_DISPATCH;
    else if (array->fFeatures & FADF_VARIANT)
        *vt = VT_VARIANT;
    else
        return E_INVALIDARG;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI SafeArrayPtrOfIndex(SAFEARRAY *array, LONG *indices, void **element)
{
    if (!array || !indices || !element)
        return E_INVALIDARG;
    if (!array->pvData)
        return E_INVALIDARG;
    SIZE_T offset = 0, stride = 1;
    for (USHORT i = 0; i < array->cDims; i++) {
        const SAFEARRAYBOUND *bound = &array->rgsabound[array->cDims - 1 - i];
        LONG64 index = (LONG64)indices[i] - bound->lLbound;
        if (index < 0 || index >= bound->cElements)
            return DISP_E_BADINDEX;
        offset += (SIZE_T)index * stride;
        stride *= bound->cElements;
    }
    *element = (BYTE *)array->pvData + offset * array->cbElements;
    return S_OK;
}

/* copies one element the way the array type owns it. */
static HRESULT copy_element(SAFEARRAY *array, void *to, const void *from)
{
    if (array->fFeatures & FADF_BSTR) {
        BSTR source = *(const BSTR *)from, copy = nullptr;
        if (source) {
            copy = SysAllocStringByteLen((LPCSTR)source, SysStringByteLen(source));
            if (!copy)
                return E_OUTOFMEMORY;
        }
        *(BSTR *)to = copy;
    } else if (array->fFeatures & (FADF_UNKNOWN | FADF_DISPATCH)) {
        IUnknown *source = *(IUnknown *const *)from;
        AutAddRef(source);
        *(IUnknown **)to = source;
    } else if (array->fFeatures & FADF_VARIANT) {
        ((VARIANT *)to)->vt = VT_EMPTY;
        return VariantCopy(to, from);
    } else {
        memcpy(to, from, array->cbElements);
    }
    return S_OK;
}

static void free_element(SAFEARRAY *array, void *element)
{
    if (array->fFeatures & FADF_BSTR)
        SysFreeString(*(BSTR *)element);
    else if (array->fFeatures & (FADF_UNKNOWN | FADF_DISPATCH))
        AutRelease(*(IUnknown **)element);
    else if (array->fFeatures & FADF_VARIANT)
        VariantClear(element);
}

/* strings and interfaces are passed by value, every other type through a pointer. */
WINOLEAUTAPI HRESULT WINAPI SafeArrayPutElement(SAFEARRAY *array, LONG *indices, void *value)
{
    if (!array || !indices)
        return E_INVALIDARG;
    if (!value && !(array->fFeatures & (FADF_BSTR | FADF_UNKNOWN | FADF_DISPATCH)))
        return E_INVALIDARG;
    void *element;
    HRESULT hr = SafeArrayLock(array);
    if (FAILED(hr))
        return hr;
    hr = SafeArrayPtrOfIndex(array, indices, &element);
    if (SUCCEEDED(hr)) {
        union {
            VARIANT variant;
            DECIMAL decimal;
            LONGLONG integer;
        } fresh;
        const void *from = (array->fFeatures & (FADF_BSTR | FADF_UNKNOWN | FADF_DISPATCH)) ? (void *)&value : value;
        hr = copy_element(array, &fresh, from);
        if (SUCCEEDED(hr)) {
            free_element(array, element);
            memcpy(element, &fresh, array->cbElements);
        }
    }
    SafeArrayUnlock(array);
    return hr;
}

WINOLEAUTAPI HRESULT WINAPI SafeArrayGetElement(SAFEARRAY *array, LONG *indices, void *value)
{
    if (!array || !indices || !value)
        return E_INVALIDARG;
    void *element;
    HRESULT hr = SafeArrayLock(array);
    if (FAILED(hr))
        return hr;
    hr = SafeArrayPtrOfIndex(array, indices, &element);
    if (SUCCEEDED(hr))
        hr = copy_element(array, value, element);
    SafeArrayUnlock(array);
    return hr;
}

WINOLEAUTAPI HRESULT WINAPI SafeArrayCopyData(SAFEARRAY *source, SAFEARRAY *target)
{
    if (!source || !target || source->cDims != target->cDims || source->cbElements != target->cbElements)
        return E_INVALIDARG;
    for (USHORT i = 0; i < source->cDims; i++)
        if (source->rgsabound[i].cElements != target->rgsabound[i].cElements)
            return E_INVALIDARG;
    if (!source->pvData || !target->pvData)
        return E_INVALIDARG;
    clear_elements(target);
    SIZE_T count = element_count(source);
    for (SIZE_T i = 0; i < count; i++) {
        HRESULT hr = copy_element(source, (BYTE *)target->pvData + i * source->cbElements,
                                  (BYTE *)source->pvData + i * source->cbElements);
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI SafeArrayCopy(SAFEARRAY *array, SAFEARRAY **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!array)
        return S_OK;
    if (array->fFeatures & FADF_RECORD)
        return E_NOTIMPL;
    SAFEARRAY *copy;
    HRESULT hr = SafeArrayAllocDescriptor(array->cDims, &copy);
    if (FAILED(hr))
        return hr;
    memcpy(prefix(copy), prefix(array), SA_PREFIX);
    copy->fFeatures = array->fFeatures & ~FADF_NOT_OWNED;
    copy->cbElements = array->cbElements;
    memcpy(copy->rgsabound, array->rgsabound, array->cDims * sizeof(SAFEARRAYBOUND));
    if (array->pvData) {
        hr = SafeArrayAllocData(copy);
        if (SUCCEEDED(hr))
            hr = SafeArrayCopyData(array, copy);
        if (FAILED(hr)) {
            SafeArrayDestroy(copy);
            return hr;
        }
    }
    *out = copy;
    return S_OK;
}

/*
 * changes the bounds of the last dimension, whose bounds are stored first.
 * that dimension varies slowest, so elements are added or dropped at the end.
 */
WINOLEAUTAPI HRESULT WINAPI SafeArrayRedim(SAFEARRAY *array, SAFEARRAYBOUND *bound)
{
    if (!array || !bound)
        return E_INVALIDARG;
    if (array->cLocks)
        return DISP_E_ARRAYISLOCKED;
    if (array->fFeatures & FADF_FIXEDSIZE)
        return E_INVALIDARG;
    ULONG64 slice = 1;
    for (USHORT i = 1; i < array->cDims; i++)
        slice *= array->rgsabound[i].cElements;
    ULONG64 old_count = slice * array->rgsabound[0].cElements, new_count = slice * bound->cElements;
    if (new_count * array->cbElements > 0x7FFFFFFFull)
        return E_OUTOFMEMORY;
    if (new_count != old_count) {
        if (array->fFeatures & FADF_NOT_OWNED)
            return E_INVALIDARG;
        for (ULONG64 i = new_count; i < old_count && array->pvData; i++)
            free_element(array, (BYTE *)array->pvData + i * array->cbElements);
        SIZE_T bytes = (SIZE_T)(new_count * array->cbElements);
        void *data = array->pvData ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, array->pvData, bytes ? bytes : 1)
                                   : AutAllocZero(bytes ? bytes : 1);
        if (!data)
            return E_OUTOFMEMORY;
        array->pvData = data;
    }
    array->rgsabound[0] = *bound;
    return S_OK;
}
