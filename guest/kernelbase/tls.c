/* thread local storage and fiber local storage. */
#include "kernelbase_private.h"

#define TLS_TOTAL (TLS_MINIMUM_AVAILABLE + TLS_EXPANSION_SLOTS)

/* every thread's teb is linked through TlsLinks so freed slots can be cleared everywhere. */
static LIST_ENTRY g_tls_threads = {&g_tls_threads, &g_tls_threads};
static RTL_CRITICAL_SECTION g_tls_lock = RTL_CRITICAL_SECTION_INIT;

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

/* the slots and their callbacks live in ntdll, which runs them as a thread or the process ends. */

DWORD WINAPI FlsAlloc(PFLS_CALLBACK_FUNCTION callback)
{
    ULONG index;
    if (!NT_SUCCESS(RtlFlsAlloc(callback, &index))) {
        KbSetError(ERROR_NO_MORE_ITEMS);
        return FLS_OUT_OF_INDEXES;
    }
    return index;
}

BOOL WINAPI FlsFree(DWORD index)
{
    if (!NT_SUCCESS(RtlFlsFree(index))) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

PVOID WINAPI FlsGetValue2(DWORD index)
{
    if (!index || index >= RTL_FLS_MAXIMUM_AVAILABLE) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    return RtlFlsGetValue2(index);
}

PVOID WINAPI FlsGetValue(DWORD index)
{
    PVOID value;
    if (!NT_SUCCESS(RtlFlsGetValue(index, &value))) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    KbSetError(ERROR_SUCCESS);
    return value;
}

BOOL WINAPI FlsSetValue(DWORD index, PVOID value)
{
    NTSTATUS status = RtlFlsSetValue(index, value);
    if (!NT_SUCCESS(status)) {
        KbSetError(status == STATUS_NO_MEMORY ? ERROR_NOT_ENOUGH_MEMORY : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

void KbThreadDetach(void)
{
    unlink_thread();
}
