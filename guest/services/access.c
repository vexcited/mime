/*
 * services.exe access checks. the manager and every service have a security descriptor, checked against the token the
 * client connected with, obtained by impersonating the named pipe.
 */
#include "services.h"
#include <crypto/crypto.h>
#include <mime/win32/rpc.h>

/* the identity of a pipe's client, or of the caller of the rpc call this thread serves. */
static BOOL impersonate(HANDLE pipe)
{
    if (pipe != SCM_RPC_CALLER)
        return ImpersonateNamedPipeClient(pipe);
    RPC_STATUS status = RpcImpersonateClient(nullptr);
    if (status)
        SetLastError(status);
    return status == RPC_S_OK;
}

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

/*
 * the privileges by luid, the table services.exe matches RequiredPrivileges against. LookupPrivilegeValueW of microsoft's
 * advapi32 asks lsass, which mime does not have.
 */
static const WCHAR *const g_privilege_names[] = {
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

/* the luid of a privilege name, 0 when unknown. */
DWORD AccessPrivilegeValue(const WCHAR *name)
{
    for (DWORD i = 0; i < RTL_NUMBER_OF(g_privilege_names); i++)
        if (!_wcsicmp(name, g_privilege_names[i]))
            return i + 2;
    return 0;
}

NTSYSAPI NTSTATUS NTAPI RtlAcquirePrivilege(PULONG privileges, ULONG count, ULONG flags, PVOID *state);
NTSYSAPI void NTAPI RtlReleasePrivilege(PVOID state);

/* the privilege is enabled for this thread only, on its own copy of the token, as windows' manager does. */
PVOID AccessAcquirePrivilege(const WCHAR *name)
{
    ULONG luid = AccessPrivilegeValue(name);
    PVOID state = nullptr;
    if (!luid || !NT_SUCCESS(RtlAcquirePrivilege(&luid, 1, 0, &state)))
        return nullptr;
    return state;
}

void AccessReleasePrivilege(PVOID state)
{
    if (state)
        RtlReleasePrivilege(state);
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

/* ScRemoveProcessPrivileges: the manager gives up SeTrustedCredManAccess and SeRelabel. */
static void remove_own_privileges(void)
{
    HANDLE token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        return;
    struct {
        DWORD Count;
        LUID_AND_ATTRIBUTES Privileges[2];
    } state = {2, {{{31, 0}, SE_PRIVILEGE_REMOVED}, {{32, 0}, SE_PRIVILEGE_REMOVED}}};
    AdjustTokenPrivileges(token, FALSE, (TOKEN_PRIVILEGES *)&state, 0, nullptr, nullptr);
    CloseHandle(token);
}

void AccessInit(void)
{
    remove_own_privileges();
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
    if (!impersonate(pipe))
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
    if (!impersonate(pipe))
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

/* an acl of allowed aces, each sid with its rights. */
static BYTE *make_acl(DWORD count, PVOID const *sids, const DWORD *rights)
{
    DWORD size = 8;
    for (DWORD i = 0; i < count; i++)
        size += 8 + GetLengthSid(sids[i]);
    BYTE *acl = LocalAlloc(LMEM_ZEROINIT, size);
    if (!acl)
        return nullptr;
    acl[0] = 2;
    *(WORD *)(acl + 2) = (WORD)size;
    *(WORD *)(acl + 4) = (WORD)count;
    BYTE *ace = acl + 8;
    for (DWORD i = 0; i < count; i++) {
        DWORD length = GetLengthSid(sids[i]);
        *(WORD *)(ace + 2) = (WORD)(8 + length);
        *(DWORD *)(ace + 4) = rights[i];
        memcpy(ace + 8, sids[i], length);
        ace += 8 + length;
    }
    return acl;
}

/* the privileges of a logon of each account, with their attributes, as windows 11 gives them. */
typedef struct {
    BYTE Value;
    BYTE Attributes;
} PRIVILEGE_SPEC;

static const PRIVILEGE_SPEC g_system_privileges[] = {
    {3, 0},  {4, 3},  {5, 0},  {7, 3},  {8, 0},  {9, 0},  {10, 0}, {11, 3}, {12, 0}, {13, 3}, {14, 3}, {15, 3},
    {16, 3}, {17, 0}, {18, 0}, {19, 0}, {20, 3}, {21, 3}, {22, 0}, {23, 3}, {25, 0}, {28, 0}, {29, 3}, {30, 3},
    {33, 3}, {34, 3}, {35, 3}, {36, 3},
};
static const PRIVILEGE_SPEC g_local_service_privileges[] = {
    {3, 0}, {5, 0}, {12, 0}, {19, 0}, {21, 0}, {23, 3}, {25, 0}, {29, 3}, {30, 3}, {33, 0}, {34, 0},
};
static const PRIVILEGE_SPEC g_network_service_privileges[] = {
    {3, 0}, {5, 0}, {19, 0}, {21, 0}, {23, 3}, {25, 0}, {29, 3}, {30, 3}, {33, 0}, {34, 0},
};
/* an administrator logged on as a service: the whole token, no filtering, at the high level. */
static const PRIVILEGE_SPEC g_administrator_privileges[] = {
    {5, 0},  {8, 0},  {9, 0},  {10, 0}, {11, 0}, {12, 0}, {13, 0}, {14, 0}, {15, 0}, {17, 0}, {18, 0}, {19, 0},
    {20, 0}, {22, 0}, {23, 3}, {24, 0}, {25, 0}, {28, 0}, {29, 3}, {30, 3}, {33, 0}, {34, 0}, {35, 0}, {36, 0},
};

/*
 * the groups a logon of the local service account gets after the others: the group sids of the capabilities
 * packageManagement, phoneCall, phoneCallSystem, cellularMessaging, cellularDeviceIdentity, cellularDeviceControl, one
 * unnamed and packageQuery, S-1-5-32 and the sha-256 of the upper case name. measured on windows 11.
 */
static const DWORD g_local_service_capabilities[8][9] = {
    {32, 734518492, 402359323, 2580938124, 1419864735, 4212787651, 2727913556, 228323224, 564805089},
    {32, 383293015, 3350740429, 1839969850, 1819881064, 1569454686, 4198502490, 78857879, 1413643331},
    {32, 2035927579, 283314533, 3422103930, 3587774809, 765962649, 3034203285, 3544878962, 607181067},
    {32, 3659434007, 2290108278, 1125199667, 3679670526, 1293081662, 2164323352, 1777701501, 2595986263},
    {32, 11742800, 2107441976, 3443185924, 4134956905, 3840447964, 3749968454, 3843513199, 670971053},
    {32, 3523901360, 1745872541, 794127107, 675934034, 1867954868, 1951917511, 1111796624, 2052600462},
    {32, 1488445330, 856673777, 1515413738, 1380768593, 2977925950, 2228326386, 886087428, 2802422674},
    {32, 1962849891, 688487262, 3571417821, 3628679630, 802580238, 1922556387, 206211640, 3335523193},
};

#define ACCOUNT_USER 0
#define ACCOUNT_SYSTEM 18
#define ACCOUNT_LOCAL_SERVICE 19
#define ACCOUNT_NETWORK_SERVICE 20

typedef struct SERVICE_LOGON {
    DWORD Account;                      /* one of ACCOUNT_*. */
    BYTE User[SECURITY_MAX_SID_SIZE_];  /* the user of ACCOUNT_USER. */
    const WCHAR *const *Names;          /* the services whose sids the token holds. */
    DWORD Count;
    DWORD First;                        /* the index of the starting service among them, Count when absent. */
    BOOL OwnSid;                        /* the starting service has a sid: ScAdjustTokenDefaultDacl applies. */
    BOOL Restricted;                    /* its sid type is restricted. */
} SERVICE_LOGON;

typedef struct GROUP_LIST {
    DWORD Count;
    TOKEN_GROUP_ Groups[40];
} GROUP_LIST;

static void add_group(GROUP_LIST *list, PVOID sid, DWORD attributes)
{
    if (list->Count < RTL_NUMBER_OF(list->Groups))
        list->Groups[list->Count++] = (TOKEN_GROUP_){sid, attributes};
}

/*
 * the token of a service's process, as the logon of windows 11 builds it, measured with scratch/procsd/self.c of the
 * research repository. LogonUserExExW of ScLogonService appends what ScBuildServiceTokenGroups gives when the process
 * hosts services with sids: those sids, the starting one enabled and the others to be enabled by svchost, a new logon
 * sid, LOCAL, and the write restricted sid for a restricted service. without them the logon sid follows LOCAL. the
 * default dacl is ScAdjustTokenDefaultDacl's when the starting service has a sid, the logon's otherwise. a restricted
 * service gets a write restricted token, ScMakeServiceTokenWriteRestricted.
 */
static HANDLE build_service_token(const SERVICE_LOGON *l, BYTE *logon, DWORD *error)
{
    BYTE user_sid[SECURITY_MAX_SID_SIZE_], everyone[SECURITY_MAX_SID_SIZE_], users[SECURITY_MAX_SID_SIZE_],
        authenticated[SECURITY_MAX_SID_SIZE_], service[SECURITY_MAX_SID_SIZE_], label[SECURITY_MAX_SID_SIZE_],
        local[SECURITY_MAX_SID_SIZE_], console[SECURITY_MAX_SID_SIZE_], organization[SECURITY_MAX_SID_SIZE_],
        admins[SECURITY_MAX_SID_SIZE_], system[SECURITY_MAX_SID_SIZE_], owner_rights[SECURITY_MAX_SID_SIZE_],
        write_restricted[SECURITY_MAX_SID_SIZE_], none[SECURITY_MAX_SID_SIZE_], local_admin[SECURITY_MAX_SID_SIZE_],
        local_account[SECURITY_MAX_SID_SIZE_], ntlm[SECURITY_MAX_SID_SIZE_],
        capabilities[RTL_NUMBER_OF(g_local_service_capabilities)][SECURITY_MAX_SID_SIZE_];
    BOOL user_account = l->Account == ACCOUNT_USER;
    DWORD sub[6];
    if (user_account) {
        memcpy(user_sid, l->User, GetLengthSid((PSID)l->User));
        memcpy(none, user_sid, GetLengthSid((PSID)user_sid));
        *(DWORD *)(none + 8 + 4 * (none[1] - 1)) = 513;
    } else {
        sub[0] = l->Account;
        make_sid(user_sid, 5, 1, sub);
    }
    sub[0] = 0;
    make_sid(everyone, 1, 1, sub);
    sub[0] = 32;
    sub[1] = 545;
    make_sid(users, 5, 2, sub);
    sub[1] = 544;
    make_sid(admins, 5, 2, sub);
    sub[0] = 64;
    sub[1] = 10;
    make_sid(ntlm, 5, 2, sub);
    sub[0] = 11;
    make_sid(authenticated, 5, 1, sub);
    sub[0] = 6;
    make_sid(service, 5, 1, sub);
    sub[0] = 15;
    make_sid(organization, 5, 1, sub);
    sub[0] = 18;
    make_sid(system, 5, 1, sub);
    sub[0] = 33;
    make_sid(write_restricted, 5, 1, sub);
    sub[0] = 113;
    make_sid(local_account, 5, 1, sub);
    sub[0] = 114;
    make_sid(local_admin, 5, 1, sub);
    sub[0] = 0;
    make_sid(local, 2, 1, sub);
    sub[0] = 1;
    make_sid(console, 2, 1, sub);
    sub[0] = 4;
    make_sid(owner_rights, 3, 1, sub);
    sub[0] = user_account ? 0x3000 : 0x4000;
    make_sid(label, 16, 1, sub);
    for (DWORD i = 0; i < RTL_NUMBER_OF(g_local_service_capabilities); i++)
        make_sid(capabilities[i], 5, 9, g_local_service_capabilities[i]);
    LUID id;
    AllocateLocallyUniqueId(&id);
    sub[0] = 5;
    sub[1] = (DWORD)id.HighPart;
    sub[2] = id.LowPart;
    make_sid(logon, 5, 3, sub);

    BYTE *service_sids = SvcAllocZero((l->Count ? l->Count : 1) * SECURITY_MAX_SID_SIZE_);
    GROUP_LIST *groups = SvcAllocZero(sizeof(*groups));
    if (!service_sids || !groups) {
        SvcFree(service_sids);
        SvcFree(groups);
        *error = ERROR_NOT_ENOUGH_MEMORY;
        return nullptr;
    }
    const DWORD normal = 7; /* mandatory, enabled by default, enabled. */
    if (user_account) {
        add_group(groups, none, normal);
        add_group(groups, everyone, normal);
        add_group(groups, local_admin, normal);
        add_group(groups, users, normal);
        add_group(groups, admins, 0xF);
    } else {
        add_group(groups, label, 0x60);
        add_group(groups, everyone, normal);
        add_group(groups, users, normal);
    }
    add_group(groups, service, normal);
    add_group(groups, console, normal);
    add_group(groups, authenticated, normal);
    add_group(groups, organization, normal);
    if (user_account)
        add_group(groups, local_account, normal);
    DWORD logon_attributes = l->OwnSid ? 0xC000000F : 0xC0000007;
    if (l->Count) {
        for (DWORD i = 0; i < l->Count; i++) {
            BYTE *sid = service_sids + i * SECURITY_MAX_SID_SIZE_;
            AccessServiceSid(l->Names[i], sid);
            add_group(groups, sid, i == l->First ? 0xE : 0xA);
        }
        add_group(groups, logon, logon_attributes);
        add_group(groups, local, normal);
        if (l->Restricted)
            add_group(groups, write_restricted, normal);
    } else {
        add_group(groups, local, normal);
        add_group(groups, logon, logon_attributes);
    }
    if (l->Account == ACCOUNT_LOCAL_SERVICE)
        for (DWORD i = 0; i < RTL_NUMBER_OF(g_local_service_capabilities); i++)
            add_group(groups, capabilities[i], normal);
    if (l->Account == ACCOUNT_SYSTEM)
        add_group(groups, admins, 0xE);
    if (user_account) {
        add_group(groups, ntlm, normal);
        add_group(groups, label, 0x60);
    }

    const PRIVILEGE_SPEC *table;
    DWORD held;
    switch (l->Account) {
    case ACCOUNT_SYSTEM:
        table = g_system_privileges;
        held = RTL_NUMBER_OF(g_system_privileges);
        break;
    case ACCOUNT_LOCAL_SERVICE:
        table = g_local_service_privileges;
        held = RTL_NUMBER_OF(g_local_service_privileges);
        break;
    case ACCOUNT_NETWORK_SERVICE:
        table = g_network_service_privileges;
        held = RTL_NUMBER_OF(g_network_service_privileges);
        break;
    default:
        table = g_administrator_privileges;
        held = RTL_NUMBER_OF(g_administrator_privileges);
        break;
    }
    struct {
        DWORD Count;
        LUID_AND_ATTRIBUTES Privileges[RTL_NUMBER_OF(g_system_privileges)];
    } privileges = {held};
    for (DWORD i = 0; i < held; i++)
        privileges.Privileges[i] = (LUID_AND_ATTRIBUTES){{table[i].Value, 0}, table[i].Attributes};

    PVOID dacl_sids[3 + 64];
    DWORD dacl_rights[3 + 64];
    DWORD aces = 0;
    if (l->OwnSid) {
        dacl_sids[aces] = system;
        dacl_rights[aces++] = GENERIC_ALL_RIGHT;
        dacl_sids[aces] = owner_rights;
        dacl_rights[aces++] = 0x20000; /* READ_CONTROL. */
        for (DWORD i = 0; i < l->Count && aces < RTL_NUMBER_OF(dacl_sids); i++) {
            dacl_sids[aces] = service_sids + i * SECURITY_MAX_SID_SIZE_;
            dacl_rights[aces++] = GENERIC_ALL_RIGHT;
        }
    } else if (user_account) {
        dacl_sids[aces] = admins;
        dacl_rights[aces++] = GENERIC_ALL_RIGHT;
        dacl_sids[aces] = system;
        dacl_rights[aces++] = GENERIC_ALL_RIGHT;
        dacl_sids[aces] = logon;
        dacl_rights[aces++] = 0xA0000000; /* GENERIC_READ and GENERIC_EXECUTE. */
    } else {
        dacl_sids[aces] = system;
        dacl_rights[aces++] = GENERIC_ALL_RIGHT;
        dacl_sids[aces] = user_sid;
        dacl_rights[aces++] = GENERIC_ALL_RIGHT;
    }
    BYTE *dacl = make_acl(aces, dacl_sids, dacl_rights);
    if (!dacl) {
        SvcFree(service_sids);
        SvcFree(groups);
        *error = ERROR_NOT_ENOUGH_MEMORY;
        return nullptr;
    }

    struct {
        PVOID Sid;
        DWORD Attributes;
    } user = {user_sid, 0};
    PVOID primary = user_account ? (PVOID)none : (PVOID)user_sid;
    PVOID owner = user_account ? (PVOID)admins : (PVOID)user_sid;
    PVOID default_dacl = dacl;
    struct {
        UCHAR Name[8];
        LUID Id;
    } source = {{'S', 'c', 'm', ' ', ' ', ' ', ' ', ' '}, {0, 0}};
    AllocateLocallyUniqueId(&source.Id);
    LUID authentication = {l->Account == ACCOUNT_SYSTEM ? 0x3E7u : l->Account == ACCOUNT_LOCAL_SERVICE ? 0x3E5u : 0x3E4u, 0};
    if (user_account)
        AllocateLocallyUniqueId(&authentication);
    SECURITY_QUALITY_OF_SERVICE_ qos = {sizeof(qos), 2, 0, 0};
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, nullptr, 0, nullptr, nullptr);
    oa.SecurityQualityOfService = &qos;
    LARGE_INTEGER expiration = {.QuadPart = 0x7FFFFFFFFFFFFFFFll};
    PVOID acquired = AccessAcquirePrivilege(L"SeCreateTokenPrivilege");
    HANDLE token = nullptr;
    NTSTATUS status = NtCreateToken(&token, TOKEN_ALL_ACCESS, &oa, TokenPrimary, &authentication, &expiration, &user,
                                    groups, &privileges, &owner, &primary, &default_dacl, &source);
    AccessReleasePrivilege(acquired);
    LocalFree(dacl);
    if (NT_SUCCESS(status) && l->Restricted) {
        GROUP_LIST *restricting = SvcAllocZero(sizeof(*restricting));
        HANDLE filtered = nullptr;
        if (restricting) {
            for (DWORD i = 0; i < l->Count; i++)
                add_group(restricting, service_sids + i * SECURITY_MAX_SID_SIZE_, 0);
            add_group(restricting, everyone, 0);
            add_group(restricting, logon, 0);
            add_group(restricting, write_restricted, 0);
            status = NtFilterToken(token, 8 /* WRITE_RESTRICTED */, nullptr, nullptr, restricting, &filtered);
            SvcFree(restricting);
        } else {
            status = STATUS_NO_MEMORY;
        }
        CloseHandle(token);
        token = NT_SUCCESS(status) ? filtered : nullptr;
    }
    SvcFree(service_sids);
    SvcFree(groups);
    if (!NT_SUCCESS(status)) {
        *error = RtlNtStatusToDosError(status);
        return nullptr;
    }
    DWORD session = 0;
    SetTokenInformation(token, TokenSessionId, &session, sizeof(session));
    return token;
}

/*
 * the user of a service that runs as an account of the machine, ".\name" or "machine\name". mime has one account, its
 * user, who administers it, and no passwords. another name fails to log on, as on windows.
 */
static DWORD machine_account(const WCHAR *account, BYTE *sid)
{
    const WCHAR *slash = wcsrchr(account, L'\\');
    if (!slash)
        return ERROR_SERVICE_LOGON_FAILED;
    WCHAR machine[64];
    DWORD chars = RTL_NUMBER_OF(machine);
    size_t domain = (size_t)(slash - account);
    BOOL local = domain == 1 && account[0] == L'.';
    if (!local && GetComputerNameW(machine, &chars))
        local = wcslen(machine) == domain && !_wcsnicmp(account, machine, domain);
    WCHAR name[64];
    if (!local || !LsaMachineUser(sid, name) || _wcsicmp(name, slash + 1))
        return ERROR_SERVICE_LOGON_FAILED;
    return 0;
}

/*
 * the token for a service's process, NULL when services.exe's own token is the right one: for the system account when
 * the process hosts no service with a sid. the descriptor of the process is made as ScCreateAndSetSD of services.exe
 * makes it: the logon sid does everything and administrators query, the logon sid owns it when the service has a sid.
 */
HANDLE AccessServiceToken(SERVICE *s, const WCHAR *const *sid_names, DWORD sid_count, PSECURITY_DESCRIPTOR *process_sd,
                          DWORD *error)
{
    *error = 0;
    *process_sd = nullptr;
    HKEY key = DbOpenKey(s->Name, KEY_READ, FALSE);
    if (!key)
        return nullptr;
    WCHAR *account = DbReadString(key, L"ObjectName", nullptr);
    DWORD sid_type = SERVICE_SID_TYPE_NONE;
    DbReadDword(key, L"ServiceSidType", &sid_type);
    RegCloseKey(key);
    SERVICE_LOGON l = {.Names = sid_names, .Count = sid_count, .First = sid_count};
    for (DWORD i = 0; i < sid_count; i++)
        if (!_wcsicmp(sid_names[i], s->Name))
            l.First = i;
    l.OwnSid = sid_type != SERVICE_SID_TYPE_NONE;
    l.Restricted = sid_type == SERVICE_SID_TYPE_RESTRICTED;
    if (!account || !*account || !_wcsicmp(account, L"LocalSystem") || !_wcsicmp(account, L"NT AUTHORITY\\SYSTEM") ||
        !_wcsicmp(account, L"NT AUTHORITY\\LocalSystem"))
        l.Account = ACCOUNT_SYSTEM;
    else if (!_wcsicmp(account, L"NT AUTHORITY\\LocalService") || !_wcsicmp(account, L"LocalService"))
        l.Account = ACCOUNT_LOCAL_SERVICE;
    else if (!_wcsicmp(account, L"NT AUTHORITY\\NetworkService") || !_wcsicmp(account, L"NetworkService"))
        l.Account = ACCOUNT_NETWORK_SERVICE;
    else
        *error = machine_account(account, l.User);
    SvcFree(account);
    if (*error || (l.Account == ACCOUNT_SYSTEM && !sid_count))
        return nullptr;
    BYTE logon[SECURITY_MAX_SID_SIZE_];
    HANDLE token = build_service_token(&l, logon, error);
    if (!token)
        return nullptr;
    BYTE admins[SECURITY_MAX_SID_SIZE_];
    DWORD sub[2] = {32, 544};
    make_sid(admins, 5, 2, sub);
    PVOID sids[2] = {logon, admins};
    static const DWORD rights[2] = {0x1FFFFF, 0x1400};
    BYTE *dacl = make_acl(2, sids, rights);
    BYTE absolute[SECURITY_DESCRIPTOR_MIN_LENGTH_];
    DWORD length = 0;
    if (dacl && InitializeSecurityDescriptor(absolute, 1) && SetSecurityDescriptorDacl(absolute, TRUE, dacl, FALSE) &&
        (!l.OwnSid || SetSecurityDescriptorOwner(absolute, logon, FALSE)) &&
        !MakeSelfRelativeSD(absolute, nullptr, &length) && length) {
        *process_sd = LocalAlloc(0, length);
        if (*process_sd && !MakeSelfRelativeSD(absolute, *process_sd, &length)) {
            LocalFree(*process_sd);
            *process_sd = nullptr;
        }
    }
    if (dacl)
        LocalFree(dacl);
    return token;
}

/*
 * the token of a process of the system account that hosts no service with a sid, which is the manager's own on windows.
 * mime's manager also creates the tokens of the logons, so the copy loses SeCreateTokenPrivilege, and the privileges
 * the manager enabled for its own work are back to their defaults.
 */
HANDLE AccessManagerToken(DWORD *error)
{
    HANDLE own, token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, &own)) {
        *error = GetLastError();
        return nullptr;
    }
    BOOL ok = DuplicateTokenEx(own, TOKEN_ALL_ACCESS, nullptr, 2, TokenPrimary, &token);
    CloseHandle(own);
    BYTE buffer[4096];
    DWORD length;
    if (ok && GetTokenInformation(token, TokenPrivileges, buffer, sizeof(buffer), &length)) {
        TOKEN_PRIVILEGES *held = (TOKEN_PRIVILEGES *)buffer;
        for (DWORD i = 0; i < held->PrivilegeCount; i++) {
            DWORD attributes = held->Privileges[i].Attributes;
            held->Privileges[i].Attributes = held->Privileges[i].Luid.LowPart == 2 && !held->Privileges[i].Luid.HighPart
                                                 ? SE_PRIVILEGE_REMOVED
                                             : attributes & SE_PRIVILEGE_ENABLED_BY_DEFAULT ? SE_PRIVILEGE_ENABLED
                                                                                            : 0;
        }
        ok = AdjustTokenPrivileges(token, FALSE, held, 0, nullptr, nullptr);
    }
    if (!ok) {
        *error = GetLastError();
        if (token)
            CloseHandle(token);
        return nullptr;
    }
    return token;
}

/* whether a multi string list of privilege names only names known privileges, ScReadPrivileges. */
BOOL AccessValidPrivileges(const WCHAR *list)
{
    for (const WCHAR *p = list; p && *p; p += wcslen(p) + 1)
        if (!AccessPrivilegeValue(p))
            return FALSE;
    return TRUE;
}

/*
 * ScCheckPrivilegesInToken and ScRemovePrivileges of services.exe. the privileges the starting service requires must all
 * be in the token, else error 1297. when every service of the process lists what it requires, every other
 * privilege but SeChangeNotifyPrivilege is removed from the token.
 */
DWORD AccessRemovePrivileges(HANDLE token, const WCHAR *const *lists, DWORD count, BOOL strip)
{
    BYTE buffer[4096];
    DWORD length;
    if (!GetTokenInformation(token, TokenPrivileges, buffer, sizeof(buffer), &length))
        return GetLastError();
    TOKEN_PRIVILEGES *held = (TOKEN_PRIVILEGES *)buffer;
    for (const WCHAR *p = lists[0]; p && *p; p += wcslen(p) + 1) {
        DWORD value = AccessPrivilegeValue(p);
        DWORD i = 0;
        while (i < held->PrivilegeCount && held->Privileges[i].Luid.LowPart != value)
            i++;
        if (i == held->PrivilegeCount)
            return 1297;
    }
    if (!strip)
        return 0;
    DWORD removed = 0;
    for (DWORD i = 0; i < held->PrivilegeCount; i++) {
        DWORD value = held->Privileges[i].Luid.LowPart;
        BOOL kept = value == 23 || held->Privileges[i].Luid.HighPart;
        for (DWORD j = 0; j < count && !kept; j++)
            for (const WCHAR *p = lists[j]; p && *p && !kept; p += wcslen(p) + 1)
                kept = AccessPrivilegeValue(p) == value;
        if (!kept)
            held->Privileges[removed++] = (LUID_AND_ATTRIBUTES){{value, 0}, SE_PRIVILEGE_REMOVED};
    }
    held->PrivilegeCount = removed;
    if (removed && !AdjustTokenPrivileges(token, FALSE, held, 0, nullptr, nullptr))
        return GetLastError();
    return 0;
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
