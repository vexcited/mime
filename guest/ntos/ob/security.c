/*
 * object security descriptors. an object gets one when inserted: the creator's, plus what its directory
 * hands down, completed from the creating thread's token. types that store theirs elsewhere, files with
 * their file system and keys with the registry, provide a pair of procedures. the descriptor is a shared
 * blob in the object header's reserved word. a type without generic rights has none, and an object
 * nobody gave one answers with the system's default.
 *
 * a user mode open of an existing object is checked against its descriptor with the thread's token, see
 * ObCheckOpenAccess, and creating a named object is checked against the target directory's. handles are
 * checked at each use. the privilege, owner and label rules for setting a descriptor are in se/sd.c.
 */
#include "../ntos.h"
#include "../se/se.h"
#include <mime/ntrtlsec.h>

#define OB_SD_PENDING OB_FLAG_SD_PENDING
#define STATUS_INVALID_SECURITY_DESCR ((NTSTATUS)0xC0000079L)

/* for a type with no mapping of its own: the standard rights and all the specific ones. */
static const ULONG g_plain_mapping[4] = {0x20000, 0x20000, 0x20000, 0x1FFFFF};

static FAST_MUTEX g_lock;

void ObpInitializeSecurity(void)
{
    ExInitializeFastMutex(&g_lock);
}

static SD_MAPPING mapping_of(const OBJECT_TYPE *type)
{
    return type->Init.GenericMapping[3] ? type->Init.GenericMapping : g_plain_mapping;
}

static BOOLEAN securable(const OBJECT_TYPE *type)
{
    return type->Init.GenericMapping[3] != 0 || type->Init.GetSecurity != nullptr;
}

void ObpFreeSecurity(PVOID header)
{
    OBJECT_HEADER *h = header;
    if (h->Reserved) {
        SeSdDereference((SD_BLOB *)(ULONG_PTR)h->Reserved);
        h->Reserved = 0;
    }
}

/* ---------------------------------------------------------------- reading and storing */

NTSTATUS ObReferenceSecurity(PVOID object, SD_BLOB **sd)
{
    OBJECT_HEADER *h = OBJECT_TO_HEADER(object);
    *sd = nullptr;
    if (h->Type->Init.GetSecurity) {
        NTSTATUS status = h->Type->Init.GetSecurity(object, (PVOID *)sd);
        if (status != STATUS_INVALID_DEVICE_REQUEST)
            return status;
    }
    ExAcquireFastMutex(&g_lock);
    SD_BLOB *kept = (SD_BLOB *)(ULONG_PTR)h->Reserved;
    if (kept)
        *sd = SeSdReference(kept);
    ExReleaseFastMutex(&g_lock);
    return STATUS_SUCCESS;
}

NTSTATUS ObStoreSecurity(PVOID object, SD_BLOB *sd)
{
    OBJECT_HEADER *h = OBJECT_TO_HEADER(object);
    if (h->Type->Init.PutSecurity) {
        NTSTATUS status = h->Type->Init.PutSecurity(object, sd);
        if (status != STATUS_INVALID_DEVICE_REQUEST)
            return status;
    }
    ExAcquireFastMutex(&g_lock);
    SD_BLOB *old = (SD_BLOB *)(ULONG_PTR)h->Reserved;
    h->Reserved = (ULONG_PTR)SeSdReference(sd);
    ExReleaseFastMutex(&g_lock);
    SeSdDereference(old);
    return STATUS_SUCCESS;
}

/* ---------------------------------------------------------------- new objects */

/* whether any ace is inheritable by objects created in the container. */
static BOOLEAN hands_down(const SD_BLOB *sd)
{
    const SECURITY_DESCRIPTOR_RELATIVE *r = (const SECURITY_DESCRIPTOR_RELATIVE *)sd->Data;
    static const struct {
        USHORT Present;
        USHORT Offset;
    } lists[2] = {{SE_DACL_PRESENT, 16}, {SE_SACL_PRESENT, 12}};
    for (int i = 0; i < 2; i++) {
        ULONG offset = *(const ULONG *)(sd->Data + lists[i].Offset);
        if (!(r->Control & lists[i].Present) || !offset)
            continue;
        const ACL *acl = (const ACL *)(sd->Data + offset);
        const UCHAR *ace = (const UCHAR *)(acl + 1);
        for (ULONG n = 0; n < acl->AceCount; n++, ace += *(const USHORT *)(ace + 2))
            if (ace[1] & (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE))
                return TRUE;
    }
    return FALSE;
}

NTSTATUS ObComputeSecurity(POBJECT_TYPE type_object, const OBJECT_ATTRIBUTES *attributes, PVOID parent,
                           KPROCESSOR_MODE mode, SD_BLOB **result)
{
    const OBJECT_TYPE *type = type_object;
    *result = nullptr;
    /* file systems and the registry make their own descriptors, and a type with no rights has none. */
    if (type->Init.GetSecurity || !type->Init.GenericMapping[3])
        return STATUS_SUCCESS;

    SD_BLOB *creator = nullptr;
    if (attributes && attributes->SecurityDescriptor) {
        NTSTATUS status = SeSdCapture(attributes->SecurityDescriptor, mode, &creator);
        if (!NT_SUCCESS(status))
            return status;
    }
    SD_BLOB *parent_sd = nullptr;
    if (parent)
        ObReferenceSecurity(parent, &parent_sd);
    BOOLEAN container = type == ObDirectoryType;
    SD_BLOB *sd = nullptr;
    NTSTATUS status;
    if (!creator && (!parent_sd || !hands_down(parent_sd)))
        status = SeSdDefault(type->Init.GenericMapping, container, &sd);
    else
        status = SeSdAssign(parent_sd, creator, type->Init.GenericMapping, container, mode, &sd);
    if (parent_sd)
        SeSdDereference(parent_sd);
    if (creator)
        SeSdDereference(creator);
    /* at boot, before the first token exists, objects get no descriptor. */
    if (status == STATUS_NO_TOKEN)
        return STATUS_SUCCESS;
    if (!NT_SUCCESS(status))
        return status;
    *result = sd;
    return STATUS_SUCCESS;
}

/* takes ownership of a descriptor made by ObComputeSecurity. */
void ObAttachSecurity(PVOID object, SD_BLOB *sd)
{
    OBJECT_HEADER *h = OBJECT_TO_HEADER(object);
    h->Flags &= ~OB_SD_PENDING;
    if (sd)
        h->Reserved = (ULONG_PTR)sd;
}

/* the directories programs create named objects in. */
static BOOLEAN creatable_directory(PVOID object)
{
    WCHAR buffer[128];
    UNICODE_STRING name;
    if (!NT_SUCCESS(ObpQueryNameString(object, &name, buffer, RTL_NUMBER_OF(buffer))))
        return FALSE;
    static const PCWSTR creatable[] = {L"\\BaseNamedObjects", L"\\RPC Control", L"\\Windows\\WindowStations"};
    static const WCHAR sessions[] = L"\\Sessions\\";
    for (ULONG i = 0; i < RTL_NUMBER_OF(creatable); i++)
        if (!_wcsicmp(buffer, creatable[i]))
            return TRUE;
    /* the same inside a session: \Sessions\n\BaseNamedObjects and \Sessions\n\Windows\WindowStations. */
    if (!_wcsnicmp(buffer, sessions, RTL_NUMBER_OF(sessions) - 1)) {
        const WCHAR *rest = buffer + RTL_NUMBER_OF(sessions) - 1;
        while (*rest >= '0' && *rest <= '9')
            rest++;
        return !_wcsicmp(rest, L"\\BaseNamedObjects") || !_wcsicmp(rest, L"\\Windows\\WindowStations");
    }
    return FALSE;
}

/* for objects the kernel put in the namespace, see SeSdBootDefault. */
NTSTATUS ObAssignBootSecurity(PVOID object)
{
    OBJECT_HEADER *h = OBJECT_TO_HEADER(object);
    h->Flags &= ~(ULONG)OB_SD_PENDING;
    const OBJECT_TYPE *type = h->Type;
    if (!securable(type))
        return STATUS_SUCCESS;
    SD_BLOB *sd;
    NTSTATUS status = SeSdBootDefault(mapping_of(type), type == ObDirectoryType,
                                      type == ObDirectoryType && creatable_directory(object) ? SE_BOOT_CREATABLE
                                                                                              : SE_BOOT_PLAIN,
                                      &sd);
    if (NT_SUCCESS(status))
        h->Reserved = (ULONG_PTR)sd;
    return status;
}

/* for a window station or desktop everyone at the console uses: full access except changing its descriptor. */
NTSTATUS ObSetInteractiveSecurity(PVOID object, BOOLEAN everyone)
{
    OBJECT_HEADER *h = OBJECT_TO_HEADER(object);
    const OBJECT_TYPE *type = h->Type;
    SD_BLOB *sd;
    NTSTATUS status = SeSdBootDefault(mapping_of(type), FALSE, everyone ? SE_BOOT_INTERACTIVE : SE_BOOT_PLAIN_SYSTEM, &sd);
    if (!NT_SUCCESS(status))
        return status;
    h->Flags &= ~(ULONG)OB_SD_PENDING;
    status = ObStoreSecurity(object, sd);
    SeSdDereference(sd);
    return status;
}

NTSTATUS ObAssignSecurity(PVOID object, const OBJECT_ATTRIBUTES *attributes, PVOID parent, KPROCESSOR_MODE mode)
{
    OBJECT_HEADER *h = OBJECT_TO_HEADER(object);
    if (!(h->Flags & OB_SD_PENDING))
        return STATUS_SUCCESS;
    SD_BLOB *sd;
    NTSTATUS status = ObComputeSecurity(h->Type, attributes, parent, mode, &sd);
    if (!NT_SUCCESS(status)) {
        h->Flags &= ~OB_SD_PENDING;
        return status;
    }
    ObAttachSecurity(object, sd);
    return STATUS_SUCCESS;
}

/* replaces an existing descriptor with what the creator offered. */
NTSTATUS ObReplaceSecurity(PVOID object, const OBJECT_ATTRIBUTES *attributes, KPROCESSOR_MODE mode)
{
    OBJECT_HEADER *h = OBJECT_TO_HEADER(object);
    SD_BLOB *sd;
    NTSTATUS status = ObComputeSecurity(h->Type, attributes, nullptr, mode, &sd);
    if (!NT_SUCCESS(status))
        return status;
    status = ObStoreSecurity(object, sd);
    SeSdDereference(sd);
    return status;
}

/* ---------------------------------------------------------------- checks on open */

static void trace_open_denial(PVOID object, ACCESS_MASK desired, NTSTATUS status)
{
    OBJECT_HEADER *h = OBJECT_TO_HEADER(object);
    char what[24];
    ULONG n = 0;
    if (h->Type == SeTokenObjectType)
        KiLogPrintf("  the token is %llx", ((PTOKEN)object)->TokenId);
    for (; n < sizeof(what) - 1 && n < h->Type->Name.Length / sizeof(WCHAR); n++)
        what[n] = (char)h->Type->Name.Buffer[n];
    what[n] = 0;
    SeTraceDenial(what, &h->Name, desired, status);
}

/*
 * kernel mode callers, types that keep no descriptor and objects without one are not checked, and then
 * *granted is the access asked for. the file system and the registry check their own during the open.
 */
NTSTATUS ObCheckOpenAccessEx(PVOID object, ACCESS_MASK desired, KPROCESSOR_MODE mode, ULONG flags, ACCESS_MASK *granted)
{
    const OBJECT_TYPE *type = OBJECT_TO_HEADER(object)->Type;
    *granted = desired;
    if (mode != UserMode || !SeAccessEnforced || !type->Init.GenericMapping[3] || type->Init.GetSecurity)
        return STATUS_SUCCESS;
    SD_BLOB *sd;
    ObReferenceSecurity(object, &sd);
    if (!sd)
        return STATUS_SUCCESS;
    NTSTATUS status = SeCheckAccess(sd, type->Init.GenericMapping, desired, 0, flags, granted);
    if (!NT_SUCCESS(status)) {
        *granted = 0;
        trace_open_denial(object, desired, status);
        SeTraceDescriptor(sd);
    }
    SeSdDereference(sd);
    return status;
}

NTSTATUS ObCheckOpenAccess(PVOID object, ACCESS_MASK desired, KPROCESSOR_MODE mode, ACCESS_MASK *granted)
{
    return ObCheckOpenAccessEx(object, desired, mode, 0, granted);
}

/* creating a named object needs create access on the directory. */
#define DIRECTORY_CREATE_OBJECT_ 0x4

NTSTATUS ObCheckCreateAccess(PVOID directory, KPROCESSOR_MODE mode)
{
    ACCESS_MASK granted;
    if (mode != UserMode || !SeAccessEnforced)
        return STATUS_SUCCESS;
    SD_BLOB *sd;
    ObReferenceSecurity(directory, &sd);
    if (!sd)
        return STATUS_SUCCESS;
    NTSTATUS status = SeCheckAccess(sd, ObDirectoryType->Init.GenericMapping, DIRECTORY_CREATE_OBJECT_, 0, 0, &granted);
    SeSdDereference(sd);
    if (!NT_SUCCESS(status))
        trace_open_denial(directory, DIRECTORY_CREATE_OBJECT_, status);
    return status;
}

/* ---------------------------------------------------------------- services */

static BOOLEAN pseudo(HANDLE handle)
{
    return handle == NtCurrentProcess() || handle == NtCurrentThread();
}

/* user mode callers need the rights on the handle, and the security privilege for ACCESS_SYSTEM_SECURITY. */
static NTSTATUS check_rights(HANDLE handle, ACCESS_MASK granted, ACCESS_MASK needed)
{
    if (KeGetPreviousMode() != UserMode)
        return STATUS_SUCCESS;
    if (!pseudo(handle) && (granted & needed) != needed)
        return STATUS_ACCESS_DENIED;
    if ((needed & ACCESS_SYSTEM_SECURITY) && !SepSinglePrivilegeCheck(SE_SECURITY_PRIVILEGE, UserMode))
        return STATUS_PRIVILEGE_NOT_HELD;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtSetSecurityObject(HANDLE handle, ULONG info, PVOID descriptor)
{
    KPROCESSOR_MODE mode = KeGetPreviousMode();
    if (!descriptor)
        return STATUS_ACCESS_VIOLATION;
    PVOID object;
    ACCESS_MASK granted;
    NTSTATUS status = ObReferenceHandle(handle, nullptr, mode, &object, &granted);
    if (!NT_SUCCESS(status))
        return status;
    status = check_rights(handle, granted, SeSdWriteRights(info));
    SD_BLOB *modification = nullptr;
    if (NT_SUCCESS(status))
        status = SeSdCapture(descriptor, mode, &modification);
    if (NT_SUCCESS(status)) {
        const OBJECT_TYPE *type = OBJECT_TO_HEADER(object)->Type;
        /* serializes the read, modify and store of the descriptor. */
        static FAST_MUTEX set_lock;
        static BOOLEAN set_ready;
        if (!set_ready) {
            ExInitializeFastMutex(&set_lock);
            set_ready = TRUE;
        }
        ExAcquireFastMutex(&set_lock);
        SD_BLOB *old = nullptr;
        status = ObReferenceSecurity(object, &old);
        if (NT_SUCCESS(status) && !old)
            status = SeSdSystemDefault(mapping_of(type), type == ObDirectoryType, &old);
        SD_BLOB *next = nullptr;
        if (NT_SUCCESS(status))
            status = SeSdSet(info, modification, old, mapping_of(type), mode, &next);
        if (old)
            SeSdDereference(old);
        if (NT_SUCCESS(status)) {
            status = ObStoreSecurity(object, next);
            if (SeTraceAccess) {
                KiLogPrintf("security set on a %ws %llx, status %08x", type->Name.Buffer,
                            type == SeTokenObjectType ? ((PTOKEN)object)->TokenId : 0, status);
                SeTraceDescriptor(next);
            }
            SeSdDereference(next);
        }
        ExReleaseFastMutex(&set_lock);
    }
    if (modification)
        SeSdDereference(modification);
    ObDereferenceObject(object);
    return status;
}

NTSTATUS NTAPI NtQuerySecurityObject(HANDLE handle, ULONG info, PVOID descriptor, ULONG length, PULONG returned)
{
    KPROCESSOR_MODE mode = KeGetPreviousMode();
    PVOID object;
    ACCESS_MASK granted;
    NTSTATUS status = ObReferenceHandle(handle, nullptr, mode, &object, &granted);
    if (!NT_SUCCESS(status))
        return status;
    status = check_rights(handle, granted, SeSdReadRights(info));
    SD_BLOB *sd = nullptr;
    if (NT_SUCCESS(status))
        status = ObReferenceSecurity(object, &sd);
    if (NT_SUCCESS(status) && !sd && securable(OBJECT_TO_HEADER(object)->Type)) {
        /* created before a token existed, or never given one. */
        const OBJECT_TYPE *type = OBJECT_TO_HEADER(object)->Type;
        status = SeSdSystemDefault(mapping_of(type), type == ObDirectoryType, &sd);
    }
    ULONG needed = 0;
    if (NT_SUCCESS(status)) {
        status = SeSdQuery(info, sd, nullptr, &needed);
        if (status == STATUS_BUFFER_TOO_SMALL && needed <= length) {
            PUCHAR out = ExAllocatePool2(POOL_FLAG_NON_PAGED, needed, KI_TAG('O', 'b', 'S', 'q'));
            if (!out) {
                status = STATUS_INSUFFICIENT_RESOURCES;
            } else {
                ULONG room = needed;
                status = SeSdQuery(info, sd, out, &room);
                if (NT_SUCCESS(status))
                    status = mode == UserMode ? KiCopyToUser(descriptor, out, needed)
                                              : (memcpy(descriptor, out, needed), STATUS_SUCCESS);
                ExFreePool(out);
            }
        }
    }
    if (sd)
        SeSdDereference(sd);
    ObDereferenceObject(object);
    if (returned && (NT_SUCCESS(status) || status == STATUS_BUFFER_TOO_SMALL)) {
        if (mode == UserMode)
            KiCopyToUser(returned, &needed, sizeof(needed));
        else
            *returned = needed;
    }
    return status;
}

NTSTATUS NTAPI ZwQuerySecurityObject(HANDLE handle, ULONG info, PVOID descriptor, ULONG length, PULONG returned)
{
    PKTHREAD t = KiCurrentThread();
    KPROCESSOR_MODE old = t->PreviousMode;
    t->PreviousMode = KernelMode;
    NTSTATUS status = NtQuerySecurityObject(handle, info, descriptor, length, returned);
    t->PreviousMode = old;
    return status;
}

NTSTATUS NTAPI ZwSetSecurityObject(HANDLE handle, ULONG info, PVOID descriptor)
{
    PKTHREAD t = KiCurrentThread();
    KPROCESSOR_MODE old = t->PreviousMode;
    t->PreviousMode = KernelMode;
    NTSTATUS status = NtSetSecurityObject(handle, info, descriptor);
    t->PreviousMode = old;
    return status;
}
