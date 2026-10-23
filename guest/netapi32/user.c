/*
 * accounts and the machine's role. the only account is the logged on
 * user, a local administrator filtered by uac. the machine is a standalone
 * workgroup workstation, joined to no azure directory.
 */
#include <mime/nt.h>
#include <mime/ntrtl.h>
#include <mime/win32/winbase.h>

#define NETAPI DECLSPEC_EXPORT

typedef DWORD NET_API_STATUS;
typedef LPCWSTR LMCSTR;

#define NERR_Success 0
#define NERR_UserNotFound 2221
#define USER_PRIV_ADMIN 2
#define UF_SCRIPT 0x0001
#define UF_NORMAL_ACCOUNT 0x0200
#define UF_DONT_EXPIRE_PASSWD 0x10000
#define USER_MAXSTORAGE_UNLIMITED ((DWORD)-1)
#define TOKEN_QUERY 0x0008

NETAPI NET_API_STATUS WINAPI NetApiBufferAllocate(DWORD size, LPVOID *buffer);

typedef BOOL(WINAPI *LOOKUP_ACCOUNT_SID)(LPCWSTR, PSID, LPWSTR, LPDWORD, LPWSTR, LPDWORD, PDWORD);

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

/* reads the sid and account name from the process token. */
static BOOL current_user(BYTE *sid, DWORD sid_size, WCHAR *name, DWORD name_size)
{
    HANDLE token;
    if (!NT_SUCCESS(NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY, &token)))
        return FALSE;
    ULONG64 buffer[32];
    ULONG length;
    NTSTATUS status = NtQueryInformationToken(token, 1, buffer, sizeof(buffer), &length);
    NtClose(token);
    if (!NT_SUCCESS(status))
        return FALSE;
    const BYTE *s = (const BYTE *)buffer[0];
    DWORD n = 8 + 4 * s[1];
    if (n > sid_size)
        return FALSE;
    memcpy(sid, s, n);
    HMODULE advapi = LoadLibraryW(L"advapi32.dll");
    LOOKUP_ACCOUNT_SID lookup = advapi ? (LOOKUP_ACCOUNT_SID)GetProcAddress(advapi, "LookupAccountSidW") : nullptr;
    WCHAR domain[256];
    DWORD domain_size = RTL_NUMBER_OF(domain), use;
    BOOL ok = lookup && lookup(nullptr, (PSID)sid, name, &name_size, domain, &domain_size, &use);
    if (advapi)
        FreeLibrary(advapi);
    return ok;
}

typedef struct USER_INFO_1 {
    LPWSTR name, password;
    DWORD password_age, priv;
    LPWSTR home_dir, comment;
    DWORD flags;
    LPWSTR script_path;
} USER_INFO_1;

typedef struct USER_INFO_10 {
    LPWSTR name, comment, usr_comment, full_name;
} USER_INFO_10;

typedef struct USER_INFO_11 {
    LPWSTR name, comment, usr_comment, full_name;
    DWORD priv, auth_flags, password_age;
    LPWSTR home_dir, parms;
    DWORD last_logon, last_logoff, bad_pw_count, num_logons;
    LPWSTR logon_server;
    DWORD country_code;
    LPWSTR workstations;
    DWORD max_storage, units_per_week;
    PBYTE logon_hours;
    DWORD code_page;
} USER_INFO_11;

typedef struct USER_INFO_20 {
    LPWSTR name, full_name, comment;
    DWORD flags, user_id;
} USER_INFO_20;

typedef struct USER_INFO_23 {
    LPWSTR name, full_name, comment;
    DWORD flags;
    PSID sid;
} USER_INFO_23;

#define UNITS_PER_WEEK 168
#define USER_FLAGS (UF_SCRIPT | UF_NORMAL_ACCOUNT | UF_DONT_EXPIRE_PASSWD)

/* the logged on user is the only account; strings and the sid follow the fixed part. */
NETAPI NET_API_STATUS WINAPI NetUserGetInfo(LPCWSTR server, LPCWSTR user, DWORD level, LPBYTE *buffer)
{
    if (!buffer || !user)
        return ERROR_INVALID_PARAMETER;
    if (!local_server(server))
        return ERROR_BAD_NETPATH;
    SIZE_T fixed;
    switch (level) {
    case 0:
        fixed = sizeof(LPWSTR);
        break;
    case 1:
        fixed = sizeof(USER_INFO_1);
        break;
    case 10:
        fixed = sizeof(USER_INFO_10);
        break;
    case 11:
        fixed = sizeof(USER_INFO_11);
        break;
    case 20:
        fixed = sizeof(USER_INFO_20);
        break;
    case 23:
        fixed = sizeof(USER_INFO_23);
        break;
    default:
        return ERROR_INVALID_LEVEL;
    }
    BYTE sid[68];
    WCHAR name[256];
    if (!current_user(sid, sizeof(sid), name, RTL_NUMBER_OF(name)) || _wcsicmp(user, name))
        return NERR_UserNotFound;
    SIZE_T name_bytes = (wcslen(name) + 1) * sizeof(WCHAR), sid_bytes = 8 + 4 * sid[1];
    fixed = (fixed + 7) & ~(SIZE_T)7;
    SIZE_T size = fixed + name_bytes + sizeof(WCHAR) + sid_bytes + 8 + UNITS_PER_WEEK / 8;
    BYTE *out;
    NET_API_STATUS status = NetApiBufferAllocate((DWORD)size, (LPVOID *)&out);
    if (status)
        return status;
    memset(out, 0, size);
    LPWSTR text = (LPWSTR)(out + fixed), empty = (LPWSTR)((BYTE *)text + name_bytes);
    memcpy(text, name, name_bytes);
    PSID copy = (PSID)(((ULONG_PTR)(empty + 1) + 7) & ~(ULONG_PTR)7);
    memcpy(copy, sid, sid_bytes);
    PBYTE hours = (PBYTE)copy + sid_bytes;
    memset(hours, 0xFF, UNITS_PER_WEEK / 8);
    switch (level) {
    case 0:
        *(LPWSTR *)out = text;
        break;
    case 1:
        *(USER_INFO_1 *)out = (USER_INFO_1){text, nullptr, 0, USER_PRIV_ADMIN, empty, empty, USER_FLAGS, empty};
        break;
    case 10:
        *(USER_INFO_10 *)out = (USER_INFO_10){text, empty, empty, empty};
        break;
    case 11:
        *(USER_INFO_11 *)out = (USER_INFO_11){
            .name = text, .comment = empty, .usr_comment = empty, .full_name = empty, .priv = USER_PRIV_ADMIN,
            .home_dir = empty, .parms = empty, .logon_server = (LPWSTR)L"\\\\*", .workstations = empty,
            .max_storage = USER_MAXSTORAGE_UNLIMITED, .units_per_week = UNITS_PER_WEEK, .logon_hours = hours};
        break;
    case 20:
        *(USER_INFO_20 *)out = (USER_INFO_20){text, empty, empty, USER_FLAGS, ((DWORD *)(sid + 8))[sid[1] - 1]};
        break;
    case 23:
        *(USER_INFO_23 *)out = (USER_INFO_23){text, empty, empty, USER_FLAGS, copy};
        break;
    }
    *buffer = out;
    return NERR_Success;
}

typedef enum {
    DsRole_RoleStandaloneWorkstation,
} DSROLE_MACHINE_ROLE;

typedef struct DSROLE_PRIMARY_DOMAIN_INFO_BASIC {
    DSROLE_MACHINE_ROLE MachineRole;
    ULONG Flags;
    LPWSTR DomainNameFlat;
    LPWSTR DomainNameDns;
    LPWSTR DomainForestName;
    GUID DomainGuid;
} DSROLE_PRIMARY_DOMAIN_INFO_BASIC;

#define DsRolePrimaryDomainInfoBasic 1
#define DsRoleUpgradeStatus 2
#define DsRoleOperationState 3

NETAPI DWORD WINAPI DsRoleGetPrimaryDomainInformation(LPCWSTR server, DWORD level, PBYTE *buffer)
{
    if (!buffer)
        return ERROR_INVALID_PARAMETER;
    if (!local_server(server))
        return ERROR_BAD_NETPATH;
    static const WCHAR workgroup[] = L"WORKGROUP";
    SIZE_T size;
    switch (level) {
    case DsRolePrimaryDomainInfoBasic:
        size = sizeof(DSROLE_PRIMARY_DOMAIN_INFO_BASIC) + sizeof(workgroup);
        break;
    case DsRoleUpgradeStatus:
        size = 2 * sizeof(DWORD);
        break;
    case DsRoleOperationState:
        size = sizeof(DWORD);
        break;
    default:
        return ERROR_INVALID_PARAMETER;
    }
    BYTE *out = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, size);
    if (!out)
        return ERROR_NOT_ENOUGH_MEMORY;
    if (level == DsRolePrimaryDomainInfoBasic) {
        DSROLE_PRIMARY_DOMAIN_INFO_BASIC *basic = (DSROLE_PRIMARY_DOMAIN_INFO_BASIC *)out;
        basic->MachineRole = DsRole_RoleStandaloneWorkstation;
        basic->DomainNameFlat = (LPWSTR)(basic + 1);
        memcpy(basic->DomainNameFlat, workgroup, sizeof(workgroup));
    }
    *buffer = out;
    return ERROR_SUCCESS;
}

NETAPI void WINAPI DsRoleFreeMemory(PVOID buffer)
{
    if (buffer)
        LocalFree(buffer);
}

/* a machine joined to no directory gets success and no information. */
NETAPI HRESULT WINAPI NetGetAadJoinInformation(LPCWSTR tenant, PVOID *info)
{
    if (!info)
        return E_INVALIDARG;
    *info = nullptr;
    return S_OK;
}

NETAPI void WINAPI NetFreeAadJoinInformation(PVOID info)
{
    if (info)
        LocalFree(info);
}
