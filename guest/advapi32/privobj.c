/*
 * private object security, access mask helpers and audit alarms.
 *
 * private object security wraps the ntdll rtl routines that keep descriptors for a subsystem's own objects. there is
 * no audit log, so the alarms fail like they do for a caller without the audit privilege.
 */
#include "advapi32_private.h"

#ifndef ACCESS_SYSTEM_SECURITY
#define ACCESS_SYSTEM_SECURITY 0x01000000L
#endif
#ifndef READ_CONTROL
#define READ_CONTROL 0x00020000L
#endif
#ifndef WRITE_DAC
#define WRITE_DAC 0x00040000L
#endif
#ifndef WRITE_OWNER
#define WRITE_OWNER 0x00080000L
#endif

static BOOL finish(NTSTATUS status)
{
    if (NT_SUCCESS(status))
        return TRUE;
    return AdvSetStatus(status);
}

ADVAPI BOOL WINAPI CreatePrivateObjectSecurity(PSECURITY_DESCRIPTOR parent, PSECURITY_DESCRIPTOR creator,
                                               PSECURITY_DESCRIPTOR *object, BOOL container, HANDLE token,
                                               PGENERIC_MAPPING mapping)
{
    return finish(RtlNewSecurityObject(parent, creator, object, container != 0, token, mapping));
}

ADVAPI BOOL WINAPI CreatePrivateObjectSecurityEx(PSECURITY_DESCRIPTOR parent, PSECURITY_DESCRIPTOR creator,
                                                 PSECURITY_DESCRIPTOR *object, GUID *type, BOOL container,
                                                 ULONG flags, HANDLE token, PGENERIC_MAPPING mapping)
{
    return finish(RtlNewSecurityObjectEx(parent, creator, object, type, container != 0, flags, token, mapping));
}

ADVAPI BOOL WINAPI CreatePrivateObjectSecurityWithMultipleInheritance(
    PSECURITY_DESCRIPTOR parent, PSECURITY_DESCRIPTOR creator, PSECURITY_DESCRIPTOR *object, GUID **types,
    ULONG count, BOOL container, ULONG flags, HANDLE token, PGENERIC_MAPPING mapping)
{
    return finish(RtlNewSecurityObjectWithMultipleInheritance(parent, creator, object, (const GUID **)types, count,
                                                              container != 0, flags, token, mapping));
}

ADVAPI BOOL WINAPI DestroyPrivateObjectSecurity(PSECURITY_DESCRIPTOR *object)
{
    return finish(RtlDeleteSecurityObject(object));
}

ADVAPI BOOL WINAPI GetPrivateObjectSecurity(PSECURITY_DESCRIPTOR object, SECURITY_INFORMATION information,
                                            PSECURITY_DESCRIPTOR result, DWORD length, PDWORD needed)
{
    return finish(RtlQuerySecurityObject(object, information, result, length, needed));
}

ADVAPI BOOL WINAPI SetPrivateObjectSecurity(SECURITY_INFORMATION information, PSECURITY_DESCRIPTOR modification,
                                            PSECURITY_DESCRIPTOR *object, PGENERIC_MAPPING mapping, HANDLE token)
{
    return finish(RtlSetSecurityObject(information, modification, object, mapping, token));
}

ADVAPI BOOL WINAPI SetPrivateObjectSecurityEx(SECURITY_INFORMATION information, PSECURITY_DESCRIPTOR modification,
                                              PSECURITY_DESCRIPTOR *object, ULONG flags, PGENERIC_MAPPING mapping,
                                              HANDLE token)
{
    return finish(RtlSetSecurityObjectEx(information, modification, object, flags, mapping, token));
}

ADVAPI BOOL WINAPI ConvertToAutoInheritPrivateObjectSecurity(PSECURITY_DESCRIPTOR parent,
                                                             PSECURITY_DESCRIPTOR current,
                                                             PSECURITY_DESCRIPTOR *result, GUID *type,
                                                             BOOLEAN container, PGENERIC_MAPPING mapping)
{
    return finish(RtlConvertToAutoInheritSecurityObject(parent, current, result, type, container, mapping));
}

/* ---------------------------------------------------------------- access masks */

ADVAPI BOOL WINAPI AreAllAccessesGranted(DWORD granted, DWORD desired)
{
    return RtlAreAllAccessesGranted(granted, desired);
}

ADVAPI BOOL WINAPI AreAnyAccessesGranted(DWORD granted, DWORD desired)
{
    return RtlAreAnyAccessesGranted(granted, desired);
}

/* the handle access needed to read the descriptor parts the information names. */
ADVAPI void WINAPI QuerySecurityAccessMask(SECURITY_INFORMATION information, LPDWORD desired)
{
    DWORD mask = 0;
    if (information & BACKUP_SECURITY_INFORMATION)
        mask = ACCESS_SYSTEM_SECURITY | READ_CONTROL;
    if (information & (0x17 | 0x20 | 0x40 | 0x80 | 0x100))
        mask |= READ_CONTROL;
    if (information & 8)
        mask |= ACCESS_SYSTEM_SECURITY;
    *desired = mask;
}

/* the handle access needed to change those parts. */
ADVAPI void WINAPI SetSecurityAccessMask(SECURITY_INFORMATION information, LPDWORD desired)
{
    DWORD mask = 0;
    if (information & BACKUP_SECURITY_INFORMATION)
        mask = ACCESS_SYSTEM_SECURITY | WRITE_DAC | WRITE_OWNER;
    if (information & 0x13)
        mask |= WRITE_OWNER;
    if (information & (4 | 0x20 | 0x80 | 0x100))
        mask |= WRITE_DAC;
    if (information & (8 | 0x40))
        mask |= ACCESS_SYSTEM_SECURITY;
    if ((information & 0x1F8) && (information & (PROTECTED_SACL_SECURITY_INFORMATION | UNPROTECTED_SACL_SECURITY_INFORMATION)))
        mask |= ACCESS_SYSTEM_SECURITY;
    *desired = mask;
}

ADVAPI BOOL WINAPI GetKernelObjectSecurity(HANDLE handle, SECURITY_INFORMATION information, PSECURITY_DESCRIPTOR sd,
                                           DWORD length, LPDWORD needed)
{
    return finish(NtQuerySecurityObject(handle, information, sd, length, needed));
}

ADVAPI BOOL WINAPI SetKernelObjectSecurity(HANDLE handle, SECURITY_INFORMATION information, PSECURITY_DESCRIPTOR sd)
{
    return finish(NtSetSecurityObject(handle, information, sd));
}

/* ---------------------------------------------------------------- privileges */

#define PRIVILEGE_SET_ALL_NECESSARY 1
#define SE_PRIVILEGE_USED_FOR_ACCESS 0x80000000L
#ifndef ERROR_BAD_IMPERSONATION_LEVEL
#define ERROR_BAD_IMPERSONATION_LEVEL 1346L
#endif

/*
 * a token may stand for a client in a check only if it is an impersonation token that identifies the client.
 * returns the error to report, or zero.
 */
DWORD AdvImpersonationError(HANDLE token)
{
    if (!token)
        return ERROR_INVALID_HANDLE;
    DWORD type = 0, level = 0, returned;
    NTSTATUS status = NtQueryInformationToken(token, TokenType, &type, sizeof(type), &returned);
    if (!NT_SUCCESS(status))
        return RtlNtStatusToDosError(status);
    if (type != 2)
        return ERROR_NO_IMPERSONATION_TOKEN;
    status = NtQueryInformationToken(token, TokenImpersonationLevel, &level, sizeof(level), &returned);
    if (!NT_SUCCESS(status))
        return RtlNtStatusToDosError(status);
    return level < 1 ? ERROR_BAD_IMPERSONATION_LEVEL : ERROR_SUCCESS;
}

/*
 * marks the required privileges the token holds enabled as used. the result says whether it holds all of them or,
 * unless the set requires all, any.
 */
ADVAPI BOOL WINAPI PrivilegeCheck(HANDLE token, PPRIVILEGE_SET required, LPBOOL result)
{
    if (!required || !result) {
        AdvSetError(ERROR_NOACCESS);
        return FALSE;
    }
    DWORD error = AdvImpersonationError(token);
    if (error) {
        /* this check has no error for a non impersonation token, so it reports a bad level. */
        AdvSetError(error == ERROR_NO_IMPERSONATION_TOKEN ? ERROR_BAD_IMPERSONATION_LEVEL : error);
        return FALSE;
    }
    TOKEN_PRIVILEGES *held = AdvQueryToken(token, TokenPrivileges);
    if (!held) {
        AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    DWORD found = 0;
    for (DWORD i = 0; i < required->PrivilegeCount; i++) {
        for (DWORD j = 0; j < held->PrivilegeCount; j++) {
            if (held->Privileges[j].Luid.LowPart == required->Privilege[i].Luid.LowPart &&
                held->Privileges[j].Luid.HighPart == required->Privilege[i].Luid.HighPart) {
                if (held->Privileges[j].Attributes & SE_PRIVILEGE_ENABLED) {
                    required->Privilege[i].Attributes |= SE_PRIVILEGE_USED_FOR_ACCESS;
                    found++;
                }
                break;
            }
        }
    }
    AdvFree(held);
    *result = (required->Control & PRIVILEGE_SET_ALL_NECESSARY) ? found == required->PrivilegeCount : found != 0;
    return TRUE;
}

/* ---------------------------------------------------------------- audit alarms */

/*
 * every alarm ends in a system service that checks the audit privilege first. nothing in a session holds it enabled,
 * so they fail with ERROR_PRIVILEGE_NOT_HELD after clearing their outputs.
 */
static BOOL not_held(void)
{
    AdvSetError(ERROR_PRIVILEGE_NOT_HELD);
    return FALSE;
}

ADVAPI BOOL WINAPI AccessCheckAndAuditAlarmW(LPCWSTR subsystem, LPVOID handle_id, LPWSTR type_name, LPWSTR name,
                                             PSECURITY_DESCRIPTOR sd, DWORD desired, PGENERIC_MAPPING mapping,
                                             BOOL creation, LPDWORD granted, LPBOOL status, LPBOOL generate)
{
    *generate = FALSE;
    return not_held();
}

ADVAPI BOOL WINAPI AccessCheckAndAuditAlarmA(LPCSTR subsystem, LPVOID handle_id, LPSTR type_name, LPSTR name,
                                             PSECURITY_DESCRIPTOR sd, DWORD desired, PGENERIC_MAPPING mapping,
                                             BOOL creation, LPDWORD granted, LPBOOL status, LPBOOL generate)
{
    *generate = FALSE;
    return not_held();
}

ADVAPI BOOL WINAPI AccessCheckByTypeAndAuditAlarmW(LPCWSTR subsystem, LPVOID handle_id, LPCWSTR type_name,
                                                   LPCWSTR name, PSECURITY_DESCRIPTOR sd, PSID self, DWORD desired,
                                                   DWORD audit_type, DWORD flags, PVOID types, DWORD type_count,
                                                   PGENERIC_MAPPING mapping, BOOL creation, LPDWORD granted,
                                                   LPBOOL status, LPBOOL generate)
{
    *generate = FALSE;
    return not_held();
}

ADVAPI BOOL WINAPI AccessCheckByTypeAndAuditAlarmA(LPCSTR subsystem, LPVOID handle_id, LPCSTR type_name,
                                                   LPCSTR name, PSECURITY_DESCRIPTOR sd, PSID self, DWORD desired,
                                                   DWORD audit_type, DWORD flags, PVOID types, DWORD type_count,
                                                   PGENERIC_MAPPING mapping, BOOL creation, LPDWORD granted,
                                                   LPBOOL status, LPBOOL generate)
{
    *generate = FALSE;
    return not_held();
}

ADVAPI BOOL WINAPI AccessCheckByTypeResultListAndAuditAlarmW(LPCWSTR subsystem, LPVOID handle_id, LPCWSTR type_name,
                                                             LPCWSTR name, PSECURITY_DESCRIPTOR sd, PSID self,
                                                             DWORD desired, DWORD audit_type, DWORD flags,
                                                             PVOID types, DWORD type_count,
                                                             PGENERIC_MAPPING mapping, BOOL creation,
                                                             LPDWORD granted, LPDWORD status, LPBOOL generate)
{
    *generate = FALSE;
    return not_held();
}

ADVAPI BOOL WINAPI AccessCheckByTypeResultListAndAuditAlarmA(LPCSTR subsystem, LPVOID handle_id, LPCSTR type_name,
                                                             LPCSTR name, PSECURITY_DESCRIPTOR sd, PSID self,
                                                             DWORD desired, DWORD audit_type, DWORD flags,
                                                             PVOID types, DWORD type_count,
                                                             PGENERIC_MAPPING mapping, BOOL creation,
                                                             LPDWORD granted, LPDWORD status, LPBOOL generate)
{
    *generate = FALSE;
    return not_held();
}

ADVAPI BOOL WINAPI AccessCheckByTypeResultListAndAuditAlarmByHandleW(
    LPCWSTR subsystem, LPVOID handle_id, HANDLE token, LPCWSTR type_name, LPCWSTR name, PSECURITY_DESCRIPTOR sd,
    PSID self, DWORD desired, DWORD audit_type, DWORD flags, PVOID types, DWORD type_count,
    PGENERIC_MAPPING mapping, BOOL creation, LPDWORD granted, LPDWORD status, LPBOOL generate)
{
    *generate = FALSE;
    return not_held();
}

ADVAPI BOOL WINAPI AccessCheckByTypeResultListAndAuditAlarmByHandleA(
    LPCSTR subsystem, LPVOID handle_id, HANDLE token, LPCSTR type_name, LPCSTR name, PSECURITY_DESCRIPTOR sd,
    PSID self, DWORD desired, DWORD audit_type, DWORD flags, PVOID types, DWORD type_count,
    PGENERIC_MAPPING mapping, BOOL creation, LPDWORD granted, LPDWORD status, LPBOOL generate)
{
    *generate = FALSE;
    return not_held();
}

ADVAPI BOOL WINAPI ObjectOpenAuditAlarmW(LPCWSTR subsystem, LPVOID handle_id, LPWSTR type_name, LPWSTR name,
                                         PSECURITY_DESCRIPTOR sd, HANDLE token, DWORD desired, DWORD granted,
                                         PPRIVILEGE_SET privileges, BOOL creation, BOOL access_granted,
                                         LPBOOL generate)
{
    return not_held();
}

ADVAPI BOOL WINAPI ObjectOpenAuditAlarmA(LPCSTR subsystem, LPVOID handle_id, LPSTR type_name, LPSTR name,
                                         PSECURITY_DESCRIPTOR sd, HANDLE token, DWORD desired, DWORD granted,
                                         PPRIVILEGE_SET privileges, BOOL creation, BOOL access_granted,
                                         LPBOOL generate)
{
    return not_held();
}

ADVAPI BOOL WINAPI ObjectPrivilegeAuditAlarmW(LPCWSTR subsystem, LPVOID handle_id, HANDLE token, DWORD desired,
                                              PPRIVILEGE_SET privileges, BOOL access_granted)
{
    return not_held();
}

ADVAPI BOOL WINAPI ObjectPrivilegeAuditAlarmA(LPCSTR subsystem, LPVOID handle_id, HANDLE token, DWORD desired,
                                              PPRIVILEGE_SET privileges, BOOL access_granted)
{
    return not_held();
}

ADVAPI BOOL WINAPI ObjectCloseAuditAlarmW(LPCWSTR subsystem, LPVOID handle_id, BOOL generate)
{
    return not_held();
}

ADVAPI BOOL WINAPI ObjectCloseAuditAlarmA(LPCSTR subsystem, LPVOID handle_id, BOOL generate)
{
    return not_held();
}

ADVAPI BOOL WINAPI ObjectDeleteAuditAlarmW(LPCWSTR subsystem, LPVOID handle_id, BOOL generate)
{
    return not_held();
}

ADVAPI BOOL WINAPI ObjectDeleteAuditAlarmA(LPCSTR subsystem, LPVOID handle_id, BOOL generate)
{
    return not_held();
}

ADVAPI BOOL WINAPI PrivilegedServiceAuditAlarmW(LPCWSTR subsystem, LPCWSTR service, HANDLE token,
                                                PPRIVILEGE_SET privileges, BOOL access_granted)
{
    return not_held();
}

ADVAPI BOOL WINAPI PrivilegedServiceAuditAlarmA(LPCSTR subsystem, LPCSTR service, HANDLE token,
                                                PPRIVILEGE_SET privileges, BOOL access_granted)
{
    return not_held();
}
