/*
 * lan manager functions for a standalone machine.
 *
 * mime has no network stack, so there is no server service sharing folders
 * and no remote machine to reach. the machine is in the default workgroup,
 * as Windows setup leaves it.
 */
#include <mime/nt.h>
#include <mime/ntrtl.h>
#include <mime/win32/winbase.h>

#define NETAPI DECLSPEC_EXPORT

typedef DWORD NET_API_STATUS;
typedef WCHAR *LMSTR;
typedef const WCHAR *LMCSTR;

#define NERR_Success 0
#define NERR_ServerNotStarted 2114
#define MAX_PREFERRED_LENGTH ((DWORD)-1)

typedef enum _NETSETUP_JOIN_STATUS {
    NetSetupUnknownStatus,
    NetSetupUnjoined,
    NetSetupWorkgroupName,
    NetSetupDomainName,
} NETSETUP_JOIN_STATUS, *PNETSETUP_JOIN_STATUS;

NETAPI NET_API_STATUS WINAPI NetApiBufferAllocate(DWORD size, LPVOID *buffer)
{
    if (!buffer)
        return ERROR_INVALID_PARAMETER;
    *buffer = LocalAlloc(LMEM_FIXED, size);
    return *buffer ? NERR_Success : ERROR_NOT_ENOUGH_MEMORY;
}

NETAPI NET_API_STATUS WINAPI NetApiBufferFree(LPVOID buffer)
{
    if (buffer)
        LocalFree(buffer);
    return NERR_Success;
}

NETAPI NET_API_STATUS WINAPI NetApiBufferReallocate(LPVOID old, DWORD size, LPVOID *buffer)
{
    if (!buffer)
        return ERROR_INVALID_PARAMETER;
    if (!size) {
        NetApiBufferFree(old);
        *buffer = nullptr;
        return NERR_Success;
    }
    LPVOID p = old ? LocalReAlloc(old, size, LMEM_MOVEABLE) : LocalAlloc(LMEM_FIXED, size);
    if (!p)
        return ERROR_NOT_ENOUGH_MEMORY;
    *buffer = p;
    return NERR_Success;
}

NETAPI NET_API_STATUS WINAPI NetApiBufferSize(LPVOID buffer, LPDWORD size)
{
    if (!buffer || !size)
        return ERROR_INVALID_PARAMETER;
    *size = (DWORD)LocalSize(buffer);
    return NERR_Success;
}

static BOOL local_server(LMCSTR server)
{
    if (!server || !*server)
        return TRUE;
    while (*server == L'\\')
        server++;
    WCHAR computer[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD n = RTL_NUMBER_OF(computer);
    return !_wcsicmp(server, L".") || !_wcsicmp(server, L"localhost") ||
           (GetComputerNameW(computer, &n) && !_wcsicmp(server, computer));
}

/* the workstation calls live in wkscli.dll, which netapi32 loads at the first of them, as Windows does. */
static HMODULE g_wkscli;

static FARPROC wkscli_function(const char *name, DWORD *error)
{
    *error = NERR_Success;
    HMODULE module = g_wkscli;
    if (!module) {
        module = LoadLibraryExW(L"wkscli.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) {
            *error = GetLastError();
            return nullptr;
        }
        if (InterlockedCompareExchangePointer((PVOID volatile *)&g_wkscli, module, nullptr))
            FreeLibrary(module);
        module = g_wkscli;
    }
    FARPROC function = GetProcAddress(module, name);
    if (!function)
        *error = GetLastError();
    return function;
}

NETAPI NET_API_STATUS WINAPI NetGetJoinInformation(LPCWSTR server, LPWSTR *name, PNETSETUP_JOIN_STATUS type)
{
    DWORD error;
    NET_API_STATUS(WINAPI * function)(LPCWSTR, LPWSTR *, PNETSETUP_JOIN_STATUS) =
        (void *)wkscli_function("NetGetJoinInformation", &error);
    return function ? function(server, name, type) : error;
}

NETAPI NET_API_STATUS WINAPI NetWkstaGetInfo(LMSTR server, DWORD level, LPBYTE *buffer)
{
    DWORD error;
    NET_API_STATUS(WINAPI * function)(LMSTR, DWORD, LPBYTE *) = (void *)wkscli_function("NetWkstaGetInfo", &error);
    return function ? function(server, level, buffer) : error;
}

NETAPI NET_API_STATUS WINAPI NetWkstaUserGetInfo(LMSTR reserved, DWORD level, LPBYTE *buffer)
{
    DWORD error;
    NET_API_STATUS(WINAPI * function)(LMSTR, DWORD, LPBYTE *) = (void *)wkscli_function("NetWkstaUserGetInfo", &error);
    return function ? function(reserved, level, buffer) : error;
}

/* the server service is not running, and other machines cannot be reached. */
NETAPI NET_API_STATUS WINAPI NetShareEnum(LMSTR server, DWORD level, LPBYTE *buffer, DWORD max_length,
                                          LPDWORD read, LPDWORD total, LPDWORD resume)
{
    if (level != 0 && level != 1 && level != 2 && level != 502 && level != 503)
        return ERROR_INVALID_LEVEL;
    if (!buffer || !read || !total)
        return ERROR_INVALID_PARAMETER;
    *buffer = nullptr;
    *read = 0;
    *total = 0;
    return local_server(server) ? NERR_ServerNotStarted : ERROR_BAD_NETPATH;
}

/* the server service answers for the machine, and it is not running. */
NETAPI NET_API_STATUS WINAPI NetServerGetInfo(LMSTR server, DWORD level, LPBYTE *buffer)
{
    switch (level) {
    case 100:
    case 101:
    case 102:
    case 402:
    case 403:
    case 502:
    case 503:
    case 599:
    case 1005:
    case 1010:
    case 1016:
    case 1017:
    case 1018:
    case 1107:
    case 1501:
    case 1502:
    case 1503:
    case 1506:
    case 1509:
    case 1510:
    case 1511:
    case 1512:
    case 1513:
    case 1514:
    case 1515:
    case 1516:
    case 1518:
    case 1523:
    case 1528:
    case 1529:
    case 1530:
    case 1533:
    case 1534:
    case 1535:
    case 1536:
    case 1538:
    case 1539:
    case 1540:
    case 1541:
    case 1542:
    case 1543:
    case 1544:
    case 1545:
    case 1546:
    case 1547:
    case 1548:
    case 1549:
    case 1550:
    case 1552:
    case 1553:
    case 1554:
    case 1555:
    case 1556:
        break;
    default:
        return ERROR_INVALID_LEVEL;
    }
    if (!buffer)
        return ERROR_INVALID_PARAMETER;
    *buffer = nullptr;
    return local_server(server) ? NERR_ServerNotStarted : ERROR_BAD_NETPATH;
}

NETAPI NET_API_STATUS WINAPI NetShareAdd(LMSTR server, DWORD level, LPBYTE buffer, LPDWORD parameter)
{
    if (parameter)
        *parameter = 0;
    if (level != 2 && level != 502 && level != 503)
        return ERROR_INVALID_LEVEL;
    if (!buffer)
        return ERROR_INVALID_PARAMETER;
    return local_server(server) ? NERR_ServerNotStarted : ERROR_BAD_NETPATH;
}

/*
 * the server and workstation services that these calls belong to are not running. the answers are the ones of a
 * machine that has no connection, session, open remote file or share, and of a computer that is not the primary
 * domain controller of a domain, which the groups belong to.
 */
#define NERR_UseNotFound 2250
#define NERR_DeviceNotShared 2311
#define NERR_ClientNameNotFound 2312
#define NERR_FileIdNotFound 2314
#define NERR_NotPrimary 2226

NETAPI NET_API_STATUS WINAPI NetUseEnum(LMSTR server, DWORD level, LPBYTE *buffer, DWORD max_length, LPDWORD read,
                                        LPDWORD total, LPDWORD resume)
{
    if (level > 2)
        return ERROR_INVALID_LEVEL;
    if (!buffer || !read || !total)
        return ERROR_INVALID_PARAMETER;
    *buffer = nullptr;
    *read = 0;
    *total = 0;
    return local_server(server) ? NERR_Success : ERROR_BAD_NETPATH;
}

NETAPI NET_API_STATUS WINAPI NetUseGetInfo(LMSTR server, LMSTR name, DWORD level, LPBYTE *buffer)
{
    if (level > 2)
        return ERROR_INVALID_LEVEL;
    if (!name || !buffer)
        return ERROR_INVALID_PARAMETER;
    *buffer = nullptr;
    return local_server(server) ? NERR_UseNotFound : ERROR_BAD_NETPATH;
}

NETAPI NET_API_STATUS WINAPI NetShareCheck(LMSTR server, LMSTR device, LPDWORD type)
{
    if (!device || !type)
        return ERROR_INVALID_PARAMETER;
    *type = 0;
    return local_server(server) ? NERR_ServerNotStarted : ERROR_BAD_NETPATH;
}

NETAPI NET_API_STATUS WINAPI NetSessionEnum(LMSTR server, LMSTR client, LMSTR user, DWORD level, LPBYTE *buffer,
                                            DWORD max_length, LPDWORD read, LPDWORD total, LPDWORD resume)
{
    if (level != 0 && level != 1 && level != 2 && level != 10 && level != 502)
        return ERROR_INVALID_LEVEL;
    if (!buffer || !read || !total)
        return ERROR_INVALID_PARAMETER;
    *buffer = nullptr;
    *read = 0;
    *total = 0;
    return local_server(server) ? NERR_ServerNotStarted : ERROR_BAD_NETPATH;
}

NETAPI NET_API_STATUS WINAPI NetSessionGetInfo(LMSTR server, LMSTR client, LMSTR user, DWORD level, LPBYTE *buffer)
{
    if (level != 0 && level != 1 && level != 2 && level != 10 && level != 502)
        return ERROR_INVALID_LEVEL;
    if (!client || !user || !buffer)
        return ERROR_INVALID_PARAMETER;
    *buffer = nullptr;
    return local_server(server) ? NERR_ServerNotStarted : ERROR_BAD_NETPATH;
}

NETAPI NET_API_STATUS WINAPI NetSessionDel(LMSTR server, LMSTR client, LMSTR user)
{
    return local_server(server) ? NERR_ServerNotStarted : ERROR_BAD_NETPATH;
}

NETAPI NET_API_STATUS WINAPI NetFileEnum(LMSTR server, LMSTR base, LMSTR user, DWORD level, LPBYTE *buffer,
                                         DWORD max_length, LPDWORD read, LPDWORD total, ULONG_PTR *resume)
{
    if (level != 2 && level != 3)
        return ERROR_INVALID_LEVEL;
    if (!buffer || !read || !total)
        return ERROR_INVALID_PARAMETER;
    *buffer = nullptr;
    *read = 0;
    *total = 0;
    return local_server(server) ? NERR_ServerNotStarted : ERROR_BAD_NETPATH;
}

NETAPI NET_API_STATUS WINAPI NetFileGetInfo(LMSTR server, DWORD id, DWORD level, LPBYTE *buffer)
{
    if (level != 2 && level != 3)
        return ERROR_INVALID_LEVEL;
    if (!buffer)
        return ERROR_INVALID_PARAMETER;
    *buffer = nullptr;
    return local_server(server) ? NERR_ServerNotStarted : ERROR_BAD_NETPATH;
}

NETAPI NET_API_STATUS WINAPI NetFileClose(LMSTR server, DWORD id)
{
    return local_server(server) ? NERR_ServerNotStarted : ERROR_BAD_NETPATH;
}

NETAPI NET_API_STATUS WINAPI NetConnectionEnum(LMSTR server, LMSTR qualifier, DWORD level, LPBYTE *buffer,
                                               DWORD max_length, LPDWORD read, LPDWORD total, LPDWORD resume)
{
    if (level != 0 && level != 1)
        return ERROR_INVALID_LEVEL;
    if (!qualifier || !buffer || !read || !total)
        return ERROR_INVALID_PARAMETER;
    *buffer = nullptr;
    *read = 0;
    *total = 0;
    return local_server(server) ? NERR_ServerNotStarted : ERROR_BAD_NETPATH;
}

NETAPI NET_API_STATUS WINAPI NetGroupEnum(LPCWSTR server, DWORD level, LPBYTE *buffer, DWORD max_length, LPDWORD read,
                                          LPDWORD total, ULONG_PTR *resume)
{
    if (level != 0 && level != 1 && level != 2 && level != 3)
        return ERROR_INVALID_LEVEL;
    if (!buffer || !read || !total)
        return ERROR_INVALID_PARAMETER;
    *buffer = nullptr;
    *read = 0;
    *total = 0;
    return local_server(server) ? NERR_NotPrimary : ERROR_BAD_NETPATH;
}

NETAPI NET_API_STATUS WINAPI NetGroupGetInfo(LPCWSTR server, LPCWSTR group, DWORD level, LPBYTE *buffer)
{
    if (level != 0 && level != 1 && level != 2 && level != 3)
        return ERROR_INVALID_LEVEL;
    if (!group || !buffer)
        return ERROR_INVALID_PARAMETER;
    *buffer = nullptr;
    return local_server(server) ? NERR_NotPrimary : ERROR_BAD_NETPATH;
}

NETAPI NET_API_STATUS WINAPI NetGroupAdd(LPCWSTR server, DWORD level, LPBYTE buffer, LPDWORD parameter)
{
    if (parameter)
        *parameter = 0;
    if (level != 0 && level != 1 && level != 2)
        return ERROR_INVALID_LEVEL;
    if (!buffer)
        return ERROR_INVALID_PARAMETER;
    return local_server(server) ? NERR_NotPrimary : ERROR_BAD_NETPATH;
}

NETAPI NET_API_STATUS WINAPI NetGroupDel(LPCWSTR server, LPCWSTR group)
{
    if (!group)
        return ERROR_INVALID_PARAMETER;
    return local_server(server) ? NERR_NotPrimary : ERROR_BAD_NETPATH;
}

/*
 * the validation of a password against the account policy of the machine, which windows asks the security accounts
 * manager for. a standalone machine's policy is the default of a client: no minimum length, no complexity rule, no
 * history, no lockout, and passwords last 42 days.
 */
#define NERR_PasswordExpired 2242
#define NERR_PasswordHistConflict 2244
#define NERR_PasswordTooShort 2245
#define NERR_PasswordTooRecent 2246
#define NERR_PasswordTooLong 2202
#define NERR_PasswordNotComplexEnough 2203
#define NERR_PasswordMustChange 2701
#define NERR_AccountLockedOut 2702

#define NET_VALIDATE_PASSWORD_LAST_SET 0x1
#define NET_VALIDATE_BAD_PASSWORD_TIME 0x2
#define NET_VALIDATE_LOCKOUT_TIME 0x4
#define NET_VALIDATE_BAD_PASSWORD_COUNT 0x8
#define NET_VALIDATE_PASSWORD_HISTORY_LENGTH 0x10
#define NET_VALIDATE_PASSWORD_HISTORY 0x20

#define POLICY_MIN_LENGTH 0
#define POLICY_COMPLEX FALSE
#define POLICY_MAX_AGE_100NS (42LL * 24 * 3600 * 10000000LL)
#define POLICY_MIN_AGE_100NS 0LL
#define POLICY_LOCKOUT_THRESHOLD 0
#define PASSWORD_LENGTH_LIMIT 256

typedef struct PW_HASH {
    ULONG Length;
    LPBYTE Hash;
} PW_HASH;

typedef struct PW_PERSISTED {
    ULONG PresentFields;
    FILETIME PasswordLastSet, BadPasswordTime, LockoutTime;
    ULONG BadPasswordCount;
    ULONG PasswordHistoryLength;
    PW_HASH *PasswordHistory;
} PW_PERSISTED;

typedef struct PW_AUTH {
    PW_PERSISTED Persisted;
    BOOLEAN PasswordMatched;
} PW_AUTH;

typedef struct PW_CHANGE {
    PW_PERSISTED Persisted;
    LPWSTR ClearPassword;
    LPWSTR UserAccountName;
    PW_HASH HashedPassword;
    BOOLEAN PasswordMatch;
} PW_CHANGE;

typedef struct PW_RESET {
    PW_PERSISTED Persisted;
    LPWSTR ClearPassword;
    LPWSTR UserAccountName;
    PW_HASH HashedPassword;
    BOOLEAN PasswordMustChangeAtNextLogon;
    BOOLEAN ClearLockout;
} PW_RESET;

typedef struct PW_OUTPUT {
    PW_PERSISTED Persisted;
    NET_API_STATUS ValidationStatus;
} PW_OUTPUT;

static LONGLONG file_time_value(FILETIME t)
{
    return (LONGLONG)t.dwHighDateTime << 32 | t.dwLowDateTime;
}

/* the classes of characters a complex password draws on: upper, lower, digit and the rest. */
static int character_classes(const WCHAR *p)
{
    int upper = 0, lower = 0, digit = 0, other = 0;
    for (; *p; p++) {
        if (*p >= 'A' && *p <= 'Z')
            upper = 1;
        else if (*p >= 'a' && *p <= 'z')
            lower = 1;
        else if (*p >= '0' && *p <= '9')
            digit = 1;
        else
            other = 1;
    }
    return upper + lower + digit + other;
}

static BOOL contains_name(const WCHAR *password, const WCHAR *name)
{
    if (!name || wcslen(name) < 3)
        return FALSE;
    SIZE_T n = wcslen(name);
    for (const WCHAR *p = password; *p; p++)
        if (!_wcsnicmp(p, name, n))
            return TRUE;
    return FALSE;
}

static NET_API_STATUS check_new_password(const WCHAR *password, const WCHAR *account)
{
    SIZE_T length = password ? wcslen(password) : 0;
    if (length > PASSWORD_LENGTH_LIMIT)
        return NERR_PasswordTooLong;
    if (length < POLICY_MIN_LENGTH)
        return NERR_PasswordTooShort;
    if (POLICY_COMPLEX && (length < 6 || character_classes(password) < 3 || contains_name(password, account)))
        return NERR_PasswordNotComplexEnough;
    return NERR_Success;
}

NETAPI NET_API_STATUS WINAPI NetValidatePasswordPolicy(LPCWSTR server, LPVOID qualifier, int type, LPVOID input, LPVOID *output)
{
    if (!output)
        return ERROR_INVALID_PARAMETER;
    *output = nullptr;
    if (qualifier || type < 1 || type > 3 || !input)
        return ERROR_INVALID_PARAMETER;
    if (!local_server(server))
        return ERROR_BAD_NETPATH;
    PW_OUTPUT *out = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, sizeof(PW_OUTPUT));
    if (!out)
        return ERROR_NOT_ENOUGH_MEMORY;
    PW_PERSISTED *fields = (PW_PERSISTED *)input;
    out->Persisted = *fields;
    out->Persisted.PasswordHistory = nullptr;
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    ULONG present = fields->PresentFields;
    NET_API_STATUS status = NERR_Success;
    /* an account is locked out until the lockout time passes, and a password that is too old must change. */
    BOOL locked = (present & NET_VALIDATE_LOCKOUT_TIME) && file_time_value(fields->LockoutTime) && POLICY_LOCKOUT_THRESHOLD &&
                  fields->BadPasswordCount >= POLICY_LOCKOUT_THRESHOLD;
    BOOL expired = (present & NET_VALIDATE_PASSWORD_LAST_SET) && file_time_value(fields->PasswordLastSet) &&
                   file_time_value(now) - file_time_value(fields->PasswordLastSet) > POLICY_MAX_AGE_100NS;
    if (type == 1) {
        PW_AUTH *auth = (PW_AUTH *)input;
        if (locked) {
            status = NERR_AccountLockedOut;
        } else if (!auth->PasswordMatched) {
            out->Persisted.BadPasswordCount = fields->BadPasswordCount + 1;
            out->Persisted.BadPasswordTime = now;
            out->Persisted.PresentFields |= NET_VALIDATE_BAD_PASSWORD_TIME | NET_VALIDATE_BAD_PASSWORD_COUNT;
            if (POLICY_LOCKOUT_THRESHOLD && out->Persisted.BadPasswordCount >= POLICY_LOCKOUT_THRESHOLD) {
                out->Persisted.LockoutTime = now;
                out->Persisted.PresentFields |= NET_VALIDATE_LOCKOUT_TIME;
            }
        } else if (expired) {
            status = NERR_PasswordMustChange;
        } else {
            out->Persisted.BadPasswordCount = 0;
            out->Persisted.PresentFields |= NET_VALIDATE_BAD_PASSWORD_COUNT;
        }
    } else if (type == 2) {
        PW_CHANGE *change = (PW_CHANGE *)input;
        if (locked) {
            status = NERR_AccountLockedOut;
        } else if (!change->PasswordMatch) {
            out->Persisted.BadPasswordCount = fields->BadPasswordCount + 1;
            out->Persisted.BadPasswordTime = now;
            out->Persisted.PresentFields |= NET_VALIDATE_BAD_PASSWORD_TIME | NET_VALIDATE_BAD_PASSWORD_COUNT;
            status = ERROR_INVALID_PASSWORD;
        } else if ((present & NET_VALIDATE_PASSWORD_LAST_SET) && file_time_value(fields->PasswordLastSet) &&
                   file_time_value(now) - file_time_value(fields->PasswordLastSet) < POLICY_MIN_AGE_100NS) {
            status = NERR_PasswordTooRecent;
        } else {
            status = check_new_password(change->ClearPassword, change->UserAccountName);
        }
        if (status == NERR_Success) {
            out->Persisted.PasswordLastSet = now;
            out->Persisted.BadPasswordCount = 0;
            out->Persisted.PresentFields |= NET_VALIDATE_PASSWORD_LAST_SET | NET_VALIDATE_BAD_PASSWORD_COUNT;
        }
    } else {
        PW_RESET *reset = (PW_RESET *)input;
        status = check_new_password(reset->ClearPassword, reset->UserAccountName);
        if (status == NERR_Success) {
            out->Persisted.PasswordLastSet = now;
            out->Persisted.PresentFields |= NET_VALIDATE_PASSWORD_LAST_SET;
            if (reset->ClearLockout) {
                memset(&out->Persisted.LockoutTime, 0, sizeof(FILETIME));
                out->Persisted.BadPasswordCount = 0;
                out->Persisted.PresentFields |= NET_VALIDATE_LOCKOUT_TIME | NET_VALIDATE_BAD_PASSWORD_COUNT;
            }
            if (reset->PasswordMustChangeAtNextLogon)
                memset(&out->Persisted.PasswordLastSet, 0, sizeof(FILETIME));
        }
    }
    out->ValidationStatus = status;
    *output = out;
    return NERR_Success;
}

NETAPI NET_API_STATUS WINAPI NetValidatePasswordPolicyFree(LPVOID *output)
{
    if (!output)
        return ERROR_INVALID_PARAMETER;
    if (*output && LocalFree(*output))
        return ERROR_INVALID_PARAMETER;
    *output = nullptr;
    return NERR_Success;
}
