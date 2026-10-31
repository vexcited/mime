/*
 * version 2 performance counter providers, PerfStartProviderEx and friends. a provider writes counters to memory that
 * consumers read through the performance data helper, which does not exist here. the calls accept what a provider
 * does and return their own blocks so it runs, but the counters are never published.
 */
#include "advapi32_private.h"

#define PERF_MAX_COUNTERS 64

/* what a provider says about a counter set: the id and size of each counter, in the order it gave them. */
typedef struct PERF_SET {
    struct PERF_SET *Next;
    GUID Id;
    ULONG Count;
    ULONG CounterIds[PERF_MAX_COUNTERS];
    ULONG CounterSizes[PERF_MAX_COUNTERS];
} PERF_SET;

typedef struct PERF_PROVIDER {
    struct PERF_PROVIDER *Next;
    GUID Id;
    PERF_SET *Sets;
} PERF_PROVIDER;

/* the header of a counter set in the template a provider hands over, followed by one counter record each. */
typedef struct PERF_SET_HEADER {
    GUID CounterSetGuid;
    GUID ProviderGuid;
    ULONG NumCounters;
    ULONG InstanceType;
} PERF_SET_HEADER;

typedef struct PERF_COUNTER_RECORD {
    ULONG CounterId;
    ULONG Type;
    ULONGLONG Attrib;
    ULONG Size;
    ULONG DetailLevel;
    LONG Scale;
    ULONG Offset;
} PERF_COUNTER_RECORD;

/* an instance block header: counter set, id and name, with room for the counter values. */
typedef struct PERF_INSTANCE {
    GUID CounterSet;
    ULONG InstanceId;
    ULONG InstanceNameOffset;
    ULONG InstanceNameSize;
    ULONG Reserved;
    PVOID Counters[PERF_MAX_COUNTERS];
    ULONG Count;
    ULONG CounterIds[PERF_MAX_COUNTERS];
    ULONG CounterSizes[PERF_MAX_COUNTERS];
    volatile ULONGLONG Values[PERF_MAX_COUNTERS];
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
    while (provider->Sets) {
        PERF_SET *set = provider->Sets;
        provider->Sets = set->Next;
        LocalFree(set);
    }
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
    if (!counter_set || size < sizeof(PERF_SET_HEADER))
        return ERROR_INVALID_PARAMETER;
    const PERF_SET_HEADER *header = counter_set;
    if (header->NumCounters > PERF_MAX_COUNTERS ||
        size < sizeof(*header) + (SIZE_T)header->NumCounters * sizeof(PERF_COUNTER_RECORD))
        return ERROR_INVALID_PARAMETER;
    PERF_SET *set = LocalAlloc(LPTR, sizeof(*set));
    if (!set)
        return ERROR_NOT_ENOUGH_MEMORY;
    set->Id = header->CounterSetGuid;
    set->Count = header->NumCounters;
    const PERF_COUNTER_RECORD *records = (const PERF_COUNTER_RECORD *)(header + 1);
    for (ULONG i = 0; i < set->Count; i++) {
        set->CounterIds[i] = records[i].CounterId;
        set->CounterSizes[i] = records[i].Size;
    }
    AcquireSRWLockExclusive(&g_perf_lock);
    PERF_PROVIDER *provider = provider_of(handle);
    if (provider) {
        set->Next = provider->Sets;
        provider->Sets = set;
    }
    ReleaseSRWLockExclusive(&g_perf_lock);
    if (!provider)
        LocalFree(set);
    return provider ? ERROR_SUCCESS : ERROR_INVALID_HANDLE;
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
    AcquireSRWLockShared(&g_perf_lock);
    PERF_PROVIDER *provider = provider_of(handle);
    for (PERF_SET *set = provider ? provider->Sets : nullptr; set; set = set->Next) {
        if (memcmp(&set->Id, counter_set, sizeof(GUID)) != 0)
            continue;
        instance->Count = set->Count;
        memcpy(instance->CounterIds, set->CounterIds, sizeof(set->CounterIds));
        memcpy(instance->CounterSizes, set->CounterSizes, sizeof(set->CounterSizes));
        break;
    }
    ReleaseSRWLockShared(&g_perf_lock);
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

/* the slot of a counter in an instance, by the id the template gave it. */
static ULONG counter_slot(const PERF_INSTANCE *instance, ULONG counter, ULONG size, ULONG *error)
{
    *error = ERROR_SUCCESS;
    for (ULONG i = 0; i < instance->Count; i++) {
        if (instance->CounterIds[i] != counter)
            continue;
        if (instance->CounterSizes[i] != size)
            *error = ERROR_INVALID_PARAMETER;
        return i;
    }
    /* an instance of a set the provider did not describe takes the counter number as the slot. */
    if (!instance->Count && counter < PERF_MAX_COUNTERS)
        return counter;
    *error = instance->Count ? ERROR_NOT_FOUND : ERROR_INVALID_PARAMETER;
    return 0;
}

typedef enum { COUNTER_SET, COUNTER_INCREMENT, COUNTER_DECREMENT } COUNTER_OPERATION;

static ULONG change_counter(HANDLE handle, PVOID instance_block, ULONG counter, ULONGLONG value, ULONG size,
                            COUNTER_OPERATION operation)
{
    if (!started(handle))
        return ERROR_INVALID_HANDLE;
    if (!instance_block)
        return ERROR_INVALID_PARAMETER;
    PERF_INSTANCE *instance = instance_block;
    ULONG error;
    ULONG slot = counter_slot(instance, counter, size, &error);
    if (error)
        return error;
    ULONGLONG mask = size == sizeof(ULONG) ? 0xFFFFFFFFull : ~0ull;
    if (operation == COUNTER_SET)
        __atomic_store_n(&instance->Values[slot], value & mask, __ATOMIC_RELAXED);
    else if (operation == COUNTER_INCREMENT)
        __atomic_fetch_add(&instance->Values[slot], value & mask, __ATOMIC_RELAXED);
    else
        __atomic_fetch_sub(&instance->Values[slot], value & mask, __ATOMIC_RELAXED);
    return ERROR_SUCCESS;
}

ADVAPI ULONG WINAPI PerfSetULongCounterValue(HANDLE handle, PVOID instance, ULONG counter, ULONG value)
{
    return change_counter(handle, instance, counter, value, sizeof(ULONG), COUNTER_SET);
}

ADVAPI ULONG WINAPI PerfSetULongLongCounterValue(HANDLE handle, PVOID instance, ULONG counter, ULONGLONG value)
{
    return change_counter(handle, instance, counter, value, sizeof(ULONGLONG), COUNTER_SET);
}

ADVAPI ULONG WINAPI PerfIncrementULongCounterValue(HANDLE handle, PVOID instance, ULONG counter, ULONG value)
{
    return change_counter(handle, instance, counter, value, sizeof(ULONG), COUNTER_INCREMENT);
}

ADVAPI ULONG WINAPI PerfIncrementULongLongCounterValue(HANDLE handle, PVOID instance, ULONG counter, ULONGLONG value)
{
    return change_counter(handle, instance, counter, value, sizeof(ULONGLONG), COUNTER_INCREMENT);
}

ADVAPI ULONG WINAPI PerfDecrementULongCounterValue(HANDLE handle, PVOID instance, ULONG counter, ULONG value)
{
    return change_counter(handle, instance, counter, value, sizeof(ULONG), COUNTER_DECREMENT);
}

ADVAPI ULONG WINAPI PerfDecrementULongLongCounterValue(HANDLE handle, PVOID instance, ULONG counter, ULONGLONG value)
{
    return change_counter(handle, instance, counter, value, sizeof(ULONGLONG), COUNTER_DECREMENT);
}
