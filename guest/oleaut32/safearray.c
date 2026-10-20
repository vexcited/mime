/*
 * safe arrays. like windows, every descriptor is allocated with 16 bytes in
 * front of it: the element interface id for FADF_HAVEIID, or the element
 * type in the last 4 bytes for FADF_HAVEVARTYPE. the bounds are stored in
 * reverse order of the dimensions, and the first index varies fastest.
 */
#include "oleaut32_private.h"

#define SA_PREFIX 16
#define FADF_NOT_OWNED (FADF_AUTO | FADF_STATIC | FADF_EMBEDDED)

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

static IRecordInfo **record_slot(SAFEARRAY *array)
{
    return &((IRecordInfo **)array)[-1];
}

/*
 * descriptors and data that something outside took a reference to, with SafeArrayAddRef. They are freed when the last
 * reference goes, whoever destroyed the array first. A data entry also remembers what is needed to free its elements.
 */
typedef struct {
    void *Key;
    LONG Refs;
    /* data only. */
    USHORT Features;
    ULONG Elements;
    ULONG ElementSize;
    IRecordInfo *Record;
} REFENTRY;

typedef struct {
    REFENTRY *Items;
    ULONG Count, Capacity;
} REFMAP;

static SRWLOCK g_ref_lock = SRWLOCK_INIT;
static REFMAP g_descriptors, g_data;

static REFENTRY *ref_find(REFMAP *map, void *key)
{
    for (ULONG i = 0; i < map->Count; i++)
        if (map->Items[i].Key == key)
            return &map->Items[i];
    return nullptr;
}

static REFENTRY *ref_add(REFMAP *map, void *key)
{
    if (map->Count == map->Capacity) {
        ULONG capacity = map->Capacity ? map->Capacity * 2 : 8;
        REFENTRY *items = AutAllocZero((SIZE_T)capacity * sizeof(REFENTRY));
        if (!items)
            return nullptr;
        if (map->Count)
            memcpy(items, map->Items, map->Count * sizeof(REFENTRY));
        AutFree(map->Items);
        map->Items = items;
        map->Capacity = capacity;
    }
    REFENTRY *entry = &map->Items[map->Count++];
    memset(entry, 0, sizeof(*entry));
    entry->Key = key;
    return entry;
}

static void ref_remove(REFMAP *map, REFENTRY *entry)
{
    *entry = map->Items[--map->Count];
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
    if (out)
        *out = nullptr;
    if (vt != VT_RECORD && !element_size(vt))
        return E_INVALIDARG;
    HRESULT hr = SafeArrayAllocDescriptor(dims, out);
    if (FAILED(hr))
        return hr;
    SAFEARRAY *array = *out;
    if (vt == VT_RECORD) {
        /* the size of an element is the one of the record, which is set with its information. */
        array->fFeatures = FADF_RECORD;
        return S_OK;
    }
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
static void clear_block(USHORT features, void *data, SIZE_T count, ULONG size, IRecordInfo *record)
{
    if (!data)
        return;
    if (features & FADF_BSTR) {
        BSTR *items = data;
        for (SIZE_T i = 0; i < count; i++) {
            SysFreeString(items[i]);
            items[i] = nullptr;
        }
    } else if (features & (FADF_UNKNOWN | FADF_DISPATCH)) {
        IUnknown **items = data;
        for (SIZE_T i = 0; i < count; i++) {
            AutRelease(items[i]);
            items[i] = nullptr;
        }
    } else if (features & FADF_VARIANT) {
        VARIANT *items = data;
        for (SIZE_T i = 0; i < count; i++)
            VariantClear(&items[i]);
    } else if (features & FADF_RECORD) {
        for (SIZE_T i = 0; record && i < count; i++)
            record->lpVtbl->RecordClear(record, (BYTE *)data + i * size);
    }
}

static void clear_elements(SAFEARRAY *array)
{
    SIZE_T count = element_count(array);
    if (array->pvData && count != (SIZE_T)-1)
        clear_block(array->fFeatures, array->pvData, count, array->cbElements,
                    (array->fFeatures & FADF_RECORD) ? *record_slot(array) : nullptr);
}

static void free_descriptor(SAFEARRAY *array)
{
    if (array->fFeatures & FADF_RECORD)
        AutRelease(*record_slot(array));
    if (!(array->fFeatures & FADF_NOT_OWNED))
        AutFree(prefix(array));
}

WINOLEAUTAPI HRESULT WINAPI SafeArrayDestroyData(SAFEARRAY *array)
{
    if (!array)
        return E_INVALIDARG;
    if (array->cLocks)
        return DISP_E_ARRAYISLOCKED;
    /* data that others hold a reference to stays with them, and the elements go when they let it go. */
    if (array->pvData && g_data.Count) {
        BOOL kept = FALSE;
        AcquireSRWLockExclusive(&g_ref_lock);
        REFENTRY *entry = ref_find(&g_data, array->pvData);
        if (entry && --entry->Refs > 0) {
            kept = TRUE;
        } else if (entry) {
            AutRelease(entry->Record);
            ref_remove(&g_data, entry);
        }
        ReleaseSRWLockExclusive(&g_ref_lock);
        if (kept) {
            array->pvData = nullptr;
            return S_OK;
        }
    }
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
    if (g_descriptors.Count) {
        BOOL kept = FALSE;
        AcquireSRWLockExclusive(&g_ref_lock);
        REFENTRY *entry = ref_find(&g_descriptors, array);
        if (entry && --entry->Refs > 0)
            kept = TRUE;
        else if (entry)
            ref_remove(&g_descriptors, entry);
        ReleaseSRWLockExclusive(&g_ref_lock);
        if (kept)
            return S_OK;
    }
    free_descriptor(array);
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
    } else if (array->fFeatures & FADF_RECORD) {
        IRecordInfo *record = *record_slot(array);
        if (!record)
            return E_INVALIDARG;
        return record->lpVtbl->RecordCopy(record, (void *)from, to);
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
    else if (array->fFeatures & FADF_RECORD) {
        IRecordInfo *record = *record_slot(array);
        if (record)
            record->lpVtbl->RecordClear(record, element);
    }
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
    if (SUCCEEDED(hr) && (array->fFeatures & FADF_RECORD)) {
        hr = copy_element(array, element, value);
    } else if (SUCCEEDED(hr)) {
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
    SAFEARRAY *copy;
    HRESULT hr = SafeArrayAllocDescriptor(array->cDims, &copy);
    if (FAILED(hr))
        return hr;
    memcpy(prefix(copy), prefix(array), SA_PREFIX);
    copy->fFeatures = array->fFeatures & ~FADF_NOT_OWNED;
    if (array->fFeatures & FADF_RECORD) {
        /* the copy holds a reference of its own, not the one the bytes of the prefix were. */
        *record_slot(copy) = nullptr;
        SafeArraySetRecordInfo(copy, *record_slot(array));
    }
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

/* a one dimensional array of bytes holds the bytes of a string, and the other way around. */
WINOLEAUTAPI HRESULT WINAPI BstrFromVector(SAFEARRAY *array, BSTR *out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!array)
        return E_INVALIDARG;
    if (array->cDims != 1 || array->cbElements != 1)
        return E_INVALIDARG;
    *out = SysAllocStringByteLen(array->pvData, array->rgsabound[0].cElements);
    return *out || !array->rgsabound[0].cElements ? S_OK : E_OUTOFMEMORY;
}

WINOLEAUTAPI HRESULT WINAPI VectorFromBstr(BSTR bstr, SAFEARRAY **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    ULONG length = SysStringByteLen(bstr);
    SAFEARRAY *array = SafeArrayCreateVector(VT_UI1, 0, length);
    if (!array)
        return E_OUTOFMEMORY;
    if (length)
        memcpy(array->pvData, bstr, length);
    *out = array;
    return S_OK;
}

/* ------------------------------------------------------------------ the rest of the creating and the references */

/* the size of an element and the features that go with its type, or false when no array holds the type. */
static BOOL element_traits(VARTYPE vt, IRecordInfo *record, ULONG *size, USHORT *features)
{
    *features = 0;
    switch (vt) {
    case VT_BSTR:
        *features = FADF_BSTR;
        break;
    case VT_DISPATCH:
        *features = FADF_DISPATCH;
        break;
    case VT_UNKNOWN:
        *features = FADF_UNKNOWN;
        break;
    case VT_VARIANT:
        *features = FADF_VARIANT;
        break;
    case VT_RECORD: {
        ULONG bytes = 0;
        if (!record)
            return FALSE;
        record->lpVtbl->GetSize(record, &bytes);
        if (bytes > 0xFFFF)
            return FALSE;
        *size = bytes;
        *features = FADF_RECORD;
        return TRUE;
    }
    default:
        break;
    }
    *size = element_size(vt);
    return *size != 0;
}

WINOLEAUTAPI SAFEARRAY *WINAPI SafeArrayCreateEx(VARTYPE vt, UINT dims, SAFEARRAYBOUND *bounds, PVOID extra)
{
    ULONG size;
    USHORT features;
    SAFEARRAY *array;
    if (!bounds || !dims || dims > 0xFFFF || !element_traits(vt, extra, &size, &features))
        return nullptr;
    if (FAILED(SafeArrayAllocDescriptorEx(vt, dims, &array)))
        return nullptr;
    array->cbElements = size;
    array->fFeatures |= features;
    if (vt == VT_RECORD) {
        AutAddRef(extra);
        *record_slot(array) = extra;
    } else if (extra && (vt == VT_UNKNOWN || vt == VT_DISPATCH)) {
        memcpy(prefix(array), extra, sizeof(GUID));
    }
    for (UINT i = 0; i < dims; i++)
        array->rgsabound[dims - 1 - i] = bounds[i];
    if (FAILED(SafeArrayAllocData(array))) {
        SafeArrayDestroyDescriptor(array);
        return nullptr;
    }
    return array;
}

WINOLEAUTAPI SAFEARRAY *WINAPI SafeArrayCreateVectorEx(VARTYPE vt, LONG lower, ULONG count, PVOID extra)
{
    SAFEARRAYBOUND bound = {count, lower};
    return SafeArrayCreateEx(vt, 1, &bound, extra);
}

WINOLEAUTAPI HRESULT WINAPI SafeArrayGetIID(SAFEARRAY *array, GUID *iid)
{
    if (!array || !iid || !(array->fFeatures & FADF_HAVEIID))
        return E_INVALIDARG;
    memcpy(iid, prefix(array), sizeof(GUID));
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI SafeArraySetIID(SAFEARRAY *array, REFGUID iid)
{
    if (!array || !(array->fFeatures & FADF_HAVEIID))
        return E_INVALIDARG;
    memcpy(prefix(array), iid, sizeof(GUID));
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI SafeArrayAddRef(SAFEARRAY *array, PVOID *data)
{
    if (!array || !data)
        return E_INVALIDARG;
    *data = nullptr;
    HRESULT hr = S_OK;
    AcquireSRWLockExclusive(&g_ref_lock);
    REFENTRY *entry = ref_find(&g_descriptors, array);
    if (!entry) {
        entry = ref_add(&g_descriptors, array);
        if (entry)
            entry->Refs = 1;
    }
    if (!entry) {
        hr = E_OUTOFMEMORY;
    } else {
        entry->Refs++;
        /* only data the array owns is kept, a static one is the caller's to keep alive. */
        if (!(array->fFeatures & FADF_NOT_OWNED) && array->pvData) {
            REFENTRY *d = ref_find(&g_data, array->pvData);
            if (!d) {
                d = ref_add(&g_data, array->pvData);
                if (d) {
                    SIZE_T count = element_count(array);
                    d->Refs = 1;
                    d->Features = array->fFeatures;
                    d->Elements = count == (SIZE_T)-1 ? 0 : (ULONG)count;
                    d->ElementSize = array->cbElements;
                    if (array->fFeatures & FADF_RECORD) {
                        d->Record = *record_slot(array);
                        AutAddRef(d->Record);
                    }
                }
            }
            if (!d) {
                entry->Refs--;
                hr = E_OUTOFMEMORY;
            } else {
                d->Refs++;
                *data = array->pvData;
            }
        }
    }
    ReleaseSRWLockExclusive(&g_ref_lock);
    return hr;
}

WINOLEAUTAPI void WINAPI SafeArrayReleaseData(PVOID data)
{
    if (!data)
        return;
    REFENTRY gone = {0};
    BOOL last = FALSE;
    AcquireSRWLockExclusive(&g_ref_lock);
    REFENTRY *entry = ref_find(&g_data, data);
    if (entry && --entry->Refs <= 0) {
        gone = *entry;
        ref_remove(&g_data, entry);
        last = TRUE;
    }
    ReleaseSRWLockExclusive(&g_ref_lock);
    if (last) {
        clear_block(gone.Features, data, gone.Elements, gone.ElementSize, gone.Record);
        AutRelease(gone.Record);
        AutFree(data);
    }
}

WINOLEAUTAPI void WINAPI SafeArrayReleaseDescriptor(SAFEARRAY *array)
{
    if (!array)
        return;
    BOOL last = TRUE;
    AcquireSRWLockExclusive(&g_ref_lock);
    REFENTRY *entry = ref_find(&g_descriptors, array);
    if (entry) {
        if (--entry->Refs > 0)
            last = FALSE;
        else
            ref_remove(&g_descriptors, entry);
    }
    ReleaseSRWLockExclusive(&g_ref_lock);
    if (last)
        free_descriptor(array);
}
