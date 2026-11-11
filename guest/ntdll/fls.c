#include "ntdll_private.h"

typedef struct FLS_DATA {
    LIST_ENTRY Links;
    PVOID Slots[RTL_FLS_MAXIMUM_AVAILABLE];
} FLS_DATA;

static LIST_ENTRY g_fls_list = {&g_fls_list, &g_fls_list};
static PRTL_FLS_CALLBACK g_fls_callbacks[RTL_FLS_MAXIMUM_AVAILABLE];
static ULONG g_fls_bitmap[RTL_FLS_MAXIMUM_AVAILABLE / 32];
static RTL_CRITICAL_SECTION g_fls_lock = RTL_CRITICAL_SECTION_INIT;

static BOOLEAN allocated(ULONG index)
{
    return index && index < RTL_FLS_MAXIMUM_AVAILABLE && ((g_fls_bitmap[index / 32] >> (index % 32)) & 1);
}

NTDLL_EXPORT NTSTATUS NTAPI RtlFlsAlloc(PRTL_FLS_CALLBACK callback, PULONG index)
{
    ULONG found = 0;
    RtlEnterCriticalSection(&g_fls_lock);
    /* index 0 stays unused so a zeroed variable never names a live slot. */
    for (ULONG i = 1; i < RTL_FLS_MAXIMUM_AVAILABLE; i++) {
        if (!allocated(i)) {
            g_fls_bitmap[i / 32] |= 1u << (i % 32);
            g_fls_callbacks[i] = callback;
            found = i;
            break;
        }
    }
    RtlLeaveCriticalSection(&g_fls_lock);
    if (!found)
        return STATUS_NO_MEMORY;
    FLS_DATA *data = NtCurrentTeb()->FlsData;
    if (data)
        data->Slots[found] = nullptr;
    *index = found;
    return STATUS_SUCCESS;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlFlsFree(ULONG index)
{
    RtlEnterCriticalSection(&g_fls_lock);
    if (!allocated(index)) {
        RtlLeaveCriticalSection(&g_fls_lock);
        return STATUS_INVALID_PARAMETER;
    }
    PRTL_FLS_CALLBACK callback = g_fls_callbacks[index];
    for (PLIST_ENTRY e = g_fls_list.Flink; e != &g_fls_list; e = e->Flink) {
        FLS_DATA *data = CONTAINING_RECORD(e, FLS_DATA, Links);
        PVOID value = data->Slots[index];
        data->Slots[index] = nullptr;
        if (value && callback)
            callback(value);
    }
    g_fls_callbacks[index] = nullptr;
    g_fls_bitmap[index / 32] &= ~(1u << (index % 32));
    RtlLeaveCriticalSection(&g_fls_lock);
    return STATUS_SUCCESS;
}

NTDLL_EXPORT PVOID NTAPI RtlFlsGetValue2(ULONG index)
{
    FLS_DATA *data = NtCurrentTeb()->FlsData;
    return data && index && index < RTL_FLS_MAXIMUM_AVAILABLE ? data->Slots[index] : nullptr;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlFlsGetValue(ULONG index, PVOID *value)
{
    if (!allocated(index))
        return STATUS_INVALID_PARAMETER;
    *value = RtlFlsGetValue2(index);
    return STATUS_SUCCESS;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlFlsSetValue(ULONG index, PVOID value)
{
    if (!allocated(index))
        return STATUS_INVALID_PARAMETER;
    PTEB teb = NtCurrentTeb();
    FLS_DATA *data = teb->FlsData;
    if (!data) {
        data = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, HEAP_ZERO_MEMORY, sizeof(FLS_DATA));
        if (!data)
            return STATUS_NO_MEMORY;
        RtlEnterCriticalSection(&g_fls_lock);
        InsertTailList(&g_fls_list, &data->Links);
        RtlLeaveCriticalSection(&g_fls_lock);
        teb->FlsData = data;
    }
    data->Slots[index] = value;
    return STATUS_SUCCESS;
}

/* flag 1 runs the callbacks of a thread's or fiber's data, flag 2 frees it. */
NTDLL_EXPORT void NTAPI RtlProcessFlsData(PVOID fls, ULONG flags)
{
    FLS_DATA *data = fls;
    if (!data)
        return;
    if (flags & 1) {
        RtlEnterCriticalSection(&g_fls_lock);
        for (ULONG i = 1; i < RTL_FLS_MAXIMUM_AVAILABLE; i++) {
            PVOID value = data->Slots[i];
            if (!value || !allocated(i))
                continue;
            data->Slots[i] = nullptr;
            PRTL_FLS_CALLBACK callback = g_fls_callbacks[i];
            if (callback)
                callback(value);
        }
        RemoveEntryList(&data->Links);
        InitializeListHead(&data->Links);
        RtlLeaveCriticalSection(&g_fls_lock);
    }
    if (flags & 2)
        RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, data);
}
