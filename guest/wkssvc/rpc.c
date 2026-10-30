/*
 * the workstation service's side of the interface: what the calls of netapi32 are answered with. The redirector of
 * Windows holds the tuning values and the connections; Mime has no network to reach with one, so the values are the
 * defaults of the service with what its registry key says, and there are no connections and no transports.
 */
#include "wkssvc_private.h"
#include "wkssvc/wkssvc.h"

#define PLATFORM_ID_NT 500
#define NERR_UseNotFound 2250
#define NERR_WkstaNotStarted 2138
#define WORKGROUP L"WORKGROUP"
#define TOKEN_QUERY 0x0008
#define TokenUser 1

WINBASEAPI BOOL WINAPI GetUserNameW(LPWSTR buffer, LPDWORD size);
WINBASEAPI BOOL WINAPI OpenThreadToken(HANDLE thread, DWORD access, BOOL as_self, PHANDLE token);
WINBASEAPI BOOL WINAPI GetTokenInformation(HANDLE token, DWORD klass, LPVOID info, DWORD length, PDWORD returned);
WINBASEAPI BOOL WINAPI LookupAccountSidW(LPCWSTR system, PSID sid, LPWSTR name, LPDWORD name_size, LPWSTR domain,
                                         LPDWORD domain_size, PDWORD use);

void *WINAPI MIDL_user_allocate(size_t size)
{
    return LocalAlloc(LMEM_FIXED, size);
}

void WINAPI MIDL_user_free(void *p)
{
    LocalFree(p);
}

/* the stubs name the binding routines of the generic handles, which only a client calls. */
handle_t WINAPI WKSSVC_IDENTIFY_HANDLE_bind(WKSSVC_IDENTIFY_HANDLE server)
{
    return nullptr;
}

void WINAPI WKSSVC_IDENTIFY_HANDLE_unbind(WKSSVC_IDENTIFY_HANDLE server, handle_t binding)
{
}

handle_t WINAPI WKSSVC_IMPERSONATE_HANDLE_bind(WKSSVC_IMPERSONATE_HANDLE server)
{
    return nullptr;
}

void WINAPI WKSSVC_IMPERSONATE_HANDLE_unbind(WKSSVC_IMPERSONATE_HANDLE server, handle_t binding)
{
}

/* ------------------------------------------------------------------ the configuration */

/* the values of the service in the order of WKSTA_INFO_502, with the defaults the service starts with. */
static const struct {
    const WCHAR *Name;
    DWORD Default;
} g_values[] = {
    {L"CharWait", 3600},
    {L"CollectionTime", 250},
    {L"MaxCollectionCount", 16},
    {L"KeepConn", 600},
    {L"MaxCmds", 50},
    {L"SessTimeout", 60},
    {L"SizCharBuf", 512},
    {L"MaxThreads", 17},
    {L"LockQuota", 6144},
    {L"LockIncrement", 10},
    {L"LockMaximum", 500},
    {L"PipeIncrement", 10},
    {L"PipeMaximum", 500},
    {L"CacheFileTimeout", 10},
    {L"DormantFileLimit", 1023},
    {L"ReadAheadThroughput", 0xFFFFFFFF},
    {L"MailslotBuffers", 3},
    {L"ServerAnnounceBuffers", 20},
    {L"NumIllegalDatagramEvents", 5},
    {L"IllegalDatagramResetTime", 3600},
    {L"LogElectionPackets", 0},
    {L"UseOpportunisticLocking", 1},
    {L"UseUnlockBehind", 1},
    {L"UseCloseBehind", 1},
    {L"BufNamedPipes", 1},
    {L"UseLockReadUnlock", 1},
    {L"UtilizeNtCaching", 1},
    {L"UseRawRead", 1},
    {L"UseRawWrite", 1},
    {L"UseWriteRawData", 1},
    {L"UseEncryption", 1},
    {L"BufFilesDenyWrite", 1},
    {L"BufReadOnlyFiles", 1},
    {L"ForceCoreCreateMode", 1},
    {L"Use512ByteMaxTransfer", 0},
};
#define VALUE_COUNT (sizeof(g_values) / sizeof(g_values[0]))
static_assert(VALUE_COUNT * sizeof(DWORD) == sizeof(WKSTA_INFO_502), "the values are the fields of level 502");

static DWORD g_config[VALUE_COUNT];
static WCHAR g_computer[MAX_COMPUTERNAME_LENGTH + 1];
static FILETIME g_started;
static SRWLOCK g_lock = SRWLOCK_INIT;

/* the name of the machine and the values, from the service's key. the lock is not held. */
DWORD WsLoadConfiguration(void)
{
    DWORD size = RTL_NUMBER_OF(g_computer);
    if (!GetComputerNameW(g_computer, &size))
        return GetLastError();
    HKEY key;
    BOOL have = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\LanmanWorkstation\\Parameters", 0,
                              KEY_READ, &key) == ERROR_SUCCESS;
    AcquireSRWLockExclusive(&g_lock);
    for (SIZE_T i = 0; i < VALUE_COUNT; i++) {
        DWORD value, type, bytes = sizeof(value);
        g_config[i] = g_values[i].Default;
        if (have && RegQueryValueExW(key, g_values[i].Name, nullptr, &type, (BYTE *)&value, &bytes) == ERROR_SUCCESS &&
            type == REG_DWORD)
            g_config[i] = value;
    }
    GetSystemTimeAsFileTime(&g_started);
    ReleaseSRWLockExclusive(&g_lock);
    if (have)
        RegCloseKey(key);
    return ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ helpers */

static void copy_into(WCHAR *to, const WCHAR *from, DWORD chars)
{
    DWORD n = 0;
    while (n + 1 < chars && from[n]) {
        to[n] = from[n];
        n++;
    }
    to[n] = 0;
}

static unsigned short *copy_text(const WCHAR *text)
{
    SIZE_T bytes = (wcslen(text) + 1) * sizeof(WCHAR);
    unsigned short *copy = MIDL_user_allocate(bytes);
    if (copy)
        memcpy(copy, text, bytes);
    return copy;
}

/* the caller must be someone the pipe could impersonate. */
static BOOL impersonating(void)
{
    return RpcImpersonateClient(nullptr) == RPC_S_OK;
}

static void stop_impersonating(void)
{
    RpcRevertToSelf();
}

/* ------------------------------------------------------------------ the workstation */

unsigned long WINAPI wkssvc_NetrWkstaGetInfo_s(WKSSVC_IDENTIFY_HANDLE server, unsigned long level, WKSTA_INFO *info)
{
    if (level != 100 && level != 101 && level != 102 && level != 502)
        return ERROR_INVALID_LEVEL;
    /* the descriptor of the configuration gives the three levels of reading to everyone. */
    if (!impersonating())
        return ERROR_ACCESS_DENIED;
    stop_impersonating();
    if (level == 502) {
        WKSTA_INFO_502 *out = MIDL_user_allocate(sizeof(*out));
        if (!out)
            return ERROR_NOT_ENOUGH_MEMORY;
        AcquireSRWLockShared(&g_lock);
        memcpy(out, g_config, sizeof(*out));
        ReleaseSRWLockShared(&g_lock);
        info->WkstaInfo502 = out;
        return NO_ERROR;
    }
    /* the three levels share their leading fields. */
    WKSTA_INFO_102 *out = MIDL_user_allocate(sizeof(*out));
    if (!out)
        return ERROR_NOT_ENOUGH_MEMORY;
    memset(out, 0, sizeof(*out));
    out->wki102_platform_id = PLATFORM_ID_NT;
    out->wki102_computername = copy_text(g_computer);
    out->wki102_langroup = copy_text(WORKGROUP);
    out->wki102_ver_major = 10;
    out->wki102_ver_minor = 0;
    if (level >= 101)
        out->wki102_lanroot = copy_text(L"");
    if (level == 102)
        out->wki102_logged_on_users = 1;
    if (!out->wki102_computername || !out->wki102_langroup || (level >= 101 && !out->wki102_lanroot)) {
        MIDL_user_free(out->wki102_computername);
        MIDL_user_free(out->wki102_langroup);
        MIDL_user_free(out->wki102_lanroot);
        MIDL_user_free(out);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    /* the structures of the levels are prefixes of one another, and the union holds a pointer to the one asked for. */
    if (level == 100)
        info->WkstaInfo100 = (WKSTA_INFO_100 *)out;
    else if (level == 101)
        info->WkstaInfo101 = (WKSTA_INFO_101 *)out;
    else
        info->WkstaInfo102 = out;
    return NO_ERROR;
}

unsigned long WINAPI wkssvc_NetrWkstaSetInfo_s(WKSSVC_IDENTIFY_HANDLE server)
{
    return ERROR_NOT_SUPPORTED;
}

/* ------------------------------------------------------------------ the users */

/* the name and domain of the account the caller runs as, or the name of the interactive user. */
static BOOL caller_account(WCHAR *name, DWORD name_chars, WCHAR *domain, DWORD domain_chars)
{
    BOOL ok = FALSE;
    HANDLE token;
    if (!impersonating())
        return FALSE;
    if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token)) {
        BYTE buffer[128];
        DWORD returned;
        if (GetTokenInformation(token, TokenUser, buffer, sizeof(buffer), &returned)) {
            DWORD use, n = name_chars, d = domain_chars;
            ok = LookupAccountSidW(nullptr, *(PSID *)buffer, name, &n, domain, &d, &use);
        }
        CloseHandle(token);
    }
    stop_impersonating();
    return ok;
}

static BOOL account_of_caller(WCHAR *name, DWORD name_chars, WCHAR *domain, DWORD domain_chars)
{
    if (caller_account(name, name_chars, domain, domain_chars))
        return TRUE;
    /* an account the machine has no record of is named by the user it logged on as. */
    DWORD size = name_chars;
    if (!impersonating())
        return FALSE;
    BOOL ok = GetUserNameW(name, &size);
    stop_impersonating();
    copy_into(domain, g_computer, domain_chars);
    return ok;
}

unsigned long WINAPI wkssvc_NetrWkstaUserGetInfo_s(WKSSVC_IMPERSONATE_HANDLE reserved, unsigned long level,
                                                   WKSTA_USER_INFO *info)
{
    if (reserved)
        return ERROR_INVALID_PARAMETER;
    if (level > 1 && level != 1101)
        return ERROR_INVALID_LEVEL;
    WCHAR name[257], domain[257];
    if (!account_of_caller(name, RTL_NUMBER_OF(name), domain, RTL_NUMBER_OF(domain)))
        return ERROR_ACCESS_DENIED;
    if (level == 0) {
        WKSTA_USER_INFO_0 *out = MIDL_user_allocate(sizeof(*out));
        if (!out)
            return ERROR_NOT_ENOUGH_MEMORY;
        out->wkui0_username = copy_text(name);
        info->UserInfo0 = out;
        return out->wkui0_username ? NO_ERROR : ERROR_NOT_ENOUGH_MEMORY;
    }
    if (level == 1101) {
        WKSTA_USER_INFO_1101 *out = MIDL_user_allocate(sizeof(*out));
        if (!out)
            return ERROR_NOT_ENOUGH_MEMORY;
        out->wkui1101_oth_domains = copy_text(L"");
        info->UserInfo1101 = out;
        return out->wkui1101_oth_domains ? NO_ERROR : ERROR_NOT_ENOUGH_MEMORY;
    }
    WKSTA_USER_INFO_1 *out = MIDL_user_allocate(sizeof(*out));
    if (!out)
        return ERROR_NOT_ENOUGH_MEMORY;
    out->wkui1_username = copy_text(name);
    out->wkui1_logon_domain = copy_text(domain);
    out->wkui1_oth_domains = copy_text(L"");
    out->wkui1_logon_server = copy_text(g_computer);
    info->UserInfo1 = out;
    return NO_ERROR;
}

/* the users logged on are the one the session of the console has. */
static BOOL console_user(WCHAR *name, DWORD chars)
{
    typedef DWORD(WINAPI * GET_ACTIVE)(void);
    typedef BOOL(WINAPI * QUERY)(HANDLE server, DWORD session, int klass, LPWSTR *buffer, DWORD *size);
    typedef void(WINAPI * FREE)(PVOID memory);
    HMODULE wts = LoadLibraryW(L"wtsapi32.dll");
    if (!wts)
        return FALSE;
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    GET_ACTIVE active = (GET_ACTIVE)GetProcAddress(kernel, "WTSGetActiveConsoleSessionId");
    QUERY query = (QUERY)GetProcAddress(wts, "WTSQuerySessionInformationW");
    FREE release = (FREE)GetProcAddress(wts, "WTSFreeMemory");
    BOOL ok = FALSE;
    LPWSTR text = nullptr;
    DWORD bytes = 0;
    DWORD session = active ? active() : 1;
    if (query && release && session != 0xFFFFFFFF && query(nullptr, session, 5 /* WTSUserName */, &text, &bytes) && text) {
        if (*text) {
            copy_into(name, text, chars);
            ok = TRUE;
        }
        release(text);
    }
    FreeLibrary(wts);
    return ok;
}

unsigned long WINAPI wkssvc_NetrWkstaUserEnum_s(WKSSVC_IDENTIFY_HANDLE server, WKSTA_USER_ENUM_STRUCT *users,
                                                unsigned long maximum, unsigned long *total, unsigned long *resume)
{
    if (users->Level > 1)
        return ERROR_INVALID_LEVEL;
    /* a continuation of an enumeration that is over, which has one entry. */
    WCHAR name[257];
    BOOL present = !(resume && *resume) && console_user(name, RTL_NUMBER_OF(name));
    *total = present ? 1 : 0;
    if (resume)
        *resume = 0;
    if (users->Level == 0) {
        WKSTA_USER_INFO_0_CONTAINER *c = MIDL_user_allocate(sizeof(*c));
        if (!c)
            return ERROR_NOT_ENOUGH_MEMORY;
        memset(c, 0, sizeof(*c));
        users->WkstaUserInfo.Level0 = c;
        if (present) {
            c->Buffer = MIDL_user_allocate(sizeof(*c->Buffer));
            if (!c->Buffer)
                return ERROR_NOT_ENOUGH_MEMORY;
            c->EntriesRead = 1;
            c->Buffer[0].wkui0_username = copy_text(name);
        }
        return NO_ERROR;
    }
    WKSTA_USER_INFO_1_CONTAINER *c = MIDL_user_allocate(sizeof(*c));
    if (!c)
        return ERROR_NOT_ENOUGH_MEMORY;
    memset(c, 0, sizeof(*c));
    users->WkstaUserInfo.Level1 = c;
    if (present) {
        c->Buffer = MIDL_user_allocate(sizeof(*c->Buffer));
        if (!c->Buffer)
            return ERROR_NOT_ENOUGH_MEMORY;
        c->EntriesRead = 1;
        c->Buffer[0].wkui1_username = copy_text(name);
        c->Buffer[0].wkui1_logon_domain = copy_text(g_computer);
        c->Buffer[0].wkui1_oth_domains = copy_text(L"");
        c->Buffer[0].wkui1_logon_server = copy_text(g_computer);
    }
    return NO_ERROR;
}

unsigned long WINAPI wkssvc_NetrWkstaUserSetInfo_s(WKSSVC_IDENTIFY_HANDLE server)
{
    return ERROR_NOT_SUPPORTED;
}

/* ------------------------------------------------------------------ transports and connections */

unsigned long WINAPI wkssvc_NetrWkstaTransportEnum_s(WKSSVC_IDENTIFY_HANDLE server, WKSTA_TRANSPORT_ENUM_STRUCT *transports,
                                                     unsigned long maximum, unsigned long *total, unsigned long *resume)
{
    if (transports->Level != 0)
        return ERROR_INVALID_LEVEL;
    WKSTA_TRANSPORT_INFO_0_CONTAINER *c = MIDL_user_allocate(sizeof(*c));
    if (!c)
        return ERROR_NOT_ENOUGH_MEMORY;
    memset(c, 0, sizeof(*c));
    transports->WkstaTransportInfo.Level0 = c;
    *total = 0;
    if (resume)
        *resume = 0;
    return NO_ERROR;
}

unsigned long WINAPI wkssvc_NetrWkstaTransportAdd_s(WKSSVC_IDENTIFY_HANDLE server)
{
    return ERROR_NOT_SUPPORTED;
}

unsigned long WINAPI wkssvc_NetrWkstaTransportDel_s(WKSSVC_IDENTIFY_HANDLE server)
{
    return ERROR_NOT_SUPPORTED;
}

unsigned long WINAPI wkssvc_NetrUseAdd_s(WKSSVC_IDENTIFY_HANDLE server, unsigned long level, USE_INFO *info,
                                         unsigned long *parameter)
{
    if (level < 1 || level > 3)
        return ERROR_INVALID_LEVEL;
    /* no redirector reaches a share. */
    return ERROR_BAD_NETPATH;
}

unsigned long WINAPI wkssvc_NetrUseGetInfo_s(WKSSVC_IDENTIFY_HANDLE server, unsigned short *name, unsigned long level,
                                             USE_INFO *info)
{
    if (level > 3)
        return ERROR_INVALID_LEVEL;
    return NERR_UseNotFound;
}

unsigned long WINAPI wkssvc_NetrUseDel_s(WKSSVC_IDENTIFY_HANDLE server, unsigned short *name, unsigned long force)
{
    return NERR_UseNotFound;
}

unsigned long WINAPI wkssvc_NetrUseEnum_s(WKSSVC_IDENTIFY_HANDLE server, USE_ENUM_STRUCT *uses, unsigned long maximum,
                                          unsigned long *total, unsigned long *resume)
{
    if (uses->Level > 2)
        return ERROR_INVALID_LEVEL;
    *total = 0;
    if (resume)
        *resume = 0;
    if (uses->Level == 0) {
        USE_INFO_0_CONTAINER *c = MIDL_user_allocate(sizeof(*c));
        if (!c)
            return ERROR_NOT_ENOUGH_MEMORY;
        memset(c, 0, sizeof(*c));
        uses->UseInfo.Level0 = c;
    } else if (uses->Level == 1) {
        USE_INFO_1_CONTAINER *c = MIDL_user_allocate(sizeof(*c));
        if (!c)
            return ERROR_NOT_ENOUGH_MEMORY;
        memset(c, 0, sizeof(*c));
        uses->UseInfo.Level1 = c;
    } else {
        USE_INFO_2_CONTAINER *c = MIDL_user_allocate(sizeof(*c));
        if (!c)
            return ERROR_NOT_ENOUGH_MEMORY;
        memset(c, 0, sizeof(*c));
        uses->UseInfo.Level2 = c;
    }
    return NO_ERROR;
}

unsigned long WINAPI wkssvc_NetrMessageBufferSend_s(WKSSVC_IDENTIFY_HANDLE server)
{
    return ERROR_NOT_SUPPORTED;
}

/* ------------------------------------------------------------------ statistics and joining */

unsigned long WINAPI wkssvc_NetrWorkstationStatisticsGet_s(WKSSVC_IDENTIFY_HANDLE server, unsigned short *service,
                                                           unsigned long level, unsigned long options,
                                                           STAT_WORKSTATION_0 **buffer)
{
    if (level != 0)
        return ERROR_INVALID_LEVEL;
    STAT_WORKSTATION_0 *out = MIDL_user_allocate(sizeof(*out));
    if (!out)
        return ERROR_NOT_ENOUGH_MEMORY;
    memset(out, 0, sizeof(*out));
    AcquireSRWLockShared(&g_lock);
    out->StatisticsStartTime = ((LONGLONG)g_started.dwHighDateTime << 32) | g_started.dwLowDateTime;
    ReleaseSRWLockShared(&g_lock);
    *buffer = out;
    return NO_ERROR;
}

unsigned long WINAPI wkssvc_NetrLogonDomainNameAdd_s(WKSSVC_IDENTIFY_HANDLE server)
{
    return ERROR_NOT_SUPPORTED;
}

unsigned long WINAPI wkssvc_NetrLogonDomainNameDel_s(WKSSVC_IDENTIFY_HANDLE server)
{
    return ERROR_NOT_SUPPORTED;
}

unsigned long WINAPI wkssvc_NetrJoinDomain_s(WKSSVC_IDENTIFY_HANDLE server)
{
    return ERROR_NOT_SUPPORTED;
}

unsigned long WINAPI wkssvc_NetrUnjoinDomain_s(WKSSVC_IDENTIFY_HANDLE server)
{
    return ERROR_NOT_SUPPORTED;
}

unsigned long WINAPI wkssvc_NetrRenameMachineInDomain_s(WKSSVC_IDENTIFY_HANDLE server)
{
    return ERROR_NOT_SUPPORTED;
}

unsigned long WINAPI wkssvc_NetrValidateName_s(WKSSVC_IDENTIFY_HANDLE server)
{
    return ERROR_NOT_SUPPORTED;
}

/* the machine is in the workgroup setup leaves it in. */
unsigned long WINAPI wkssvc_NetrGetJoinInformation_s(WKSSVC_IDENTIFY_HANDLE server, unsigned short **name,
                                                     NETSETUP_JOIN_STATUS *type)
{
    unsigned short *copy = copy_text(WORKGROUP);
    if (!copy)
        return ERROR_NOT_ENOUGH_MEMORY;
    *name = copy;
    *type = NetSetupWorkgroupName;
    return NO_ERROR;
}
