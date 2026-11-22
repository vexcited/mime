/*
 * plug and play, as much of it as user mode sees: a tree of device nodes
 * named by instance id, their properties in the registry's Enum keys where
 * windows keeps them, and their device interfaces as symbolic links in
 * \GLOBAL?? named the windows way. mime's drivers add and remove devices;
 * cfgmgr32 reads the tree through NtPlugPlayControl with mime's own
 * requests.
 */
#define MIME_KERNEL_PNP
#include "../cm/cm.h"
#include <mime/ntrtlsec.h>
#include <mime/pnp.h>
#include "../se/se.h"

#define INSTANCE_CHARS 200
#define LINK_CHARS 260
#define REFERENCE_CHARS 32
#define CLASS_CHARS 40

typedef struct DEVNODE {
    LIST_ENTRY Links;
    struct DEVNODE *Parent;
    WCHAR Instance[INSTANCE_CHARS];
    WCHAR Class[CLASS_CHARS]; /* the setup class, a guid in braces. */
    LIST_ENTRY Interfaces;
} DEVNODE;

typedef struct DEVINTERFACE {
    LIST_ENTRY Links;
    GUID Class;
    WCHAR Link[LINK_CHARS]; /* the name in \GLOBAL??. */
    WCHAR Reference[REFERENCE_CHARS]; /* the reference string, which ends the interface path. */
} DEVINTERFACE;

/*
 * tree changes bump a generation, and a program waits for a generation other than the one it saw. each generation
 * has its own event, signaled when the next one begins, so a change between a look and a wait is never lost.
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

/* lock held. wakes the programs waiting for a change. */
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
 * the two keys every device of the reference machine has under its own: its parameters, and its properties, which
 * only the system opens. a user is refused there, an administrator too.
 */
static void device_subkeys(PCWSTR instance)
{
    WCHAR path[INSTANCE_CHARS + 96] = L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Enum\\";
    wcscat(path, instance);
    SIZE_T end = wcslen(path);
    wcscat(path, L"\\Device Parameters");
    CmpOpenPath(path, TRUE);
    path[end] = 0;
    wcscat(path, L"\\Properties");
    CM_NODE *properties = CmpOpenPath(path, TRUE);
    if (!properties || properties->Sd)
        return;
    SD_ACE aces[] = {{CONTAINER_INHERIT_ACE, 0xF003F /* KEY_ALL_ACCESS */, SeSidSystem}};
    SD_BLOB *sd;
    if (NT_SUCCESS(SeSdBuild(SeSidSystem, SeSidSystem, SE_DACL_PROTECTED, aces, 1, &sd)))
        properties->Sd = sd;
}

/* adds a device under its parent, with the properties windows' installers would leave in its Enum key. */
NTSTATUS NTAPI MimePnpAddDevice(PCWSTR instance, PCWSTR parent, const MIME_DEVICE_PROPERTIES *props)
{
    if (wcslen(instance) >= INSTANCE_CHARS)
        return STATUS_NAME_TOO_LONG;
    WCHAR path[INSTANCE_CHARS + 64] = L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Enum\\";
    wcscat(path, instance);
    /*
     * the registry's own lock, as for any writer: a key made here while another thread sets a value would put two
     * records at one place of the journal, and the next boot would find it corrupt and start from empty. the
     * devices the registry describes at boot are added with the lock already held.
     */
    BOOLEAN locked = CmpLock.Owner != (PVOID)KeGetCurrentThread();
    if (locked)
        ExAcquireFastMutex(&CmpLock);
    CM_NODE *key = CmpOpenPath(path, TRUE);
    if (!key) {
        if (locked)
            CmpUnlock();
        return STATUS_INSUFFICIENT_RESOURCES;
    }
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
    device_subkeys(instance);
    if (locked)
        CmpUnlock();
    DEVNODE *d = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*d), KI_TAG('P', 'n', 'p', 'D'));
    if (!d)
        return STATUS_INSUFFICIENT_RESOURCES;
    memset(d, 0, sizeof(*d));
    memcpy(d->Instance, instance, (wcslen(instance) + 1) * sizeof(WCHAR));
    if (props->ClassGuid && wcslen(props->ClassGuid) < CLASS_CHARS)
        wcscpy(d->Class, props->ClassGuid);
    InitializeListHead(&d->Interfaces);
    lock();
    d->Parent = parent ? find(parent) : nullptr;
    InsertTailList(&g_devnodes, &d->Links);
    changed();
    unlock();
    return STATUS_SUCCESS;
}

/* shows the devices there are now to the visitor, which runs without the lock and may add interfaces and devices. */
void IopPnpEachDevice(void (*visit)(PCWSTR instance, PCWSTR klass))
{
    enum { ENTRY_CHARS = INSTANCE_CHARS + CLASS_CHARS };
    lock();
    ULONG count = 0;
    for (LIST_ENTRY *e = g_devnodes.Flink; e != &g_devnodes; e = e->Flink)
        count++;
    PWCHAR copy = count ? ExAllocatePool2(POOL_FLAG_NON_PAGED, (SIZE_T)count * ENTRY_CHARS * sizeof(WCHAR),
                                          KI_TAG('P', 'n', 'p', 'V'))
                        : nullptr;
    ULONG n = 0;
    for (LIST_ENTRY *e = g_devnodes.Flink; copy && e != &g_devnodes; e = e->Flink, n++) {
        DEVNODE *d = CONTAINING_RECORD(e, DEVNODE, Links);
        memcpy(copy + (SIZE_T)n * ENTRY_CHARS, d->Instance, sizeof(d->Instance));
        memcpy(copy + (SIZE_T)n * ENTRY_CHARS + INSTANCE_CHARS, d->Class, sizeof(d->Class));
    }
    unlock();
    for (ULONG i = 0; i < n; i++)
        visit(copy + (SIZE_T)i * ENTRY_CHARS, copy + (SIZE_T)i * ENTRY_CHARS + INSTANCE_CHARS);
    if (copy)
        ExFreePool(copy);
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

/* removes a device and its children; like windows, their Enum keys stay. */
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
 * windows names an interface by its instance id with # for \, then the class in braces, lowercase as device paths
 * show it. a reference string follows the path after a backslash: it is not part of the link, it is the name the
 * device is opened with.
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

/* appends a string to a multi-sz, counting what it needs past the end. */
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

/* \\?\, the link, then a backslash and the reference string. */
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

/* path has no \\?\ prefix. */
static BOOLEAN path_matches(const DEVINTERFACE *i, PCWSTR path)
{
    SIZE_T n = wcslen(i->Link);
    if (_wcsnicmp(path, i->Link, n))
        return FALSE;
    if (!i->Reference[0])
        return path[n] == 0;
    return path[n] == L'\\' && same(path + n + 1, i->Reference);
}

/*
 * the key of a phantom device, for an administrator. the key is the administrators' to change, since the calls that
 * set it up and remove it come from the caller's cfgmgr32 where windows' go through the manager.
 */
static NTSTATUS create_phantom(PCWSTR instance)
{
    if (!instance[0] || wcslen(instance) >= INSTANCE_CHARS)
        return STATUS_INVALID_PARAMETER;
    PTOKEN token = SepReferenceEffectiveToken(PsCurrentThread());
    BOOLEAN elevated = token && SepElevated(token);
    if (token)
        ObDereferenceObject(token);
    if (!elevated)
        return STATUS_ACCESS_DENIED;
    /* the registry's lock is taken outside the tree's, as where a driver adds a device. */
    lock();
    BOOLEAN live = find(instance) != nullptr;
    unlock();
    if (live)
        return STATUS_OBJECT_NAME_COLLISION;
    WCHAR path[INSTANCE_CHARS + 64] = L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Enum\\";
    wcscat(path, instance);
    NTSTATUS status = STATUS_SUCCESS;
    ExAcquireFastMutex(&CmpLock);
    if (CmpOpenPath(path, FALSE)) {
        status = STATUS_OBJECT_NAME_COLLISION;
    } else {
        CM_NODE *key = CmpOpenPath(path, TRUE);
        ULONG phantom = 1;
        if (!key) {
            status = STATUS_INSUFFICIENT_RESOURCES;
        } else {
            CmpSetValue(key, L"Phantom", 7, REG_DWORD, &phantom, sizeof(phantom));
            SD_ACE aces[] = {{CONTAINER_INHERIT_ACE, 0xF003F /* KEY_ALL_ACCESS */, SeSidSystem},
                             {CONTAINER_INHERIT_ACE, 0xF003F, SeSidAdministrators},
                             {CONTAINER_INHERIT_ACE, 0x20019 /* KEY_READ */, SeSidUsers}};
            SD_BLOB *sd;
            if (!key->Sd && NT_SUCCESS(SeSdBuild(SeSidSystem, SeSidSystem, SE_DACL_PROTECTED, aces, 3, &sd)))
                key->Sd = sd;
        }
    }
    CmpUnlock();
    return status;
}

static NTSTATUS answer(MIME_PNP_REQUEST *r, ULONG capacity, MIME_PNP_CLASS klass)
{
    ULONG at = 0;
    switch (klass) {
    case MIME_PNP_LIST_INTERFACES:
        /* the interface paths of a class, for one device or all. */
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
        /* the device and class of an interface path, with or without \\?\. */
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

/* waits for a generation other than the given one, sleeping without the lock. */
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

/*
 * the host's audio devices are in the tree as endpoints. a change of their default one or of a format leaves the
 * tree as it is, yet programs learn of it the same way: mmdevapi waits for a generation and lists them again.
 */
static KEVENT g_host_changed;

static void host_changes(PVOID context)
{
    (void)context;
    for (;;) {
        IopSyncAudioEndpoints();
        lock();
        changed();
        unlock();
        KeWaitForSingleObject(&g_host_changed, Executive, KernelMode, FALSE, nullptr);
    }
}

void IopStartPnpHostChanges(void)
{
    KeInitializeEvent(&g_host_changed, SynchronizationEvent, FALSE);
    PsCreateSystemThread(nullptr, 0, nullptr, nullptr, nullptr, host_changes, nullptr);
}

/* called from the session interrupt on MIME_IRQ_AUDIO. */
void IopPnpHostChanged(void)
{
    KeSetEvent(&g_host_changed, IO_NO_INCREMENT, FALSE);
}

/* answers a request that lies in kernel memory. back is how much of it goes to the caller. */
static NTSTATUS run_request(ULONG klass, MIME_PNP_REQUEST *r, ULONG length, ULONG *back)
{
    NTSTATUS status;
    r->Instance[RTL_NUMBER_OF(r->Instance) - 1] = 0;
    ULONG capacity = (length - (ULONG)sizeof(MIME_PNP_REQUEST)) / sizeof(WCHAR);
    if (klass == MIME_PNP_WAIT_CHANGE) {
        status = wait_change(r);
        r->OutChars = 0;
    } else if (klass == MIME_PNP_CREATE_PHANTOM) {
        status = create_phantom(r->Instance);
        r->OutChars = 0;
    } else {
        lock();
        status = answer(r, capacity, (MIME_PNP_CLASS)klass);
        unlock();
    }
    *back = status == STATUS_BUFFER_TOO_SMALL ? (ULONG)sizeof(MIME_PNP_REQUEST)
                                              : (ULONG)sizeof(MIME_PNP_REQUEST) + r->OutChars * sizeof(WCHAR);
    if (*back > length)
        *back = length;
    return status;
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
        ULONG back;
        status = run_request(klass, r, length, &back);
        if (NT_SUCCESS(status) || status == STATUS_BUFFER_TOO_SMALL) {
            NTSTATUS copied = KiCopyToUser(user_buffer, r, back);
            if (!NT_SUCCESS(copied))
                status = copied;
        }
    }
    ExFreePool(r);
    return status;
}

/* ------------------------------------------------------------------ \Device\DeviceApi */

/*
 * the device of the plug and play manager for user mode, which windows' cfgmgr32 opens as \Device\DeviceApi\CMApi.
 * the wow64 layer of windows answers NtPlugPlayControl with STATUS_NOT_IMPLEMENTED, so mime's cfgmgr32 sends its
 * requests here in a 32-bit process: a buffered control whose function is the class and whose buffer is the request.
 * a small buffer is told in the request itself, since a failed control gives no data back.
 */
static NTSTATUS NTAPI deviceapi_request(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    NTSTATUS status = STATUS_SUCCESS;
    ULONG_PTR information = 0;
    if (stack->MajorFunction == IRP_MJ_CREATE) {
        UNICODE_STRING api = RTL_CONSTANT_STRING(L"\\CMApi");
        if (!RtlEqualUnicodeString(&stack->FileObject->FileName, &api, TRUE))
            status = STATUS_OBJECT_NAME_NOT_FOUND;
    } else if (stack->MajorFunction == IRP_MJ_DEVICE_CONTROL) {
        ULONG code = stack->Parameters.DeviceIoControl.IoControlCode;
        ULONG in_len = stack->Parameters.DeviceIoControl.InputBufferLength;
        ULONG out_len = stack->Parameters.DeviceIoControl.OutputBufferLength;
        MIME_PNP_REQUEST *r = irp->AssociatedIrp.SystemBuffer;
        if ((code & 0xFFFFE003) != MIME_PNP_CONTROL(0) || in_len < sizeof(MIME_PNP_REQUEST) || in_len != out_len ||
            in_len > 0x40000) {
            status = STATUS_INVALID_PARAMETER;
        } else {
            ULONG back;
            status = run_request(MIME_PNP_CONTROL_CLASS(code), r, in_len, &back);
            if (status == STATUS_BUFFER_TOO_SMALL) {
                r->Flags = MIME_PNP_TOO_SMALL;
                status = STATUS_SUCCESS;
            }
            if (NT_SUCCESS(status))
                information = back;
        }
    }
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = information;
    IofCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

static NTSTATUS NTAPI deviceapi_entry(PDRIVER_OBJECT driver, PUNICODE_STRING registry)
{
    driver->MajorFunction[IRP_MJ_CREATE] = deviceapi_request;
    driver->MajorFunction[IRP_MJ_CLEANUP] = deviceapi_request;
    driver->MajorFunction[IRP_MJ_CLOSE] = deviceapi_request;
    driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = deviceapi_request;
    UNICODE_STRING name = RTL_CONSTANT_STRING(L"\\Device\\DeviceApi");
    PDEVICE_OBJECT device;
    return IoCreateDevice(driver, 0, &name, FILE_DEVICE_UNKNOWN, 0, FALSE, &device);
}

void IopInitializeDeviceApi(void)
{
    if (!NT_SUCCESS(IoCreateDriver(L"\\Driver\\PnpManager", deviceapi_entry, nullptr)))
        KiLogPrintf("the device api failed to start");
}
