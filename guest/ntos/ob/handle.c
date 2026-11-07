/* handle tables and handle based object services. */
#include "../ntos.h"
#include "../se/se.h"

#define KERNEL_HANDLE_BIT 0xFFFFFFFF80000000ull
#define OBJ_PROTECT_CLOSE 0x00000001 /* set by SetHandleInformation's HANDLE_FLAG_PROTECT_FROM_CLOSE. */
#define STATUS_HANDLE_NOT_CLOSABLE ((NTSTATUS)0xC0000235L)
#define ObjectHandleFlagInformation 4
#define HANDLE_TO_INDEX(h) ((((ULONG_PTR)(h)) & 0x7FFFFFFF) / 4 - 1)
#define INDEX_TO_HANDLE(i) ((HANDLE)(ULONG_PTR)(((i) + 1) * 4))

NTSTATUS ObInitializeHandleTable(PHANDLE_TABLE table)
{
    table->Capacity = 64;
    table->Count = 0;
    table->FirstFree = 0;
    table->Lock = 0;
    table->Entries = ExAllocatePool2(POOL_FLAG_NON_PAGED, table->Capacity * sizeof(HANDLE_TABLE_ENTRY),
                                     KI_TAG('O', 'b', 'H', 't'));
    return table->Entries ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

static PHANDLE_TABLE table_for(HANDLE handle, KPROCESSOR_MODE mode, PEPROCESS *owner)
{
    if (((ULONG_PTR)handle & KERNEL_HANDLE_BIT) == KERNEL_HANDLE_BIT && mode == KernelMode) {
        *owner = PsInitialSystemProcess;
        return &PsInitialSystemProcess->HandleTable;
    }
    *owner = PsCurrentProcess();
    return &(*owner)->HandleTable;
}

static NTSTATUS create_handle(PEPROCESS process, PVOID object, ACCESS_MASK access, ULONG attributes, PHANDLE handle);

/* the object callbacks drivers registered see each handle to a process or a thread, before and after. */
static NTSTATUS create_handle_seen(PEPROCESS process, PVOID object, ACCESS_MASK access, ULONG attributes, PHANDLE handle,
                                   BOOLEAN duplicate, PEPROCESS source)
{
    if (!ObpObjectCallbacksPresent(object))
        return create_handle(process, object, access, attributes, handle);
    BOOLEAN kernel_handle = process == PsInitialSystemProcess && (attributes & OBJ_KERNEL_HANDLE);
    access = ObpCallPreCallbacks(object, access, duplicate, kernel_handle, source, process);
    NTSTATUS status = create_handle(process, object, access, attributes, handle);
    ObpCallPostCallbacks(object, access, status, duplicate, kernel_handle);
    return status;
}

NTSTATUS ObCreateHandle(PEPROCESS process, PVOID object, ACCESS_MASK access, ULONG attributes, PHANDLE handle)
{
    return create_handle_seen(process, object, access, attributes, handle, FALSE, nullptr);
}

NTSTATUS ObpCreateDuplicateHandle(PEPROCESS source, PEPROCESS process, PVOID object, ACCESS_MASK access,
                                  ULONG attributes, PHANDLE handle)
{
    return create_handle_seen(process, object, access, attributes, handle, TRUE, source);
}

static NTSTATUS create_handle(PEPROCESS process, PVOID object, ACCESS_MASK access, ULONG attributes, PHANDLE handle)
{
    OBJECT_HEADER *header = OBJECT_TO_HEADER(object);
    if (header->Type->Init.ImplyAccess)
        access = header->Type->Init.ImplyAccess(access);
    PHANDLE_TABLE table = &process->HandleTable;
    PHANDLE_TABLE_ENTRY prepared = nullptr;
    ULONG prepared_capacity = 0;
    for (;;) {
        KIRQL irql = KeAcquireSpinLockRaiseToDpc(&table->Lock);
        if (!table->Entries || !table->Capacity) {
            KeReleaseSpinLock(&table->Lock, irql);
            if (prepared)
                ExFreePool(prepared);
            return STATUS_PROCESS_IS_TERMINATING;
        }
        ULONG index = table->FirstFree;
        while (index < table->Capacity && table->Entries[index].Object)
            index++;
        PHANDLE_TABLE_ENTRY old = nullptr;
        if (index >= table->Capacity) {
            if (prepared_capacity > table->Capacity) {
                memcpy(prepared, table->Entries, table->Capacity * sizeof(HANDLE_TABLE_ENTRY));
                old = table->Entries;
                index = table->Capacity;
                table->Entries = prepared;
                table->Capacity = prepared_capacity;
                prepared = nullptr;
            } else {
                ULONG capacity = table->Capacity;
                KeReleaseSpinLock(&table->Lock, irql);
                if (prepared)
                    ExFreePool(prepared);
                if (capacity >= (1u << 24))
                    return STATUS_INSUFFICIENT_RESOURCES;
                prepared_capacity = capacity * 2;
                prepared = ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                           prepared_capacity * sizeof(HANDLE_TABLE_ENTRY), KI_TAG('O', 'b', 'H', 't'));
                if (!prepared)
                    return STATUS_INSUFFICIENT_RESOURCES;
                continue;
            }
        }
        /* publish the handle counts before another thread can close the new entry. */
        __atomic_add_fetch(&header->HandleCount, 1, __ATOMIC_RELAXED);
        __atomic_add_fetch(&header->Type->TotalHandles, 1, __ATOMIC_RELAXED);
        table->Entries[index].Object = object;
        table->Entries[index].GrantedAccess = access;
        table->Entries[index].Attributes = attributes & OBJ_INHERIT;
        table->Count++;
        table->FirstFree = index + 1;
        KeReleaseSpinLock(&table->Lock, irql);
        if (old)
            ExFreePool(old);
        if (prepared)
            ExFreePool(prepared);
        HANDLE value = INDEX_TO_HANDLE(index);
        if (process == PsInitialSystemProcess && (attributes & OBJ_KERNEL_HANDLE))
            value = (HANDLE)((ULONG_PTR)value | KERNEL_HANDLE_BIT);
        *handle = value;
        return STATUS_SUCCESS;
    }
}

static BOOLEAN handle_listed(const HANDLE *list, ULONG count, ULONG_PTR index)
{
    ULONG low = 0, high = count;
    while (low < high) {
        ULONG mid = (low + high) / 2;
        ULONG_PTR at = HANDLE_TO_INDEX(list[mid]);
        if (at == index)
            return TRUE;
        if (at < index)
            low = mid + 1;
        else
            high = mid;
    }
    return FALSE;
}

/*
 * like Windows, the parent's inheritable handles land at the same values in the child's empty table. with a
 * list, only the listed handles, sorted, and each must be an inheritable handle of the parent.
 */
static NTSTATUS inherit_handles(PEPROCESS parent, PEPROCESS child, const HANDLE *list, ULONG list_count)
{
    PHANDLE_TABLE from = &parent->HandleTable, to = &child->HandleTable;
    KIRQL irql = KeAcquireSpinLockRaiseToDpc(&from->Lock);
    for (ULONG i = 0; i < list_count; i++) {
        ULONG_PTR index = HANDLE_TO_INDEX(list[i]);
        if (((ULONG_PTR)list[i] & 3) || index >= from->Capacity || !from->Entries[index].Object ||
            !(from->Entries[index].Attributes & OBJ_INHERIT)) {
            KeReleaseSpinLock(&from->Lock, irql);
            return STATUS_INVALID_PARAMETER;
        }
    }
    ULONG end = 0;
    for (ULONG i = 0; i < from->Capacity; i++)
        if (from->Entries[i].Object && (from->Entries[i].Attributes & OBJ_INHERIT) &&
            (!list || handle_listed(list, list_count, i)))
            end = i + 1;
    KeReleaseSpinLock(&from->Lock, irql);
    if (!end)
        return STATUS_SUCCESS;
    ULONG capacity = to->Capacity;
    while (capacity < end)
        capacity *= 2;
    PHANDLE_TABLE_ENTRY entries = ExAllocatePool2(POOL_FLAG_NON_PAGED, capacity * sizeof(HANDLE_TABLE_ENTRY),
                                                  KI_TAG('O', 'b', 'H', 't'));
    if (!entries)
        return STATUS_INSUFFICIENT_RESOURCES;
    memset(entries, 0, capacity * sizeof(HANDLE_TABLE_ENTRY));
    ULONG count = 0;
    irql = KeAcquireSpinLockRaiseToDpc(&from->Lock);
    for (ULONG i = 0; i < from->Capacity && i < capacity; i++) {
        HANDLE_TABLE_ENTRY *e = &from->Entries[i];
        if (!e->Object || !(e->Attributes & OBJ_INHERIT) || (list && !handle_listed(list, list_count, i)))
            continue;
        OBJECT_HEADER *h = OBJECT_TO_HEADER(e->Object);
        ObReferenceObject(e->Object);
        __atomic_add_fetch(&h->HandleCount, 1, __ATOMIC_RELAXED);
        __atomic_add_fetch(&h->Type->TotalHandles, 1, __ATOMIC_RELAXED);
        entries[i] = *e;
        count++;
    }
    KeReleaseSpinLock(&from->Lock, irql);
    irql = KeAcquireSpinLockRaiseToDpc(&to->Lock);
    PHANDLE_TABLE_ENTRY old = to->Entries;
    to->Entries = entries;
    to->Capacity = capacity;
    to->Count = count;
    to->FirstFree = 0;
    KeReleaseSpinLock(&to->Lock, irql);
    ExFreePool(old);
    return STATUS_SUCCESS;
}

NTSTATUS ObInheritHandles(PEPROCESS parent, PEPROCESS child)
{
    return inherit_handles(parent, child, nullptr, 0);
}

NTSTATUS ObInheritHandleList(PEPROCESS parent, PEPROCESS child, const HANDLE *list, ULONG count)
{
    return inherit_handles(parent, child, list, count);
}

/* consumes the caller's pointer reference to object. */
NTSTATUS ObInsertObject(PVOID object, ACCESS_MASK access, ULONG attributes, PHANDLE handle)
{
    /* ACCESS_SYSTEM_SECURITY needs the security privilege, as when opening a file or key. */
    if ((access & ACCESS_SYSTEM_SECURITY) && KeGetPreviousMode() == UserMode &&
        !SepSinglePrivilegeCheck(SE_SECURITY_PRIVILEGE, UserMode)) {
        ObDereferenceObject(object);
        return STATUS_PRIVILEGE_NOT_HELD;
    }
    /* generic rights become the type's own, and MAXIMUM_ALLOWED gets everything the type has. */
    const ULONG *mapping = OBJECT_TO_HEADER(object)->Type->Init.GenericMapping;
    if (mapping[3] && (access & (GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL | MAXIMUM_ALLOWED))) {
        ACCESS_MASK mapped = access & ~(GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL | MAXIMUM_ALLOWED);
        if (access & (GENERIC_ALL | MAXIMUM_ALLOWED))
            mapped |= mapping[3];
        if (access & GENERIC_READ)
            mapped |= mapping[0];
        if (access & GENERIC_WRITE)
            mapped |= mapping[1];
        if (access & GENERIC_EXECUTE)
            mapped |= mapping[2];
        access = mapped;
    }
    if (OBJECT_TO_HEADER(object)->Flags & OB_FLAG_SD_PENDING) {
        NTSTATUS status = (OBJECT_TO_HEADER(object)->Flags & OB_FLAG_BOOT) ? ObAssignBootSecurity(object)
                                                                            : ObAssignSecurity(object, nullptr, nullptr, KernelMode);
        if (!NT_SUCCESS(status)) {
            ObDereferenceObject(object);
            return status;
        }
    }
    PEPROCESS process = PsCurrentProcess();
    if ((attributes & OBJ_KERNEL_HANDLE) || (OBJECT_TO_HEADER(object)->Flags & OB_FLAG_KERNEL))
        process = PsInitialSystemProcess, attributes |= OBJ_KERNEL_HANDLE;
    NTSTATUS status = handle ? ObCreateHandle(process, object, access, attributes, handle) : STATUS_SUCCESS;
    if (!NT_SUCCESS(status) || !handle)
        ObDereferenceObject(object);
    return status;
}

/* handles OBJ_OPENIF and name collisions. */
NTSTATUS ObInsertObjectNamed(PVOID object, POBJECT_ATTRIBUTES attributes, ACCESS_MASK access, KPROCESSOR_MODE mode,
                             PHANDLE handle)
{
    ULONG attrs = attributes ? attributes->Attributes : 0;
    if (mode == UserMode)
        attrs &= ~OBJ_KERNEL_HANDLE;
    if (!attributes || !attributes->ObjectName || !attributes->ObjectName->Length) {
        NTSTATUS status = ObAssignSecurity(object, attributes, nullptr, mode);
        if (!NT_SUCCESS(status)) {
            ObDereferenceObject(object);
            return status;
        }
        return ObInsertObject(object, access, attrs, handle);
    }

    UNICODE_STRING name = *attributes->ObjectName;
    POBJECT_DIRECTORY parent = nullptr;
    UNICODE_STRING leaf = name;
    ULONG len = name.Length / sizeof(WCHAR);
    ULONG slash = len;
    while (slash > 0 && name.Buffer[slash - 1] != '\\')
        slash--;
    NTSTATUS status;
    if (attributes->RootDirectory) {
        status = ObReferenceHandle(attributes->RootDirectory, ObDirectoryType, mode, (PVOID *)&parent, nullptr);
        if (!NT_SUCCESS(status)) {
            ObDereferenceObject(object);
            return status;
        }
        if (slash == 1) {
            ObDereferenceObject(parent);
            ObDereferenceObject(object);
            return STATUS_OBJECT_NAME_INVALID;
        }
        if (slash) {
            /* a path below the root directory, through symbolic links such as Global. */
            UNICODE_STRING dir = {(USHORT)((slash - 1) * 2), (USHORT)((slash - 1) * 2), name.Buffer};
            ObDereferenceObject(parent);
            status = ObLookupObjectName(&dir, attributes->RootDirectory, 0, ObDirectoryType, mode, nullptr,
                                        (PVOID *)&parent);
            if (!NT_SUCCESS(status)) {
                ObDereferenceObject(object);
                return status == STATUS_OBJECT_NAME_NOT_FOUND ? STATUS_OBJECT_PATH_NOT_FOUND : status;
            }
            leaf.Buffer = name.Buffer + slash;
            leaf.Length = leaf.MaximumLength = (USHORT)((len - slash) * 2);
        }
    } else {
        if (slash == 0) {
            ObDereferenceObject(object);
            return STATUS_OBJECT_PATH_SYNTAX_BAD;
        }
        UNICODE_STRING dir = {(USHORT)((slash > 1 ? slash - 1 : 1) * 2), 0, name.Buffer};
        dir.MaximumLength = dir.Length;
        status = ObLookupObjectName(&dir, nullptr, 0, ObDirectoryType, mode, nullptr, (PVOID *)&parent);
        if (!NT_SUCCESS(status)) {
            ObDereferenceObject(object);
            return STATUS_OBJECT_PATH_NOT_FOUND;
        }
        leaf.Buffer = name.Buffer + slash;
        leaf.Length = leaf.MaximumLength = (USHORT)((len - slash) * 2);
    }

    /* a free name is created only where the caller may create objects, but a taken one is still found. */
    NTSTATUS may_create = ObCheckCreateAccess(parent, mode);
    status = ObAssignSecurity(object, attributes, parent, mode);
    if (NT_SUCCESS(status)) {
        if (NT_SUCCESS(may_create))
            status = ObInsertIntoDirectory(parent, object, &leaf);
        else
            status = ObDirectoryHasName(parent, &leaf) ? STATUS_OBJECT_NAME_COLLISION : may_create;
    }
    if (status == STATUS_OBJECT_NAME_COLLISION && (attrs & OBJ_OPENIF)) {
        PVOID existing;
        UNICODE_STRING full = name;
        NTSTATUS st = ObLookupObjectName(&full, attributes->RootDirectory, attrs, OBJECT_TO_HEADER(object)->Type,
                                         mode, nullptr, &existing);
        ObDereferenceObject(parent);
        ObDereferenceObject(object);
        if (!NT_SUCCESS(st))
            return st == STATUS_OBJECT_TYPE_MISMATCH ? st : STATUS_OBJECT_NAME_COLLISION;
        /* the existing object is opened like any other, checked against the access asked for. */
        ACCESS_MASK granted;
        st = ObCheckOpenAccess(existing, access, mode, &granted);
        if (!NT_SUCCESS(st)) {
            ObDereferenceObject(existing);
            return st;
        }
        st = ObInsertObject(existing, granted, attrs, handle);
        return NT_SUCCESS(st) ? STATUS_OBJECT_NAME_EXISTS : st;
    }
    ObDereferenceObject(parent);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(object);
        return status;
    }
    if (attrs & OBJ_PERMANENT) {
        OBJECT_TO_HEADER(object)->Flags |= OB_FLAG_PERMANENT;
        ObReferenceObject(object);
    }
    return ObInsertObject(object, access, attrs, handle);
}

/*
 * with strict handle checks, an invalid handle raises an exception as the system call returns. the null
 * handle is assumed exempt.
 */
static NTSTATUS invalid_handle(HANDLE handle, KPROCESSOR_MODE mode)
{
    if (mode == UserMode && handle && PsGetCurrentProcess()->MitigationPolicy[MIME_POLICY_STRICT_HANDLE_CHECK] & MIME_STRICT_HANDLE_RAISE)
        PsCurrentThread()->PendingException = STATUS_INVALID_HANDLE;
    return STATUS_INVALID_HANDLE;
}

NTSTATUS ObReferenceHandle(HANDLE handle, POBJECT_TYPE type, KPROCESSOR_MODE mode, PVOID *object,
                           ACCESS_MASK *granted)
{
    if (handle == NtCurrentProcess()) {
        PVOID p = PsCurrentProcess();
        if (type && type != PsProcessTypeObject)
            return STATUS_OBJECT_TYPE_MISMATCH;
        ObReferenceObject(p);
        *object = p;
        if (granted)
            *granted = PROCESS_ALL_ACCESS;
        return STATUS_SUCCESS;
    }
    if (handle == NtCurrentThread()) {
        PVOID t = PsCurrentThread();
        if (type && type != PsThreadTypeObject)
            return STATUS_OBJECT_TYPE_MISMATCH;
        ObReferenceObject(t);
        *object = t;
        if (granted)
            *granted = THREAD_ALL_ACCESS;
        return STATUS_SUCCESS;
    }
    PEPROCESS owner;
    PHANDLE_TABLE table = table_for(handle, mode, &owner);
    ULONG_PTR index = HANDLE_TO_INDEX(handle);
    KIRQL irql = KeAcquireSpinLockRaiseToDpc(&table->Lock);
    if (!handle || index >= table->Capacity || !table->Entries[index].Object) {
        KeReleaseSpinLock(&table->Lock, irql);
        return invalid_handle(handle, mode);
    }
    PVOID obj = table->Entries[index].Object;
    ACCESS_MASK access = table->Entries[index].GrantedAccess;
    if (type && OBJECT_TO_HEADER(obj)->Type != type) {
        KeReleaseSpinLock(&table->Lock, irql);
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    ObReferenceObject(obj);
    KeReleaseSpinLock(&table->Lock, irql);
    *object = obj;
    if (granted)
        *granted = access;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI ObReferenceObjectByHandle(HANDLE handle, ACCESS_MASK access, POBJECT_TYPE type, KPROCESSOR_MODE mode,
                                         PVOID *object, PVOID info)
{
    return ObReferenceHandle(handle, type, mode, object, nullptr);
}

static void close_object_handle(PEPROCESS process, PVOID object)
{
    OBJECT_HEADER *header = OBJECT_TO_HEADER(object);
    LONG_PTR remaining = __atomic_sub_fetch(&header->HandleCount, 1, __ATOMIC_ACQ_REL);
    __atomic_sub_fetch(&header->Type->TotalHandles, 1, __ATOMIC_RELAXED);
    if (header->Type->Init.CloseProcedure)
        header->Type->Init.CloseProcedure(process, object, (ULONG)remaining + 1);
    ObDereferenceObject(object);
}

static NTSTATUS close_in_table(PEPROCESS process, PHANDLE_TABLE table, ULONG_PTR index)
{
    KIRQL irql = KeAcquireSpinLockRaiseToDpc(&table->Lock);
    if (index >= table->Capacity || !table->Entries[index].Object) {
        KeReleaseSpinLock(&table->Lock, irql);
        return STATUS_INVALID_HANDLE;
    }
    PVOID obj = table->Entries[index].Object;
    table->Entries[index].Object = nullptr;
    table->Count--;
    if (index < table->FirstFree)
        table->FirstFree = (ULONG)index;
    KeReleaseSpinLock(&table->Lock, irql);

    close_object_handle(process, obj);
    return STATUS_SUCCESS;
}

NTSTATUS ObCloseHandle(HANDLE handle, KPROCESSOR_MODE mode)
{
    if (handle == NtCurrentProcess() || handle == NtCurrentThread())
        return STATUS_SUCCESS;
    PEPROCESS owner;
    PHANDLE_TABLE table = table_for(handle, mode, &owner);
    ULONG_PTR index = HANDLE_TO_INDEX(handle);
    /* a protected handle stays open, though process exit still closes it. */
    KIRQL irql = KeAcquireSpinLockRaiseToDpc(&table->Lock);
    BOOLEAN protected = index < table->Capacity && table->Entries[index].Object &&
                        (table->Entries[index].Attributes & OBJ_PROTECT_CLOSE);
    KeReleaseSpinLock(&table->Lock, irql);
    if (protected)
        return STATUS_HANDLE_NOT_CLOSABLE;
    NTSTATUS status = close_in_table(owner, table, index);
    return status == STATUS_INVALID_HANDLE ? invalid_handle(handle, mode) : status;
}

typedef struct OBJECT_HANDLE_FLAGS {
    BOOLEAN Inherit;
    BOOLEAN ProtectFromClose;
} OBJECT_HANDLE_FLAGS;

/* read and optionally replaced under the table's lock. */
static NTSTATUS handle_flags(HANDLE handle, OBJECT_HANDLE_FLAGS *flags, BOOLEAN set, ACCESS_MASK *granted)
{
    PEPROCESS owner;
    PHANDLE_TABLE table = table_for(handle, KeGetPreviousMode(), &owner);
    ULONG_PTR index = HANDLE_TO_INDEX(handle);
    KIRQL irql = KeAcquireSpinLockRaiseToDpc(&table->Lock);
    if (index >= table->Capacity || !table->Entries[index].Object) {
        KeReleaseSpinLock(&table->Lock, irql);
        return STATUS_INVALID_HANDLE;
    }
    HANDLE_TABLE_ENTRY *e = &table->Entries[index];
    if (set) {
        e->Attributes = (flags->Inherit ? OBJ_INHERIT : 0) | (flags->ProtectFromClose ? OBJ_PROTECT_CLOSE : 0);
    } else {
        flags->Inherit = (e->Attributes & OBJ_INHERIT) != 0;
        flags->ProtectFromClose = (e->Attributes & OBJ_PROTECT_CLOSE) != 0;
    }
    if (granted)
        *granted = e->GrantedAccess;
    KeReleaseSpinLock(&table->Lock, irql);
    return STATUS_SUCCESS;
}

/* only handle flags are stored. other classes are accepted and ignored. */
NTSTATUS NTAPI NtSetInformationObject(HANDLE handle, ULONG klass, PVOID info, ULONG length)
{
    if (klass != ObjectHandleFlagInformation)
        return STATUS_SUCCESS;
    OBJECT_HANDLE_FLAGS flags;
    if (length < sizeof(flags))
        return STATUS_INFO_LENGTH_MISMATCH;
    NTSTATUS status = KiCopyFromUser(&flags, info, sizeof(flags));
    return NT_SUCCESS(status) ? handle_flags(handle, &flags, TRUE, nullptr) : status;
}

NTSTATUS NTAPI NtClose(HANDLE handle)
{
    return ObCloseHandle(handle, KeGetPreviousMode());
}

NTSTATUS NTAPI ZwClose(HANDLE handle)
{
    return ObCloseHandle(handle, KernelMode);
}

void ObKillProcessHandles(PEPROCESS process)
{
    PHANDLE_TABLE table = &process->HandleTable;
    KIRQL irql = KeAcquireSpinLockRaiseToDpc(&table->Lock);
    PHANDLE_TABLE_ENTRY entries = table->Entries;
    ULONG capacity = table->Capacity;
    table->Entries = nullptr;
    table->Capacity = 0;
    table->Count = 0;
    table->FirstFree = 0;
    KeReleaseSpinLock(&table->Lock, irql);
    for (ULONG i = 0; i < capacity; i++)
        if (entries[i].Object)
            close_object_handle(process, entries[i].Object);
    if (entries)
        ExFreePool(entries);
}

NTSTATUS NTAPI ObOpenObjectByName(POBJECT_ATTRIBUTES attributes, POBJECT_TYPE type, KPROCESSOR_MODE mode,
                                  PVOID access_state, ACCESS_MASK access, PVOID parse_context, PHANDLE handle)
{
    if (!attributes || !attributes->ObjectName)
        return STATUS_OBJECT_NAME_INVALID;
    PVOID object;
    NTSTATUS status = ObLookupObjectName(attributes->ObjectName, attributes->RootDirectory, attributes->Attributes,
                                         type, mode, parse_context, &object);
    if (!NT_SUCCESS(status))
        return status;
    ULONG attrs = attributes->Attributes;
    if (mode == UserMode)
        attrs &= ~OBJ_KERNEL_HANDLE;
    ACCESS_MASK granted;
    status = ObCheckOpenAccess(object, access, mode, &granted);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(object);
        return status;
    }
    return ObInsertObject(object, granted, attrs, handle);
}

NTSTATUS NTAPI ObOpenObjectByPointer(PVOID object, ULONG attributes, PVOID access_state, ACCESS_MASK access,
                                     POBJECT_TYPE type, KPROCESSOR_MODE mode, PHANDLE handle)
{
    if (type && OBJECT_TO_HEADER(object)->Type != type)
        return STATUS_OBJECT_TYPE_MISMATCH;
    ObReferenceObject(object);
    if (mode == UserMode)
        attributes &= ~OBJ_KERNEL_HANDLE;
    return ObInsertObject(object, access, attributes, handle);
}

#define DUPLICATE_CLOSE_SOURCE 0x00000001
#define DUPLICATE_SAME_ACCESS 0x00000002

NTSTATUS ObDuplicateHandle(PEPROCESS source_process, HANDLE source, PEPROCESS target_process, PHANDLE target,
                           ACCESS_MASK access, ULONG attributes, ULONG options)
{
    PVOID object;
    ACCESS_MASK granted = access;
    NTSTATUS status;
    if (source == NtCurrentProcess() || source == NtCurrentThread()) {
        status = ObReferenceHandle(source, nullptr, KernelMode, &object, &granted);
    } else {
        PHANDLE_TABLE table = &source_process->HandleTable;
        ULONG_PTR index = HANDLE_TO_INDEX(source);
        KIRQL irql = KeAcquireSpinLockRaiseToDpc(&table->Lock);
        if (!source || index >= table->Capacity || !table->Entries[index].Object) {
            KeReleaseSpinLock(&table->Lock, irql);
            return STATUS_INVALID_HANDLE;
        }
        object = table->Entries[index].Object;
        granted = table->Entries[index].GrantedAccess;
        ObReferenceObject(object);
        KeReleaseSpinLock(&table->Lock, irql);
        status = STATUS_SUCCESS;
    }
    if (!NT_SUCCESS(status))
        return status;
    if (!(options & DUPLICATE_SAME_ACCESS)) {
        ACCESS_MASK wanted = access;
        if (wanted & MAXIMUM_ALLOWED) {
            wanted = (wanted & ~MAXIMUM_ALLOWED) | granted;
        } else {
            wanted = ObMapGenericAccess(OBJECT_TO_HEADER(object)->Type, wanted);
        }
        /* drop bits the type has no right for, like the sandbox's synchronization rights on a mutex. */
        ULONG valid = OBJECT_TO_HEADER(object)->Type->Init.ValidAccessMask;
        if (valid)
            wanted &= valid | 0x01000000 /* ACCESS_SYSTEM_SECURITY */;
        /*
         * the rights a handle has beyond its source are checked against the descriptor of the object, as an open
         * checks them. a section keeps to the rights of its source, so a read-only one cannot become writable.
         */
        if (KeGetPreviousMode() == UserMode && (wanted & ~granted)) {
            NTSTATUS check = STATUS_ACCESS_DENIED;
            SD_BLOB *sd = nullptr;
            const OBJECT_TYPE *type = OBJECT_TO_HEADER(object)->Type;
            /* the types that check their own descriptor, such as keys and files, have no check to repeat here. */
            if (type != MmSectionType && type->Init.GenericMapping[3] && !type->Init.GetSecurity)
                ObReferenceSecurity(object, &sd);
            if (sd) {
                ACCESS_MASK more;
                check = ObCheckOpenAccess(object, wanted & ~granted, UserMode, &more);
                SeSdDereference(sd);
            }
            if (!NT_SUCCESS(check)) {
                ObDereferenceObject(object);
                return STATUS_ACCESS_DENIED;
            }
        }
        granted = wanted;
    }
    HANDLE h = nullptr;
    if (target_process && target) {
        status = ObpCreateDuplicateHandle(source_process, target_process, object, granted, attributes, &h);
        if (!NT_SUCCESS(status)) {
            ObDereferenceObject(object);
            return status;
        }
        *target = h;
    } else {
        ObDereferenceObject(object);
    }
    if (options & DUPLICATE_CLOSE_SOURCE)
        close_in_table(source_process, &source_process->HandleTable, HANDLE_TO_INDEX(source));
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtDuplicateObject(HANDLE source_process_handle, HANDLE source, HANDLE target_process_handle,
                                 PHANDLE target, ACCESS_MASK access, ULONG attributes, ULONG options)
{
    PEPROCESS sp, tp = nullptr;
    NTSTATUS status = PsReferenceProcessHandle(source_process_handle, PROCESS_DUP_HANDLE, &sp);
    if (!NT_SUCCESS(status))
        return status;
    if (target_process_handle) {
        status = PsReferenceProcessHandle(target_process_handle, PROCESS_DUP_HANDLE, &tp);
        if (!NT_SUCCESS(status)) {
            ObDereferenceObject(sp);
            return status;
        }
    }
    HANDLE h = nullptr;
    status = ObDuplicateHandle(sp, source, tp, target ? &h : nullptr, access, attributes, options);
    if (NT_SUCCESS(status) && target) {
        if (KeGetPreviousMode() == UserMode)
            status = KiCopyToUser(target, &h, sizeof(h));
        else
            *target = h;
    }
    ObDereferenceObject(sp);
    if (tp)
        ObDereferenceObject(tp);
    return status;
}

/* -------------------------------------------------------- object apis */

static NTSTATUS capture_attributes(POBJECT_ATTRIBUTES user, POBJECT_ATTRIBUTES copy, PUNICODE_STRING name)
{
    name->Buffer = nullptr;
    if (!user) {
        memset(copy, 0, sizeof(*copy));
        return STATUS_SUCCESS;
    }
    if (KeGetPreviousMode() == UserMode) {
        if (!NT_SUCCESS(KiCopyFromUser(copy, user, sizeof(*copy))))
            return STATUS_ACCESS_VIOLATION;
    } else {
        *copy = *user;
    }
    if (copy->ObjectName) {
        NTSTATUS status = KiCaptureUnicodeString(name, copy->ObjectName);
        if (!NT_SUCCESS(status))
            return status;
        copy->ObjectName = name;
    }
    return STATUS_SUCCESS;
}

NTSTATUS ObCaptureObjectAttributes(POBJECT_ATTRIBUTES user, POBJECT_ATTRIBUTES copy, PUNICODE_STRING name)
{
    return capture_attributes(user, copy, name);
}

NTSTATUS ObStoreHandle(PHANDLE user_handle, HANDLE handle)
{
    if (KeGetPreviousMode() == UserMode) {
        NTSTATUS status = KiCopyToUser(user_handle, &handle, sizeof(handle));
        if (!NT_SUCCESS(status))
            NtClose(handle);
        return status;
    }
    *user_handle = handle;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtCreateDirectoryObject(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING name;
    NTSTATUS status = capture_attributes(attributes, &oa, &name);
    if (!NT_SUCCESS(status))
        return status;
    POBJECT_DIRECTORY dir;
    status = ObCreateObject(ObDirectoryType, &oa, KeGetPreviousMode(), sizeof(OBJECT_DIRECTORY), (PVOID *)&dir);
    if (NT_SUCCESS(status)) {
        memset(dir, 0, sizeof(*dir));
        HANDLE h;
        status = ObInsertObjectNamed(dir, &oa, access, KeGetPreviousMode(), &h);
        if (NT_SUCCESS(status))
            status = ObStoreHandle(handle, h) == STATUS_SUCCESS ? status : STATUS_ACCESS_VIOLATION;
    }
    KiFreeCapturedString(&name);
    return status;
}

NTSTATUS NTAPI NtOpenDirectoryObject(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING name;
    NTSTATUS status = capture_attributes(attributes, &oa, &name);
    if (!NT_SUCCESS(status))
        return status;
    HANDLE h;
    status = ObOpenObjectByName(&oa, ObDirectoryType, KeGetPreviousMode(), nullptr, access, nullptr, &h);
    if (NT_SUCCESS(status))
        status = ObStoreHandle(handle, h);
    KiFreeCapturedString(&name);
    return status;
}

NTSTATUS NTAPI NtOpenSymbolicLinkObject(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING name;
    NTSTATUS status = capture_attributes(attributes, &oa, &name);
    if (!NT_SUCCESS(status))
        return status;
    oa.Attributes |= OBJ_OPENLINK;
    HANDLE h;
    status = ObOpenObjectByName(&oa, ObSymbolicLinkType, KeGetPreviousMode(), nullptr, access, nullptr, &h);
    if (NT_SUCCESS(status))
        status = ObStoreHandle(handle, h);
    KiFreeCapturedString(&name);
    return status;
}

NTSTATUS NTAPI NtCreateSymbolicLinkObject(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes,
                                          PUNICODE_STRING target)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING name, captured;
    NTSTATUS status = capture_attributes(attributes, &oa, &name);
    if (!NT_SUCCESS(status))
        return status;
    status = KiCaptureUnicodeString(&captured, target);
    if (!NT_SUCCESS(status)) {
        KiFreeCapturedString(&name);
        return status;
    }
    POBJECT_SYMBOLIC_LINK link;
    status = ObCreateObject(ObSymbolicLinkType, &oa, KeGetPreviousMode(), sizeof(OBJECT_SYMBOLIC_LINK), (PVOID *)&link);
    if (NT_SUCCESS(status)) {
        /* the link owns its target. ObCreateObject left the body uninitialized. */
        link->Target.Buffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, captured.Length + sizeof(WCHAR), KI_TAG('O', 'b', 'S', 'l'));
        if (!link->Target.Buffer) {
            link->Target.Length = link->Target.MaximumLength = 0;
            ObDereferenceObject(link);
            status = STATUS_INSUFFICIENT_RESOURCES;
        } else {
            if (captured.Length)
                memcpy(link->Target.Buffer, captured.Buffer, captured.Length);
            link->Target.Buffer[captured.Length / sizeof(WCHAR)] = 0;
            link->Target.Length = captured.Length;
            link->Target.MaximumLength = (USHORT)(captured.Length + sizeof(WCHAR));
            HANDLE h;
            status = ObInsertObjectNamed(link, &oa, access, KeGetPreviousMode(), &h);
            if (NT_SUCCESS(status))
                status = ObStoreHandle(handle, h);
        }
    }
    KiFreeCapturedString(&captured);
    KiFreeCapturedString(&name);
    return status;
}

NTSTATUS NTAPI NtMakeTemporaryObject(HANDLE handle)
{
    PVOID object;
    ACCESS_MASK granted;
    NTSTATUS status = ObReferenceHandle(handle, nullptr, KeGetPreviousMode(), &object, &granted);
    if (!NT_SUCCESS(status))
        return status;
    if (KeGetPreviousMode() == UserMode && !(granted & DELETE)) {
        ObDereferenceObject(object);
        return STATUS_ACCESS_DENIED;
    }
    ObMakeTemporaryObject(object);
    ObDereferenceObject(object);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtQuerySymbolicLinkObject(HANDLE handle, PUNICODE_STRING target, PULONG returned)
{
    POBJECT_SYMBOLIC_LINK link;
    NTSTATUS status = ObReferenceHandle(handle, ObSymbolicLinkType, KeGetPreviousMode(), (PVOID *)&link, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    UNICODE_STRING out;
    status = KiCopyFromUser(&out, target, sizeof(out));
    if (NT_SUCCESS(status)) {
        ULONG need = link->Target.Length;
        if (returned)
            KiCopyToUser(returned, &need, sizeof(need));
        if (need > out.MaximumLength) {
            status = STATUS_BUFFER_TOO_SMALL;
        } else {
            status = KiCopyToUser(out.Buffer, link->Target.Buffer, need);
            out.Length = (USHORT)need;
            if (NT_SUCCESS(status))
                status = KiCopyToUser(target, &out, sizeof(out));
        }
    }
    ObDereferenceObject(link);
    return status;
}

NTSTATUS NTAPI NtQueryObject(HANDLE handle, OBJECT_INFORMATION_CLASS klass, PVOID info, ULONG length,
                             PULONG returned)
{
    /* the table of all types needs no handle. */
    if ((ULONG)klass == 3 /* ObjectTypesInformation */)
        return ObQueryAllTypesInformation(info, length, returned);
    PVOID object;
    NTSTATUS status = ObReferenceHandle(handle, nullptr, KeGetPreviousMode(), &object, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    if (klass == ObjectNameInformation) {
        UNICODE_STRING name = {};
        PWSTR buf = ExAllocatePool2(POOL_FLAG_NON_PAGED, 1024 * sizeof(WCHAR), KI_TAG('O', 'b', 'N', 'm'));
        if (!buf) {
            ObDereferenceObject(object);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        if (OBJECT_TO_HEADER(object)->Type == IoFileObjectTypeObject) {
            /* a file's name is its device's and its path on it: \Device\HarddiskVolume1\Users\... */
            PFILE_OBJECT file = object;
            UNICODE_STRING device = {};
            if (file->DeviceObject && OBJECT_TO_HEADER(file->DeviceObject)->Directory)
                ObpQueryNameString(file->DeviceObject, &device, buf, 512);
            ULONG chars = device.Length / sizeof(WCHAR);
            ULONG path = file->FileName.Length / sizeof(WCHAR);
            if (path > 1024 - 2 - chars)
                path = 1024 - 2 - chars;
            if (path && file->FileName.Buffer[0] != '\\' && chars)
                buf[chars++] = '\\';
            if (path) {
                memcpy(buf + chars, file->FileName.Buffer, path * sizeof(WCHAR));
                chars += path;
            }
            buf[chars] = 0;
            name.Buffer = buf;
            name.Length = (USHORT)(chars * sizeof(WCHAR));
        } else if (OBJECT_TO_HEADER(object)->Type == CmKeyType) {
            ULONG chars = CmQueryKeyObjectName(object, buf, 1023);
            if (chars > 1023)
                chars = 1023;
            buf[chars] = 0;
            name.Buffer = buf;
            name.Length = (USHORT)(chars * sizeof(WCHAR));
        } else if (OBJECT_TO_HEADER(object)->Directory) {
            ObpQueryNameString(object, &name, buf, 512);
        }
        ULONG need = sizeof(OBJECT_NAME_INFORMATION) + name.Length + sizeof(WCHAR);
        if (returned)
            KiCopyToUser(returned, &need, sizeof(need));
        if (length < need) {
            status = STATUS_INFO_LENGTH_MISMATCH;
        } else {
            OBJECT_NAME_INFORMATION oni;
            oni.Name.Length = name.Length;
            oni.Name.MaximumLength = (USHORT)(name.Length + sizeof(WCHAR));
            oni.Name.Buffer = (PWSTR)((PUCHAR)info + sizeof(oni));
            status = KiCopyToUser(info, &oni, sizeof(oni));
            if (NT_SUCCESS(status) && name.Length)
                status = KiCopyToUser(oni.Name.Buffer, name.Buffer, name.Length + sizeof(WCHAR));
        }
        ExFreePool(buf);
    } else if (klass == ObjectHandleFlagInformation || klass == ObjectBasicInformation) {
        OBJECT_HANDLE_FLAGS flags;
        ACCESS_MASK granted = 0;
        status = handle_flags(handle, &flags, FALSE, &granted);
        struct {
            ULONG Attributes;
            ACCESS_MASK GrantedAccess;
            ULONG HandleCount, PointerCount, PagedPoolCharge, NonPagedPoolCharge, Reserved[3];
            ULONG NameInfoSize, TypeInfoSize, SecurityDescriptorSize;
            LARGE_INTEGER CreationTime;
        } basic = {};
        basic.Attributes = (flags.Inherit ? OBJ_INHERIT : 0) | (flags.ProtectFromClose ? OBJ_PROTECT_CLOSE : 0);
        basic.GrantedAccess = granted;
        basic.HandleCount = (ULONG)OBJECT_TO_HEADER(object)->HandleCount;
        basic.PointerCount = (ULONG)OBJECT_TO_HEADER(object)->PointerCount;
        const void *out = klass == ObjectBasicInformation ? (const void *)&basic : (const void *)&flags;
        ULONG need = klass == ObjectBasicInformation ? sizeof(basic) : sizeof(flags);
        if (NT_SUCCESS(status) && returned)
            KiCopyToUser(returned, &need, sizeof(need));
        if (NT_SUCCESS(status))
            status = length < need ? STATUS_INFO_LENGTH_MISMATCH : KiCopyToUser(info, out, need);
    } else if (klass == ObjectTypeInformation) {
        POBJECT_TYPE type = OBJECT_TO_HEADER(object)->Type;
        struct {
            UNICODE_STRING TypeName;
            ULONG TotalNumberOfObjects, TotalNumberOfHandles, TotalPagedPoolUsage, TotalNonPagedPoolUsage;
            ULONG TotalNamePoolUsage, TotalHandleTableUsage, HighWaterNumberOfObjects, HighWaterNumberOfHandles;
            ULONG HighWaterPagedPoolUsage, HighWaterNonPagedPoolUsage, HighWaterNamePoolUsage;
            ULONG HighWaterHandleTableUsage, InvalidAttributes;
            ULONG GenericMapping[4];
            ULONG ValidAccessMask;
            BOOLEAN SecurityRequired, MaintainHandleCount;
            UCHAR TypeIndex;
            CHAR ReservedByte;
            ULONG PoolType, DefaultPagedPoolCharge, DefaultNonPagedPoolCharge;
        } t = {};
        ULONG need = sizeof(t) + type->Name.Length + sizeof(WCHAR);
        t.TypeName.Length = type->Name.Length;
        t.TypeName.MaximumLength = (USHORT)(type->Name.Length + sizeof(WCHAR));
        t.TypeName.Buffer = (PWSTR)((PUCHAR)info + sizeof(t));
        t.TotalNumberOfObjects = t.HighWaterNumberOfObjects = (ULONG)type->TotalObjects;
        t.TotalNumberOfHandles = t.HighWaterNumberOfHandles = (ULONG)type->TotalHandles;
        t.InvalidAttributes = 0xB0;
        t.ValidAccessMask = type->Init.ValidAccessMask;
        t.TypeIndex = (UCHAR)(type->Index + 2);
        t.PoolType = 0x200;
        if (returned)
            KiCopyToUser(returned, &need, sizeof(need));
        if (length < need) {
            status = STATUS_INFO_LENGTH_MISMATCH;
        } else {
            status = KiCopyToUser(info, &t, sizeof(t));
            if (NT_SUCCESS(status))
                status = KiCopyToUser(t.TypeName.Buffer, type->Name.Buffer, type->Name.Length + sizeof(WCHAR));
        }
    } else {
        status = STATUS_INVALID_INFO_CLASS;
    }
    ObDereferenceObject(object);
    return status;
}

/*
 * ExEnumHandleTable: each handle as windows' HANDLE_TABLE_ENTRY, its object header taken to be 0x30 bytes before the
 * body as windows has it, until the callback returns TRUE. access the callback takes away from the entry is taken
 * from the handle.
 */
typedef struct {
    ULONG64 LowValue;
    ULONG64 HighValue;
} WIN_HANDLE_TABLE_ENTRY;

typedef BOOLEAN(NTAPI *EX_ENUMERATE_HANDLE_ROUTINE)(PHANDLE_TABLE table, WIN_HANDLE_TABLE_ENTRY *entry, HANDLE handle,
                                                    PVOID context);

#define WIN_OBJECT_HEADER_SIZE 0x30
#define WIN_GRANTED_ACCESS_BITS 0x1FFFFFFull

BOOLEAN NTAPI ExEnumHandleTable(PHANDLE_TABLE table, EX_ENUMERATE_HANDLE_ROUTINE routine, PVOID context, PHANDLE found)
{
    if (!table || !routine)
        return FALSE;
    for (ULONG index = 0;; index++) {
        KIRQL irql = KeAcquireSpinLockRaiseToDpc(&table->Lock);
        if (!table->Entries || index >= table->Capacity) {
            KeReleaseSpinLock(&table->Lock, irql);
            return FALSE;
        }
        PVOID object = table->Entries[index].Object;
        ACCESS_MASK access = table->Entries[index].GrantedAccess;
        ULONG attributes = table->Entries[index].Attributes;
        if (object)
            ObReferenceObject(object);
        KeReleaseSpinLock(&table->Lock, irql);
        if (!object)
            continue;
        ULONG64 header = (ULONG64)object - WIN_OBJECT_HEADER_SIZE;
        WIN_HANDLE_TABLE_ENTRY entry;
        entry.LowValue = 1 | ((ULONG64)(attributes & 7) << 17) | ((header >> 4) << 20);
        entry.HighValue = access & WIN_GRANTED_ACCESS_BITS;
        HANDLE handle = INDEX_TO_HANDLE(index);
        BOOLEAN stop = routine(table, &entry, handle, context);
        ACCESS_MASK left = (ACCESS_MASK)(entry.HighValue & WIN_GRANTED_ACCESS_BITS);
        if (left != (access & WIN_GRANTED_ACCESS_BITS)) {
            irql = KeAcquireSpinLockRaiseToDpc(&table->Lock);
            if (table->Entries && index < table->Capacity && table->Entries[index].Object == object)
                table->Entries[index].GrantedAccess &= left | ~(ACCESS_MASK)WIN_GRANTED_ACCESS_BITS;
            KeReleaseSpinLock(&table->Lock, irql);
        }
        ObDereferenceObject(object);
        if (stop) {
            if (found)
                *found = handle;
            return TRUE;
        }
    }
}
