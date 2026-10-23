/*
 * logon, and processes started as another user.
 *
 * there is no lsa and no secondary logon service, and the machine has one interactive user. logging on as that user
 * succeeds with any password and gives a copy of the process token; any other account fails like a wrong name or
 * password. a process started with a token is created directly with a primary copy of it.
 */
#include "advapi32_private.h"

#define LOGON32_LOGON_INTERACTIVE 2
#define LOGON32_LOGON_NETWORK 3
#define LOGON32_LOGON_BATCH 4
#define LOGON32_LOGON_SERVICE 5
#define LOGON32_LOGON_UNLOCK 7
#define LOGON32_LOGON_NETWORK_CLEARTEXT 8
#define LOGON32_LOGON_NEW_CREDENTIALS 9
#define LOGON_WITH_PROFILE 0x1
#define LOGON_NETCREDENTIALS_ONLY 0x2

#ifndef ERROR_NO_SUCH_LOGON_SESSION
#define ERROR_NO_SUCH_LOGON_SESSION 1312L
#endif
#ifndef ERROR_LOGON_FAILURE
#define ERROR_LOGON_FAILURE 1326L
#endif

typedef struct QUOTA_LIMITS {
    SIZE_T PagedPoolLimit, NonPagedPoolLimit, MinimumWorkingSetSize, MaximumWorkingSetSize, PagefileLimit;
    LARGE_INTEGER TimeLimit;
} QUOTA_LIMITS, *PQUOTA_LIMITS;

ADVAPI BOOL WINAPI GetUserNameW(LPWSTR buffer, LPDWORD size);
WINBASEAPI BOOL WINAPI CreateProcessAsUserW(HANDLE token, LPCWSTR application, LPWSTR command_line,
                                        LPSECURITY_ATTRIBUTES process_sa, LPSECURITY_ATTRIBUTES thread_sa, BOOL inherit,
                                        DWORD flags, LPVOID environment, LPCWSTR directory, LPSTARTUPINFOW startup,
                                        LPPROCESS_INFORMATION info);

/* the session's account, given as user or user@machine, with the machine or . as domain. */
static BOOL is_session_user(PCWSTR user, PCWSTR domain)
{
    WCHAR name[260], machine[64], local_user[260];
    DWORD n = RTL_NUMBER_OF(name), m = RTL_NUMBER_OF(machine);
    if (!GetUserNameW(name, &n) || !GetComputerNameW(machine, &m))
        return FALSE;
    /* a user principal name carries its domain. */
    PCWSTR at = wcschr(user, L'@');
    SIZE_T length = at ? (SIZE_T)(at - user) : wcslen(user);
    if (length >= RTL_NUMBER_OF(local_user))
        return FALSE;
    memcpy(local_user, user, length * sizeof(WCHAR));
    local_user[length] = 0;
    if (at) {
        if (domain && *domain)
            return FALSE;
        domain = at + 1;
    }
    if (_wcsicmp(local_user, name))
        return FALSE;
    return !domain || !*domain || !wcscmp(domain, L".") || !_wcsicmp(domain, machine);
}

/* a copy of the process token: primary, or impersonation for a network logon. */
static NTSTATUS logon_token(DWORD type, PHANDLE token)
{
    HANDLE process_token;
    NTSTATUS status = NtOpenProcessToken(NtCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, &process_token);
    if (!NT_SUCCESS(status))
        return status;
    struct {
        DWORD Length;
        SECURITY_IMPERSONATION_LEVEL Level;
        BOOLEAN Tracking, Effective;
    } qos = {sizeof(qos), SecurityImpersonation, TRUE, FALSE};
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, nullptr, 0, nullptr, nullptr);
    oa.SecurityQualityOfService = &qos;
    BOOL network = type == LOGON32_LOGON_NETWORK;
    status = NtDuplicateToken(process_token, TOKEN_ALL_ACCESS, &oa, FALSE, network ? TokenImpersonation : TokenPrimary,
                              token);
    NtClose(process_token);
    return status;
}

/* the token's logon sid, in a process heap block. */
static PSID logon_sid(HANDLE token)
{
    TOKEN_GROUPS *groups = AdvQueryTokenGroups(token);
    PSID sid = nullptr;
    for (DWORD i = 0; groups && i < groups->GroupCount; i++) {
        if ((groups->Groups[i].Attributes & SE_GROUP_LOGON_ID) == SE_GROUP_LOGON_ID) {
            DWORD length = AdvSidLength(groups->Groups[i].Sid);
            sid = AdvAlloc(length);
            if (sid)
                memcpy(sid, groups->Groups[i].Sid, length);
            break;
        }
    }
    AdvFree(groups);
    return sid;
}

/* checks in the order windows does: provider, type, name, outputs. the password is ignored. */
ADVAPI BOOL WINAPI LogonUserExW(LPCWSTR user, LPCWSTR domain, LPCWSTR password, DWORD type, DWORD provider,
                                PHANDLE token, PSID *sid, PVOID *profile, LPDWORD profile_length,
                                PQUOTA_LIMITS quota)
{
    if (token)
        *token = nullptr;
    if (provider > 4) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    switch (type) {
    case LOGON32_LOGON_INTERACTIVE:
    case LOGON32_LOGON_NETWORK:
    case LOGON32_LOGON_BATCH:
    case LOGON32_LOGON_SERVICE:
    case LOGON32_LOGON_UNLOCK:
    case LOGON32_LOGON_NETWORK_CLEARTEXT:
    case LOGON32_LOGON_NEW_CREDENTIALS:
        break;
    default:
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!user || !*user || !token || (profile != nullptr) != (profile_length != nullptr)) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (sid)
        *sid = nullptr;
    if (profile) {
        *profile = nullptr;
        *profile_length = 0;
    }
    if (quota)
        memset(quota, 0, sizeof(*quota));
    if (!is_session_user(user, domain)) {
        AdvSetError(ERROR_LOGON_FAILURE);
        return FALSE;
    }
    HANDLE logon;
    NTSTATUS status = logon_token(type, &logon);
    if (!NT_SUCCESS(status))
        return AdvSetStatus(status);
    if (sid) {
        *sid = logon_sid(logon);
        if (!*sid) {
            NtClose(logon);
            AdvSetError(ERROR_NO_SUCH_LOGON_SESSION);
            return FALSE;
        }
    }
    *token = logon;
    return TRUE;
}

ADVAPI BOOL WINAPI LogonUserW(LPCWSTR user, LPCWSTR domain, LPCWSTR password, DWORD type, DWORD provider,
                              PHANDLE token)
{
    if (token)
        *token = nullptr;
    return LogonUserExW(user, domain, password, type, provider, token, nullptr, nullptr, nullptr, nullptr);
}

ADVAPI BOOL WINAPI LogonUserA(LPCSTR user, LPCSTR domain, LPCSTR password, DWORD type, DWORD provider, PHANDLE token)
{
    PWSTR u = AdvAnsiToWide(user), d = AdvAnsiToWide(domain);
    BOOL ok = (!user || u) && (!domain || d) && LogonUserW(u, d, nullptr, type, provider, token);
    AdvFree(u);
    AdvFree(d);
    return ok;
}

ADVAPI BOOL WINAPI LogonUserExA(LPCSTR user, LPCSTR domain, LPCSTR password, DWORD type, DWORD provider, PHANDLE token,
                                PSID *sid, PVOID *profile, LPDWORD profile_length, PQUOTA_LIMITS quota)
{
    PWSTR u = AdvAnsiToWide(user), d = AdvAnsiToWide(domain);
    BOOL ok = (!user || u) && (!domain || d) &&
              LogonUserExW(u, d, nullptr, type, provider, token, sid, profile, profile_length, quota);
    AdvFree(u);
    AdvFree(d);
    return ok;
}

/* windows has the secondary logon service start the process; here it is created directly. */
ADVAPI BOOL WINAPI CreateProcessWithTokenW(HANDLE token, DWORD logon_flags, LPCWSTR application, LPWSTR command_line,
                                           DWORD creation_flags, LPVOID environment, LPCWSTR directory,
                                           LPSTARTUPINFOW startup, LPPROCESS_INFORMATION info)
{
    if (!startup || !info || (!application && !command_line) || (creation_flags & EXTENDED_STARTUPINFO_PRESENT)) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    memset(info, 0, sizeof(*info));
    HANDLE primary;
    OBJECT_ATTRIBUTES oa;
    /* the copy keeps the source token's descriptor, so it stays open to whoever that token was. */
    UCHAR descriptor[1024];
    ULONG needed;
    PVOID sd = NT_SUCCESS(NtQuerySecurityObject(token, 0x1F /* owner, group, dacl, sacl, label */ & ~8u, descriptor,
                                                sizeof(descriptor), &needed))
                   ? descriptor
                   : nullptr;
    InitializeObjectAttributes(&oa, nullptr, 0, nullptr, sd);
    NTSTATUS status = NtDuplicateToken(token, TOKEN_ALL_ACCESS, &oa, FALSE, TokenPrimary, &primary);
    if (!NT_SUCCESS(status))
        return AdvSetStatus(status);
    BOOL ok = CreateProcessAsUserW(primary, application, command_line, nullptr, nullptr, FALSE, creation_flags,
                                   environment, directory, startup, info);
    DWORD error = GetLastError();
    NtClose(primary);
    if (!ok)
        AdvSetError(error);
    return ok;
}

ADVAPI BOOL WINAPI CreateProcessWithLogonW(LPCWSTR user, LPCWSTR domain, LPCWSTR password, DWORD logon_flags,
                                           LPCWSTR application, LPWSTR command_line, DWORD creation_flags,
                                           LPVOID environment, LPCWSTR directory, LPSTARTUPINFOW startup,
                                           LPPROCESS_INFORMATION info)
{
    if (!user || !startup || !info || (logon_flags & ~(DWORD)(LOGON_WITH_PROFILE | LOGON_NETCREDENTIALS_ONLY))) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    HANDLE token;
    if (!LogonUserW(user, domain, password, LOGON32_LOGON_INTERACTIVE, 0, &token))
        return FALSE;
    BOOL ok = CreateProcessWithTokenW(token, logon_flags, application, command_line, creation_flags, environment,
                                      directory, startup, info);
    DWORD error = GetLastError();
    NtClose(token);
    if (!ok)
        AdvSetError(error);
    return ok;
}
