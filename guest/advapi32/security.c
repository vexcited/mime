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
    IDENTITY id = {AdvQueryTokenUser(token), AdvQueryTokenGroups(token), nullptr};
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

static DWORD effective_rights(PACL acl, PTRUSTEE_W trustee, BOOL ansi, PACCESS_MASK rights)
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
    ACCESS_MASK allowed = 0, denied = 0;
    evaluate_dacl(acl, &id, nullptr, &allowed, &denied);
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
    PVOID primary = user_owned ? AdvQueryToken(nullptr, TokenPrimaryGroup) : nullptr;
    ACL *acl = (ACL *)acl_storage;
    RtlCreateAcl(acl, sizeof(acl_storage), ACL_REVISION);
    RtlAddAccessAllowedAceEx(acl, ACL_REVISION, ace_flags, all, system);
    RtlAddAccessAllowedAceEx(acl, ACL_REVISION, ace_flags, all, admins);
    if (user_owned)
        RtlAddAccessAllowedAceEx(acl, ACL_REVISION, ace_flags, all, user);
    else
        RtlAddAccessAllowedAceEx(acl, ACL_REVISION, ace_flags, users_read, users);
    SECURITY_DESCRIPTOR sd;
    RtlCreateSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    if (info & OWNER_SECURITY_INFORMATION)
        RtlSetOwnerSecurityDescriptor(&sd, user_owned ? (PSID)user : (PSID)admins, FALSE);
    if (info & GROUP_SECURITY_INFORMATION)
        RtlSetGroupSecurityDescriptor(&sd, primary ? *(PSID *)primary : (PSID)system, FALSE);
    if (info & DACL_SECURITY_INFORMATION) {
        RtlSetDaclSecurityDescriptor(&sd, TRUE, acl, FALSE);
        sd.Control |= SE_DACL_AUTO_INHERITED;
    }
    ULONG size = 0;
    NTSTATUS status = RtlAbsoluteToSelfRelativeSD(&sd, nullptr, &size);
    *out = status == STATUS_BUFFER_TOO_SMALL ? LocalAlloc(LMEM_FIXED, size) : nullptr;
    if (*out && !NT_SUCCESS(RtlAbsoluteToSelfRelativeSD(&sd, *out, &size))) {
        LocalFree(*out);
        *out = nullptr;
    }
    AdvFree(primary);
    return *out ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
}

/* the descriptor the kernel keeps for an object, or ERROR_CALL_NOT_IMPLEMENTED when it keeps none. */
static DWORD query_object(HANDLE handle, DWORD info, BYTE **out)
{
    ULONG needed = 0;
    NTSTATUS status = NtQuerySecurityObject(handle, info, nullptr, 0, &needed);
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
        *out = nullptr;
        return RtlNtStatusToDosError(status);
    }
    return ERROR_SUCCESS;
}

static DWORD file_security(LPCWSTR name, DWORD info, BYTE **out)
{
    *out = nullptr;
    if (info & SACL_SECURITY_INFORMATION)
        return ERROR_PRIVILEGE_NOT_HELD;
    DWORD attributes = GetFileAttributesW(name);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return GetLastError();
    HANDLE h = CreateFileW(name, READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
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
    *out = nullptr;
    if (info & SACL_SECURITY_INFORMATION)
        return ERROR_PRIVILEGE_NOT_HELD;
    HKEY root = nullptr;
    PCWSTR rest = nullptr;
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
    BYTE *copy = nullptr;
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
        *owner = info & OWNER_SECURITY_INFORMATION ? p.Owner : nullptr;
    if (group)
        *group = info & GROUP_SECURITY_INFORMATION ? p.Group : nullptr;
    if (dacl)
        *dacl = info & DACL_SECURITY_INFORMATION ? p.Dacl : nullptr;
    if (sacl)
        *sacl = nullptr;
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

/* ---------------------------------------------------------------- setting object security */

/*
 * the kernel keeps no descriptors on files or keys, so a valid change is
 * accepted without being stored: queries keep answering the synthesized
 * descriptor, which already grants the user what installers ask for.
 */
static DWORD check_new_security(SECURITY_INFORMATION info, const SID *owner, const SID *group, const ACL *dacl,
                                const ACL *sacl)
{
    if (info & (SACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION))
        return ERROR_PRIVILEGE_NOT_HELD;
    if ((info & OWNER_SECURITY_INFORMATION) && owner && !AdvSidValid(owner))
        return ERROR_INVALID_OWNER;
    if ((info & GROUP_SECURITY_INFORMATION) && group && !AdvSidValid(group))
        return 1308; /* ERROR_INVALID_PRIMARY_GROUP */
    if ((info & DACL_SECURITY_INFORMATION) && dacl && !acl_valid(dacl))
        return ERROR_INVALID_ACL;
    return ERROR_SUCCESS;
}

static DWORD check_descriptor(SECURITY_INFORMATION info, PSECURITY_DESCRIPTOR sd)
{
    SD_PARTS p;
    if (!sd || !sd_parts(sd, &p))
        return ERROR_INVALID_SECURITY_DESCR;
    return check_new_security(info, p.Owner, p.Group, p.Dacl, p.Sacl);
}

ADVAPI BOOL WINAPI SetFileSecurityW(LPCWSTR name, SECURITY_INFORMATION info, PSECURITY_DESCRIPTOR sd)
{
    if (!name) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (GetFileAttributesW(name) == INVALID_FILE_ATTRIBUTES)
        return FALSE;
    DWORD error = check_descriptor(info, sd);
    if (error)
        AdvSetError(error);
    return !error;
}

ADVAPI BOOL WINAPI SetFileSecurityA(LPCSTR name, SECURITY_INFORMATION info, PSECURITY_DESCRIPTOR sd)
{
    if (!name) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PWSTR wide = AdvAnsiToWide(name);
    BOOL ok = wide && SetFileSecurityW(wide, info, sd);
    AdvFree(wide);
    return ok;
}

ADVAPI DWORD WINAPI SetNamedSecurityInfoW(LPWSTR name, SE_OBJECT_TYPE type, SECURITY_INFORMATION info, PSID owner,
                                          PSID group, PACL dacl, PACL sacl)
{
    if (!name)
        return ERROR_INVALID_PARAMETER;
    BYTE *current = nullptr;
    DWORD error;
    /* the existing descriptor query also proves the object exists. */
    switch (type) {
    case SE_FILE_OBJECT:
        error = file_security(name, OWNER_SECURITY_INFORMATION, &current);
        break;
    case SE_REGISTRY_KEY:
    case SE_REGISTRY_WOW64_32KEY:
    case SE_REGISTRY_WOW64_64KEY:
        error = key_security(name, OWNER_SECURITY_INFORMATION, &current);
        break;
    case SE_SERVICE:
        return ERROR_SERVICE_DOES_NOT_EXIST;
    default:
        return ERROR_INVALID_PARAMETER;
    }
    LocalFree(current);
    if (error)
        return error;
    return check_new_security(info, owner, group, dacl, sacl);
}

ADVAPI DWORD WINAPI SetNamedSecurityInfoA(LPSTR name, SE_OBJECT_TYPE type, SECURITY_INFORMATION info, PSID owner,
                                          PSID group, PACL dacl, PACL sacl)
{
    if (!name)
        return ERROR_INVALID_PARAMETER;
    PWSTR wide = AdvAnsiToWide(name);
    if (!wide)
        return ERROR_NOT_ENOUGH_MEMORY;
    DWORD error = SetNamedSecurityInfoW(wide, type, info, owner, group, dacl, sacl);
    AdvFree(wide);
    return error;
}

static DWORD check_key(HKEY key)
{
    DWORD subkeys;
    return RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
}

ADVAPI LSTATUS WINAPI RegSetKeySecurity(HKEY key, SECURITY_INFORMATION info, PSECURITY_DESCRIPTOR sd)
{
    DWORD error = check_key(key);
    return error ? error : check_descriptor(info, sd);
}

ADVAPI LSTATUS WINAPI RegGetKeySecurity(HKEY key, SECURITY_INFORMATION info, PSECURITY_DESCRIPTOR sd, LPDWORD size)
{
    if (!size)
        return ERROR_INVALID_PARAMETER;
    DWORD error = check_key(key);
    if (error)
        return error;
    if (info & SACL_SECURITY_INFORMATION)
        return ERROR_PRIVILEGE_NOT_HELD;
    BYTE *copy = nullptr;
    error = query_object(key, info, &copy);
    if (error == ERROR_CALL_NOT_IMPLEMENTED)
        error = synthesize(&copy, info, KEY_ALL_ACCESS, KEY_READ, CONTAINER_INHERIT_ACE | INHERITED_ACE, FALSE);
    if (error)
        return error;
    DWORD need = (DWORD)LocalSize(copy);
    if (!sd || *size < need)
        error = ERROR_INSUFFICIENT_BUFFER;
    else
        memcpy(sd, copy, need);
    *size = need;
    LocalFree(copy);
    return error;
}

/* ---------------------------------------------------------------- security of handles */

ADVAPI DWORD WINAPI GetSecurityInfo(HANDLE handle, SE_OBJECT_TYPE type, SECURITY_INFORMATION info, PSID *owner,
                                    PSID *group, PACL *dacl, PACL *sacl, PSECURITY_DESCRIPTOR *sd)
{
    if (!handle || (!sd && (owner || group || dacl || sacl)))
        return ERROR_INVALID_PARAMETER;
    if (info & SACL_SECURITY_INFORMATION)
        return ERROR_PRIVILEGE_NOT_HELD;
    BYTE *copy = nullptr;
    DWORD error = query_object(handle, info, &copy);
    /* objects the kernel keeps no descriptor for get what windows gives a new one of the user. */
    if (error == ERROR_CALL_NOT_IMPLEMENTED) {
        switch (type) {
        case SE_FILE_OBJECT:
            error = synthesize(&copy, info, FILE_ALL_ACCESS, 0, 0, TRUE);
            break;
        case SE_REGISTRY_KEY:
        case SE_REGISTRY_WOW64_32KEY:
        case SE_REGISTRY_WOW64_64KEY:
            error = synthesize(&copy, info, KEY_ALL_ACCESS, KEY_READ, 0, TRUE);
            break;
        default:
            error = synthesize(&copy, info, GENERIC_ALL, 0, 0, TRUE);
            break;
        }
    }
    if (error)
        return error;
    SD_PARTS p;
    sd_parts(copy, &p);
    if (owner)
        *owner = info & OWNER_SECURITY_INFORMATION ? p.Owner : nullptr;
    if (group)
        *group = info & GROUP_SECURITY_INFORMATION ? p.Group : nullptr;
    if (dacl)
        *dacl = info & DACL_SECURITY_INFORMATION ? p.Dacl : nullptr;
    if (sacl)
        *sacl = nullptr;
    if (sd)
        *sd = copy;
    else
        LocalFree(copy);
    return ERROR_SUCCESS;
}

ADVAPI DWORD WINAPI SetSecurityInfo(HANDLE handle, SE_OBJECT_TYPE type, SECURITY_INFORMATION info, PSID owner,
                                    PSID group, PACL dacl, PACL sacl)
{
    if (!handle)
        return ERROR_INVALID_HANDLE;
    return check_new_security(info, owner, group, dacl, sacl);
}
