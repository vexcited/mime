/*
 * handle tables and handle based object services.
 */
#include "../ntos.h"

#define KERNEL_HANDLE_BIT 0xFFFFFFFF80000000ull
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

NTSTATUS ObCreateHandle(PEPROCESS process, PVOID object, ACCESS_MASK access, ULONG attributes, PHANDLE handle)
{
    PHANDLE_TABLE table = &process->HandleTable;
    KIRQL irql = KeAcquireSpinLockRaiseToDpc(&table->Lock);
    ULONG index = table->FirstFree;
    while (index < table->Capacity && table->Entries[index].Object)
        index++;
    if (index >= table->Capacity) {
        ULONG new_capacity = table->Capacity * 2;
        if (new_capacity > (1u << 24)) {
            KeReleaseSpinLock(&table->Lock, irql);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        KeReleaseSpinLock(&table->Lock, irql);
        PHANDLE_TABLE_ENTRY entries = ExAllocatePool2(POOL_FLAG_NON_PAGED, new_capacity * sizeof(HANDLE_TABLE_ENTRY),
                                                      KI_TAG('O', 'b', 'H', 't'));
        if (!entries)
            return STATUS_INSUFFICIENT_RESOURCES;
        irql = KeAcquireSpinLockRaiseToDpc(&table->Lock);
        if (table->Capacity < new_capacity) {
            memcpy(entries, table->Entries, table->Capacity * sizeof(HANDLE_TABLE_ENTRY));
            PHANDLE_TABLE_ENTRY old = table->Entries;
            index = table->Capacity;
            table->Entries = entries;
            table->Capacity = new_capacity;
            KeReleaseSpinLock(&table->Lock, irql);
            ExFreePool(old);
            irql = KeAcquireSpinLockRaiseToDpc(&table->Lock);
        } else {
            KeReleaseSpinLock(&table->Lock, irql);
            ExFreePool(entries);
            return ObCreateHandle(process, object, access, attributes, handle);
        }
    }
    table->Entries[index].Object = object;
    table->Entries[index].GrantedAccess = access;
    table->Entries[index].Attributes = attributes & OBJ_INHERIT;
    table->Count++;
    table->FirstFree = index + 1;
    KeReleaseSpinLock(&table->Lock, irql);

    OBJECT_HEADER *h = OBJECT_TO_HEADER(object);
    __atomic_add_fetch(&h->HandleCount, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&h->Type->TotalHandles, 1, __ATOMIC_RELAXED);
    HANDLE value = INDEX_TO_HANDLE(index);
    if (process == PsInitialSystemProcess && (attributes & OBJ_KERNEL_HANDLE))
        value = (HANDLE)((ULONG_PTR)value | KERNEL_HANDLE_BIT);
    *handle = value;
    return STATUS_SUCCESS;
}

/* consumes the caller's pointer reference to object. */
NTSTATUS ObInsertObject(PVOID object, ACCESS_MASK access, ULONG attributes, PHANDLE handle)
{
    PEPROCESS process = PsCurrentProcess();
    if ((attributes & OBJ_KERNEL_HANDLE) || (OBJECT_TO_HEADER(object)->Flags & OB_FLAG_KERNEL))
        process = PsInitialSystemProcess, attributes |= OBJ_KERNEL_HANDLE;
    NTSTATUS status = handle ? ObCreateHandle(process, object, access, attributes, handle) : STATUS_SUCCESS;
    if (!NT_SUCCESS(status) || !handle)
        ObDereferenceObject(object);
    return status;
}

/* inserts a possibly named object, handling OBJ_OPENIF and name collisions. */
NTSTATUS ObInsertObjectNamed(PVOID object, POBJECT_ATTRIBUTES attributes, ACCESS_MASK access, KPROCESSOR_MODE mode,
                             PHANDLE handle)
{
    ULONG attrs = attributes ? attributes->Attributes : 0;
    if (mode == UserMode)
        attrs &= ~OBJ_KERNEL_HANDLE;
    if (!attributes || !attributes->ObjectName || !attributes->ObjectName->Length)
        return ObInsertObject(object, access, attrs, handle);

    UNICODE_STRING name = *attributes->ObjectName;
    POBJECT_DIRECTORY parent = NULL;
    UNICODE_STRING leaf = name;
    ULONG len = name.Length / sizeof(WCHAR);
    ULONG slash = len;
    while (slash > 0 && name.Buffer[slash - 1] != '\\')
        slash--;
    NTSTATUS status;
    if (attributes->RootDirectory) {
        status = ObReferenceHandle(attributes->RootDirectory, ObDirectoryType, mode, (PVOID *)&parent, NULL);
        if (!NT_SUCCESS(status)) {
            ObDereferenceObject(object);
            return status;
        }
        if (slash) {
            ObDereferenceObject(parent);
            ObDereferenceObject(object);
            return STATUS_OBJECT_NAME_INVALID;
        }
    } else {
        if (slash == 0) {
            ObDereferenceObject(object);
            return STATUS_OBJECT_PATH_SYNTAX_BAD;
        }
        UNICODE_STRING dir = {(USHORT)((slash > 1 ? slash - 1 : 1) * 2), 0, name.Buffer};
        dir.MaximumLength = dir.Length;
        status = ObLookupObjectName(&dir, NULL, 0, ObDirectoryType, mode, NULL, (PVOID *)&parent);
        if (!NT_SUCCESS(status)) {
            ObDereferenceObject(object);
            return STATUS_OBJECT_PATH_NOT_FOUND;
        }
        leaf.Buffer = name.Buffer + slash;
        leaf.Length = leaf.MaximumLength = (USHORT)((len - slash) * 2);
    }

    status = ObInsertIntoDirectory(parent, object, &leaf);
    if (status == STATUS_OBJECT_NAME_COLLISION && (attrs & OBJ_OPENIF)) {
        PVOID existing;
        UNICODE_STRING full = name;
        NTSTATUS st = ObLookupObjectName(&full, attributes->RootDirectory, attrs, OBJECT_TO_HEADER(object)->Type,
                                         mode, NULL, &existing);
        ObDereferenceObject(parent);
        ObDereferenceObject(object);
        if (!NT_SUCCESS(st))
            return st == STATUS_OBJECT_TYPE_MISMATCH ? st : STATUS_OBJECT_NAME_COLLISION;
        st = ObInsertObject(existing, access, attrs, handle);
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
        return STATUS_INVALID_HANDLE;
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
    return ObReferenceHandle(handle, type, mode, object, NULL);
}

static NTSTATUS close_in_table(PEPROCESS process, PHANDLE_TABLE table, ULONG_PTR index)
{
    KIRQL irql = KeAcquireSpinLockRaiseToDpc(&table->Lock);
    if (index >= table->Capacity || !table->Entries[index].Object) {
        KeReleaseSpinLock(&table->Lock, irql);
        return STATUS_INVALID_HANDLE;
    }
    PVOID obj = table->Entries[index].Object;
    table->Entries[index].Object = NULL;
    table->Count--;
    if (index < table->FirstFree)
        table->FirstFree = (ULONG)index;
    KeReleaseSpinLock(&table->Lock, irql);

    OBJECT_HEADER *h = OBJECT_TO_HEADER(obj);
    LONG_PTR remaining = __atomic_sub_fetch(&h->HandleCount, 1, __ATOMIC_ACQ_REL);
    __atomic_sub_fetch(&h->Type->TotalHandles, 1, __ATOMIC_RELAXED);
    if (h->Type->Init.CloseProcedure)
        h->Type->Init.CloseProcedure(process, obj, (ULONG)remaining + 1);
    /* unnamed temporary objects leave the namespace with their last handle. */
    ObDereferenceObject(obj);
    return STATUS_SUCCESS;
}

NTSTATUS ObCloseHandle(HANDLE handle, KPROCESSOR_MODE mode)
{
    if (handle == NtCurrentProcess() || handle == NtCurrentThread())
        return STATUS_SUCCESS;
    PEPROCESS owner;
    PHANDLE_TABLE table = table_for(handle, mode, &owner);
    return close_in_table(owner, table, HANDLE_TO_INDEX(handle));
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
    for (ULONG i = 0; i < table->Capacity; i++) {
        if (table->Entries && table->Entries[i].Object)
            close_in_table(process, table, i);
    }
    if (table->Entries)
        ExFreePool(table->Entries);
    table->Entries = NULL;
    table->Capacity = 0;
}

NTSTATUS ObOpenObjectByName(POBJECT_ATTRIBUTES attributes, POBJECT_TYPE type, KPROCESSOR_MODE mode,
                            ACCESS_MASK access, PVOID parse_context, PHANDLE handle)
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
    return ObInsertObject(object, access, attrs, handle);
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
        status = ObReferenceHandle(source, NULL, KernelMode, &object, &granted);
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
    if (!(options & DUPLICATE_SAME_ACCESS))
        granted = access;
    HANDLE h = NULL;
    if (target_process && target) {
        status = ObCreateHandle(target_process, object, granted, attributes, &h);
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
    PEPROCESS sp, tp = NULL;
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
    HANDLE h = NULL;
    status = ObDuplicateHandle(sp, source, tp, target ? &h : NULL, access, attributes, options);
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
    name->Buffer = NULL;
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
    status = ObOpenObjectByName(&oa, ObDirectoryType, KeGetPreviousMode(), access, NULL, &h);
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
    status = ObOpenObjectByName(&oa, ObSymbolicLinkType, KeGetPreviousMode(), access, NULL, &h);
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
        /* the link owns its target, ObCreateObject left the body uninitialized. */
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
    NTSTATUS status = ObReferenceHandle(handle, NULL, KeGetPreviousMode(), &object, &granted);
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
    NTSTATUS status = ObReferenceHandle(handle, ObSymbolicLinkType, KeGetPreviousMode(), (PVOID *)&link, NULL);
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
    PVOID object;
    NTSTATUS status = ObReferenceHandle(handle, NULL, KeGetPreviousMode(), &object, NULL);
    if (!NT_SUCCESS(status))
        return status;
    if (klass == ObjectNameInformation) {
        WCHAR buf[512];
        UNICODE_STRING name = {0};
        if (OBJECT_TO_HEADER(object)->Directory)
            ObQueryNameString(object, &name, buf, RTL_NUMBER_OF(buf));
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
    } else {
        status = STATUS_INVALID_INFO_CLASS;
    }
    ObDereferenceObject(object);
    return status;
}
