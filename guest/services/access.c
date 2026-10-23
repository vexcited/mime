/*
 * services.exe access checks. the manager and every service have a security descriptor, checked against the token the
 * client connected with, obtained by impersonating the named pipe.
 */
#include "services.h"
#include <crypto/crypto.h>

PSECURITY_DESCRIPTOR g_manager_sd;
/* windows' generic mappings for the manager and for services. */
GENERIC_MAPPING g_manager_mapping = {0x20014, 0x20022, 0x20009, 0xF003F};
GENERIC_MAPPING g_service_mapping = {0x2008D, 0x20002, 0x20170, 0xF01FF};
static PSECURITY_DESCRIPTOR g_default_service_sd;

/*
 * windows' default descriptors. the manager lets everyone connect, interactive users and services list and read, the
 * system also change the boot configuration, and administrators do everything. a service lets the system and
 * administrators do what they need, and interactive users and services query, interrogate and send user controls.
 */
static const WCHAR g_manager_sddl[] =
    L"O:SYG:SYD:(A;;CC;;;AU)(A;;CCLCRPRC;;;IU)(A;;CCLCRPRC;;;SU)(A;;CCLCRPWPRC;;;SY)(A;;KA;;;BA)(A;;CC;;;AC)";
static const WCHAR g_service_sddl[] = L"O:SYG:SYD:(A;;CCLCSWRPWPDTLOCRRC;;;SY)(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;BA)"
                                      L"(A;;CCLCSWLOCRRC;;;IU)(A;;CCLCSWLOCRRC;;;SU)";

void AccessEnablePrivilege(const WCHAR *name)
{
    HANDLE token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        return;
    TOKEN_PRIVILEGES p;
    p.PrivilegeCount = 1;
    p.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (LookupPrivilegeValueW(nullptr, name, &p.Privileges[0].Luid))
        AdjustTokenPrivileges(token, FALSE, &p, sizeof(p), nullptr, nullptr);
    CloseHandle(token);
}

static PSECURITY_DESCRIPTOR from_registry(HKEY key)
{
    DWORD type = 0, size = 0;
    if (RegQueryValueExW(key, L"Security", nullptr, &type, nullptr, &size) != ERROR_SUCCESS || type != REG_BINARY || !size)
        return nullptr;
    PSECURITY_DESCRIPTOR sd = LocalAlloc(0, size);
    if (!sd)
        return nullptr;
    if (RegQueryValueExW(key, L"Security", nullptr, &type, (BYTE *)sd, &size) != ERROR_SUCCESS ||
        !IsValidSecurityDescriptor(sd)) {
        LocalFree(sd);
        return nullptr;
    }
    return sd;
}

void AccessInit(void)
{
    /* windows keeps the manager's descriptor below ServiceGroupOrder. */
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\ServiceGroupOrder\\Security", 0, KEY_READ,
                      &key) == ERROR_SUCCESS) {
        g_manager_sd = from_registry(key);
        RegCloseKey(key);
    }
    if (!g_manager_sd && !ConvertStringSecurityDescriptorToSecurityDescriptorW(g_manager_sddl, SDDL_REVISION_1,
                                                                                &g_manager_sd, nullptr))
        SvcLog("the descriptor of the manager: %lu", GetLastError());
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(g_service_sddl, SDDL_REVISION_1, &g_default_service_sd,
                                                              nullptr))
        SvcLog("the descriptor of a service: %lu", GetLastError());
}

PSECURITY_DESCRIPTOR AccessDefaultServiceSd(void)
{
    return g_default_service_sd;
}

/* read from the service's key on first use. */
DWORD AccessReadSd(SERVICE *s)
{
    if (s->Sd)
        return ERROR_SUCCESS;
    WCHAR path[SVC_NAME_MAX + 16];
    _snwprintf(path, RTL_NUMBER_OF(path) - 1, L"%ls\\Security", s->Name);
    path[RTL_NUMBER_OF(path) - 1] = 0;
    HKEY key = DbOpenKey(path, KEY_READ, FALSE);
    if (key) {
        s->Sd = from_registry(key);
        RegCloseKey(key);
    }
    if (!s->Sd) {
        DWORD length = GetSecurityDescriptorLength(g_default_service_sd);
        s->Sd = LocalAlloc(0, length);
        if (!s->Sd)
            return ERROR_NOT_ENOUGH_MEMORY;
        memcpy(s->Sd, g_default_service_sd, length);
    }
    return ERROR_SUCCESS;
}

DWORD AccessWriteSd(SERVICE *s)
{
    WCHAR path[SVC_NAME_MAX + 16];
    _snwprintf(path, RTL_NUMBER_OF(path) - 1, L"%ls\\Security", s->Name);
    path[RTL_NUMBER_OF(path) - 1] = 0;
    HKEY key = DbOpenKey(path, KEY_ALL_ACCESS, TRUE);
    if (!key)
        return GetLastError();
    DWORD error = (DWORD)RegSetValueExW(key, L"Security", 0, REG_BINARY, (const BYTE *)s->Sd,
                                        GetSecurityDescriptorLength(s->Sd));
    RegCloseKey(key);
    return error;
}

/* what the client is granted of the rights it asks for; ERROR_ACCESS_DENIED when it gets none. */
DWORD AccessCheckClient(HANDLE pipe, PSECURITY_DESCRIPTOR sd, DWORD desired, const GENERIC_MAPPING *mapping,
                        DWORD *granted)
{
    if (!sd)
        return ERROR_INVALID_SECURITY_DESCR_;
    if (!ImpersonateNamedPipeClient(pipe))
        return GetLastError();
    HANDLE token;
    BOOL ok = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token);
    DWORD error = ok ? 0 : GetLastError();
    RevertToSelf();
    if (!ok)
        return error;
    GENERIC_MAPPING map = *mapping;
    DWORD mask = desired;
    MapGenericMask(&mask, &map);
    PRIVILEGE_SET privileges;
    DWORD length = sizeof(privileges), got = 0;
    BOOL allowed = FALSE;
    ok = AccessCheck(sd, token, mask, &map, &privileges, &length, &got, &allowed);
    error = ok ? (allowed ? 0 : ERROR_ACCESS_DENIED) : GetLastError();
    CloseHandle(token);
    if (!error)
        *granted = got;
    return error;
}

/* DOMAIN\name of the client, for the lock owner. */
DWORD AccessClientName(HANDLE pipe, WCHAR *name, DWORD chars)
{
    if (!ImpersonateNamedPipeClient(pipe))
        return GetLastError();
    HANDLE token;
    BOOL ok = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token);
    DWORD error = ok ? 0 : GetLastError();
    RevertToSelf();
    if (!ok)
        return error;
    BYTE buffer[256];
    DWORD returned;
    if (GetTokenInformation(token, TokenUser, buffer, sizeof(buffer), &returned)) {
        WCHAR user[256], domain[256];
        DWORD user_size = RTL_NUMBER_OF(user), domain_size = RTL_NUMBER_OF(domain), use;
        if (LookupAccountSidW(nullptr, ((TOKEN_USER_ *)buffer)->Sid, user, &user_size, domain, &domain_size, &use))
            _snwprintf(name, chars - 1, L"%ls\\%ls", domain, user);
        else
            error = GetLastError();
    } else {
        error = GetLastError();
    }
    name[chars - 1] = 0;
    CloseHandle(token);
    return error;
}

/* the security information must only name the four parts that exist. */
DWORD AccessQuerySd(PSECURITY_DESCRIPTOR sd, DWORD information, void *out, DWORD size, DWORD *needed)
{
    BOOL present, defaulted;
    PSID owner = nullptr, group = nullptr;
    PVOID dacl = nullptr, sacl = nullptr;
    BYTE absolute[SECURITY_DESCRIPTOR_MIN_LENGTH_];
    if (!InitializeSecurityDescriptor(absolute, 1))
        return GetLastError();
    if (information & OWNER_SECURITY_INFORMATION) {
        GetSecurityDescriptorOwner(sd, &owner, &defaulted);
        SetSecurityDescriptorOwner(absolute, owner, defaulted);
    }
    if (information & GROUP_SECURITY_INFORMATION) {
        GetSecurityDescriptorGroup(sd, &group, &defaulted);
        SetSecurityDescriptorGroup(absolute, group, defaulted);
    }
    if (information & DACL_SECURITY_INFORMATION) {
        GetSecurityDescriptorDacl(sd, &present, &dacl, &defaulted);
        SetSecurityDescriptorDacl(absolute, present, dacl, defaulted);
    }
    if (information & SACL_SECURITY_INFORMATION) {
        GetSecurityDescriptorSacl(sd, &present, &sacl, &defaulted);
        SetSecurityDescriptorSacl(absolute, present, sacl, defaulted);
    }
    DWORD length = size;
    if (MakeSelfRelativeSD(absolute, out, &length)) {
        *needed = length;
        return ERROR_SUCCESS;
    }
    DWORD error = GetLastError();
    *needed = length;
    return error == ERROR_INSUFFICIENT_BUFFER ? ERROR_INSUFFICIENT_BUFFER : error;
}

/* ------------------------------------------------------------------ service process tokens */

static void make_sid(BYTE *sid, BYTE authority, DWORD count, const DWORD *sub)
{
    sid[0] = 1;
    sid[1] = (BYTE)count;
    memset(sid + 2, 0, 5);
    sid[7] = authority;
    memcpy(sid + 8, sub, count * 4);
}

/* the service's own sid: S-1-5-80 and the five words of the sha-1 of the upper case name. */
DWORD AccessServiceSid(const WCHAR *name, BYTE *sid)
{
    WCHAR upper[SVC_NAME_MAX + 1];
    size_t n = wcslen(name);
    for (size_t i = 0; i <= n; i++)
        upper[i] = towupper(name[i]);
    uint8_t digest[20];
    mime_hash(&mime_sha1, upper, n * sizeof(WCHAR), digest);
    DWORD sub[6] = {80};
    memcpy(sub + 1, digest, 20);
    make_sid(sid, 5, 6, sub);
    return 0;
}

typedef struct TOKEN_GROUP_ {
    PVOID Sid;
    DWORD Attributes;
} TOKEN_GROUP_;

/*
 * the token of a service running as one of the service accounts: the account sid, its usual groups and privileges,
 * built as a logon would build them. user accounts are logged on instead.
 */
static HANDLE build_service_token(DWORD account, const WCHAR *name, DWORD sid_type, DWORD *error)
{
    BYTE user_sid[SECURITY_MAX_SID_SIZE_], everyone[SECURITY_MAX_SID_SIZE_], users[SECURITY_MAX_SID_SIZE_],
        authenticated[SECURITY_MAX_SID_SIZE_], service[SECURITY_MAX_SID_SIZE_], label[SECURITY_MAX_SID_SIZE_],
        service_sid[SECURITY_MAX_SID_SIZE_], local[SECURITY_MAX_SID_SIZE_], network[SECURITY_MAX_SID_SIZE_];
    DWORD sub[6];
    sub[0] = account;
    make_sid(user_sid, 5, 1, sub);
    sub[0] = 0;
    make_sid(everyone, 1, 1, sub);
    sub[0] = 32;
    sub[1] = 545;
    make_sid(users, 5, 2, sub);
    sub[0] = 11;
    make_sid(authenticated, 5, 1, sub);
    sub[0] = 6;
    make_sid(service, 5, 1, sub);
    sub[0] = 0;
    make_sid(local, 2, 1, sub);
    sub[0] = 2;
    make_sid(network, 5, 1, sub);
    sub[0] = 0x4000;
    make_sid(label, 16, 1, sub);

    struct {
        DWORD Count;
        TOKEN_GROUP_ Groups[9];
    } groups = {};
    const DWORD normal = 7;           /* mandatory, enabled by default, enabled. */
    const DWORD owner_group = 7 | 8;
    (void)owner_group;
    DWORD n = 0;
    groups.Groups[n++] = (TOKEN_GROUP_){everyone, normal};
    groups.Groups[n++] = (TOKEN_GROUP_){local, normal};
    groups.Groups[n++] = (TOKEN_GROUP_){users, normal};
    groups.Groups[n++] = (TOKEN_GROUP_){authenticated, normal};
    groups.Groups[n++] = (TOKEN_GROUP_){service, normal};
    if (account == 20)
        groups.Groups[n++] = (TOKEN_GROUP_){network, normal};
    if (sid_type != SERVICE_SID_TYPE_NONE && name) {
        AccessServiceSid(name, service_sid);
        groups.Groups[n++] = (TOKEN_GROUP_){service_sid, normal | 8};
    }
    groups.Groups[n++] = (TOKEN_GROUP_){label, 0x60}; /* integrity label, enabled. */
    groups.Count = n;

    static const WCHAR *const names[] = {L"SeChangeNotifyPrivilege", L"SeCreateGlobalPrivilege",
                                         L"SeImpersonatePrivilege", L"SeIncreaseWorkingSetPrivilege"};
    struct {
        DWORD Count;
        LUID_AND_ATTRIBUTES Privileges[4];
    } privileges = {};
    for (DWORD i = 0; i < RTL_NUMBER_OF(names); i++) {
        if (LookupPrivilegeValueW(nullptr, names[i], &privileges.Privileges[privileges.Count].Luid)) {
            /* the first three are enabled, as a logon of the account enables them. */
            privileges.Privileges[privileges.Count].Attributes = i < 3 ? 3 : 1;
            privileges.Count++;
        }
    }

    struct {
        PVOID Sid;
        DWORD Attributes;
    } user = {user_sid, 0};
    PVOID primary = user_sid;
    PVOID owner = user_sid;
    struct {
        UCHAR Name[8];
        LUID Id;
    } source = {{'S', 'c', 'm', ' ', ' ', ' ', ' ', ' '}, {0, 0}};
    AllocateLocallyUniqueId(&source.Id);
    LUID authentication = {account == 19 ? 0x3E5u : 0x3E4u, 0};
    SECURITY_QUALITY_OF_SERVICE_ qos = {sizeof(qos), 2, 0, 0};
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, nullptr, 0, nullptr, nullptr);
    oa.SecurityQualityOfService = &qos;
    LARGE_INTEGER expiration = {.QuadPart = 0x7FFFFFFFFFFFFFFFll};
    AccessEnablePrivilege(L"SeCreateTokenPrivilege");
    HANDLE token = nullptr;
    NTSTATUS status = NtCreateToken(&token, TOKEN_ALL_ACCESS, &oa, TokenPrimary, &authentication, &expiration, &user,
                                    &groups, &privileges, &owner, &primary, nullptr, &source);
    if (!NT_SUCCESS(status)) {
        *error = RtlNtStatusToDosError(status);
        return nullptr;
    }
    DWORD session = 0;
    SetTokenInformation(token, TokenSessionId, &session, sizeof(session));
    return token;
}

/* the token for a service's process, NULL when services.exe's own token is the right one. */
HANDLE AccessServiceToken(SERVICE *s, DWORD *error)
{
    *error = 0;
    HKEY key = DbOpenKey(s->Name, KEY_READ, FALSE);
    if (!key)
        return nullptr;
    WCHAR *account = DbReadString(key, L"ObjectName", nullptr);
    DWORD sid_type = SERVICE_SID_TYPE_NONE;
    DbReadDword(key, L"ServiceSidType", &sid_type);
    RegCloseKey(key);
    HANDLE token = nullptr;
    if (!account || !*account || !_wcsicmp(account, L"LocalSystem") || !_wcsicmp(account, L"NT AUTHORITY\\SYSTEM") ||
        !_wcsicmp(account, L"NT AUTHORITY\\LocalSystem")) {
        /* the system's own token, unless the service wants a sid of its own. */
        if (sid_type == SERVICE_SID_TYPE_NONE) {
            SvcFree(account);
            return nullptr;
        }
        HANDLE own;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, &own)) {
            if (!DuplicateTokenEx(own, TOKEN_ALL_ACCESS, nullptr, 2, TokenPrimary, &token))
                *error = GetLastError();
            CloseHandle(own);
        } else {
            *error = GetLastError();
        }
    } else if (!_wcsicmp(account, L"NT AUTHORITY\\LocalService") || !_wcsicmp(account, L"LocalService")) {
        token = build_service_token(19, s->Name, sid_type, error);
    } else if (!_wcsicmp(account, L"NT AUTHORITY\\NetworkService") || !_wcsicmp(account, L"NetworkService")) {
        token = build_service_token(20, s->Name, sid_type, error);
    } else {
        /* a user: ".\name" or "machine\name", logged on with the password the service was given. */
        const WCHAR *name = account;
        const WCHAR *slash = wcschr(account, L'\\');
        if (slash)
            name = slash + 1;
        HANDLE logon = nullptr;
        if (LogonUserW(name, L".", L"", 5 /* LOGON32_LOGON_SERVICE */, 0, &logon)) {
            if (!DuplicateTokenEx(logon, TOKEN_ALL_ACCESS, nullptr, 2, TokenPrimary, &token))
                *error = GetLastError();
            CloseHandle(logon);
            DWORD session = 0;
            if (token)
                SetTokenInformation(token, TokenSessionId, &session, sizeof(session));
        } else {
            *error = ERROR_SERVICE_LOGON_FAILED;
        }
    }
    SvcFree(account);
    return token;
}

/* replaces the descriptor parts the information names with the modification's. */
DWORD AccessSetSd(PSECURITY_DESCRIPTOR *sdp, DWORD information, PSECURITY_DESCRIPTOR modification)
{
    BOOL present = FALSE, defaulted = FALSE;
    PSID owner = nullptr, group = nullptr;
    PVOID dacl = nullptr, sacl = nullptr;
    BOOL dacl_present = FALSE, sacl_present = FALSE;
    PSECURITY_DESCRIPTOR old = *sdp;
    GetSecurityDescriptorOwner(old, &owner, &defaulted);
    GetSecurityDescriptorGroup(old, &group, &defaulted);
    GetSecurityDescriptorDacl(old, &dacl_present, &dacl, &defaulted);
    GetSecurityDescriptorSacl(old, &sacl_present, &sacl, &defaulted);
    if (information & OWNER_SECURITY_INFORMATION)
        GetSecurityDescriptorOwner(modification, &owner, &defaulted);
    if (information & GROUP_SECURITY_INFORMATION)
        GetSecurityDescriptorGroup(modification, &group, &defaulted);
    if (information & DACL_SECURITY_INFORMATION) {
        GetSecurityDescriptorDacl(modification, &present, &dacl, &defaulted);
        dacl_present = present;
    }
    if (information & SACL_SECURITY_INFORMATION) {
        GetSecurityDescriptorSacl(modification, &present, &sacl, &defaulted);
        sacl_present = present;
    }
    BYTE absolute[SECURITY_DESCRIPTOR_MIN_LENGTH_];
    if (!InitializeSecurityDescriptor(absolute, 1))
        return GetLastError();
    SetSecurityDescriptorOwner(absolute, owner, FALSE);
    SetSecurityDescriptorGroup(absolute, group, FALSE);
    SetSecurityDescriptorDacl(absolute, dacl_present, dacl, FALSE);
    SetSecurityDescriptorSacl(absolute, sacl_present, sacl, FALSE);
    DWORD length = 0;
    MakeSelfRelativeSD(absolute, nullptr, &length);
    if (!length)
        return GetLastError();
    PSECURITY_DESCRIPTOR fresh = LocalAlloc(0, length);
    if (!fresh)
        return ERROR_NOT_ENOUGH_MEMORY;
    if (!MakeSelfRelativeSD(absolute, fresh, &length)) {
        DWORD error = GetLastError();
        LocalFree(fresh);
        return error;
    }
    LocalFree(old);
    *sdp = fresh;
    return ERROR_SUCCESS;
}
