/*
 * security descriptors of objects: the one a new object gets from its creator, its parent and the token of the
 * thread that makes it, the parts of it a program reads and changes, and the checks windows makes on the way.
 * Access is not checked against any descriptor, only what the owner, privilege and label rules say about
 * setting one.
 *
 * A descriptor is a blob: self relative, shared by reference count and never changed once made. The algorithms
 * on lists and parts are the rtl's, shared with ntdll.
 */
#include "se.h"
#include <mime/ntrtlsec.h>

#define SD_CACHE_ENTRIES 32

#define STATUS_INVALID_SECURITY_DESCR ((NTSTATUS)0xC0000079L)

const UCHAR SeSidSystem[12] = {1, 1, 0, 0, 0, 0, 0, 5, 18, 0, 0, 0};
const UCHAR SeSidAdministrators[16] = {1, 2, 0, 0, 0, 0, 0, 5, 32, 0, 0, 0, 0x20, 0x02, 0, 0};
const UCHAR SeSidUsers[16] = {1, 2, 0, 0, 0, 0, 0, 5, 32, 0, 0, 0, 0x21, 0x02, 0, 0};
const UCHAR SeSidEveryone[12] = {1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0};
const UCHAR SeSidAuthenticatedUsers[12] = {1, 1, 0, 0, 0, 0, 0, 5, 11, 0, 0, 0};
const UCHAR SeSidCreatorOwner[12] = {1, 1, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0};
const UCHAR SeSidTrustedInstaller[32] = {1, 6, 0, 0, 0, 0, 0, 5, 80, 0, 0, 0, 0x35, 0x74, 0x8F, 0x38, 0x99, 0xEE,
                                         0x4F, 0xCB, 0xDC, 0xA5, 0x1C, 0x6D, 0x57, 0xA6, 0x8F, 0x6E, 0x40, 0x28,
                                         0x74, 0x87};
const UCHAR SeSidAllAppPackages[16] = {1, 2, 0, 0, 0, 0, 0, 15, 2, 0, 0, 0, 1, 0, 0, 0};
const UCHAR SeSidRestrictedPackages[16] = {1, 2, 0, 0, 0, 0, 0, 15, 2, 0, 0, 0, 2, 0, 0, 0};

/* ---------------------------------------------------------------- blobs */

SD_BLOB *SeSdAllocate(ULONG length)
{
    SD_BLOB *sd = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(SD_BLOB) + length, KI_TAG('S', 'e', 'S', 'd'));
    if (sd) {
        sd->Refs = 1;
        sd->Length = length;
    }
    return sd;
}

SD_BLOB *SeSdReference(SD_BLOB *sd)
{
    __atomic_add_fetch(&sd->Refs, 1, __ATOMIC_RELAXED);
    return sd;
}

void SeSdDereference(SD_BLOB *sd)
{
    if (sd && __atomic_sub_fetch(&sd->Refs, 1, __ATOMIC_ACQ_REL) == 0)
        ExFreePool(sd);
}

/* a blob of a descriptor the rtl made in its own memory, which is freed. */
static NTSTATUS blob_of_rtl(PSECURITY_DESCRIPTOR made, SD_BLOB **out)
{
    ULONG length = RtlLengthSecurityDescriptor(made);
    SD_BLOB *sd = SeSdAllocate(length);
    if (!sd) {
        MimeRtlFree(made);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    memcpy(sd->Data, made, length);
    MimeRtlFree(made);
    *out = sd;
    return STATUS_SUCCESS;
}

NTSTATUS SeSdFromBytes(const void *descriptor, ULONG length, SD_BLOB **out)
{
    if (length < 20 || length > SD_MAX_LENGTH)
        return STATUS_INVALID_SECURITY_DESCR;
    SD_BLOB *sd = SeSdAllocate(length);
    if (!sd)
        return STATUS_INSUFFICIENT_RESOURCES;
    memcpy(sd->Data, descriptor, length);
    if (!(((SECURITY_DESCRIPTOR_RELATIVE *)sd->Data)->Control & SE_SELF_RELATIVE) ||
        !RtlValidRelativeSecurityDescriptor(sd->Data, length, 0)) {
        SeSdDereference(sd);
        return STATUS_INVALID_SECURITY_DESCR;
    }
    *out = sd;
    return STATUS_SUCCESS;
}

/* ---------------------------------------------------------------- capture */

static NTSTATUS copy_in(KPROCESSOR_MODE mode, void *to, const void *from, SIZE_T length)
{
    if (mode == UserMode)
        return KiCopyFromUser(to, from, length);
    memcpy(to, from, length);
    return STATUS_SUCCESS;
}

/* the size of a sid or an acl at an address of the caller, 0 when it cannot be one. */
static ULONG part_length(KPROCESSOR_MODE mode, const void *address, BOOLEAN sid)
{
    UCHAR first[8];
    if (!NT_SUCCESS(copy_in(mode, first, address, sizeof(first))))
        return 0;
    if (sid)
        return first[0] == 1 && first[1] <= 15 ? 8 + 4 * (ULONG)first[1] : 0;
    ULONG size = *(USHORT *)(first + 2);
    return size >= 8 && !(size & 3) ? size : 0;
}

NTSTATUS SeSdCapture(const void *descriptor, KPROCESSOR_MODE mode, SD_BLOB **out)
{
    UCHAR head[40];
    *out = nullptr;
    NTSTATUS status = copy_in(mode, head, descriptor, 20);
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
            if ((i == 2 && !(control & SE_SACL_PRESENT)) || (i == 3 && !(control & SE_DACL_PRESENT)))
                continue;
            ULONG part = part_length(mode, (const UCHAR *)descriptor + offsets[i], i < 2);
            if (!part || offsets[i] > SD_MAX_LENGTH || offsets[i] + part > SD_MAX_LENGTH)
                return STATUS_INVALID_SECURITY_DESCR;
            if (offsets[i] + part > end)
                end = offsets[i] + part;
        }
        SD_BLOB *sd = SeSdAllocate(end);
        if (!sd)
            return STATUS_INSUFFICIENT_RESOURCES;
        status = copy_in(mode, sd->Data, descriptor, end);
        if (NT_SUCCESS(status) && !RtlValidRelativeSecurityDescriptor(sd->Data, end, 0))
            status = STATUS_INVALID_SECURITY_DESCR;
        if (!NT_SUCCESS(status)) {
            SeSdDereference(sd);
            return status;
        }
        *out = sd;
        return STATUS_SUCCESS;
    }
    /*
     * absolute: the owner, group, sacl and dacl are pointers after the header. A 32-bit program's descriptor
     * keeps them where the offsets of a relative one are, as 32-bit addresses.
     */
    BOOLEAN narrow = mode == UserMode && PspIsWow64Process(PsCurrentProcess());
    status = copy_in(mode, head, descriptor, narrow ? 20 : sizeof(head));
    if (!NT_SUCCESS(status))
        return status;
    const UCHAR *pointers[4];
    for (int i = 0; i < 4; i++)
        pointers[i] = narrow ? (const UCHAR *)(ULONG_PTR) * (ULONG *)(head + 4 + 4 * i)
                             : (const UCHAR *)*(ULONG_PTR *)(head + 8 + 8 * i);
    ULONG lengths[4] = {};
    ULONG total = 20;
    for (int i = 0; i < 4; i++) {
        if (!pointers[i] || (i == 2 && !(control & SE_SACL_PRESENT)) || (i == 3 && !(control & SE_DACL_PRESENT)))
            continue;
        lengths[i] = part_length(mode, pointers[i], i < 2);
        if (!lengths[i])
            return STATUS_INVALID_SECURITY_DESCR;
        total += (lengths[i] + 3) & ~3u;
        if (total > SD_MAX_LENGTH)
            return STATUS_INVALID_SECURITY_DESCR;
    }
    SD_BLOB *sd = SeSdAllocate(total);
    if (!sd)
        return STATUS_INSUFFICIENT_RESOURCES;
    memset(sd->Data, 0, total);
    sd->Data[0] = 1;
    *(USHORT *)(sd->Data + 2) = (control & ~SE_SELF_RELATIVE) | SE_SELF_RELATIVE;
    sd->Data[1] = head[1];
    ULONG at = 20;
    for (int i = 0; i < 4 && NT_SUCCESS(status); i++) {
        if (!lengths[i])
            continue;
        status = copy_in(mode, sd->Data + at, pointers[i], lengths[i]);
        /* the offsets sit in the order owner, group, sacl, dacl. */
        *(ULONG *)(sd->Data + 4 + 4 * i) = at;
        at += (lengths[i] + 3) & ~3u;
    }
    if (NT_SUCCESS(status) && !RtlValidRelativeSecurityDescriptor(sd->Data, total, 0))
        status = STATUS_INVALID_SECURITY_DESCR;
    if (!NT_SUCCESS(status)) {
        SeSdDereference(sd);
        return status;
    }
    *out = sd;
    return STATUS_SUCCESS;
}

/* ---------------------------------------------------------------- building */

NTSTATUS SeSdBuild(const UCHAR *owner, const UCHAR *group, USHORT control, const SD_ACE *dacl, ULONG dacl_count,
                   SD_BLOB **out)
{
    ULONG aces = 0;
    for (ULONG i = 0; i < dacl_count; i++)
        aces += 8 + 4 * (ULONG)dacl[i].Sid[1] + 8;
    ULONG acl_size = (8 + aces + 3) & ~3u;
    ULONG owner_size = owner ? (8 + 4 * (ULONG)owner[1] + 3) & ~3u : 0;
    ULONG group_size = group ? (8 + 4 * (ULONG)group[1] + 3) & ~3u : 0;
    ULONG total = 20 + owner_size + group_size + (dacl ? acl_size : 0);
    SD_BLOB *sd = SeSdAllocate(total);
    if (!sd)
        return STATUS_INSUFFICIENT_RESOURCES;
    memset(sd->Data, 0, total);
    sd->Data[0] = 1;
    *(USHORT *)(sd->Data + 2) = control | SE_SELF_RELATIVE | (dacl ? SE_DACL_PRESENT : 0);
    ULONG at = 20;
    if (owner) {
        memcpy(sd->Data + at, owner, 8 + 4 * (ULONG)owner[1]);
        *(ULONG *)(sd->Data + 4) = at;
        at += owner_size;
    }
    if (group) {
        memcpy(sd->Data + at, group, 8 + 4 * (ULONG)group[1]);
        *(ULONG *)(sd->Data + 8) = at;
        at += group_size;
    }
    if (dacl) {
        UCHAR *acl = sd->Data + at;
        acl[0] = ACL_REVISION;
        *(USHORT *)(acl + 2) = (USHORT)acl_size;
        *(USHORT *)(acl + 4) = (USHORT)dacl_count;
        UCHAR *ace = acl + 8;
        for (ULONG i = 0; i < dacl_count; i++) {
            ULONG sid = 8 + 4 * (ULONG)dacl[i].Sid[1];
            ace[0] = ACCESS_ALLOWED_ACE_TYPE;
            ace[1] = dacl[i].Flags;
            *(USHORT *)(ace + 2) = (USHORT)(8 + sid);
            *(ULONG *)(ace + 4) = dacl[i].Mask;
            memcpy(ace + 8, dacl[i].Sid, sid);
            ace += 8 + sid;
        }
        *(ULONG *)(sd->Data + 16) = at;
    }
    *out = sd;
    return STATUS_SUCCESS;
}

/* ---------------------------------------------------------------- the token defaults */

typedef struct {
    PTOKEN Owner; /* gives the owner and the group: the token of the thread, else the process's */
    PTOKEN Dacl;  /* gives the default dacl */
} SUBJECT;

static NTSTATUS fetch_default(void *context, ULONG which, PVOID *value)
{
    SUBJECT *s = context;
    switch (which) {
    case MIME_SEC_OWNER:
        *value = s->Owner->Groups[s->Owner->OwnerIndex].Sid;
        return STATUS_SUCCESS;
    case MIME_SEC_GROUP:
        *value = s->Owner->PrimaryGroup;
        return STATUS_SUCCESS;
    default:
        *value = s->Dacl->DefaultDacl;
        return STATUS_SUCCESS;
    }
}

/* the sid of a label, S-1-16-rid, as the rid. */
static ULONG label_rid(const UCHAR *label)
{
    return label[1] ? ((const ULONG *)(label + 8))[label[1] - 1] : 0;
}

/* the integrity label ace of a sacl, or NULL. */
static const UCHAR *find_label(const SECURITY_DESCRIPTOR_RELATIVE *sd)
{
    if (!(sd->Control & SE_SACL_PRESENT) || !sd->Sacl)
        return nullptr;
    const ACL *sacl = (const ACL *)((const UCHAR *)sd + sd->Sacl);
    const UCHAR *ace = (const UCHAR *)(sacl + 1);
    for (ULONG i = 0; i < sacl->AceCount; i++, ace += *(const USHORT *)(ace + 2))
        if (ace[0] == SYSTEM_MANDATORY_LABEL_ACE_TYPE)
            return ace;
    return nullptr;
}

/* the sd with the label ace of the integrity level added to its sacl, which a low integrity creator leaves. */
static NTSTATUS add_label(SD_BLOB *sd, ULONG rid, SD_BLOB **out)
{
    const SECURITY_DESCRIPTOR_RELATIVE *r = (const SECURITY_DESCRIPTOR_RELATIVE *)sd->Data;
    const ACL *old = (r->Control & SE_SACL_PRESENT) && r->Sacl ? (const ACL *)(sd->Data + r->Sacl) : nullptr;
    ULONG old_size = old ? old->AclSize : 8;
    ULONG ace_size = 8 + 12;
    ULONG sacl_size = (old_size + ace_size + 3) & ~3u;
    ULONG rest = 0;
    ULONG owner = r->Owner ? (8 + 4 * (ULONG)sd->Data[r->Owner + 1] + 3) & ~3u : 0;
    ULONG group = r->Group ? (8 + 4 * (ULONG)sd->Data[r->Group + 1] + 3) & ~3u : 0;
    ULONG dacl = (r->Control & SE_DACL_PRESENT) && r->Dacl ? (((const ACL *)(sd->Data + r->Dacl))->AclSize + 3) & ~3u : 0;
    rest = owner + group + dacl;
    SD_BLOB *made = SeSdAllocate(20 + sacl_size + rest);
    if (!made)
        return STATUS_INSUFFICIENT_RESOURCES;
    memset(made->Data, 0, made->Length);
    made->Data[0] = 1;
    *(USHORT *)(made->Data + 2) = r->Control | SE_SACL_PRESENT;
    ULONG at = 20;
    UCHAR *acl = made->Data + at;
    acl[0] = old && old->AclRevision > ACL_REVISION ? old->AclRevision : ACL_REVISION;
    *(USHORT *)(acl + 2) = (USHORT)sacl_size;
    USHORT count = old ? old->AceCount : 0;
    if (old && old_size > 8)
        memcpy(acl + 8, old + 1, old_size - 8);
    UCHAR *ace = acl + old_size;
    ace[0] = SYSTEM_MANDATORY_LABEL_ACE_TYPE;
    ace[1] = 0;
    *(USHORT *)(ace + 2) = (USHORT)ace_size;
    *(ULONG *)(ace + 4) = 1; /* no write up */
    ace[8] = 1;
    ace[9] = 1;
    ace[15] = 16;
    *(ULONG *)(ace + 16) = rid;
    *(USHORT *)(acl + 4) = count + 1;
    *(ULONG *)(made->Data + 12) = at;
    at += sacl_size;
    if (owner) {
        memcpy(made->Data + at, sd->Data + r->Owner, owner);
        *(ULONG *)(made->Data + 4) = at;
        at += owner;
    }
    if (group) {
        memcpy(made->Data + at, sd->Data + r->Group, group);
        *(ULONG *)(made->Data + 8) = at;
        at += group;
    }
    if (dacl) {
        memcpy(made->Data + at, sd->Data + r->Dacl, dacl);
        *(ULONG *)(made->Data + 16) = at;
    }
    *out = made;
    return STATUS_SUCCESS;
}

/* whether a sid is the user of the token, or a group that may own what it makes. */
static BOOLEAN owner_allowed(const TOKEN *t, const UCHAR *sid)
{
    if (t->Present & t->Enabled & (1ull << 18)) /* SeRestorePrivilege */
        return TRUE;
    for (ULONG i = 0; i < t->GroupCount; i++) {
        const SEP_GROUP *g = &t->Groups[i];
        if (i && !(g->Attributes & SE_GROUP_OWNER))
            continue;
        if ((g->Attributes & SE_GROUP_USE_FOR_DENY_ONLY) || !SepSidEqual(g->Sid, sid))
            continue;
        return TRUE;
    }
    return FALSE;
}

static BOOLEAN privilege_held(const TOKEN *t, ULONG luid)
{
    return (t->Present & t->Enabled & (1ull << luid)) != 0;
}

/* ---------------------------------------------------------------- new objects */

/* with a token given, that one is the whole subject, else it is the calling thread. */
static NTSTATUS assign(const SD_BLOB *parent, const SD_BLOB *creator, SD_MAPPING mapping, BOOLEAN container,
                       KPROCESSOR_MODE mode, PTOKEN fixed, SD_BLOB **out)
{
    PTOKEN effective = fixed ? fixed : SepReferenceEffectiveToken(PsCurrentThread());
    PTOKEN primary = fixed ? fixed : SepReferencePrimaryToken(PsCurrentProcess());
    if (fixed) {
        ObReferenceObject(effective);
        ObReferenceObject(primary);
    }
    if (!effective || !primary) {
        if (effective)
            ObDereferenceObject(effective);
        if (primary)
            ObDereferenceObject(primary);
        return STATUS_NO_TOKEN;
    }
    /* what a server makes while it only identifies a client is owned as the server. */
    BOOLEAN server = effective->Type == TokenImpersonation && effective->ImpersonationLevel < SecurityImpersonation;
    SUBJECT subject = {server ? primary : effective, effective};
    MIME_SEC_DEFAULTS defaults = {fetch_default, &subject};

    ULONG flags = 0;
    const SECURITY_DESCRIPTOR_RELATIVE *c = creator ? (const SECURITY_DESCRIPTOR_RELATIVE *)creator->Data : nullptr;
    const SECURITY_DESCRIPTOR_RELATIVE *p = parent ? (const SECURITY_DESCRIPTOR_RELATIVE *)parent->Data : nullptr;
    /* a parent whose lists were inherited themselves hands down by auto inheritance, unless the creator brings a list. */
    if (p && (p->Control & SE_DACL_AUTO_INHERITED) && !(c && (c->Control & SE_DACL_PRESENT)))
        flags |= SEF_DACL_AUTO_INHERIT;
    if (p && (p->Control & SE_SACL_AUTO_INHERITED) && !(c && (c->Control & SE_SACL_PRESENT)))
        flags |= SEF_SACL_AUTO_INHERIT;
    if (c && (c->Control & (SE_DACL_AUTO_INHERIT_REQ | SE_DACL_AUTO_INHERITED)) ==
                 (SE_DACL_AUTO_INHERIT_REQ | SE_DACL_AUTO_INHERITED))
        flags |= SEF_DACL_AUTO_INHERIT;
    if (c && (c->Control & (SE_SACL_AUTO_INHERIT_REQ | SE_SACL_AUTO_INHERITED)) ==
                 (SE_SACL_AUTO_INHERIT_REQ | SE_SACL_AUTO_INHERITED))
        flags |= SEF_SACL_AUTO_INHERIT;

    NTSTATUS status = STATUS_SUCCESS;
    ExAcquireFastMutex(&SeLock);
    if (mode == UserMode && c) {
        /* an owner the caller may not give, a sacl without the security privilege, a label above its own. */
        PSID owner = nullptr;
        BOOLEAN defaulted;
        RtlGetOwnerSecurityDescriptor((PSECURITY_DESCRIPTOR)c, &owner, &defaulted);
        if (owner && !owner_allowed(subject.Owner, owner))
            status = STATUS_INVALID_OWNER;
        PSID group = nullptr;
        RtlGetGroupSecurityDescriptor((PSECURITY_DESCRIPTOR)c, &group, &defaulted);
        if (NT_SUCCESS(status) && (c->Control & SE_SACL_PRESENT) && c->Sacl) {
            const ACL *sacl = (const ACL *)((const UCHAR *)c + c->Sacl);
            BOOLEAN audit = FALSE;
            const UCHAR *ace = (const UCHAR *)(sacl + 1);
            for (ULONG i = 0; i < sacl->AceCount; i++, ace += *(const USHORT *)(ace + 2)) {
                if (ace[0] != SYSTEM_MANDATORY_LABEL_ACE_TYPE && ace[0] != SYSTEM_RESOURCE_ATTRIBUTE_ACE_TYPE &&
                    ace[0] != SYSTEM_SCOPED_POLICY_ID_ACE_TYPE && ace[0] != SYSTEM_PROCESS_TRUST_LABEL_ACE_TYPE &&
                    ace[0] != SYSTEM_ACCESS_FILTER_ACE_TYPE)
                    audit = TRUE;
            }
            if (audit && !privilege_held(effective, SE_SECURITY_PRIVILEGE))
                status = STATUS_PRIVILEGE_NOT_HELD;
        }
        const UCHAR *label = c ? find_label(c) : nullptr;
        if (NT_SUCCESS(status) && label) {
            /* a label may not be above the creator's level, a rid beyond what an integrity sid is, or odd flags. */
            const UCHAR *sid = label + 8;
            if (label[1] & 0xE0 || *(const ULONG *)(label + 4) & ~7u || sid[1] != 1 || sid[7] != 16 || sid[2] || sid[3] ||
                sid[4] || sid[5] || sid[6])
                status = STATUS_INVALID_PARAMETER;
            else if (label_rid(sid) > SepIntegrityRid(effective) && !privilege_held(effective, 32))
                status = STATUS_PRIVILEGE_NOT_HELD;
        }
    }
    PSECURITY_DESCRIPTOR made = nullptr;
    if (NT_SUCCESS(status)) {
        status = MimeSecNewObject(parent ? (PSECURITY_DESCRIPTOR)parent->Data : nullptr,
                                  creator ? (PSECURITY_DESCRIPTOR)creator->Data : nullptr, &made, nullptr, 0,
                                  container, flags, (const GENERIC_MAPPING *)mapping, &defaults);
    }
    ULONG rid = SepIntegrityRid(effective);
    ExReleaseFastMutex(&SeLock);
    ObDereferenceObject(effective);
    ObDereferenceObject(primary);
    if (!NT_SUCCESS(status))
        return status;
    SD_BLOB *sd;
    status = blob_of_rtl(made, &sd);
    if (!NT_SUCCESS(status))
        return status;
    /* what a low integrity process makes is low integrity, unless the parent or the creator said otherwise. */
    if (rid && rid < 0x2000 && !find_label((const SECURITY_DESCRIPTOR_RELATIVE *)sd->Data)) {
        SD_BLOB *labelled;
        status = add_label(sd, rid, &labelled);
        SeSdDereference(sd);
        if (!NT_SUCCESS(status))
            return status;
        sd = labelled;
    }
    *out = sd;
    return STATUS_SUCCESS;
}

NTSTATUS SeSdAssign(const SD_BLOB *parent, const SD_BLOB *creator, SD_MAPPING mapping, BOOLEAN container,
                    KPROCESSOR_MODE mode, SD_BLOB **out)
{
    return assign(parent, creator, mapping, container, mode, nullptr, out);
}

NTSTATUS SeSdDerive(const SD_BLOB *parent, SD_MAPPING mapping, BOOLEAN container, SD_BLOB **out)
{
    PTOKEN session = SepReferenceSessionToken();
    NTSTATUS status = assign(parent, nullptr, mapping, container, KernelMode, session, out);
    ObDereferenceObject(session);
    return status;
}

BOOLEAN SeSdSubjectIsDefault(void)
{
    PTOKEN t = SepReferenceEffectiveToken(PsCurrentThread());
    PTOKEN session = SepReferenceSessionToken();
    BOOLEAN same = FALSE;
    if (t) {
        ExAcquireFastMutex(&SeLock);
        same = t->Groups[t->OwnerIndex].Sid[1] == session->Groups[session->OwnerIndex].Sid[1] &&
               SepSidEqual(t->Groups[t->OwnerIndex].Sid, session->Groups[session->OwnerIndex].Sid) &&
               SepSidEqual(t->PrimaryGroup, session->PrimaryGroup) &&
               t->DefaultDaclSize == session->DefaultDaclSize &&
               (!t->DefaultDaclSize || !memcmp(t->DefaultDacl, session->DefaultDacl, t->DefaultDaclSize)) &&
               SepIntegrityRid(t) >= 0x2000;
        ExReleaseFastMutex(&SeLock);
        ObDereferenceObject(t);
    }
    ObDereferenceObject(session);
    return same;
}

/*
 * the descriptor most objects get, made once for a token and a type: cached by the token's id and the id of its
 * last change, so creating an event costs a lookup.
 */
typedef struct {
    ULONG64 TokenId, ModifiedId;
    SD_MAPPING Mapping;
    BOOLEAN Container;
    SD_BLOB *Sd;
} DEFAULT_ENTRY;

static DEFAULT_ENTRY g_defaults[SD_CACHE_ENTRIES];
static FAST_MUTEX g_default_lock;
static BOOLEAN g_default_ready;

NTSTATUS SeSdDefault(SD_MAPPING mapping, BOOLEAN container, SD_BLOB **out)
{
    PTOKEN t = SepReferenceEffectiveToken(PsCurrentThread());
    if (!t)
        return STATUS_NO_TOKEN;
    ULONG64 id = t->TokenId, modified = t->ModifiedId;
    ObDereferenceObject(t);
    if (!g_default_ready) {
        ExInitializeFastMutex(&g_default_lock);
        g_default_ready = TRUE;
    }
    ULONG slot = (ULONG)((id * 31 + modified * 7 + ((ULONG_PTR)mapping >> 4)) % SD_CACHE_ENTRIES);
    ExAcquireFastMutex(&g_default_lock);
    DEFAULT_ENTRY *e = &g_defaults[slot];
    if (e->Sd && e->TokenId == id && e->ModifiedId == modified && e->Mapping == mapping && e->Container == container) {
        *out = SeSdReference(e->Sd);
        ExReleaseFastMutex(&g_default_lock);
        return STATUS_SUCCESS;
    }
    ExReleaseFastMutex(&g_default_lock);
    SD_BLOB *sd;
    NTSTATUS status = assign(nullptr, nullptr, mapping, container, KernelMode, nullptr, &sd);
    if (!NT_SUCCESS(status))
        return status;
    ExAcquireFastMutex(&g_default_lock);
    e = &g_defaults[slot];
    SD_BLOB *old = e->Sd;
    e->TokenId = id;
    e->ModifiedId = modified;
    e->Mapping = mapping;
    e->Container = container;
    e->Sd = SeSdReference(sd);
    ExReleaseFastMutex(&g_default_lock);
    SeSdDereference(old);
    *out = sd;
    return STATUS_SUCCESS;
}

NTSTATUS SeSdSystemDefault(SD_MAPPING mapping, BOOLEAN container, SD_BLOB **out)
{
    SD_ACE aces[3] = {{container ? CONTAINER_INHERIT_ACE : 0, mapping[3], SeSidSystem},
                      {container ? CONTAINER_INHERIT_ACE : 0, mapping[3], SeSidAdministrators},
                      {container ? CONTAINER_INHERIT_ACE : 0, mapping[0] | mapping[2], SeSidEveryone}};
    return SeSdBuild(SeSidAdministrators, SeSidSystem, SE_DACL_AUTO_INHERITED, aces, 3, out);
}

/* ---------------------------------------------------------------- rights */

ACCESS_MASK SeSdReadRights(ULONG info)
{
    ACCESS_MASK rights = 0;
    if (info & 0x10000)
        rights |= 0x1020000;
    if (info & (0x17 | 0x20 | 0x40 | 0x80 | 0x100))
        rights |= 0x20000;
    if (info & 8)
        rights |= 0x1000000;
    return rights;
}

ACCESS_MASK SeSdWriteRights(ULONG info)
{
    ACCESS_MASK rights = 0;
    if (info & 0x10000)
        rights |= 0x1040000;
    if (info & 0x13)
        rights |= 0x80000;
    if (info & (4 | 0x20 | 0x80 | 0x100))
        rights |= 0x40000;
    if (info & (8 | 0x40))
        rights |= 0x1000000;
    if ((info & 0x1F8) && (info & 0x50000000))
        rights |= 0x1000000;
    return rights;
}

/* ---------------------------------------------------------------- set and query */

NTSTATUS SeSdSet(ULONG info, const SD_BLOB *modification, const SD_BLOB *old, SD_MAPPING mapping,
                 KPROCESSOR_MODE mode, SD_BLOB **out)
{
    const SECURITY_DESCRIPTOR_RELATIVE *m = (const SECURITY_DESCRIPTOR_RELATIVE *)modification->Data;
    ULONG parts = info & 0x1FF;
    if (info & 0x10000) {
        /* a backup of the whole descriptor: all the parts the modification has. */
        parts = 0x17F | (info & 0x80) | (info & 0x100);
        if (!(m->Control & SE_SACL_PRESENT))
            parts &= ~0x1F8u;
        else
            parts |= 8;
    }
    if (((parts & OWNER_SECURITY_INFORMATION) && !m->Owner) || ((parts & GROUP_SECURITY_INFORMATION) && !m->Group))
        return STATUS_INVALID_SECURITY_DESCR;

    NTSTATUS status = STATUS_SUCCESS;
    if (mode == UserMode) {
        PTOKEN t = SepReferenceEffectiveToken(PsCurrentThread());
        if (!t)
            return STATUS_NO_TOKEN;
        ExAcquireFastMutex(&SeLock);
        if ((parts & OWNER_SECURITY_INFORMATION) && !owner_allowed(t, modification->Data + m->Owner))
            status = STATUS_INVALID_OWNER;
        if (NT_SUCCESS(status) && (parts & LABEL_SECURITY_INFORMATION)) {
            /*
             * a label can be set as high as the caller's own level, or higher with the relabel privilege; with none
             * given the label goes, which takes the level of medium.
             */
            const UCHAR *label = find_label(m);
            ULONG wanted = label ? label_rid(label + 8) : 0x2000;
            BOOLEAN identify = t->Type == TokenImpersonation && t->ImpersonationLevel < SecurityImpersonation;
            if (label && (label[1] & 0xE0 || *(const ULONG *)(label + 4) & ~7u))
                status = STATUS_INVALID_PARAMETER;
            else if (identify || (wanted > SepIntegrityRid(t) && !privilege_held(t, 32)))
                status = STATUS_INVALID_LABEL;
        }
        ExReleaseFastMutex(&SeLock);
        ObDereferenceObject(t);
        if (!NT_SUCCESS(status))
            return status;
    }

    /* the rtl swaps the object's block for the new one, so it works on a copy. */
    PSECURITY_DESCRIPTOR work = MimeRtlAllocate(old->Length);
    if (!work)
        return STATUS_INSUFFICIENT_RESOURCES;
    memcpy(work, old->Data, old->Length);
    status = RtlSetSecurityObjectEx(parts, (PSECURITY_DESCRIPTOR)modification->Data, &work, 0,
                                    (const GENERIC_MAPPING *)mapping, nullptr);
    if (!NT_SUCCESS(status)) {
        MimeRtlFree(work);
        return status;
    }
    return blob_of_rtl(work, out);
}

/* the layout windows uses: header, owner, group, dacl, sacl, each padded to four. */
NTSTATUS SeSdQuery(ULONG info, const SD_BLOB *sd, PVOID buffer, PULONG length)
{
    ULONG room = *length;
    if (!sd) {
        *length = 20;
        if (room < 20)
            return STATUS_BUFFER_TOO_SMALL;
        memset(buffer, 0, 20);
        ((UCHAR *)buffer)[0] = 1;
        *(USHORT *)((UCHAR *)buffer + 2) = SE_SELF_RELATIVE;
        return STATUS_SUCCESS;
    }
    const SECURITY_DESCRIPTOR_RELATIVE *r = (const SECURITY_DESCRIPTOR_RELATIVE *)sd->Data;
    ULONG parts = info;
    if (info & 0x10000) {
        parts = info | 0x1FF;
        if (!(info & 0x80))
            parts &= ~0x80u;
        if (!(info & 0x100))
            parts &= ~0x100u;
    }
    const UCHAR *owner = (parts & OWNER_SECURITY_INFORMATION) && r->Owner ? sd->Data + r->Owner : nullptr;
    const UCHAR *group = (parts & GROUP_SECURITY_INFORMATION) && r->Group ? sd->Data + r->Group : nullptr;
    const ACL *dacl = (parts & DACL_SECURITY_INFORMATION) && (r->Control & SE_DACL_PRESENT) && r->Dacl
                          ? (const ACL *)(sd->Data + r->Dacl)
                          : nullptr;
    const ACL *sacl = (parts & 0x1F8) && (r->Control & SE_SACL_PRESENT) && r->Sacl ? (const ACL *)(sd->Data + r->Sacl)
                                                                                    : nullptr;
    ULONG owner_size = owner ? 8 + 4 * (ULONG)owner[1] : 0;
    ULONG group_size = group ? 8 + 4 * (ULONG)group[1] : 0;
    ULONG dacl_size = dacl ? (dacl->AclSize + 3u) & ~3u : 0;
    ULONG sacl_size = sacl ? (MimeSecFilterSacl(sacl, nullptr, parts & 0x1F8) + 3u) & ~3u : 0;
    ULONG total = 20 + ((owner_size + 3) & ~3u) + ((group_size + 3) & ~3u) + dacl_size + sacl_size;
    *length = total;
    if (total > room)
        return STATUS_BUFFER_TOO_SMALL;
    UCHAR *out = buffer;
    memset(out, 0, total);
    out[0] = 1;
    USHORT control = SE_SELF_RELATIVE;
    ULONG at = 20;
    if (owner) {
        memcpy(out + at, owner, owner_size);
        *(ULONG *)(out + 4) = at;
        control |= r->Control & SE_OWNER_DEFAULTED;
        at += (owner_size + 3) & ~3u;
    }
    if (group) {
        memcpy(out + at, group, group_size);
        *(ULONG *)(out + 8) = at;
        control |= r->Control & SE_GROUP_DEFAULTED;
        at += (group_size + 3) & ~3u;
    }
    if (parts & DACL_SECURITY_INFORMATION) {
        control |= r->Control & (SE_DACL_PRESENT | SE_DACL_DEFAULTED | SE_DACL_AUTO_INHERITED | SE_DACL_PROTECTED);
        if (dacl) {
            memcpy(out + at, dacl, dacl->AclSize);
            *(ULONG *)(out + 16) = at;
            at += dacl_size;
        }
    }
    if (parts & 0x1F8) {
        control |= r->Control & (SE_SACL_PRESENT | SE_SACL_DEFAULTED | SE_SACL_AUTO_INHERITED | SE_SACL_PROTECTED);
        if (sacl) {
            MimeSecFilterSacl(sacl, (PACL)(out + at), parts & 0x1F8);
            *(ULONG *)(out + 12) = at;
        }
    }
    *(USHORT *)(out + 2) = control;
    return STATUS_SUCCESS;
}
