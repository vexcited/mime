/*
 * object manager: object types, reference counting, handles and the
 * object namespace.
 */
#include "../ntos.h"
#include <mime/ntrtlsec.h>

POBJECT_TYPE ObTypeType;
POBJECT_TYPE ObDirectoryType;
POBJECT_TYPE ObSymbolicLinkType;
POBJECT_DIRECTORY ObRootDirectory;

static POBJECT_TYPE g_types[64];
static ULONG g_type_count;
static KSPIN_LOCK g_namespace_lock;

POBJECT_TYPE ObCreateObjectType(PCWSTR name, const OBJECT_TYPE_INITIALIZER *init)
{
    POBJECT_TYPE type = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(OBJECT_TYPE), KI_TAG('O', 'b', 'T', 'y'));
    if (!type)
        return nullptr;
    size_t len = wcslen(name);
    if (len >= RTL_NUMBER_OF(type->NameBuffer))
        len = RTL_NUMBER_OF(type->NameBuffer) - 1;
    memcpy(type->NameBuffer, name, len * sizeof(WCHAR));
    type->NameBuffer[len] = 0;
    RtlInitUnicodeString(&type->Name, type->NameBuffer);
    type->Init = *init;
    type->Index = g_type_count;
    if (g_type_count < RTL_NUMBER_OF(g_types))
        g_types[g_type_count++] = type;
    return type;
}

/* what NtQueryObject says of a type, in windows' layout. */
typedef struct OB_TYPE_INFORMATION {
    UNICODE_STRING TypeName;
    ULONG TotalNumberOfObjects, TotalNumberOfHandles;
    ULONG TotalPagedPoolUsage, TotalNonPagedPoolUsage, TotalNamePoolUsage, TotalHandleTableUsage;
    ULONG HighWaterNumberOfObjects, HighWaterNumberOfHandles;
    ULONG HighWaterPagedPoolUsage, HighWaterNonPagedPoolUsage, HighWaterNamePoolUsage, HighWaterHandleTableUsage;
    ULONG InvalidAttributes;
    GENERIC_MAPPING GenericMapping;
    ULONG ValidAccessMask;
    BOOLEAN SecurityRequired, MaintainHandleCount;
    UCHAR TypeIndex;
    CHAR ReservedByte;
    ULONG PoolType;
    ULONG DefaultPagedPoolCharge, DefaultNonPagedPoolCharge;
} OB_TYPE_INFORMATION;
MIME_STATIC_ASSERT(sizeof(OB_TYPE_INFORMATION) == 0x68, "OB_TYPE_INFORMATION");

/* the generic rights of the types programs open most, the others map them all to nothing. */
static GENERIC_MAPPING mapping_of(PCWSTR name, ACCESS_MASK all)
{
    static const struct {
        PCWSTR name;
        GENERIC_MAPPING mapping;
    } known[] = {
        {L"File", {0x120089, 0x120116, 0x1200A0, 0x1F01FF}},
        {L"Event", {0x20001, 0x20002, 0x120000, 0x1F0003}},
        {L"Mutant", {0x20001, 0x20000, 0x120000, 0x1F0001}},
        {L"Semaphore", {0x20001, 0x20002, 0x120000, 0x1F0003}},
        {L"Timer", {0x20001, 0x20002, 0x120000, 0x1F0003}},
        {L"IoCompletion", {0x20001, 0x20002, 0x120000, 0x1F0003}},
        {L"Section", {0x20001, 0x20002, 0x20000, 0xF001F}},
        {L"Process", {0x20410, 0x20BEA, 0x121001, 0x1FFFFF}},
        {L"Thread", {0x20048, 0x20437, 0x121A00, 0x1FFFFF}},
        {L"Key", {0x20019, 0x20006, 0x20019, 0xF003F}},
        {L"Token", {0x20008, 0x200E0, 0x20000, 0xF01FF}},
    };
    for (ULONG i = 0; i < RTL_NUMBER_OF(known); i++) {
        if (!_wcsicmp(known[i].name, name))
            return known[i].mapping;
    }
    return (GENERIC_MAPPING){0, 0, 0, all};
}

ACCESS_MASK ObMapGenericAccess(POBJECT_TYPE type, ACCESS_MASK mask)
{
    GENERIC_MAPPING mapping = mapping_of(type->Name.Buffer, type->Init.ValidAccessMask);
    RtlMapGenericMask(&mask, &mapping);
    return mask;
}

/* the size a type's information takes with its name after it, padded to eight. */
static ULONG type_information_size(POBJECT_TYPE type)
{
    return (ULONG)(sizeof(OB_TYPE_INFORMATION) + ((type->Name.Length + sizeof(WCHAR) + 7) & ~7u));
}

/* builds it for a buffer that will be at user_base in the caller. */
static void build_type_information(POBJECT_TYPE type, PUCHAR out, PUCHAR user_base)
{
    OB_TYPE_INFORMATION *info = (OB_TYPE_INFORMATION *)out;
    memset(out, 0, type_information_size(type));
    memcpy(out + sizeof(*info), type->Name.Buffer, type->Name.Length);
    info->TypeName.Length = type->Name.Length;
    info->TypeName.MaximumLength = (USHORT)(type->Name.Length + sizeof(WCHAR));
    info->TypeName.Buffer = (PWSTR)(user_base + sizeof(*info));
    info->TotalNumberOfObjects = info->HighWaterNumberOfObjects = (ULONG)type->TotalObjects;
    info->TotalNumberOfHandles = info->HighWaterNumberOfHandles = (ULONG)type->TotalHandles;
    info->GenericMapping = mapping_of(type->Name.Buffer, type->Init.ValidAccessMask);
    info->ValidAccessMask = type->Init.ValidAccessMask;
    info->SecurityRequired = TRUE;
    info->TypeIndex = (UCHAR)(type->Index + 2);
    info->PoolType = type->Init.PoolType;
    info->DefaultNonPagedPoolCharge = type->Init.DefaultObjectSize;
}

static NTSTATUS return_answer(const void *answer, ULONG need, PVOID info, ULONG length, PULONG returned)
{
    if (returned)
        KiCopyToUser(returned, &need, sizeof(need));
    if (length < need)
        return STATUS_INFO_LENGTH_MISMATCH;
    return KiCopyToUser(info, answer, need);
}

/* ObjectTypesInformation: the count, then every type's information, each starting on eight bytes. */
NTSTATUS ObQueryAllTypesInformation(PVOID info, ULONG length, PULONG returned)
{
    ULONG need = 8;
    for (ULONG i = 0; i < g_type_count; i++)
        need += type_information_size(g_types[i]);
    PUCHAR answer = ExAllocatePool2(POOL_FLAG_NON_PAGED, need, KI_TAG('O', 'b', 'Q', 't'));
    if (!answer)
        return STATUS_INSUFFICIENT_RESOURCES;
    memset(answer, 0, 8);
    *(PULONG)answer = g_type_count;
    ULONG offset = 8;
    for (ULONG i = 0; i < g_type_count; i++) {
        build_type_information(g_types[i], answer + offset, (PUCHAR)info + offset);
        offset += type_information_size(g_types[i]);
    }
    NTSTATUS status = return_answer(answer, need, info, length, returned);
    ExFreePool(answer);
    return status;
}

POBJECT_TYPE ObGetObjectType(PVOID object)
{
    return OBJECT_TO_HEADER(object)->Type;
}

NTSTATUS ObCreateObject(POBJECT_TYPE type, POBJECT_ATTRIBUTES attributes, KPROCESSOR_MODE mode, ULONG body_size,
                        PVOID *object)
{
    OBJECT_HEADER *h = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(OBJECT_HEADER) + body_size, KI_TAG('O', 'b', 'j', ' '));
    if (!h)
        return STATUS_INSUFFICIENT_RESOURCES;
    h->PointerCount = 1;
    h->HandleCount = 0;
    h->Type = type;
    h->Directory = nullptr;
    h->Flags = OB_FLAG_SD_PENDING;
    if (attributes) {
        if (attributes->Attributes & OBJ_PERMANENT)
            h->Flags |= OB_FLAG_PERMANENT;
        if (attributes->Attributes & OBJ_KERNEL_HANDLE)
            h->Flags |= OB_FLAG_KERNEL;
    }
    __atomic_add_fetch(&type->TotalObjects, 1, __ATOMIC_RELAXED);
    *object = HEADER_TO_OBJECT(h);
    return STATUS_SUCCESS;
}

LONG_PTR FASTCALL ObfReferenceObject(PVOID object)
{
    return __atomic_add_fetch(&OBJECT_TO_HEADER(object)->PointerCount, 1, __ATOMIC_RELAXED);
}

static void remove_from_directory(OBJECT_HEADER *h);

static void delete_object(OBJECT_HEADER *h)
{
    PVOID body = HEADER_TO_OBJECT(h);
    if (h->Directory)
        remove_from_directory(h);
    if (h->Type->Init.DeleteProcedure)
        h->Type->Init.DeleteProcedure(body);
    if (h->Name.Buffer)
        ExFreePool(h->Name.Buffer);
    ObpFreeSecurity(h);
    __atomic_sub_fetch(&h->Type->TotalObjects, 1, __ATOMIC_RELAXED);
    ExFreePool(h);
}

LONG_PTR FASTCALL ObfDereferenceObject(PVOID object)
{
    OBJECT_HEADER *h = OBJECT_TO_HEADER(object);
    LONG_PTR count = __atomic_sub_fetch(&h->PointerCount, 1, __ATOMIC_ACQ_REL);
    if (count == 0)
        delete_object(h);
    else if (count < 0)
        KeBugCheckEx(0x00000018, 0, (ULONG_PTR)object, (ULONG_PTR)count, 0); /* REFERENCE_BY_POINTER */
    return count;
}

void NTAPI ObDereferenceObjectDeferDelete(PVOID object)
{
    ObfDereferenceObject(object);
}

NTSTATUS NTAPI ObReferenceObjectByPointer(PVOID object, ACCESS_MASK access, POBJECT_TYPE type, KPROCESSOR_MODE mode)
{
    if (type && OBJECT_TO_HEADER(object)->Type != type)
        return STATUS_OBJECT_TYPE_MISMATCH;
    ObfReferenceObject(object);
    return STATUS_SUCCESS;
}

PDISPATCHER_HEADER ObGetWaitObject(PVOID object)
{
    POBJECT_TYPE type = OBJECT_TO_HEADER(object)->Type;
    if (type->Init.WaitObject)
        return type->Init.WaitObject(object);
    if (type->Init.Waitable)
        return object;
    return nullptr;
}

/* ---------------------------------------------------------- directory */

static ULONG name_hash(PCUNICODE_STRING name)
{
    ULONG h = 0;
    for (USHORT i = 0; i < name->Length / sizeof(WCHAR); i++)
        h = h * 31 + towupper(name->Buffer[i]);
    return h % OB_DIRECTORY_BUCKETS;
}

static PVOID directory_lookup(POBJECT_DIRECTORY dir, PCUNICODE_STRING name)
{
    for (OBJECT_DIRECTORY_ENTRY *e = dir->Buckets[name_hash(name)]; e; e = e->Next) {
        OBJECT_HEADER *h = OBJECT_TO_HEADER(e->Object);
        if (RtlEqualUnicodeString(&h->Name, name, TRUE))
            return e->Object;
    }
    return nullptr;
}

NTSTATUS ObInsertIntoDirectory(POBJECT_DIRECTORY dir, PVOID object, PCUNICODE_STRING name)
{
    OBJECT_HEADER *h = OBJECT_TO_HEADER(object);
    KIRQL irql = KeAcquireSpinLockRaiseToDpc(&g_namespace_lock);
    if (directory_lookup(dir, name)) {
        KeReleaseSpinLock(&g_namespace_lock, irql);
        return STATUS_OBJECT_NAME_COLLISION;
    }
    KeReleaseSpinLock(&g_namespace_lock, irql);

    OBJECT_DIRECTORY_ENTRY *e = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*e), KI_TAG('O', 'b', 'D', 'e'));
    PWSTR buf = ExAllocatePool2(POOL_FLAG_NON_PAGED, name->Length + sizeof(WCHAR), KI_TAG('O', 'b', 'N', 'm'));
    if (!e || !buf) {
        ExFreePool(e);
        ExFreePool(buf);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    memcpy(buf, name->Buffer, name->Length);
    buf[name->Length / sizeof(WCHAR)] = 0;
    h->Name.Buffer = buf;
    h->Name.Length = name->Length;
    h->Name.MaximumLength = (USHORT)(name->Length + sizeof(WCHAR));
    e->Object = object;
    irql = KeAcquireSpinLockRaiseToDpc(&g_namespace_lock);
    ULONG bucket = name_hash(name);
    e->Next = dir->Buckets[bucket];
    dir->Buckets[bucket] = e;
    h->Directory = dir;
    KeReleaseSpinLock(&g_namespace_lock, irql);
    return STATUS_SUCCESS;
}

static void remove_from_directory(OBJECT_HEADER *h)
{
    POBJECT_DIRECTORY dir = h->Directory;
    KIRQL irql = KeAcquireSpinLockRaiseToDpc(&g_namespace_lock);
    OBJECT_DIRECTORY_ENTRY **link = &dir->Buckets[name_hash(&h->Name)];
    PVOID body = HEADER_TO_OBJECT(h);
    while (*link) {
        if ((*link)->Object == body) {
            OBJECT_DIRECTORY_ENTRY *e = *link;
            *link = e->Next;
            KeReleaseSpinLock(&g_namespace_lock, irql);
            ExFreePool(e);
            h->Directory = nullptr;
            return;
        }
        link = &(*link)->Next;
    }
    KeReleaseSpinLock(&g_namespace_lock, irql);
    h->Directory = nullptr;
}

/*
 * resolves an absolute path. symbolic links are followed; when a component
 * is an object with a parse procedure (devices), the rest of the path is
 * handed to it, which is how file opens reach file system drivers.
 */
NTSTATUS ObLookupObjectName(PUNICODE_STRING name, HANDLE root, ULONG attributes, POBJECT_TYPE type,
                            KPROCESSOR_MODE mode, PVOID parse_context, PVOID *object)
{
    /* object attributes with no name are an empty one: with a root directory that is the directory, without one a name nobody can parse. */
    UNICODE_STRING nothing = {};
    if (!name)
        name = &nothing;
    WCHAR *path = ExAllocatePool2(POOL_FLAG_PAGED, 1024 * sizeof(WCHAR), KI_TAG('O', 'b', 'P', 't'));
    if (!path)
        return STATUS_INSUFFICIENT_RESOURCES;
    ULONG len = 0;
    PVOID current = nullptr;

    if (root) {
        NTSTATUS status = ObReferenceHandle(root, nullptr, mode, &current, nullptr);
        if (!NT_SUCCESS(status)) {
            ExFreePool(path);
            return status;
        }
        if (OBJECT_TO_HEADER(current)->Type != ObDirectoryType) {
            /* relative opens below a non directory object go straight to its parse procedure. */
            POBJECT_TYPE t = OBJECT_TO_HEADER(current)->Type;
            NTSTATUS st = STATUS_OBJECT_TYPE_MISMATCH;
            if (t->Init.ParseProcedure) {
                UNICODE_STRING remaining = *name;
                st = t->Init.ParseProcedure(current, type, attributes, name, &remaining, parse_context, object);
            }
            ObDereferenceObject(current);
            ExFreePool(path);
            return st;
        }
    } else {
        if (name->Length < sizeof(WCHAR) || name->Buffer[0] != '\\') {
            ExFreePool(path);
            return STATUS_OBJECT_PATH_SYNTAX_BAD;
        }
        current = ObRootDirectory;
        ObReferenceObject(current);
    }

    len = name->Length / sizeof(WCHAR);
    if (len >= 1024) {
        ObDereferenceObject(current);
        ExFreePool(path);
        return STATUS_NAME_TOO_LONG;
    }
    memcpy(path, name->Buffer, name->Length);
    path[len] = 0;
    ULONG pos = 0;
    NTSTATUS status = STATUS_SUCCESS;
    int reparse = 0;

    for (;;) {
        OBJECT_HEADER *ch = OBJECT_TO_HEADER(current);
        if (ch->Type != ObDirectoryType && ch->Type->Init.ParseProcedure && (pos < len || parse_context)) {
            /* the remaining path keeps its leading backslash, an empty one opens the object itself. */
            UNICODE_STRING complete = {(USHORT)(len * 2), (USHORT)(len * 2), path};
            UNICODE_STRING remaining = {(USHORT)((len - pos) * 2), (USHORT)((len - pos) * 2), path + pos};
            status = ch->Type->Init.ParseProcedure(current, type, attributes, &complete, &remaining, parse_context,
                                                   object);
            ObDereferenceObject(current);
            break;
        }
        while (pos < len && path[pos] == '\\')
            pos++;
        if (pos >= len) {
            *object = current;
            status = (type && ch->Type != type) ? STATUS_OBJECT_TYPE_MISMATCH : STATUS_SUCCESS;
            if (!NT_SUCCESS(status))
                ObDereferenceObject(current);
            break;
        }
        if (ch->Type != ObDirectoryType) {
            status = STATUS_OBJECT_PATH_NOT_FOUND;
            ObDereferenceObject(current);
            break;
        }
        ULONG start = pos;
        while (pos < len && path[pos] != '\\')
            pos++;
        UNICODE_STRING component = {(USHORT)((pos - start) * 2), (USHORT)((pos - start) * 2), path + start};
        KIRQL irql = KeAcquireSpinLockRaiseToDpc(&g_namespace_lock);
        PVOID next = directory_lookup((POBJECT_DIRECTORY)current, &component);
        if (next)
            ObReferenceObject(next);
        KeReleaseSpinLock(&g_namespace_lock, irql);
        if (!next) {
            status = pos >= len ? STATUS_OBJECT_NAME_NOT_FOUND : STATUS_OBJECT_PATH_NOT_FOUND;
            ObDereferenceObject(current);
            break;
        }
        ObDereferenceObject(current);
        current = next;

        OBJECT_HEADER *nh = OBJECT_TO_HEADER(next);
        if (nh->Type == ObSymbolicLinkType && !(pos >= len && (attributes & OBJ_OPENLINK))) {
            /* splice the target in front of the remaining path and restart at the root. */
            POBJECT_SYMBOLIC_LINK link = next;
            ULONG tlen = link->Target.Length / sizeof(WCHAR);
            ULONG rest = len - pos;
            if (tlen + rest >= 1024 || ++reparse > 32) {
                status = STATUS_NAME_TOO_LONG;
                ObDereferenceObject(current);
                break;
            }
            memmove(path + tlen, path + pos, rest * sizeof(WCHAR));
            memcpy(path, link->Target.Buffer, tlen * sizeof(WCHAR));
            len = tlen + rest;
            path[len] = 0;
            pos = 0;
            ObDereferenceObject(current);
            current = ObRootDirectory;
            ObReferenceObject(current);
        }
    }
    ExFreePool(path);
    return status;
}

NTSTATUS NTAPI ObReferenceObjectByName(PUNICODE_STRING name, ULONG attributes, PVOID access_state,
                                       ACCESS_MASK access, POBJECT_TYPE type, KPROCESSOR_MODE mode,
                                       PVOID parse_context, PVOID *object)
{
    return ObLookupObjectName(name, nullptr, attributes, type, mode, parse_context, object);
}

/* creates (or opens) a directory chain such as L"\\Device". */
NTSTATUS ObCreateDirectory(PCWSTR path, POBJECT_DIRECTORY *out)
{
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, path);
    PVOID existing;
    if (NT_SUCCESS(ObLookupObjectName(&name, nullptr, 0, ObDirectoryType, KernelMode, nullptr, &existing))) {
        if (out)
            *out = existing;
        else
            ObDereferenceObject(existing);
        return STATUS_OBJECT_NAME_EXISTS;
    }
    const WCHAR *slash = wcsrchr(path, '\\');
    POBJECT_DIRECTORY parent = ObRootDirectory;
    BOOLEAN deref_parent = FALSE;
    if (slash != path) {
        WCHAR parent_path[256];
        size_t n = (size_t)(slash - path);
        if (n >= RTL_NUMBER_OF(parent_path))
            return STATUS_NAME_TOO_LONG;
        memcpy(parent_path, path, n * sizeof(WCHAR));
        parent_path[n] = 0;
        NTSTATUS status = ObCreateDirectory(parent_path, &parent);
        if (!NT_SUCCESS(status))
            return status;
        deref_parent = TRUE;
    }
    POBJECT_DIRECTORY dir;
    NTSTATUS status = ObCreateObject(ObDirectoryType, nullptr, KernelMode, sizeof(OBJECT_DIRECTORY), (PVOID *)&dir);
    if (NT_SUCCESS(status)) {
        memset(dir, 0, sizeof(*dir));
        UNICODE_STRING leaf;
        RtlInitUnicodeString(&leaf, slash + 1);
        OBJECT_TO_HEADER(dir)->Flags |= OB_FLAG_PERMANENT;
        status = ObInsertIntoDirectory(parent, dir, &leaf);
        if (NT_SUCCESS(status) && out) {
            ObReferenceObject(dir);
            *out = dir;
        }
    }
    if (deref_parent)
        ObDereferenceObject(parent);
    return status;
}

NTSTATUS ObCreateSymbolicLinkObject(PCUNICODE_STRING link, PCUNICODE_STRING target, BOOLEAN permanent)
{
    const WCHAR *buf = link->Buffer;
    ULONG len = link->Length / sizeof(WCHAR);
    ULONG slash = len;
    while (slash > 0 && buf[slash - 1] != '\\')
        slash--;
    if (slash == 0)
        return STATUS_OBJECT_PATH_SYNTAX_BAD;
    POBJECT_DIRECTORY parent = ObRootDirectory;
    BOOLEAN deref = FALSE;
    if (slash > 1) {
        UNICODE_STRING dir_name = {(USHORT)((slash - 1) * 2), (USHORT)((slash - 1) * 2), (PWSTR)buf};
        NTSTATUS status = ObLookupObjectName(&dir_name, nullptr, 0, ObDirectoryType, KernelMode, nullptr, (PVOID *)&parent);
        if (!NT_SUCCESS(status))
            return status;
        deref = TRUE;
    }
    POBJECT_SYMBOLIC_LINK sl;
    NTSTATUS status = ObCreateObject(ObSymbolicLinkType, nullptr, KernelMode, sizeof(OBJECT_SYMBOLIC_LINK), (PVOID *)&sl);
    if (NT_SUCCESS(status)) {
        sl->Target.Buffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, target->Length + sizeof(WCHAR), KI_TAG('O', 'b', 'S', 'l'));
        if (!sl->Target.Buffer) {
            ObDereferenceObject(sl);
            status = STATUS_INSUFFICIENT_RESOURCES;
        } else {
            memcpy(sl->Target.Buffer, target->Buffer, target->Length);
            sl->Target.Buffer[target->Length / sizeof(WCHAR)] = 0;
            sl->Target.Length = target->Length;
            sl->Target.MaximumLength = (USHORT)(target->Length + sizeof(WCHAR));
            UNICODE_STRING leaf = {(USHORT)((len - slash) * 2), (USHORT)((len - slash) * 2), (PWSTR)buf + slash};
            if (permanent)
                OBJECT_TO_HEADER(sl)->Flags |= OB_FLAG_PERMANENT;
            status = ObInsertIntoDirectory(parent, sl, &leaf);
            if (!NT_SUCCESS(status))
                ObDereferenceObject(sl);
        }
    }
    if (deref)
        ObDereferenceObject(parent);
    return status;
}

NTSTATUS ObCreateSymbolicLink(PCWSTR link, PCWSTR target)
{
    UNICODE_STRING l, t;
    RtlInitUnicodeString(&l, link);
    RtlInitUnicodeString(&t, target);
    return ObCreateSymbolicLinkObject(&l, &t, TRUE);
}

NTSTATUS NTAPI IoCreateSymbolicLink(PUNICODE_STRING link, PUNICODE_STRING target)
{
    return ObCreateSymbolicLinkObject(link, target, TRUE);
}

NTSTATUS NTAPI IoDeleteSymbolicLink(PUNICODE_STRING link)
{
    PVOID object;
    NTSTATUS status = ObLookupObjectName(link, nullptr, OBJ_OPENLINK, ObSymbolicLinkType, KernelMode, nullptr, &object);
    if (!NT_SUCCESS(status))
        return status;
    OBJECT_HEADER *h = OBJECT_TO_HEADER(object);
    if (h->Directory)
        remove_from_directory(h);
    if (h->Flags & OB_FLAG_PERMANENT) {
        h->Flags &= ~OB_FLAG_PERMANENT;
        ObDereferenceObject(object);
    }
    ObDereferenceObject(object);
    return STATUS_SUCCESS;
}

void ObMakeTemporaryObject(PVOID object)
{
    /* drops the reference that kept a permanent object named, it leaves with its last reference. */
    OBJECT_HEADER *h = OBJECT_TO_HEADER(object);
    if (__atomic_fetch_and(&h->Flags, ~OB_FLAG_PERMANENT, __ATOMIC_ACQ_REL) & OB_FLAG_PERMANENT)
        ObDereferenceObject(object);
}

NTSTATUS ObQueryNameString(PVOID object, PUNICODE_STRING name, PWSTR buffer, ULONG buffer_chars)
{
    /* builds the full path by walking up the directories. */
    WCHAR tmp[512];
    ULONG pos = RTL_NUMBER_OF(tmp);
    OBJECT_HEADER *h = OBJECT_TO_HEADER(object);
    tmp[--pos] = 0;
    while (h && h->Directory && h->Name.Length) {
        ULONG n = h->Name.Length / sizeof(WCHAR);
        if (n + 1 >= pos)
            return STATUS_NAME_TOO_LONG;
        pos -= n;
        memcpy(tmp + pos, h->Name.Buffer, n * sizeof(WCHAR));
        tmp[--pos] = '\\';
        h = OBJECT_TO_HEADER(h->Directory);
        if (HEADER_TO_OBJECT(h) == ObRootDirectory)
            break;
    }
    ULONG len = RTL_NUMBER_OF(tmp) - 1 - pos;
    if (len + 1 > buffer_chars)
        return STATUS_BUFFER_TOO_SMALL;
    memcpy(buffer, tmp + pos, (len + 1) * sizeof(WCHAR));
    name->Buffer = buffer;
    name->Length = (USHORT)(len * sizeof(WCHAR));
    name->MaximumLength = (USHORT)((len + 1) * sizeof(WCHAR));
    return STATUS_SUCCESS;
}

static void symlink_delete(PVOID object)
{
    POBJECT_SYMBOLIC_LINK link = object;
    if (link->Target.Buffer)
        ExFreePool(link->Target.Buffer);
}

void ObInitialize(void)
{
    ObpInitializeSecurity();
    OBJECT_TYPE_INITIALIZER init = {};
    init.ValidAccessMask = DIRECTORY_ALL_ACCESS;
    ObSetGenericMapping(&init, 0x20000, 0x20000, 0x20000, 0xF0001);
    ObTypeType = ObCreateObjectType(L"Type", &init);
    ObSetGenericMapping(&init, 0x20003, 0x2000C, 0x20003, 0xF000F);
    ObDirectoryType = ObCreateObjectType(L"Directory", &init);
    init.ValidAccessMask = SYMBOLIC_LINK_ALL_ACCESS;
    init.DeleteProcedure = symlink_delete;
    ObSetGenericMapping(&init, 0x20001, 0x20000, 0x20001, 0xF0001);
    ObSymbolicLinkType = ObCreateObjectType(L"SymbolicLink", &init);

    ObCreateObject(ObDirectoryType, nullptr, KernelMode, sizeof(OBJECT_DIRECTORY), (PVOID *)&ObRootDirectory);
    memset(ObRootDirectory, 0, sizeof(*ObRootDirectory));
    OBJECT_TO_HEADER(ObRootDirectory)->Flags |= OB_FLAG_PERMANENT;

    static const PCWSTR dirs[] = {
        L"\\Device", L"\\Driver", L"\\FileSystem", L"\\GLOBAL??", L"\\KnownDlls", L"\\BaseNamedObjects",
        L"\\Sessions", L"\\Sessions\\BNOLINKS", L"\\Sessions\\1", L"\\Sessions\\1\\BaseNamedObjects", L"\\Windows",
        L"\\Windows\\WindowStations",
        L"\\Sessions\\1\\Windows", L"\\Sessions\\1\\Windows\\WindowStations", L"\\ObjectTypes",
        L"\\Callback", L"\\KernelObjects", L"\\RPC Control",
    };
    for (ULONG i = 0; i < RTL_NUMBER_OF(dirs); i++)
        ObCreateDirectory(dirs[i], nullptr);
    ObCreateSymbolicLink(L"\\??", L"\\GLOBAL??");
    ObCreateSymbolicLink(L"\\DosDevices", L"\\GLOBAL??");
    ObCreateSymbolicLink(L"\\SystemRoot", L"\\GLOBAL??\\C:\\Windows");
    ObCreateSymbolicLink(L"\\GLOBAL??\\Global", L"\\GLOBAL??");
    ObCreateSymbolicLink(L"\\Sessions\\1\\BaseNamedObjects\\Global", L"\\BaseNamedObjects");
    /* Session\<n>\ names reach the objects of a session from any other. */
    ObCreateSymbolicLink(L"\\Sessions\\1\\BaseNamedObjects\\Session", L"\\Sessions\\BNOLINKS");
    ObCreateSymbolicLink(L"\\Sessions\\BNOLINKS\\1", L"\\Sessions\\1\\BaseNamedObjects");
    ObCreateSymbolicLink(L"\\BaseNamedObjects\\Global", L"\\BaseNamedObjects");
    ObCreateSymbolicLink(L"\\BaseNamedObjects\\Session", L"\\Sessions\\BNOLINKS");
}

/* ------------------------------------------------------------------ listing a directory */

typedef struct DIRECTORY_INFORMATION {
    UNICODE_STRING Name;
    UNICODE_STRING TypeName;
} DIRECTORY_INFORMATION;

/*
 * windows' layout: the entries, a zeroed one, then their strings, each
 * ending in a null; the context is the index of the next entry, in the
 * order of the hash buckets.
 */
NTSTATUS NTAPI NtQueryDirectoryObject(HANDLE handle, PVOID buffer, ULONG length, BOOLEAN single, BOOLEAN restart,
                                      PULONG user_context, PULONG user_returned)
{
    POBJECT_DIRECTORY dir;
    NTSTATUS status = ObReferenceHandle(handle, ObDirectoryType, KeGetPreviousMode(), (PVOID *)&dir, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    ULONG start = 0;
    if (!restart && user_context && !NT_SUCCESS(KiCopyFromUser(&start, user_context, sizeof(start)))) {
        ObDereferenceObject(dir);
        return STATUS_ACCESS_VIOLATION;
    }
    if (length > 0x100000)
        length = 0x100000;
    PUCHAR out = ExAllocatePool2(POOL_FLAG_NON_PAGED, length ? length : 1, KI_TAG('O', 'b', 'Q', 'd'));
    if (!out) {
        ObDereferenceObject(dir);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    /* first pass counts what fits, second writes it; names are copied under the lock. */
    ULONG index = 0, taken = 0, needed = 0;
    SIZE_T strings = 0;
    BOOLEAN more = FALSE;
    KIRQL irql = KeAcquireSpinLockRaiseToDpc(&g_namespace_lock);
    ULONG total = 0;
    for (ULONG b = 0; b < OB_DIRECTORY_BUCKETS; b++)
        for (OBJECT_DIRECTORY_ENTRY *e = dir->Buckets[b]; e; e = e->Next)
            total++;
    for (ULONG pass = 0; pass < 2; pass++) {
        index = 0;
        ULONG count = 0;
        SIZE_T text = 0;
        for (ULONG b = 0; b < OB_DIRECTORY_BUCKETS; b++) {
            for (OBJECT_DIRECTORY_ENTRY *e = dir->Buckets[b]; e; e = e->Next, index++) {
                if (index < start)
                    continue;
                OBJECT_HEADER *h = OBJECT_TO_HEADER(e->Object);
                POBJECT_TYPE type = ObGetObjectType(e->Object);
                SIZE_T name = h->Name.Length + sizeof(WCHAR), type_name = type->Name.Length + sizeof(WCHAR);
                if (pass == 0) {
                    SIZE_T size = (count + 2) * sizeof(DIRECTORY_INFORMATION) + text + name + type_name;
                    if ((single && count == 1) || size > length) {
                        if (!count)
                            needed = (ULONG)size;
                        more = TRUE;
                        goto counted;
                    }
                    count++;
                    text += name + type_name;
                    continue;
                }
                if (count >= taken)
                    goto written;
                DIRECTORY_INFORMATION *info = (DIRECTORY_INFORMATION *)out + count;
                PUCHAR place = out + (taken + 1) * sizeof(DIRECTORY_INFORMATION) + text;
                memcpy(place, h->Name.Buffer, h->Name.Length);
                ((PWCHAR)place)[h->Name.Length / sizeof(WCHAR)] = 0;
                info->Name = (UNICODE_STRING){h->Name.Length, (USHORT)name, (PWSTR)place};
                memcpy(place + name, type->Name.Buffer, type->Name.Length);
                ((PWCHAR)(place + name))[type->Name.Length / sizeof(WCHAR)] = 0;
                info->TypeName = (UNICODE_STRING){type->Name.Length, (USHORT)type_name, (PWSTR)(place + name)};
                text += name + type_name;
                count++;
            }
        }
    counted:
        if (pass == 0) {
            taken = count;
            strings = text;
        }
    }
written:
    KeReleaseSpinLock(&g_namespace_lock, irql);
    ObDereferenceObject(dir);
    ULONG used = (ULONG)((taken + 1) * sizeof(DIRECTORY_INFORMATION) + strings);
    if (!taken) {
        ExFreePool(out);
        if (start >= total)
            return STATUS_NO_MORE_ENTRIES;
        if (user_returned)
            KiCopyToUser(user_returned, &needed, sizeof(needed));
        return STATUS_BUFFER_TOO_SMALL;
    }
    memset((DIRECTORY_INFORMATION *)out + taken, 0, sizeof(DIRECTORY_INFORMATION));
    /* the pointers become the caller's. */
    for (ULONG i = 0; i < taken; i++) {
        DIRECTORY_INFORMATION *info = (DIRECTORY_INFORMATION *)out + i;
        info->Name.Buffer = (PWSTR)((PUCHAR)buffer + ((PUCHAR)info->Name.Buffer - out));
        info->TypeName.Buffer = (PWSTR)((PUCHAR)buffer + ((PUCHAR)info->TypeName.Buffer - out));
    }
    status = KiCopyToUser(buffer, out, used);
    ExFreePool(out);
    if (!NT_SUCCESS(status))
        return status;
    ULONG next = start + taken;
    if (user_context)
        KiCopyToUser(user_context, &next, sizeof(next));
    if (user_returned)
        KiCopyToUser(user_returned, &used, sizeof(used));
    return more && !single ? STATUS_MORE_ENTRIES : STATUS_SUCCESS;
}
