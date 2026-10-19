/*
 * plug and play, as much as user mode sees of it: a tree of device
 * nodes named by their instance ids, their properties in the Enum keys of
 * the registry where windows keeps them, and their device interfaces as
 * symbolic links in \GLOBAL?? named as windows names them. drivers of
 * mime add and remove devices; cfgmgr32 reads the tree through
 * NtPlugPlayControl, whose requests are mime's own.
 */
#define MIME_KERNEL_PNP
#include "../cm/cm.h"
#include <mime/pnp.h>

#define INSTANCE_CHARS 200
#define LINK_CHARS 260
#define REFERENCE_CHARS 32

typedef struct DEVNODE {
    LIST_ENTRY Links;
    struct DEVNODE *Parent;
    WCHAR Instance[INSTANCE_CHARS];
    LIST_ENTRY Interfaces;
} DEVNODE;

typedef struct DEVINTERFACE {
    LIST_ENTRY Links;
    GUID Class;
    WCHAR Link[LINK_CHARS]; /* the name in \GLOBAL?? */
    WCHAR Reference[REFERENCE_CHARS]; /* the reference string, which the path of the interface ends with */
} DEVINTERFACE;

/*
 * what changes in the tree is announced with a generation: it goes up at every change, and a program waits for a
 * generation other than the one it saw. each generation has its own event, signaled when the next one begins, so a
 * change between a program's look and its wait is never lost.
 */
typedef struct PNP_EPOCH {
    KEVENT Event;
    LONG Refs;
} PNP_EPOCH;

static LIST_ENTRY g_devnodes = {&g_devnodes, &g_devnodes};
static KGUARDED_MUTEX g_lock;
static BOOLEAN g_ready;
static ULONG g_generation = 1;
static PNP_EPOCH *g_epoch;

static void lock(void)
{
    if (!g_ready) {
        KeInitializeGuardedMutex(&g_lock);
        g_ready = TRUE;
    }
    KeAcquireGuardedMutex(&g_lock);
}

static void unlock(void)
{
    KeReleaseGuardedMutex(&g_lock);
}

static void epoch_release(PNP_EPOCH *e)
{
    if (__atomic_sub_fetch(&e->Refs, 1, __ATOMIC_ACQ_REL) == 0)
        ExFreePool(e);
}

/* a change was made, with the lock held: the programs waiting for one wake. */
static void changed(void)
{
    g_generation++;
    PNP_EPOCH *old = g_epoch;
    g_epoch = nullptr;
    if (old) {
        KeSetEvent(&old->Event, IO_NO_INCREMENT, FALSE);
        epoch_release(old);
    }
}

static BOOLEAN same(PCWSTR a, PCWSTR b)
{
    return !_wcsicmp(a, b);
}

static DEVNODE *find(PCWSTR instance)
{
    for (LIST_ENTRY *e = g_devnodes.Flink; e != &g_devnodes; e = e->Flink) {
        DEVNODE *d = CONTAINING_RECORD(e, DEVNODE, Links);
        if (same(d->Instance, instance))
            return d;
    }
    return nullptr;
}

/* ------------------------------------------------------------------ the registry */

/* REG_SZ, or REG_MULTI_SZ when the value ends in two nulls. */
static void set_value(CM_NODE *key, PCWSTR name, PCWSTR value, BOOLEAN multi)
{
    if (!value)
        return;
    SIZE_T chars = 0;
    if (multi) {
        while (value[chars] || value[chars + 1])
            chars++;
        chars += 2;
    } else {
        chars = wcslen(value) + 1;
    }
    CmpSetValue(key, name, (ULONG)wcslen(name), multi ? REG_MULTI_SZ : REG_SZ, value, (ULONG)(chars * sizeof(WCHAR)));
}

/* ------------------------------------------------------------------ devices */

/*
 * a device present from now on, under its parent, with the properties
 * windows' installers would leave in its Enum key.
 */
NTSTATUS NTAPI MimePnpAddDevice(PCWSTR instance, PCWSTR parent, const MIME_DEVICE_PROPERTIES *props)
{
    if (wcslen(instance) >= INSTANCE_CHARS)
        return STATUS_NAME_TOO_LONG;
    WCHAR path[INSTANCE_CHARS + 64] = L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Enum\\";
    wcscat(path, instance);
    CM_NODE *key = CmpOpenPath(path, TRUE);
    if (!key)
        return STATUS_INSUFFICIENT_RESOURCES;
    set_value(key, L"DeviceDesc", props->DeviceDesc, FALSE);
    set_value(key, L"HardwareID", props->HardwareIds, TRUE);
    set_value(key, L"CompatibleIDs", props->CompatibleIds, TRUE);
    set_value(key, L"Class", props->Class, FALSE);
    set_value(key, L"ClassGUID", props->ClassGuid, FALSE);
    set_value(key, L"Mfg", props->Manufacturer, FALSE);
    set_value(key, L"Service", props->Service, FALSE);
    set_value(key, L"LocationInformation", props->Location, FALSE);
    set_value(key, L"ContainerID", props->ContainerId, FALSE);
    set_value(key, L"FriendlyName", props->FriendlyName, FALSE);
    DEVNODE *d = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*d), KI_TAG('P', 'n', 'p', 'D'));
    if (!d)
        return STATUS_INSUFFICIENT_RESOURCES;
    memset(d, 0, sizeof(*d));
    memcpy(d->Instance, instance, (wcslen(instance) + 1) * sizeof(WCHAR));
    InitializeListHead(&d->Interfaces);
    lock();
    d->Parent = parent ? find(parent) : nullptr;
    InsertTailList(&g_devnodes, &d->Links);
    changed();
    unlock();
    return STATUS_SUCCESS;
}

static void remove_interface(DEVINTERFACE *i)
{
    WCHAR path[LINK_CHARS + 16] = L"\\GLOBAL??\\";
    wcscat(path, i->Link);
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, path);
    IoDeleteSymbolicLink(&name);
    RemoveEntryList(&i->Links);
    ExFreePool(i);
}

/* a device gone, and its children with it; their Enum keys stay, as windows keeps them. */
void NTAPI MimePnpRemoveDevice(PCWSTR instance)
{
    lock();
    DEVNODE *d = find(instance);
    if (d) {
        for (LIST_ENTRY *e = g_devnodes.Flink; e != &g_devnodes;) {
            DEVNODE *c = CONTAINING_RECORD(e, DEVNODE, Links);
            e = e->Flink;
            if (c->Parent == d)
                c->Parent = nullptr;
        }
        while (!IsListEmpty(&d->Interfaces))
            remove_interface(CONTAINING_RECORD(d->Interfaces.Flink, DEVINTERFACE, Links));
        RemoveEntryList(&d->Links);
        ExFreePool(d);
        changed();
    }
    unlock();
}

/*
 * windows' name for an interface: the instance id with # for \, then
 * the class in braces, lowercase as device paths show it. a reference string follows the path after a
 * backslash and is not part of the link, it is the name the device is opened with.
 */
NTSTATUS NTAPI MimePnpAddInterfaceRef(PCWSTR instance, const GUID *klass, PCWSTR device, PCWSTR reference)
{
    if (reference && wcslen(reference) >= REFERENCE_CHARS)
        return STATUS_NAME_TOO_LONG;
    lock();
    DEVNODE *d = find(instance);
    if (!d) {
        unlock();
        return STATUS_NO_SUCH_DEVICE;
    }
    DEVINTERFACE *i = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*i), KI_TAG('P', 'n', 'p', 'I'));
    if (!i) {
        unlock();
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    memset(i, 0, sizeof(*i));
    i->Class = *klass;
    if (reference)
        wcscpy(i->Reference, reference);
    SIZE_T n = 0;
    for (PCWSTR p = instance; *p && n < LINK_CHARS - 40; p++)
        i->Link[n++] = *p == L'\\' ? L'#' : (WCHAR)towlower(*p);
    _snwprintf(i->Link + n, LINK_CHARS - n, L"#{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
                        klass->Data1, klass->Data2, klass->Data3, klass->Data4[0], klass->Data4[1], klass->Data4[2],
                        klass->Data4[3], klass->Data4[4], klass->Data4[5], klass->Data4[6], klass->Data4[7]);
    WCHAR path[LINK_CHARS + 16] = L"\\GLOBAL??\\";
    wcscat(path, i->Link);
    UNICODE_STRING link, target;
    RtlInitUnicodeString(&link, path);
    RtlInitUnicodeString(&target, device);
    NTSTATUS status = IoCreateSymbolicLink(&link, &target);
    if (NT_SUCCESS(status)) {
        InsertTailList(&d->Interfaces, &i->Links);
        changed();
    } else {
        ExFreePool(i);
    }
    unlock();
    return status;
}

NTSTATUS NTAPI MimePnpAddInterface(PCWSTR instance, const GUID *klass, PCWSTR device)
{
    return MimePnpAddInterfaceRef(instance, klass, device, nullptr);
}

/* ------------------------------------------------------------------ NtPlugPlayControl */

/* appends a string to a multi-sz being built, counting what it needs past the end. */
static void append(MIME_PNP_REQUEST *r, ULONG capacity, ULONG *at, PCWSTR prefix, PCWSTR s)
{
    SIZE_T p = prefix ? wcslen(prefix) : 0, n = wcslen(s);
    if (*at + p + n + 1 < capacity) {
        if (p)
            memcpy(r->Out + *at, prefix, p * sizeof(WCHAR));
        memcpy(r->Out + *at + p, s, (n + 1) * sizeof(WCHAR));
    }
    *at += (ULONG)(p + n + 1);
}

/* the path of an interface, \\?\ and its link, and the reference string after a backslash. */
static void append_path(MIME_PNP_REQUEST *r, ULONG capacity, ULONG *at, const DEVINTERFACE *i)
{
    SIZE_T prefix = 4, n = wcslen(i->Link), ref = i->Reference[0] ? wcslen(i->Reference) + 1 : 0;
    if (*at + prefix + n + ref + 1 < capacity) {
        memcpy(r->Out + *at, L"\\\\?\\", prefix * sizeof(WCHAR));
        memcpy(r->Out + *at + prefix, i->Link, n * sizeof(WCHAR));
        if (ref) {
            r->Out[*at + prefix + n] = L'\\';
            memcpy(r->Out + *at + prefix + n + 1, i->Reference, (ref - 1) * sizeof(WCHAR));
        }
        r->Out[*at + prefix + n + ref] = 0;
    }
    *at += (ULONG)(prefix + n + ref + 1);
}

/* whether a path, without its \\?\ prefix, names the interface. */
static BOOLEAN path_matches(const DEVINTERFACE *i, PCWSTR path)
{
    SIZE_T n = wcslen(i->Link);
    if (_wcsnicmp(path, i->Link, n))
        return FALSE;
    if (!i->Reference[0])
        return path[n] == 0;
    return path[n] == L'\\' && same(path + n + 1, i->Reference);
}

static NTSTATUS answer(MIME_PNP_REQUEST *r, ULONG capacity, MIME_PNP_CLASS klass)
{
    ULONG at = 0;
    switch (klass) {
    case MIME_PNP_LIST_INTERFACES:
        /* the paths of the interfaces of a class, of one device or of all. */
        for (LIST_ENTRY *e = g_devnodes.Flink; e != &g_devnodes; e = e->Flink) {
            DEVNODE *d = CONTAINING_RECORD(e, DEVNODE, Links);
            if (r->Instance[0] && !same(d->Instance, r->Instance))
                continue;
            for (LIST_ENTRY *f = d->Interfaces.Flink; f != &d->Interfaces; f = f->Flink) {
                DEVINTERFACE *i = CONTAINING_RECORD(f, DEVINTERFACE, Links);
                if (r->Flags || !memcmp(&i->Class, &r->Class, sizeof(GUID)))
                    append_path(r, capacity, &at, i);
            }
        }
        break;
    case MIME_PNP_LIST_DEVICES:
        for (LIST_ENTRY *e = g_devnodes.Flink; e != &g_devnodes; e = e->Flink)
            append(r, capacity, &at, nullptr, CONTAINING_RECORD(e, DEVNODE, Links)->Instance);
        break;
    case MIME_PNP_INTERFACE_DEVICE: {
        /* the device of an interface path, \\?\ or not, and its class. */
        PCWSTR path = r->Instance;
        if (!wcsncmp(path, L"\\\\?\\", 4) || !wcsncmp(path, L"\\??\\", 4))
            path += 4;
        for (LIST_ENTRY *e = g_devnodes.Flink; e != &g_devnodes; e = e->Flink) {
            DEVNODE *d = CONTAINING_RECORD(e, DEVNODE, Links);
            for (LIST_ENTRY *f = d->Interfaces.Flink; f != &d->Interfaces; f = f->Flink) {
                DEVINTERFACE *i = CONTAINING_RECORD(f, DEVINTERFACE, Links);
                if (path_matches(i, path)) {
                    r->Class = i->Class;
                    append(r, capacity, &at, nullptr, d->Instance);
                    goto done;
                }
            }
        }
        return STATUS_NO_SUCH_DEVICE;
    }
    case MIME_PNP_RELATED: {
        DEVNODE *d = find(r->Instance), *related = nullptr;
        if (!d)
            return STATUS_NO_SUCH_DEVICE;
        BOOLEAN past = FALSE;
        for (LIST_ENTRY *e = g_devnodes.Flink; e != &g_devnodes && !related; e = e->Flink) {
            DEVNODE *c = CONTAINING_RECORD(e, DEVNODE, Links);
            if (r->Flags == MIME_PNP_PARENT)
                related = d->Parent;
            else if (r->Flags == MIME_PNP_CHILD && c->Parent == d)
                related = c;
            else if (r->Flags == MIME_PNP_SIBLING) {
                if (c == d)
                    past = TRUE;
                else if (past && c->Parent == d->Parent)
                    related = c;
            }
        }
        if (!related)
            return STATUS_NO_SUCH_DEVICE;
        append(r, capacity, &at, nullptr, related->Instance);
        break;
    }
    case MIME_PNP_STATUS:
        if (!find(r->Instance))
            return STATUS_NO_SUCH_DEVICE;
        break;
    default:
        return STATUS_INVALID_PARAMETER;
    }
done:
    r->OutChars = at + (klass == MIME_PNP_LIST_INTERFACES || klass == MIME_PNP_LIST_DEVICES ? 1 : 0);
    if (r->OutChars > capacity)
        return STATUS_BUFFER_TOO_SMALL;
    if (klass == MIME_PNP_LIST_INTERFACES || klass == MIME_PNP_LIST_DEVICES)
        r->Out[at] = 0;
    return STATUS_SUCCESS;
}

/* waits for a generation other than the one asked about, with no lock held while it sleeps. */
static NTSTATUS wait_change(MIME_PNP_REQUEST *r)
{
    for (;;) {
        lock();
        if (g_generation != r->Flags) {
            r->Flags = g_generation;
            unlock();
            return STATUS_SUCCESS;
        }
        if (!g_epoch) {
            g_epoch = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*g_epoch), KI_TAG('P', 'n', 'p', 'E'));
            if (!g_epoch) {
                unlock();
                return STATUS_INSUFFICIENT_RESOURCES;
            }
            KeInitializeEvent(&g_epoch->Event, NotificationEvent, FALSE);
            g_epoch->Refs = 1;
        }
        PNP_EPOCH *e = g_epoch;
        __atomic_add_fetch(&e->Refs, 1, __ATOMIC_ACQ_REL);
        unlock();
        NTSTATUS status = KeWaitForSingleObject(&e->Event, UserRequest, UserMode, TRUE, nullptr);
        epoch_release(e);
        if (status != STATUS_WAIT_0)
            return status;
    }
}

NTSTATUS NTAPI NtPlugPlayControl(ULONG klass, PVOID user_buffer, ULONG length)
{
    if (length < sizeof(MIME_PNP_REQUEST) || length > 0x40000)
        return STATUS_INVALID_PARAMETER;
    MIME_PNP_REQUEST *r = ExAllocatePool2(POOL_FLAG_NON_PAGED, length, KI_TAG('P', 'n', 'p', 'R'));
    if (!r)
        return STATUS_INSUFFICIENT_RESOURCES;
    NTSTATUS status = KiCopyFromUser(r, user_buffer, length);
    if (NT_SUCCESS(status)) {
        r->Instance[RTL_NUMBER_OF(r->Instance) - 1] = 0;
        ULONG capacity = (length - (ULONG)sizeof(MIME_PNP_REQUEST)) / sizeof(WCHAR);
        if (klass == MIME_PNP_WAIT_CHANGE) {
            status = wait_change(r);
            r->OutChars = 0;
        } else {
            lock();
            status = answer(r, capacity, (MIME_PNP_CLASS)klass);
            unlock();
        }
        ULONG back = status == STATUS_BUFFER_TOO_SMALL ? (ULONG)sizeof(MIME_PNP_REQUEST)
                                                        : (ULONG)sizeof(MIME_PNP_REQUEST) + r->OutChars * sizeof(WCHAR);
        if (NT_SUCCESS(status) || status == STATUS_BUFFER_TOO_SMALL) {
            NTSTATUS copied = KiCopyToUser(user_buffer, r, back < length ? back : length);
            if (!NT_SUCCESS(copied))
                status = copied;
        }
    }
    ExFreePool(r);
    return status;
}
