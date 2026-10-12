/*
 * access tokens: opening, querying, duplicating and privileges.
 *
 * the kernel describes every token of the session; what it does not
 * implement yet (NtDuplicateToken) is approximated here, see DuplicateTokenEx.
 */
#include "advapi32_private.h"

NTSTATUS AdvResolveToken(HANDLE token, ACCESS_MASK access, HANDLE *real, HANDLE *opened)
{
    *opened = NULL;
    *real = token;
    if (token == ADV_CURRENT_THREAD_TOKEN || token == ADV_CURRENT_THREAD_EFFECTIVE_TOKEN) {
        NTSTATUS status = NtOpenThreadToken(NtCurrentThread(), access, TRUE, opened);
        if (NT_SUCCESS(status) || token == ADV_CURRENT_THREAD_TOKEN) {
            *real = *opened;
            return status;
        }
        token = ADV_CURRENT_PROCESS_TOKEN;
    }
    if (token == ADV_CURRENT_PROCESS_TOKEN) {
        NTSTATUS status = NtOpenProcessToken(NtCurrentProcess(), access, opened);
        *real = *opened;
        return status;
    }
    return STATUS_SUCCESS;
}

/* NtQueryInformationToken into a heap buffer sized from the first answer. */
static PVOID query_token(HANDLE token, TOKEN_INFORMATION_CLASS klass, NTSTATUS *result)
{
    HANDLE real, opened;
    NTSTATUS status = AdvResolveToken(token ? token : ADV_CURRENT_THREAD_EFFECTIVE_TOKEN, TOKEN_QUERY, &real, &opened);
    PVOID buffer = NULL;
    ULONG size = 256;
    while (NT_SUCCESS(status)) {
        AdvFree(buffer);
        buffer = AdvAlloc(size);
        if (!buffer) {
            status = STATUS_NO_MEMORY;
            break;
        }
        ULONG needed = 0;
        status = NtQueryInformationToken(real, klass, buffer, size, &needed);
        if (status != STATUS_BUFFER_TOO_SMALL || needed <= size)
            break;
        size = needed;
    }
    if (opened)
        NtClose(opened);
    if (!NT_SUCCESS(status)) {
        AdvFree(buffer);
        buffer = NULL;
    }
    if (result)
        *result = status;
    return buffer;
}

TOKEN_USER *AdvQueryTokenUser(HANDLE token)
{
    return query_token(token, TokenUser, NULL);
}

TOKEN_GROUPS *AdvQueryTokenGroups(HANDLE token)
{
    return query_token(token, TokenGroups, NULL);
}

PVOID AdvQueryToken(HANDLE token, DWORD klass)
{
    return query_token(token, klass, NULL);
}

BOOL AdvCurrentUserSid(void *storage)
{
    TOKEN_USER *user = AdvQueryTokenUser(NULL);
    BOOL ok = user && AdvSidValid(user->User.Sid);
    if (ok)
        memcpy(storage, user->User.Sid, AdvSidLength(user->User.Sid));
    AdvFree(user);
    return ok;
}

ADVAPI BOOL WINAPI OpenProcessToken(HANDLE process, DWORD access, PHANDLE token)
{
    NTSTATUS status = NtOpenProcessToken(process, access, token);
    return NT_SUCCESS(status) ? TRUE : AdvSetStatus(status);
}

ADVAPI BOOL WINAPI OpenThreadToken(HANDLE thread, DWORD access, BOOL self, PHANDLE token)
{
    NTSTATUS status = NtOpenThreadToken(thread, access, (BOOLEAN)self, token);
    return NT_SUCCESS(status) ? TRUE : AdvSetStatus(status);
}

ADVAPI BOOL WINAPI GetTokenInformation(HANDLE token, TOKEN_INFORMATION_CLASS klass, LPVOID info, DWORD length,
                                       PDWORD returned)
{
    HANDLE real, opened;
    NTSTATUS status = AdvResolveToken(token, TOKEN_QUERY, &real, &opened);
    if (NT_SUCCESS(status)) {
        ULONG n = 0;
        status = NtQueryInformationToken(real, klass, info, length, &n);
        if (returned)
            *returned = n;
    }
    if (opened)
        NtClose(opened);
    return NT_SUCCESS(status) ? TRUE : AdvSetStatus(status);
}

/*
 * NtDuplicateToken when the kernel has it. without it, the new handle refers
 * to the same token object: the kernel's tokens are immutable descriptions of
 * the session's user, so a copy would describe the same identity anyway.
 */
ADVAPI BOOL WINAPI DuplicateTokenEx(HANDLE token, DWORD access, LPSECURITY_ATTRIBUTES sa,
                                    SECURITY_IMPERSONATION_LEVEL level, TOKEN_TYPE type, PHANDLE result)
{
    if (!result || (type != TokenPrimary && type != TokenImpersonation) || (DWORD)level > SecurityDelegation) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    struct {
        DWORD Length;
        SECURITY_IMPERSONATION_LEVEL ImpersonationLevel;
        BOOLEAN ContextTrackingMode;
        BOOLEAN EffectiveOnly;
    } qos = {sizeof(qos), level, TRUE, FALSE}; /* dynamic tracking, as kernelbase asks */
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, NULL, sa && sa->bInheritHandle ? OBJ_INHERIT : 0, NULL,
                               sa ? sa->lpSecurityDescriptor : NULL);
    oa.SecurityQualityOfService = &qos;
    NTSTATUS status = NtDuplicateToken(token, access, &oa, FALSE, type, result);
    if (status == STATUS_NOT_IMPLEMENTED)
        status = NtDuplicateObject(NtCurrentProcess(), token, NtCurrentProcess(), result, 0,
                                   sa && sa->bInheritHandle ? OBJ_INHERIT : 0, DUPLICATE_SAME_ACCESS);
    return NT_SUCCESS(status) ? TRUE : AdvSetStatus(status);
}

ADVAPI BOOL WINAPI DuplicateToken(HANDLE token, SECURITY_IMPERSONATION_LEVEL level, PHANDLE result)
{
    return DuplicateTokenEx(token, TOKEN_IMPERSONATE | TOKEN_QUERY, NULL, level, TokenImpersonation, result);
}

ADVAPI BOOL WINAPI AdjustTokenPrivileges(HANDLE token, BOOL disable_all, PTOKEN_PRIVILEGES state, DWORD length,
                                         PTOKEN_PRIVILEGES previous, PDWORD returned)
{
    ULONG n = 0;
    NTSTATUS status = NtAdjustPrivilegesToken(token, (BOOLEAN)disable_all, state, length, previous, &n);
    if (returned)
        *returned = n;
    if (!NT_SUCCESS(status))
        return AdvSetStatus(status);
    /* STATUS_NOT_ALL_ASSIGNED is a success that still reports the error. */
    AdvSetError(status == 0x00000106L ? ERROR_NOT_ALL_ASSIGNED : ERROR_SUCCESS);
    return TRUE;
}

/* a token is a member of its user and of its enabled groups that are not for deny only. */
ADVAPI BOOL WINAPI CheckTokenMembership(HANDLE token, PSID sid, PBOOL member)
{
    if (!member || !AdvSidValid(sid)) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *member = FALSE;
    NTSTATUS status;
    TOKEN_USER *user = query_token(token, TokenUser, &status);
    TOKEN_GROUPS *groups = user ? query_token(token, TokenGroups, &status) : NULL;
    if (!groups) {
        AdvFree(user);
        return AdvSetStatus(status);
    }
    if (AdvSidEqual(user->User.Sid, sid))
        *member = TRUE;
    for (DWORD i = 0; i < groups->GroupCount && !*member; i++) {
        DWORD a = groups->Groups[i].Attributes;
        if ((a & SE_GROUP_ENABLED) && !(a & SE_GROUP_USE_FOR_DENY_ONLY) && AdvSidEqual(groups->Groups[i].Sid, sid))
            *member = TRUE;
    }
    AdvFree(user);
    AdvFree(groups);
    return TRUE;
}

/* ---------------------------------------------------------------- privilege names */

/* the luid of each privilege is its index in this table, as on every windows system. */
static const PCWSTR g_privileges[] = {
    NULL,
    NULL,
    L"SeCreateTokenPrivilege",
    L"SeAssignPrimaryTokenPrivilege",
    L"SeLockMemoryPrivilege",
    L"SeIncreaseQuotaPrivilege",
    L"SeMachineAccountPrivilege",
    L"SeTcbPrivilege",
    L"SeSecurityPrivilege",
    L"SeTakeOwnershipPrivilege",
    L"SeLoadDriverPrivilege",
    L"SeSystemProfilePrivilege",
    L"SeSystemtimePrivilege",
    L"SeProfileSingleProcessPrivilege",
    L"SeIncreaseBasePriorityPrivilege",
    L"SeCreatePagefilePrivilege",
    L"SeCreatePermanentPrivilege",
    L"SeBackupPrivilege",
    L"SeRestorePrivilege",
    L"SeShutdownPrivilege",
    L"SeDebugPrivilege",
    L"SeAuditPrivilege",
    L"SeSystemEnvironmentPrivilege",
    L"SeChangeNotifyPrivilege",
    L"SeRemoteShutdownPrivilege",
    L"SeUndockPrivilege",
    L"SeSyncAgentPrivilege",
    L"SeEnableDelegationPrivilege",
    L"SeManageVolumePrivilege",
    L"SeImpersonatePrivilege",
    L"SeCreateGlobalPrivilege",
    L"SeTrustedCredManAccessPrivilege",
    L"SeRelabelPrivilege",
    L"SeIncreaseWorkingSetPrivilege",
    L"SeTimeZonePrivilege",
    L"SeCreateSymbolicLinkPrivilege",
    L"SeDelegateSessionUserImpersonatePrivilege",
};

ADVAPI BOOL WINAPI LookupPrivilegeValueW(LPCWSTR system, LPCWSTR name, PLUID luid)
{
    if (!name || !luid) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!AdvIsLocalSystem(system)) {
        AdvSetError(RPC_S_SERVER_UNAVAILABLE);
        return FALSE;
    }
    for (DWORD i = 0; i < RTL_NUMBER_OF(g_privileges); i++) {
        if (g_privileges[i] && !_wcsicmp(name, g_privileges[i])) {
            luid->LowPart = i;
            luid->HighPart = 0;
            return TRUE;
        }
    }
    AdvSetError(ERROR_NO_SUCH_PRIVILEGE);
    return FALSE;
}

ADVAPI BOOL WINAPI LookupPrivilegeValueA(LPCSTR system, LPCSTR name, PLUID luid)
{
    PWSTR s = AdvAnsiToWide(system), n = AdvAnsiToWide(name);
    BOOL ok = (!system || s) && (!name || n) && LookupPrivilegeValueW(s, n, luid);
    AdvFree(s);
    AdvFree(n);
    return ok;
}

/* the name and its length without the terminator; the size counts the terminator when too small. */
ADVAPI BOOL WINAPI LookupPrivilegeNameW(LPCWSTR system, PLUID luid, LPWSTR name, LPDWORD size)
{
    if (!luid || !size) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!AdvIsLocalSystem(system)) {
        AdvSetError(RPC_S_SERVER_UNAVAILABLE);
        return FALSE;
    }
    if (luid->HighPart || luid->LowPart >= RTL_NUMBER_OF(g_privileges) || !g_privileges[luid->LowPart]) {
        AdvSetError(ERROR_NO_SUCH_PRIVILEGE);
        return FALSE;
    }
    PCWSTR text = g_privileges[luid->LowPart];
    DWORD n = (DWORD)wcslen(text);
    if (!name || *size <= n) {
        *size = n + 1;
        AdvSetError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    memcpy(name, text, (n + 1) * sizeof(WCHAR));
    *size = n;
    return TRUE;
}

ADVAPI BOOL WINAPI LookupPrivilegeNameA(LPCSTR system, PLUID luid, LPSTR name, LPDWORD size)
{
    if (!size) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PWSTR s = AdvAnsiToWide(system);
    WCHAR wide[64];
    DWORD n = RTL_NUMBER_OF(wide);
    BOOL ok = (!system || s) && LookupPrivilegeNameW(s, luid, wide, &n);
    AdvFree(s);
    if (!ok)
        return FALSE;
    if (!name || *size <= n) {
        *size = n + 1;
        AdvSetError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    for (DWORD i = 0; i <= n; i++)
        name[i] = (CHAR)wide[i];
    *size = n;
    return TRUE;
}
