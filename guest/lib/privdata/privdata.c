/*
 * private data as windows' directx keeps it: setting a guid again replaces
 * its value, setting it with no data removes it, and an interface is held
 * with a reference and handed out with another.
 */
#include "privdata.h"

#define DXGI_ERROR_NOT_FOUND ((HRESULT)0x887A0002)
#define DXGI_ERROR_MORE_DATA ((HRESULT)0x887A0003)

struct MimePrivateEntry {
    MimePrivateEntry *next;
    GUID guid;
    IUnknown *unknown;
    UINT size;
    BYTE bytes[];
};

static void entry_free(MimePrivateEntry *entry)
{
    if (entry->unknown)
        entry->unknown->lpVtbl->Release(entry->unknown);
    HeapFree(GetProcessHeap(), 0, entry);
}

/* unlinks the entry of the guid, the caller holding the lock. */
static MimePrivateEntry *take(MimePrivateData *data, REFGUID guid)
{
    for (MimePrivateEntry **link = &data->first; *link; link = &(*link)->next)
        if (IsEqualGUID(&(*link)->guid, guid)) {
            MimePrivateEntry *entry = *link;
            *link = entry->next;
            return entry;
        }
    return NULL;
}

static HRESULT store(MimePrivateData *data, REFGUID guid, UINT size, const void *bytes, IUnknown *unknown)
{
    MimePrivateEntry *entry = NULL;
    if (size || unknown) {
        entry = HeapAlloc(GetProcessHeap(), 0, sizeof(*entry) + size);
        if (!entry)
            return E_OUTOFMEMORY;
        entry->guid = *guid;
        entry->unknown = unknown;
        entry->size = size;
        if (unknown)
            unknown->lpVtbl->AddRef(unknown);
        else
            memcpy(entry->bytes, bytes, size);
    }
    AcquireSRWLockExclusive(&data->lock);
    MimePrivateEntry *old = take(data, guid);
    if (entry) {
        entry->next = data->first;
        data->first = entry;
    }
    ReleaseSRWLockExclusive(&data->lock);
    if (old)
        entry_free(old);
    return S_OK;
}

HRESULT MimePrivateSet(MimePrivateData *data, REFGUID guid, UINT size, const void *bytes)
{
    if (!guid || (size && !bytes))
        return E_INVALIDARG;
    return store(data, guid, bytes ? size : 0, bytes, NULL);
}

HRESULT MimePrivateSetInterface(MimePrivateData *data, REFGUID guid, const IUnknown *unknown)
{
    if (!guid)
        return E_INVALIDARG;
    return store(data, guid, unknown ? sizeof(IUnknown *) : 0, NULL, (IUnknown *)unknown);
}

HRESULT MimePrivateGet(MimePrivateData *data, REFGUID guid, UINT *size, void *bytes)
{
    if (!guid || !size)
        return E_INVALIDARG;
    HRESULT hr = S_OK;
    AcquireSRWLockShared(&data->lock);
    MimePrivateEntry *entry = data->first;
    while (entry && !IsEqualGUID(&entry->guid, guid))
        entry = entry->next;
    if (!entry) {
        *size = 0;
        hr = DXGI_ERROR_NOT_FOUND;
    } else if (!bytes) {
        *size = entry->size;
    } else if (*size < entry->size) {
        *size = entry->size;
        hr = DXGI_ERROR_MORE_DATA;
    } else {
        *size = entry->size;
        if (entry->unknown) {
            entry->unknown->lpVtbl->AddRef(entry->unknown);
            memcpy(bytes, &entry->unknown, sizeof(IUnknown *));
        } else {
            memcpy(bytes, entry->bytes, entry->size);
        }
    }
    ReleaseSRWLockShared(&data->lock);
    return hr;
}

void MimePrivateClear(MimePrivateData *data)
{
    MimePrivateEntry *entry = data->first;
    data->first = NULL;
    while (entry) {
        MimePrivateEntry *next = entry->next;
        entry_free(entry);
        entry = next;
    }
}
