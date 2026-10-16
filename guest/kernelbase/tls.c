/*
 * thread local storage, the TlsAlloc family, and fiber local storage.
 */
#include "kernelbase_private.h"

#define TLS_TOTAL (TLS_MINIMUM_AVAILABLE + TLS_EXPANSION_SLOTS)
#define FLS_SLOTS 512

/* every thread's teb is linked through TlsLinks so freed slots can be cleared everywhere. */
static LIST_ENTRY g_tls_threads = {&g_tls_threads, &g_tls_threads};
static RTL_CRITICAL_SECTION g_tls_lock = RTL_CRITICAL_SECTION_INIT;

typedef struct FLS_DATA {
    LIST_ENTRY Links;
    PVOID Slots[FLS_SLOTS];
} FLS_DATA;

static LIST_ENTRY g_fls_list = {&g_fls_list, &g_fls_list};
static PFLS_CALLBACK_FUNCTION g_fls_callbacks[FLS_SLOTS];
static ULONG g_fls_bitmap[FLS_SLOTS / 32];
static RTL_CRITICAL_SECTION g_fls_lock = RTL_CRITICAL_SECTION_INIT;

void KbThreadAttach(void)
{
    PTEB teb = NtCurrentTeb();
    RtlEnterCriticalSection(&g_tls_lock);
    InsertTailList(&g_tls_threads, &teb->TlsLinks);
    RtlLeaveCriticalSection(&g_tls_lock);
}

static void unlink_thread(void)
{
    PTEB teb = NtCurrentTeb();
    RtlEnterCriticalSection(&g_tls_lock);
    if (teb->TlsLinks.Flink && teb->TlsLinks.Flink != &teb->TlsLinks) {
        RemoveEntryList(&teb->TlsLinks);
        InitializeListHead(&teb->TlsLinks);
    }
    PVOID *expansion = teb->TlsExpansionSlots;
    teb->TlsExpansionSlots = nullptr;
    RtlLeaveCriticalSection(&g_tls_lock);
    KbFree(expansion);
}

/* ------------------------------------------------------------------ tls */

static BOOL bit_test(const ULONG *bits, ULONG i)
{
    return (bits[i / 32] >> (i % 32)) & 1;
}

static void bit_set(ULONG *bits, ULONG i)
{
    bits[i / 32] |= 1u << (i % 32);
}

static void bit_clear(ULONG *bits, ULONG i)
{
    bits[i / 32] &= ~(1u << (i % 32));
}

DWORD WINAPI TlsAlloc(void)
{
    PPEB peb = KbPeb();
    DWORD index = TLS_OUT_OF_INDEXES;
    RtlAcquirePebLock();
    for (ULONG i = 0; i < TLS_MINIMUM_AVAILABLE; i++) {
        if (!bit_test(peb->TlsBitmapBits, i)) {
            bit_set(peb->TlsBitmapBits, i);
            index = i;
            break;
        }
    }
    if (index == TLS_OUT_OF_INDEXES) {
        for (ULONG i = 0; i < TLS_EXPANSION_SLOTS; i++) {
            if (!bit_test(peb->TlsExpansionBitmapBits, i)) {
                bit_set(peb->TlsExpansionBitmapBits, i);
                index = TLS_MINIMUM_AVAILABLE + i;
                break;
            }
        }
    }
    RtlReleasePebLock();
    if (index == TLS_OUT_OF_INDEXES) {
        KbSetError(ERROR_NO_MORE_ITEMS);
        return index;
    }
    /* the slot may hold a stale value from a previous owner in this thread. */
    PTEB teb = NtCurrentTeb();
    if (index < TLS_MINIMUM_AVAILABLE)
        teb->TlsSlots[index] = nullptr;
    else if (teb->TlsExpansionSlots)
        teb->TlsExpansionSlots[index - TLS_MINIMUM_AVAILABLE] = nullptr;
    return index;
}

BOOL WINAPI TlsFree(DWORD index)
{
    PPEB peb = KbPeb();
    if (index >= TLS_TOTAL) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    RtlAcquirePebLock();
    BOOL allocated = index < TLS_MINIMUM_AVAILABLE ? bit_test(peb->TlsBitmapBits, index)
                                                   : bit_test(peb->TlsExpansionBitmapBits, index - TLS_MINIMUM_AVAILABLE);
    if (!allocated) {
        RtlReleasePebLock();
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    RtlEnterCriticalSection(&g_tls_lock);
    for (PLIST_ENTRY e = g_tls_threads.Flink; e != &g_tls_threads; e = e->Flink) {
        PTEB teb = CONTAINING_RECORD(e, TEB, TlsLinks);
        if (index < TLS_MINIMUM_AVAILABLE)
            teb->TlsSlots[index] = nullptr;
        else if (teb->TlsExpansionSlots)
            teb->TlsExpansionSlots[index - TLS_MINIMUM_AVAILABLE] = nullptr;
    }
    RtlLeaveCriticalSection(&g_tls_lock);
    if (index < TLS_MINIMUM_AVAILABLE)
        bit_clear(peb->TlsBitmapBits, index);
    else
        bit_clear(peb->TlsExpansionBitmapBits, index - TLS_MINIMUM_AVAILABLE);
    RtlReleasePebLock();
    return TRUE;
}

LPVOID WINAPI TlsGetValue2(DWORD index)
{
    PTEB teb = NtCurrentTeb();
    if (index < TLS_MINIMUM_AVAILABLE)
        return teb->TlsSlots[index];
    if (index >= TLS_TOTAL) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    return teb->TlsExpansionSlots ? teb->TlsExpansionSlots[index - TLS_MINIMUM_AVAILABLE] : nullptr;
}

LPVOID WINAPI TlsGetValue(DWORD index)
{
    if (index >= TLS_TOTAL) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    KbSetError(ERROR_SUCCESS);
    return TlsGetValue2(index);
}

BOOL WINAPI TlsSetValue(DWORD index, LPVOID value)
{
    PTEB teb = NtCurrentTeb();
    if (index < TLS_MINIMUM_AVAILABLE) {
        teb->TlsSlots[index] = value;
        return TRUE;
    }
    if (index >= TLS_TOTAL) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!teb->TlsExpansionSlots) {
        PVOID *slots = KbAllocZero(TLS_EXPANSION_SLOTS * sizeof(PVOID));
        if (!slots) {
            KbSetError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        RtlEnterCriticalSection(&g_tls_lock);
        teb->TlsExpansionSlots = slots;
        RtlLeaveCriticalSection(&g_tls_lock);
    }
    teb->TlsExpansionSlots[index - TLS_MINIMUM_AVAILABLE] = value;
    return TRUE;
}

/* ------------------------------------------------------------------ fls */

DWORD WINAPI FlsAlloc(PFLS_CALLBACK_FUNCTION callback)
{
    DWORD index = FLS_OUT_OF_INDEXES;
    RtlEnterCriticalSection(&g_fls_lock);
    /* index 0 stays unused so a zeroed variable never names a live slot. */
    for (ULONG i = 1; i < FLS_SLOTS; i++) {
        if (!bit_test(g_fls_bitmap, i)) {
            bit_set(g_fls_bitmap, i);
            g_fls_callbacks[i] = callback;
            index = i;
            break;
        }
    }
    RtlLeaveCriticalSection(&g_fls_lock);
    if (index == FLS_OUT_OF_INDEXES) {
        KbSetError(ERROR_NO_MORE_ITEMS);
        return index;
    }
    FLS_DATA *data = NtCurrentTeb()->FlsData;
    if (data)
        data->Slots[index] = nullptr;
    return index;
}

static BOOL valid_fls(DWORD index)
{
    return index && index < FLS_SLOTS && bit_test(g_fls_bitmap, index);
}

BOOL WINAPI FlsFree(DWORD index)
{
    RtlEnterCriticalSection(&g_fls_lock);
    if (!valid_fls(index)) {
        RtlLeaveCriticalSection(&g_fls_lock);
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PFLS_CALLBACK_FUNCTION callback = g_fls_callbacks[index];
    for (PLIST_ENTRY e = g_fls_list.Flink; e != &g_fls_list; e = e->Flink) {
        FLS_DATA *data = CONTAINING_RECORD(e, FLS_DATA, Links);
        PVOID value = data->Slots[index];
        data->Slots[index] = nullptr;
        if (value && callback)
            callback(value);
    }
    g_fls_callbacks[index] = nullptr;
    bit_clear(g_fls_bitmap, index);
    RtlLeaveCriticalSection(&g_fls_lock);
    return TRUE;
}

PVOID WINAPI FlsGetValue2(DWORD index)
{
    if (!index || index >= FLS_SLOTS) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    FLS_DATA *data = NtCurrentTeb()->FlsData;
    return data ? data->Slots[index] : nullptr;
}

PVOID WINAPI FlsGetValue(DWORD index)
{
    if (!valid_fls(index)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    KbSetError(ERROR_SUCCESS);
    return FlsGetValue2(index);
}

BOOL WINAPI FlsSetValue(DWORD index, PVOID value)
{
    if (!valid_fls(index)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PTEB teb = NtCurrentTeb();
    FLS_DATA *data = teb->FlsData;
    if (!data) {
        data = KbAllocZero(sizeof(FLS_DATA));
        if (!data) {
            KbSetError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        RtlEnterCriticalSection(&g_fls_lock);
        InsertTailList(&g_fls_list, &data->Links);
        RtlLeaveCriticalSection(&g_fls_lock);
        teb->FlsData = data;
    }
    data->Slots[index] = value;
    return TRUE;
}

/* runs the callbacks of every live slot of the exiting thread, then frees its data. */
void KbFlsThreadCleanup(void)
{
    PTEB teb = NtCurrentTeb();
    FLS_DATA *data = teb->FlsData;
    if (!data)
        return;
    RtlEnterCriticalSection(&g_fls_lock);
    for (ULONG i = 1; i < FLS_SLOTS; i++) {
        PVOID value = data->Slots[i];
        if (!value || !bit_test(g_fls_bitmap, i))
            continue;
        data->Slots[i] = nullptr;
        PFLS_CALLBACK_FUNCTION callback = g_fls_callbacks[i];
        if (callback)
            callback(value);
    }
    RemoveEntryList(&data->Links);
    teb->FlsData = nullptr;
    RtlLeaveCriticalSection(&g_fls_lock);
    KbFree(data);
}

void KbThreadDetach(void)
{
    KbFlsThreadCleanup();
    unlink_thread();
}

void KbProcessDetach(void)
{
    KbFlsThreadCleanup();
}
