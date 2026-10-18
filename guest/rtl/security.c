/*
 * sids, access control lists and security descriptors: the rtl routines ntoskrnl.exe and ntdll.dll both
 * export. they validate in the order windows does, return its status codes and lay descriptors out the way
 * it does, so programs that read the bytes see the same ones.
 */
#define NTSYSAPI
#include "rtl.h"
#include <mime/ntrtlsec.h>
#include <stdarg.h>

/* the types that carry a sid right after the mask, as a bit set of ace types. */
#define SID_ACE_TYPES 0x001A660Fu
/* the object ace types, which have a flags field and guids before the sid. */
#define OBJECT_ACE_TYPES 0x000199E0u

#define ACE_LONG_ALIGNED(size) (((size) & 3) == 0)

static const UCHAR g_world_authority[6] = {0, 0, 0, 0, 0, 1};
static const UCHAR g_label_authority[6] = {0, 0, 0, 0, 0, 16};
static const UCHAR g_policy_authority[6] = {0, 0, 0, 0, 0, 17};
static const UCHAR g_trust_authority[6] = {0, 0, 0, 0, 0, 19};

/* ---------------------------------------------------------------- sids */

BOOLEAN NTAPI RtlValidSid(PSID sid)
{
    const SID *s = sid;
    return s && (s->Revision & 0xF) == SID_REVISION && s->SubAuthorityCount <= SID_MAX_SUB_AUTHORITIES;
}

ULONG NTAPI RtlLengthSid(PSID sid)
{
    return 8 + 4 * (ULONG)((const SID *)sid)->SubAuthorityCount;
}

ULONG NTAPI RtlLengthRequiredSid(ULONG count)
{
    return count > 0x3FFFFFF7 ? 0xFFFFFFFF : 8 + 4 * count;
}

BOOLEAN NTAPI RtlEqualSid(PSID a, PSID b)
{
    const SID *x = a, *y = b;
    return x->Revision == y->Revision && x->SubAuthorityCount == y->SubAuthorityCount &&
           memcmp(a, b, 8 + 4 * (ULONG)x->SubAuthorityCount) == 0;
}

/* the same authority and all sub authorities but the last, or the last six of a package sid. */
BOOLEAN NTAPI RtlEqualPrefixSid(PSID a, PSID b)
{
    const SID *x = a, *y = b;
    if (x->Revision != y->Revision || memcmp(&x->IdentifierAuthority, &y->IdentifierAuthority, 6) ||
        x->SubAuthorityCount != y->SubAuthorityCount)
        return FALSE;
    ULONG count = x->SubAuthorityCount;
    if (count == 0)
        return TRUE;
    /* the app container authority of 11 sub authorities leaves out the last five. */
    const UCHAR *au = x->IdentifierAuthority.Value;
    ULONG skip = count == 11 && !au[0] && !au[1] && !au[2] && !au[3] && !au[4] && au[5] == 11 ? 5 : 1;
    return memcmp(x->SubAuthority, y->SubAuthority, 4 * (count - skip)) == 0;
}

NTSTATUS NTAPI RtlCopySid(ULONG length, PSID dst, PSID src)
{
    ULONG n = RtlLengthSid(src);
    if (n > length)
        return STATUS_BUFFER_TOO_SMALL;
    memmove(dst, src, n);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlInitializeSid(PSID sid, PSID_IDENTIFIER_AUTHORITY authority, UCHAR count)
{
    if (count > SID_MAX_SUB_AUTHORITIES)
        return STATUS_INVALID_PARAMETER;
    SID *s = sid;
    s->Revision = SID_REVISION;
    s->SubAuthorityCount = count;
    s->IdentifierAuthority = *authority;
    return STATUS_SUCCESS;
}

/* the count is a byte, but a variadic call may not name a parameter that is promoted. */
NTSTATUS RtlInitializeSidEx(PSID sid, PSID_IDENTIFIER_AUTHORITY authority, ULONG count_argument, ...)
{
    UCHAR count = (UCHAR)count_argument;
    if (count > SID_MAX_SUB_AUTHORITIES)
        return STATUS_INVALID_PARAMETER;
    SID *s = sid;
    s->Revision = SID_REVISION;
    s->SubAuthorityCount = count;
    s->IdentifierAuthority = *authority;
    va_list ap;
    va_start(ap, count_argument);
    for (UCHAR i = 0; i < count; i++)
        s->SubAuthority[i] = va_arg(ap, ULONG);
    va_end(ap);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlAllocateAndInitializeSid(PSID_IDENTIFIER_AUTHORITY authority, UCHAR count, ULONG s0, ULONG s1,
                                           ULONG s2, ULONG s3, ULONG s4, ULONG s5, ULONG s6, ULONG s7, PSID *sid)
{
    if (count > 8)
        return STATUS_INVALID_SID;
    SID *s = MimeRtlAllocate(8 + 4 * (SIZE_T)count);
    if (!s)
        return STATUS_NO_MEMORY;
    const ULONG sub[8] = {s0, s1, s2, s3, s4, s5, s6, s7};
    s->Revision = SID_REVISION;
    s->SubAuthorityCount = count;
    s->IdentifierAuthority = *authority;
    memcpy(s->SubAuthority, sub, 4 * (SIZE_T)count);
    *sid = s;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlAllocateAndInitializeSidEx(PSID_IDENTIFIER_AUTHORITY authority, UCHAR count,
                                             const ULONG *sub_authorities, PSID *sid)
{
    if (count > SID_MAX_SUB_AUTHORITIES)
        return STATUS_INVALID_PARAMETER;
    SID *s = MimeRtlAllocate(8 + 4 * (SIZE_T)count);
    if (!s)
        return STATUS_NO_MEMORY;
    s->Revision = SID_REVISION;
    s->SubAuthorityCount = count;
    s->IdentifierAuthority = *authority;
    memcpy(s->SubAuthority, sub_authorities, 4 * (SIZE_T)count);
    *sid = s;
    return STATUS_SUCCESS;
}

PVOID NTAPI RtlFreeSid(PSID sid)
{
    MimeRtlFree(sid);
    return nullptr;
}

PSID_IDENTIFIER_AUTHORITY NTAPI RtlIdentifierAuthoritySid(PSID sid)
{
    return &((SID *)sid)->IdentifierAuthority;
}

PULONG NTAPI RtlSubAuthoritySid(PSID sid, ULONG index)
{
    return &((SID *)sid)->SubAuthority[index];
}

PUCHAR NTAPI RtlSubAuthorityCountSid(PSID sid)
{
    return &((SID *)sid)->SubAuthorityCount;
}

NTSTATUS NTAPI RtlCopySidAndAttributesArray(ULONG count, PSID_AND_ATTRIBUTES source, ULONG sid_bytes,
                                            PSID_AND_ATTRIBUTES dest, PSID sid_area, PSID *sid_end,
                                            PULONG sid_bytes_left)
{
    UCHAR *area = sid_area;
    for (ULONG i = 0; i < count; i++) {
        ULONG n = RtlLengthSid(source[i].Sid);
        if (n > sid_bytes)
            return STATUS_BUFFER_TOO_SMALL;
        dest[i].Sid = area;
        dest[i].Attributes = source[i].Attributes;
        sid_bytes -= n;
        RtlCopySid(n, area, source[i].Sid);
        area += n;
    }
    *sid_end = area;
    *sid_bytes_left = sid_bytes;
    return STATUS_SUCCESS;
}

static BOOLEAN authority_is(const SID *sid, const UCHAR *authority)
{
    return memcmp(&sid->IdentifierAuthority, authority, 6) == 0;
}

/* the levels of mandatory label sids compare by their one sub authority, the integrity level. */
NTSTATUS NTAPI RtlSidDominates(PSID a, PSID b, PBOOLEAN dominates)
{
    const SID *x = a, *y = b;
    *dominates = FALSE;
    if (!authority_is(x, g_label_authority) || !authority_is(y, g_label_authority))
        return STATUS_INVALID_PARAMETER;
    ULONG xl = x->SubAuthorityCount ? x->SubAuthority[x->SubAuthorityCount - 1] : 0;
    *dominates = RtlEqualSid(a, b) || y->SubAuthorityCount == 0 || xl >= y->SubAuthority[y->SubAuthorityCount - 1];
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlSidIsHigherLevel(PSID a, PSID b, PBOOLEAN higher)
{
    const SID *x = a, *y = b;
    if (!authority_is(x, g_label_authority) || !authority_is(y, g_label_authority))
        return STATUS_INVALID_PARAMETER;
    *higher = x->SubAuthority[0] > y->SubAuthority[0];
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlSidEqualLevel(PSID a, PSID b, PBOOLEAN equal)
{
    const SID *x = a, *y = b;
    if (!authority_is(x, g_label_authority) || !authority_is(y, g_label_authority))
        return STATUS_INVALID_PARAMETER;
    *equal = x->SubAuthority[0] == y->SubAuthority[0];
    return STATUS_SUCCESS;
}

/* ---------------------------------------------------------------- access control lists */

NTSTATUS NTAPI RtlCreateAcl(PACL acl, ULONG length, ULONG revision)
{
    if (length < sizeof(ACL))
        return STATUS_BUFFER_TOO_SMALL;
    if (revision - MIN_ACL_REVISION > MAX_ACL_REVISION - MIN_ACL_REVISION || length > 0xFFFC)
        return STATUS_INVALID_PARAMETER;
    acl->AclRevision = (UCHAR)revision;
    acl->Sbz1 = 0;
    acl->AclSize = (USHORT)(length & 0xFFFC);
    acl->AceCount = 0;
    acl->Sbz2 = 0;
    return STATUS_SUCCESS;
}

static ULONG sid_end_offset(const UCHAR *sid, ULONG at)
{
    return at + 8 + 4 * (ULONG)sid[1];
}

/* whether the sid at the offset of an ace is valid and fits in its size. */
static BOOLEAN ace_sid_fits(const UCHAR *ace, ULONG size, ULONG at)
{
    if (size < at + 8 || ace[at] != SID_REVISION || ace[at + 1] > SID_MAX_SUB_AUTHORITIES)
        return FALSE;
    return size >= sid_end_offset(ace + at, at);
}

static BOOLEAN valid_compound_ace(const UCHAR *ace, ULONG size)
{
    if (!ACE_LONG_ALIGNED(size) || size < 0x18 || *(const USHORT *)(ace + 8) != COMPOUND_ACE_IMPERSONATION)
        return FALSE;
    if (!ace_sid_fits(ace, size, 12))
        return FALSE;
    return ace_sid_fits(ace, size, sid_end_offset(ace + 12, 12));
}

static BOOLEAN valid_object_ace(const UCHAR *ace, ULONG size)
{
    if (!ACE_LONG_ALIGNED(size) || size < 12)
        return FALSE;
    ULONG flags = *(const ULONG *)(ace + 8);
    ULONG guids = 16 * (flags & ACE_OBJECT_TYPE_PRESENT) + 8 * (flags & ACE_INHERITED_OBJECT_TYPE_PRESENT);
    return size >= guids + 24 && ace_sid_fits(ace, size, 12 + guids);
}

BOOLEAN NTAPI RtlValidAcl(PACL acl)
{
    if (!acl || acl->AclRevision < MIN_ACL_REVISION || acl->AclRevision > MAX_ACL_REVISION || ((ULONG_PTR)acl & 1) ||
        acl->AclSize < sizeof(ACL))
        return FALSE;
    const UCHAR *ace = (const UCHAR *)(acl + 1), *end = (const UCHAR *)acl + acl->AclSize;
    for (ULONG i = 0; i < acl->AceCount; i++) {
        if (ace + 4 > end || ((ULONG_PTR)ace & 1))
            return FALSE;
        ULONG size = *(const USHORT *)(ace + 2);
        if (ace + size > end)
            return FALSE;
        UCHAR type = ace[0];
        if (type <= SYSTEM_PROCESS_TRUST_LABEL_ACE_TYPE && (SID_ACE_TYPES >> type & 1)) {
            if (!ACE_LONG_ALIGNED(size) || size < 16 || !ace_sid_fits(ace, size, 8))
                return FALSE;
        } else if (type == ACCESS_ALLOWED_COMPOUND_ACE_TYPE) {
            if (acl->AclRevision < 3 || !valid_compound_ace(ace, size))
                return FALSE;
        } else if (type <= 0x10 && (OBJECT_ACE_TYPES >> type & 1)) {
            if (acl->AclRevision < 4 || !valid_object_ace(ace, size))
                return FALSE;
        } else if (type == SYSTEM_RESOURCE_ATTRIBUTE_ACE_TYPE) {
            /* the claim itself is not checked, only the sid before it. */
            if (!ACE_LONG_ALIGNED(size) || size < 0x10 || ace[8] != SID_REVISION ||
                ace[9] > SID_MAX_SUB_AUTHORITIES || size < 4 * (ULONG)ace[9] + 36)
                return FALSE;
        } else if (type == SYSTEM_ACCESS_FILTER_ACE_TYPE) {
            if (!ACE_LONG_ALIGNED(size) || size < 0x10 || ace[8] != SID_REVISION || ace[9] > SID_MAX_SUB_AUTHORITIES)
                return FALSE;
            ULONG at = 4 * ((ULONG)ace[9] + 2);
            if ((LONG)(size - at - 8) < 6 || memcmp(ace + 8 + at, "artx", 4))
                return FALSE;
        } else if (size < 4) {
            return FALSE;
        }
        ace += size;
    }
    return TRUE;
}

NTSTATUS NTAPI RtlGetAce(PACL acl, ULONG index, PVOID *result)
{
    if (acl->AclRevision < MIN_ACL_REVISION || acl->AclRevision > MAX_ACL_REVISION || index >= acl->AceCount)
        return STATUS_INVALID_PARAMETER;
    UCHAR *ace = (UCHAR *)(acl + 1), *end = (UCHAR *)acl + acl->AclSize;
    for (ULONG i = 0;; i++) {
        *result = ace;
        if (i >= index)
            break;
        if (ace >= end)
            return STATUS_INVALID_PARAMETER;
        ace += *(USHORT *)(ace + 2);
    }
    return ace >= end ? STATUS_INVALID_PARAMETER : STATUS_SUCCESS;
}

/* the first byte after the aces, or NULL when the list runs past its size. */
static UCHAR *first_free(const ACL *acl, BOOLEAN *consistent)
{
    UCHAR *ace = (UCHAR *)(acl + 1);
    const UCHAR *end = (const UCHAR *)acl + acl->AclSize;
    *consistent = TRUE;
    for (ULONG i = 0; i < acl->AceCount; i++) {
        if (ace >= end) {
            *consistent = FALSE;
            return nullptr;
        }
        ace += *(USHORT *)(ace + 2);
    }
    return ace > end ? nullptr : ace;
}

BOOLEAN NTAPI RtlFirstFreeAce(PACL acl, PVOID *ace)
{
    BOOLEAN ok;
    UCHAR *free = first_free(acl, &ok);
    *ace = free;
    return ok;
}

NTSTATUS NTAPI RtlAddAce(PACL acl, ULONG revision, ULONG index, PVOID list, ULONG length)
{
    if (!RtlValidAcl(acl))
        return STATUS_INVALID_PARAMETER;
    BOOLEAN ok;
    UCHAR *free = first_free(acl, &ok);
    if (!ok)
        return STATUS_INVALID_PARAMETER;
    UCHAR *end = (UCHAR *)acl + acl->AclSize;
    UCHAR new_revision = (UCHAR)revision > acl->AclRevision ? (UCHAR)revision : acl->AclRevision;
    /* each ace of the list needs a revision of the acl that knows its type. */
    const UCHAR *ace = list, *list_end = ace + length;
    USHORT added = 0;
    while (ace < list_end) {
        UCHAR type = ace[0];
        ULONG size = *(const USHORT *)(ace + 2);
        if (type > 3) {
            if (type <= 4) {
                if (revision < 3)
                    return STATUS_INVALID_PARAMETER;
            } else if (type <= 8) {
                if (revision < 4)
                    return STATUS_INVALID_PARAMETER;
            } else if (size == 0) {
                return STATUS_INVALID_PARAMETER;
            }
        }
        if (size == 0)
            return STATUS_INVALID_PARAMETER;
        ace += size;
        added++;
    }
    if (ace > list_end)
        return STATUS_INVALID_PARAMETER;
    if (!free || free + length > end)
        return STATUS_BUFFER_TOO_SMALL;
    UCHAR *at = (UCHAR *)(acl + 1);
    for (ULONG i = 0; index != 0 && i < acl->AceCount; i++) {
        at += *(USHORT *)(at + 2);
        if (i + 1 >= index)
            break;
    }
    memmove(at + length, at, free - at);
    memmove(at, list, length);
    acl->AceCount += added;
    acl->AclRevision = new_revision;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlDeleteAce(PACL acl, ULONG index)
{
    if (!RtlValidAcl(acl) || index >= acl->AceCount)
        return STATUS_INVALID_PARAMETER;
    BOOLEAN ok;
    UCHAR *free = first_free(acl, &ok);
    if (!ok)
        return STATUS_INVALID_PARAMETER;
    UCHAR *ace = (UCHAR *)(acl + 1);
    for (ULONG i = 0; i < index; i++)
        ace += *(USHORT *)(ace + 2);
    ULONG size = *(USHORT *)(ace + 2);
    ULONG tail = (ULONG)(free - ace);
    memmove(ace, ace + size, tail - size);
    memset(ace + tail - size, 0, size);
    acl->AceCount--;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlQueryInformationAcl(PACL acl, PVOID info, ULONG length, ACL_INFORMATION_CLASS klass)
{
    if (acl->AclRevision < MIN_ACL_REVISION || acl->AclRevision > MAX_ACL_REVISION)
        return STATUS_INVALID_PARAMETER;
    if (klass == AclRevisionInformation) {
        if (length < sizeof(ULONG))
            return STATUS_BUFFER_TOO_SMALL;
        *(PULONG)info = acl->AclRevision;
        return STATUS_SUCCESS;
    }
    if (klass != AclSizeInformation)
        return STATUS_INVALID_INFO_CLASS;
    if (length < sizeof(ACL_SIZE_INFORMATION))
        return STATUS_BUFFER_TOO_SMALL;
    const UCHAR *ace = (const UCHAR *)(acl + 1), *end = (const UCHAR *)acl + acl->AclSize;
    for (ULONG i = 0; i < acl->AceCount; i++) {
        if (ace >= end)
            return STATUS_INVALID_PARAMETER;
        ace += *(const USHORT *)(ace + 2);
    }
    ACL_SIZE_INFORMATION *size = info;
    size->AceCount = acl->AceCount;
    if (ace > end) {
        size->AclBytesInUse = acl->AclSize;
        size->AclBytesFree = 0;
    } else {
        size->AclBytesInUse = (ULONG)(ace - (const UCHAR *)acl);
        size->AclBytesFree = acl->AclSize - size->AclBytesInUse;
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlSetInformationAcl(PACL acl, PVOID info, ULONG length, ACL_INFORMATION_CLASS klass)
{
    if (acl->AclRevision < MIN_ACL_REVISION || acl->AclRevision > MAX_ACL_REVISION)
        return STATUS_INVALID_PARAMETER;
    if (klass != AclRevisionInformation)
        return STATUS_INVALID_INFO_CLASS;
    if (length < sizeof(ULONG))
        return STATUS_BUFFER_TOO_SMALL;
    if (*(PULONG)info < acl->AclRevision)
        return STATUS_INVALID_PARAMETER;
    acl->AclRevision = (UCHAR)*(PULONG)info;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlGetAcesBufferSize(PACL acl, PULONG size)
{
    if (!acl || !size)
        return STATUS_INVALID_PARAMETER;
    const UCHAR *ace = (const UCHAR *)(acl + 1);
    ULONG total = 0;
    for (ULONG i = 0; i < acl->AceCount; i++) {
        ULONG n = *(const USHORT *)(ace + 2);
        total += n;
        ace += n;
    }
    *size = total;
    return STATUS_SUCCESS;
}

PVOID NTAPI RtlFindAceByType(PACL acl, UCHAR type, PULONG index)
{
    if (!acl)
        return nullptr;
    UCHAR *ace = (UCHAR *)(acl + 1);
    for (ULONG i = 0; i < acl->AceCount; i++) {
        if (index ? i >= *index && ace[0] == type : ace[0] == type) {
            if (index)
                *index = i;
            return ace;
        }
        ace += *(USHORT *)(ace + 2);
    }
    return nullptr;
}

BOOLEAN NTAPI RtlOwnerAcesPresent(PACL acl)
{
    static const UCHAR owner_rights[12] = {1, 1, 0, 0, 0, 0, 0, 3, 4, 0, 0, 0};
    if (!acl)
        return FALSE;
    const UCHAR *ace = (const UCHAR *)(acl + 1);
    for (ULONG i = 0; i < acl->AceCount; ace += *(const USHORT *)(ace + 2), i++) {
        if (ace[1] & INHERIT_ONLY_ACE)
            continue;
        UCHAR type = ace[0];
        const UCHAR *sid;
        if (type == 5 || type == 6 || type == 7 || type == 8 || type == 11 || type == 12 || type == 15 || type == 16) {
            ULONG flags = *(const ULONG *)(ace + 8);
            sid = ace + 12 + 16 * (flags & ACE_OBJECT_TYPE_PRESENT) + 8 * (flags & ACE_INHERITED_OBJECT_TYPE_PRESENT);
        } else if (type == ACCESS_ALLOWED_COMPOUND_ACE_TYPE) {
            sid = ace + 12;
        } else if (type <= 3 || type == 9 || type == 10 || type == 13 || type == 14) {
            sid = ace + 8;
        } else {
            continue;
        }
        if (!memcmp(sid, owner_rights, sizeof(owner_rights)))
            return TRUE;
    }
    return FALSE;
}

BOOLEAN NTAPI RtlAreAllAccessesGranted(ACCESS_MASK granted, ACCESS_MASK desired)
{
    return (desired & ~granted) == 0;
}

BOOLEAN NTAPI RtlAreAnyAccessesGranted(ACCESS_MASK granted, ACCESS_MASK desired)
{
    return (granted & desired) != 0;
}

VOID NTAPI RtlMapGenericMask(PACCESS_MASK mask, const GENERIC_MAPPING *mapping)
{
    ACCESS_MASK m = *mask;
    if (m & GENERIC_READ)
        m |= mapping->GenericRead;
    if (m & GENERIC_WRITE)
        m |= mapping->GenericWrite;
    if (m & GENERIC_EXECUTE)
        m |= mapping->GenericExecute;
    if (m & GENERIC_ALL)
        m |= mapping->GenericAll;
    *mask = m & 0x0FFFFFFF;
}

/*
 * the aces below share their checks: the sid, the revisions of the list and of the call, the flags, then the
 * list itself and its room, always in that order.
 */
static NTSTATUS revision_of(const ACL *acl, ULONG revision, UCHAR *result)
{
    if (acl->AclRevision > MAX_ACL_REVISION || revision > MAX_ACL_REVISION)
        return STATUS_REVISION_MISMATCH;
    *result = (UCHAR)revision > acl->AclRevision ? (UCHAR)revision : acl->AclRevision;
    return STATUS_SUCCESS;
}

static NTSTATUS room_for(PACL acl, ULONG size, UCHAR **slot)
{
    if (!RtlValidAcl(acl))
        return STATUS_INVALID_ACL;
    BOOLEAN ok;
    UCHAR *free = first_free(acl, &ok);
    if (!ok)
        return STATUS_INVALID_ACL;
    if (!free || free + size > (UCHAR *)acl + acl->AclSize)
        return STATUS_ALLOTTED_SPACE_EXCEEDED;
    *slot = free;
    return STATUS_SUCCESS;
}

static void put_header(UCHAR *ace, UCHAR type, ULONG flags, ULONG size, ACCESS_MASK mask)
{
    ace[0] = type;
    ace[1] = (UCHAR)flags;
    *(USHORT *)(ace + 2) = (USHORT)size;
    *(ACCESS_MASK *)(ace + 4) = mask;
}

static NTSTATUS add_known(PACL acl, ULONG revision, ULONG flags, ACCESS_MASK mask, PSID sid, UCHAR type)
{
    if (!RtlValidSid(sid))
        return STATUS_INVALID_SID;
    UCHAR new_revision;
    NTSTATUS status = revision_of(acl, revision, &new_revision);
    if (!NT_SUCCESS(status))
        return status;
    if (flags & ~VALID_INHERIT_FLAGS) {
        /* the critical flag is for allow aces, the success and failure ones for audit aces. */
        ULONG extra = type == ACCESS_ALLOWED_ACE_TYPE ? flags & ~0x3Fu
                      : type == SYSTEM_AUDIT_ACE_TYPE ? flags & ~0xDFu
                                                      : flags & ~VALID_INHERIT_FLAGS;
        if (extra)
            return STATUS_INVALID_PARAMETER;
    }
    UCHAR *slot;
    ULONG size = 8 + RtlLengthSid(sid);
    if (!NT_SUCCESS(status = room_for(acl, size, &slot)))
        return status;
    put_header(slot, type, flags, size, mask);
    memmove(slot + 8, sid, size - 8);
    acl->AceCount++;
    acl->AclRevision = new_revision;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlAddAccessAllowedAce(PACL acl, ULONG revision, ACCESS_MASK mask, PSID sid)
{
    return add_known(acl, revision, 0, mask, sid, ACCESS_ALLOWED_ACE_TYPE);
}

NTSTATUS NTAPI RtlAddAccessAllowedAceEx(PACL acl, ULONG revision, ULONG flags, ACCESS_MASK mask, PSID sid)
{
    return add_known(acl, revision, flags, mask, sid, ACCESS_ALLOWED_ACE_TYPE);
}

NTSTATUS NTAPI RtlAddAccessDeniedAce(PACL acl, ULONG revision, ACCESS_MASK mask, PSID sid)
{
    return add_known(acl, revision, 0, mask, sid, ACCESS_DENIED_ACE_TYPE);
}

NTSTATUS NTAPI RtlAddAccessDeniedAceEx(PACL acl, ULONG revision, ULONG flags, ACCESS_MASK mask, PSID sid)
{
    return add_known(acl, revision, flags, mask, sid, ACCESS_DENIED_ACE_TYPE);
}

NTSTATUS NTAPI RtlAddAuditAccessAce(PACL acl, ULONG revision, ACCESS_MASK mask, PSID sid, BOOLEAN success,
                                    BOOLEAN failure)
{
    ULONG flags = (success ? SUCCESSFUL_ACCESS_ACE_FLAG : 0) | (failure ? FAILED_ACCESS_ACE_FLAG : 0);
    return add_known(acl, revision, flags, mask, sid, SYSTEM_AUDIT_ACE_TYPE);
}

NTSTATUS NTAPI RtlAddAuditAccessAceEx(PACL acl, ULONG revision, ULONG flags, ACCESS_MASK mask, PSID sid,
                                      BOOLEAN success, BOOLEAN failure)
{
    flags |= (success ? SUCCESSFUL_ACCESS_ACE_FLAG : 0) | (failure ? FAILED_ACCESS_ACE_FLAG : 0);
    return add_known(acl, revision, flags, mask, sid, SYSTEM_AUDIT_ACE_TYPE);
}

static NTSTATUS add_object(PACL acl, ULONG revision, ULONG flags, ACCESS_MASK mask, const GUID *object,
                           const GUID *inherited, PSID sid, UCHAR type)
{
    if (!RtlValidSid(sid))
        return STATUS_INVALID_SID;
    if (acl->AclRevision > MAX_ACL_REVISION || revision != 4)
        return STATUS_REVISION_MISMATCH;
    if ((flags & ~VALID_INHERIT_FLAGS) && (type != SYSTEM_AUDIT_OBJECT_ACE_TYPE || (flags & ~0xDFu)))
        return STATUS_INVALID_PARAMETER;
    ULONG size = 12 + RtlLengthSid(sid) + (object ? 16 : 0) + (inherited ? 16 : 0);
    UCHAR *slot;
    NTSTATUS status = room_for(acl, size, &slot);
    if (!NT_SUCCESS(status))
        return status;
    put_header(slot, type, flags, size, mask);
    *(ULONG *)(slot + 8) = (object ? ACE_OBJECT_TYPE_PRESENT : 0) | (inherited ? ACE_INHERITED_OBJECT_TYPE_PRESENT : 0);
    UCHAR *at = slot + 12;
    if (object) {
        memcpy(at, object, 16);
        at += 16;
    }
    if (inherited) {
        memcpy(at, inherited, 16);
        at += 16;
    }
    memmove(at, sid, RtlLengthSid(sid));
    acl->AceCount++;
    acl->AclRevision = 4;
    return STATUS_SUCCESS;
}

/* without a guid an object ace is the plain ace. */
NTSTATUS NTAPI RtlAddAccessAllowedObjectAce(PACL acl, ULONG revision, ULONG flags, ACCESS_MASK mask,
                                            const GUID *object, const GUID *inherited, PSID sid)
{
    if (object || inherited)
        return add_object(acl, revision, flags, mask, object, inherited, sid, ACCESS_ALLOWED_OBJECT_ACE_TYPE);
    return add_known(acl, revision, flags, mask, sid, ACCESS_ALLOWED_ACE_TYPE);
}

NTSTATUS NTAPI RtlAddAccessDeniedObjectAce(PACL acl, ULONG revision, ULONG flags, ACCESS_MASK mask,
                                           const GUID *object, const GUID *inherited, PSID sid)
{
    if (object || inherited)
        return add_object(acl, revision, flags, mask, object, inherited, sid, ACCESS_DENIED_OBJECT_ACE_TYPE);
    return add_known(acl, revision, flags, mask, sid, ACCESS_DENIED_ACE_TYPE);
}

NTSTATUS NTAPI RtlAddAuditAccessObjectAce(PACL acl, ULONG revision, ULONG flags, ACCESS_MASK mask,
                                          const GUID *object, const GUID *inherited, PSID sid, BOOLEAN success,
                                          BOOLEAN failure)
{
    flags |= (success ? SUCCESSFUL_ACCESS_ACE_FLAG : 0) | (failure ? FAILED_ACCESS_ACE_FLAG : 0);
    if (object || inherited)
        return add_object(acl, revision, flags, mask, object, inherited, sid, SYSTEM_AUDIT_OBJECT_ACE_TYPE);
    return add_known(acl, revision, flags, mask, sid, SYSTEM_AUDIT_ACE_TYPE);
}

NTSTATUS NTAPI RtlAddMandatoryAce(PACL acl, ULONG revision, ULONG flags, PSID sid, UCHAR type, ACCESS_MASK mask)
{
    if (!acl)
        return STATUS_INVALID_ACL;
    if (type != SYSTEM_MANDATORY_LABEL_ACE_TYPE)
        return STATUS_INVALID_PARAMETER;
    if (!RtlValidSid(sid))
        return STATUS_INVALID_SID;
    if (!authority_is(sid, g_label_authority))
        return STATUS_INVALID_PARAMETER;
    UCHAR new_revision;
    NTSTATUS status = revision_of(acl, revision, &new_revision);
    if (!NT_SUCCESS(status))
        return status;
    if ((flags & ~VALID_INHERIT_FLAGS) || (mask & ~7u))
        return STATUS_INVALID_PARAMETER;
    UCHAR *slot;
    ULONG size = 8 + RtlLengthSid(sid);
    if (!NT_SUCCESS(status = room_for(acl, size, &slot)))
        return status;
    put_header(slot, type, flags, size, mask);
    memmove(slot + 8, sid, size - 8);
    acl->AceCount++;
    acl->AclRevision = new_revision;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlAddCompoundAce(PACL acl, ULONG revision, UCHAR type, ACCESS_MASK mask, PSID server, PSID client)
{
    if (!RtlValidSid(server) || !RtlValidSid(client))
        return STATUS_INVALID_SID;
    if (acl->AclRevision > MAX_ACL_REVISION || revision - 3 > 1)
        return STATUS_REVISION_MISMATCH;
    ULONG server_size = RtlLengthSid(server), client_size = RtlLengthSid(client);
    ULONG size = 12 + server_size + client_size;
    UCHAR *slot;
    NTSTATUS status = room_for(acl, size, &slot);
    if (!NT_SUCCESS(status))
        return status;
    put_header(slot, ACCESS_ALLOWED_COMPOUND_ACE_TYPE, 0, size, mask);
    *(USHORT *)(slot + 8) = type;
    *(USHORT *)(slot + 10) = 0;
    memmove(slot + 12, server, server_size);
    memmove(slot + 12 + server_size, client, client_size);
    acl->AceCount++;
    if (acl->AclRevision < revision)
        acl->AclRevision = (UCHAR)revision;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlAddScopedPolicyIDAce(PACL acl, ULONG revision, ULONG flags, ACCESS_MASK mask, PSID sid)
{
    if (!acl)
        return STATUS_INVALID_ACL;
    if (!RtlValidSid(sid))
        return STATUS_INVALID_SID;
    if (!authority_is(sid, g_policy_authority))
        return STATUS_INVALID_PARAMETER;
    UCHAR new_revision;
    NTSTATUS status = revision_of(acl, revision, &new_revision);
    if (!NT_SUCCESS(status))
        return status;
    if ((flags & ~VALID_INHERIT_FLAGS) || mask)
        return STATUS_INVALID_PARAMETER;
    UCHAR *slot;
    ULONG size = 8 + RtlLengthSid(sid);
    if (!NT_SUCCESS(status = room_for(acl, size, &slot)))
        return status;
    put_header(slot, SYSTEM_SCOPED_POLICY_ID_ACE_TYPE, flags, size, 0);
    memmove(slot + 8, sid, size - 8);
    acl->AceCount++;
    acl->AclRevision = new_revision;
    return STATUS_SUCCESS;
}

/*
 * the attribute of a resource attribute ace, checked, and its size once it is written after the sid in the relative
 * form: offsets of the name and of each value up front, the name, then the values one after the other.
 */
typedef struct {
    PWSTR Name;
    USHORT Type;
    USHORT Reserved;
    ULONG Flags;
    ULONG Count;
    PVOID Values;
} ATTRIBUTE_V1;

typedef struct {
    PVOID Value;
    ULONG Length;
} ATTRIBUTE_OCTETS;

#ifndef STATUS_INTEGER_OVERFLOW
#define STATUS_INTEGER_OVERFLOW ((NTSTATUS)0xC0000095L)
#endif

#define ATTRIBUTE_INT64 1
#define ATTRIBUTE_UINT64 2
#define ATTRIBUTE_STRING 3
#define ATTRIBUTE_SID 5
#define ATTRIBUTE_BOOLEAN 6
#define ATTRIBUTE_OCTET_STRING 0x10

static ULONG string_chars(PCWSTR s, ULONG limit)
{
    ULONG n = 0;
    while (n < limit && s[n])
        n++;
    return n < limit ? n : limit;
}

static BOOLEAN attribute_valid(const ATTRIBUTE_V1 *a)
{
    if (!a || !a->Name || a->Reserved || (a->Flags & 0xFFC0) || !a->Count || !a->Values)
        return FALSE;
    ULONG n = string_chars(a->Name, 0x8000);
    if (!n || n == 0x8000)
        return FALSE;
    switch (a->Type) {
    case ATTRIBUTE_INT64:
    case ATTRIBUTE_UINT64:
        return TRUE;
    case ATTRIBUTE_STRING:
        for (ULONG i = 0; i < a->Count; i++)
            if (!((PWSTR *)a->Values)[i])
                return FALSE;
        return TRUE;
    case ATTRIBUTE_SID:
        for (ULONG i = 0; i < a->Count; i++)
            if (!((ATTRIBUTE_OCTETS *)a->Values)[i].Value || !((ATTRIBUTE_OCTETS *)a->Values)[i].Length)
                return FALSE;
        return TRUE;
    case ATTRIBUTE_BOOLEAN:
        for (ULONG i = 0; i < a->Count; i++)
            if (((ULONG64 *)a->Values)[i] >= 2)
                return FALSE;
        return TRUE;
    case ATTRIBUTE_OCTET_STRING:
        for (ULONG i = 0; i < a->Count; i++) {
            const ATTRIBUTE_OCTETS *o = &((ATTRIBUTE_OCTETS *)a->Values)[i];
            if (o->Value ? !o->Length : o->Length)
                return FALSE;
        }
        return TRUE;
    default:
        return FALSE;
    }
}

/* the bytes of the relative form, or the status that says why it does not exist. dst is NULL to only measure. */
static NTSTATUS attribute_relative(const ATTRIBUTE_V1 *a, UCHAR *dst, ULONG *size)
{
    ULONG64 total = 16 + 4 * (ULONG64)a->Count;
    ULONG name_bytes = (string_chars(a->Name, 0x8000) + 1) * sizeof(WCHAR);
    ULONG64 at = total + name_bytes;
    total = at;
    for (ULONG i = 0; i < a->Count; i++) {
        ULONG64 bytes = 0;
        switch (a->Type) {
        case ATTRIBUTE_INT64:
        case ATTRIBUTE_UINT64:
        case ATTRIBUTE_BOOLEAN:
            bytes = 8;
            break;
        case ATTRIBUTE_STRING: {
            ULONG n = string_chars(((PWSTR *)a->Values)[i], 0x8000);
            if (n == 0x8000)
                return STATUS_INVALID_PARAMETER;
            bytes = (n + 1) * sizeof(WCHAR);
            break;
        }
        default:
            bytes = 4 + (ULONG64)((ATTRIBUTE_OCTETS *)a->Values)[i].Length;
            break;
        }
        if (dst) {
            *(ULONG *)(dst + 16 + 4 * i) = (ULONG)at;
            switch (a->Type) {
            case ATTRIBUTE_STRING:
                memcpy(dst + at, ((PWSTR *)a->Values)[i], bytes);
                break;
            case ATTRIBUTE_SID:
            case ATTRIBUTE_OCTET_STRING: {
                const ATTRIBUTE_OCTETS *o = &((ATTRIBUTE_OCTETS *)a->Values)[i];
                memcpy(dst + at, &o->Length, 4);
                if (o->Length)
                    memcpy(dst + at + 4, o->Value, o->Length);
                break;
            }
            default:
                memcpy(dst + at, &((ULONG64 *)a->Values)[i], 8);
                break;
            }
        }
        at += bytes;
        total = at;
    }
    if (total > 0xFFFFFFF0ull)
        return STATUS_INTEGER_OVERFLOW;
    *size = (ULONG)((total + 3) & ~3ull);
    if (dst) {
        memset(dst + total, 0, *size - total);
        *(ULONG *)dst = 16 + 4 * a->Count;
        *(USHORT *)(dst + 4) = a->Type;
        *(USHORT *)(dst + 6) = 0;
        *(ULONG *)(dst + 8) = a->Flags;
        *(ULONG *)(dst + 12) = a->Count;
        memcpy(dst + 16 + 4 * a->Count, a->Name, name_bytes);
    }
    return STATUS_SUCCESS;
}

/*
 * adds a resource attribute ace, which holds one attribute after the sid of everyone. the length that comes back is
 * the bytes the list uses with the new ace, and when the list is too small the bytes it needs.
 */
NTSTATUS NTAPI RtlAddResourceAttributeAce(PACL acl, ULONG revision, ULONG flags, ACCESS_MASK mask, PSID sid,
                                          PVOID information, PULONG length)
{
    typedef struct {
        USHORT Version;
        USHORT Reserved;
        ULONG Count;
        ATTRIBUTE_V1 *Attributes;
    } INFORMATION;
    const INFORMATION *info = information;
    if (!length)
        return STATUS_INVALID_PARAMETER;
    *length = 0;
    if (!acl)
        return STATUS_INVALID_ACL;
    if (!RtlValidSid(sid))
        return STATUS_INVALID_SID;
    const SID *everyone = sid;
    if (!authority_is(everyone, g_world_authority) || everyone->SubAuthorityCount != 1 || everyone->SubAuthority[0])
        return STATUS_INVALID_PARAMETER;
    UCHAR new_revision;
    NTSTATUS status = revision_of(acl, revision, &new_revision);
    if (!NT_SUCCESS(status))
        return status;
    if ((flags & ~VALID_INHERIT_FLAGS) || mask || !info || info->Version != 1 || info->Reserved || !info->Count)
        return STATUS_INVALID_PARAMETER;
    for (ULONG i = 0; i < info->Count; i++)
        if (!attribute_valid(&info->Attributes[i]))
            return STATUS_INVALID_PARAMETER;
    if (info->Count != 1)
        return STATUS_INVALID_PARAMETER;
    ULONG data;
    status = attribute_relative(info->Attributes, nullptr, &data);
    if (!NT_SUCCESS(status))
        return status;
    if (!RtlValidAcl(acl))
        return STATUS_INVALID_ACL;
    BOOLEAN ok;
    UCHAR *free = first_free(acl, &ok);
    if (!ok)
        return STATUS_INVALID_ACL;
    ULONG ace_size = 16 + 4 * (ULONG)everyone->SubAuthorityCount + data;
    if (ace_size > 0xFFFF)
        return STATUS_INTEGER_OVERFLOW;
    ULONG used = sizeof(ACL);
    const UCHAR *ace = (const UCHAR *)(acl + 1);
    for (ULONG i = 0; i < acl->AceCount; i++) {
        used += *(const USHORT *)(ace + 2);
        ace += *(const USHORT *)(ace + 2);
    }
    *length = used + ace_size;
    if (!free || free + ace_size > (UCHAR *)acl + acl->AclSize) {
        *length = (used + ace_size + 3) & ~3u;
        return STATUS_ALLOTTED_SPACE_EXCEEDED;
    }
    put_header(free, SYSTEM_RESOURCE_ATTRIBUTE_ACE_TYPE, flags, ace_size, 0);
    RtlCopySid(RtlLengthSid(sid), free + 8, sid);
    attribute_relative(info->Attributes, free + 16 + 4 * everyone->SubAuthorityCount, &data);
    acl->AceCount++;
    acl->AclRevision = new_revision;
    return STATUS_SUCCESS;
}

/* a process trust label sid has the trust authority and two sub authorities, the trust type and the level. */
static BOOLEAN valid_trust_label_sid(const SID *sid)
{
    if (sid->SubAuthorityCount != 2 || sid->Revision != SID_REVISION || !authority_is(sid, g_trust_authority))
        return FALSE;
    return sid->SubAuthority[0] != 0 || sid->SubAuthority[1] == 0;
}

BOOLEAN NTAPI RtlIsValidProcessTrustLabelSid(PSID sid)
{
    return valid_trust_label_sid(sid);
}

NTSTATUS NTAPI RtlSidDominatesForTrust(PSID a, PSID b, PBOOLEAN dominates)
{
    const SID *x = a, *y = b;
    *dominates = FALSE;
    if (x && !valid_trust_label_sid(x))
        return STATUS_INVALID_PARAMETER;
    if (!y) {
        *dominates = TRUE;
        return STATUS_SUCCESS;
    }
    if (!valid_trust_label_sid(y))
        return STATUS_INVALID_PARAMETER;
    if (!x)
        *dominates = y->SubAuthority[0] == 0;
    else
        *dominates = x->SubAuthority[0] >= y->SubAuthority[0] && x->SubAuthority[1] >= y->SubAuthority[1];
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlAddProcessTrustLabelAce(PACL acl, ULONG revision, ULONG flags, PSID sid, UCHAR type,
                                          ACCESS_MASK mask)
{
    if (!acl || !RtlValidAcl(acl))
        return STATUS_INVALID_ACL;
    if (type != SYSTEM_PROCESS_TRUST_LABEL_ACE_TYPE)
        return STATUS_INVALID_PARAMETER;
    if (!RtlValidSid(sid))
        return STATUS_INVALID_SID;
    if (!valid_trust_label_sid(sid))
        return STATUS_INVALID_PARAMETER;
    UCHAR new_revision;
    NTSTATUS status = revision_of(acl, revision, &new_revision);
    if (!NT_SUCCESS(status))
        return status;
    if ((flags & ~VALID_INHERIT_FLAGS) || (mask & 0xFF000000u))
        return STATUS_INVALID_PARAMETER;
    UCHAR *slot;
    ULONG size = 8 + RtlLengthSid(sid);
    if (!NT_SUCCESS(status = room_for(acl, size, &slot)))
        return status;
    put_header(slot, type, flags, size, mask);
    memmove(slot + 8, sid, size - 8);
    acl->AceCount++;
    acl->AclRevision = new_revision;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlAddAccessFilterAce(PACL acl, ULONG revision, ULONG flags, PSID sid, UCHAR type, ACCESS_MASK mask,
                                     PVOID condition, USHORT condition_length)
{
    if (!acl || !RtlValidAcl(acl))
        return STATUS_INVALID_ACL;
    if (!condition || condition_length < 6 || condition_length == 0xFFFF || memcmp(condition, "artx", 4) || type != SYSTEM_ACCESS_FILTER_ACE_TYPE)
        return STATUS_INVALID_PARAMETER;
    if (!RtlValidSid(sid))
        return STATUS_INVALID_SID;
    const SID *s = sid;
    if (flags & 0x40) {
        if (!valid_trust_label_sid(s))
            return STATUS_INVALID_PARAMETER;
    } else if (!authority_is(s, (const UCHAR[6]){0, 0, 0, 0, 0, 1}) || s->SubAuthorityCount != 1 ||
               s->SubAuthority[0] != 0) {
        return STATUS_INVALID_PARAMETER;
    }
    UCHAR new_revision;
    NTSTATUS status = revision_of(acl, revision, &new_revision);
    if (!NT_SUCCESS(status))
        return status;
    if ((flags & ~0x5Fu) || (mask & 0xFF000000u))
        return STATUS_INVALID_PARAMETER;
    ULONG padded = ((ULONG)condition_length + 3) & ~3u;
    ULONG size = 8 + RtlLengthSid(sid) + padded;
    if (size > 0xFFFF)
        return STATUS_INVALID_PARAMETER;
    UCHAR *slot;
    if (!NT_SUCCESS(status = room_for(acl, size, &slot)))
        return status;
    put_header(slot, type, flags, size, mask);
    memmove(slot + 8, sid, RtlLengthSid(sid));
    memcpy(slot + 8 + RtlLengthSid(sid), condition, condition_length);
    memset(slot + 8 + RtlLengthSid(sid) + condition_length, 0, padded - condition_length);
    acl->AceCount++;
    acl->AclRevision = new_revision;
    return STATUS_SUCCESS;
}

/* ---------------------------------------------------------------- security descriptors */

/* the parts of a descriptor of either form, without looking at the present flags. */
static PSID sd_owner(const SECURITY_DESCRIPTOR *sd)
{
    if (sd->Control & SE_SELF_RELATIVE) {
        const SECURITY_DESCRIPTOR_RELATIVE *r = (const void *)sd;
        return r->Owner ? (PSID)((PUCHAR)sd + r->Owner) : nullptr;
    }
    return sd->Owner;
}

static PSID sd_group(const SECURITY_DESCRIPTOR *sd)
{
    if (sd->Control & SE_SELF_RELATIVE) {
        const SECURITY_DESCRIPTOR_RELATIVE *r = (const void *)sd;
        return r->Group ? (PSID)((PUCHAR)sd + r->Group) : nullptr;
    }
    return sd->Group;
}

static PACL sd_sacl(const SECURITY_DESCRIPTOR *sd)
{
    if (sd->Control & SE_SELF_RELATIVE) {
        const SECURITY_DESCRIPTOR_RELATIVE *r = (const void *)sd;
        return r->Sacl ? (PACL)((PUCHAR)sd + r->Sacl) : nullptr;
    }
    return sd->Sacl;
}

static PACL sd_dacl(const SECURITY_DESCRIPTOR *sd)
{
    if (sd->Control & SE_SELF_RELATIVE) {
        const SECURITY_DESCRIPTOR_RELATIVE *r = (const void *)sd;
        return r->Dacl ? (PACL)((PUCHAR)sd + r->Dacl) : nullptr;
    }
    return sd->Dacl;
}

NTSTATUS NTAPI RtlCreateSecurityDescriptor(PSECURITY_DESCRIPTOR sd, ULONG revision)
{
    if (revision != SECURITY_DESCRIPTOR_REVISION)
        return STATUS_UNKNOWN_REVISION;
    memset(sd, 0, sizeof(SECURITY_DESCRIPTOR));
    ((SECURITY_DESCRIPTOR *)sd)->Revision = SECURITY_DESCRIPTOR_REVISION;
    return STATUS_SUCCESS;
}

BOOLEAN NTAPI RtlValidSecurityDescriptor(PSECURITY_DESCRIPTOR sd)
{
    const SECURITY_DESCRIPTOR *s = sd;
    if (s->Revision != SECURITY_DESCRIPTOR_REVISION)
        return FALSE;
    PSID owner = sd_owner(s), group = sd_group(s);
    if ((owner && !RtlValidSid(owner)) || (group && !RtlValidSid(group)))
        return FALSE;
    if (s->Control & SE_DACL_PRESENT) {
        PACL dacl = sd_dacl(s);
        if (dacl && !RtlValidAcl(dacl))
            return FALSE;
    }
    if (s->Control & SE_SACL_PRESENT) {
        PACL sacl = sd_sacl(s);
        if (sacl && !RtlValidAcl(sacl))
            return FALSE;
    }
    return TRUE;
}

/* a self relative descriptor of a known length: every part must lie inside it, aligned, whole and valid. */
static BOOLEAN relative_sid_fits(const UCHAR *base, ULONG length, ULONG offset)
{
    if (offset >= length || offset < sizeof(SECURITY_DESCRIPTOR_RELATIVE) || (offset & 3))
        return FALSE;
    ULONG left = length - offset;
    return left >= 12 && base[offset] == SID_REVISION && base[offset + 1] <= SID_MAX_SUB_AUTHORITIES &&
           left >= 4 * (ULONG)base[offset + 1] + 8;
}

static BOOLEAN relative_acl_fits(const UCHAR *base, ULONG length, ULONG offset)
{
    if (offset >= length || offset < sizeof(SECURITY_DESCRIPTOR_RELATIVE) || (offset & 3))
        return FALSE;
    ULONG left = length - offset;
    return left >= 8 && left >= *(const USHORT *)(base + offset + 2) && RtlValidAcl((PACL)(base + offset));
}

BOOLEAN NTAPI RtlValidRelativeSecurityDescriptor(PSECURITY_DESCRIPTOR sd, ULONG length, SECURITY_INFORMATION required)
{
    const SECURITY_DESCRIPTOR_RELATIVE *r = sd;
    if (length < sizeof(*r) || r->Revision != SECURITY_DESCRIPTOR_REVISION || !(r->Control & SE_SELF_RELATIVE))
        return FALSE;
    const UCHAR *base = sd;
    if (r->Owner ? !relative_sid_fits(base, length, r->Owner) : (required & OWNER_SECURITY_INFORMATION))
        return FALSE;
    if (r->Group ? !relative_sid_fits(base, length, r->Group) : (required & GROUP_SECURITY_INFORMATION))
        return FALSE;
    if ((r->Control & SE_DACL_PRESENT) && r->Dacl && !relative_acl_fits(base, length, r->Dacl))
        return FALSE;
    if ((r->Control & SE_SACL_PRESENT) && r->Sacl && !relative_acl_fits(base, length, r->Sacl))
        return FALSE;
    return TRUE;
}

static ULONG padded_sid(PSID sid)
{
    return sid ? (4 * (ULONG)((const SID *)sid)->SubAuthorityCount + 11) & ~3u : 0;
}

static ULONG padded_acl(PACL acl)
{
    return acl ? ((ULONG)acl->AclSize + 3) & ~3u : 0;
}

ULONG NTAPI RtlLengthSecurityDescriptor(PSECURITY_DESCRIPTOR sd)
{
    const SECURITY_DESCRIPTOR *s = sd;
    ULONG length = s->Control & SE_SELF_RELATIVE ? sizeof(SECURITY_DESCRIPTOR_RELATIVE) : sizeof(SECURITY_DESCRIPTOR);
    length += padded_sid(sd_owner(s)) + padded_sid(sd_group(s));
    if (s->Control & SE_DACL_PRESENT)
        length += padded_acl(sd_dacl(s));
    if (s->Control & SE_SACL_PRESENT)
        length += padded_acl(sd_sacl(s));
    return length;
}

NTSTATUS NTAPI RtlGetOwnerSecurityDescriptor(PSECURITY_DESCRIPTOR sd, PSID *owner, PBOOLEAN defaulted)
{
    const SECURITY_DESCRIPTOR *s = sd;
    if (s->Revision != SECURITY_DESCRIPTOR_REVISION)
        return STATUS_UNKNOWN_REVISION;
    *owner = sd_owner(s);
    *defaulted = s->Control & SE_OWNER_DEFAULTED;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlSetOwnerSecurityDescriptor(PSECURITY_DESCRIPTOR sd, PSID owner, BOOLEAN defaulted)
{
    SECURITY_DESCRIPTOR *s = sd;
    if (s->Revision != SECURITY_DESCRIPTOR_REVISION)
        return STATUS_UNKNOWN_REVISION;
    if (s->Control & SE_SELF_RELATIVE)
        return STATUS_INVALID_SECURITY_DESCR;
    s->Owner = owner;
    s->Control &= ~SE_OWNER_DEFAULTED;
    if (defaulted)
        s->Control |= SE_OWNER_DEFAULTED;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlGetGroupSecurityDescriptor(PSECURITY_DESCRIPTOR sd, PSID *group, PBOOLEAN defaulted)
{
    const SECURITY_DESCRIPTOR *s = sd;
    if (s->Revision != SECURITY_DESCRIPTOR_REVISION)
        return STATUS_UNKNOWN_REVISION;
    *group = sd_group(s);
    *defaulted = (s->Control & SE_GROUP_DEFAULTED) != 0;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlSetGroupSecurityDescriptor(PSECURITY_DESCRIPTOR sd, PSID group, BOOLEAN defaulted)
{
    SECURITY_DESCRIPTOR *s = sd;
    if (s->Revision != SECURITY_DESCRIPTOR_REVISION)
        return STATUS_UNKNOWN_REVISION;
    if (s->Control & SE_SELF_RELATIVE)
        return STATUS_INVALID_SECURITY_DESCR;
    s->Group = group;
    s->Control &= ~SE_GROUP_DEFAULTED;
    if (defaulted)
        s->Control |= SE_GROUP_DEFAULTED;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlGetDaclSecurityDescriptor(PSECURITY_DESCRIPTOR sd, PBOOLEAN present, PACL *dacl,
                                            PBOOLEAN defaulted)
{
    const SECURITY_DESCRIPTOR *s = sd;
    if (s->Revision != SECURITY_DESCRIPTOR_REVISION)
        return STATUS_UNKNOWN_REVISION;
    *present = (s->Control & SE_DACL_PRESENT) != 0;
    if (*present) {
        *dacl = sd_dacl(s);
        *defaulted = (s->Control & SE_DACL_DEFAULTED) != 0;
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlSetDaclSecurityDescriptor(PSECURITY_DESCRIPTOR sd, BOOLEAN present, PACL dacl, BOOLEAN defaulted)
{
    SECURITY_DESCRIPTOR *s = sd;
    if (s->Revision != SECURITY_DESCRIPTOR_REVISION)
        return STATUS_UNKNOWN_REVISION;
    if (s->Control & SE_SELF_RELATIVE)
        return STATUS_INVALID_SECURITY_DESCR;
    if (!present) {
        s->Control &= ~SE_DACL_PRESENT;
        return STATUS_SUCCESS;
    }
    s->Dacl = dacl;
    s->Control |= SE_DACL_PRESENT;
    s->Control &= ~SE_DACL_DEFAULTED;
    if (defaulted)
        s->Control |= SE_DACL_DEFAULTED;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlGetSaclSecurityDescriptor(PSECURITY_DESCRIPTOR sd, PBOOLEAN present, PACL *sacl,
                                            PBOOLEAN defaulted)
{
    const SECURITY_DESCRIPTOR *s = sd;
    if (s->Revision != SECURITY_DESCRIPTOR_REVISION)
        return STATUS_UNKNOWN_REVISION;
    *present = (s->Control & SE_SACL_PRESENT) != 0;
    if (*present) {
        *sacl = sd_sacl(s);
        *defaulted = (s->Control & SE_SACL_DEFAULTED) != 0;
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlSetSaclSecurityDescriptor(PSECURITY_DESCRIPTOR sd, BOOLEAN present, PACL sacl, BOOLEAN defaulted)
{
    SECURITY_DESCRIPTOR *s = sd;
    if (s->Revision != SECURITY_DESCRIPTOR_REVISION)
        return STATUS_UNKNOWN_REVISION;
    if (s->Control & SE_SELF_RELATIVE)
        return STATUS_INVALID_SECURITY_DESCR;
    if (!present) {
        s->Control &= ~SE_SACL_PRESENT;
        return STATUS_SUCCESS;
    }
    s->Sacl = sacl;
    s->Control |= SE_SACL_PRESENT;
    s->Control &= ~SE_SACL_DEFAULTED;
    if (defaulted)
        s->Control |= SE_SACL_DEFAULTED;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlGetControlSecurityDescriptor(PSECURITY_DESCRIPTOR sd, PSECURITY_DESCRIPTOR_CONTROL control,
                                               PULONG revision)
{
    const SECURITY_DESCRIPTOR *s = sd;
    *revision = s->Revision;
    if (s->Revision != SECURITY_DESCRIPTOR_REVISION)
        return STATUS_UNKNOWN_REVISION;
    *control = s->Control;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlSetControlSecurityDescriptor(PSECURITY_DESCRIPTOR sd, SECURITY_DESCRIPTOR_CONTROL mask,
                                               SECURITY_DESCRIPTOR_CONTROL bits)
{
    SECURITY_DESCRIPTOR *s = sd;
    /* the presence, defaulting and form bits and the rm bit follow from the other calls. */
    if ((~mask & bits) || (mask & 0xC03F))
        return STATUS_INVALID_PARAMETER;
    s->Control = (SECURITY_DESCRIPTOR_CONTROL)(bits | (s->Control & ~mask));
    return STATUS_SUCCESS;
}

BOOLEAN NTAPI RtlGetSecurityDescriptorRMControl(PSECURITY_DESCRIPTOR sd, PUCHAR control)
{
    const SECURITY_DESCRIPTOR *s = sd;
    if (!(s->Control & SE_RM_CONTROL_VALID)) {
        *control = 0;
        return FALSE;
    }
    *control = s->Sbz1;
    return TRUE;
}

VOID NTAPI RtlSetSecurityDescriptorRMControl(PSECURITY_DESCRIPTOR sd, PUCHAR control)
{
    SECURITY_DESCRIPTOR *s = sd;
    if (control) {
        s->Control |= SE_RM_CONTROL_VALID;
        s->Sbz1 = *control;
    } else {
        s->Control &= ~SE_RM_CONTROL_VALID;
        s->Sbz1 = 0;
    }
}

/*
 * the self relative layout windows writes: the 20 byte header, then the sacl, the dacl, the owner and the group,
 * each padded to four bytes, absent parts having offset 0.
 */
NTSTATUS NTAPI RtlMakeSelfRelativeSD(PSECURITY_DESCRIPTOR absolute, PSECURITY_DESCRIPTOR relative, PULONG length)
{
    const SECURITY_DESCRIPTOR *s = absolute;
    PSID owner = sd_owner(s), group = sd_group(s);
    PACL dacl = s->Control & SE_DACL_PRESENT ? sd_dacl(s) : nullptr;
    PACL sacl = s->Control & SE_SACL_PRESENT ? sd_sacl(s) : nullptr;
    ULONG owner_size = padded_sid(owner), group_size = padded_sid(group);
    ULONG dacl_size = padded_acl(dacl), sacl_size = padded_acl(sacl);
    ULONG total = sizeof(SECURITY_DESCRIPTOR_RELATIVE) + owner_size + group_size + dacl_size + sacl_size;
    if (total > *length) {
        *length = total;
        return STATUS_BUFFER_TOO_SMALL;
    }
    if (!relative)
        return STATUS_INVALID_PARAMETER;
    UCHAR *out = relative;
    memset(out, 0, total);
    SECURITY_DESCRIPTOR_RELATIVE *r = relative;
    r->Revision = s->Revision;
    r->Sbz1 = s->Sbz1;
    r->Control = s->Control;
    ULONG at = sizeof(*r);
    if (sacl_size) {
        r->Sacl = at;
        memmove(out + at, sacl, sacl->AclSize);
        at += sacl_size;
    }
    if (dacl_size) {
        r->Dacl = at;
        memmove(out + at, dacl, dacl->AclSize);
        at += dacl_size;
    }
    if (owner_size) {
        r->Owner = at;
        memmove(out + at, owner, RtlLengthSid(owner));
        at += owner_size;
    }
    if (group_size) {
        r->Group = at;
        memmove(out + at, group, RtlLengthSid(group));
    }
    r->Control |= SE_SELF_RELATIVE;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlAbsoluteToSelfRelativeSD(PSECURITY_DESCRIPTOR absolute, PSECURITY_DESCRIPTOR relative,
                                           PULONG length)
{
    if (((SECURITY_DESCRIPTOR *)absolute)->Control & SE_SELF_RELATIVE)
        return STATUS_BAD_DESCRIPTOR_FORMAT;
    return RtlMakeSelfRelativeSD(absolute, relative, length);
}

NTSTATUS NTAPI RtlSelfRelativeToAbsoluteSD(PSECURITY_DESCRIPTOR relative, PSECURITY_DESCRIPTOR absolute,
                                           PULONG absolute_size, PACL dacl_out, PULONG dacl_size, PACL sacl_out,
                                           PULONG sacl_size, PSID owner_out, PULONG owner_size, PSID group_out,
                                           PULONG group_size)
{
    const SECURITY_DESCRIPTOR_RELATIVE *r = relative;
    if (!(r->Control & SE_SELF_RELATIVE))
        return STATUS_BAD_DESCRIPTOR_FORMAT;
    const SECURITY_DESCRIPTOR *s = relative;
    PSID owner = sd_owner(s), group = sd_group(s);
    PACL dacl = r->Control & SE_DACL_PRESENT ? sd_dacl(s) : nullptr;
    PACL sacl = r->Control & SE_SACL_PRESENT ? sd_sacl(s) : nullptr;
    ULONG need_owner = padded_sid(owner), need_group = padded_sid(group);
    ULONG need_dacl = padded_acl(dacl), need_sacl = padded_acl(sacl);
    if (absolute && *absolute_size >= sizeof(SECURITY_DESCRIPTOR) && need_owner <= *owner_size &&
        need_dacl <= *dacl_size && need_sacl <= *sacl_size && need_group <= *group_size &&
        (!owner || owner_out) && (!group || group_out) && (!dacl || dacl_out) && (!sacl || sacl_out)) {
        SECURITY_DESCRIPTOR *out = absolute;
        /* the padding of the absolute form keeps the bytes of the relative header, like windows leaves it. */
        memmove(out, relative, sizeof(SECURITY_DESCRIPTOR_RELATIVE) < sizeof(*out) ? sizeof(SECURITY_DESCRIPTOR_RELATIVE)
                                                                                    : sizeof(*out));
        out->Control &= ~SE_SELF_RELATIVE;
        out->Owner = out->Group = nullptr;
        out->Sacl = out->Dacl = nullptr;
        if (owner) {
            memmove(owner_out, owner, RtlLengthSid(owner));
            out->Owner = owner_out;
        }
        if (group) {
            memmove(group_out, group, RtlLengthSid(group));
            out->Group = group_out;
        }
        if (sacl) {
            memmove(sacl_out, sacl, sacl->AclSize);
            out->Sacl = sacl_out;
        }
        if (dacl) {
            memmove(dacl_out, dacl, dacl->AclSize);
            out->Dacl = dacl_out;
        }
        return STATUS_SUCCESS;
    }
    *absolute_size = sizeof(SECURITY_DESCRIPTOR);
    *owner_size = need_owner;
    *group_size = need_group;
    *sacl_size = need_sacl;
    *dacl_size = need_dacl;
    return STATUS_BUFFER_TOO_SMALL;
}

/* converts in place: the parts move after the larger header and the offsets turn into pointers. */
NTSTATUS NTAPI RtlSelfRelativeToAbsoluteSD2(PSECURITY_DESCRIPTOR relative, PULONG size)
{
    if (!relative)
        return STATUS_INVALID_PARAMETER_1;
    if (!size)
        return STATUS_INVALID_PARAMETER_2;
    if (*size < sizeof(SECURITY_DESCRIPTOR_RELATIVE))
        return STATUS_INVALID_PARAMETER;
    SECURITY_DESCRIPTOR_RELATIVE *r = relative;
    if (!(r->Control & SE_SELF_RELATIVE))
        return STATUS_BAD_DESCRIPTOR_FORMAT;
    UCHAR *base = relative;
    PSID owner = sd_owner(relative), group = sd_group(relative);
    PACL dacl = r->Control & SE_DACL_PRESENT ? sd_dacl(relative) : nullptr;
    PACL sacl = r->Control & SE_SACL_PRESENT ? sd_sacl(relative) : nullptr;
    /* the highest end of the parts decides how much follows the header. */
    UCHAR *last = nullptr;
    if (owner)
        last = (UCHAR *)owner + padded_sid(owner);
    if (group && (UCHAR *)group + padded_sid(group) > last)
        last = (UCHAR *)group + padded_sid(group);
    if (dacl && (UCHAR *)dacl + padded_acl(dacl) > last)
        last = (UCHAR *)dacl + padded_acl(dacl);
    if (sacl && (UCHAR *)sacl + padded_acl(sacl) > last)
        last = (UCHAR *)sacl + padded_acl(sacl);
    ULONG needed = sizeof(SECURITY_DESCRIPTOR);
    if (last) {
        ULONG payload = (ULONG)(last - base) - sizeof(SECURITY_DESCRIPTOR_RELATIVE);
        needed += (payload + sizeof(PVOID) - 1) & ~(ULONG)(sizeof(PVOID) - 1);
    }
    if (needed > *size) {
        *size = needed;
        return STATUS_BUFFER_TOO_SMALL;
    }
    ULONG shift = sizeof(SECURITY_DESCRIPTOR) - sizeof(SECURITY_DESCRIPTOR_RELATIVE);
    if (last)
        memmove(base + sizeof(SECURITY_DESCRIPTOR), base + sizeof(SECURITY_DESCRIPTOR_RELATIVE),
                needed - sizeof(SECURITY_DESCRIPTOR));
    SECURITY_DESCRIPTOR *s = relative;
    s->Control &= ~SE_SELF_RELATIVE;
    s->Owner = owner ? (PSID)((UCHAR *)owner + shift) : nullptr;
    s->Group = group ? (PSID)((UCHAR *)group + shift) : nullptr;
    s->Sacl = sacl ? (PACL)((UCHAR *)sacl + shift) : nullptr;
    s->Dacl = dacl ? (PACL)((UCHAR *)dacl + shift) : nullptr;
    return STATUS_SUCCESS;
}
