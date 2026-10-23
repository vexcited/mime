/*
 * version 2 performance counter providers, PerfStartProviderEx and friends. a provider writes counters to memory that
 * consumers read through the performance data helper, which does not exist here. the calls accept what a provider
 * does and return their own blocks so it runs, but the counters are never published.
 */
#include "advapi32_private.h"

typedef struct PERF_PROVIDER {
    struct PERF_PROVIDER *Next;
    GUID Id;
} PERF_PROVIDER;

/* an instance block header: counter set, id and name, with room for the counter values. */
typedef struct PERF_INSTANCE {
    GUID CounterSet;
    ULONG InstanceId;
    ULONG InstanceNameOffset;
    ULONG InstanceNameSize;
    ULONG Reserved;
    PVOID Counters[64];
} PERF_INSTANCE;

static SRWLOCK g_perf_lock = SRWLOCK_INIT;
static PERF_PROVIDER *g_providers;

/* the started provider the handle names, or NULL. the caller holds the lock. */
static PERF_PROVIDER *provider_of(HANDLE handle)
{
    for (PERF_PROVIDER *p = g_providers; p; p = p->Next)
        if (p == handle)
            return p;
    return nullptr;
}

ADVAPI ULONG WINAPI PerfStartProviderEx(PGUID id, PVOID context, HANDLE *handle)
{
    if (!id || !handle)
        return ERROR_INVALID_PARAMETER;
    PERF_PROVIDER *provider = LocalAlloc(LPTR, sizeof(*provider));
    if (!provider)
        return ERROR_NOT_ENOUGH_MEMORY;
    provider->Id = *id;
    AcquireSRWLockExclusive(&g_perf_lock);
    provider->Next = g_providers;
    g_providers = provider;
    ReleaseSRWLockExclusive(&g_perf_lock);
    *handle = provider;
    return ERROR_SUCCESS;
}

ADVAPI ULONG WINAPI PerfStopProvider(HANDLE handle)
{
    AcquireSRWLockExclusive(&g_perf_lock);
    PERF_PROVIDER **link = &g_providers;
    while (*link && *link != handle)
        link = &(*link)->Next;
    PERF_PROVIDER *provider = *link;
    if (provider)
        *link = provider->Next;
    ReleaseSRWLockExclusive(&g_perf_lock);
    if (!provider)
        return ERROR_INVALID_HANDLE;
    LocalFree(provider);
    return ERROR_SUCCESS;
}

static BOOL started(HANDLE handle)
{
    AcquireSRWLockShared(&g_perf_lock);
    BOOL found = provider_of(handle) != nullptr;
    ReleaseSRWLockShared(&g_perf_lock);
    return found;
}

ADVAPI ULONG WINAPI PerfSetCounterSetInfo(HANDLE handle, PVOID counter_set, ULONG size)
{
    if (!started(handle))
        return ERROR_INVALID_HANDLE;
    return counter_set && size >= 16 ? ERROR_SUCCESS : ERROR_INVALID_PARAMETER;
}

ADVAPI PVOID WINAPI PerfCreateInstance(HANDLE handle, const GUID *counter_set, PCWSTR name, ULONG id)
{
    if (!started(handle) || !counter_set || !name) {
        SetLastError(started(handle) ? ERROR_INVALID_PARAMETER : ERROR_INVALID_HANDLE);
        return nullptr;
    }
    PERF_INSTANCE *instance = LocalAlloc(LPTR, sizeof(*instance));
    if (!instance) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    instance->CounterSet = *counter_set;
    instance->InstanceId = id;
    SetLastError(ERROR_SUCCESS);
    return instance;
}

ADVAPI ULONG WINAPI PerfDeleteInstance(HANDLE handle, PVOID instance)
{
    if (!started(handle))
        return ERROR_INVALID_HANDLE;
    if (!instance)
        return ERROR_INVALID_PARAMETER;
    LocalFree(instance);
    return ERROR_SUCCESS;
}

ADVAPI ULONG WINAPI PerfSetCounterRefValue(HANDLE handle, PVOID instance, ULONG counter, PVOID address)
{
    if (!started(handle))
        return ERROR_INVALID_HANDLE;
    if (!instance || counter >= 64)
        return ERROR_INVALID_PARAMETER;
    ((PERF_INSTANCE *)instance)->Counters[counter] = address;
    return ERROR_SUCCESS;
}
