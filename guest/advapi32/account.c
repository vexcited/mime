/*
 * account names: the process user name and the name to sid mapping that windows gets from the lsa.
 *
 * there is no lsa: well known sids come from a table, local accounts from the machine identity, the logged on
 * user's sid and the profiles the kernel registers at boot.
 */
#include "advapi32_private.h"
#include <crypto/crypto.h>

typedef struct ACCOUNT {
    PCWSTR Name;
    PCWSTR Domain;
    BYTE Authority;
    BYTE Count;
    DWORD Sub[2];
    SID_NAME_USE Use;
} ACCOUNT;

#define NT_AUTHORITY L"NT AUTHORITY"
#define BUILTIN L"BUILTIN"
#define LABEL L"Mandatory Label"

/* names as an english installation reports them. */
static const ACCOUNT g_well_known[] = {
    {L"NULL SID", L"", 0, 1, {0}, SidTypeWellKnownGroup},
    {L"Everyone", L"", 1, 1, {0}, SidTypeWellKnownGroup},
    {L"LOCAL", L"", 2, 1, {0}, SidTypeWellKnownGroup},
    {L"CONSOLE LOGON", L"", 2, 1, {1}, SidTypeWellKnownGroup},
    {L"CREATOR OWNER", L"", 3, 1, {0}, SidTypeWellKnownGroup},
    {L"CREATOR GROUP", L"", 3, 1, {1}, SidTypeWellKnownGroup},
    {L"CREATOR OWNER SERVER", L"", 3, 1, {2}, SidTypeWellKnownGroup},
    {L"CREATOR GROUP SERVER", L"", 3, 1, {3}, SidTypeWellKnownGroup},
    {L"OWNER RIGHTS", L"", 3, 1, {4}, SidTypeWellKnownGroup},
    {NT_AUTHORITY, NT_AUTHORITY, 5, 0, {0}, SidTypeDomain},
    {L"DIALUP", NT_AUTHORITY, 5, 1, {1}, SidTypeWellKnownGroup},
    {L"NETWORK", NT_AUTHORITY, 5, 1, {2}, SidTypeWellKnownGroup},
    {L"BATCH", NT_AUTHORITY, 5, 1, {3}, SidTypeWellKnownGroup},
    {L"INTERACTIVE", NT_AUTHORITY, 5, 1, {4}, SidTypeWellKnownGroup},
    {L"SERVICE", NT_AUTHORITY, 5, 1, {6}, SidTypeWellKnownGroup},
    {L"ANONYMOUS LOGON", NT_AUTHORITY, 5, 1, {7}, SidTypeWellKnownGroup},
    {L"PROXY", NT_AUTHORITY, 5, 1, {8}, SidTypeWellKnownGroup},
    {L"ENTERPRISE DOMAIN CONTROLLERS", NT_AUTHORITY, 5, 1, {9}, SidTypeWellKnownGroup},
    {L"SELF", NT_AUTHORITY, 5, 1, {10}, SidTypeWellKnownGroup},
    {L"Authenticated Users", NT_AUTHORITY, 5, 1, {11}, SidTypeWellKnownGroup},
    {L"RESTRICTED", NT_AUTHORITY, 5, 1, {12}, SidTypeWellKnownGroup},
    {L"TERMINAL SERVER USER", NT_AUTHORITY, 5, 1, {13}, SidTypeWellKnownGroup},
    {L"REMOTE INTERACTIVE LOGON", NT_AUTHORITY, 5, 1, {14}, SidTypeWellKnownGroup},
    {L"This Organization", NT_AUTHORITY, 5, 1, {15}, SidTypeWellKnownGroup},
    {L"IUSR", NT_AUTHORITY, 5, 1, {17}, SidTypeWellKnownGroup},
    {L"SYSTEM", NT_AUTHORITY, 5, 1, {18}, SidTypeWellKnownGroup},
    {L"LOCAL SERVICE", NT_AUTHORITY, 5, 1, {19}, SidTypeWellKnownGroup},
    {L"NETWORK SERVICE", NT_AUTHORITY, 5, 1, {20}, SidTypeWellKnownGroup},
    {L"WRITE RESTRICTED", NT_AUTHORITY, 5, 1, {33}, SidTypeWellKnownGroup},
    {L"Local account", NT_AUTHORITY, 5, 1, {113}, SidTypeWellKnownGroup},
    {L"Local account and member of Administrators group", NT_AUTHORITY, 5, 1, {114}, SidTypeWellKnownGroup},
    {L"NTLM Authentication", NT_AUTHORITY, 5, 2, {64, 10}, SidTypeWellKnownGroup},
    {L"SChannel Authentication", NT_AUTHORITY, 5, 2, {64, 14}, SidTypeWellKnownGroup},
    {L"Digest Authentication", NT_AUTHORITY, 5, 2, {64, 21}, SidTypeWellKnownGroup},
    {L"NT SERVICE", L"NT SERVICE", 5, 1, {80}, SidTypeDomain},
    {L"ALL SERVICES", L"NT SERVICE", 5, 2, {80, 0}, SidTypeWellKnownGroup},
    {BUILTIN, BUILTIN, 5, 1, {32}, SidTypeDomain},
    {L"Administrators", BUILTIN, 5, 2, {32, 544}, SidTypeAlias},
    {L"Users", BUILTIN, 5, 2, {32, 545}, SidTypeAlias},
    {L"Guests", BUILTIN, 5, 2, {32, 546}, SidTypeAlias},
    {L"Power Users", BUILTIN, 5, 2, {32, 547}, SidTypeAlias},
    {L"Backup Operators", BUILTIN, 5, 2, {32, 551}, SidTypeAlias},
    {L"Replicator", BUILTIN, 5, 2, {32, 552}, SidTypeAlias},
    {L"Remote Desktop Users", BUILTIN, 5, 2, {32, 555}, SidTypeAlias},
    {L"Network Configuration Operators", BUILTIN, 5, 2, {32, 556}, SidTypeAlias},
    {L"Performance Monitor Users", BUILTIN, 5, 2, {32, 558}, SidTypeAlias},
    {L"Performance Log Users", BUILTIN, 5, 2, {32, 559}, SidTypeAlias},
    {L"Distributed COM Users", BUILTIN, 5, 2, {32, 562}, SidTypeAlias},
    {L"IIS_IUSRS", BUILTIN, 5, 2, {32, 568}, SidTypeAlias},
    {L"Cryptographic Operators", BUILTIN, 5, 2, {32, 569}, SidTypeAlias},
    {L"Event Log Readers", BUILTIN, 5, 2, {32, 573}, SidTypeAlias},
    {L"Access Control Assistance Operators", BUILTIN, 5, 2, {32, 579}, SidTypeAlias},
    {L"Remote Management Users", BUILTIN, 5, 2, {32, 580}, SidTypeAlias},
    {L"Device Owners", BUILTIN, 5, 2, {32, 583}, SidTypeAlias},
    {L"ALL APPLICATION PACKAGES", L"APPLICATION PACKAGE AUTHORITY", 15, 2, {2, 1}, SidTypeWellKnownGroup},
    {L"ALL RESTRICTED APPLICATION PACKAGES", L"APPLICATION PACKAGE AUTHORITY", 15, 2, {2, 2}, SidTypeWellKnownGroup},
    {L"Untrusted Mandatory Level", LABEL, 16, 1, {0}, SidTypeLabel},
    {L"Low Mandatory Level", LABEL, 16, 1, {0x1000}, SidTypeLabel},
    {L"Medium Mandatory Level", LABEL, 16, 1, {0x2000}, SidTypeLabel},
    {L"Medium Plus Mandatory Level", LABEL, 16, 1, {0x2100}, SidTypeLabel},
    {L"High Mandatory Level", LABEL, 16, 1, {0x3000}, SidTypeLabel},
    {L"System Mandatory Level", LABEL, 16, 1, {0x4000}, SidTypeLabel},
    {L"Protected Process Mandatory Level", LABEL, 16, 1, {0x5000}, SidTypeLabel},
};

/* built in accounts of every machine, by relative id in the machine's domain. */
static const struct {
    DWORD Rid;
    PCWSTR Name;
    SID_NAME_USE Use;
} g_machine_accounts[] = {
    {500, L"Administrator", SidTypeUser},
    {501, L"Guest", SidTypeUser},
    {503, L"DefaultAccount", SidTypeUser},
    {504, L"WDAGUtilityAccount", SidTypeUser},
    {513, L"None", SidTypeGroup},
};

#define PROFILE_LIST L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList"

/*
 * local groups made by programs live in one key of the registry, each with its relative id, and a service has a sid of
 * its own that comes from its name: the five numbers of the sha-1 of the name in capitals.
 */
#define ALIAS_KEY L"SYSTEM\\CurrentControlSet\\Control\\Lsa\\MimeAliases"
#define NT_SERVICE_DOMAIN L"NT SERVICE"
#define SERVICES_KEY L"SYSTEM\\CurrentControlSet\\Services"

static BOOL alias_by_name(PCWSTR name, DWORD *rid)
{
    HKEY key;
    WCHAR path[300];
    if (wcslen(name) > 200 || wcschr(name, L'\\'))
        return FALSE;
    _snwprintf(path, RTL_NUMBER_OF(path), ALIAS_KEY L"\\%ls", name);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_QUERY_VALUE, &key))
        return FALSE;
    DWORD size = sizeof(*rid), type;
    BOOL found = !RegQueryValueExW(key, L"Rid", nullptr, &type, (BYTE *)rid, &size) && type == REG_DWORD;
    RegCloseKey(key);
    return found;
}

static BOOL alias_by_rid(DWORD rid, PWSTR name, DWORD chars)
{
    HKEY root;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, ALIAS_KEY, 0, KEY_READ, &root))
        return FALSE;
    BOOL found = FALSE;
    for (DWORD i = 0; !found; i++) {
        WCHAR group[256];
        DWORD n = RTL_NUMBER_OF(group);
        if (RegEnumKeyExW(root, i, group, &n, nullptr, nullptr, nullptr, nullptr))
            break;
        DWORD value;
        if (alias_by_name(group, &value) && value == rid && wcslen(group) < chars) {
            wcscpy(name, group);
            found = TRUE;
        }
    }
    RegCloseKey(root);
    return found;
}

/* S-1-5-80 and the five numbers. */
static void service_sid(PCWSTR service, SID *sid)
{
    WCHAR upper[256];
    SIZE_T n = wcslen(service);
    if (n >= RTL_NUMBER_OF(upper))
        n = RTL_NUMBER_OF(upper) - 1;
    for (SIZE_T i = 0; i < n; i++)
        upper[i] = towupper(service[i]);
    uint8_t digest[20];
    mime_hash(&mime_sha1, upper, n * sizeof(WCHAR), digest);
    AdvMakeSid(sid, 5, 1, 80);
    sid->SubAuthorityCount = 6;
    for (int i = 0; i < 5; i++)
        sid->SubAuthority[1 + i] = (DWORD)digest[4 * i] | (DWORD)digest[4 * i + 1] << 8 | (DWORD)digest[4 * i + 2] << 16 |
                                   (DWORD)digest[4 * i + 3] << 24;
}

/* the service a sid of the form S-1-5-80-... belongs to, by trying each service of the machine. */
static BOOL service_name_of(const SID *sid, PWSTR name, DWORD chars)
{
    if (sid->IdentifierAuthority.Value[5] != 5 || sid->SubAuthorityCount != 6 || sid->SubAuthority[0] != 80)
        return FALSE;
    HKEY root;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, SERVICES_KEY, 0, KEY_READ, &root))
        return FALSE;
    BOOL found = FALSE;
    for (DWORD i = 0; !found; i++) {
        WCHAR service[256];
        DWORD n = RTL_NUMBER_OF(service);
        if (RegEnumKeyExW(root, i, service, &n, nullptr, nullptr, nullptr, nullptr))
            break;
        BYTE candidate[SECURITY_MAX_SID_SIZE];
        service_sid(service, (SID *)candidate);
        if (AdvSidEqual(sid, (SID *)candidate) && wcslen(service) < chars) {
            wcscpy(name, service);
            found = TRUE;
        }
    }
    RegCloseKey(root);
    return found;
}

static BOOL is_machine_domain(const SID *sid)
{
    return sid->IdentifierAuthority.Value[5] == 5 && !sid->IdentifierAuthority.Value[0] &&
           !sid->IdentifierAuthority.Value[1] && !sid->IdentifierAuthority.Value[2] &&
           !sid->IdentifierAuthority.Value[3] && !sid->IdentifierAuthority.Value[4] && sid->SubAuthorityCount >= 4 &&
           sid->SubAuthority[0] == 21;
}

/* this machine's domain sid, S-1-5-21-a-b-c, from the user's sid or a registered profile. */
BOOL AdvMachineSid(void *storage)
{
    SID *out = storage;
    BYTE user[SECURITY_MAX_SID_SIZE];
    if (AdvCurrentUserSid(user) && is_machine_domain((SID *)user) && ((SID *)user)->SubAuthorityCount == 5) {
        memcpy(out, user, AdvSidLength((SID *)user));
        out->SubAuthorityCount = 4;
        return TRUE;
    }
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, PROFILE_LIST, 0, KEY_READ, &key))
        return FALSE;
    BOOL found = FALSE;
    WCHAR name[96];
    for (DWORD i = 0; !found; i++) {
        DWORD n = RTL_NUMBER_OF(name);
        if (RegEnumKeyExW(key, i, name, &n, nullptr, nullptr, nullptr, nullptr))
            break;
        PSID sid;
        if (!wcsncmp(name, L"S-1-5-21-", 9) && ConvertStringSidToSidW(name, &sid)) {
            if (((SID *)sid)->SubAuthorityCount == 5) {
                memcpy(out, sid, AdvSidLength(sid));
                out->SubAuthorityCount = 4;
                found = TRUE;
            }
            LocalFree(sid);
        }
    }
    RegCloseKey(key);
    return found;
}

static BOOL read_string(HKEY root, PCWSTR path, PCWSTR value, PWSTR out, DWORD chars)
{
    HKEY key;
    if (RegOpenKeyExW(root, path, 0, KEY_QUERY_VALUE, &key))
        return FALSE;
    DWORD type, size = (chars - 1) * sizeof(WCHAR);
    LONG error = RegQueryValueExW(key, value, nullptr, &type, (LPBYTE)out, &size);
    RegCloseKey(key);
    if (error || (type != REG_SZ && type != REG_EXPAND_SZ))
        return FALSE;
    out[size / sizeof(WCHAR)] = 0;
    return out[0] != 0;
}

/* a local user's name: what winlogon wrote, else the profile directory name. */
static BOOL profile_user_name(const SID *sid, PWSTR out, DWORD chars)
{
    PWSTR text;
    if (!ConvertSidToStringSidW((PSID)sid, &text))
        return FALSE;
    WCHAR path[160], profile[MAX_PATH];
    _snwprintf(path, RTL_NUMBER_OF(path), L"%ls\\Volatile Environment", text);
    BOOL found = read_string(HKEY_USERS, path, L"USERNAME", out, chars);
    if (!found) {
        _snwprintf(path, RTL_NUMBER_OF(path), PROFILE_LIST L"\\%ls", text);
        if (read_string(HKEY_LOCAL_MACHINE, path, L"ProfileImagePath", profile, RTL_NUMBER_OF(profile))) {
            PCWSTR base = wcsrchr(profile, L'\\');
            base = base ? base + 1 : profile;
            if (*base && wcslen(base) < chars) {
                wcscpy(out, base);
                found = TRUE;
            }
        }
    }
    LocalFree(text);
    return found;
}

static BOOL computer_name(PWSTR out, DWORD chars)
{
    DWORD n = chars;
    return GetComputerNameW(out, &n);
}

static BOOL copy_name(PWSTR out, DWORD chars, PCWSTR text)
{
    if (wcslen(text) >= chars)
        return FALSE;
    wcscpy(out, text);
    return TRUE;
}

BOOL AdvLookupSid(const SID *sid, PWSTR name, DWORD name_size, PWSTR domain, DWORD domain_size, SID_NAME_USE *use)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_well_known); i++) {
        const ACCOUNT *a = &g_well_known[i];
        BYTE storage[SECURITY_MAX_SID_SIZE];
        AdvMakeSid(storage, a->Authority, a->Count, a->Sub[0], a->Sub[1]);
        if (AdvSidEqual(sid, (SID *)storage)) {
            *use = a->Use;
            return copy_name(name, name_size, a->Name) && copy_name(domain, domain_size, a->Domain);
        }
    }
    if (service_name_of(sid, name, name_size)) {
        *use = SidTypeWellKnownGroup;
        return copy_name(domain, domain_size, NT_SERVICE_DOMAIN);
    }
    BYTE machine[SECURITY_MAX_SID_SIZE];
    if (!is_machine_domain(sid) || !AdvMachineSid((SID *)machine) ||
        memcmp(((SID *)machine)->SubAuthority, sid->SubAuthority, 16))
        return FALSE;
    if (!computer_name(domain, domain_size))
        return FALSE;
    if (sid->SubAuthorityCount == 4) {
        *use = SidTypeDomain;
        return copy_name(name, name_size, domain);
    }
    if (sid->SubAuthorityCount != 5)
        return FALSE;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_machine_accounts); i++) {
        if (g_machine_accounts[i].Rid == sid->SubAuthority[4]) {
            *use = g_machine_accounts[i].Use;
            return copy_name(name, name_size, g_machine_accounts[i].Name);
        }
    }
    if (alias_by_rid(sid->SubAuthority[4], name, name_size)) {
        *use = SidTypeAlias;
        return TRUE;
    }
    *use = SidTypeUser;
    return profile_user_name(sid, name, name_size);
}

BOOL AdvIsLocalSystem(LPCWSTR system)
{
    if (!system || !*system)
        return TRUE;
    while (*system == L'\\')
        system++;
    WCHAR computer[MAX_COMPUTERNAME_LENGTH + 1];
    return !_wcsicmp(system, L".") || !_wcsicmp(system, L"localhost") ||
           (computer_name(computer, RTL_NUMBER_OF(computer)) && !_wcsicmp(system, computer));
}

ADVAPI BOOL WINAPI LookupAccountSidW(LPCWSTR system, PSID sid, LPWSTR name, LPDWORD name_size, LPWSTR domain,
                                     LPDWORD domain_size, PSID_NAME_USE use)
{
    if (!sid || !name_size || !domain_size) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!AdvSidValid(sid)) {
        AdvSetError(ERROR_INVALID_SID);
        return FALSE;
    }
    if (!AdvIsLocalSystem(system)) {
        AdvSetError(RPC_S_SERVER_UNAVAILABLE);
        return FALSE;
    }
    WCHAR n[256], d[256];
    SID_NAME_USE u;
    if (!AdvLookupSid(sid, n, RTL_NUMBER_OF(n), d, RTL_NUMBER_OF(d), &u)) {
        AdvSetError(ERROR_NONE_MAPPED);
        return FALSE;
    }
    DWORD nl = (DWORD)wcslen(n), dl = (DWORD)wcslen(d);
    if (!name || *name_size <= nl || !domain || *domain_size <= dl) {
        *name_size = nl + 1;
        *domain_size = dl + 1;
        AdvSetError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    memcpy(name, n, (nl + 1) * sizeof(WCHAR));
    memcpy(domain, d, (dl + 1) * sizeof(WCHAR));
    *name_size = nl;
    *domain_size = dl;
    if (use)
        *use = u;
    return TRUE;
}

/* sizes count ansi bytes, with the same rules as the wide function. */
ADVAPI BOOL WINAPI LookupAccountSidA(LPCSTR system, PSID sid, LPSTR name, LPDWORD name_size, LPSTR domain,
                                     LPDWORD domain_size, PSID_NAME_USE use)
{
    if (!sid || !name_size || !domain_size) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PWSTR s = AdvAnsiToWide(system);
    WCHAR n[256], d[256];
    DWORD nn = RTL_NUMBER_OF(n), dn = RTL_NUMBER_OF(d);
    SID_NAME_USE u;
    BOOL ok = (!system || s) && LookupAccountSidW(s, sid, n, &nn, d, &dn, &u);
    AdvFree(s);
    if (!ok)
        return FALSE;
    int nl = WideCharToMultiByte(CP_ACP, 0, n, -1, nullptr, 0, nullptr, nullptr);
    int dl = WideCharToMultiByte(CP_ACP, 0, d, -1, nullptr, 0, nullptr, nullptr);
    if (!name || *name_size < (DWORD)nl || !domain || *domain_size < (DWORD)dl) {
        *name_size = nl;
        *domain_size = dl;
        AdvSetError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    WideCharToMultiByte(CP_ACP, 0, n, -1, name, nl, nullptr, nullptr);
    WideCharToMultiByte(CP_ACP, 0, d, -1, domain, dl, nullptr, nullptr);
    *name_size = nl - 1;
    *domain_size = dl - 1;
    if (use)
        *use = u;
    return TRUE;
}

/* the name may be qualified with its domain. */
static BOOL lookup_name(PCWSTR account, SID *sid, PWSTR domain, DWORD domain_size, SID_NAME_USE *use)
{
    PCWSTR slash = wcschr(account, L'\\');
    WCHAR wanted_domain[256] = {};
    PCWSTR name = account;
    if (slash) {
        if ((SIZE_T)(slash - account) >= RTL_NUMBER_OF(wanted_domain))
            return FALSE;
        memcpy(wanted_domain, account, (slash - account) * sizeof(WCHAR));
        name = slash + 1;
    }
    if (slash && !_wcsicmp(wanted_domain, NT_SERVICE_DOMAIN) && *name) {
        /* the account of any service name resolves, whether or not the service is installed yet. */
        if (wcslen(name) > 200 || wcschr(name, L'\\'))
            return FALSE;
        service_sid(name, sid);
        *use = SidTypeWellKnownGroup;
        return copy_name(domain, domain_size, NT_SERVICE_DOMAIN);
    }
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_well_known); i++) {
        const ACCOUNT *a = &g_well_known[i];
        if (_wcsicmp(name, a->Name) || (slash && _wcsicmp(wanted_domain, a->Domain)))
            continue;
        AdvMakeSid(sid, a->Authority, a->Count, a->Sub[0], a->Sub[1]);
        *use = a->Use;
        return copy_name(domain, domain_size, a->Domain);
    }
    WCHAR computer[MAX_COMPUTERNAME_LENGTH + 1];
    if (!AdvMachineSid(sid) || !computer_name(computer, RTL_NUMBER_OF(computer)))
        return FALSE;
    if (slash && _wcsicmp(wanted_domain, computer) && _wcsicmp(wanted_domain, L"."))
        return FALSE;
    if (!copy_name(domain, domain_size, computer))
        return FALSE;
    if (!_wcsicmp(name, computer)) {
        *use = SidTypeDomain;
        return TRUE;
    }
    sid->SubAuthorityCount = 5;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_machine_accounts); i++) {
        if (!_wcsicmp(name, g_machine_accounts[i].Name)) {
            sid->SubAuthority[4] = g_machine_accounts[i].Rid;
            *use = g_machine_accounts[i].Use;
            return TRUE;
        }
    }
    DWORD alias;
    if (alias_by_name(name, &alias)) {
        sid->SubAuthority[4] = alias;
        *use = SidTypeAlias;
        return TRUE;
    }
    /* local users with a profile. */
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, PROFILE_LIST, 0, KEY_READ, &key))
        return FALSE;
    BOOL found = FALSE;
    WCHAR text[96], user[256];
    for (DWORD i = 0; !found; i++) {
        DWORD n = RTL_NUMBER_OF(text);
        if (RegEnumKeyExW(key, i, text, &n, nullptr, nullptr, nullptr, nullptr))
            break;
        PSID candidate;
        if (!ConvertStringSidToSidW(text, &candidate))
            continue;
        if (((SID *)candidate)->SubAuthorityCount == 5 && !memcmp(((SID *)candidate)->SubAuthority, sid->SubAuthority, 16) &&
            profile_user_name(candidate, user, RTL_NUMBER_OF(user)) && !_wcsicmp(user, name)) {
            sid->SubAuthority[4] = ((SID *)candidate)->SubAuthority[4];
            *use = SidTypeUser;
            found = TRUE;
        }
        LocalFree(candidate);
    }
    RegCloseKey(key);
    return found;
}

ADVAPI BOOL WINAPI LookupAccountNameW(LPCWSTR system, LPCWSTR account, PSID sid, LPDWORD sid_size, LPWSTR domain,
                                      LPDWORD domain_size, PSID_NAME_USE use)
{
    if (!account || !sid_size || !domain_size) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!AdvIsLocalSystem(system)) {
        AdvSetError(RPC_S_SERVER_UNAVAILABLE);
        return FALSE;
    }
    BYTE found[SECURITY_MAX_SID_SIZE];
    WCHAR d[256];
    SID_NAME_USE u;
    if (!lookup_name(account, (SID *)found, d, RTL_NUMBER_OF(d), &u)) {
        AdvSetError(ERROR_NONE_MAPPED);
        return FALSE;
    }
    DWORD length = AdvSidLength((SID *)found), dl = (DWORD)wcslen(d);
    if (!sid || *sid_size < length || !domain || *domain_size <= dl) {
        *sid_size = length;
        *domain_size = dl + 1;
        AdvSetError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    memcpy(sid, found, length);
    memcpy(domain, d, (dl + 1) * sizeof(WCHAR));
    *sid_size = length;
    *domain_size = dl;
    if (use)
        *use = u;
    return TRUE;
}

ADVAPI BOOL WINAPI LookupAccountNameA(LPCSTR system, LPCSTR account, PSID sid, LPDWORD sid_size, LPSTR domain,
                                      LPDWORD domain_size, PSID_NAME_USE use)
{
    if (!account || !sid_size || !domain_size) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PWSTR s = AdvAnsiToWide(system), a = AdvAnsiToWide(account);
    WCHAR d[256];
    DWORD dn = RTL_NUMBER_OF(d);
    BYTE found[SECURITY_MAX_SID_SIZE];
    DWORD length = sizeof(found);
    SID_NAME_USE u;
    BOOL ok = (!system || s) && a && LookupAccountNameW(s, a, found, &length, d, &dn, &u);
    AdvFree(s);
    AdvFree(a);
    if (!ok)
        return FALSE;
    int dl = WideCharToMultiByte(CP_ACP, 0, d, -1, nullptr, 0, nullptr, nullptr);
    if (!sid || *sid_size < length || !domain || *domain_size < (DWORD)dl) {
        *sid_size = length;
        *domain_size = dl;
        AdvSetError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    memcpy(sid, found, length);
    WideCharToMultiByte(CP_ACP, 0, d, -1, domain, dl, nullptr, nullptr);
    *sid_size = length;
    *domain_size = dl - 1;
    if (use)
        *use = u;
    return TRUE;
}

/* the user name without its domain; sizes count the terminator. */
ADVAPI BOOL WINAPI GetUserNameW(LPWSTR buffer, LPDWORD size)
{
    if (!size) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    TOKEN_USER *user = AdvQueryTokenUser(nullptr);
    WCHAR name[256], domain[256];
    SID_NAME_USE use;
    BOOL found = user && AdvLookupSid(user->User.Sid, name, RTL_NUMBER_OF(name), domain, RTL_NUMBER_OF(domain), &use);
    AdvFree(user);
    if (!found) {
        AdvSetError(ERROR_NONE_MAPPED);
        return FALSE;
    }
    DWORD n = (DWORD)wcslen(name) + 1;
    if (!buffer || *size < n) {
        *size = n;
        AdvSetError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    memcpy(buffer, name, n * sizeof(WCHAR));
    *size = n;
    return TRUE;
}

ADVAPI BOOL WINAPI GetUserNameA(LPSTR buffer, LPDWORD size)
{
    if (!size) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR name[256];
    DWORD chars = RTL_NUMBER_OF(name);
    if (!GetUserNameW(name, &chars))
        return FALSE;
    DWORD n = (DWORD)WideCharToMultiByte(CP_ACP, 0, name, -1, nullptr, 0, nullptr, nullptr);
    if (!buffer || *size < n) {
        *size = n;
        AdvSetError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    WideCharToMultiByte(CP_ACP, 0, name, -1, buffer, n, nullptr, nullptr);
    *size = n;
    return TRUE;
}
