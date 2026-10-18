/*
 * attribute blobs and their stream form. a blob is the magic "AFMI", the
 * item count, a 32 byte entry per item and the data of the sized items
 * after the entries; interfaces are left out. see docs/mfplat.md.
 */
#include "mfplat_private.h"

#define BLOB_MAGIC 0x494D4641
#define BLOB_HEADER 8
#define BLOB_ENTRY 32
#define MAX_BLOB_STRING 0x40000
#define MSHCTX_INPROC 3
#define MSHLFLAGS_NORMAL 0

WINOLEAPI HRESULT WINAPI CoMarshalInterface(IStream *stream, REFIID iid, IUnknown *object, DWORD destination,
                                            void *context, DWORD flags);
WINOLEAPI HRESULT WINAPI CoUnmarshalInterface(IStream *stream, REFIID iid, void **result);

static BOOL serialized(VARTYPE vt)
{
    return vt == VT_UI4 || vt == VT_UI8 || vt == VT_R8 || vt == VT_CLSID || vt == VT_LPWSTR ||
           vt == (VT_VECTOR | VT_UI1);
}

/* the data an item adds after the entries. */
static HRESULT data_size(const PROPVARIANT *value, UINT32 *size)
{
    switch (value->vt) {
    case VT_CLSID:
        *size = sizeof(GUID);
        return S_OK;
    case VT_LPWSTR: {
        SIZE_T bytes = (wcslen(value->pwszVal) + 1) * sizeof(WCHAR);
        if (bytes > 0xFFFFFFFF)
            return E_INVALIDARG;
        *size = (UINT32)bytes;
        return S_OK;
    }
    case VT_VECTOR | VT_UI1:
        *size = value->caub.cElems;
        return S_OK;
    default:
        *size = 0;
        return S_OK;
    }
}

/* the blob size of the locked store, and its number of entries. */
static HRESULT measure(IMFAttributes *attributes, UINT32 *size, UINT32 *entries)
{
    UINT32 count = 0, items = 0, data = 0;
    HRESULT hr = attributes->lpVtbl->GetCount(attributes, &count);
    for (UINT32 i = 0; SUCCEEDED(hr) && i < count; i++) {
        GUID key;
        PROPVARIANT value = {};
        hr = attributes->lpVtbl->GetItemByIndex(attributes, i, &key, &value);
        if (FAILED(hr))
            break;
        if (serialized(value.vt)) {
            UINT32 extra;
            hr = data_size(&value, &extra);
            if (SUCCEEDED(hr) && extra >= ~data)
                hr = E_INVALIDARG;
            if (SUCCEEDED(hr)) {
                data += extra;
                items++;
            }
        }
        PropVariantClear(&value);
    }
    if (SUCCEEDED(hr)) {
        *size = data + BLOB_ENTRY * items + BLOB_HEADER;
        *entries = items;
    }
    return hr;
}

HRESULT WINAPI MFGetAttributesAsBlobSize(IMFAttributes *attributes, UINT32 *size)
{
    UINT32 entries, total;
    HRESULT hr = attributes->lpVtbl->LockStore(attributes);
    if (SUCCEEDED(hr))
        hr = measure(attributes, &total, &entries);
    attributes->lpVtbl->UnlockStore(attributes);
    if (SUCCEEDED(hr))
        *size = total;
    return hr;
}

HRESULT WINAPI MFGetAttributesAsBlob(IMFAttributes *attributes, UINT8 *buffer, UINT size)
{
    UINT32 entries = 0, total = 0, count = 0;
    HRESULT hr = attributes->lpVtbl->LockStore(attributes);
    if (SUCCEEDED(hr))
        hr = measure(attributes, &total, &entries);
    if (SUCCEEDED(hr) && total > size)
        hr = MF_E_BUFFERTOOSMALL;
    if (SUCCEEDED(hr))
        hr = attributes->lpVtbl->GetCount(attributes, &count);
    if (FAILED(hr))
        goto done;
    DWORD header[2] = {BLOB_MAGIC, entries};
    memcpy(buffer, header, sizeof(header));
    UINT8 *entry = buffer + BLOB_HEADER;
    UINT32 offset = BLOB_HEADER + BLOB_ENTRY * entries;
    for (UINT32 i = 0; i < count; i++) {
        GUID key;
        PROPVARIANT value = {};
        hr = attributes->lpVtbl->GetItemByIndex(attributes, i, &key, &value);
        if (FAILED(hr))
            break;
        if (!serialized(value.vt)) {
            PropVariantClear(&value);
            continue;
        }
        UINT8 raw[BLOB_ENTRY] = {};
        memcpy(raw, &key, sizeof(key));
        memcpy(raw + 16, &value.vt, sizeof(value.vt));
        const void *data = nullptr;
        UINT32 extra = 0;
        switch (value.vt) {
        case VT_UI4:
            memcpy(raw + 24, &value.ulVal, sizeof(value.ulVal));
            break;
        case VT_UI8:
            memcpy(raw + 24, &value.uhVal.QuadPart, sizeof(value.uhVal.QuadPart));
            break;
        case VT_R8:
            memcpy(raw + 24, &value.dblVal, sizeof(value.dblVal));
            break;
        case VT_CLSID:
            data = value.puuid;
            extra = sizeof(GUID);
            break;
        case VT_LPWSTR: {
            SIZE_T chars = wcslen(value.pwszVal);
            if (chars > MAX_BLOB_STRING)
                hr = E_INVALIDARG;
            data = value.pwszVal;
            extra = (UINT32)(chars + 1) * sizeof(WCHAR);
            break;
        }
        default:
            data = value.caub.pElems;
            extra = value.caub.cElems;
            break;
        }
        if (SUCCEEDED(hr) && data) {
            memcpy(raw + 24, &extra, sizeof(extra));
            memcpy(raw + 28, &offset, sizeof(offset));
            if (extra)
                memcpy(buffer + offset, data, extra);
            offset += extra;
        }
        PropVariantClear(&value);
        if (FAILED(hr))
            break;
        memcpy(entry, raw, sizeof(raw));
        entry += BLOB_ENTRY;
    }
done:
    attributes->lpVtbl->UnlockStore(attributes);
    return hr;
}

/* a range of the data area, after the entries and inside the buffer. */
static BOOL in_data(UINT32 offset, UINT32 length, UINT32 data_start, UINT size)
{
    return offset >= data_start && offset <= size && length <= size - offset;
}

HRESULT WINAPI MFInitAttributesFromBlob(IMFAttributes *attributes, const UINT8 *buffer, UINT size)
{
    HRESULT hr = attributes->lpVtbl->LockStore(attributes);
    if (FAILED(hr))
        goto done;
    DWORD header[2];
    if (size < BLOB_HEADER || (memcpy(header, buffer, sizeof(header)), header[0] != BLOB_MAGIC)) {
        hr = E_INVALIDARG;
        goto done;
    }
    ULONGLONG start = (ULONGLONG)header[1] * BLOB_ENTRY + BLOB_HEADER;
    if (start > 0xFFFFFFFF) {
        hr = HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
        goto done;
    }
    if (size < start) {
        hr = E_INVALIDARG;
        goto done;
    }
    hr = attributes->lpVtbl->DeleteAllItems(attributes);
    for (UINT32 i = 0; SUCCEEDED(hr) && i < header[1]; i++) {
        const UINT8 *entry = buffer + BLOB_HEADER + i * BLOB_ENTRY;
        GUID key;
        VARTYPE vt;
        UINT32 length, offset;
        memcpy(&key, entry, sizeof(key));
        memcpy(&vt, entry + 16, sizeof(vt));
        memcpy(&length, entry + 24, sizeof(length));
        memcpy(&offset, entry + 28, sizeof(offset));
        switch (vt) {
        case VT_UI4:
            hr = attributes->lpVtbl->SetUINT32(attributes, &key, length);
            break;
        case VT_UI8: {
            UINT64 value;
            memcpy(&value, entry + 24, sizeof(value));
            hr = attributes->lpVtbl->SetUINT64(attributes, &key, value);
            break;
        }
        case VT_R8: {
            double value;
            memcpy(&value, entry + 24, sizeof(value));
            hr = attributes->lpVtbl->SetDouble(attributes, &key, value);
            break;
        }
        case VT_CLSID: {
            if (!in_data(offset, sizeof(GUID), (UINT32)start, size)) {
                hr = E_INVALIDARG;
                break;
            }
            GUID value;
            memcpy(&value, buffer + offset, sizeof(value));
            hr = attributes->lpVtbl->SetGUID(attributes, &key, &value);
            break;
        }
        case VT_LPWSTR: {
            /* an even length ending in its only null. */
            if (length < 2 || (length & 1) || !in_data(offset, length, (UINT32)start, size)) {
                hr = E_INVALIDARG;
                break;
            }
            WCHAR *text = MfAlloc(length);
            if (!text) {
                hr = E_OUTOFMEMORY;
                break;
            }
            memcpy(text, buffer + offset, length);
            UINT32 chars = length / sizeof(WCHAR) - 1;
            if (text[chars] || wcslen(text) != chars)
                hr = E_INVALIDARG;
            else
                hr = attributes->lpVtbl->SetString(attributes, &key, text);
            MfFree(text);
            break;
        }
        case VT_VECTOR | VT_UI1:
            if (!in_data(offset, length, (UINT32)start, size))
                hr = E_INVALIDARG;
            else
                hr = attributes->lpVtbl->SetBlob(attributes, &key, buffer + offset, length);
            break;
        default:
            hr = E_INVALIDARG;
            break;
        }
    }
done:
    attributes->lpVtbl->UnlockStore(attributes);
    return hr;
}

/* ------------------------------------------------------------------ streams */

static HRESULT write_all(IStream *stream, const void *data, ULONG size)
{
    if (!stream)
        return MF_E_NOT_INITIALIZED;
    ULONG written = 0;
    HRESULT hr = stream->lpVtbl->Write(stream, data, size, &written);
    if (SUCCEEDED(hr) && written != size)
        hr = E_INVALIDARG;
    return hr;
}

static HRESULT read_all(IStream *stream, void *data, ULONG size)
{
    if (!stream)
        return MF_E_NOT_INITIALIZED;
    ULONG read = 0;
    HRESULT hr = stream->lpVtbl->Read(stream, data, size, &read);
    if (SUCCEEDED(hr) && read != size)
        hr = E_INVALIDARG;
    return hr;
}

/* the interfaces, by reference, each as its key and an in process marshaling of it. */
static HRESULT write_unknowns(IMFAttributes *attributes, IStream *stream)
{
    UINT32 count = 0, unknowns = 0;
    HRESULT hr = attributes->lpVtbl->LockStore(attributes);
    if (SUCCEEDED(hr))
        hr = attributes->lpVtbl->GetCount(attributes, &count);
    GUID *keys = SUCCEEDED(hr) ? MfAlloc((SIZE_T)count * sizeof(GUID)) : nullptr;
    if (SUCCEEDED(hr) && !keys)
        hr = E_OUTOFMEMORY;
    for (UINT32 i = 0; SUCCEEDED(hr) && i < count; i++) {
        MF_ATTRIBUTE_TYPE type;
        hr = attributes->lpVtbl->GetItemByIndex(attributes, i, &keys[unknowns], nullptr);
        if (SUCCEEDED(hr))
            hr = attributes->lpVtbl->GetItemType(attributes, &keys[unknowns], &type);
        if (SUCCEEDED(hr) && type == MF_ATTRIBUTE_IUNKNOWN)
            unknowns++;
    }
    attributes->lpVtbl->UnlockStore(attributes);
    if (SUCCEEDED(hr))
        hr = write_all(stream, &unknowns, sizeof(unknowns));
    for (UINT32 i = 0; SUCCEEDED(hr) && i < unknowns; i++) {
        IUnknown *object = nullptr;
        hr = attributes->lpVtbl->GetUnknown(attributes, &keys[i], &IID_IUnknown, (void **)&object);
        if (SUCCEEDED(hr))
            hr = write_all(stream, &keys[i], sizeof(GUID));
        if (SUCCEEDED(hr))
            hr = CoMarshalInterface(stream, &IID_IUnknown, object, MSHCTX_INPROC, nullptr, MSHLFLAGS_NORMAL);
        if (object)
            object->lpVtbl->Release(object);
    }
    MfFree(keys);
    return hr;
}

static HRESULT read_unknowns(IMFAttributes *attributes, IStream *stream)
{
    UINT32 count = 0;
    HRESULT hr = read_all(stream, &count, sizeof(count));
    for (UINT32 i = 0; SUCCEEDED(hr) && i < count; i++) {
        GUID key;
        IUnknown *object = nullptr;
        hr = read_all(stream, &key, sizeof(key));
        if (SUCCEEDED(hr))
            hr = CoUnmarshalInterface(stream, &IID_IUnknown, (void **)&object);
        if (SUCCEEDED(hr))
            hr = attributes->lpVtbl->SetUnknown(attributes, &key, object);
        if (object)
            object->lpVtbl->Release(object);
    }
    return hr;
}

/* the blob size, the blob, then the interfaces with MF_ATTRIBUTE_SERIALIZE_UNKNOWN_BYREF. */
HRESULT WINAPI MFSerializeAttributesToStream(IMFAttributes *attributes, DWORD options, IStream *stream)
{
    UINT32 size = 0;
    HRESULT hr = MFGetAttributesAsBlobSize(attributes, &size);
    if (FAILED(hr))
        return hr;
    UINT8 *blob = MfAlloc(size);
    if (!blob)
        return E_OUTOFMEMORY;
    hr = MFGetAttributesAsBlob(attributes, blob, size);
    if (SUCCEEDED(hr))
        hr = write_all(stream, &size, sizeof(size));
    if (SUCCEEDED(hr))
        hr = write_all(stream, blob, size);
    MfFree(blob);
    if (SUCCEEDED(hr) && (options & MF_ATTRIBUTE_SERIALIZE_UNKNOWN_BYREF))
        hr = write_unknowns(attributes, stream);
    return hr;
}

HRESULT WINAPI MFDeserializeAttributesFromStream(IMFAttributes *attributes, DWORD options, IStream *stream)
{
    UINT32 size = 0;
    HRESULT hr = read_all(stream, &size, sizeof(size));
    if (FAILED(hr))
        return hr;
    UINT8 *blob = MfAlloc(size);
    if (!blob)
        return E_OUTOFMEMORY;
    hr = read_all(stream, blob, size);
    if (SUCCEEDED(hr))
        hr = MFInitAttributesFromBlob(attributes, blob, size);
    MfFree(blob);
    if (SUCCEEDED(hr) && (options & MF_ATTRIBUTE_SERIALIZE_UNKNOWN_BYREF))
        hr = read_unknowns(attributes, stream);
    return hr;
}
