/*
 * security descriptors, access control lists and access checks.
 *
 * mime's kernel does not keep security descriptors on files or keys yet
 * (NtQuerySecurityObject and NtAccessCheck are not implemented), so when it
 * cannot answer, the descriptors are synthesized the way windows sets them up
 * for the user's own files, and access checks are evaluated here against the
 * groups of the token, with the rules of the kernel's SeAccessCheck.
 */
#include "advapi32_private.h"

#define LONG_ALIGN(n) (((n) + 3) & ~3u)
#define GENERIC_BITS (GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL)
#ifndef SPECIFIC_RIGHTS_ALL
#define SPECIFIC_RIGHTS_ALL 0x0000FFFFL
#endif
#ifndef ACCESS_SYSTEM_SECURITY
#define ACCESS_SYSTEM_SECURITY 0x01000000L
#endif

/* ---------------------------------------------------------------- descriptors */

typedef struct SD_PARTS {
    WORD Control;
    SID *Owner;
    SID *Group;
    ACL *Sacl;
    ACL *Dacl;
} SD_PARTS;

static BOOL sd_parts(const void *sd, SD_PARTS *p)
{
    const SECURITY_DESCRIPTOR *a = sd;
    if (!a || a->Revision != SECURITY_DESCRIPTOR_REVISION)
        return FALSE;
    p->Control = a->Control;
    if (a->Control & SE_SELF_RELATIVE) {
        const SECURITY_DESCRIPTOR_RELATIVE *r = sd;
        const BYTE *base = sd;
        p->Owner = r->Owner ? (SID *)(base + r->Owner) : NULL;
        p->Group = r->Group ? (SID *)(base + r->Group) : NULL;
        p->Sacl = r->Sacl && (r->Control & SE_SACL_PRESENT) ? (ACL *)(base + r->Sacl) : NULL;
        p->Dacl = r->Dacl && (r->Control & SE_DACL_PRESENT) ? (ACL *)(base + r->Dacl) : NULL;
    } else {
        p->Owner = a->Owner;
        p->Group = a->Group;
        p->Sacl = a->Control & SE_SACL_PRESENT ? a->Sacl : NULL;
        p->Dacl = a->Control & SE_DACL_PRESENT ? a->Dacl : NULL;
    }
    return TRUE;
}

static BOOL check_sd(const void *sd, SD_PARTS *p)
{
    if (!sd_parts(sd, p)) {
        AdvSetError(ERROR_UNKNOWN_REVISION);
        return FALSE;
    }
    return TRUE;
}

/* writes a self relative descriptor when it fits; returns the size it needs. */
static DWORD write_sd(BYTE *out, DWORD size, WORD control, const SID *owner, const SID *group, const ACL *dacl)
{
    DWORD need = sizeof(SECURITY_DESCRIPTOR_RELATIVE);
    DWORD owner_at = 0, group_at = 0, dacl_at = 0;
    if (owner) {
        owner_at = need;
        need += LONG_ALIGN(AdvSidLength(owner));
    }
    if (group) {
        group_at = need;
        need += LONG_ALIGN(AdvSidLength(group));
    }
    if (dacl) {
        dacl_at = need;
        need += LONG_ALIGN(dacl->AclSize);
    }
    if (out && size >= need) {
        memset(out, 0, need);
        SECURITY_DESCRIPTOR_RELATIVE *r = (SECURITY_DESCRIPTOR_RELATIVE *)out;
        r->Revision = SECURITY_DESCRIPTOR_REVISION;
        r->Control = control | SE_SELF_RELATIVE | (dacl ? SE_DACL_PRESENT : 0);
        r->Owner = owner_at;
        r->Group = group_at;
        r->Dacl = dacl_at;
        if (owner)
            memcpy(out + owner_at, owner, AdvSidLength(owner));
        if (group)
            memcpy(out + group_at, group, AdvSidLength(group));
        if (dacl)
            memcpy(out + dacl_at, dacl, dacl->AclSize);
    }
    return need;
}

ADVAPI BOOL WINAPI InitializeSecurityDescriptor(PSECURITY_DESCRIPTOR sd, DWORD revision)
{
    if (revision != SECURITY_DESCRIPTOR_REVISION) {
        AdvSetError(ERROR_UNKNOWN_REVISION);
        return FALSE;
    }
    memset(sd, 0, sizeof(SECURITY_DESCRIPTOR));
    ((SECURITY_DESCRIPTOR *)sd)->Revision = SECURITY_DESCRIPTOR_REVISION;
    return TRUE;
}

ADVAPI BOOL WINAPI IsValidSecurityDescriptor(PSECURITY_DESCRIPTOR sd)
{
    SD_PARTS p;
    if (!sd_parts(sd, &p)) {
        AdvSetError(ERROR_INVALID_SECURITY_DESCR);
        return FALSE;
    }
    if ((p.Owner && !AdvSidValid(p.Owner)) || (p.Group && !AdvSidValid(p.Group))) {
        AdvSetError(ERROR_INVALID_SECURITY_DESCR);
        return FALSE;
    }
    return TRUE;
}

ADVAPI DWORD WINAPI GetSecurityDescriptorLength(PSECURITY_DESCRIPTOR sd)
{
    SD_PARTS p;
    if (!sd_parts(sd, &p))
        return 0;
    DWORD n = sizeof(SECURITY_DESCRIPTOR_RELATIVE);
    if (p.Owner)
        n += LONG_ALIGN(AdvSidLength(p.Owner));
    if (p.Group)
        n += LONG_ALIGN(AdvSidLength(p.Group));
    if (p.Dacl)
        n += LONG_ALIGN(p.Dacl->AclSize);
    if (p.Sacl)
        n += LONG_ALIGN(p.Sacl->AclSize);
    return n;
}

ADVAPI BOOL WINAPI GetSecurityDescriptorControl(PSECURITY_DESCRIPTOR sd, PSECURITY_DESCRIPTOR_CONTROL control,
                                                LPDWORD revision)
{
    if (!sd || !control || !revision) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *revision = ((SECURITY_DESCRIPTOR *)sd)->Revision;
    SD_PARTS p;
    if (!check_sd(sd, &p))
        return FALSE;
    *control = p.Control;
    return TRUE;
}

ADVAPI BOOL WINAPI GetSecurityDescriptorOwner(PSECURITY_DESCRIPTOR sd, PSID *owner, LPBOOL defaulted)
{
    SD_PARTS p;
    if (!owner || !defaulted) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!check_sd(sd, &p))
        return FALSE;
    *owner = p.Owner;
    *defaulted = (p.Control & SE_OWNER_DEFAULTED) != 0;
    return TRUE;
}

ADVAPI BOOL WINAPI GetSecurityDescriptorGroup(PSECURITY_DESCRIPTOR sd, PSID *group, LPBOOL defaulted)
{
    SD_PARTS p;
    if (!group || !defaulted) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!check_sd(sd, &p))
        return FALSE;
    *group = p.Group;
    *defaulted = (p.Control & SE_GROUP_DEFAULTED) != 0;
    return TRUE;
}

ADVAPI BOOL WINAPI GetSecurityDescriptorDacl(PSECURITY_DESCRIPTOR sd, LPBOOL present, PACL *dacl, LPBOOL defaulted)
{
    SD_PARTS p;
    if (!present || !dacl || !defaulted) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!check_sd(sd, &p))
        return FALSE;
    *present = (p.Control & SE_DACL_PRESENT) != 0;
    if (*present) {
        *dacl = p.Dacl;
        *defaulted = (p.Control & SE_DACL_DEFAULTED) != 0;
    }
    return TRUE;
}

ADVAPI BOOL WINAPI GetSecurityDescriptorSacl(PSECURITY_DESCRIPTOR sd, LPBOOL present, PACL *sacl, LPBOOL defaulted)
{
    SD_PARTS p;
    if (!present || !sacl || !defaulted) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!check_sd(sd, &p))
        return FALSE;
    *present = (p.Control & SE_SACL_PRESENT) != 0;
    if (*present) {
        *sacl = p.Sacl;
        *defaulted = (p.Control & SE_SACL_DEFAULTED) != 0;
    }
    return TRUE;
}

/* the setters only apply to absolute descriptors, as on windows. */
static SECURITY_DESCRIPTOR *absolute_sd(PSECURITY_DESCRIPTOR sd)
{
    SECURITY_DESCRIPTOR *a = sd;
    if (!a || a->Revision != SECURITY_DESCRIPTOR_REVISION) {
        AdvSetError(ERROR_UNKNOWN_REVISION);
        return NULL;
    }
    if (a->Control & SE_SELF_RELATIVE) {
        AdvSetError(ERROR_INVALID_SECURITY_DESCR);
        return NULL;
    }
    return a;
}

ADVAPI BOOL WINAPI SetSecurityDescriptorOwner(PSECURITY_DESCRIPTOR sd, PSID owner, BOOL defaulted)
{
    SECURITY_DESCRIPTOR *a = absolute_sd(sd);
    if (!a)
        return FALSE;
    a->Owner = owner;
    a->Control = (a->Control & ~SE_OWNER_DEFAULTED) | (defaulted ? SE_OWNER_DEFAULTED : 0);
    return TRUE;
}

ADVAPI BOOL WINAPI SetSecurityDescriptorGroup(PSECURITY_DESCRIPTOR sd, PSID group, BOOL defaulted)
{
    SECURITY_DESCRIPTOR *a = absolute_sd(sd);
    if (!a)
        return FALSE;
    a->Group = group;
    a->Control = (a->Control & ~SE_GROUP_DEFAULTED) | (defaulted ? SE_GROUP_DEFAULTED : 0);
    return TRUE;
}

ADVAPI BOOL WINAPI SetSecurityDescriptorDacl(PSECURITY_DESCRIPTOR sd, BOOL present, PACL dacl, BOOL defaulted)
{
    SECURITY_DESCRIPTOR *a = absolute_sd(sd);
    if (!a)
        return FALSE;
    a->Control &= ~(SE_DACL_PRESENT | SE_DACL_DEFAULTED);
    a->Dacl = NULL;
    if (present) {
        a->Control |= SE_DACL_PRESENT | (defaulted ? SE_DACL_DEFAULTED : 0);
        a->Dacl = dacl;
    }
    return TRUE;
}

ADVAPI BOOL WINAPI SetSecurityDescriptorSacl(PSECURITY_DESCRIPTOR sd, BOOL present, PACL sacl, BOOL defaulted)
{
    SECURITY_DESCRIPTOR *a = absolute_sd(sd);
    if (!a)
        return FALSE;
    a->Control &= ~(SE_SACL_PRESENT | SE_SACL_DEFAULTED);
    a->Sacl = NULL;
    if (present) {
        a->Control |= SE_SACL_PRESENT | (defaulted ? SE_SACL_DEFAULTED : 0);
        a->Sacl = sacl;
    }
    return TRUE;
}

/* ---------------------------------------------------------------- access control lists */

static ACE_HEADER *ace_at(const ACL *acl, DWORD index)
{
    const BYTE *p = (const BYTE *)(acl + 1), *end = (const BYTE *)acl + acl->AclSize;
    for (DWORD i = 0; i < acl->AceCount; i++) {
        const ACE_HEADER *h = (const ACE_HEADER *)p;
        if (p + sizeof(*h) > end || h->AceSize < sizeof(*h) || p + h->AceSize > end)
            return NULL;
        if (i == index)
            return (ACE_HEADER *)h;
        p += h->AceSize;
    }
    return NULL;
}

/* the first free byte after the aces, or NULL when the list is malformed. */
static BYTE *acl_end(const ACL *acl)
{
    const BYTE *p = (const BYTE *)(acl + 1), *end = (const BYTE *)acl + acl->AclSize;
    for (DWORD i = 0; i < acl->AceCount; i++) {
        const ACE_HEADER *h = (const ACE_HEADER *)p;
        if (p + sizeof(*h) > end || h->AceSize < sizeof(*h) || p + h->AceSize > end)
            return NULL;
        p += h->AceSize;
    }
    return (BYTE *)p;
}

static BOOL acl_valid(const ACL *acl)
{
    return acl && acl->AclRevision >= ACL_REVISION && acl->AclRevision <= ACL_REVISION_DS &&
           acl->AclSize >= sizeof(ACL) && acl_end(acl);
}

ADVAPI BOOL WINAPI InitializeAcl(PACL acl, DWORD length, DWORD revision)
{
    if (length < sizeof(ACL)) {
        AdvSetError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    if (revision < ACL_REVISION || revision > ACL_REVISION_DS || length > 0xFFFF) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    acl->AclRevision = (BYTE)revision;
    acl->Sbz1 = 0;
    acl->AclSize = (WORD)(length & ~3u);
    acl->AceCount = 0;
    acl->Sbz2 = 0;
    return TRUE;
}

ADVAPI BOOL WINAPI IsValidAcl(PACL acl)
{
    return acl_valid(acl);
}

ADVAPI BOOL WINAPI GetAclInformation(PACL acl, LPVOID info, DWORD length, ACL_INFORMATION_CLASS klass)
{
    if (!acl_valid(acl)) {
        AdvSetError(ERROR_INVALID_ACL);
        return FALSE;
    }
    if (klass == AclRevisionInformation) {
        if (length < sizeof(DWORD)) {
            AdvSetError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        *(DWORD *)info = acl->AclRevision;
        return TRUE;
    }
    if (klass == AclSizeInformation) {
        if (length < sizeof(ACL_SIZE_INFORMATION)) {
            AdvSetError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        ACL_SIZE_INFORMATION *s = info;
        s->AceCount = acl->AceCount;
        s->AclBytesInUse = (DWORD)(acl_end(acl) - (BYTE *)acl);
        s->AclBytesFree = acl->AclSize - s->AclBytesInUse;
        return TRUE;
    }
    AdvSetError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

ADVAPI BOOL WINAPI GetAce(PACL acl, DWORD index, LPVOID *ace)
{
    ACE_HEADER *h = acl && ace ? ace_at(acl, index) : NULL;
    if (!h) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *ace = h;
    return TRUE;
}

static BOOL add_ace(PACL acl, DWORD revision, DWORD flags, DWORD mask, PSID sid, BYTE type)
{
    if (!acl_valid(acl) || revision < ACL_REVISION || revision > ACL_REVISION_DS) {
        AdvSetError(acl_valid(acl) ? ERROR_INVALID_PARAMETER : ERROR_INVALID_ACL);
        return FALSE;
    }
    if (!AdvSidValid(sid)) {
        AdvSetError(ERROR_INVALID_SID);
        return FALSE;
    }
    if (flags & ~0x1Fu) {
        AdvSetError(ERROR_INVALID_FLAGS);
        return FALSE;
    }
    BYTE *at = acl_end(acl);
    DWORD size = offsetof(ACCESS_ALLOWED_ACE, SidStart) + AdvSidLength(sid);
    if (at + size > (BYTE *)acl + acl->AclSize) {
        AdvSetError(ERROR_ALLOTTED_SPACE_EXCEEDED);
        return FALSE;
    }
    ACCESS_ALLOWED_ACE *ace = (ACCESS_ALLOWED_ACE *)at;
    ace->Header.AceType = type;
    ace->Header.AceFlags = (BYTE)flags;
    ace->Header.AceSize = (WORD)size;
    ace->Mask = mask;
    memcpy(&ace->SidStart, sid, AdvSidLength(sid));
    acl->AceCount++;
    if (acl->AclRevision < revision)
        acl->AclRevision = (BYTE)revision;
    return TRUE;
}

ADVAPI BOOL WINAPI AddAccessAllowedAceEx(PACL acl, DWORD revision, DWORD flags, DWORD mask, PSID sid)
{
    return add_ace(acl, revision, flags, mask, sid, ACCESS_ALLOWED_ACE_TYPE);
}

ADVAPI BOOL WINAPI AddAccessAllowedAce(PACL acl, DWORD revision, DWORD mask, PSID sid)
{
    return add_ace(acl, revision, 0, mask, sid, ACCESS_ALLOWED_ACE_TYPE);
}

ADVAPI BOOL WINAPI AddAccessDeniedAceEx(PACL acl, DWORD revision, DWORD flags, DWORD mask, PSID sid)
{
    return add_ace(acl, revision, flags, mask, sid, ACCESS_DENIED_ACE_TYPE);
}

ADVAPI BOOL WINAPI AddAccessDeniedAce(PACL acl, DWORD revision, DWORD mask, PSID sid)
{
    return add_ace(acl, revision, 0, mask, sid, ACCESS_DENIED_ACE_TYPE);
}

ADVAPI void WINAPI MapGenericMask(PDWORD mask, PGENERIC_MAPPING mapping)
{
    DWORD m = *mask;
    if (m & GENERIC_READ)
        m |= mapping->GenericRead;
    if (m & GENERIC_WRITE)
        m |= mapping->GenericWrite;
    if (m & GENERIC_EXECUTE)
        m |= mapping->GenericExecute;
    if (m & GENERIC_ALL)
        m |= mapping->GenericAll;
    *mask = m & ~GENERIC_BITS;
}

/* ---------------------------------------------------------------- access evaluation */

typedef struct IDENTITY {
    TOKEN_USER *User;
    TOKEN_GROUPS *Groups;
    const SID *Sid; /* a trustee that is not the token's user, or NULL */
} IDENTITY;

/* whether an ace's sid applies: allow aces need an enabled group, deny aces also match deny only groups. */
static BOOL identity_matches(const IDENTITY *id, const SID *sid, BOOL deny)
{
    if (id->Sid) {
        BYTE everyone[16], authenticated[16], users[16];
        AdvMakeSid(everyone, 1, 1, 0);
        AdvMakeSid(authenticated, 5, 1, 11);
        AdvMakeSid(users, 5, 2, 32, 545);
        if (AdvSidEqual(sid, id->Sid) || AdvSidEqual(sid, (SID *)everyone))
            return TRUE;
        /* an account of this machine is an authenticated member of its users. */
        const SID *t = id->Sid;
        BOOL local_user = t->IdentifierAuthority.Value[5] == 5 && t->SubAuthorityCount == 5 && t->SubAuthority[0] == 21;
        return local_user && (AdvSidEqual(sid, (SID *)authenticated) || AdvSidEqual(sid, (SID *)users));
    }
    if (AdvSidEqual(sid, id->User->User.Sid))
        return TRUE;
    for (DWORD i = 0; i < id->Groups->GroupCount; i++) {
        DWORD a = id->Groups->Groups[i].Attributes;
        BOOL usable = deny ? (a & (SE_GROUP_ENABLED | SE_GROUP_USE_FOR_DENY_ONLY)) != 0
                           : (a & SE_GROUP_ENABLED) && !(a & SE_GROUP_USE_FOR_DENY_ONLY);
        if (!usable)
            continue;
        if (AdvSidEqual(sid, id->Groups->Groups[i].Sid))
            return TRUE;
    }
    return FALSE;
}

/* walks the dacl in order: each bit is decided by the first allow or deny ace that mentions it. */
static void evaluate_dacl(const ACL *dacl, const IDENTITY *id, const GENERIC_MAPPING *mapping, ACCESS_MASK *allowed,
                          ACCESS_MASK *denied)
{
    for (DWORD i = 0; i < dacl->AceCount; i++) {
        const ACCESS_ALLOWED_ACE *ace = (const ACCESS_ALLOWED_ACE *)ace_at(dacl, i);
        if (!ace)
            break;
        if (ace->Header.AceFlags & INHERIT_ONLY_ACE)
            continue;
        BYTE type = ace->Header.AceType;
        if (type != ACCESS_ALLOWED_ACE_TYPE && type != ACCESS_DENIED_ACE_TYPE)
            continue;
        if (ace->Header.AceSize < offsetof(ACCESS_ALLOWED_ACE, SidStart) + 8)
            continue;
        DWORD mask = ace->Mask;
        if (mapping)
            MapGenericMask(&mask, (PGENERIC_MAPPING)mapping);
        if (!identity_matches(id, (const SID *)&ace->SidStart, type == ACCESS_DENIED_ACE_TYPE))
            continue;
        if (type == ACCESS_ALLOWED_ACE_TYPE)
            *allowed |= mask & ~*denied;
        else
            *denied |= mask & ~*allowed;
    }
}

/* the user mode version of SeAccessCheck, for kernels without NtAccessCheck. */
static BOOL access_check(const SD_PARTS *p, HANDLE token, DWORD desired, PGENERIC_MAPPING mapping, LPDWORD granted,
                         LPBOOL status)
{
    IDENTITY id = {AdvQueryTokenUser(token), AdvQueryTokenGroups(token), NULL};
    if (!id.User || !id.Groups) {
        AdvFree(id.User);
        AdvFree(id.Groups);
        AdvSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    ACCESS_MASK wanted = desired & ~MAXIMUM_ALLOWED, allowed = 0, denied = 0;
    if (!(p->Control & SE_DACL_PRESENT) || !p->Dacl) {
        allowed = mapping->GenericAll | (desired & ~(MAXIMUM_ALLOWED | ACCESS_SYSTEM_SECURITY));
    } else {
        /* the owner may always read and change the dacl. */
        if (p->Owner && identity_matches(&id, p->Owner, FALSE))
            allowed |= READ_CONTROL | WRITE_DAC;
        evaluate_dacl(p->Dacl, &id, mapping, &allowed, &denied);
    }
    AdvFree(id.User);
    AdvFree(id.Groups);
    allowed &= ~ACCESS_SYSTEM_SECURITY; /* needs SeSecurityPrivilege, which the user does not hold */
    ACCESS_MASK result = desired & MAXIMUM_ALLOWED ? allowed | wanted : wanted;
    BOOL ok = (wanted & ~allowed) == 0 && result != 0;
    *granted = ok ? (desired & MAXIMUM_ALLOWED ? allowed : wanted) : 0;
    *status = ok;
    if (!ok)
        AdvSetError(ERROR_ACCESS_DENIED);
    return TRUE;
}

ADVAPI BOOL WINAPI AccessCheck(PSECURITY_DESCRIPTOR sd, HANDLE token, DWORD desired, PGENERIC_MAPPING mapping,
                               PPRIVILEGE_SET privileges, LPDWORD privileges_length, LPDWORD granted, LPBOOL status)
{
    if (!mapping || !privileges_length || !granted || !status) {
        AdvSetError(ERROR_NOACCESS);
        return FALSE;
    }
    NTSTATUS access_status;
    NTSTATUS result = NtAccessCheck(sd, token, desired, mapping, privileges, privileges_length, granted, &access_status);
    if (result != STATUS_NOT_IMPLEMENTED) {
        if (!NT_SUCCESS(result))
            return AdvSetStatus(result);
        *status = NT_SUCCESS(access_status);
        if (!*status)
            AdvSetStatus(access_status);
        return TRUE;
    }
    SD_PARTS p;
    if (!sd_parts(sd, &p) || !p.Owner || !p.Group) {
        AdvSetError(ERROR_INVALID_SECURITY_DESCR);
        return FALSE;
    }
    if (*privileges_length < sizeof(PRIVILEGE_SET)) {
        *privileges_length = sizeof(PRIVILEGE_SET);
        AdvSetError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    if (desired & GENERIC_BITS) {
        AdvSetError(ERROR_GENERIC_NOT_MAPPED);
        return FALSE;
    }
    if (!privileges) {
        AdvSetError(ERROR_NOACCESS);
        return FALSE;
    }
    privileges->PrivilegeCount = 0;
    privileges->Control = 0;
    *privileges_length = sizeof(PRIVILEGE_SET);
    return access_check(&p, token, desired, mapping, granted, status);
}

/* ---------------------------------------------------------------- trustees */

typedef enum _MULTIPLE_TRUSTEE_OPERATION {
    NO_MULTIPLE_TRUSTEE,
    TRUSTEE_IS_IMPERSONATE,
} MULTIPLE_TRUSTEE_OPERATION;

typedef enum _TRUSTEE_FORM {
    TRUSTEE_IS_SID,
    TRUSTEE_IS_NAME,
    TRUSTEE_BAD_FORM,
    TRUSTEE_IS_OBJECTS_AND_SID,
    TRUSTEE_IS_OBJECTS_AND_NAME,
} TRUSTEE_FORM;

typedef enum _TRUSTEE_TYPE {
    TRUSTEE_IS_UNKNOWN,
} TRUSTEE_TYPE;

typedef struct _TRUSTEE_W {
    struct _TRUSTEE_W *pMultipleTrustee;
    MULTIPLE_TRUSTEE_OPERATION MultipleTrusteeOperation;
    TRUSTEE_FORM TrusteeForm;
    TRUSTEE_TYPE TrusteeType;
    LPWSTR ptstrName;
} TRUSTEE_W, *PTRUSTEE_W;

typedef struct _OBJECTS_AND_SID {
    DWORD ObjectsPresent;
    GUID ObjectTypeGuid;
    GUID InheritedObjectTypeGuid;
    SID *pSid;
} OBJECTS_AND_SID;

typedef struct _OBJECTS_AND_NAME {
    DWORD ObjectsPresent;
    SE_OBJECT_TYPE ObjectType;
    LPWSTR ObjectTypeName;
    LPWSTR InheritedObjectTypeName;
    LPWSTR ptstrName;
} OBJECTS_AND_NAME;

ADVAPI void WINAPI BuildTrusteeWithSidW(PTRUSTEE_W trustee, PSID sid)
{
    trustee->pMultipleTrustee = NULL;
    trustee->MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE;
    trustee->TrusteeForm = TRUSTEE_IS_SID;
    trustee->TrusteeType = TRUSTEE_IS_UNKNOWN;
    trustee->ptstrName = (LPWSTR)sid;
}

/* the ansi trustee has the same layout; only the name form differs. */
ADVAPI void WINAPI BuildTrusteeWithSidA(PTRUSTEE_W trustee, PSID sid)
{
    BuildTrusteeWithSidW(trustee, sid);
}

ADVAPI void WINAPI BuildTrusteeWithNameW(PTRUSTEE_W trustee, LPWSTR name)
{
    trustee->pMultipleTrustee = NULL;
    trustee->MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE;
    trustee->TrusteeForm = TRUSTEE_IS_NAME;
    trustee->TrusteeType = TRUSTEE_IS_UNKNOWN;
    trustee->ptstrName = name;
}

static DWORD effective_rights(PACL acl, PTRUSTEE_W trustee, BOOL ansi, PACCESS_MASK rights)
{
    if (!trustee || !rights)
        return ERROR_INVALID_PARAMETER;
    BYTE storage[SECURITY_MAX_SID_SIZE];
    const SID *sid = NULL;
    PCWSTR name = NULL;
    switch (trustee->TrusteeForm) {
    case TRUSTEE_IS_SID:
        sid = (const SID *)trustee->ptstrName;
        break;
    case TRUSTEE_IS_OBJECTS_AND_SID:
        sid = trustee->ptstrName ? ((OBJECTS_AND_SID *)trustee->ptstrName)->pSid : NULL;
        break;
    case TRUSTEE_IS_NAME:
        name = trustee->ptstrName;
        break;
    case TRUSTEE_IS_OBJECTS_AND_NAME:
        name = trustee->ptstrName ? ((OBJECTS_AND_NAME *)trustee->ptstrName)->ptstrName : NULL;
        break;
    default:
        return ERROR_INVALID_PARAMETER;
    }
    if (name) {
        PWSTR wide = ansi ? AdvAnsiToWide((LPCSTR)name) : NULL;
        WCHAR domain[256];
        DWORD size = sizeof(storage), domain_size = RTL_NUMBER_OF(domain);
        SID_NAME_USE use;
        BOOL found = LookupAccountNameW(NULL, wide ? wide : name, storage, &size, domain, &domain_size, &use);
        AdvFree(wide);
        if (!found)
            return ERROR_NONE_MAPPED;
        sid = (const SID *)storage;
    }
    if (!AdvSidValid(sid))
        return ERROR_INVALID_SID;
    if (!acl) {
        *rights = STANDARD_RIGHTS_ALL | SPECIFIC_RIGHTS_ALL;
        return ERROR_SUCCESS;
    }
    if (!acl_valid(acl))
        return ERROR_INVALID_ACL;
    IDENTITY id = {AdvQueryTokenUser(NULL), NULL, NULL};
    if (id.User && AdvSidEqual(id.User->User.Sid, sid))
        id.Groups = AdvQueryTokenGroups(NULL);
    if (!id.Groups)
        id.Sid = sid;
    ACCESS_MASK allowed = 0, denied = 0;
    evaluate_dacl(acl, &id, NULL, &allowed, &denied);
    AdvFree(id.User);
    AdvFree(id.Groups);
    *rights = allowed & ~denied;
    return ERROR_SUCCESS;
}

ADVAPI DWORD WINAPI GetEffectiveRightsFromAclW(PACL acl, PTRUSTEE_W trustee, PACCESS_MASK rights)
{
    return effective_rights(acl, trustee, FALSE, rights);
}

ADVAPI DWORD WINAPI GetEffectiveRightsFromAclA(PACL acl, PTRUSTEE_W trustee, PACCESS_MASK rights)
{
    return effective_rights(acl, trustee, TRUE, rights);
}

/* ---------------------------------------------------------------- object security */

/* what windows gives a new object of the user: full control to the system, administrators and the owner. */
static DWORD synthesize(BYTE **out, DWORD info, ACCESS_MASK all, ACCESS_MASK users_read, BYTE ace_flags,
                        BOOL user_owned)
{
    BYTE user[SECURITY_MAX_SID_SIZE], system[16], admins[16], users[16];
    BYTE acl_storage[256];
    if (!AdvCurrentUserSid(user))
        return ERROR_INVALID_HANDLE;
    AdvMakeSid(system, 5, 1, 18);
    AdvMakeSid(admins, 5, 2, 32, 544);
    AdvMakeSid(users, 5, 2, 32, 545);
    PVOID primary = user_owned ? AdvQueryToken(NULL, TokenPrimaryGroup) : NULL;
    const SID *owner = user_owned ? (SID *)user : (SID *)admins;
    const SID *group = primary ? *(SID **)primary : (SID *)system;
    ACL *acl = (ACL *)acl_storage;
    InitializeAcl(acl, sizeof(acl_storage), ACL_REVISION);
    add_ace(acl, ACL_REVISION, ace_flags, all, system, ACCESS_ALLOWED_ACE_TYPE);
    add_ace(acl, ACL_REVISION, ace_flags, all, admins, ACCESS_ALLOWED_ACE_TYPE);
    if (user_owned)
        add_ace(acl, ACL_REVISION, ace_flags, all, user, ACCESS_ALLOWED_ACE_TYPE);
    else
        add_ace(acl, ACL_REVISION, ace_flags, users_read, users, ACCESS_ALLOWED_ACE_TYPE);
    const SID *o = info & OWNER_SECURITY_INFORMATION ? owner : NULL;
    const SID *g = info & GROUP_SECURITY_INFORMATION ? group : NULL;
    const ACL *d = info & DACL_SECURITY_INFORMATION ? acl : NULL;
    WORD control = d ? SE_DACL_AUTO_INHERITED : 0;
    DWORD size = write_sd(NULL, 0, control, o, g, d);
    *out = LocalAlloc(LMEM_FIXED, size);
    if (*out)
        write_sd(*out, size, control, o, g, d);
    AdvFree(primary);
    return *out ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
}

/* the descriptor the kernel keeps for an object, or ERROR_CALL_NOT_IMPLEMENTED when it keeps none. */
static DWORD query_object(HANDLE handle, DWORD info, BYTE **out)
{
    ULONG needed = 0;
    NTSTATUS status = NtQuerySecurityObject(handle, info, NULL, 0, &needed);
    if (status == STATUS_NOT_IMPLEMENTED || status == STATUS_INVALID_DEVICE_REQUEST)
        return ERROR_CALL_NOT_IMPLEMENTED;
    if (status != STATUS_BUFFER_TOO_SMALL)
        return NT_SUCCESS(status) ? ERROR_INVALID_SECURITY_DESCR : RtlNtStatusToDosError(status);
    *out = LocalAlloc(LMEM_FIXED, needed);
    if (!*out)
        return ERROR_NOT_ENOUGH_MEMORY;
    status = NtQuerySecurityObject(handle, info, *out, needed, &needed);
    if (!NT_SUCCESS(status)) {
        LocalFree(*out);
        *out = NULL;
        return RtlNtStatusToDosError(status);
    }
    return ERROR_SUCCESS;
}

static DWORD file_security(LPCWSTR name, DWORD info, BYTE **out)
{
    *out = NULL;
    if (info & SACL_SECURITY_INFORMATION)
        return ERROR_PRIVILEGE_NOT_HELD;
    DWORD attributes = GetFileAttributesW(name);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return GetLastError();
    HANDLE h = CreateFileW(name, READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    DWORD error = ERROR_CALL_NOT_IMPLEMENTED;
    if (h != INVALID_HANDLE_VALUE) {
        error = query_object(h, info, out);
        CloseHandle(h);
    }
    if (error != ERROR_CALL_NOT_IMPLEMENTED)
        return error;
    BYTE flags = INHERITED_ACE | (attributes & FILE_ATTRIBUTE_DIRECTORY ? OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE : 0);
    return synthesize(out, info, FILE_ALL_ACCESS, 0, flags, TRUE);
}

static DWORD key_security(LPCWSTR name, DWORD info, BYTE **out)
{
    static const struct {
        PCWSTR Name;
        HKEY Key;
    } roots[] = {
        {L"CLASSES_ROOT", HKEY_CLASSES_ROOT}, {L"CURRENT_USER", HKEY_CURRENT_USER},
        {L"MACHINE", HKEY_LOCAL_MACHINE},     {L"USERS", HKEY_USERS},
        {L"CURRENT_CONFIG", HKEY_CURRENT_CONFIG},
    };
    *out = NULL;
    if (info & SACL_SECURITY_INFORMATION)
        return ERROR_PRIVILEGE_NOT_HELD;
    HKEY root = NULL;
    PCWSTR rest = NULL;
    for (ULONG i = 0; i < RTL_NUMBER_OF(roots) && !root; i++) {
        SIZE_T n = wcslen(roots[i].Name);
        if (!_wcsnicmp(name, roots[i].Name, n) && (name[n] == 0 || name[n] == L'\\')) {
            root = roots[i].Key;
            rest = name[n] ? name + n + 1 : L"";
        }
    }
    if (!root)
        return ERROR_INVALID_PARAMETER;
    HKEY key;
    LONG error = RegOpenKeyExW(root, rest, 0, READ_CONTROL, &key);
    if (error)
        return error;
    error = query_object(key, info, out);
    RegCloseKey(key);
    if (error != ERROR_CALL_NOT_IMPLEMENTED)
        return error;
    return synthesize(out, info, KEY_ALL_ACCESS, KEY_READ, CONTAINER_INHERIT_ACE | INHERITED_ACE,
                      root == HKEY_CURRENT_USER);
}

ADVAPI BOOL WINAPI GetFileSecurityW(LPCWSTR name, SECURITY_INFORMATION info, PSECURITY_DESCRIPTOR sd, DWORD length,
                                    LPDWORD needed)
{
    if (!name || !needed) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BYTE *copy;
    DWORD error = file_security(name, info, &copy);
    if (error) {
        AdvSetError(error);
        return FALSE;
    }
    DWORD size = (DWORD)LocalSize(copy);
    *needed = size;
    BOOL ok = sd && length >= size;
    if (ok)
        memcpy(sd, copy, size);
    else
        AdvSetError(ERROR_INSUFFICIENT_BUFFER);
    LocalFree(copy);
    return ok;
}

ADVAPI BOOL WINAPI GetFileSecurityA(LPCSTR name, SECURITY_INFORMATION info, PSECURITY_DESCRIPTOR sd, DWORD length,
                                    LPDWORD needed)
{
    if (!name) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PWSTR wide = AdvAnsiToWide(name);
    BOOL ok = wide && GetFileSecurityW(wide, info, sd, length, needed);
    AdvFree(wide);
    return ok;
}

ADVAPI DWORD WINAPI GetNamedSecurityInfoW(LPCWSTR name, SE_OBJECT_TYPE type, SECURITY_INFORMATION info, PSID *owner,
                                          PSID *group, PACL *dacl, PACL *sacl, PSECURITY_DESCRIPTOR *sd)
{
    if (!name || (!sd && (owner || group || dacl || sacl)))
        return ERROR_INVALID_PARAMETER;
    BYTE *copy = NULL;
    DWORD error;
    switch (type) {
    case SE_FILE_OBJECT:
        error = file_security(name, info, &copy);
        break;
    case SE_REGISTRY_KEY:
    case SE_REGISTRY_WOW64_32KEY:
    case SE_REGISTRY_WOW64_64KEY:
        error = key_security(name, info, &copy);
        break;
    case SE_SERVICE:
        error = ERROR_SERVICE_DOES_NOT_EXIST; /* there is no service control manager */
        break;
    case SE_PRINTER:
        error = 1801; /* ERROR_INVALID_PRINTER_NAME */
        break;
    case SE_LMSHARE:
        error = 2310; /* NERR_NetNameNotFound */
        break;
    default:
        error = ERROR_INVALID_PARAMETER;
        break;
    }
    if (error)
        return error;
    SD_PARTS p;
    sd_parts(copy, &p);
    if (owner)
        *owner = info & OWNER_SECURITY_INFORMATION ? p.Owner : NULL;
    if (group)
        *group = info & GROUP_SECURITY_INFORMATION ? p.Group : NULL;
    if (dacl)
        *dacl = info & DACL_SECURITY_INFORMATION ? p.Dacl : NULL;
    if (sacl)
        *sacl = NULL;
    if (sd)
        *sd = copy;
    else
        LocalFree(copy);
    return ERROR_SUCCESS;
}

ADVAPI DWORD WINAPI GetNamedSecurityInfoA(LPCSTR name, SE_OBJECT_TYPE type, SECURITY_INFORMATION info, PSID *owner,
                                          PSID *group, PACL *dacl, PACL *sacl, PSECURITY_DESCRIPTOR *sd)
{
    if (!name)
        return ERROR_INVALID_PARAMETER;
    PWSTR wide = AdvAnsiToWide(name);
    if (!wide)
        return ERROR_NOT_ENOUGH_MEMORY;
    DWORD error = GetNamedSecurityInfoW(wide, type, info, owner, group, dacl, sacl, sd);
    AdvFree(wide);
    return error;
}
