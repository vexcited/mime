/*
 * trustees and explicit access: the structures that name who gets what, and the functions that build lists and
 * descriptors from them.
 *
 * SetEntriesInAcl merges entries into a list the way the access control provider of windows does: it drops what a
 * set or revoke entry replaces, folds a grant into the ace of the same trustee and inheritance when the list is
 * in canonical order, and writes the new denies before the old explicit aces and the new grants after them.
 */
#include "advapi32_private.h"

typedef struct _TRUSTEE_A {
    struct _TRUSTEE_A *pMultipleTrustee;
    MULTIPLE_TRUSTEE_OPERATION MultipleTrusteeOperation;
    TRUSTEE_FORM TrusteeForm;
    TRUSTEE_TYPE TrusteeType;
    LPSTR ptstrName;
} TRUSTEE_A, *PTRUSTEE_A;

typedef struct _EXPLICIT_ACCESS_A {
    DWORD grfAccessPermissions;
    ACCESS_MODE grfAccessMode;
    DWORD grfInheritance;
    TRUSTEE_A Trustee;
} EXPLICIT_ACCESS_A, *PEXPLICIT_ACCESS_A;

typedef struct _OBJECTS_AND_NAME_A {
    DWORD ObjectsPresent;
    SE_OBJECT_TYPE ObjectType;
    LPSTR ObjectTypeName;
    LPSTR InheritedObjectTypeName;
    LPSTR ptstrName;
} OBJECTS_AND_NAME_A, *POBJECTS_AND_NAME_A;

/* ---------------------------------------------------------------- building trustees */

static void init_trustee(PTRUSTEE_W trustee, TRUSTEE_FORM form, PVOID name)
{
    trustee->pMultipleTrustee = nullptr;
    trustee->MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE;
    trustee->TrusteeForm = form;
    trustee->TrusteeType = TRUSTEE_IS_UNKNOWN;
    trustee->ptstrName = name;
}

ADVAPI void WINAPI BuildTrusteeWithSidW(PTRUSTEE_W trustee, PSID sid)
{
    init_trustee(trustee, TRUSTEE_IS_SID, sid);
}

ADVAPI void WINAPI BuildTrusteeWithSidA(PTRUSTEE_W trustee, PSID sid)
{
    init_trustee(trustee, TRUSTEE_IS_SID, sid);
}

ADVAPI void WINAPI BuildTrusteeWithNameW(PTRUSTEE_W trustee, LPWSTR name)
{
    init_trustee(trustee, TRUSTEE_IS_NAME, name);
}

ADVAPI void WINAPI BuildTrusteeWithNameA(PTRUSTEE_W trustee, LPSTR name)
{
    init_trustee(trustee, TRUSTEE_IS_NAME, name);
}

ADVAPI void WINAPI BuildTrusteeWithObjectsAndSidW(PTRUSTEE_W trustee, POBJECTS_AND_SID objects, GUID *object,
                                                  GUID *inherited, PSID sid)
{
    init_trustee(trustee, TRUSTEE_IS_OBJECTS_AND_SID, objects);
    memset(&objects->ObjectTypeGuid, 0, sizeof(GUID));
    memset(&objects->InheritedObjectTypeGuid, 0, sizeof(GUID));
    objects->ObjectsPresent = 0;
    if (object) {
        objects->ObjectsPresent |= ACE_OBJECT_TYPE_PRESENT;
        objects->ObjectTypeGuid = *object;
    }
    if (inherited) {
        objects->ObjectsPresent |= ACE_INHERITED_OBJECT_TYPE_PRESENT;
        objects->InheritedObjectTypeGuid = *inherited;
    }
    objects->pSid = sid;
}

ADVAPI void WINAPI BuildTrusteeWithObjectsAndSidA(PTRUSTEE_W trustee, POBJECTS_AND_SID objects, GUID *object,
                                                  GUID *inherited, PSID sid)
{
    BuildTrusteeWithObjectsAndSidW(trustee, objects, object, inherited, sid);
}

ADVAPI void WINAPI BuildTrusteeWithObjectsAndNameW(PTRUSTEE_W trustee, POBJECTS_AND_NAME_W objects,
                                                   SE_OBJECT_TYPE type, LPWSTR object_name, LPWSTR inherited_name,
                                                   LPWSTR name)
{
    init_trustee(trustee, TRUSTEE_IS_OBJECTS_AND_NAME, objects);
    objects->ObjectsPresent = 0;
    objects->ObjectType = type;
    objects->ObjectTypeName = object_name;
    objects->InheritedObjectTypeName = inherited_name;
    if (object_name)
        objects->ObjectsPresent |= ACE_OBJECT_TYPE_PRESENT;
    if (inherited_name)
        objects->ObjectsPresent |= ACE_INHERITED_OBJECT_TYPE_PRESENT;
    objects->ptstrName = name;
}

ADVAPI void WINAPI BuildTrusteeWithObjectsAndNameA(PTRUSTEE_W trustee, POBJECTS_AND_NAME_A objects,
                                                   SE_OBJECT_TYPE type, LPSTR object_name, LPSTR inherited_name,
                                                   LPSTR name)
{
    BuildTrusteeWithObjectsAndNameW(trustee, (POBJECTS_AND_NAME_W)objects, type, (LPWSTR)object_name,
                                    (LPWSTR)inherited_name, (LPWSTR)name);
}

ADVAPI void WINAPI BuildImpersonateTrusteeW(PTRUSTEE_W trustee, PTRUSTEE_W impersonate)
{
    trustee->pMultipleTrustee = impersonate;
    trustee->MultipleTrusteeOperation = TRUSTEE_IS_IMPERSONATE;
}

ADVAPI void WINAPI BuildImpersonateTrusteeA(PTRUSTEE_W trustee, PTRUSTEE_W impersonate)
{
    BuildImpersonateTrusteeW(trustee, impersonate);
}

ADVAPI void WINAPI BuildExplicitAccessWithNameW(PEXPLICIT_ACCESS_W access, LPWSTR name, DWORD permissions,
                                                ACCESS_MODE mode, DWORD inheritance)
{
    init_trustee(&access->Trustee, TRUSTEE_IS_NAME, name);
    access->grfInheritance = inheritance;
    access->grfAccessPermissions = permissions;
    access->grfAccessMode = mode;
}

ADVAPI void WINAPI BuildExplicitAccessWithNameA(PEXPLICIT_ACCESS_W access, LPSTR name, DWORD permissions,
                                                ACCESS_MODE mode, DWORD inheritance)
{
    BuildExplicitAccessWithNameW(access, (LPWSTR)name, permissions, mode, inheritance);
}

ADVAPI void WINAPI BuildImpersonateExplicitAccessWithNameW(PEXPLICIT_ACCESS_W access, LPWSTR name,
                                                           PTRUSTEE_W impersonate, DWORD permissions,
                                                           ACCESS_MODE mode, DWORD inheritance)
{
    access->Trustee.TrusteeType = TRUSTEE_IS_UNKNOWN;
    access->Trustee.TrusteeForm = TRUSTEE_IS_NAME;
    access->Trustee.ptstrName = name;
    access->Trustee.MultipleTrusteeOperation = TRUSTEE_IS_IMPERSONATE;
    access->Trustee.pMultipleTrustee = impersonate;
    access->grfAccessMode = mode;
    access->grfInheritance = inheritance;
    access->grfAccessPermissions = permissions;
}

ADVAPI void WINAPI BuildImpersonateExplicitAccessWithNameA(PEXPLICIT_ACCESS_W access, LPSTR name,
                                                           PTRUSTEE_W impersonate, DWORD permissions,
                                                           ACCESS_MODE mode, DWORD inheritance)
{
    BuildImpersonateExplicitAccessWithNameW(access, (LPWSTR)name, impersonate, permissions, mode, inheritance);
}

ADVAPI PTRUSTEE_W WINAPI GetMultipleTrusteeW(PTRUSTEE_W trustee)
{
    return trustee ? trustee->pMultipleTrustee : nullptr;
}

ADVAPI PTRUSTEE_W WINAPI GetMultipleTrusteeA(PTRUSTEE_W trustee)
{
    return GetMultipleTrusteeW(trustee);
}

ADVAPI MULTIPLE_TRUSTEE_OPERATION WINAPI GetMultipleTrusteeOperationW(PTRUSTEE_W trustee)
{
    return trustee ? trustee->MultipleTrusteeOperation : NO_MULTIPLE_TRUSTEE;
}

ADVAPI MULTIPLE_TRUSTEE_OPERATION WINAPI GetMultipleTrusteeOperationA(PTRUSTEE_W trustee)
{
    return GetMultipleTrusteeOperationW(trustee);
}

ADVAPI TRUSTEE_FORM WINAPI GetTrusteeFormW(PTRUSTEE_W trustee)
{
    return trustee ? trustee->TrusteeForm : TRUSTEE_BAD_FORM;
}

ADVAPI TRUSTEE_FORM WINAPI GetTrusteeFormA(PTRUSTEE_W trustee)
{
    return GetTrusteeFormW(trustee);
}

ADVAPI LPWSTR WINAPI GetTrusteeNameW(PTRUSTEE_W trustee)
{
    return trustee->ptstrName;
}

ADVAPI LPSTR WINAPI GetTrusteeNameA(PTRUSTEE_W trustee)
{
    return (LPSTR)trustee->ptstrName;
}

ADVAPI TRUSTEE_TYPE WINAPI GetTrusteeTypeW(PTRUSTEE_W trustee)
{
    return trustee ? trustee->TrusteeType : TRUSTEE_IS_UNKNOWN;
}

ADVAPI TRUSTEE_TYPE WINAPI GetTrusteeTypeA(PTRUSTEE_W trustee)
{
    return GetTrusteeTypeW(trustee);
}

/* ---------------------------------------------------------------- resolving trustees */

/* the sid of an account name of this machine, or of the user running the program for CURRENT_USER. */
static DWORD sid_of_name(PCWSTR name, BYTE *storage)
{
    if (!name)
        return ERROR_INVALID_PARAMETER;
    if (!_wcsicmp(name, L"CURRENT_USER"))
        return AdvCurrentUserSid(storage) ? ERROR_SUCCESS : ERROR_NO_TOKEN;
    WCHAR domain[256];
    DWORD size = SECURITY_MAX_SID_SIZE, domain_size = RTL_NUMBER_OF(domain);
    SID_NAME_USE use;
    if (LookupAccountNameW(nullptr, name, storage, &size, domain, &domain_size, &use))
        return ERROR_SUCCESS;
    DWORD error = GetLastError();
    return error == ERROR_INSUFFICIENT_BUFFER && domain_size > RTL_NUMBER_OF(domain) ? ERROR_NONE_MAPPED : error;
}

/*
 * the sid a trustee stands for, copied to storage of SECURITY_MAX_SID_SIZE bytes. the object forms name their
 * guids too, counted in guids for the sizes of the ace.
 */
static DWORD trustee_sid(const TRUSTEE_W *trustee, BYTE *storage, DWORD *guids)
{
    const SID *sid;
    DWORD present = 0;
    switch (trustee->TrusteeForm) {
    case TRUSTEE_IS_SID:
        sid = (const SID *)trustee->ptstrName;
        break;
    case TRUSTEE_IS_NAME:
        return sid_of_name(trustee->ptstrName, storage);
    case TRUSTEE_IS_OBJECTS_AND_SID: {
        const OBJECTS_AND_SID *objects = (const OBJECTS_AND_SID *)trustee->ptstrName;
        sid = objects ? objects->pSid : nullptr;
        present = objects ? objects->ObjectsPresent : 0;
        break;
    }
    case TRUSTEE_IS_OBJECTS_AND_NAME: {
        const OBJECTS_AND_NAME_W *objects = (const OBJECTS_AND_NAME_W *)trustee->ptstrName;
        if (!objects)
            return ERROR_INVALID_PARAMETER;
        DWORD error = sid_of_name(objects->ptstrName, storage);
        if (!error && guids)
            *guids = ((objects->ObjectsPresent & ACE_OBJECT_TYPE_PRESENT) ? 1 : 0) +
                     ((objects->ObjectsPresent & ACE_INHERITED_OBJECT_TYPE_PRESENT) ? 1 : 0);
        return error;
    }
    default:
        return ERROR_NONE_MAPPED;
    }
    if (!sid || !RtlValidSid((PSID)sid))
        return ERROR_INVALID_PARAMETER;
    memcpy(storage, sid, AdvSidLength(sid));
    if (guids)
        *guids = ((present & ACE_OBJECT_TYPE_PRESENT) ? 1 : 0) + ((present & ACE_INHERITED_OBJECT_TYPE_PRESENT) ? 1 : 0);
    return ERROR_SUCCESS;
}

/* ---------------------------------------------------------------- setting entries in a list */

typedef struct ENTRY_INFO {
    DWORD Mask;        /* the rights, the bits an existing ace of the trustee takes or loses */
    DWORD Size;        /* the bytes of the ace the entry adds, 0 for none */
    BYTE *Sid;         /* the trustee, or the client of a compound ace */
    BYTE *Server;      /* the server of a compound ace */
    const GUID *Object;
    const GUID *Inherited;
} ENTRY_INFO;

/* an ace of an old list that merges with an entry, or loses what it grants. */
static void compare_ace(const BYTE *ace, DWORD *mask, const EXPLICIT_ACCESS_W *entry, ENTRY_INFO *info, BOOL canonical)
{
    BYTE audit_flag = FAILED_ACCESS_ACE_FLAG;
    BYTE type = ace[0];
    const void *wanted;
    const void *found;
    if (entry->grfInheritance & INHERITED_ACE)
        return;
    if (entry->Trustee.MultipleTrusteeOperation == TRUSTEE_IS_IMPERSONATE) {
        if (type != ACCESS_ALLOWED_COMPOUND_ACE_TYPE)
            return;
        const void *server = info->Server ? info->Server : (const void *)entry->Trustee.ptstrName;
        if (!RtlEqualSid((PSID)server, (PSID)(ace + 12)))
            return;
        wanted = info->Sid ? info->Sid : (const void *)entry->Trustee.pMultipleTrustee->ptstrName;
        found = ace + 12 + RtlLengthSid((PSID)server);
        if (!RtlEqualSid((PSID)wanted, (PSID)found))
            return;
    } else if (type == 4) {
        return;
    } else if (type >= 5 && type <= 8) {
        const OBJECTS_AND_SID *objects;
        const GUID *object, *inherited;
        if (entry->Trustee.TrusteeForm == TRUSTEE_IS_OBJECTS_AND_SID) {
            objects = (const OBJECTS_AND_SID *)entry->Trustee.ptstrName;
            wanted = objects->pSid;
            object = &objects->ObjectTypeGuid;
            inherited = &objects->InheritedObjectTypeGuid;
        } else if (entry->Trustee.TrusteeForm == TRUSTEE_IS_OBJECTS_AND_NAME) {
            objects = (const OBJECTS_AND_SID *)entry->Trustee.ptstrName;
            wanted = info->Sid;
            object = info->Object;
            inherited = info->Inherited;
        } else {
            return;
        }
        DWORD flags = *(const DWORD *)(ace + 8);
        if (objects->ObjectsPresent != flags)
            return;
        const BYTE *at = ace + 12;
        const GUID *ace_object = nullptr, *ace_inherited = nullptr;
        if (flags & ACE_OBJECT_TYPE_PRESENT) {
            ace_object = (const GUID *)at;
            at += 16;
        }
        if (flags & ACE_INHERITED_OBJECT_TYPE_PRESENT) {
            ace_inherited = (const GUID *)at;
            at += 16;
        }
        if (!RtlEqualSid((PSID)wanted, (PSID)at))
            return;
        if (ace_object && !(object && !memcmp(ace_object, object, 16)))
            return;
        if (ace_inherited && !(inherited && !memcmp(ace_inherited, inherited, 16)))
            return;
    } else if (type <= 3) {
        if (entry->Trustee.TrusteeForm == TRUSTEE_IS_SID)
            wanted = entry->Trustee.ptstrName;
        else if (entry->Trustee.TrusteeForm == TRUSTEE_IS_NAME)
            wanted = info->Sid;
        else
            return;
        if (!RtlEqualSid((PSID)wanted, (PSID)(ace + 8)))
            return;
    }
    /* the trustee matches: what the entry does to the ace depends on its mode and the kind of ace. */
    BYTE inheritance = ace[1] & 0x1F;
    switch (entry->grfAccessMode) {
    case GRANT_ACCESS:
        if (entry->grfInheritance != inheritance)
            return;
        if (type == ACCESS_DENIED_ACE_TYPE || type == ACCESS_DENIED_OBJECT_ACE_TYPE) {
            *mask &= ~info->Mask;
            return;
        }
        if (type != ACCESS_ALLOWED_ACE_TYPE && type != 4 && type != 5)
            return;
        if (!canonical)
            return;
        break;
    case SET_ACCESS:
        if (type >= 2 && type != 4 && type != 5 && type != 6)
            return;
        *mask = 0;
        return;
    case DENY_ACCESS:
        if (entry->grfInheritance != inheritance || (type != ACCESS_DENIED_ACE_TYPE && type != ACCESS_DENIED_OBJECT_ACE_TYPE))
            return;
        if (!canonical)
            return;
        break;
    case REVOKE_ACCESS:
        if (type == 0 || type == 2 || type == 3 || type == 4 || type == 5 || type == 7 || type == 8)
            *mask = 0;
        return;
    case SET_AUDIT_SUCCESS:
    case SET_AUDIT_FAILURE:
        if (entry->grfAccessMode == SET_AUDIT_SUCCESS)
            audit_flag = SUCCESSFUL_ACCESS_ACE_FLAG;
        if (entry->grfInheritance != inheritance || !(type == 2 || type == 3 || type == 7 || type == 8) ||
            !(ace[1] & audit_flag))
            return;
        break;
    default:
        return;
    }
    *mask |= info->Mask;
    info->Size = 0;
}

/* the ace an entry adds, written at the cursor. */
static DWORD add_entry(BYTE **cursor, const EXPLICIT_ACCESS_W *entry, const ENTRY_INFO *info)
{
    BYTE *ace = *cursor;
    DWORD size;
    BYTE flags = (BYTE)entry->grfInheritance, type;
    if (entry->grfAccessMode == REVOKE_ACCESS || (entry->grfInheritance & INHERITED_ACE) || !info->Size)
        return ERROR_SUCCESS;
    if (entry->Trustee.MultipleTrusteeOperation == TRUSTEE_IS_IMPERSONATE) {
        const void *server = info->Server ? info->Server : (const void *)entry->Trustee.ptstrName;
        const void *client = info->Sid ? info->Sid : (const void *)entry->Trustee.pMultipleTrustee->ptstrName;
        DWORD server_size = RtlLengthSid((PSID)server), client_size = RtlLengthSid((PSID)client);
        size = 12 + server_size + client_size;
        if (size > 0xFFFF)
            return ERROR_BAD_INHERITANCE_ACL;
        *(WORD *)(ace + 8) = COMPOUND_ACE_IMPERSONATION;
        *(WORD *)(ace + 10) = 0;
        memcpy(ace + 12, server, server_size);
        memcpy(ace + 12 + server_size, client, client_size);
        type = ACCESS_ALLOWED_COMPOUND_ACE_TYPE;
    } else {
        BOOL objects = entry->Trustee.TrusteeForm == TRUSTEE_IS_OBJECTS_AND_SID ||
                       entry->Trustee.TrusteeForm == TRUSTEE_IS_OBJECTS_AND_NAME;
        const void *sid;
        const GUID *object = nullptr, *inherited = nullptr;
        DWORD present = 0;
        if (entry->Trustee.TrusteeForm == TRUSTEE_IS_SID) {
            sid = entry->Trustee.ptstrName;
        } else if (entry->Trustee.TrusteeForm == TRUSTEE_IS_NAME) {
            sid = info->Sid;
        } else if (entry->Trustee.TrusteeForm == TRUSTEE_IS_OBJECTS_AND_SID) {
            const OBJECTS_AND_SID *o = (const OBJECTS_AND_SID *)entry->Trustee.ptstrName;
            sid = o->pSid;
            present = o->ObjectsPresent;
            object = &o->ObjectTypeGuid;
            inherited = &o->InheritedObjectTypeGuid;
        } else {
            const OBJECTS_AND_NAME_W *o = (const OBJECTS_AND_NAME_W *)entry->Trustee.ptstrName;
            sid = info->Sid;
            present = o->ObjectsPresent;
            object = info->Object;
            inherited = info->Inherited;
        }
        switch (entry->grfAccessMode) {
        case GRANT_ACCESS:
        case SET_ACCESS:
            type = objects ? ACCESS_ALLOWED_OBJECT_ACE_TYPE : ACCESS_ALLOWED_ACE_TYPE;
            break;
        case DENY_ACCESS:
            type = objects ? ACCESS_DENIED_OBJECT_ACE_TYPE : ACCESS_DENIED_ACE_TYPE;
            break;
        case SET_AUDIT_SUCCESS:
            flags |= SUCCESSFUL_ACCESS_ACE_FLAG;
            type = objects ? SYSTEM_AUDIT_OBJECT_ACE_TYPE : SYSTEM_AUDIT_ACE_TYPE;
            break;
        case SET_AUDIT_FAILURE:
            flags |= FAILED_ACCESS_ACE_FLAG;
            type = objects ? SYSTEM_AUDIT_OBJECT_ACE_TYPE : SYSTEM_AUDIT_ACE_TYPE;
            break;
        default:
            return ERROR_SUCCESS;
        }
        DWORD sid_size = RtlLengthSid((PSID)sid);
        BYTE *at = ace + 8;
        if (objects) {
            *(DWORD *)at = present;
            at += 4;
            if (present & ACE_OBJECT_TYPE_PRESENT) {
                memcpy(at, object, 16);
                at += 16;
            }
            if (present & ACE_INHERITED_OBJECT_TYPE_PRESENT) {
                memcpy(at, inherited, 16);
                at += 16;
            }
        }
        memcpy(at, sid, sid_size);
        size = (DWORD)(at - ace) + sid_size;
        if (size > 0xFFFF)
            return ERROR_BAD_INHERITANCE_ACL;
    }
    ace[0] = type;
    ace[1] = flags;
    *(WORD *)(ace + 2) = (WORD)size;
    *(DWORD *)(ace + 4) = info->Mask;
    *cursor = ace + size;
    return ERROR_SUCCESS;
}

static BOOL is_deny_like(ACCESS_MODE mode)
{
    return mode == DENY_ACCESS || mode == SET_AUDIT_SUCCESS || mode == SET_AUDIT_FAILURE;
}

ADVAPI DWORD WINAPI SetEntriesInAclW(ULONG count, PEXPLICIT_ACCESS_W entries, PACL old, PACL *result)
{
    if (!result || (old && !RtlValidAcl(old)))
        return ERROR_INVALID_PARAMETER;
    *result = nullptr;
    if (!count) {
        if (!old)
            return ERROR_SUCCESS;
        PACL copy = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, old->AclSize);
        if (!copy)
            return ERROR_NOT_ENOUGH_MEMORY;
        memcpy(copy, old, old->AclSize);
        *result = copy;
        return ERROR_SUCCESS;
    }
    if (!entries)
        return ERROR_INVALID_PARAMETER;
    /* the old list: where its explicit leading aces end, and whether it is in canonical order. */
    DWORD old_count = old ? old->AceCount : 0, lead = 0;
    BOOL canonical = TRUE;
    UCHAR revision = ACL_REVISION;
    if (old) {
        revision = old->AclRevision;
        const BYTE *ace = (const BYTE *)(old + 1);
        for (DWORD i = 0; i < old_count; i++) {
            BYTE type = ace[0];
            if (!(ace[1] & INHERITED_ACE)) {
                if (type == 0 || type == 4 || type == 5) {
                    /* the first explicit allow: a deny after it makes the list non canonical. */
                    const BYTE *scan = ace;
                    for (DWORD j = i; j < old_count; j++, scan += *(const WORD *)(scan + 2))
                        if (!(scan[1] & INHERITED_ACE) && (scan[0] == 1 || scan[0] == 6))
                            canonical = FALSE;
                    break;
                }
                lead = i + 1;
            }
            ace += *(const WORD *)(ace + 2);
        }
    }
    DWORD *masks = old_count ? AdvAlloc(old_count * sizeof(DWORD)) : nullptr;
    ENTRY_INFO *info = AdvAllocZero(count * sizeof(ENTRY_INFO));
    BYTE *sids = AdvAlloc((SIZE_T)count * 2 * SECURITY_MAX_SID_SIZE);
    DWORD error = ERROR_SUCCESS;
    if ((old_count && !masks) || !info || !sids)
        error = ERROR_NOT_ENOUGH_MEMORY;
    if (!error && old_count) {
        const BYTE *ace = (const BYTE *)(old + 1);
        for (DWORD i = 0; i < old_count; i++, ace += *(const WORD *)(ace + 2))
            masks[i] = *(const DWORD *)(ace + 4);
    }
    /* what each entry adds: its mask, the sid it names and the size of its ace. */
    for (ULONG j = 0; j < count && !error; j++) {
        const EXPLICIT_ACCESS_W *entry = &entries[j];
        ENTRY_INFO *item = &info[j];
        item->Mask = entry->grfAccessPermissions;
        if (entry->grfInheritance & ~VALID_INHERIT_FLAGS) {
            error = ERROR_INVALID_PARAMETER;
            break;
        }
        if (entry->grfAccessMode < GRANT_ACCESS || entry->grfAccessMode > SET_AUDIT_FAILURE) {
            error = ERROR_INVALID_PARAMETER;
            break;
        }
        if (entry->grfInheritance & INHERITED_ACE) {
            item->Size = 0;
            continue;
        }
        DWORD guids = 0, size;
        if (entry->Trustee.MultipleTrusteeOperation == TRUSTEE_IS_IMPERSONATE) {
            if (!entry->Trustee.pMultipleTrustee) {
                error = ERROR_INVALID_PARAMETER;
                break;
            }
            item->Server = sids + (SIZE_T)j * 2 * SECURITY_MAX_SID_SIZE;
            item->Sid = item->Server + SECURITY_MAX_SID_SIZE;
            error = trustee_sid(&entry->Trustee, item->Server, nullptr);
            if (!error)
                error = trustee_sid(entry->Trustee.pMultipleTrustee, item->Sid, nullptr);
            if (error)
                break;
            size = 12 + RtlLengthSid((PSID)item->Server) + RtlLengthSid((PSID)item->Sid);
            if (revision < 3)
                revision = 3;
        } else {
            TRUSTEE_FORM form = entry->Trustee.TrusteeForm;
            if (form != TRUSTEE_IS_SID && form != TRUSTEE_IS_NAME && form != TRUSTEE_IS_OBJECTS_AND_SID &&
                form != TRUSTEE_IS_OBJECTS_AND_NAME) {
                error = ERROR_INVALID_PARAMETER;
                break;
            }
            item->Sid = sids + (SIZE_T)j * 2 * SECURITY_MAX_SID_SIZE;
            error = trustee_sid(&entry->Trustee, item->Sid, &guids);
            if (error)
                break;
            size = 8 + RtlLengthSid((PSID)item->Sid);
            if (form == TRUSTEE_IS_OBJECTS_AND_SID || form == TRUSTEE_IS_OBJECTS_AND_NAME) {
                revision = ACL_REVISION_DS;
                size += 4 + 16 * guids;
                if (form == TRUSTEE_IS_OBJECTS_AND_NAME && guids) {
                    /* the names of object types are directory classes, which a machine without one lacks. */
                    error = ERROR_INVALID_PARAMETER;
                    break;
                }
            }
        }
        item->Size = entry->grfAccessMode == REVOKE_ACCESS ? 0 : size;
    }
    /* the entries act on the explicit aces of the old list. */
    if (!error) {
        for (ULONG j = 0; j < count; j++) {
            const BYTE *ace = old ? (const BYTE *)(old + 1) : nullptr;
            for (DWORD k = 0; k < old_count; k++, ace += *(const WORD *)(ace + 2))
                if (!(ace[1] & INHERITED_ACE))
                    compare_ace(ace, &masks[k], &entries[j], &info[j], canonical);
        }
    }
    DWORD kept = 0, total = old ? old->AclSize : sizeof(ACL);
    ACL *acl = nullptr;
    if (!error) {
        const BYTE *ace = old ? (const BYTE *)(old + 1) : nullptr;
        for (DWORD k = 0; k < old_count; k++, ace += *(const WORD *)(ace + 2)) {
            if (masks[k])
                kept++;
            else
                total -= *(const WORD *)(ace + 2);
        }
        for (ULONG j = 0; j < count; j++) {
            if (info[j].Size) {
                total += info[j].Size;
                kept++;
            }
        }
        if (total > 0xFFFC)
            error = ERROR_INVALID_PARAMETER;
        else if (!(acl = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, total)))
            error = ERROR_NOT_ENOUGH_MEMORY;
    }
    if (!error) {
        RtlCreateAcl(acl, total, revision);
        acl->AceCount = (WORD)kept;
        BYTE *cursor = (BYTE *)(acl + 1);
        /* the new denies and audits come first, then the old explicit aces around the new grants. */
        for (ULONG j = 0; j < count && !error; j++)
            if (is_deny_like(entries[j].grfAccessMode))
                error = add_entry(&cursor, &entries[j], &info[j]);
        const BYTE *ace = old ? (const BYTE *)(old + 1) : nullptr;
        DWORD index = 0;
        for (; index < lead && !error; index++, ace += *(const WORD *)(ace + 2)) {
            if (!(ace[1] & INHERITED_ACE) && masks[index]) {
                memcpy(cursor, ace, *(const WORD *)(ace + 2));
                *(DWORD *)(cursor + 4) = masks[index];
                cursor += *(const WORD *)(ace + 2);
            }
        }
        if (canonical)
            for (ULONG j = 0; j < count && !error; j++)
                if (entries[j].grfAccessMode == GRANT_ACCESS || entries[j].grfAccessMode == SET_ACCESS)
                    error = add_entry(&cursor, &entries[j], &info[j]);
        for (; index < old_count && !error; index++, ace += *(const WORD *)(ace + 2)) {
            if (!(ace[1] & INHERITED_ACE) && masks[index]) {
                memcpy(cursor, ace, *(const WORD *)(ace + 2));
                *(DWORD *)(cursor + 4) = masks[index];
                cursor += *(const WORD *)(ace + 2);
            }
        }
        if (!canonical)
            for (ULONG j = 0; j < count && !error; j++)
                if (entries[j].grfAccessMode == GRANT_ACCESS || entries[j].grfAccessMode == SET_ACCESS)
                    error = add_entry(&cursor, &entries[j], &info[j]);
        /* the inherited aces come last, as they were. */
        ace = old ? (const BYTE *)(old + 1) : nullptr;
        for (DWORD k = 0; k < old_count && !error; k++, ace += *(const WORD *)(ace + 2)) {
            if ((ace[1] & INHERITED_ACE) && masks[k]) {
                memcpy(cursor, ace, *(const WORD *)(ace + 2));
                cursor += *(const WORD *)(ace + 2);
            }
        }
        if (!error)
            *result = acl;
    }
    if (error && acl)
        LocalFree(acl);
    AdvFree(masks);
    AdvFree(info);
    AdvFree(sids);
    return error;
}

/* ---------------------------------------------------------------- the ansi forms */

/* the strings made wide for the call of a wide function, freed together. */
typedef struct WIDE_POOL {
    PVOID *Items;
    ULONG Count;
    ULONG Capacity;
} WIDE_POOL;

static PVOID pool_alloc(WIDE_POOL *pool, SIZE_T size)
{
    if (pool->Count == pool->Capacity) {
        ULONG capacity = pool->Capacity ? pool->Capacity * 2 : 16;
        PVOID *items = AdvAlloc(capacity * sizeof(PVOID));
        if (!items)
            return nullptr;
        if (pool->Items)
            memcpy(items, pool->Items, pool->Count * sizeof(PVOID));
        AdvFree(pool->Items);
        pool->Items = items;
        pool->Capacity = capacity;
    }
    PVOID p = AdvAllocZero(size);
    if (p)
        pool->Items[pool->Count++] = p;
    return p;
}

static void pool_free(WIDE_POOL *pool)
{
    for (ULONG i = 0; i < pool->Count; i++)
        AdvFree(pool->Items[i]);
    AdvFree(pool->Items);
}

static PWSTR pool_wide(WIDE_POOL *pool, LPCSTR text)
{
    if (!text)
        return nullptr;
    PWSTR w = AdvAnsiToWide(text);
    if (!w)
        return nullptr;
    PWSTR copy = pool_alloc(pool, (wcslen(w) + 1) * sizeof(WCHAR));
    if (copy)
        wcscpy(copy, w);
    AdvFree(w);
    return copy;
}

/* an ansi trustee as a wide one, sharing what holds no text. */
static DWORD trustee_wide(WIDE_POOL *pool, const TRUSTEE_A *src, TRUSTEE_W *dst)
{
    *dst = *(const TRUSTEE_W *)src;
    if (src->TrusteeForm == TRUSTEE_IS_NAME && src->ptstrName) {
        if (!(dst->ptstrName = pool_wide(pool, src->ptstrName)))
            return ERROR_NOT_ENOUGH_MEMORY;
    } else if (src->TrusteeForm == TRUSTEE_IS_OBJECTS_AND_NAME && src->ptstrName) {
        const OBJECTS_AND_NAME_A *a = (const OBJECTS_AND_NAME_A *)src->ptstrName;
        OBJECTS_AND_NAME_W *w = pool_alloc(pool, sizeof(*w));
        if (!w)
            return ERROR_NOT_ENOUGH_MEMORY;
        w->ObjectsPresent = a->ObjectsPresent;
        w->ObjectType = a->ObjectType;
        w->ObjectTypeName = pool_wide(pool, a->ObjectTypeName);
        w->InheritedObjectTypeName = pool_wide(pool, a->InheritedObjectTypeName);
        w->ptstrName = pool_wide(pool, a->ptstrName);
        if ((a->ObjectTypeName && !w->ObjectTypeName) || (a->InheritedObjectTypeName && !w->InheritedObjectTypeName) ||
            (a->ptstrName && !w->ptstrName))
            return ERROR_NOT_ENOUGH_MEMORY;
        dst->ptstrName = (LPWSTR)w;
    }
    if (src->MultipleTrusteeOperation == TRUSTEE_IS_IMPERSONATE && src->pMultipleTrustee) {
        TRUSTEE_W *inner = pool_alloc(pool, sizeof(*inner));
        if (!inner)
            return ERROR_NOT_ENOUGH_MEMORY;
        DWORD error = trustee_wide(pool, src->pMultipleTrustee, inner);
        if (error)
            return error;
        dst->pMultipleTrustee = inner;
    }
    return ERROR_SUCCESS;
}

static DWORD entries_wide(WIDE_POOL *pool, ULONG count, const EXPLICIT_ACCESS_A *src, EXPLICIT_ACCESS_W **out)
{
    EXPLICIT_ACCESS_W *wide = count ? pool_alloc(pool, count * sizeof(*wide)) : nullptr;
    if (count && !wide)
        return ERROR_NOT_ENOUGH_MEMORY;
    for (ULONG i = 0; i < count; i++) {
        wide[i].grfAccessPermissions = src[i].grfAccessPermissions;
        wide[i].grfAccessMode = src[i].grfAccessMode;
        wide[i].grfInheritance = src[i].grfInheritance;
        DWORD error = trustee_wide(pool, &src[i].Trustee, &wide[i].Trustee);
        if (error)
            return error;
    }
    *out = wide;
    return ERROR_SUCCESS;
}

ADVAPI DWORD WINAPI SetEntriesInAclA(ULONG count, PEXPLICIT_ACCESS_A entries, PACL old, PACL *result)
{
    if (!result || (count && !entries))
        return ERROR_INVALID_PARAMETER;
    WIDE_POOL pool = {};
    EXPLICIT_ACCESS_W *wide = nullptr;
    DWORD error = entries_wide(&pool, count, entries, &wide);
    if (!error)
        error = SetEntriesInAclW(count, wide, old, result);
    pool_free(&pool);
    return error;
}

/* ---------------------------------------------------------------- reading entries from a list */

static DWORD align8(DWORD n)
{
    return (n + 7) & ~7u;
}

/* the entries of the explicit aces of a list, in one block that holds their sids too. */
ADVAPI DWORD WINAPI GetExplicitEntriesFromAclW(PACL acl, PULONG count, PEXPLICIT_ACCESS_W *entries)
{
    if (!count || !entries)
        return ERROR_INVALID_PARAMETER;
    *count = 0;
    *entries = nullptr;
    if (!acl || !acl->AceCount)
        return ERROR_SUCCESS;
    if (!RtlValidAcl(acl))
        return ERROR_INVALID_PARAMETER;
    ULONG total = 0, n = 0;
    const BYTE *ace = (const BYTE *)(acl + 1);
    for (DWORD i = 0; i < acl->AceCount; i++, ace += *(const WORD *)(ace + 2)) {
        if (ace[1] & INHERITED_ACE)
            continue;
        BYTE type = ace[0];
        DWORD trailing;
        if (type <= 3) {
            trailing = align8(RtlLengthSid((PSID)(ace + 8)));
        } else if (type == ACCESS_ALLOWED_COMPOUND_ACE_TYPE) {
            DWORD server = RtlLengthSid((PSID)(ace + 12));
            trailing = align8(server) + align8(RtlLengthSid((PSID)(ace + 12 + server))) + sizeof(TRUSTEE_W);
        } else if (type >= 5 && type <= 8) {
            DWORD f = *(const DWORD *)(ace + 8);
            trailing = align8(RtlLengthSid((PSID)(ace + 12 + 16 * (f & 1) + 8 * (f & 2)))) + sizeof(OBJECTS_AND_SID);
        } else {
            return ERROR_INVALID_PARAMETER;
        }
        DWORD repeat = 1;
        if (type == 2 || type == 3 || type == 7 || type == 8) {
            repeat = ((ace[1] & SUCCESSFUL_ACCESS_ACE_FLAG) != 0) + ((ace[1] & FAILED_ACCESS_ACE_FLAG) != 0);
            if (!repeat)
                return ERROR_INVALID_PARAMETER;
        }
        n += repeat;
        total += repeat * (trailing + sizeof(EXPLICIT_ACCESS_W));
    }
    if (!total)
        return ERROR_SUCCESS;
    BYTE *block = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, total);
    if (!block)
        return ERROR_NOT_ENOUGH_MEMORY;
    EXPLICIT_ACCESS_W *out = (EXPLICIT_ACCESS_W *)block;
    BYTE *extra = block + n * sizeof(EXPLICIT_ACCESS_W);
    ULONG index = 0;
    ace = (const BYTE *)(acl + 1);
    for (DWORD i = 0; i < acl->AceCount; i++, ace += *(const WORD *)(ace + 2)) {
        if (ace[1] & INHERITED_ACE)
            continue;
        BYTE type = ace[0], flags = ace[1];
        DWORD mask = *(const DWORD *)(ace + 4);
        BOOL audit = type == 2 || type == 3 || type == 7 || type == 8;
        for (int pass = 0; pass < (audit ? 2 : 1); pass++) {
            ACCESS_MODE mode;
            if (audit) {
                /* success first, then failure, for an ace that audits both. */
                BOOL success = (flags & SUCCESSFUL_ACCESS_ACE_FLAG) != 0, failure = (flags & FAILED_ACCESS_ACE_FLAG) != 0;
                if (pass == 0 && success)
                    mode = SET_AUDIT_SUCCESS;
                else if (pass == 0 && failure)
                    mode = SET_AUDIT_FAILURE;
                else if (pass == 1 && success && failure)
                    mode = SET_AUDIT_FAILURE;
                else
                    break;
            } else {
                mode = type == 1 || type == 6 ? DENY_ACCESS : GRANT_ACCESS;
            }
            EXPLICIT_ACCESS_W *entry = &out[index++];
            entry->grfAccessPermissions = mask;
            entry->grfAccessMode = mode;
            entry->grfInheritance = flags & VALID_INHERIT_FLAGS;
            if (type <= 3) {
                DWORD size = RtlLengthSid((PSID)(ace + 8));
                memcpy(extra, ace + 8, size);
                BuildTrusteeWithSidW(&entry->Trustee, extra);
                extra += align8(size);
            } else if (type == ACCESS_ALLOWED_COMPOUND_ACE_TYPE) {
                DWORD server = RtlLengthSid((PSID)(ace + 12)), client = RtlLengthSid((PSID)(ace + 12 + server));
                memcpy(extra, ace + 12, server);
                BuildTrusteeWithSidW(&entry->Trustee, extra);
                extra += align8(server);
                memcpy(extra, ace + 12 + server, client);
                PSID client_sid = extra;
                extra += align8(client);
                TRUSTEE_W *inner = (TRUSTEE_W *)extra;
                BuildTrusteeWithSidW(inner, client_sid);
                BuildImpersonateTrusteeW(&entry->Trustee, inner);
                extra += sizeof(TRUSTEE_W);
            } else {
                DWORD f = *(const DWORD *)(ace + 8);
                const BYTE *at = ace + 12;
                const GUID *object = nullptr, *inherited = nullptr;
                if (f & ACE_OBJECT_TYPE_PRESENT) {
                    object = (const GUID *)at;
                    at += 16;
                }
                if (f & ACE_INHERITED_OBJECT_TYPE_PRESENT) {
                    inherited = (const GUID *)at;
                    at += 16;
                }
                DWORD size = RtlLengthSid((PSID)at);
                memcpy(extra, at, size);
                PSID sid = extra;
                extra += align8(size);
                OBJECTS_AND_SID *objects = (OBJECTS_AND_SID *)extra;
                extra += sizeof(OBJECTS_AND_SID);
                BuildTrusteeWithObjectsAndSidW(&entry->Trustee, objects, (GUID *)object, (GUID *)inherited, sid);
            }
        }
    }
    *count = index;
    *entries = (PEXPLICIT_ACCESS_W)block;
    return ERROR_SUCCESS;
}

ADVAPI DWORD WINAPI GetExplicitEntriesFromAclA(PACL acl, PULONG count, PEXPLICIT_ACCESS_A *entries)
{
    return GetExplicitEntriesFromAclW(acl, count, (PEXPLICIT_ACCESS_W *)entries);
}

/* ---------------------------------------------------------------- descriptors from entries */

/* the old descriptor's parts, which must be self relative. */
typedef struct OLD_PARTS {
    PSID Owner;
    PSID Group;
    PACL Dacl;
    PACL Sacl;
} OLD_PARTS;

static DWORD old_parts(PSECURITY_DESCRIPTOR sd, OLD_PARTS *parts)
{
    const SECURITY_DESCRIPTOR_RELATIVE *r = sd;
    memset(parts, 0, sizeof(*parts));
    if (!(r->Control & SE_SELF_RELATIVE))
        return ERROR_INVALID_SECURITY_DESCR;
    const BYTE *base = sd;
    if (r->Owner)
        parts->Owner = (PSID)(base + r->Owner);
    if (r->Group)
        parts->Group = (PSID)(base + r->Group);
    if (r->Dacl && (r->Control & SE_DACL_PRESENT))
        parts->Dacl = (PACL)(base + r->Dacl);
    if (r->Sacl && (r->Control & SE_SACL_PRESENT))
        parts->Sacl = (PACL)(base + r->Sacl);
    return ERROR_SUCCESS;
}

ADVAPI DWORD WINAPI BuildSecurityDescriptorW(PTRUSTEE_W owner, PTRUSTEE_W group, ULONG access_count,
                                             PEXPLICIT_ACCESS_W access, ULONG audit_count, PEXPLICIT_ACCESS_W audit,
                                             PSECURITY_DESCRIPTOR old, PULONG size, PSECURITY_DESCRIPTOR *result)
{
    OLD_PARTS parts = {};
    BYTE owner_storage[SECURITY_MAX_SID_SIZE], group_storage[SECURITY_MAX_SID_SIZE];
    PACL dacl = nullptr, sacl = nullptr;
    BOOL free_dacl = FALSE, free_sacl = FALSE;
    SECURITY_DESCRIPTOR sd;
    DWORD error = ERROR_SUCCESS;
    if (old && (error = old_parts(old, &parts)))
        return error;
    if (owner) {
        if ((error = trustee_sid(owner, owner_storage, nullptr)))
            return error;
        parts.Owner = owner_storage;
    }
    if (group) {
        if ((error = trustee_sid(group, group_storage, nullptr)))
            return error;
        parts.Group = group_storage;
    }
    if (access_count) {
        if ((error = SetEntriesInAclW(access_count, access, parts.Dacl, &dacl)))
            return error;
        free_dacl = TRUE;
    } else {
        dacl = parts.Dacl;
    }
    if (audit_count) {
        if ((error = SetEntriesInAclW(audit_count, audit, parts.Sacl, &sacl))) {
            if (free_dacl)
                LocalFree(dacl);
            return error;
        }
        free_sacl = TRUE;
    } else {
        sacl = parts.Sacl;
    }
    InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    if (SetSecurityDescriptorOwner(&sd, parts.Owner, FALSE) && SetSecurityDescriptorGroup(&sd, parts.Group, FALSE) &&
        SetSecurityDescriptorDacl(&sd, TRUE, dacl, FALSE) && SetSecurityDescriptorSacl(&sd, TRUE, sacl, FALSE)) {
        DWORD length = 0;
        if (MakeSelfRelativeSD(&sd, nullptr, &length)) {
            error = ERROR_INVALID_PARAMETER;
        } else if ((error = GetLastError()) == ERROR_INSUFFICIENT_BUFFER) {
            PSECURITY_DESCRIPTOR out = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, length);
            if (!out) {
                error = ERROR_NOT_ENOUGH_MEMORY;
            } else if (MakeSelfRelativeSD(&sd, out, &length)) {
                *result = out;
                *size = length;
                error = ERROR_SUCCESS;
            } else {
                error = GetLastError();
                LocalFree(out);
            }
        }
    } else {
        error = GetLastError();
    }
    if (free_dacl)
        LocalFree(dacl);
    if (free_sacl)
        LocalFree(sacl);
    return error;
}

ADVAPI DWORD WINAPI BuildSecurityDescriptorA(PTRUSTEE_A owner, PTRUSTEE_A group, ULONG access_count,
                                             PEXPLICIT_ACCESS_A access, ULONG audit_count, PEXPLICIT_ACCESS_A audit,
                                             PSECURITY_DESCRIPTOR old, PULONG size, PSECURITY_DESCRIPTOR *result)
{
    WIDE_POOL pool = {};
    TRUSTEE_W owner_w, group_w;
    EXPLICIT_ACCESS_W *access_w = nullptr, *audit_w = nullptr;
    DWORD error = ERROR_SUCCESS;
    if (owner)
        error = trustee_wide(&pool, owner, &owner_w);
    if (!error && group)
        error = trustee_wide(&pool, group, &group_w);
    if (!error)
        error = entries_wide(&pool, access_count, access, &access_w);
    if (!error)
        error = entries_wide(&pool, audit_count, audit, &audit_w);
    if (!error)
        error = BuildSecurityDescriptorW(owner ? &owner_w : nullptr, group ? &group_w : nullptr, access_count, access_w,
                                         audit_count, audit_w, old, size, result);
    pool_free(&pool);
    return error;
}
