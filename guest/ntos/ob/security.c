/*
 * security descriptors kept for objects. an object has none until a program sets one, and then
 * NtQuerySecurityObject gives it back, the parts asked for. access is not checked against it, only the rights
 * of the handle are. the descriptor is stored self relative, owner, group, sacl and dacl in that order, behind
 * the reserved word of the object header; an object that has none answers STATUS_NOT_IMPLEMENTED, which is how
 * advapi32 knows to make up the descriptor a new object of the user's would get.
 */
#include "../ntos.h"

#define OWNER_SECURITY_INFORMATION 0x1
#define GROUP_SECURITY_INFORMATION 0x2
#define DACL_SECURITY_INFORMATION 0x4
#define SACL_SECURITY_INFORMATION 0x8
#define LABEL_SECURITY_INFORMATION 0x10
#define PROTECTED_DACL_SECURITY_INFORMATION 0x80000000u
#define UNPROTECTED_DACL_SECURITY_INFORMATION 0x20000000u
#define PROTECTED_SACL_SECURITY_INFORMATION 0x40000000u
#define UNPROTECTED_SACL_SECURITY_INFORMATION 0x10000000u

#define SE_DACL_PRESENT 0x0004
#define SE_DACL_DEFAULTED 0x0008
#define SE_SACL_PRESENT 0x0010
#define SE_SACL_DEFAULTED 0x0020
#define SE_DACL_PROTECTED 0x1000
#define SE_SACL_PROTECTED 0x2000
#define SE_SELF_RELATIVE 0x8000
#define SE_OWNER_DEFAULTED 0x0001
#define SE_GROUP_DEFAULTED 0x0002

#define STATUS_INVALID_SECURITY_DESCR ((NTSTATUS)0xC0000079L)
#define STATUS_BAD_DESCRIPTOR_FORMAT ((NTSTATUS)0xC00000E7L)
#define WRITE_DAC_RIGHT 0x00040000
#define WRITE_OWNER_RIGHT 0x00080000
#define READ_CONTROL_RIGHT 0x00020000
#define ACCESS_SYSTEM_SECURITY_RIGHT 0x01000000
#define OB_SD_MAX 0x10000

/* the parts of a descriptor, as pointers into a copy. */
typedef struct OB_SD_PARTS {
    USHORT Control;
    const UCHAR *Owner, *Group, *Sacl, *Dacl;
} OB_SD_PARTS;

typedef struct OB_SD {
    ULONG Length;
    UCHAR Data[1];
} OB_SD;

static FAST_MUTEX g_lock;

void ObpInitializeSecurity(void)
{
    ExInitializeFastMutex(&g_lock);
}

static void lock(void)
{
    ExAcquireFastMutex(&g_lock);
}

static void unlock(void)
{
    ExReleaseFastMutex(&g_lock);
}

/* the length of a sid, or 0 for one that cannot be. */
static ULONG sid_length(const UCHAR *sid, SIZE_T available)
{
    if (available < 8 || sid[0] != 1 || sid[1] > 15)
        return 0;
    ULONG length = 8 + 4 * (ULONG)sid[1];
    return length <= available ? length : 0;
}

/* the length of an acl, or 0 for one whose header does not fit. */
static ULONG acl_length(const UCHAR *acl, SIZE_T available)
{
    if (available < 8)
        return 0;
    ULONG length = *(const USHORT *)(acl + 2);
    return length >= 8 && length <= available && !(length & 3) ? length : 0;
}

/* a descriptor of the caller, absolute or self relative, taken apart; the parts point into the copy. */
static NTSTATUS parse_descriptor(const UCHAR *sd, SIZE_T length, OB_SD_PARTS *p)
{
    if (length < 20 || sd[0] != 1)
        return STATUS_INVALID_SECURITY_DESCR;
    p->Control = *(const USHORT *)(sd + 2);
    p->Owner = p->Group = p->Sacl = p->Dacl = nullptr;
    if (p->Control & SE_SELF_RELATIVE) {
        ULONG owner = *(const ULONG *)(sd + 4), group = *(const ULONG *)(sd + 8);
        ULONG sacl = *(const ULONG *)(sd + 12), dacl = *(const ULONG *)(sd + 16);
        if (owner) {
            if (owner >= length || !sid_length(sd + owner, length - owner))
                return STATUS_INVALID_SECURITY_DESCR;
            p->Owner = sd + owner;
        }
        if (group) {
            if (group >= length || !sid_length(sd + group, length - group))
                return STATUS_INVALID_SECURITY_DESCR;
            p->Group = sd + group;
        }
        if (sacl && (p->Control & SE_SACL_PRESENT)) {
            if (sacl >= length || !acl_length(sd + sacl, length - sacl))
                return STATUS_INVALID_SECURITY_DESCR;
            p->Sacl = sd + sacl;
        }
        if (dacl && (p->Control & SE_DACL_PRESENT)) {
            if (dacl >= length || !acl_length(sd + dacl, length - dacl))
                return STATUS_INVALID_SECURITY_DESCR;
            p->Dacl = sd + dacl;
        }
    } else {
        /* pointers into the caller's memory, which the capture below copies. */
        return STATUS_BAD_DESCRIPTOR_FORMAT;
    }
    return STATUS_SUCCESS;
}

/* the self relative form of an absolute descriptor of the caller, in memory of the kernel. */
static NTSTATUS capture_descriptor(const void *user, UCHAR **out, SIZE_T *size)
{
    UCHAR head[40];
    *out = nullptr;
    *size = 0;
    NTSTATUS status = KiCopyFromUser(head, user, 20);
    if (!NT_SUCCESS(status))
        return status;
    if (head[0] != 1)
        return STATUS_INVALID_SECURITY_DESCR;
    USHORT control = *(USHORT *)(head + 2);
    if (control & SE_SELF_RELATIVE) {
        /* its length is where its last part ends. */
        ULONG end = 20;
        const ULONG *offsets = (const ULONG *)(head + 4);
        for (int i = 0; i < 4; i++) {
            if (!offsets[i])
                continue;
            UCHAR first[8];
            status = KiCopyFromUser(first, (const UCHAR *)user + offsets[i], 8);
            if (!NT_SUCCESS(status))
                return STATUS_INVALID_SECURITY_DESCR;
            ULONG part;
            if (i < 2) {
                if (first[0] != 1 || first[1] > 15)
                    return STATUS_INVALID_SECURITY_DESCR;
                part = 8 + 4 * (ULONG)first[1];
            } else {
                part = *(USHORT *)(first + 2);
            }
            if (offsets[i] + part > end)
                end = offsets[i] + part;
        }
        if (end > OB_SD_MAX)
            return STATUS_INVALID_SECURITY_DESCR;
        UCHAR *copy = ExAllocatePool2(POOL_FLAG_PAGED, end, KI_TAG('O', 'b', 'S', 'd'));
        if (!copy)
            return STATUS_INSUFFICIENT_RESOURCES;
        status = KiCopyFromUser(copy, user, end);
        if (!NT_SUCCESS(status)) {
            ExFreePool(copy);
            return status;
        }
        *out = copy;
        *size = end;
        return STATUS_SUCCESS;
    }
    /* absolute: the owner, group, sacl and dacl are pointers after the header. */
    status = KiCopyFromUser(head, user, sizeof(head));
    if (!NT_SUCCESS(status))
        return status;
    const UCHAR *pointers[4] = {(const UCHAR *)*(ULONG_PTR *)(head + 8), (const UCHAR *)*(ULONG_PTR *)(head + 16),
                                (const UCHAR *)*(ULONG_PTR *)(head + 24), (const UCHAR *)*(ULONG_PTR *)(head + 32)};
    UCHAR *copy = ExAllocatePool2(POOL_FLAG_PAGED, OB_SD_MAX, KI_TAG('O', 'b', 'S', 'd'));
    if (!copy)
        return STATUS_INSUFFICIENT_RESOURCES;
    memset(copy, 0, 20);
    copy[0] = 1;
    *(USHORT *)(copy + 2) = control | SE_SELF_RELATIVE;
    ULONG at = 20;
    for (int i = 0; i < 4 && NT_SUCCESS(status); i++) {
        if (!pointers[i])
            continue;
        UCHAR first[8];
        status = KiCopyFromUser(first, pointers[i], 8);
        if (!NT_SUCCESS(status)) {
            status = STATUS_INVALID_SECURITY_DESCR;
            break;
        }
        ULONG part;
        if (i < 2) {
            part = first[0] == 1 && first[1] <= 15 ? 8 + 4 * (ULONG)first[1] : 0;
        } else {
            part = *(USHORT *)(first + 2);
            if (part < 8)
                part = 0;
        }
        if (!part || at + part > OB_SD_MAX) {
            status = STATUS_INVALID_SECURITY_DESCR;
            break;
        }
        status = KiCopyFromUser(copy + at, pointers[i], part);
        /* the offsets sit in the order owner, group, sacl, dacl. */
        *(ULONG *)(copy + 4 + 4 * i) = at;
        at = (at + part + 3) & ~3u;
    }
    if (!NT_SUCCESS(status)) {
        ExFreePool(copy);
        return status;
    }
    *out = copy;
    *size = at;
    return STATUS_SUCCESS;
}

/* the part of a stored descriptor that comes before or after, rebuilt from the parts that stay and the new ones. */
static NTSTATUS build(const OB_SD_PARTS *old, const OB_SD_PARTS *added, ULONG info, OB_SD **out)
{
    const UCHAR *owner = (info & OWNER_SECURITY_INFORMATION) ? added->Owner : old->Owner;
    const UCHAR *group = (info & GROUP_SECURITY_INFORMATION) ? added->Group : old->Group;
    BOOLEAN dacl_new = (info & DACL_SECURITY_INFORMATION) != 0, sacl_new = (info & (SACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION)) != 0;
    const UCHAR *dacl = dacl_new ? added->Dacl : old->Dacl;
    const UCHAR *sacl = sacl_new ? added->Sacl : old->Sacl;
    USHORT control = SE_SELF_RELATIVE;
    USHORT source = dacl_new ? added->Control : old->Control;
    control |= source & (SE_DACL_PRESENT | SE_DACL_DEFAULTED | SE_DACL_PROTECTED);
    source = sacl_new ? added->Control : old->Control;
    control |= source & (SE_SACL_PRESENT | SE_SACL_DEFAULTED | SE_SACL_PROTECTED);
    if (dacl_new && (info & PROTECTED_DACL_SECURITY_INFORMATION))
        control |= SE_DACL_PROTECTED;
    if (dacl_new && (info & UNPROTECTED_DACL_SECURITY_INFORMATION))
        control &= ~SE_DACL_PROTECTED;
    ULONG o = owner ? sid_length(owner, 8 + 4 * 15) : 0, g = group ? sid_length(group, 8 + 4 * 15) : 0;
    ULONG s = sacl ? acl_length(sacl, 0xFFFF) : 0, d = dacl ? acl_length(dacl, 0xFFFF) : 0;
    ULONG total = 20 + ((o + 3) & ~3u) + ((g + 3) & ~3u) + ((s + 3) & ~3u) + ((d + 3) & ~3u);
    OB_SD *sd = ExAllocatePool2(POOL_FLAG_PAGED, sizeof(ULONG) + total, KI_TAG('O', 'b', 'S', 'd'));
    if (!sd)
        return STATUS_INSUFFICIENT_RESOURCES;
    sd->Length = total;
    UCHAR *base = sd->Data;
    base[0] = 1;
    *(USHORT *)(base + 2) = control;
    ULONG at = 20;
    const UCHAR *parts[4] = {owner, group, sacl, dacl};
    ULONG lengths[4] = {o, g, s, d};
    for (int i = 0; i < 4; i++) {
        if (!lengths[i])
            continue;
        *(ULONG *)(base + 4 + 4 * i) = at;
        memcpy(base + at, parts[i], lengths[i]);
        at += (lengths[i] + 3) & ~3u;
    }
    *out = sd;
    return STATUS_SUCCESS;
}

static OB_SD *stored(PVOID object)
{
    return (OB_SD *)(ULONG_PTR)OBJECT_TO_HEADER(object)->Reserved;
}

/* the rights a handle needs to change or read the parts. */
static ACCESS_MASK rights_to_change(ULONG info)
{
    ACCESS_MASK rights = 0;
    if (info & (OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION))
        rights |= WRITE_OWNER_RIGHT;
    if (info & (DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION))
        rights |= WRITE_DAC_RIGHT;
    if (info & (SACL_SECURITY_INFORMATION | PROTECTED_SACL_SECURITY_INFORMATION | UNPROTECTED_SACL_SECURITY_INFORMATION))
        rights |= ACCESS_SYSTEM_SECURITY_RIGHT;
    return rights;
}

static ACCESS_MASK rights_to_read(ULONG info)
{
    ACCESS_MASK rights = 0;
    if (info & (OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION))
        rights |= READ_CONTROL_RIGHT;
    if (info & SACL_SECURITY_INFORMATION)
        rights |= ACCESS_SYSTEM_SECURITY_RIGHT;
    return rights;
}

NTSTATUS NTAPI NtSetSecurityObject(HANDLE handle, ULONG info, PVOID descriptor)
{
    if (!info || !descriptor)
        return STATUS_INVALID_PARAMETER;
    PVOID object;
    ACCESS_MASK granted;
    NTSTATUS status = ObReferenceHandle(handle, nullptr, KeGetPreviousMode(), &object, &granted);
    if (!NT_SUCCESS(status))
        return status;
    ACCESS_MASK needed = rights_to_change(info);
    if (KeGetPreviousMode() == UserMode && handle != NtCurrentProcess() && handle != NtCurrentThread() &&
        (granted & needed) != needed) {
        ObDereferenceObject(object);
        return STATUS_ACCESS_DENIED;
    }
    UCHAR *copy = nullptr;
    SIZE_T size = 0;
    status = capture_descriptor(descriptor, &copy, &size);
    OB_SD_PARTS added = {};
    if (NT_SUCCESS(status))
        status = parse_descriptor(copy, size, &added);
    if (NT_SUCCESS(status)) {
        /* the parts asked for must be there, a missing owner or group is no owner or group. */
        if (((info & OWNER_SECURITY_INFORMATION) && !added.Owner) || ((info & GROUP_SECURITY_INFORMATION) && !added.Group))
            status = STATUS_INVALID_SECURITY_DESCR;
    }
    if (NT_SUCCESS(status)) {
        lock();
        OB_SD *old = stored(object);
        OB_SD_PARTS before = {};
        if (old)
            parse_descriptor(old->Data, old->Length, &before);
        OB_SD *next;
        status = build(&before, &added, info, &next);
        if (NT_SUCCESS(status)) {
            OBJECT_TO_HEADER(object)->Reserved = (ULONG_PTR)next;
            if (old)
                ExFreePool(old);
        }
        unlock();
    }
    if (copy)
        ExFreePool(copy);
    ObDereferenceObject(object);
    return status;
}

NTSTATUS NTAPI NtQuerySecurityObject(HANDLE handle, ULONG info, PVOID descriptor, ULONG length, PULONG returned)
{
    PVOID object;
    ACCESS_MASK granted;
    NTSTATUS status = ObReferenceHandle(handle, nullptr, KeGetPreviousMode(), &object, &granted);
    if (!NT_SUCCESS(status))
        return status;
    ACCESS_MASK needed = rights_to_read(info);
    if (KeGetPreviousMode() == UserMode && handle != NtCurrentProcess() && handle != NtCurrentThread() &&
        (granted & needed) != needed) {
        ObDereferenceObject(object);
        return STATUS_ACCESS_DENIED;
    }
    lock();
    OB_SD *sd = stored(object);
    if (!sd) {
        unlock();
        ObDereferenceObject(object);
        return STATUS_NOT_IMPLEMENTED;
    }
    OB_SD_PARTS p = {};
    parse_descriptor(sd->Data, sd->Length, &p);
    const UCHAR *owner = (info & OWNER_SECURITY_INFORMATION) ? p.Owner : nullptr;
    const UCHAR *group = (info & GROUP_SECURITY_INFORMATION) ? p.Group : nullptr;
    const UCHAR *dacl = (info & DACL_SECURITY_INFORMATION) ? p.Dacl : nullptr;
    const UCHAR *sacl = (info & (SACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION)) ? p.Sacl : nullptr;
    USHORT control = SE_SELF_RELATIVE;
    if (info & DACL_SECURITY_INFORMATION)
        control |= p.Control & (SE_DACL_PRESENT | SE_DACL_DEFAULTED | SE_DACL_PROTECTED);
    if (info & (SACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION))
        control |= p.Control & (SE_SACL_PRESENT | SE_SACL_DEFAULTED | SE_SACL_PROTECTED);
    ULONG o = owner ? sid_length(owner, 8 + 4 * 15) : 0, g = group ? sid_length(group, 8 + 4 * 15) : 0;
    ULONG s = sacl ? acl_length(sacl, 0xFFFF) : 0, d = dacl ? acl_length(dacl, 0xFFFF) : 0;
    ULONG total = 20 + ((o + 3) & ~3u) + ((g + 3) & ~3u) + ((s + 3) & ~3u) + ((d + 3) & ~3u);
    UCHAR *out = nullptr;
    if (total <= length) {
        out = ExAllocatePool2(POOL_FLAG_PAGED, total, KI_TAG('O', 'b', 'S', 'q'));
        if (!out) {
            unlock();
            ObDereferenceObject(object);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        out[0] = 1;
        *(USHORT *)(out + 2) = control;
        ULONG at = 20;
        const UCHAR *parts[4] = {owner, group, sacl, dacl};
        ULONG lengths[4] = {o, g, s, d};
        for (int i = 0; i < 4; i++) {
            if (!lengths[i])
                continue;
            *(ULONG *)(out + 4 + 4 * i) = at;
            memcpy(out + at, parts[i], lengths[i]);
            at += (lengths[i] + 3) & ~3u;
        }
    }
    unlock();
    ObDereferenceObject(object);
    if (returned)
        KiCopyToUser(returned, &total, sizeof(total));
    if (!out)
        return STATUS_BUFFER_TOO_SMALL;
    status = KiCopyToUser(descriptor, out, total);
    ExFreePool(out);
    return status;
}

/* an object's descriptor goes with it. */
void ObpFreeSecurity(PVOID header)
{
    OBJECT_HEADER *h = header;
    if (h->Reserved) {
        ExFreePool((PVOID)(ULONG_PTR)h->Reserved);
        h->Reserved = 0;
    }
}
