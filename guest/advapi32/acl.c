/*
 * access control lists, aces and security descriptors: the win32 face of the rtl routines of ntdll.
 *
 * each function converts the status of its routine to the last error the way kernelbase does. the functions that
 * take ace flags answer ERROR_INVALID_FLAGS when the flags are what the routine rejects.
 */
#include "advapi32_private.h"

static BOOL result(NTSTATUS status)
{
    return NT_SUCCESS(status) ? TRUE : AdvSetStatus(status);
}

/* the routines that check ace flags report an invalid parameter as invalid flags. */
static BOOL result_flags(NTSTATUS status)
{
    if (NT_SUCCESS(status))
        return TRUE;
    if (status == STATUS_INVALID_PARAMETER) {
        AdvSetError(ERROR_INVALID_FLAGS);
        return FALSE;
    }
    return AdvSetStatus(status);
}

/* ---------------------------------------------------------------- access control lists */

ADVAPI BOOL WINAPI InitializeAcl(PACL acl, DWORD length, DWORD revision)
{
    return result(RtlCreateAcl(acl, length, revision));
}

ADVAPI BOOL WINAPI IsValidAcl(PACL acl)
{
    if (RtlValidAcl(acl))
        return TRUE;
    AdvSetError(ERROR_INVALID_ACL);
    return FALSE;
}

ADVAPI BOOL WINAPI GetAclInformation(PACL acl, LPVOID info, DWORD length, ACL_INFORMATION_CLASS klass)
{
    return result(RtlQueryInformationAcl(acl, info, length, klass));
}

ADVAPI BOOL WINAPI SetAclInformation(PACL acl, LPVOID info, DWORD length, ACL_INFORMATION_CLASS klass)
{
    return result(RtlSetInformationAcl(acl, info, length, klass));
}

ADVAPI BOOL WINAPI GetAce(PACL acl, DWORD index, LPVOID *ace)
{
    return result(RtlGetAce(acl, index, ace));
}

ADVAPI BOOL WINAPI AddAce(PACL acl, DWORD revision, DWORD index, LPVOID list, DWORD length)
{
    return result(RtlAddAce(acl, revision, index, list, length));
}

ADVAPI BOOL WINAPI DeleteAce(PACL acl, DWORD index)
{
    return result(RtlDeleteAce(acl, index));
}

ADVAPI BOOL WINAPI FindFirstFreeAce(PACL acl, LPVOID *ace)
{
    if (RtlFirstFreeAce(acl, ace))
        return TRUE;
    AdvSetError(ERROR_INVALID_ACL);
    return FALSE;
}

ADVAPI BOOL WINAPI AddAccessAllowedAce(PACL acl, DWORD revision, DWORD mask, PSID sid)
{
    return result(RtlAddAccessAllowedAce(acl, revision, mask, sid));
}

ADVAPI BOOL WINAPI AddAccessAllowedAceEx(PACL acl, DWORD revision, DWORD flags, DWORD mask, PSID sid)
{
    return result_flags(RtlAddAccessAllowedAceEx(acl, revision, flags, mask, sid));
}

ADVAPI BOOL WINAPI AddAccessDeniedAce(PACL acl, DWORD revision, DWORD mask, PSID sid)
{
    return result(RtlAddAccessDeniedAce(acl, revision, mask, sid));
}

ADVAPI BOOL WINAPI AddAccessDeniedAceEx(PACL acl, DWORD revision, DWORD flags, DWORD mask, PSID sid)
{
    return result_flags(RtlAddAccessDeniedAceEx(acl, revision, flags, mask, sid));
}

ADVAPI BOOL WINAPI AddAuditAccessAce(PACL acl, DWORD revision, DWORD mask, PSID sid, BOOL success, BOOL failure)
{
    return result(RtlAddAuditAccessAce(acl, revision, mask, sid, success != 0, failure != 0));
}

ADVAPI BOOL WINAPI AddAuditAccessAceEx(PACL acl, DWORD revision, DWORD flags, DWORD mask, PSID sid, BOOL success,
                                       BOOL failure)
{
    return result_flags(RtlAddAuditAccessAceEx(acl, revision, flags, mask, sid, success != 0, failure != 0));
}

ADVAPI BOOL WINAPI AddAccessAllowedObjectAce(PACL acl, DWORD revision, DWORD flags, DWORD mask, GUID *object,
                                             GUID *inherited, PSID sid)
{
    return result_flags(RtlAddAccessAllowedObjectAce(acl, revision, flags, mask, object, inherited, sid));
}

ADVAPI BOOL WINAPI AddAccessDeniedObjectAce(PACL acl, DWORD revision, DWORD flags, DWORD mask, GUID *object,
                                            GUID *inherited, PSID sid)
{
    return result_flags(RtlAddAccessDeniedObjectAce(acl, revision, flags, mask, object, inherited, sid));
}

ADVAPI BOOL WINAPI AddAuditAccessObjectAce(PACL acl, DWORD revision, DWORD flags, DWORD mask, GUID *object,
                                           GUID *inherited, PSID sid, BOOL success, BOOL failure)
{
    return result_flags(
        RtlAddAuditAccessObjectAce(acl, revision, flags, mask, object, inherited, sid, success != 0, failure != 0));
}

ADVAPI BOOL WINAPI AddMandatoryAce(PACL acl, DWORD revision, DWORD flags, DWORD policy, PSID label)
{
    return result(RtlAddMandatoryAce(acl, revision, flags, label, SYSTEM_MANDATORY_LABEL_ACE_TYPE, policy));
}

/*
 * a conditional ace: the condition text is compiled to the binary form, the ace is built after the sid with the
 * condition padded to four bytes, and added at the end. the length that comes back is the bytes the list uses, or
 * the bytes it would need when the ace does not fit.
 */
ADVAPI BOOL WINAPI AddConditionalAce(PACL acl, DWORD revision, DWORD flags, UCHAR type, DWORD mask, PSID sid,
                                     PWCHAR condition, DWORD *used)
{
    PVOID binary = nullptr;
    DWORD binary_size = 0, error = 0;
    BYTE *ace = nullptr;
    BOOL ok = FALSE;
    *used = 0;
    if (!acl || !condition || ((type - 9) & 0xFA) != 0 || type == SYSTEM_ALARM_CALLBACK_ACE_TYPE)
        error = ERROR_INVALID_PARAMETER;
    else if (!RtlValidSid(sid))
        error = ERROR_INVALID_SID;
    else if (acl->AclRevision > ACL_REVISION_DS || revision > ACL_REVISION_DS)
        error = ERROR_REVISION_MISMATCH;
    else if (flags & (type == SYSTEM_AUDIT_CALLBACK_ACE_TYPE ? ~0xDFu : ~0x1Fu))
        error = ERROR_INVALID_PARAMETER;
    else
        error = AdvConditionFromString(condition, &binary, &binary_size);
    if (!error) {
        DWORD padded = (binary_size + 3) & ~3u, sid_size = RtlLengthSid(sid);
        DWORD size = 8 + sid_size + padded;
        if (binary_size > 0xFFFFFFFC || padded + 12 < padded || size < padded + 12) {
            error = ERROR_ARITHMETIC_OVERFLOW;
        } else if (!(ace = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, size))) {
            error = ERROR_NOT_ENOUGH_MEMORY;
        } else {
            ace[0] = type;
            ace[1] = (BYTE)flags;
            *(WORD *)(ace + 2) = (WORD)size;
            *(DWORD *)(ace + 4) = mask;
            memcpy(ace + 8, sid, sid_size);
            memcpy(ace + 8 + sid_size, binary, binary_size);
            NTSTATUS status = RtlAddAce(acl, revision, 0xFFFFFFFF, ace, size);
            /* the bytes in use now, from the aces actually in the list. */
            DWORD in_use = sizeof(ACL);
            const BYTE *at = (const BYTE *)(acl + 1);
            for (DWORD i = 0; i < acl->AceCount; i++) {
                DWORD n = *(const WORD *)(at + 2);
                in_use += n;
                at += n;
            }
            *used = in_use;
            if (NT_SUCCESS(status)) {
                ok = TRUE;
            } else {
                *used = (in_use + size + 3) & ~3u;
                error = RtlNtStatusToDosError(status);
            }
        }
    }
    if (error)
        AdvSetError(error);
    if (binary)
        LocalFree(binary);
    if (ace)
        LocalFree(ace);
    return ok;
}

ADVAPI void WINAPI MapGenericMask(PDWORD mask, PGENERIC_MAPPING mapping)
{
    RtlMapGenericMask(mask, mapping);
}

/* ---------------------------------------------------------------- security descriptors */

ADVAPI BOOL WINAPI InitializeSecurityDescriptor(PSECURITY_DESCRIPTOR sd, DWORD revision)
{
    return result(RtlCreateSecurityDescriptor(sd, revision));
}

ADVAPI BOOL WINAPI IsValidSecurityDescriptor(PSECURITY_DESCRIPTOR sd)
{
    if (RtlValidSecurityDescriptor(sd))
        return TRUE;
    AdvSetError(ERROR_INVALID_SECURITY_DESCR);
    return FALSE;
}

ADVAPI BOOL WINAPI IsValidRelativeSecurityDescriptor(PSECURITY_DESCRIPTOR sd, ULONG length, SECURITY_INFORMATION required)
{
    if (RtlValidRelativeSecurityDescriptor(sd, length, required))
        return TRUE;
    AdvSetError(ERROR_INVALID_SECURITY_DESCR);
    return FALSE;
}

ADVAPI DWORD WINAPI GetSecurityDescriptorLength(PSECURITY_DESCRIPTOR sd)
{
    return RtlLengthSecurityDescriptor(sd);
}

ADVAPI BOOL WINAPI GetSecurityDescriptorControl(PSECURITY_DESCRIPTOR sd, PSECURITY_DESCRIPTOR_CONTROL control,
                                                LPDWORD revision)
{
    return result(RtlGetControlSecurityDescriptor(sd, control, revision));
}

ADVAPI BOOL WINAPI SetSecurityDescriptorControl(PSECURITY_DESCRIPTOR sd, SECURITY_DESCRIPTOR_CONTROL mask,
                                                SECURITY_DESCRIPTOR_CONTROL bits)
{
    return result(RtlSetControlSecurityDescriptor(sd, mask, bits));
}

ADVAPI BOOL WINAPI GetSecurityDescriptorOwner(PSECURITY_DESCRIPTOR sd, PSID *owner, LPBOOL defaulted)
{
    BOOLEAN d = FALSE;
    NTSTATUS status = RtlGetOwnerSecurityDescriptor(sd, owner, &d);
    if (!NT_SUCCESS(status))
        return AdvSetStatus(status);
    *defaulted = d;
    return TRUE;
}

ADVAPI BOOL WINAPI GetSecurityDescriptorGroup(PSECURITY_DESCRIPTOR sd, PSID *group, LPBOOL defaulted)
{
    BOOLEAN d = FALSE;
    NTSTATUS status = RtlGetGroupSecurityDescriptor(sd, group, &d);
    if (!NT_SUCCESS(status))
        return AdvSetStatus(status);
    *defaulted = d;
    return TRUE;
}

ADVAPI BOOL WINAPI GetSecurityDescriptorDacl(PSECURITY_DESCRIPTOR sd, LPBOOL present, PACL *dacl, LPBOOL defaulted)
{
    BOOLEAN p = FALSE, d = FALSE;
    NTSTATUS status = RtlGetDaclSecurityDescriptor(sd, &p, dacl, &d);
    if (!NT_SUCCESS(status))
        return AdvSetStatus(status);
    *present = p;
    *defaulted = d;
    return TRUE;
}

ADVAPI BOOL WINAPI GetSecurityDescriptorSacl(PSECURITY_DESCRIPTOR sd, LPBOOL present, PACL *sacl, LPBOOL defaulted)
{
    BOOLEAN p = FALSE, d = FALSE;
    NTSTATUS status = RtlGetSaclSecurityDescriptor(sd, &p, sacl, &d);
    if (!NT_SUCCESS(status))
        return AdvSetStatus(status);
    *present = p;
    *defaulted = d;
    return TRUE;
}

ADVAPI BOOL WINAPI SetSecurityDescriptorOwner(PSECURITY_DESCRIPTOR sd, PSID owner, BOOL defaulted)
{
    return result(RtlSetOwnerSecurityDescriptor(sd, owner, defaulted != 0));
}

ADVAPI BOOL WINAPI SetSecurityDescriptorGroup(PSECURITY_DESCRIPTOR sd, PSID group, BOOL defaulted)
{
    return result(RtlSetGroupSecurityDescriptor(sd, group, defaulted != 0));
}

ADVAPI BOOL WINAPI SetSecurityDescriptorDacl(PSECURITY_DESCRIPTOR sd, BOOL present, PACL dacl, BOOL defaulted)
{
    return result(RtlSetDaclSecurityDescriptor(sd, present != 0, dacl, defaulted != 0));
}

ADVAPI BOOL WINAPI SetSecurityDescriptorSacl(PSECURITY_DESCRIPTOR sd, BOOL present, PACL sacl, BOOL defaulted)
{
    return result(RtlSetSaclSecurityDescriptor(sd, present != 0, sacl, defaulted != 0));
}

ADVAPI DWORD WINAPI GetSecurityDescriptorRMControl(PSECURITY_DESCRIPTOR sd, PUCHAR control)
{
    return RtlGetSecurityDescriptorRMControl(sd, control) ? ERROR_SUCCESS : ERROR_INVALID_DATA;
}

ADVAPI DWORD WINAPI SetSecurityDescriptorRMControl(PSECURITY_DESCRIPTOR sd, PUCHAR control)
{
    RtlSetSecurityDescriptorRMControl(sd, control);
    return ERROR_SUCCESS;
}

ADVAPI BOOL WINAPI MakeSelfRelativeSD(PSECURITY_DESCRIPTOR absolute, PSECURITY_DESCRIPTOR relative, LPDWORD length)
{
    if (!absolute)
        return AdvSetStatus(STATUS_INVALID_PARAMETER);
    return result(RtlAbsoluteToSelfRelativeSD(absolute, relative, length));
}

ADVAPI BOOL WINAPI MakeAbsoluteSD(PSECURITY_DESCRIPTOR relative, PSECURITY_DESCRIPTOR absolute, LPDWORD absolute_size,
                                  PACL dacl, LPDWORD dacl_size, PACL sacl, LPDWORD sacl_size, PSID owner,
                                  LPDWORD owner_size, PSID group, LPDWORD group_size)
{
    if (!relative)
        return AdvSetStatus(STATUS_INVALID_PARAMETER);
    return result(RtlSelfRelativeToAbsoluteSD(relative, absolute, absolute_size, dacl, dacl_size, sacl, sacl_size,
                                              owner, owner_size, group, group_size));
}

ADVAPI BOOL WINAPI MakeAbsoluteSD2(PSECURITY_DESCRIPTOR relative, LPDWORD size)
{
    return result(RtlSelfRelativeToAbsoluteSD2(relative, size));
}
