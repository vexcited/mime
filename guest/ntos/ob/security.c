/*
 * the security descriptors of objects. An object has one once it is inserted: the creator's, with what its
 * directory hands down, completed from the token of the thread that made it. The types whose objects keep
 * theirs elsewhere (files with their file system, keys with the registry) say so with a pair of procedures.
 * The descriptor is a shared blob behind the reserved word of the object header; a type without generic
 * rights has none, and an object nobody gave one to answers with the system's.
 *
 * Access is never checked against a descriptor. The handle's rights are, as are the privileges and the owner
 * and label rules of setting one (se/sd.c).
 */
#include "../ntos.h"
#include "../se/se.h"
#include <mime/ntrtlsec.h>

#define OB_SD_PENDING OB_FLAG_SD_PENDING
#define STATUS_INVALID_SECURITY_DESCR ((NTSTATUS)0xC0000079L)

/* the rights of an object whose type says nothing about them: the standard ones and all the specific ones. */
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

/* an object's descriptor goes with it. */
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

/* whether a descriptor hands anything down to what is made in its container. */
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

NTSTATUS ObAssignSecurity(PVOID object, const OBJECT_ATTRIBUTES *attributes, PVOID parent, KPROCESSOR_MODE mode)
{
    OBJECT_HEADER *h = OBJECT_TO_HEADER(object);
    const OBJECT_TYPE *type = h->Type;
    if (!(h->Flags & OB_SD_PENDING))
        return STATUS_SUCCESS;
    h->Flags &= ~OB_SD_PENDING;
    /* a file system or the registry makes the descriptors of its own, and a type with no rights has none. */
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
    /* before the first token exists, at boot, objects are made without. */
    if (status == STATUS_NO_TOKEN)
        return STATUS_SUCCESS;
    if (!NT_SUCCESS(status))
        return status;
    h->Reserved = (ULONG_PTR)sd;
    return STATUS_SUCCESS;
}

/* ---------------------------------------------------------------- the services */

static BOOLEAN pseudo(HANDLE handle)
{
    return handle == NtCurrentProcess() || handle == NtCurrentThread();
}

/* the rights a user mode caller must have on the handle, the privilege for the system access right. */
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
        /* one change at a time, as the descriptor is read, changed and stored again. */
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
        /* made before a token existed, or never given one. */
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
