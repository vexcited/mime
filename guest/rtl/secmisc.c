/*
 * the rest of the rtl security routines that need neither a token nor a hash: app container sids, sid hash
 * tables, descriptor copies and normal forms, sid replacement and the descriptors built from ace tables.
 */
#define NTSYSAPI
#include "rtl.h"
#include <mime/ntrtlsec.h>


/* ---------------------------------------------------------------- app container and capability sids */

static const UCHAR g_package_authority[6] = {0, 0, 0, 0, 0, 15};

/* an application package authority sid whose first sub authority is the kind: 2 packages, 3 capabilities. */
static BOOLEAN package_sid(const SID *sid, ULONG kind)
{
    return sid->SubAuthorityCount >= 2 && sid->Revision == SID_REVISION &&
           !memcmp(&sid->IdentifierAuthority, g_package_authority, 6) && sid->SubAuthority[0] == kind;
}

BOOLEAN NTAPI RtlIsPackageSid(PSID sid)
{
    return package_sid(sid, 2);
}

BOOLEAN NTAPI RtlIsCapabilitySid(PSID sid)
{
    return package_sid(sid, 3);
}

/* 1 is a child container, 2 a parent one and 3 a package sid of some other size. */
NTSTATUS NTAPI RtlGetAppContainerSidType(PSID sid, PULONG type)
{
    const SID *s = sid;
    if (package_sid(s, 2)) {
        if (s->SubAuthorityCount == 8) {
            *type = 2;
            return STATUS_SUCCESS;
        }
        if (s->SubAuthorityCount == 12) {
            *type = 1;
            return STATUS_SUCCESS;
        }
        *type = 3;
    } else {
        *type = 0;
    }
    return STATUS_NOT_APPCONTAINER;
}

/* the sid of the parent of a child container: the first eight sub authorities. */
NTSTATUS NTAPI RtlGetAppContainerParent(PSID sid, PSID *parent)
{
    const SID *s = sid;
    *parent = nullptr;
    if (!package_sid(s, 2) || s->SubAuthorityCount != 12)
        return STATUS_INVALID_PARAMETER;
    SID *copy = MimeRtlAllocate(8 + 4 * 8);
    if (!copy)
        return STATUS_INSUFFICIENT_RESOURCES;
    memcpy(copy, s, 8 + 4 * 8);
    copy->SubAuthorityCount = 8;
    *parent = copy;
    return STATUS_SUCCESS;
}

/* ---------------------------------------------------------------- sid hash tables */

#define HASH_BITS ((ULONG)(8 * sizeof(ULONG_PTR)))

/* the byte that tells sids apart most is the lowest of the last sub authority, split in two nibbles. */
static UCHAR hash_byte(const SID *sid)
{
    return ((const UCHAR *)sid)[4 * (ULONG)sid->SubAuthorityCount + 4];
}

NTSTATUS NTAPI RtlSidHashInitialize(PSID_AND_ATTRIBUTES sids, ULONG count, PSID_AND_ATTRIBUTES_HASH hash)
{
    if (!hash)
        return STATUS_INVALID_PARAMETER;
    memset(hash, 0, sizeof(*hash));
    if (sids && count) {
        hash->SidAttr = sids;
        hash->SidCount = count;
        for (ULONG i = 0; i < count && i < HASH_BITS; i++) {
            UCHAR b = hash_byte(sids[i].Sid);
            hash->Hash[b & 0xF] |= (ULONG_PTR)1 << i;
            hash->Hash[16 + (b >> 4)] |= (ULONG_PTR)1 << i;
        }
    }
    return STATUS_SUCCESS;
}

PSID_AND_ATTRIBUTES NTAPI RtlSidHashLookup(PSID_AND_ATTRIBUTES_HASH hash, PSID sid)
{
    if (!hash || !sid)
        return nullptr;
    const SID *s = sid;
    ULONG length = 8 + 4 * (ULONG)s->SubAuthorityCount;
    UCHAR b = hash_byte(s);
    /* the entries that share both nibbles are the only ones worth comparing. */
    ULONG_PTR candidates = hash->Hash[b & 0xF] & hash->Hash[16 + (b >> 4)];
    for (ULONG i = 0; candidates; i++, candidates >>= 1) {
        if (!(candidates & 1))
            continue;
        const SID *other = hash->SidAttr[i].Sid;
        if (*(const USHORT *)other == *(const USHORT *)s && !memcmp(s, other, length))
            return &hash->SidAttr[i];
    }
    for (ULONG i = HASH_BITS; i < hash->SidCount; i++) {
        const SID *other = hash->SidAttr[i].Sid;
        if (*(const USHORT *)other == *(const USHORT *)s && !memcmp(s, other, length))
            return &hash->SidAttr[i];
    }
    return nullptr;
}

/* ---------------------------------------------------------------- process protection */

/* for each signer of the process doing the opening, the signers of the target it may open. */
static const ULONG g_protected_access[9] = {0x0, 0x2, 0x4, 0x108, 0x110, 0x13E, 0x17E, 0x1FE, 0x0};

/* a protection is a type in the low three bits and a signer above the fourth. */
BOOLEAN NTAPI RtlTestProtectedAccess(UCHAR source, UCHAR target)
{
    if (!(target & 7))
        return TRUE;
    if ((source & 7) < (target & 7))
        return FALSE;
    ULONG signer = source >> 4;
    return signer < RTL_NUMBER_OF(g_protected_access) && ((g_protected_access[signer] >> (target >> 4)) & 1);
}

/* ---------------------------------------------------------------- security error codes */

/* the sspi errors a descriptor routine may meet, as the status codes of the same trouble. */
NTSTATUS NTAPI RtlMapSecurityErrorToNtStatus(ULONG error)
{
    switch (error) {
    case 0x80090300: return (NTSTATUS)0xC000009A;
    case 0x80090301: return (NTSTATUS)0xC0000008;
    case 0x80090302:
    case 0x8009030A: return (NTSTATUS)0xC00000BB;
    case 0x80090303: return (NTSTATUS)0xC00000BE;
    case 0x80090304: return (NTSTATUS)0xC00000E5;
    case 0x80090305:
    case 0x80090307:
    case 0x80090316: return (NTSTATUS)0xC00000FE;
    case 0x80090306: return (NTSTATUS)0xC0000061;
    case 0x80090308:
    case 0x80090309:
    case 0x8009035D: return (NTSTATUS)0xC000000D;
    case 0x8009030B: return (NTSTATUS)0xC000010D;
    case 0x8009030C: return (NTSTATUS)0xC000006D;
    case 0x8009030D:
    case 0x8009030E: return (NTSTATUS)0xC000005F;
    case 0x8009030F:
    case 0x80090310: return (NTSTATUS)0xC0000022;
    case 0x80090311: return (NTSTATUS)0xC000005E;
    case 0x80090324: return (NTSTATUS)0xC0000133;
    case 0x80090331: return (NTSTATUS)0xC00000BB;
    default: return (NTSTATUS)error;
    }
}

/* ---------------------------------------------------------------- descriptors */

static PSID part_sid(PSECURITY_DESCRIPTOR sd, BOOLEAN group)
{
    PSID sid = nullptr;
    BOOLEAN defaulted;
    if (group)
        RtlGetGroupSecurityDescriptor(sd, &sid, &defaulted);
    else
        RtlGetOwnerSecurityDescriptor(sd, &sid, &defaulted);
    return sid;
}

static PACL part_acl(PSECURITY_DESCRIPTOR sd, BOOLEAN dacl)
{
    PACL acl = nullptr;
    BOOLEAN present, defaulted;
    if (dacl)
        RtlGetDaclSecurityDescriptor(sd, &present, &acl, &defaulted);
    else
        RtlGetSaclSecurityDescriptor(sd, &present, &acl, &defaulted);
    return present ? acl : nullptr;
}

/* a copy of a self relative descriptor, in one block. an absolute one is made self relative on the way. */
NTSTATUS NTAPI RtlCopySecurityDescriptor(PSECURITY_DESCRIPTOR source, PSECURITY_DESCRIPTOR *copy)
{
    const SECURITY_DESCRIPTOR_RELATIVE *r = source;
    if (!(r->Control & SE_SELF_RELATIVE)) {
        ULONG length = 0;
        NTSTATUS status = RtlAbsoluteToSelfRelativeSD(source, nullptr, &length);
        if (status != STATUS_BUFFER_TOO_SMALL)
            return status;
        *copy = MimeRtlAllocate(length);
        if (!*copy)
            return STATUS_NO_MEMORY;
        status = RtlAbsoluteToSelfRelativeSD(source, *copy, &length);
        if (!NT_SUCCESS(status)) {
            MimeRtlFree(*copy);
            *copy = nullptr;
        }
        return status;
    }
    ULONG length = sizeof(SECURITY_DESCRIPTOR_RELATIVE);
    PSID owner = part_sid(source, FALSE), group = part_sid(source, TRUE);
    PACL dacl = part_acl(source, TRUE), sacl = part_acl(source, FALSE);
    if (owner)
        length += (4 * (ULONG)((SID *)owner)->SubAuthorityCount + 11) & ~3u;
    if (group)
        length += (4 * (ULONG)((SID *)group)->SubAuthorityCount + 11) & ~3u;
    if (dacl)
        length += (dacl->AclSize + 3) & ~3u;
    if (sacl)
        length += (sacl->AclSize + 3) & ~3u;
    *copy = MimeRtlAllocate(length);
    if (!*copy)
        return STATUS_NO_MEMORY;
    memcpy(*copy, source, length);
    return STATUS_SUCCESS;
}

/* only the bits of the control that a caller may set, 0x3FC0, are touched. */
NTSTATUS NTAPI RtlSetAttributesSecurityDescriptor(PSECURITY_DESCRIPTOR sd, SECURITY_DESCRIPTOR_CONTROL control,
                                                  PULONG revision)
{
    *revision = *(const UCHAR *)sd;
    if (*(const UCHAR *)sd != SECURITY_DESCRIPTOR_REVISION)
        return STATUS_UNKNOWN_REVISION;
    return RtlSetControlSecurityDescriptor(sd, control & 0x3FC0, control & 0x3FC0);
}

/* whether a sid is the old one, or the old one with one more relative id. */
static BOOLEAN sid_has_prefix(const SID *sid, const SID *old)
{
    if (sid->Revision != old->Revision)
        return FALSE;
    if (sid->SubAuthorityCount != old->SubAuthorityCount && sid->SubAuthorityCount != old->SubAuthorityCount + 1)
        return FALSE;
    return !memcmp(&sid->IdentifierAuthority, &old->IdentifierAuthority, 6) &&
           !memcmp(sid->SubAuthority, old->SubAuthority, 4 * (ULONG)old->SubAuthorityCount);
}

static void replace_in_sid(SID *sid, const SID *old, const SID *replacement, PULONG changed)
{
    if (sid && sid_has_prefix(sid, old)) {
        memmove(sid->SubAuthority, replacement->SubAuthority, 4 * (ULONG)old->SubAuthorityCount);
        (*changed)++;
    }
}

static void replace_in_acl(const ACL *acl, const SID *old, const SID *replacement, PULONG changed)
{
    if (!acl)
        return;
    UCHAR *ace = (UCHAR *)(acl + 1);
    for (ULONG i = 0; i < acl->AceCount; i++, ace += *(USHORT *)(ace + 2)) {
        if (ace[0] <= SYSTEM_ALARM_ACE_TYPE) {
            replace_in_sid((SID *)(ace + 8), old, replacement, changed);
        } else if (ace[0] == ACCESS_ALLOWED_COMPOUND_ACE_TYPE) {
            /* the client is the second sid, after the server. */
            replace_in_sid((SID *)(ace + 4 * (ULONG)ace[13] + 20), old, replacement, changed);
        }
    }
}

/*
 * swaps the leading sub authorities of every sid that starts with the old sid, as when a machine moves to
 * another domain: the owner, the group and the plain and compound aces. the new sid has the old one's size.
 */
NTSTATUS NTAPI RtlReplaceSidInSd(PSECURITY_DESCRIPTOR sd, PSID old, PSID replacement, PULONG changed)
{
    *changed = 0;
    BOOLEAN defaulted;
    PSID owner = nullptr, group = nullptr;
    NTSTATUS status = RtlGetOwnerSecurityDescriptor(sd, &owner, &defaulted);
    if (!NT_SUCCESS(status))
        return status;
    replace_in_sid(owner, old, replacement, changed);
    status = RtlGetGroupSecurityDescriptor(sd, &group, &defaulted);
    if (!NT_SUCCESS(status))
        return status;
    replace_in_sid(group, old, replacement, changed);
    BOOLEAN present;
    PACL acl = nullptr;
    status = RtlGetDaclSecurityDescriptor(sd, &present, &acl, &defaulted);
    if (!NT_SUCCESS(status))
        return status;
    if (present)
        replace_in_acl(acl, old, replacement, changed);
    acl = nullptr;
    status = RtlGetSaclSecurityDescriptor(sd, &present, &acl, &defaulted);
    if (NT_SUCCESS(status) && present)
        replace_in_acl(acl, old, replacement, changed);
    return status;
}

/* ---------------------------------------------------------------- normal form */

/* the aces of a list in their compact form: a repeated allow ace once, then padded and with no slack. */
static ULONG compact_acl(PACL out, const ACL *acl, const GENERIC_MAPPING *mapping)
{
    UCHAR *to = (UCHAR *)(out + 1);
    const UCHAR *ace = (const UCHAR *)(acl + 1);
    USHORT kept = 0;
    for (ULONG i = 0; i < acl->AceCount; i++) {
        ULONG size = *(const USHORT *)(ace + 2);
        UCHAR head[8];
        BOOLEAN remapped = FALSE;
        /* the aces that apply to the object get their generic rights turned into specific ones. */
        if (mapping && size >= 8 && (ace[0] < 0xB || ace[0] == 13 || ace[0] == 14)) {
            memcpy(head, ace, 8);
            if (!(ace[1] & INHERIT_ONLY_ACE)) {
                ACCESS_MASK wide = *(const ULONG *)(ace + 4);
                if (wide & GENERIC_READ)
                    wide |= mapping->GenericRead;
                if (wide & GENERIC_WRITE)
                    wide |= mapping->GenericWrite;
                if (wide & GENERIC_EXECUTE)
                    wide |= mapping->GenericExecute;
                if (wide & GENERIC_ALL)
                    wide |= mapping->GenericAll;
                /* the audit types may keep the system access right. */
                ACCESS_MASK allowed = mapping->GenericAll | (((0x673u >> ace[0]) & 1) ? 0 : 0x01000000);
                *(ULONG *)(head + 4) = wide & allowed & 0x0FFFFFFF;
            }
            remapped = memcmp(head, ace, 8) != 0;
        }
        BOOLEAN duplicate = FALSE;
        if (ace[0] == ACCESS_ALLOWED_ACE_TYPE) {
            const UCHAR *other = (const UCHAR *)(out + 1);
            const UCHAR *header = remapped ? head : ace;
            for (USHORT j = 0; j < kept && !duplicate; j++, other += *(const USHORT *)(other + 2))
                duplicate = *(const USHORT *)(other + 2) == size && size >= 8 && !memcmp(other, header, 8) &&
                            !memcmp(other + 8, ace + 8, size - 8);
        }
        if (!duplicate) {
            if (remapped) {
                memcpy(to, head, 8);
                memmove(to + 8, ace + 8, size - 8);
            } else {
                memmove(to, ace, size);
            }
            to += size;
            kept++;
        }
        ace += size;
    }
    ULONG used = (ULONG)(to - (UCHAR *)out);
    ULONG padded = (used + 3) & ~3u;
    memset(to, 0, padded - used);
    *(ULONG64 *)out = *(const ULONG64 *)acl;
    out->AclSize = (USHORT)padded;
    out->AceCount = kept;
    return padded;
}

/*
 * the form in which equal descriptors have equal bytes: sacl, dacl, owner and group in that order with no gaps,
 * an empty sacl left out, and compact lists. returns whether the descriptor was not in that form. unless that
 * is only asked, a normalized copy is written to the buffer given, else the descriptor itself is replaced.
 */
BOOLEAN NTAPI RtlNormalizeSecurityDescriptor(PSECURITY_DESCRIPTOR *sd, ULONG length, PSECURITY_DESCRIPTOR *normal,
                                             PULONG normal_length, BOOLEAN check_only)
{
    const UCHAR *source = *sd;
    const SECURITY_DESCRIPTOR_RELATIVE *r = (const void *)source;
    if (!RtlValidSecurityDescriptor(*sd) || length < RtlLengthSecurityDescriptor(*sd) ||
        !(r->Control & SE_SELF_RELATIVE))
        return FALSE;

    UCHAR *image = MimeRtlAllocate(length);
    if (!image)
        return FALSE;
    SECURITY_DESCRIPTOR_RELATIVE *n = (void *)image;
    memset(image, 0, length);
    memcpy(image, source, 4);
    ULONG at = sizeof(SECURITY_DESCRIPTOR_RELATIVE);
    if (r->Sacl && ((const ACL *)(source + r->Sacl))->AceCount) {
        n->Sacl = at;
        at += compact_acl((PACL)(image + at), (const ACL *)(source + r->Sacl), nullptr);
    }
    if (r->Dacl) {
        n->Dacl = at;
        at += compact_acl((PACL)(image + at), (const ACL *)(source + r->Dacl), nullptr);
    }
    if (r->Owner) {
        ULONG size = 8 + 4 * (ULONG)((const SID *)(source + r->Owner))->SubAuthorityCount;
        memcpy(image + at, source + r->Owner, size);
        n->Owner = at;
        at += size;
    }
    if (r->Group) {
        ULONG size = 8 + 4 * (ULONG)((const SID *)(source + r->Group))->SubAuthorityCount;
        memcpy(image + at, source + r->Group, size);
        n->Group = at;
        at += size;
    }
    BOOLEAN changed = memcmp(image, source, at) != 0;
    if (!changed || check_only) {
        MimeRtlFree(image);
        return changed;
    }
    if (normal && *normal) {
        memcpy(*normal, image, at);
        MimeRtlFree(image);
    } else if (normal) {
        *normal = image;
    } else {
        MimeRtlFree(*sd);
        *sd = image;
    }
    if (normal_length)
        *normal_length = at;
    return TRUE;
}

/* ---------------------------------------------------------------- descriptors from ace tables */

/*
 * an absolute descriptor from a table of aces, the allow and deny ones in its dacl and the audit ones in its
 * sacl, in one block with both lists after the header. each ace is four bytes larger than its sid needs.
 */
NTSTATUS NTAPI RtlCreateAndSetSD(RTL_ACE_DATA *table, ULONG count, PSID owner, PSID group, PSECURITY_DESCRIPTOR *result)
{
    ULONG dacl_size = 8, sacl_size = 8, largest = 0;
    for (ULONG i = 0; i < count; i++) {
        ULONG ace = 4 * (ULONG)((SID *)*table[i].Sid)->SubAuthorityCount + 20;
        if (table[i].AceType <= ACCESS_DENIED_ACE_TYPE) {
            if (dacl_size + ace < dacl_size)
                return STATUS_NO_MEMORY;
            dacl_size += ace;
        } else if (table[i].AceType == SYSTEM_AUDIT_ACE_TYPE) {
            if (sacl_size + ace < sacl_size)
                return STATUS_NO_MEMORY;
            sacl_size += ace;
        } else {
            return STATUS_INVALID_PARAMETER;
        }
        if (ace > largest)
            largest = ace;
    }
    ULONG total = sizeof(SECURITY_DESCRIPTOR);
    if (dacl_size != 8)
        total += dacl_size;
    if (sacl_size != 8)
        total += sacl_size;
    UCHAR *block = MimeRtlAllocate(total);
    UCHAR *scratch = block ? MimeRtlAllocate(largest ? largest : 1) : nullptr;
    NTSTATUS status = STATUS_NO_MEMORY;
    if (!scratch) {
        if (block)
            MimeRtlFree(block);
        return status;
    }
    PACL dacl = dacl_size != 8 ? (PACL)(block + sizeof(SECURITY_DESCRIPTOR)) : nullptr;
    PACL sacl = sacl_size != 8 ? (PACL)(block + sizeof(SECURITY_DESCRIPTOR) + (dacl ? dacl_size : 0)) : nullptr;
    status = STATUS_SUCCESS;
    if (dacl)
        status = RtlCreateAcl(dacl, dacl_size, ACL_REVISION);
    if (NT_SUCCESS(status) && sacl)
        status = RtlCreateAcl(sacl, sacl_size, ACL_REVISION);
    for (ULONG i = 0; i < count && NT_SUCCESS(status); i++) {
        const SID *sid = *table[i].Sid;
        ULONG sid_size = 8 + 4 * (ULONG)sid->SubAuthorityCount;
        memset(scratch, 0, sid_size + 12);
        scratch[0] = table[i].AceType;
        scratch[1] = table[i].AceFlags | table[i].InheritFlags;
        *(USHORT *)(scratch + 2) = (USHORT)(sid_size + 12);
        *(ULONG *)(scratch + 4) = table[i].Mask;
        status = RtlCopySid(sid_size, scratch + 8, (PSID)sid);
        if (NT_SUCCESS(status))
            status = RtlAddAce(table[i].AceType == SYSTEM_AUDIT_ACE_TYPE ? sacl : dacl, ACL_REVISION, 0xFFFFFFFF,
                               scratch, sid_size + 12);
    }
    if (NT_SUCCESS(status)) {
        memset(block, 0, sizeof(SECURITY_DESCRIPTOR));
        status = RtlCreateSecurityDescriptor(block, SECURITY_DESCRIPTOR_REVISION);
    }
    if (NT_SUCCESS(status))
        status = RtlSetOwnerSecurityDescriptor(block, owner, FALSE);
    if (NT_SUCCESS(status))
        status = RtlSetGroupSecurityDescriptor(block, group, FALSE);
    if (NT_SUCCESS(status))
        status = RtlSetDaclSecurityDescriptor(block, TRUE, dacl, FALSE);
    if (NT_SUCCESS(status))
        status = RtlSetSaclSecurityDescriptor(block, sacl != nullptr, sacl, FALSE);
    MimeRtlFree(scratch);
    if (!NT_SUCCESS(status)) {
        MimeRtlFree(block);
        return status;
    }
    *result = block;
    return STATUS_SUCCESS;
}
