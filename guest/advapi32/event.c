/* providers remain disabled while the guest has no tracing sessions. */
#include "advapi32_private.h"

typedef struct EVENT_PROVIDER {
    struct EVENT_PROVIDER *Next;
    ULONGLONG Handle;
    PVOID Traits;
    BOOLEAN DescriptorType;
} EVENT_PROVIDER;
static SRWLOCK provider_lock;
static EVENT_PROVIDER *providers;
static ULONGLONG next_provider;

ADVAPI ULONG WINAPI EventRegister(const GUID *id, PVOID callback, PVOID context, ULONGLONG *handle)
{
    if (!handle)
        return ERROR_INVALID_PARAMETER;
    *handle = 0;
    if (!id)
        return ERROR_INVALID_PARAMETER;
    EVENT_PROVIDER *provider = AdvAlloc(sizeof(*provider));
    if (!provider)
        return ERROR_NOT_ENOUGH_MEMORY;
    provider->Traits = NULL;
    provider->DescriptorType = FALSE;
    AcquireSRWLockExclusive(&provider_lock);
    provider->Handle = ++next_provider;
    provider->Next = providers;
    providers = provider;
    *handle = provider->Handle;
    ReleaseSRWLockExclusive(&provider_lock);
    return ERROR_SUCCESS;
}

ADVAPI ULONG WINAPI EventUnregister(ULONGLONG handle)
{
    if (!handle)
        return ERROR_SUCCESS;
    AcquireSRWLockExclusive(&provider_lock);
    EVENT_PROVIDER **link = &providers;
    while (*link && (*link)->Handle != handle)
        link = &(*link)->Next;
    EVENT_PROVIDER *provider = *link;
    if (provider)
        *link = provider->Next;
    ReleaseSRWLockExclusive(&provider_lock);
    if (!provider)
        return ERROR_INVALID_HANDLE;
    AdvFree(provider->Traits);
    AdvFree(provider);
    return ERROR_SUCCESS;
}

ADVAPI ULONG WINAPI EventWriteTransfer(ULONGLONG handle, const void *descriptor, const GUID *activity,
    const GUID *related, ULONG count, const void *data)
{
    if (!descriptor || count > 128 || (count && !data))
        return ERROR_INVALID_PARAMETER;
    AcquireSRWLockShared(&provider_lock);
    EVENT_PROVIDER *provider = providers;
    while (provider && provider->Handle != handle)
        provider = provider->Next;
    BOOL exists = provider != NULL;
    ReleaseSRWLockShared(&provider_lock);
    return exists ? ERROR_SUCCESS : ERROR_INVALID_HANDLE;
}

ADVAPI ULONG WINAPI EventWrite(ULONGLONG handle, const void *descriptor, ULONG count, const void *data)
{
    return EventWriteTransfer(handle, descriptor, NULL, NULL, count, data);
}

ADVAPI BOOLEAN WINAPI EventEnabled(ULONGLONG handle, const void *descriptor)
{
    return FALSE;
}

ADVAPI BOOLEAN WINAPI EventProviderEnabled(ULONGLONG handle, UCHAR level, ULONGLONG keyword)
{
    return FALSE;
}

ADVAPI ULONG WINAPI EventSetInformation(ULONGLONG handle, ULONG kind, const void *data, ULONG size)
{
    if (size && !data)
        return ERROR_INVALID_PARAMETER;
    if (kind != 0 && kind != 2 && kind != 3)
        return ERROR_NOT_SUPPORTED;
    PVOID traits = NULL;
    if (kind == 2) {
        if (size < 3 || size > 0xffff || *(const USHORT *)data != size)
            return ERROR_INVALID_PARAMETER;
        ULONG offset = 2;
        const BYTE *bytes = data;
        while (offset < size && bytes[offset]) offset++;
        if (offset == size) return ERROR_INVALID_PARAMETER;
        offset++;
        while (offset < size) {
            if (size - offset < 3) return ERROR_INVALID_PARAMETER;
            USHORT length;
            memcpy(&length, bytes + offset, sizeof(length));
            if (length < 3 || length > size - offset) return ERROR_INVALID_PARAMETER;
            offset += length;
        }
        traits = AdvAlloc(size);
        if (!traits) return ERROR_NOT_ENOUGH_MEMORY;
        memcpy(traits, data, size);
    } else if (kind == 3 && (size != 1 || *(const BYTE *)data > 1)) {
        return ERROR_INVALID_PARAMETER;
    }
    AcquireSRWLockExclusive(&provider_lock);
    EVENT_PROVIDER *provider = providers;
    while (provider && provider->Handle != handle) provider = provider->Next;
    if (provider && kind == 2) {
        PVOID old = provider->Traits;
        provider->Traits = traits;
        provider->DescriptorType = TRUE;
        traits = old;
    } else if (provider && kind == 3) {
        provider->DescriptorType = *(const BYTE *)data;
    }
    ReleaseSRWLockExclusive(&provider_lock);
    AdvFree(traits);
    return provider ? ERROR_SUCCESS : ERROR_INVALID_PARAMETER;
}
