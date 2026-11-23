#include <mime/win32/winbase.h>
#include <mime/ntapi.h>
#include <mime/ntrtl.h>

#define LOGON32_LOGON_INTERACTIVE 2
#define LOGON32_LOGON_NETWORK 3
#define LOGON32_LOGON_BATCH 4
#define LOGON32_LOGON_SERVICE 5
#define LOGON32_LOGON_UNLOCK 7
#define LOGON32_LOGON_NETWORK_CLEARTEXT 8
#define LOGON32_LOGON_NEW_CREDENTIALS 9
#define LOGON32_PROVIDER_VIRTUAL 4
#define LOGON_TOKEN_GROUPS 2
#define LOGON_GROUP_LOGON_ID 0xC0000000u
#define LOGON_NEVER 0x7FFFFFFFFFFFFFFFll
#define LOGON_NAME_SAM_COMPATIBLE 2
#ifndef ERROR_NO_SUCH_LOGON_SESSION
#define ERROR_NO_SUCH_LOGON_SESSION 1312L
#endif
#ifndef ERROR_LOGON_FAILURE
#define ERROR_LOGON_FAILURE 1326L
#endif
#define LOGON_TOKEN_DUPLICATE 0x2
#define LOGON_TOKEN_QUERY 0x8
#define LOGON_TOKEN_ALL_ACCESS 0xF01FF
#define LOGON_TOKEN_PRIMARY 1
#define LOGON_TOKEN_IMPERSONATION 2
#define LOGON_SECURITY_IMPERSONATION 2

typedef struct LOGON_GROUP {
    BYTE *Sid;
    DWORD Attributes;
} LOGON_GROUP;

typedef struct LOGON_GROUPS {
    DWORD GroupCount;
    LOGON_GROUP Groups[1];
} LOGON_GROUPS;

NTSYSCALLAPI NTSTATUS NTAPI NtDuplicateToken(HANDLE token, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes,
                                             BOOLEAN effective_only, ULONG type, PHANDLE new_token);
NTSYSCALLAPI NTSTATUS NTAPI NtQueryInformationToken(HANDLE token, ULONG klass, PVOID info, ULONG length, PULONG returned);

typedef struct LOGON_QUOTA_LIMITS {
    SIZE_T PagedPoolLimit, NonPagedPoolLimit, MinimumWorkingSetSize, MaximumWorkingSetSize, PagefileLimit;
    LARGE_INTEGER TimeLimit;
} LOGON_QUOTA_LIMITS;

typedef struct LOGON_STRING {
    USHORT Length, MaximumLength;
    PWSTR Buffer;
} LOGON_STRING;

/* MSV1_0_INTERACTIVE_PROFILE, what the msv1_0 package gives for a logon that is not a network one. */
typedef struct LOGON_INTERACTIVE_PROFILE {
    ULONG MessageType;
    USHORT LogonCount, BadPasswordCount;
    LARGE_INTEGER LogonTime, LogoffTime, KickOffTime, PasswordLastSet, PasswordCanChange, PasswordMustChange;
    LOGON_STRING LogonScript, HomeDirectory, FullName, ProfilePath, HomeDirectoryDrive, LogonServer;
    ULONG UserFlags;
} LOGON_INTERACTIVE_PROFILE;

/* MSV1_0_LM20_LOGON_PROFILE, for a network logon. */
typedef struct LOGON_NETWORK_PROFILE {
    ULONG MessageType;
    LARGE_INTEGER KickOffTime, LogoffTime;
    ULONG UserFlags;
    UCHAR UserSessionKey[16];
    LOGON_STRING LogonDomainName;
    UCHAR LanmanSessionKey[8];
    LOGON_STRING LogonServer, UserParameters;
} LOGON_NETWORK_PROFILE;

typedef BOOLEAN(WINAPI *GET_USER_NAME_EX)(ULONG format, PWSTR name, PULONG chars);

static BOOL fail(DWORD error)
{
    RtlSetLastWin32Error(error);
    return FALSE;
}

/*
 * the machine has no account database and no passwords: the account a caller can log on as is its own, given as user or
 * user@machine with the machine, a dot or nothing as domain, with any password.
 */
static BOOL is_own_account(PCWSTR user, PCWSTR domain, PCWSTR machine)
{
    WCHAR own[300], local_user[260];
    ULONG chars = RTL_NUMBER_OF(own);
    HMODULE secur32 = LoadLibraryW(L"secur32.dll");
    GET_USER_NAME_EX get = secur32 ? (GET_USER_NAME_EX)GetProcAddress(secur32, "GetUserNameExW") : nullptr;
    BOOL known = get && get(LOGON_NAME_SAM_COMPATIBLE, own, &chars);
    if (secur32)
        FreeLibrary(secur32);
    if (!known)
        return FALSE;
    PCWSTR name = wcschr(own, L'\\');
    name = name ? name + 1 : own;
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

static NTSTATUS copy_token(BOOL impersonation, PHANDLE token)
{
    HANDLE process_token;
    NTSTATUS status = NtOpenProcessToken(NtCurrentProcess(), LOGON_TOKEN_DUPLICATE | LOGON_TOKEN_QUERY, &process_token);
    if (!NT_SUCCESS(status))
        return status;
    struct {
        DWORD Length;
        DWORD Level;
        BOOLEAN Tracking, Effective;
    } qos = {sizeof(qos), LOGON_SECURITY_IMPERSONATION, TRUE, FALSE};
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, nullptr, 0, nullptr, nullptr);
    oa.SecurityQualityOfService = &qos;
    status = NtDuplicateToken(process_token, LOGON_TOKEN_ALL_ACCESS, &oa, FALSE,
                              impersonation ? LOGON_TOKEN_IMPERSONATION : LOGON_TOKEN_PRIMARY,
                              token);
    NtClose(process_token);
    return status;
}

/* the logon sid of the token, which the caller frees with LocalFree. */
static PSID logon_sid(HANDLE token)
{
    ULONG size = 0;
    NtQueryInformationToken(token, LOGON_TOKEN_GROUPS, nullptr, 0, &size);
    LOGON_GROUPS *groups = size ? LocalAlloc(LMEM_FIXED, size) : nullptr;
    PSID sid = nullptr;
    if (groups && NT_SUCCESS(NtQueryInformationToken(token, LOGON_TOKEN_GROUPS, groups, size, &size))) {
        for (DWORD i = 0; i < groups->GroupCount; i++) {
            if ((groups->Groups[i].Attributes & LOGON_GROUP_LOGON_ID) != LOGON_GROUP_LOGON_ID)
                continue;
            const BYTE *found = groups->Groups[i].Sid;
            DWORD length = 8 + 4 * found[1];
            sid = LocalAlloc(LMEM_FIXED, length);
            if (sid)
                memcpy(sid, found, length);
            break;
        }
    }
    if (groups)
        LocalFree(groups);
    return sid;
}

static PWSTR put_text(BYTE *block, ULONG *at, LOGON_STRING *string, PCWSTR text)
{
    USHORT bytes = (USHORT)(wcslen(text) * sizeof(WCHAR));
    string->Length = bytes;
    string->MaximumLength = bytes + sizeof(WCHAR);
    string->Buffer = (PWSTR)(block + *at);
    memcpy(string->Buffer, text, bytes + sizeof(WCHAR));
    *at += bytes + sizeof(WCHAR);
    return string->Buffer;
}

/*
 * the profile of the logon, in memory of its own that LsaFreeReturnBuffer releases. the strings follow the structure, each
 * with its end, the ones that are empty too.
 */
static PVOID logon_profile(BOOL network, PCWSTR machine, DWORD *length)
{
    ULONG text = (ULONG)(wcslen(machine) + 1) * sizeof(WCHAR);
    ULONG head = network ? sizeof(LOGON_NETWORK_PROFILE) : sizeof(LOGON_INTERACTIVE_PROFILE);
    ULONG total = head + (network ? 2 * text + 2 : text + 10);
    PVOID base = nullptr;
    SIZE_T size = total;
    if (!NT_SUCCESS(NtAllocateVirtualMemory(NtCurrentProcess(), &base, 0, &size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)))
        return nullptr;
    ULONG at = head;
    if (network) {
        LOGON_NETWORK_PROFILE *p = base;
        p->MessageType = 3;
        p->KickOffTime.QuadPart = LOGON_NEVER;
        p->LogoffTime.QuadPart = LOGON_NEVER;
        p->UserFlags = 0x2120;
        put_text(base, &at, &p->LogonDomainName, machine);
        put_text(base, &at, &p->LogonServer, machine);
    } else {
        LOGON_INTERACTIVE_PROFILE *p = base;
        FILETIME now;
        GetSystemTimeAsFileTime(&now);
        p->MessageType = 2;
        p->LogonTime.LowPart = now.dwLowDateTime;
        p->LogonTime.HighPart = (LONG)now.dwHighDateTime;
        p->LogoffTime.QuadPart = LOGON_NEVER;
        p->KickOffTime.QuadPart = LOGON_NEVER;
        p->PasswordMustChange.QuadPart = LOGON_NEVER;
        put_text(base, &at, &p->LogonServer, machine);
        p->UserFlags = 0x120;
    }
    *length = total;
    return base;
}

/*
 * the checks come in the order of windows 11, measured with lk.c of scratch/g2/lsa of the research repository:
 * provider, type and name leave the outputs alone, the token is cleared before the virtual provider and the profile pair
 * are refused, the other outputs before the logon itself. new credentials never look at the account.
 */
BOOL WINAPI LogonUserExExW(PCWSTR user, PCWSTR domain, PCWSTR password, DWORD type, DWORD provider, PVOID groups,
                           PHANDLE token, PSID *sid, PVOID *profile, PDWORD profile_length, LOGON_QUOTA_LIMITS *quota)
{
    if (provider > LOGON32_PROVIDER_VIRTUAL)
        return fail(ERROR_INVALID_PARAMETER);
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
        return fail(ERROR_INVALID_PARAMETER);
    }
    if (!user || !*user)
        return fail(ERROR_INVALID_PARAMETER);
    if (token)
        *token = nullptr;
    if (provider == LOGON32_PROVIDER_VIRTUAL || (profile != nullptr) != (profile_length != nullptr))
        return fail(ERROR_INVALID_PARAMETER);
    if (sid)
        *sid = nullptr;
    if (profile) {
        *profile = nullptr;
        *profile_length = 0;
    }
    WCHAR machine[64];
    DWORD chars = RTL_NUMBER_OF(machine);
    if (!GetComputerNameW(machine, &chars))
        return FALSE;
    BOOL new_credentials = type == LOGON32_LOGON_NEW_CREDENTIALS;
    if (!new_credentials && !is_own_account(user, domain, machine))
        return fail(ERROR_LOGON_FAILURE);
    if (!token)
        return fail(ERROR_INVALID_PARAMETER);
    BOOL network = type == LOGON32_LOGON_NETWORK;
    HANDLE logon;
    NTSTATUS status = copy_token(network || type == LOGON32_LOGON_NETWORK_CLEARTEXT, &logon);
    if (!NT_SUCCESS(status))
        return fail(RtlNtStatusToDosError(status));
    PSID logon_id = sid ? logon_sid(logon) : nullptr;
    DWORD length = 0;
    PVOID block = profile && !new_credentials ? logon_profile(network, machine, &length) : nullptr;
    if ((sid && !logon_id) || (profile && !new_credentials && !block)) {
        NtClose(logon);
        if (logon_id)
            LocalFree(logon_id);
        return fail(sid && !logon_id ? ERROR_NO_SUCH_LOGON_SESSION : ERROR_NOT_ENOUGH_MEMORY);
    }
    if (sid)
        *sid = logon_id;
    if (profile) {
        *profile = block;
        *profile_length = length;
    }
    if (quota) {
        memset(quota, 0, sizeof(*quota));
        quota->PagedPoolLimit = 0x2000000;
        quota->NonPagedPoolLimit = 0x100000;
        quota->MinimumWorkingSetSize = 0x10000;
        quota->MaximumWorkingSetSize = 0xF000000;
    }
    *token = logon;
    return TRUE;
}
