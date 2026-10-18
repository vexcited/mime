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
        p->Owner = r->Owner ? (SID *)(base + r->Owner) : nullptr;
        p->Group = r->Group ? (SID *)(base + r->Group) : nullptr;
        p->Sacl = r->Sacl && (r->Control & SE_SACL_PRESENT) ? (ACL *)(base + r->Sacl) : nullptr;
        p->Dacl = r->Dacl && (r->Control & SE_DACL_PRESENT) ? (ACL *)(base + r->Dacl) : nullptr;
    } else {
        p->Owner = a->Owner;
        p->Group = a->Group;
        p->Sacl = a->Control & SE_SACL_PRESENT ? a->Sacl : nullptr;
        p->Dacl = a->Control & SE_DACL_PRESENT ? a->Dacl : nullptr;
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
            return nullptr;
        if (i == index)
            return (ACE_HEADER *)h;
        p += h->AceSize;
    }
    return nullptr;
}

static BOOL acl_valid(const ACL *acl)
{
    return RtlValidAcl((PACL)acl);
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

/* a node of an object type tree. */
typedef struct OBJECT_TYPE_LIST {
    WORD Level;
    WORD Sbz;
    GUID *ObjectType;
} OBJECT_TYPE_LIST;

#define ACCESS_MAX_LEVEL 4

/* PRINCIPAL_SELF stands for the sid the caller names in the aces that hold it. */
static const SID *ace_sid(const SID *sid, const SID *self)
{
    static const BYTE principal_self[12] = {1, 1, 0, 0, 0, 0, 0, 5, 10, 0, 0, 0};
    return self && AdvSidEqual(sid, (const SID *)principal_self) ? self : sid;
}

/* the last node below the one at index, in the order the list is given. */
static DWORD subtree_end(const OBJECT_TYPE_LIST *list, DWORD count, DWORD index)
{
    DWORD end = index + 1;
    while (end < count && list[end].Level > list[index].Level)
        end++;
    return end;
}

/*
 * the user mode version of SeAccessCheck, for kernels without NtAccessCheck. every node of the object type
 * tree has its own granted and denied bits: an ace for a type reaches its subtree, and a deny also reaches the
 * nodes above, since the whole is not usable when a part of it is forbidden. without a tree there is one node.
 */
static void evaluate_tree(const ACL *dacl, const IDENTITY *id, const GENERIC_MAPPING *mapping, const SID *self,
                          const OBJECT_TYPE_LIST *list, DWORD count, ACCESS_MASK *allowed, ACCESS_MASK *denied)
{
    DWORD nodes = count ? count : 1;
    for (DWORD i = 0; i < dacl->AceCount; i++) {
        const ACCESS_ALLOWED_ACE *ace = (const ACCESS_ALLOWED_ACE *)ace_at(dacl, i);
        if (!ace)
            break;
        if (ace->Header.AceFlags & INHERIT_ONLY_ACE)
            continue;
        BYTE type = ace->Header.AceType;
        const SID *sid;
        DWORD first = 0, last = nodes, ancestors = 0;
        if (type == ACCESS_ALLOWED_ACE_TYPE || type == ACCESS_DENIED_ACE_TYPE) {
            if (ace->Header.AceSize < offsetof(ACCESS_ALLOWED_ACE, SidStart) + 8)
                continue;
            sid = (const SID *)&ace->SidStart;
        } else if (type == ACCESS_ALLOWED_OBJECT_ACE_TYPE || type == ACCESS_DENIED_OBJECT_ACE_TYPE) {
            const BYTE *raw = (const BYTE *)ace;
            DWORD flags = *(const DWORD *)(raw + 8);
            DWORD offset = 12 + 16 * (flags & 1) + 16 * ((flags >> 1) & 1);
            if (ace->Header.AceSize < offset + 8)
                continue;
            sid = (const SID *)(raw + offset);
            if (flags & 1) {
                /* an ace for a type needs the node of that type. */
                const GUID *guid = (const GUID *)(raw + 12);
                DWORD node = 0;
                while (node < count && memcmp(list[node].ObjectType, guid, sizeof(GUID)) != 0)
                    node++;
                if (node == count)
                    continue;
                first = node;
                last = subtree_end(list, count, node);
                ancestors = node;
            }
        } else {
            continue;
        }
        DWORD mask = ace->Mask;
        if (mapping)
            MapGenericMask(&mask, (PGENERIC_MAPPING)mapping);
        if (!identity_matches(id, ace_sid(sid, self), type == ACCESS_DENIED_ACE_TYPE ||
                                                           type == ACCESS_DENIED_OBJECT_ACE_TYPE))
            continue;
        if (type == ACCESS_ALLOWED_ACE_TYPE || type == ACCESS_ALLOWED_OBJECT_ACE_TYPE) {
            for (DWORD n = first; n < last; n++)
                allowed[n] |= mask & ~denied[n];
        } else {
            for (DWORD n = first; n < last; n++)
                denied[n] |= mask & ~allowed[n];
            /* the nodes above, the ones whose subtree holds the denied one. */
            DWORD level = first < count ? list[first].Level : 0;
            for (DWORD n = ancestors; n > 0 && level > 0; n--) {
                if (list[n - 1].Level < level) {
                    denied[n - 1] |= mask & ~allowed[n - 1];
                    level = list[n - 1].Level;
                }
            }
        }
    }
}

/* the tree has to start at level zero and go down one level at a time, with a type for each node. */
static BOOL object_types_valid(const OBJECT_TYPE_LIST *list, DWORD count)
{
    if (list[0].Level != 0)
        return FALSE;
    for (DWORD i = 0; i < count; i++) {
        if (!list[i].ObjectType || list[i].Level > ACCESS_MAX_LEVEL || (i && list[i].Level > list[i - 1].Level + 1) ||
            (i && list[i].Level == 0))
            return FALSE;
        for (DWORD j = 0; j < i; j++)
            if (memcmp(list[i].ObjectType, list[j].ObjectType, sizeof(GUID)) == 0)
                return FALSE;
    }
    return TRUE;
}

/* the check of every node. results[i] is the granted access, or zero when the node was refused. */
static BOOL access_check(const SD_PARTS *p, HANDLE token, DWORD desired, PGENERIC_MAPPING mapping, const SID *self,
                         const OBJECT_TYPE_LIST *list, DWORD count, DWORD *results, BOOL *statuses)
{
    IDENTITY id = {AdvQueryTokenUser(token), AdvQueryTokenGroups(token), nullptr};
    if (!id.User || !id.Groups) {
        AdvFree(id.User);
        AdvFree(id.Groups);
        AdvSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    DWORD nodes = count ? count : 1;
    ACCESS_MASK stack[16];
    ACCESS_MASK *masks = nodes <= 8 ? stack : AdvAlloc(nodes * 2 * sizeof(ACCESS_MASK));
    if (!masks) {
        AdvFree(id.User);
        AdvFree(id.Groups);
        AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    ACCESS_MASK *allowed = masks, *denied = masks + nodes;
    memset(masks, 0, nodes * 2 * sizeof(ACCESS_MASK));
    ACCESS_MASK wanted = desired & ~MAXIMUM_ALLOWED;
    if (!(p->Control & SE_DACL_PRESENT) || !p->Dacl) {
        for (DWORD n = 0; n < nodes; n++)
            allowed[n] = mapping->GenericAll | (desired & ~(MAXIMUM_ALLOWED | ACCESS_SYSTEM_SECURITY));
    } else {
        /* the owner may always read and change the dacl. */
        if (p->Owner && identity_matches(&id, p->Owner, FALSE))
            for (DWORD n = 0; n < nodes; n++)
                allowed[n] = READ_CONTROL | WRITE_DAC;
        evaluate_tree(p->Dacl, &id, mapping, self, list, count, allowed, denied);
    }
    AdvFree(id.User);
    AdvFree(id.Groups);
    for (DWORD n = 0; n < nodes; n++) {
        ACCESS_MASK granted = allowed[n] & ~ACCESS_SYSTEM_SECURITY; /* needs SeSecurityPrivilege */
        ACCESS_MASK result = desired & MAXIMUM_ALLOWED ? granted | wanted : wanted;
        BOOL ok = (wanted & ~granted) == 0 && result != 0;
        results[n] = ok ? (desired & MAXIMUM_ALLOWED ? granted : wanted) : 0;
        statuses[n] = ok;
    }
    if (masks != stack)
        AdvFree(masks);
    return TRUE;
}

/* what the access check functions share before they look at the descriptor. */
static BOOL check_arguments(PSECURITY_DESCRIPTOR sd, DWORD desired, PGENERIC_MAPPING mapping, PPRIVILEGE_SET privileges,
                            LPDWORD privileges_length, const OBJECT_TYPE_LIST *list, DWORD count, SD_PARTS *p)
{
    if (!sd_parts(sd, p) || !p->Owner || !p->Group) {
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
    if (count && (!list || !object_types_valid(list, count))) {
        AdvSetError(list ? ERROR_INVALID_PARAMETER : ERROR_NOACCESS);
        return FALSE;
    }
    privileges->PrivilegeCount = 0;
    privileges->Control = 0;
    *privileges_length = sizeof(PRIVILEGE_SET);
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
    DWORD token_error = AdvImpersonationError(token);
    if (token_error) {
        AdvSetError(token_error);
        return FALSE;
    }
    SD_PARTS p;
    if (!check_arguments(sd, desired, mapping, privileges, privileges_length, nullptr, 0, &p))
        return FALSE;
    if (!access_check(&p, token, desired, mapping, nullptr, nullptr, 0, granted, status))
        return FALSE;
    if (!*status)
        AdvSetError(ERROR_ACCESS_DENIED);
    return TRUE;
}

/* the root of the tree decides, which a refused part of the tree has already told. */
ADVAPI BOOL WINAPI AccessCheckByType(PSECURITY_DESCRIPTOR sd, PSID self, HANDLE token, DWORD desired,
                                     OBJECT_TYPE_LIST *list, DWORD count, PGENERIC_MAPPING mapping,
                                     PPRIVILEGE_SET privileges, LPDWORD privileges_length, LPDWORD granted,
                                     LPBOOL status)
{
    if (!mapping || !privileges_length || !granted || !status) {
        AdvSetError(ERROR_NOACCESS);
        return FALSE;
    }
    DWORD token_error = AdvImpersonationError(token);
    if (token_error) {
        AdvSetError(token_error);
        return FALSE;
    }
    SD_PARTS p;
    if (!check_arguments(sd, desired, mapping, privileges, privileges_length, list, count, &p))
        return FALSE;
    DWORD node_granted[1];
    BOOL node_status[1];
    DWORD *results = node_granted;
    BOOL *statuses = node_status;
    DWORD nodes = count ? count : 1;
    DWORD *heap = nullptr;
    if (nodes > 1) {
        heap = AdvAlloc(nodes * (sizeof(DWORD) + sizeof(BOOL)));
        if (!heap) {
            AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        results = heap;
        statuses = (BOOL *)(heap + nodes);
    }
    BOOL ok = access_check(&p, token, desired, mapping, self, list, count, results, statuses);
    if (ok) {
        *granted = results[0];
        *status = statuses[0];
        if (!*status)
            AdvSetError(ERROR_ACCESS_DENIED);
    }
    AdvFree(heap);
    return ok;
}

/* every node has its own access and status, the status being zero or an error code. */
ADVAPI BOOL WINAPI AccessCheckByTypeResultList(PSECURITY_DESCRIPTOR sd, PSID self, HANDLE token, DWORD desired,
                                               OBJECT_TYPE_LIST *list, DWORD count, PGENERIC_MAPPING mapping,
                                               PPRIVILEGE_SET privileges, LPDWORD privileges_length,
                                               LPDWORD granted, LPDWORD statuses)
{
    if (!mapping || !privileges_length || !granted || !statuses || !count) {
        AdvSetError(count ? ERROR_NOACCESS : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    DWORD token_error = AdvImpersonationError(token);
    if (token_error) {
        AdvSetError(token_error);
        return FALSE;
    }
    SD_PARTS p;
    if (!check_arguments(sd, desired, mapping, privileges, privileges_length, list, count, &p))
        return FALSE;
    BOOL *flags = AdvAlloc(count * sizeof(BOOL));
    if (!flags) {
        AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    BOOL ok = access_check(&p, token, desired, mapping, self, list, count, granted, flags);
    if (ok)
        for (DWORD i = 0; i < count; i++)
            statuses[i] = flags[i] ? ERROR_SUCCESS : ERROR_ACCESS_DENIED;
    AdvFree(flags);
    return ok;
}

/* ---------------------------------------------------------------- trustees */

/* the rights an acl gives a trustee, or the rights it audits for it when audit_failed is given. */
static DWORD effective_rights(PACL acl, PTRUSTEE_W trustee, BOOL ansi, PACCESS_MASK rights, PACCESS_MASK audit_failed)
{
    if (!trustee || !rights)
        return ERROR_INVALID_PARAMETER;
    BYTE storage[SECURITY_MAX_SID_SIZE];
    const SID *sid = nullptr;
    PCWSTR name = nullptr;
    switch (trustee->TrusteeForm) {
    case TRUSTEE_IS_SID:
        sid = (const SID *)trustee->ptstrName;
        break;
    case TRUSTEE_IS_OBJECTS_AND_SID:
        sid = trustee->ptstrName ? ((OBJECTS_AND_SID *)trustee->ptstrName)->pSid : nullptr;
        break;
    case TRUSTEE_IS_NAME:
        name = trustee->ptstrName;
        break;
    case TRUSTEE_IS_OBJECTS_AND_NAME:
        name = trustee->ptstrName ? ((OBJECTS_AND_NAME_W *)trustee->ptstrName)->ptstrName : nullptr;
        break;
    default:
        return ERROR_INVALID_PARAMETER;
    }
    if (name) {
        PWSTR wide = ansi ? AdvAnsiToWide((LPCSTR)name) : nullptr;
        WCHAR domain[256];
        DWORD size = sizeof(storage), domain_size = RTL_NUMBER_OF(domain);
        SID_NAME_USE use;
        BOOL found = LookupAccountNameW(nullptr, wide ? wide : name, storage, &size, domain, &domain_size, &use);
        AdvFree(wide);
        if (!found)
            return ERROR_NONE_MAPPED;
        sid = (const SID *)storage;
    }
    if (!AdvSidValid(sid))
        return ERROR_INVALID_SID;
    if (!acl) {
        /* no audit entries, which the account lookup behind this call reports as a bad parameter. */
        if (audit_failed)
            return ERROR_INVALID_PARAMETER;
        *rights = STANDARD_RIGHTS_ALL | SPECIFIC_RIGHTS_ALL;
        return ERROR_SUCCESS;
    }
    if (!acl_valid(acl))
        return ERROR_INVALID_ACL;
    IDENTITY id = {AdvQueryTokenUser(nullptr), nullptr, nullptr};
    if (id.User && AdvSidEqual(id.User->User.Sid, sid))
        id.Groups = AdvQueryTokenGroups(nullptr);
    if (!id.Groups)
        id.Sid = sid;
    if (audit_failed) {
        /* every audit ace of the trustee or of a group it is in adds its mask for the outcomes it names. */
        ACCESS_MASK successful = 0, failed = 0;
        BOOL any = FALSE;
        for (DWORD i = 0; i < acl->AceCount; i++) {
            const ACCESS_ALLOWED_ACE *ace = (const ACCESS_ALLOWED_ACE *)ace_at(acl, i);
            if (!ace)
                break;
            BYTE type = ace->Header.AceType;
            const BYTE *raw = (const BYTE *)ace;
            const SID *ace_sid_ptr;
            if (type == SYSTEM_AUDIT_ACE_TYPE) {
                ace_sid_ptr = (const SID *)&ace->SidStart;
            } else if (type == SYSTEM_AUDIT_OBJECT_ACE_TYPE) {
                DWORD flags = *(const DWORD *)(raw + 8);
                ace_sid_ptr = (const SID *)(raw + 12 + 16 * (flags & 1) + 16 * ((flags >> 1) & 1));
            } else {
                continue;
            }
            any = TRUE;
            if (!identity_matches(&id, ace_sid_ptr, FALSE))
                continue;
            if (ace->Header.AceFlags & SUCCESSFUL_ACCESS_ACE_FLAG)
                successful |= ace->Mask;
            if (ace->Header.AceFlags & FAILED_ACCESS_ACE_FLAG)
                failed |= ace->Mask;
        }
        AdvFree(id.User);
        AdvFree(id.Groups);
        if (!any)
            return ERROR_INVALID_PARAMETER;
        *rights = successful;
        *audit_failed = failed;
        return ERROR_SUCCESS;
    }
    ACCESS_MASK allowed = 0, denied = 0;
    evaluate_tree(acl, &id, nullptr, nullptr, nullptr, 0, &allowed, &denied);
    AdvFree(id.User);
    AdvFree(id.Groups);
    *rights = allowed & ~denied;
    return ERROR_SUCCESS;
}

ADVAPI DWORD WINAPI GetEffectiveRightsFromAclW(PACL acl, PTRUSTEE_W trustee, PACCESS_MASK rights)
{
    return effective_rights(acl, trustee, FALSE, rights, nullptr);
}

ADVAPI DWORD WINAPI GetEffectiveRightsFromAclA(PACL acl, PTRUSTEE_W trustee, PACCESS_MASK rights)
{
    return effective_rights(acl, trustee, TRUE, rights, nullptr);
}

ADVAPI DWORD WINAPI GetAuditedPermissionsFromAclW(PACL acl, PTRUSTEE_W trustee, PACCESS_MASK successful,
                                                  PACCESS_MASK failed)
{
    if (!successful || !failed)
        return ERROR_INVALID_PARAMETER;
    return effective_rights(acl, trustee, FALSE, successful, failed);
}

ADVAPI DWORD WINAPI GetAuditedPermissionsFromAclA(PACL acl, PTRUSTEE_W trustee, PACCESS_MASK successful,
                                                  PACCESS_MASK failed)
{
    if (!successful || !failed)
        return ERROR_INVALID_PARAMETER;
    return effective_rights(acl, trustee, TRUE, successful, failed);
}
