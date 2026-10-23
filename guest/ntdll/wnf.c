/*
 * user mode side of the windows notification facility. the kernel keeps names and their data, subscriptions
 * live here. the kernel sets one process event whenever a subscribed name changes, and a delivery thread then
 * compares each subscription's change stamp with the kernel's and calls the callbacks that differ. a change
 * during a callback sets the event again, so none is missed.
 *
 * package identity is a token security attribute that desktop processes lack, so its queries return
 * STATUS_NOT_FOUND, like Windows for an unpackaged process.
 */
#include "ntdll_private.h"

#define SUBSCRIPTION_EVENT_MASK 0x11

typedef struct WNF_USER_SUBSCRIPTION {
    struct WNF_USER_SUBSCRIPTION *Next;
    WNF_STATE_NAME Name;
    WNF_CHANGE_STAMP Stamp; /* the last stamp given to the callback. */
    PWNF_USER_CALLBACK Callback;
    PVOID Context;
    WNF_TYPE_ID TypeId;
    BOOLEAN HasType;
    BOOLEAN Removed;
    LONG Running;
    ULONG RunningThread;
} WNF_USER_SUBSCRIPTION;

static RTL_SRWLOCK g_lock;
static WNF_USER_SUBSCRIPTION *g_subscriptions;
static HANDLE g_event;
static BOOLEAN g_started;

static BOOLEAN same_name(const WNF_STATE_NAME *a, const WNF_STATE_NAME *b)
{
    return a->Data[0] == b->Data[0] && a->Data[1] == b->Data[1];
}

/* ------------------------------------------------------------------ publishing and querying */

NTDLL_EXPORT NTSTATUS NTAPI RtlPublishWnfStateData(WNF_STATE_NAME state_name, PCWNF_TYPE_ID type_id, const VOID *buffer,
                                                   ULONG length, const VOID *explicit_scope)
{
    return NtUpdateWnfStateData(&state_name, buffer, length, type_id, explicit_scope, 0, 0);
}

NTDLL_EXPORT NTSTATUS NTAPI RtlTestAndPublishWnfStateData(WNF_STATE_NAME state_name, PCWNF_TYPE_ID type_id,
                                                          const VOID *buffer, ULONG length, const VOID *explicit_scope,
                                                          WNF_CHANGE_STAMP matching_stamp)
{
    return NtUpdateWnfStateData(&state_name, buffer, length, type_id, explicit_scope, matching_stamp, 1);
}

/* a name holds at most 4096 bytes. */
NTDLL_EXPORT NTSTATUS NTAPI RtlQueryWnfStateDataWithExplicitScope(PWNF_CHANGE_STAMP change_stamp,
                                                                  WNF_STATE_NAME state_name, const VOID *explicit_scope,
                                                                  PWNF_USER_CALLBACK callback, PVOID context,
                                                                  PCWNF_TYPE_ID type_id)
{
    UCHAR buffer[WNF_MAX_STATE_SIZE];
    ULONG size = sizeof(buffer);
    WNF_CHANGE_STAMP stamp = 0;
    NTSTATUS status = NtQueryWnfStateData(&state_name, type_id, explicit_scope, &stamp, buffer, &size);
    if (!NT_SUCCESS(status))
        return status;
    *change_stamp = stamp;
    return callback(state_name, stamp, (PWNF_TYPE_ID)type_id, context, buffer, size);
}

NTDLL_EXPORT NTSTATUS NTAPI RtlQueryWnfStateData(PWNF_CHANGE_STAMP change_stamp, WNF_STATE_NAME state_name,
                                                 PWNF_USER_CALLBACK callback, PVOID context, PCWNF_TYPE_ID type_id)
{
    return RtlQueryWnfStateDataWithExplicitScope(change_stamp, state_name, nullptr, callback, context, type_id);
}

NTDLL_EXPORT BOOLEAN NTAPI RtlEqualWnfChangeStamps(WNF_CHANGE_STAMP first, WNF_CHANGE_STAMP second)
{
    return first == second;
}

/* ------------------------------------------------------------------ subscriptions */

static void free_subscription(WNF_USER_SUBSCRIPTION *s)
{
    RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, s);
}

/* called with the lock held. the kernel is told when this was the last subscription to its name. */
static void unlink_subscription(WNF_USER_SUBSCRIPTION *s)
{
    WNF_USER_SUBSCRIPTION **at = &g_subscriptions;
    while (*at && *at != s)
        at = &(*at)->Next;
    if (*at)
        *at = s->Next;
    BOOLEAN other = FALSE;
    for (WNF_USER_SUBSCRIPTION *p = g_subscriptions; p; p = p->Next)
        if (same_name(&p->Name, &s->Name))
            other = TRUE;
    if (!other)
        NtUnsubscribeWnfStateChange(&s->Name);
}

/* the first subscription starts the delivery thread. */
static DWORD NTAPI deliver(PVOID parameter);

static NTSTATUS start_delivery(void)
{
    if (g_started)
        return STATUS_SUCCESS;
    HANDLE event;
    NTSTATUS status = NtCreateEvent(&event, EVENT_ALL_ACCESS, nullptr, SynchronizationEvent, FALSE);
    if (!NT_SUCCESS(status))
        return status;
    status = NtSetWnfProcessNotificationEvent(event);
    if (!NT_SUCCESS(status)) {
        NtClose(event);
        return status;
    }
    g_event = event;
    HANDLE thread;
    status = NtCreateThreadEx(&thread, THREAD_ALL_ACCESS, nullptr, NtCurrentProcess(), (PVOID)deliver, nullptr, 0, 0, 0,
                              0, nullptr);
    if (!NT_SUCCESS(status)) {
        NtSetWnfProcessNotificationEvent(nullptr);
        NtClose(event);
        g_event = nullptr;
        return status;
    }
    NtClose(thread);
    g_started = TRUE;
    return STATUS_SUCCESS;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlSubscribeWnfStateChangeNotification(PVOID *subscription, WNF_STATE_NAME state_name,
                                                                   WNF_CHANGE_STAMP change_stamp,
                                                                   PWNF_USER_CALLBACK callback, PVOID context,
                                                                   PCWNF_TYPE_ID type_id, ULONG serialization_group,
                                                                   ULONG unknown)
{
    if (!subscription || !callback)
        return STATUS_INVALID_PARAMETER;
    WNF_USER_SUBSCRIPTION *s = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s)
        return STATUS_NO_MEMORY;
    s->Name = state_name;
    s->Stamp = change_stamp;
    s->Callback = callback;
    s->Context = context;
    if (type_id) {
        s->TypeId = *type_id;
        s->HasType = TRUE;
    }
    RtlAcquireSRWLockExclusive(&g_lock);
    NTSTATUS status = start_delivery();
    if (NT_SUCCESS(status)) {
        status = NtSubscribeWnfStateChange(&state_name, change_stamp, SUBSCRIPTION_EVENT_MASK, nullptr);
        if (NT_SUCCESS(status)) {
            s->Next = g_subscriptions;
            g_subscriptions = s;
        }
    }
    RtlReleaseSRWLockExclusive(&g_lock);
    if (!NT_SUCCESS(status)) {
        free_subscription(s);
        return status;
    }
    *subscription = s;
    return STATUS_SUCCESS;
}

static BOOLEAN registered(WNF_USER_SUBSCRIPTION *s)
{
    for (WNF_USER_SUBSCRIPTION *p = g_subscriptions; p; p = p->Next)
        if (p == s)
            return TRUE;
    return FALSE;
}

/* a callback in flight finishes first, unless it is the calling thread. */
NTDLL_EXPORT NTSTATUS NTAPI RtlUnsubscribeWnfStateChangeNotification(PVOID subscription)
{
    WNF_USER_SUBSCRIPTION *s = subscription;
    RtlAcquireSRWLockExclusive(&g_lock);
    if (!s || !registered(s) || s->Removed) {
        RtlReleaseSRWLockExclusive(&g_lock);
        return STATUS_INVALID_PARAMETER;
    }
    s->Removed = TRUE;
    if (!s->Running) {
        unlink_subscription(s);
        RtlReleaseSRWLockExclusive(&g_lock);
        free_subscription(s);
        return STATUS_SUCCESS;
    }
    /* the delivery thread frees it when its call returns. */
    RtlReleaseSRWLockExclusive(&g_lock);
    return STATUS_SUCCESS;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlUnsubscribeWnfNotificationWaitForCompletion(PVOID subscription)
{
    WNF_USER_SUBSCRIPTION *s = subscription;
    ULONG self = (ULONG)(ULONG_PTR)NtCurrentTeb()->ClientId.UniqueThread;
    RtlAcquireSRWLockExclusive(&g_lock);
    if (!s || !registered(s) || s->Removed) {
        RtlReleaseSRWLockExclusive(&g_lock);
        return STATUS_INVALID_PARAMETER;
    }
    /* a call cannot wait for itself. */
    if (s->Running && s->RunningThread == self) {
        RtlReleaseSRWLockExclusive(&g_lock);
        return STATUS_INVALID_PARAMETER;
    }
    s->Removed = TRUE;
    while (s->Running) {
        RtlReleaseSRWLockExclusive(&g_lock);
        LARGE_INTEGER pause = {.QuadPart = -10000};
        NtDelayExecution(FALSE, &pause);
        RtlAcquireSRWLockExclusive(&g_lock);
    }
    unlink_subscription(s);
    RtlReleaseSRWLockExclusive(&g_lock);
    free_subscription(s);
    return STATUS_SUCCESS;
}

/* called with the lock held, returns a subscription whose stamp differs from the kernel's. */
static WNF_USER_SUBSCRIPTION *next_change(UCHAR *buffer, ULONG *size, WNF_CHANGE_STAMP *stamp)
{
    for (WNF_USER_SUBSCRIPTION *s = g_subscriptions; s; s = s->Next) {
        if (s->Removed)
            continue;
        ULONG capacity = WNF_MAX_STATE_SIZE;
        WNF_CHANGE_STAMP now = 0;
        NTSTATUS status = NtQueryWnfStateData(&s->Name, s->HasType ? &s->TypeId : nullptr, nullptr, &now, buffer,
                                              &capacity);
        if (!NT_SUCCESS(status) || now == s->Stamp)
            continue;
        s->Stamp = now;
        *size = capacity;
        *stamp = now;
        return s;
    }
    return nullptr;
}

static DWORD NTAPI deliver(PVOID parameter)
{
    UCHAR buffer[WNF_MAX_STATE_SIZE];
    ULONG self = (ULONG)(ULONG_PTR)NtCurrentTeb()->ClientId.UniqueThread;
    for (;;) {
        NtWaitForSingleObject(g_event, FALSE, nullptr);
        for (;;) {
            ULONG size = 0;
            WNF_CHANGE_STAMP stamp = 0;
            RtlAcquireSRWLockExclusive(&g_lock);
            WNF_USER_SUBSCRIPTION *s = next_change(buffer, &size, &stamp);
            if (s) {
                s->Running++;
                s->RunningThread = self;
            }
            RtlReleaseSRWLockExclusive(&g_lock);
            if (!s)
                break;
            s->Callback(s->Name, stamp, s->HasType ? &s->TypeId : nullptr, s->Context, buffer, size);
            RtlAcquireSRWLockExclusive(&g_lock);
            s->Running--;
            BOOLEAN gone = s->Removed && !s->Running && registered(s);
            if (gone)
                unlink_subscription(s);
            RtlReleaseSRWLockExclusive(&g_lock);
            if (gone)
                free_subscription(s);
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ packages */

NTDLL_EXPORT NTSTATUS NTAPI RtlQueryPackageIdentity(PVOID token, PWSTR package_full_name, PSIZE_T package_size,
                                                    PWSTR app_id, PSIZE_T app_id_size, PBOOLEAN packaged)
{
    if (packaged)
        *packaged = FALSE;
    return STATUS_NOT_FOUND;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlQueryPackageIdentityEx(PVOID token, PWSTR package_full_name, PSIZE_T package_size,
                                                      PWSTR app_id, PSIZE_T app_id_size, PULONG64 flags,
                                                      PBOOLEAN packaged)
{
    if (flags)
        *flags = 0;
    if (packaged)
        *packaged = FALSE;
    return STATUS_NOT_FOUND;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlQueryPackageClaims(PVOID token, PWSTR package_full_name, PSIZE_T package_size,
                                                  PWSTR app_id, PSIZE_T app_id_size, PGUID dynamic_id,
                                                  PULONG64 package_flags, PULONG64 attributes_present)
{
    if (dynamic_id)
        memset(dynamic_id, 0, sizeof(*dynamic_id));
    if (package_flags)
        *package_flags = 0;
    if (attributes_present)
        *attributes_present = 0;
    return STATUS_NOT_FOUND;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlQueryPackageClaimsEx(PVOID token, PWSTR package_full_name, PSIZE_T package_size,
                                                    PWSTR app_id, PSIZE_T app_id_size, PGUID dynamic_id,
                                                    PULONG64 package_flags, PULONG64 attributes_present, PVOID reserved1,
                                                    PVOID reserved2)
{
    return RtlQueryPackageClaims(token, package_full_name, package_size, app_id, app_id_size, dynamic_id,
                                 package_flags, attributes_present);
}
