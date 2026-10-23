/*
 * access tokens: opening, querying, duplicating and privileges. tokens are kernel objects; this is the win32 side of
 * their services.
 */
#include "advapi32_private.h"

/* the kernel knows the pseudo handles of the process, thread and effective tokens. */
NTSTATUS AdvResolveToken(HANDLE token, ACCESS_MASK access, HANDLE *real, HANDLE *opened)
{
    *opened = nullptr;
    *real = token;
    return STATUS_SUCCESS;
}

/* NtQueryInformationToken into a heap buffer sized from the first answer. */
static PVOID query_token(HANDLE token, TOKEN_INFORMATION_CLASS klass, NTSTATUS *result)
{
    HANDLE real, opened;
    NTSTATUS status = AdvResolveToken(token ? token : ADV_CURRENT_THREAD_EFFECTIVE_TOKEN, TOKEN_QUERY, &real, &opened);
    PVOID buffer = nullptr;
    ULONG size = 256;
    while (NT_SUCCESS(status) || status == STATUS_BUFFER_TOO_SMALL) {
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
        buffer = nullptr;
    }
    if (result)
        *result = status;
    return buffer;
}

TOKEN_USER *AdvQueryTokenUser(HANDLE token)
{
    return query_token(token, TokenUser, nullptr);
}

TOKEN_GROUPS *AdvQueryTokenGroups(HANDLE token)
{
    return query_token(token, TokenGroups, nullptr);
}

PVOID AdvQueryToken(HANDLE token, DWORD klass)
{
    return query_token(token, klass, nullptr);
}

BOOL AdvCurrentUserSid(void *storage)
{
    TOKEN_USER *user = AdvQueryTokenUser(nullptr);
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

/* the quality of service of every copy a thread makes: dynamic tracking, full identity. */
typedef struct TOKEN_QOS {
    DWORD Length;
    SECURITY_IMPERSONATION_LEVEL ImpersonationLevel;
    BOOLEAN ContextTrackingMode;
    BOOLEAN EffectiveOnly;
} TOKEN_QOS;

ADVAPI BOOL WINAPI DuplicateTokenEx(HANDLE token, DWORD access, LPSECURITY_ATTRIBUTES sa,
                                    SECURITY_IMPERSONATION_LEVEL level, TOKEN_TYPE type, PHANDLE result)
{
    TOKEN_QOS qos = {sizeof(qos), level, TRUE, FALSE};
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, nullptr, sa && sa->bInheritHandle ? OBJ_INHERIT : 0, nullptr,
                               sa ? sa->lpSecurityDescriptor : nullptr);
    oa.SecurityQualityOfService = &qos;
    NTSTATUS status = NtDuplicateToken(token, access, &oa, FALSE, type, result);
    return NT_SUCCESS(status) ? TRUE : AdvSetStatus(status);
}

ADVAPI BOOL WINAPI DuplicateToken(HANDLE token, SECURITY_IMPERSONATION_LEVEL level, PHANDLE result)
{
    return DuplicateTokenEx(token, TOKEN_IMPERSONATE | TOKEN_QUERY, nullptr, level, TokenImpersonation, result);
}

ADVAPI BOOL WINAPI SetTokenInformation(HANDLE token, TOKEN_INFORMATION_CLASS klass, LPVOID info, DWORD length)
{
    NTSTATUS status = NtSetInformationToken(token, klass, info, length);
    return NT_SUCCESS(status) ? TRUE : AdvSetStatus(status);
}

ADVAPI BOOL WINAPI AllocateLocallyUniqueId(PLUID luid)
{
    NTSTATUS status = NtAllocateLocallyUniqueId(luid);
    return NT_SUCCESS(status) ? TRUE : AdvSetStatus(status);
}

/* a restricted token's arrays are copied after their counts, in the layout the kernel reads. */
ADVAPI BOOL WINAPI CreateRestrictedToken(HANDLE token, DWORD flags, DWORD disable_count, PSID_AND_ATTRIBUTES disable,
                                         DWORD delete_count, PLUID_AND_ATTRIBUTES delete_privileges,
                                         DWORD restrict_count, PSID_AND_ATTRIBUTES restrict_sids, PHANDLE result)
{
    TOKEN_GROUPS *disabled = nullptr, *restricted = nullptr;
    TOKEN_PRIVILEGES *deleted = nullptr;
    NTSTATUS status = STATUS_SUCCESS;
    if (disable_count) {
        if (!disable) {
            status = STATUS_INVALID_PARAMETER;
        } else if ((ULONG64)disable_count * sizeof(SID_AND_ATTRIBUTES) > 0xFFFFFFE0) {
            status = (NTSTATUS)0xC0000095L; /* STATUS_INTEGER_OVERFLOW. */
        } else {
            disabled = AdvAlloc(8 + (SIZE_T)disable_count * sizeof(SID_AND_ATTRIBUTES));
            if (!disabled) {
                status = STATUS_INSUFFICIENT_RESOURCES;
            } else {
                disabled->GroupCount = disable_count;
                memcpy(disabled->Groups, disable, (SIZE_T)disable_count * sizeof(SID_AND_ATTRIBUTES));
            }
        }
    }
    if (NT_SUCCESS(status) && delete_count) {
        if (!delete_privileges) {
            status = STATUS_INVALID_PARAMETER;
        } else if ((ULONG64)delete_count * sizeof(LUID_AND_ATTRIBUTES) > 0xFFFFFFE0) {
            status = (NTSTATUS)0xC0000095L;
        } else {
            deleted = AdvAlloc(4 + (SIZE_T)delete_count * sizeof(LUID_AND_ATTRIBUTES));
            if (!deleted) {
                status = STATUS_INSUFFICIENT_RESOURCES;
            } else {
                deleted->PrivilegeCount = delete_count;
                memcpy(deleted->Privileges, delete_privileges, (SIZE_T)delete_count * sizeof(LUID_AND_ATTRIBUTES));
            }
        }
    }
    if (NT_SUCCESS(status) && restrict_count) {
        if (!restrict_sids) {
            status = STATUS_INVALID_PARAMETER;
        } else if ((ULONG64)restrict_count * sizeof(SID_AND_ATTRIBUTES) > 0xFFFFFFE0) {
            status = (NTSTATUS)0xC0000095L;
        } else {
            restricted = AdvAlloc(8 + (SIZE_T)restrict_count * sizeof(SID_AND_ATTRIBUTES));
            if (!restricted) {
                status = STATUS_INSUFFICIENT_RESOURCES;
            } else {
                restricted->GroupCount = restrict_count;
                memcpy(restricted->Groups, restrict_sids, (SIZE_T)restrict_count * sizeof(SID_AND_ATTRIBUTES));
            }
        }
    }
    if (NT_SUCCESS(status))
        status = NtFilterToken(token, flags, disabled, deleted, restricted, result);
    AdvFree(disabled);
    AdvFree(deleted);
    AdvFree(restricted);
    return NT_SUCCESS(status) ? TRUE : AdvSetStatus(status);
}

/* a token is restricted when it has restricting sids. */
ADVAPI BOOL WINAPI IsTokenRestricted(HANDLE token)
{
    ULONG needed = 0;
    NTSTATUS status = NtQueryInformationToken(token, TokenRestrictedSids, nullptr, 0, &needed);
    if (status != STATUS_BUFFER_TOO_SMALL)
        return AdvSetStatus(status);
    TOKEN_GROUPS *groups = AdvAlloc(needed);
    if (!groups) {
        AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    status = NtQueryInformationToken(token, TokenRestrictedSids, groups, needed, &needed);
    BOOL restricted = NT_SUCCESS(status) && groups->GroupCount != 0;
    AdvFree(groups);
    if (!NT_SUCCESS(status))
        return AdvSetStatus(status);
    return restricted;
}

/* ---------------------------------------------------------------- thread identities */

ADVAPI BOOL WINAPI SetThreadToken(PHANDLE thread, HANDLE token)
{
    HANDLE info = token;
    NTSTATUS status = NtSetInformationThread(thread ? *thread : NtCurrentThread(), ThreadImpersonationToken, &info,
                                             sizeof(info));
    return NT_SUCCESS(status) ? TRUE : AdvSetStatus(status);
}

ADVAPI BOOL WINAPI RevertToSelf(void)
{
    HANDLE none = nullptr;
    NTSTATUS status = NtSetInformationThread(NtCurrentThread(), ThreadImpersonationToken, &none, sizeof(none));
    return NT_SUCCESS(status) ? TRUE : AdvSetStatus(status);
}

ADVAPI BOOL WINAPI ImpersonateSelf(SECURITY_IMPERSONATION_LEVEL level)
{
    NTSTATUS status = RtlImpersonateSelf(level);
    return NT_SUCCESS(status) ? TRUE : AdvSetStatus(status);
}

/* an impersonation token is used directly, a primary one through a copy that can impersonate. */
ADVAPI BOOL WINAPI ImpersonateLoggedOnUser(HANDLE token)
{
    ULONG type = 0, returned;
    NTSTATUS status = NtQueryInformationToken(token, TokenType, &type, sizeof(type), &returned);
    if (NT_SUCCESS(status)) {
        HANDLE copy = token;
        if (type == TokenPrimary) {
            TOKEN_QOS qos = {sizeof(qos), SecurityImpersonation, TRUE, FALSE};
            OBJECT_ATTRIBUTES oa;
            InitializeObjectAttributes(&oa, nullptr, 0, nullptr, nullptr);
            oa.SecurityQualityOfService = &qos;
            status = NtDuplicateToken(token, TOKEN_QUERY | TOKEN_IMPERSONATE, &oa, FALSE, TokenImpersonation, &copy);
        }
        if (NT_SUCCESS(status)) {
            status = NtSetInformationThread(NtCurrentThread(), ThreadImpersonationToken, &copy, sizeof(copy));
            if (type == TokenPrimary)
                NtClose(copy);
        }
    }
    return NT_SUCCESS(status) ? TRUE : AdvSetStatus(status);
}

ADVAPI BOOL WINAPI ImpersonateAnonymousToken(HANDLE thread)
{
    NTSTATUS status = NtImpersonateAnonymousToken(thread);
    return NT_SUCCESS(status) ? TRUE : AdvSetStatus(status);
}

/* the pipe server takes its client's identity with FSCTL_PIPE_IMPERSONATE, waiting when pending. */
ADVAPI BOOL WINAPI ImpersonateNamedPipeClient(HANDLE pipe)
{
    HANDLE event = nullptr;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb = {};
    InitializeObjectAttributes(&oa, nullptr, 0, nullptr, nullptr);
    NTSTATUS status = NtCreateEvent(&event, EVENT_ALL_ACCESS, &oa, NotificationEvent, FALSE);
    if (NT_SUCCESS(status)) {
        status = NtFsControlFile(pipe, event, nullptr, nullptr, &iosb, 0x0011001C /* FSCTL_PIPE_IMPERSONATE */, nullptr,
                                 0, nullptr, 0);
        if (status == STATUS_PENDING) {
            status = NtWaitForSingleObject(event, FALSE, nullptr);
            if (NT_SUCCESS(status))
                status = iosb.Status;
        }
        NtClose(event);
    }
    return NT_SUCCESS(status) ? TRUE : AdvSetStatus(status);
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

ADVAPI BOOL WINAPI AdjustTokenGroups(HANDLE token, BOOL reset_to_default, PTOKEN_GROUPS state, DWORD length,
                                     PTOKEN_GROUPS previous, PDWORD returned)
{
    ULONG n = 0;
    NTSTATUS status = NtAdjustGroupsToken(token, (BOOLEAN)reset_to_default, state, length, previous, &n);
    if (returned)
        *returned = n;
    if (!NT_SUCCESS(status))
        return AdvSetStatus(status);
    AdvSetError(status == 0x00000106L ? ERROR_NOT_ALL_ASSIGNED : ERROR_SUCCESS);
    return TRUE;
}

/* a token is a member of its user, its enabled groups that are not deny only, and what restricts it. */
ADVAPI BOOL WINAPI CheckTokenMembership(HANDLE token, PSID sid, PBOOL member)
{
    if (!member || !AdvSidValid(sid)) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *member = FALSE;
    NTSTATUS status;
    TOKEN_USER *user = query_token(token, TokenUser, &status);
    TOKEN_GROUPS *groups = user ? query_token(token, TokenGroups, &status) : nullptr;
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
    /* a restricted token must also hold the sid among its restricting sids. */
    if (*member) {
        TOKEN_GROUPS *restricted = query_token(token, TokenRestrictedSids, &status);
        if (restricted && restricted->GroupCount) {
            *member = FALSE;
            for (DWORD i = 0; i < restricted->GroupCount && !*member; i++) {
                DWORD a = restricted->Groups[i].Attributes;
                if ((a & SE_GROUP_ENABLED) && !(a & SE_GROUP_USE_FOR_DENY_ONLY) &&
                    AdvSidEqual(restricted->Groups[i].Sid, sid))
                    *member = TRUE;
            }
        }
        AdvFree(restricted);
    }
    AdvFree(user);
    AdvFree(groups);
    return TRUE;
}

/* ---------------------------------------------------------------- privilege names */

/* each privilege's luid is its index in this table, the same on every windows system. */
static const PCWSTR g_privileges[] = {
    nullptr,
    nullptr,
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

/* returns the name and its length without the terminator; when too small the size counts the terminator. */
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

/* each privilege's english display text, as windows has it, by luid. */
static const PCWSTR g_privilege_text[] = {
    nullptr,
    nullptr,
    L"Create a token object",
    L"Replace a process level token",
    L"Lock pages in memory",
    L"Adjust memory quotas for a process",
    L"Add workstations to domain",
    L"Act as part of the operating system",
    L"Manage auditing and security log",
    L"Take ownership of files or other objects",
    L"Load and unload device drivers",
    L"Profile system performance",
    L"Change the system time",
    L"Profile single process",
    L"Increase scheduling priority",
    L"Create a pagefile",
    L"Create permanent shared objects",
    L"Back up files and directories",
    L"Restore files and directories",
    L"Shut down the system",
    L"Debug programs",
    L"Generate security audits",
    L"Modify firmware environment values",
    L"Bypass traverse checking",
    L"Force shutdown from a remote system",
    L"Remove computer from docking station",
    L"Synchronize directory service data",
    L"Enable computer and user accounts to be trusted for delegation",
    L"Perform volume maintenance tasks",
    L"Impersonate a client after authentication",
    L"Create global objects",
    L"Access Credential Manager as a trusted caller",
    L"Modify an object label",
    L"Increase a process working set",
    L"Change the time zone",
    L"Create symbolic links",
    L"Obtain an impersonation token for another user in the same session",
};

/* a privilege's display text by name, sized like LookupPrivilegeName, plus the text's language. */
ADVAPI BOOL WINAPI LookupPrivilegeDisplayNameW(LPCWSTR system, LPCWSTR name, LPWSTR display, LPDWORD size, LPDWORD language)
{
    if (!name || !size) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!AdvIsLocalSystem(system)) {
        AdvSetError(RPC_S_SERVER_UNAVAILABLE);
        return FALSE;
    }
    PCWSTR text = nullptr;
    for (DWORD i = 0; i < RTL_NUMBER_OF(g_privileges) && !text; i++)
        if (g_privileges[i] && !_wcsicmp(name, g_privileges[i]))
            text = g_privilege_text[i];
    if (!text) {
        AdvSetError(ERROR_NO_SUCH_PRIVILEGE);
        return FALSE;
    }
    DWORD n = (DWORD)wcslen(text);
    if (!display || *size <= n) {
        *size = n + 1;
        AdvSetError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    memcpy(display, text, (n + 1) * sizeof(WCHAR));
    *size = n;
    if (language)
        *language = 0x409;
    return TRUE;
}

ADVAPI BOOL WINAPI LookupPrivilegeDisplayNameA(LPCSTR system, LPCSTR name, LPSTR display, LPDWORD size, LPDWORD language)
{
    if (!name || !size) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PWSTR s = AdvAnsiToWide(system), n = AdvAnsiToWide(name);
    WCHAR wide[96];
    DWORD length = RTL_NUMBER_OF(wide);
    BOOL ok = (!system || s) && n && LookupPrivilegeDisplayNameW(s, n, wide, &length, language);
    AdvFree(s);
    AdvFree(n);
    if (!ok && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return FALSE;
    if (!ok) {
        /* a text too long for the stack buffer: pass on the size the wide call reported. */
        *size = length;
        return FALSE;
    }
    if (!display || *size <= length) {
        *size = length + 1;
        AdvSetError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    for (DWORD i = 0; i <= length; i++)
        display[i] = (CHAR)wide[i];
    *size = length;
    return TRUE;
}
