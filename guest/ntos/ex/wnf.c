/*
 * the windows notification facility: named pieces of state with a change stamp that programs publish and
 * subscribe to. a name is 64 bits giving its lifetime, scope and a sequence number, stored xored with a constant.
 * well known names exist from the start, a name made with NtCreateWnfStateName lives until deleted, and a
 * temporary one until its creator exits. permanent and persistent names last while the system runs, not on disk.
 *
 * a subscribed process has the event it gave NtSetWnfProcessNotificationEvent set whenever a name's data changes,
 * and compares change stamps to find which names changed, so a notification arriving while it is busy is not
 * lost. NtGetCompleteWnfStateSubscription, which windows uses to learn the changes from the kernel, is not offered.
 */
#include "../ntos.h"

#define WNF_VERSION 1
#define WNF_NAME_KEY 0x41C64E6DA3BC0074ull
#define WNF_MAX_NAMES 1024
#define WNF_MAX_SUBSCRIPTIONS 256

typedef struct WNF_INSTANCE {
    struct WNF_INSTANCE *Next;
    ULONG64 Scope; /* the process for a process scoped name, otherwise 0 */
    WNF_CHANGE_STAMP Stamp;
    ULONG Size;
    PUCHAR Data;
    WNF_TYPE_ID TypeId;
} WNF_INSTANCE;

typedef struct WNF_ENTRY {
    struct WNF_ENTRY *Next;
    ULONG64 Name; /* xored, as the caller holds it */
    WNF_STATE_NAME_LIFETIME Lifetime;
    WNF_DATA_SCOPE Scope;
    ULONG MaximumSize;
    WNF_TYPE_ID TypeId;
    BOOLEAN HasType;
    HANDLE Creator; /* of a temporary name */
    WNF_INSTANCE *Instances;
} WNF_ENTRY;

typedef struct WNF_SUBSCRIPTION {
    ULONG64 Name;
    ULONG64 Id;
} WNF_SUBSCRIPTION;

typedef struct WNF_PROCESS {
    struct WNF_PROCESS *Next;
    HANDLE Process;
    PKEVENT Event;
    WNF_SUBSCRIPTION Subscriptions[WNF_MAX_SUBSCRIPTIONS];
    ULONG Count;
} WNF_PROCESS;

static WNF_ENTRY *g_entries;
static WNF_PROCESS *g_processes;
static ULONG g_entry_count;
static ULONG64 g_sequence = 1;
static ULONG64 g_subscription_id = 1;
static KGUARDED_MUTEX g_lock;

static void lock(void)
{
    KeAcquireGuardedMutex(&g_lock);
}

static void unlock(void)
{
    KeReleaseGuardedMutex(&g_lock);
}

static ULONG64 value_of(const WNF_STATE_NAME *name)
{
    return (ULONG64)name->Data[1] << 32 | name->Data[0];
}

/* xoring with the key converts between the stored and plain forms. */
static ULONG64 decoded(ULONG64 value)
{
    return value ^ WNF_NAME_KEY;
}

static ULONG64 encoded(ULONG64 raw)
{
    return raw ^ WNF_NAME_KEY;
}

static BOOLEAN name_valid(ULONG64 value)
{
    return (decoded(value) & 0xf) == WNF_VERSION;
}

static WNF_STATE_NAME_LIFETIME lifetime_of(ULONG64 value)
{
    return (WNF_STATE_NAME_LIFETIME)(decoded(value) >> 4 & 3);
}

static WNF_DATA_SCOPE scope_of(ULONG64 value)
{
    return (WNF_DATA_SCOPE)(decoded(value) >> 6 & 0xf);
}

static NTSTATUS capture(void *to, const void *from, SIZE_T size)
{
    if (KeGetPreviousMode() == UserMode)
        return KiCopyFromUser(to, from, size);
    memcpy(to, from, size);
    return STATUS_SUCCESS;
}

static NTSTATUS publish(void *to, const void *from, SIZE_T size)
{
    if (KeGetPreviousMode() == UserMode)
        return KiCopyToUser(to, from, size);
    memcpy(to, from, size);
    return STATUS_SUCCESS;
}

static NTSTATUS capture_name(PCWNF_STATE_NAME user, ULONG64 *value)
{
    WNF_STATE_NAME name;
    if (!user)
        return STATUS_INVALID_PARAMETER;
    NTSTATUS status = capture(&name, user, sizeof(name));
    if (!NT_SUCCESS(status))
        return status;
    *value = value_of(&name);
    return name_valid(*value) ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

/* well known names are made on first use; null for a name never made. */
static WNF_ENTRY *entry_of(ULONG64 value, BOOLEAN make)
{
    for (WNF_ENTRY *e = g_entries; e; e = e->Next)
        if (e->Name == value)
            return e;
    if (!make || lifetime_of(value) != WnfWellKnownStateName || g_entry_count >= WNF_MAX_NAMES)
        return nullptr;
    WNF_ENTRY *e = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*e), KI_TAG('W', 'n', 'f', 'N'));
    if (!e)
        return nullptr;
    e->Name = value;
    e->Lifetime = WnfWellKnownStateName;
    e->Scope = scope_of(value);
    e->MaximumSize = WNF_MAX_STATE_SIZE;
    e->Next = g_entries;
    g_entries = e;
    g_entry_count++;
    return e;
}

/*
 * with one session and one user, the machine, system and session scopes share one instance; a process scoped
 * name has one per process. an explicit scope names the process.
 */
static ULONG64 scope_key(const WNF_ENTRY *e, const VOID *explicit_scope, NTSTATUS *status)
{
    *status = STATUS_SUCCESS;
    if (e->Scope != WnfDataScopeProcess)
        return 0;
    if (!explicit_scope)
        return (ULONG64)(ULONG_PTR)PsCurrentProcess()->UniqueProcessId;
    ULONG_PTR pid = 0;
    *status = capture(&pid, explicit_scope, sizeof(pid));
    return (ULONG64)pid;
}

static WNF_INSTANCE *instance_of(const WNF_ENTRY *e, ULONG64 key)
{
    for (WNF_INSTANCE *i = e->Instances; i; i = i->Next)
        if (i->Scope == key)
            return i;
    return nullptr;
}

static void free_instances(WNF_ENTRY *e)
{
    while (e->Instances) {
        WNF_INSTANCE *i = e->Instances;
        e->Instances = i->Next;
        ExFreePool(i->Data);
        ExFreePool(i);
    }
}

static void free_entry(WNF_ENTRY *e)
{
    free_instances(e);
    ExFreePool(e);
}

static WNF_PROCESS *process_of(HANDLE pid, BOOLEAN make)
{
    for (WNF_PROCESS *p = g_processes; p; p = p->Next)
        if (p->Process == pid)
            return p;
    if (!make)
        return nullptr;
    WNF_PROCESS *p = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*p), KI_TAG('W', 'n', 'f', 'P'));
    if (!p)
        return nullptr;
    p->Process = pid;
    p->Next = g_processes;
    g_processes = p;
    return p;
}

static void notify(ULONG64 value)
{
    for (WNF_PROCESS *p = g_processes; p; p = p->Next) {
        if (!p->Event)
            continue;
        for (ULONG i = 0; i < p->Count; i++)
            if (p->Subscriptions[i].Name == value) {
                KeSetEvent(p->Event, IO_NO_INCREMENT, FALSE);
                break;
            }
    }
}

/* an exiting process takes its subscriptions and temporary names with it. */
static void NTAPI process_notify(HANDLE parent, HANDLE pid, BOOLEAN create)
{
    if (create)
        return;
    lock();
    WNF_PROCESS **at = &g_processes;
    while (*at && (*at)->Process != pid)
        at = &(*at)->Next;
    WNF_PROCESS *gone = *at;
    if (gone) {
        *at = gone->Next;
        if (gone->Event)
            ObDereferenceObject(gone->Event);
        ExFreePool(gone);
    }
    WNF_ENTRY **e = &g_entries;
    while (*e) {
        WNF_ENTRY *entry = *e;
        if (entry->Lifetime == WnfTemporaryStateName && entry->Creator == pid) {
            *e = entry->Next;
            free_entry(entry);
            g_entry_count--;
            continue;
        }
        /* its instances of process scoped names go too. */
        if (entry->Scope == WnfDataScopeProcess) {
            WNF_INSTANCE **i = &entry->Instances;
            while (*i) {
                WNF_INSTANCE *instance = *i;
                if (instance->Scope == (ULONG64)(ULONG_PTR)pid) {
                    *i = instance->Next;
                    ExFreePool(instance->Data);
                    ExFreePool(instance);
                    continue;
                }
                i = &instance->Next;
            }
        }
        e = &entry->Next;
    }
    unlock();
}

void ExpInitializeWnf(void)
{
    KeInitializeGuardedMutex(&g_lock);
    PsSetCreateProcessNotifyRoutine(process_notify, FALSE);
}

/* ------------------------------------------------------------------ names */

NTSTATUS NTAPI NtCreateWnfStateName(PWNF_STATE_NAME state_name, WNF_STATE_NAME_LIFETIME lifetime, WNF_DATA_SCOPE scope,
                                    BOOLEAN persistent, PCWNF_TYPE_ID type_id, ULONG maximum_size,
                                    PVOID descriptor)
{
    if (!state_name || lifetime < WnfPermanentStateName || lifetime > WnfTemporaryStateName ||
        scope > WnfDataScopePhysicalMachine || !maximum_size || maximum_size > WNF_MAX_STATE_SIZE)
        return STATUS_INVALID_PARAMETER;
    WNF_TYPE_ID type = {};
    if (type_id) {
        NTSTATUS status = capture(&type, type_id, sizeof(type));
        if (!NT_SUCCESS(status))
            return status;
    }
    WNF_ENTRY *e = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*e), KI_TAG('W', 'n', 'f', 'N'));
    if (!e)
        return STATUS_INSUFFICIENT_RESOURCES;
    lock();
    if (g_entry_count >= WNF_MAX_NAMES) {
        unlock();
        ExFreePool(e);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    ULONG64 raw = WNF_VERSION | (ULONG64)lifetime << 4 | (ULONG64)scope << 6 | (ULONG64)(persistent != 0) << 10 |
                  g_sequence++ << 11;
    e->Name = encoded(raw);
    e->Lifetime = lifetime;
    e->Scope = scope;
    e->MaximumSize = maximum_size;
    e->TypeId = type;
    e->HasType = type_id != nullptr;
    e->Creator = lifetime == WnfTemporaryStateName ? PsCurrentProcess()->UniqueProcessId : nullptr;
    e->Next = g_entries;
    g_entries = e;
    g_entry_count++;
    unlock();
    WNF_STATE_NAME out = {{(ULONG)e->Name, (ULONG)(e->Name >> 32)}};
    NTSTATUS status = publish(state_name, &out, sizeof(out));
    if (!NT_SUCCESS(status)) {
        lock();
        WNF_ENTRY **at = &g_entries;
        while (*at && *at != e)
            at = &(*at)->Next;
        if (*at) {
            *at = e->Next;
            free_entry(e);
            g_entry_count--;
        }
        unlock();
    }
    return status;
}

NTSTATUS NTAPI NtDeleteWnfStateName(PCWNF_STATE_NAME state_name)
{
    ULONG64 value;
    NTSTATUS status = capture_name(state_name, &value);
    if (!NT_SUCCESS(status))
        return status;
    if (lifetime_of(value) == WnfWellKnownStateName)
        return STATUS_ACCESS_DENIED;
    lock();
    WNF_ENTRY **at = &g_entries;
    while (*at && (*at)->Name != value)
        at = &(*at)->Next;
    WNF_ENTRY *e = *at;
    if (!e) {
        unlock();
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    *at = e->Next;
    notify(value);
    for (WNF_PROCESS *p = g_processes; p; p = p->Next)
        for (ULONG i = 0; i < p->Count;)
            if (p->Subscriptions[i].Name == value)
                p->Subscriptions[i] = p->Subscriptions[--p->Count];
            else
                i++;
    free_entry(e);
    g_entry_count--;
    unlock();
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtQueryWnfStateNameInformation(PCWNF_STATE_NAME state_name, WNF_STATE_NAME_INFORMATION information_class,
                                              const VOID *explicit_scope, PVOID information, ULONG information_size)
{
    ULONG64 value;
    NTSTATUS status = capture_name(state_name, &value);
    if (!NT_SUCCESS(status))
        return status;
    if ((ULONG)information_class > WnfInfoIsQuiescent || !information || information_size < sizeof(ULONG))
        return information_size < sizeof(ULONG) ? STATUS_BUFFER_TOO_SMALL : STATUS_INVALID_PARAMETER;
    ULONG answer = 0;
    lock();
    WNF_ENTRY *e = entry_of(value, TRUE);
    if (information_class == WnfInfoStateNameExist) {
        answer = e != nullptr;
    } else if (information_class == WnfInfoSubscribersPresent) {
        for (WNF_PROCESS *p = g_processes; p && !answer; p = p->Next)
            for (ULONG i = 0; i < p->Count; i++)
                if (p->Subscriptions[i].Name == value)
                    answer = 1;
    } else {
        answer = 1;
    }
    unlock();
    return publish(information, &answer, sizeof(answer));
}

/* ------------------------------------------------------------------ data */

NTSTATUS NTAPI NtUpdateWnfStateData(PCWNF_STATE_NAME state_name, const VOID *buffer, ULONG length,
                                    PCWNF_TYPE_ID type_id, const VOID *explicit_scope, WNF_CHANGE_STAMP matching_stamp,
                                    ULONG check_stamp)
{
    ULONG64 value;
    NTSTATUS status = capture_name(state_name, &value);
    if (!NT_SUCCESS(status))
        return status;
    if (length > WNF_MAX_STATE_SIZE || (length && !buffer))
        return STATUS_INVALID_BUFFER_SIZE;
    WNF_TYPE_ID type = {};
    if (type_id) {
        status = capture(&type, type_id, sizeof(type));
        if (!NT_SUCCESS(status))
            return status;
    }
    PUCHAR data = nullptr;
    if (length) {
        data = ExAllocatePool2(POOL_FLAG_NON_PAGED, length, KI_TAG('W', 'n', 'f', 'D'));
        if (!data)
            return STATUS_INSUFFICIENT_RESOURCES;
        status = capture(data, buffer, length);
        if (!NT_SUCCESS(status)) {
            ExFreePool(data);
            return status;
        }
    }
    lock();
    WNF_ENTRY *e = entry_of(value, TRUE);
    if (!e) {
        status = STATUS_OBJECT_NAME_NOT_FOUND;
    } else if (length > e->MaximumSize) {
        status = STATUS_INVALID_BUFFER_SIZE;
    } else if (type_id && e->HasType && memcmp(&type, &e->TypeId, sizeof(type))) {
        status = STATUS_INVALID_PARAMETER;
    } else {
        ULONG64 key = scope_key(e, explicit_scope, &status);
        if (NT_SUCCESS(status)) {
            WNF_INSTANCE *i = instance_of(e, key);
            if (check_stamp && matching_stamp != (i ? i->Stamp : 0)) {
                status = STATUS_UNSUCCESSFUL;
            } else {
                if (!i) {
                    i = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*i), KI_TAG('W', 'n', 'f', 'I'));
                    if (!i) {
                        status = STATUS_INSUFFICIENT_RESOURCES;
                    } else {
                        i->Scope = key;
                        i->Next = e->Instances;
                        e->Instances = i;
                    }
                }
                if (i) {
                    ExFreePool(i->Data);
                    i->Data = data;
                    data = nullptr;
                    i->Size = length;
                    if (type_id)
                        i->TypeId = type;
                    i->Stamp++;
                    notify(value);
                }
            }
        }
    }
    unlock();
    ExFreePool(data);
    return status;
}

NTSTATUS NTAPI NtDeleteWnfStateData(PCWNF_STATE_NAME state_name, const VOID *explicit_scope)
{
    ULONG64 value;
    NTSTATUS status = capture_name(state_name, &value);
    if (!NT_SUCCESS(status))
        return status;
    lock();
    WNF_ENTRY *e = entry_of(value, TRUE);
    if (!e) {
        unlock();
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    ULONG64 key = scope_key(e, explicit_scope, &status);
    if (NT_SUCCESS(status)) {
        WNF_INSTANCE **at = &e->Instances;
        while (*at && (*at)->Scope != key)
            at = &(*at)->Next;
        WNF_INSTANCE *i = *at;
        if (i) {
            *at = i->Next;
            ExFreePool(i->Data);
            ExFreePool(i);
        }
    }
    unlock();
    return status;
}

/* a name without data has stamp 0 and no bytes. STATUS_BUFFER_TOO_SMALL returns the size needed. */
NTSTATUS NTAPI NtQueryWnfStateData(PCWNF_STATE_NAME state_name, PCWNF_TYPE_ID type_id, const VOID *explicit_scope,
                                   PWNF_CHANGE_STAMP change_stamp, PVOID buffer, PULONG buffer_size)
{
    ULONG64 value;
    NTSTATUS status = capture_name(state_name, &value);
    if (!NT_SUCCESS(status))
        return status;
    if (!change_stamp || !buffer_size)
        return STATUS_INVALID_PARAMETER;
    ULONG capacity;
    status = capture(&capacity, buffer_size, sizeof(capacity));
    if (!NT_SUCCESS(status))
        return status;
    if (capacity && !buffer)
        return STATUS_INVALID_PARAMETER;
    WNF_TYPE_ID type = {};
    if (type_id) {
        status = capture(&type, type_id, sizeof(type));
        if (!NT_SUCCESS(status))
            return status;
    }
    PUCHAR copy = capacity ? ExAllocatePool2(POOL_FLAG_NON_PAGED, capacity, KI_TAG('W', 'n', 'f', 'D')) : nullptr;
    if (capacity && !copy)
        return STATUS_INSUFFICIENT_RESOURCES;
    WNF_CHANGE_STAMP stamp = 0;
    ULONG size = 0;
    lock();
    WNF_ENTRY *e = entry_of(value, TRUE);
    if (!e) {
        status = STATUS_OBJECT_NAME_NOT_FOUND;
    } else {
        ULONG64 key = scope_key(e, explicit_scope, &status);
        WNF_INSTANCE *i = NT_SUCCESS(status) ? instance_of(e, key) : nullptr;
        if (i) {
            stamp = i->Stamp;
            size = i->Size;
            if (size > capacity)
                status = STATUS_BUFFER_TOO_SMALL;
            else if (size)
                memcpy(copy, i->Data, size);
        }
    }
    unlock();
    if (NT_SUCCESS(status) || status == STATUS_BUFFER_TOO_SMALL) {
        NTSTATUS written = publish(change_stamp, &stamp, sizeof(stamp));
        if (NT_SUCCESS(written))
            written = publish(buffer_size, &size, sizeof(size));
        if (NT_SUCCESS(written) && NT_SUCCESS(status) && size)
            written = publish(buffer, copy, size);
        if (!NT_SUCCESS(written))
            status = written;
    }
    ExFreePool(copy);
    return status;
}

/* ------------------------------------------------------------------ subscriptions */

NTSTATUS NTAPI NtSetWnfProcessNotificationEvent(HANDLE event)
{
    PKEVENT object = nullptr;
    if (event) {
        NTSTATUS status = ObReferenceHandle(event, ExEventType, KeGetPreviousMode(), (PVOID *)&object, nullptr);
        if (!NT_SUCCESS(status))
            return status;
    }
    lock();
    WNF_PROCESS *p = process_of(PsCurrentProcess()->UniqueProcessId, TRUE);
    PKEVENT old = nullptr;
    if (p) {
        old = p->Event;
        p->Event = object;
    }
    unlock();
    if (!p) {
        if (object)
            ObDereferenceObject(object);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    if (old)
        ObDereferenceObject(old);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtSubscribeWnfStateChange(PCWNF_STATE_NAME state_name, WNF_CHANGE_STAMP change_stamp, ULONG event_mask,
                                         PULONG64 subscription_id)
{
    ULONG64 value;
    NTSTATUS status = capture_name(state_name, &value);
    if (!NT_SUCCESS(status))
        return status;
    ULONG64 id = 0;
    lock();
    WNF_ENTRY *e = entry_of(value, TRUE);
    if (!e) {
        unlock();
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    WNF_PROCESS *p = process_of(PsCurrentProcess()->UniqueProcessId, TRUE);
    if (!p) {
        unlock();
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    ULONG i;
    for (i = 0; i < p->Count; i++)
        if (p->Subscriptions[i].Name == value)
            break;
    if (i == p->Count) {
        if (p->Count == WNF_MAX_SUBSCRIPTIONS) {
            unlock();
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        p->Subscriptions[i].Name = value;
        p->Subscriptions[i].Id = g_subscription_id++;
        p->Count++;
    }
    id = p->Subscriptions[i].Id;
    /* data newer than the caller's stamp is a change it has not heard of. */
    WNF_INSTANCE *latest = instance_of(e, scope_key(e, nullptr, &status));
    if (latest && latest->Stamp > change_stamp && p->Event)
        KeSetEvent(p->Event, IO_NO_INCREMENT, FALSE);
    unlock();
    if (subscription_id)
        return publish(subscription_id, &id, sizeof(id));
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtUnsubscribeWnfStateChange(PCWNF_STATE_NAME state_name)
{
    ULONG64 value;
    NTSTATUS status = capture_name(state_name, &value);
    if (!NT_SUCCESS(status))
        return status;
    lock();
    WNF_PROCESS *p = process_of(PsCurrentProcess()->UniqueProcessId, FALSE);
    status = STATUS_OBJECT_NAME_NOT_FOUND;
    for (ULONG i = 0; p && i < p->Count; i++) {
        if (p->Subscriptions[i].Name == value) {
            p->Subscriptions[i] = p->Subscriptions[--p->Count];
            status = STATUS_SUCCESS;
            break;
        }
    }
    unlock();
    return status;
}

/* windows has a process ask the kernel for its changes; here the process compares stamps itself. */
NTSTATUS NTAPI NtGetCompleteWnfStateSubscription(PWNF_STATE_NAME old_state_name, PULONG64 old_subscription_id,
                                                 ULONG old_event_mask, ULONG old_status, PVOID new_delivery_descriptor,
                                                 ULONG descriptor_size)
{
    return STATUS_NOT_IMPLEMENTED;
}
