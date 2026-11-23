/*
 * the windows notification facility: named pieces of state with a change stamp that programs publish and
 * subscribe to. a name is 64 bits giving its lifetime, scope and a sequence number, stored xored with a constant.
 * well known names exist from the start, a name made with NtCreateWnfStateName lives until deleted, and a
 * temporary one until its creator exits. permanent and persistent names last while the system runs, not on disk.
 *
 * a subscribed process has the event it gave NtSetWnfProcessNotificationEvent set whenever a name it subscribed to
 * has something to tell, and asks NtGetCompleteWnfStateSubscription for its notifications one at a time. the states
 * of a subscription and the order of its steps are those of ExpWnfNotifyNameSubscribers,
 * ExpWnfDeliverThreadNotifications and ExpWnfCompleteThreadSubscriptions of ntoskrnl 26300. only the data event is
 * told: the events about the subscribers of a name and about its end are not.
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

#define WNF_EVENT_DATA 0x1
#define WNF_EVENT_ALL 0x1f
/* the least room the kernel wants for a notification: the descriptor and a state of the largest size. */
#define WNF_DELIVERY_SIZE 0x1030

/* idle, waiting in the queue of its process, told and not yet completed, completed with a failure. */
enum { WNF_SUB_IDLE, WNF_SUB_PENDING, WNF_SUB_DELIVERED, WNF_SUB_HELD };

typedef struct WNF_SUBSCRIPTION {
    ULONG64 Name;
    ULONG64 Id;
    ULONG Mask;
    ULONG State;
    ULONG Pending;
    ULONG Delivered;
    ULONG64 Queued; /* its place in the queue */
} WNF_SUBSCRIPTION;

typedef struct WNF_DELIVERY {
    ULONG64 SubscriptionId;
    WNF_STATE_NAME StateName;
    WNF_CHANGE_STAMP ChangeStamp;
    ULONG StateDataSize;
    ULONG EventMask;
    WNF_TYPE_ID TypeId;
    ULONG StateDataOffset;
} WNF_DELIVERY;

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
static ULONG64 g_queued = 1;
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

/* queues the events a subscription asked for, and wakes its process when the subscription was not waiting already. */
static void queue_events(WNF_PROCESS *p, WNF_SUBSCRIPTION *s, ULONG events)
{
    events &= s->Mask;
    if (!events)
        return;
    BOOLEAN wake = FALSE;
    if (s->State == WNF_SUB_IDLE) {
        s->Queued = g_queued++;
        s->State = WNF_SUB_PENDING;
        wake = TRUE;
    } else if (s->State == WNF_SUB_HELD) {
        s->State = WNF_SUB_PENDING;
        wake = TRUE;
    }
    s->Pending = s->Mask & (s->Pending | events);
    if (wake && p->Event)
        KeSetEvent(p->Event, IO_NO_INCREMENT, FALSE);
}

/* the instance of a process scoped name is told to its process alone. */
static void notify(const WNF_ENTRY *e, ULONG64 key)
{
    for (WNF_PROCESS *p = g_processes; p; p = p->Next) {
        if (e->Scope == WnfDataScopeProcess && (ULONG64)(ULONG_PTR)p->Process != key)
            continue;
        for (ULONG i = 0; i < p->Count; i++)
            if (p->Subscriptions[i].Name == e->Name) {
                queue_events(p, &p->Subscriptions[i], WNF_EVENT_DATA);
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

/*
 * the states an installed machine holds from its first run on, read on windows 11 26300: the first run experience is
 * over and its user has logged on, each a dword of one. OOBEComplete of kernel32 answers from them.
 */
static void publish_installed_state(void)
{
    static const ULONG64 names[] = {
        0x0D83063EA3BC2475ull, /* WNF_SHEL_OOBE_USER_LOGON_COMPLETE */
        0x41960B29A3BC0C75ull, /* WNF_DEP_OOBE_COMPLETE */
    };
    /* at boot, before any thread could ask: no lock is taken. */
    for (ULONG i = 0; i < RTL_NUMBER_OF(names); i++) {
        WNF_ENTRY *e = entry_of(names[i], TRUE);
        WNF_INSTANCE *instance = e ? ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*instance), KI_TAG('W', 'n', 'f', 'I')) : nullptr;
        PUCHAR data = instance ? ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(ULONG), KI_TAG('W', 'n', 'f', 'D')) : nullptr;
        if (!data) {
            if (instance)
                ExFreePool(instance);
            continue;
        }
        memset(instance, 0, sizeof(*instance));
        *(PULONG)data = 1;
        instance->Stamp = 1;
        instance->Size = sizeof(ULONG);
        instance->Data = data;
        instance->Next = e->Instances;
        e->Instances = instance;
    }
}

void ExpInitializeWnf(void)
{
    KeInitializeGuardedMutex(&g_lock);
    PsSetCreateProcessNotifyRoutine(process_notify, FALSE);
    publish_installed_state();
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
                    notify(e, key);
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
    if (event_mask & ~WNF_EVENT_ALL)
        return STATUS_INVALID_PARAMETER;
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
    WNF_SUBSCRIPTION *s = &p->Subscriptions[i];
    if (i == p->Count) {
        if (p->Count == WNF_MAX_SUBSCRIPTIONS) {
            unlock();
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        memset(s, 0, sizeof(*s));
        s->Name = value;
        s->Id = g_subscription_id++;
        s->Mask = event_mask;
        p->Count++;
    } else {
        /* a second subscription to a name changes the events of the first, and what waits follows. */
        s->Mask = event_mask;
        s->Pending &= event_mask;
        s->Delivered &= event_mask;
        if (s->State != WNF_SUB_IDLE && !s->Pending && !s->Delivered)
            s->State = WNF_SUB_IDLE;
    }
    id = s->Id;
    /* data with another stamp than the caller's is a change it has not heard of. */
    WNF_INSTANCE *latest = instance_of(e, scope_key(e, nullptr, &status));
    if (latest && latest->Stamp != change_stamp)
        queue_events(p, s, WNF_EVENT_DATA);
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

/* the notification a process answered: one with more to tell waits again, a failed one is held until the next event. */
static void complete(WNF_PROCESS *p, ULONG64 id, ULONG result)
{
    for (ULONG i = 0; i < p->Count; i++) {
        WNF_SUBSCRIPTION *s = &p->Subscriptions[i];
        if (s->Id != id)
            continue;
        if (s->State != WNF_SUB_DELIVERED && s->State != WNF_SUB_HELD)
            return;
        if (s->Pending) {
            s->State = WNF_SUB_PENDING;
            if (result == (ULONG)STATUS_RETRY)
                s->Pending |= WNF_EVENT_DATA & s->Mask;
        } else if (result) {
            s->State = WNF_SUB_HELD;
        } else {
            s->State = WNF_SUB_IDLE;
        }
        if (s->State != WNF_SUB_HELD)
            s->Delivered = 0;
        return;
    }
}

/* the oldest waiting subscription with something to tell, written with the data of its name behind the descriptor. */
static NTSTATUS deliver(WNF_PROCESS *p, WNF_DELIVERY *descriptor, ULONG capacity)
{
    for (;;) {
        WNF_SUBSCRIPTION *s = nullptr;
        for (ULONG i = 0; i < p->Count; i++)
            if (p->Subscriptions[i].State == WNF_SUB_PENDING && (!s || p->Subscriptions[i].Queued < s->Queued))
                s = &p->Subscriptions[i];
        if (!s)
            return STATUS_NO_MORE_ENTRIES;
        ULONG events = s->Pending & s->Mask;
        WNF_ENTRY *e = entry_of(s->Name, FALSE);
        NTSTATUS status = STATUS_SUCCESS;
        WNF_INSTANCE *instance = e ? instance_of(e, scope_key(e, nullptr, &status)) : nullptr;
        /* a name nobody published has no data event. */
        if (!instance || !instance->Stamp || instance->Size > capacity - sizeof(*descriptor))
            events &= ~WNF_EVENT_DATA;
        if (!events) {
            s->State = WNF_SUB_IDLE;
            s->Pending = s->Delivered = 0;
            continue;
        }
        memset(descriptor, 0, sizeof(*descriptor));
        descriptor->SubscriptionId = s->Id;
        descriptor->StateName.Data[0] = (ULONG)s->Name;
        descriptor->StateName.Data[1] = (ULONG)(s->Name >> 32);
        if (events & WNF_EVENT_DATA) {
            descriptor->TypeId = e->HasType ? e->TypeId : instance->TypeId;
            descriptor->ChangeStamp = instance->Stamp;
            descriptor->StateDataSize = instance->Size;
            if (instance->Size)
                memcpy(descriptor + 1, instance->Data, instance->Size);
        }
        descriptor->EventMask = events;
        descriptor->StateDataOffset = sizeof(*descriptor);
        s->Delivered = events;
        s->Pending = 0;
        s->State = WNF_SUB_DELIVERED;
        return STATUS_SUCCESS;
    }
}

/*
 * completes the notification the caller got before, when it names one, and writes the next. a completion counts with
 * events and a result of success or STATUS_RETRY, and anything else is an invalid parameter, as is a process that
 * never subscribed. no room asks for no notification.
 */
NTSTATUS NTAPI NtGetCompleteWnfStateSubscription(PWNF_STATE_NAME old_state_name, PULONG64 old_subscription_id,
                                                 ULONG old_event_mask, ULONG old_status, PVOID new_delivery_descriptor,
                                                 ULONG descriptor_size)
{
    ULONG64 id = 0;
    BOOLEAN completes = old_state_name && old_subscription_id;
    if (completes) {
        if (!old_event_mask || (old_status && old_status != (ULONG)STATUS_RETRY))
            return STATUS_INVALID_PARAMETER;
        ULONG64 value;
        NTSTATUS status = capture_name(old_state_name, &value);
        if (!NT_SUCCESS(status))
            return STATUS_INVALID_PARAMETER;
        status = capture(&id, old_subscription_id, sizeof(id));
        if (!NT_SUCCESS(status))
            return status;
    }
    if (descriptor_size && descriptor_size < WNF_DELIVERY_SIZE) {
        lock();
        WNF_PROCESS *p = process_of(PsCurrentProcess()->UniqueProcessId, FALSE);
        if (p && completes)
            complete(p, id, old_status);
        unlock();
        return p ? STATUS_BUFFER_TOO_SMALL : STATUS_INVALID_PARAMETER;
    }
    WNF_DELIVERY *descriptor = nullptr;
    if (descriptor_size) {
        descriptor = ExAllocatePool2(POOL_FLAG_NON_PAGED, WNF_DELIVERY_SIZE, KI_TAG('W', 'n', 'f', 'D'));
        if (!descriptor)
            return STATUS_INSUFFICIENT_RESOURCES;
    }
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    lock();
    WNF_PROCESS *p = process_of(PsCurrentProcess()->UniqueProcessId, FALSE);
    if (p) {
        if (completes)
            complete(p, id, old_status);
        if (descriptor)
            status = deliver(p, descriptor, WNF_DELIVERY_SIZE);
        else if (completes)
            status = STATUS_SUCCESS;
    }
    unlock();
    if (descriptor && status == STATUS_SUCCESS)
        status = publish(new_delivery_descriptor, descriptor, sizeof(*descriptor) + descriptor->StateDataSize);
    ExFreePool(descriptor);
    return status;
}
