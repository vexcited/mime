/*
 * the lookup interface of the lsa over ncalrpc:[lsapolicylookup], which genuine sechost's LsaLookupOpenLocalPolicy,
 * LsaLookupGetDomainInfo and LsaLookupClose speak; windows serves it from lsass, mime from the manager, like the rest
 * of what lsass does here, see lsa.c. answers measured on windows.
 */
#include "services.h"
#include <mime/win32/rpc.h>
#include "services/lsalookup.h"

#define LSA_POLICY_MAGIC 0x4c4f504c /* "LPOL" */
#define STATUS_INVALID_HANDLE_ 0xC0000008
#define STATUS_INVALID_INFO_CLASS_ 0xC0000003
#define STATUS_NO_MEMORY_ 0xC0000017
#define STATUS_NOT_IMPLEMENTED_ 0xC0000002
#define STATUS_SOME_NOT_MAPPED_ 0x00000107
#define STATUS_NONE_MAPPED_ 0xC0000073
#define PROFILE_LIST L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList"

typedef struct LSA_POLICY {
    DWORD Magic;
    DWORD Access;
} LSA_POLICY;

static LSA_POLICY *policy_of(LSAPR_HANDLE handle)
{
    LSA_POLICY *p = handle;
    return p && p->Magic == LSA_POLICY_MAGIC ? p : nullptr;
}

/* the client end binds itself; these serve the generic handle of the description. */
handle_t WINAPI LSAPR_SERVER_NAME_bind(LSAPR_SERVER_NAME name)
{
    RPC_WSTR text = nullptr;
    handle_t binding = nullptr;
    if (RpcStringBindingComposeW(nullptr, (RPC_WSTR)L"ncalrpc", nullptr, (RPC_WSTR)L"lsapolicylookup", nullptr,
                                 &text) == RPC_S_OK) {
        RpcBindingFromStringBindingW(text, &binding);
        RpcStringFreeW(&text);
    }
    return binding;
}

void WINAPI LSAPR_SERVER_NAME_unbind(LSAPR_SERVER_NAME name, handle_t binding)
{
    RpcBindingFree(&binding);
}

long WINAPI lsalookup_LsarOpenPolicy_s(LSAPR_SERVER_NAME SystemName, LSAPR_OBJECT_ATTRIBUTES *ObjectAttributes,
                                       unsigned long DesiredAccess, LSAPR_HANDLE *PolicyHandle)
{
    LSA_POLICY *p = SvcAllocZero(sizeof(*p));
    if (!p)
        return (long)STATUS_NO_MEMORY_;
    p->Magic = LSA_POLICY_MAGIC;
    p->Access = DesiredAccess;
    *PolicyHandle = p;
    return 0;
}

long WINAPI lsalookup_LsarClose_s(LSAPR_HANDLE *ObjectHandle)
{
    LSA_POLICY *p = policy_of(*ObjectHandle);
    if (!p)
        return (long)STATUS_INVALID_HANDLE_;
    p->Magic = 0;
    SvcFree(p);
    *ObjectHandle = nullptr;
    return 0;
}

void WINAPI LSAPR_HANDLE_rundown(void *handle)
{
    LSA_POLICY *p = policy_of(handle);
    if (p) {
        p->Magic = 0;
        SvcFree(p);
    }
}

long WINAPI lsalookup_LsarLookupUserAccountType_s(handle_t h)
{
    return (long)STATUS_NOT_IMPLEMENTED_;
}

long WINAPI lsalookup_LsarManageSidNameMapping_s(handle_t h)
{
    return (long)STATUS_NOT_IMPLEMENTED_;
}

/* a string the stubs free with MIDL_user_free, MaximumLength counting the terminator as windows has it. */
static BOOL set_string(RPC_UNICODE_STRING *out, const WCHAR *text)
{
    memset(out, 0, sizeof(*out));
    if (!text)
        return TRUE;
    size_t n = wcslen(text);
    out->Buffer = MIDL_user_allocate((n + 1) * sizeof(WCHAR));
    if (!out->Buffer)
        return FALSE;
    memcpy(out->Buffer, text, (n + 1) * sizeof(WCHAR));
    out->Length = (unsigned short)(n * sizeof(WCHAR));
    out->MaximumLength = (unsigned short)((n + 1) * sizeof(WCHAR));
    return TRUE;
}

/*
 * the sid of the account domain, the machine's: the sid of a local account without its last part. the kernel writes
 * the profile of the machine's user under ProfileList at boot, see guest/ntos/cm/boot.c.
 */
static RPC_SID *domain_sid(void)
{
    HKEY list;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, PROFILE_LIST, 0, KEY_READ, &list))
        return nullptr;
    RPC_SID *out = nullptr;
    WCHAR name[96];
    for (DWORD i = 0; !out; i++) {
        DWORD chars = RTL_NUMBER_OF(name);
        if (RegEnumKeyExW(list, i, name, &chars, nullptr, nullptr, nullptr, nullptr))
            break;
        if (wcsncmp(name, L"S-1-5-21-", 9))
            continue;
        BYTE sid[68];
        DWORD size = sizeof(sid), type;
        if (RegGetValueW(list, name, L"Sid", RRF_RT_REG_BINARY, &type, sid, &size) || size < 12 || sid[1] < 2)
            continue;
        BYTE count = (BYTE)(sid[1] - 1);
        out = MIDL_user_allocate(8 + count * sizeof(DWORD));
        if (!out)
            break;
        out->Revision = sid[0];
        out->SubAuthorityCount = count;
        memcpy(out->IdentifierAuthority.Value, sid + 2, 6);
        memcpy(out->SubAuthority, sid + 8, count * sizeof(DWORD));
    }
    RegCloseKey(list);
    return out;
}

/* the sid and the name of the machine's user, from the profile the kernel writes at boot. */
BOOL LsaMachineUser(BYTE sid[68], WCHAR name[64])
{
    HKEY list;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, PROFILE_LIST, 0, KEY_READ, &list))
        return FALSE;
    BOOL found = FALSE;
    WCHAR key[96], path[MAX_PATH];
    for (DWORD i = 0; !found; i++) {
        DWORD chars = RTL_NUMBER_OF(key);
        if (RegEnumKeyExW(list, i, key, &chars, nullptr, nullptr, nullptr, nullptr))
            break;
        if (wcsncmp(key, L"S-1-5-21-", 9))
            continue;
        DWORD size = 68, type;
        if (RegGetValueW(list, key, L"Sid", RRF_RT_REG_BINARY, &type, sid, &size) || size < 12 || sid[1] < 2 ||
            size < 8 + sid[1] * sizeof(DWORD))
            continue;
        size = sizeof(path);
        if (RegGetValueW(list, key, L"ProfileImagePath", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, &type,
                         path, &size))
            continue;
        const WCHAR *leaf = wcsrchr(path, L'\\');
        leaf = leaf ? leaf + 1 : path;
        if (!*leaf || wcslen(leaf) >= 64)
            continue;
        wcscpy(name, leaf);
        found = TRUE;
    }
    RegCloseKey(list);
    return found;
}

/* the accounts every windows knows, with the names of an english one. the domain's sid is the sid without its end. */
typedef struct KNOWN_ACCOUNT {
    BYTE Authority;
    BYTE Count;
    DWORD Sub[2];
    const WCHAR *Name;
    const WCHAR *Domain;
    BYTE Use;
} KNOWN_ACCOUNT;

#define NT_AUTHORITY L"NT AUTHORITY"
#define BUILTIN L"BUILTIN"
#define LABEL L"Mandatory Label"
#define PACKAGES L"APPLICATION PACKAGE AUTHORITY"

static const KNOWN_ACCOUNT g_known[] = {
    {0, 1, {0}, L"NULL SID", L"", 5},
    {1, 1, {0}, L"Everyone", L"", 5},
    {2, 1, {0}, L"LOCAL", L"", 5},
    {2, 1, {1}, L"CONSOLE LOGON", L"", 5},
    {3, 1, {0}, L"CREATOR OWNER", L"", 5},
    {3, 1, {1}, L"CREATOR GROUP", L"", 5},
    {3, 1, {4}, L"OWNER RIGHTS", L"", 5},
    {5, 1, {1}, L"DIALUP", NT_AUTHORITY, 5},
    {5, 1, {2}, L"NETWORK", NT_AUTHORITY, 5},
    {5, 1, {3}, L"BATCH", NT_AUTHORITY, 5},
    {5, 1, {4}, L"INTERACTIVE", NT_AUTHORITY, 5},
    {5, 1, {6}, L"SERVICE", NT_AUTHORITY, 5},
    {5, 1, {7}, L"ANONYMOUS LOGON", NT_AUTHORITY, 5},
    {5, 1, {10}, L"SELF", NT_AUTHORITY, 5},
    {5, 1, {11}, L"Authenticated Users", NT_AUTHORITY, 5},
    {5, 1, {12}, L"RESTRICTED", NT_AUTHORITY, 5},
    {5, 1, {13}, L"TERMINAL SERVER USER", NT_AUTHORITY, 5},
    {5, 1, {14}, L"REMOTE INTERACTIVE LOGON", NT_AUTHORITY, 5},
    {5, 1, {15}, L"This Organization", NT_AUTHORITY, 5},
    {5, 1, {17}, L"IUSR", NT_AUTHORITY, 5},
    {5, 1, {18}, L"SYSTEM", NT_AUTHORITY, 5},
    {5, 1, {19}, L"LOCAL SERVICE", NT_AUTHORITY, 5},
    {5, 1, {20}, L"NETWORK SERVICE", NT_AUTHORITY, 5},
    {5, 1, {32}, BUILTIN, BUILTIN, 3},
    {5, 2, {32, 544}, L"Administrators", BUILTIN, 4},
    {5, 2, {32, 545}, L"Users", BUILTIN, 4},
    {5, 2, {32, 546}, L"Guests", BUILTIN, 4},
    {5, 2, {32, 547}, L"Power Users", BUILTIN, 4},
    {5, 2, {32, 551}, L"Backup Operators", BUILTIN, 4},
    {5, 2, {32, 555}, L"Remote Desktop Users", BUILTIN, 4},
    {5, 2, {32, 558}, L"Performance Monitor Users", BUILTIN, 4},
    {5, 2, {32, 559}, L"Performance Log Users", BUILTIN, 4},
    {5, 2, {32, 562}, L"Distributed COM Users", BUILTIN, 4},
    {5, 2, {32, 568}, L"IIS_IUSRS", BUILTIN, 4},
    {5, 2, {32, 573}, L"Event Log Readers", BUILTIN, 4},
    {15, 2, {2, 1}, L"ALL APPLICATION PACKAGES", PACKAGES, 5},
    {15, 2, {2, 2}, L"ALL RESTRICTED APPLICATION PACKAGES", PACKAGES, 5},
    {16, 1, {0}, L"Untrusted Mandatory Level", LABEL, 10},
    {16, 1, {4096}, L"Low Mandatory Level", LABEL, 10},
    {16, 1, {8192}, L"Medium Mandatory Level", LABEL, 10},
    {16, 1, {8448}, L"Medium Plus Mandatory Level", LABEL, 10},
    {16, 1, {12288}, L"High Mandatory Level", LABEL, 10},
    {16, 1, {16384}, L"System Mandatory Level", LABEL, 10},
    {16, 1, {20480}, L"Protected Process Mandatory Level", LABEL, 10},
};

/* one answer of a lookup before it is copied out: the sid is the account's, the domain's is it cut by Cut parts. */
typedef struct LOOKUP_ANSWER {
    BYTE Sid[68];
    BYTE Cut;
    BYTE Use;
    const WCHAR *Name;
    const WCHAR *Domain;
} LOOKUP_ANSWER;

typedef struct LOOKUP_STATE {
    BYTE UserSid[68];
    WCHAR User[64];
    WCHAR Computer[MAX_COMPUTERNAME_LENGTH + 1];
    BOOL HasUser;
} LOOKUP_STATE;

static void lookup_begin(LOOKUP_STATE *state)
{
    DWORD n = RTL_NUMBER_OF(state->Computer);
    if (!GetComputerNameW(state->Computer, &n))
        state->Computer[0] = 0;
    state->HasUser = LsaMachineUser(state->UserSid, state->User);
}

static void known_sid(const KNOWN_ACCOUNT *k, BYTE sid[68])
{
    memset(sid, 0, 8);
    sid[0] = 1;
    sid[1] = k->Count;
    sid[7] = k->Authority;
    memcpy(sid + 8, k->Sub, k->Count * sizeof(DWORD));
}

static void answer_known(LOOKUP_ANSWER *a, const KNOWN_ACCOUNT *k)
{
    known_sid(k, a->Sid);
    a->Cut = k->Use == 3 ? 0 : 1;
    a->Use = k->Use;
    a->Name = k->Name;
    a->Domain = k->Domain;
}

static BOOL answer_sid(const LOOKUP_STATE *state, const RPC_SID *sid, LOOKUP_ANSWER *a)
{
    if (!sid || sid->Revision != 1 || sid->SubAuthorityCount > 15)
        return FALSE;
    DWORD size = 8 + sid->SubAuthorityCount * sizeof(DWORD);
    BYTE known[68];
    for (size_t i = 0; i < RTL_NUMBER_OF(g_known); i++) {
        known_sid(&g_known[i], known);
        if (known[1] == sid->SubAuthorityCount && !memcmp(known, sid, size)) {
            answer_known(a, &g_known[i]);
            return TRUE;
        }
    }
    if (!state->HasUser)
        return FALSE;
    BYTE parts = state->UserSid[1];
    if (sid->SubAuthorityCount == parts && !memcmp((const BYTE *)sid + 2, state->UserSid + 2, size - 2)) {
        memcpy(a->Sid, state->UserSid, size);
        a->Cut = 1;
        a->Use = 1;
        a->Name = state->User;
        a->Domain = state->Computer;
        return TRUE;
    }
    if (sid->SubAuthorityCount == parts - 1 && !memcmp((const BYTE *)sid + 2, state->UserSid + 2, size - 2)) {
        memcpy(a->Sid, sid, size);
        a->Cut = 0;
        a->Use = 3;
        a->Name = state->Computer;
        a->Domain = state->Computer;
        return TRUE;
    }
    return FALSE;
}

static BOOL answer_name(const LOOKUP_STATE *state, const RPC_UNICODE_STRING *name, LOOKUP_ANSWER *a)
{
    WCHAR text[160];
    size_t n = name->Length / sizeof(WCHAR);
    if (!name->Buffer || !n || n >= RTL_NUMBER_OF(text))
        return FALSE;
    memcpy(text, name->Buffer, n * sizeof(WCHAR));
    text[n] = 0;
    WCHAR *account = wcschr(text, L'\\');
    const WCHAR *domain = nullptr;
    if (account) {
        *account++ = 0;
        domain = text;
    } else {
        account = text;
    }
    for (size_t i = 0; i < RTL_NUMBER_OF(g_known); i++) {
        const KNOWN_ACCOUNT *k = &g_known[i];
        if (_wcsicmp(account, k->Name) || (domain && _wcsicmp(domain, k->Domain)))
            continue;
        answer_known(a, k);
        return TRUE;
    }
    if (!state->HasUser || (domain && _wcsicmp(domain, state->Computer)))
        return FALSE;
    BYTE parts = state->UserSid[1];
    if (!_wcsicmp(account, state->User)) {
        memcpy(a->Sid, state->UserSid, 8 + parts * sizeof(DWORD));
        a->Cut = 1;
        a->Use = 1;
        a->Name = state->User;
        a->Domain = state->Computer;
        return TRUE;
    }
    if (!domain && !_wcsicmp(account, state->Computer)) {
        memcpy(a->Sid, state->UserSid, 8 + (parts - 1) * sizeof(DWORD));
        a->Sid[1] = (BYTE)(parts - 1);
        a->Cut = 0;
        a->Use = 3;
        a->Name = state->Computer;
        a->Domain = state->Computer;
        return TRUE;
    }
    return FALSE;
}

static RPC_SID *copy_sid(const BYTE *sid, BYTE cut)
{
    BYTE count = (BYTE)(sid[1] - cut);
    RPC_SID *out = MIDL_user_allocate(8 + count * sizeof(DWORD));
    if (!out)
        return nullptr;
    memcpy(out, sid, 8 + count * sizeof(DWORD));
    out->SubAuthorityCount = count;
    return out;
}

/* the place of the answer's domain in the list, added when it is not there; -1 without memory. */
static long domain_index(LSAPR_REFERENCED_DOMAIN_LIST *list, const LOOKUP_ANSWER *a)
{
    for (unsigned long i = 0; i < list->Entries; i++) {
        const RPC_UNICODE_STRING *have = &list->Domains[i].Name;
        size_t n = wcslen(a->Domain);
        if (have->Length == n * sizeof(WCHAR) && (!n || !memcmp(have->Buffer, a->Domain, have->Length)))
            return (long)i;
    }
    LSAPR_TRUST_INFORMATION *d = &list->Domains[list->Entries];
    d->Sid = copy_sid(a->Sid, a->Cut);
    if (!d->Sid || !set_string(&d->Name, a->Domain))
        return -1;
    return (long)list->Entries++;
}

static LSAPR_REFERENCED_DOMAIN_LIST *new_domain_list(unsigned long count)
{
    LSAPR_REFERENCED_DOMAIN_LIST *list = MIDL_user_allocate(sizeof(*list));
    if (!list)
        return nullptr;
    memset(list, 0, sizeof(*list));
    if (count) {
        list->Domains = MIDL_user_allocate(count * sizeof(*list->Domains));
        if (!list->Domains) {
            MIDL_user_free(list);
            return nullptr;
        }
        memset(list->Domains, 0, count * sizeof(*list->Domains));
    }
    list->MaxEntries = 32;
    return list;
}

static long lookup_status(unsigned long mapped, unsigned long count)
{
    if (mapped == count)
        return 0;
    return mapped ? (long)STATUS_SOME_NOT_MAPPED_ : (long)STATUS_NONE_MAPPED_;
}

static WCHAR *put_number(WCHAR *out, const WCHAR *end, DWORD value)
{
    WCHAR digits[10];
    int n = 0;
    do
        digits[n++] = (WCHAR)(L'0' + value % 10);
    while (value /= 10);
    while (n && out < end)
        *out++ = digits[--n];
    return out;
}

/* the text of a sid, which windows gives as the name of one it does not know. */
static void sid_text(const RPC_SID *sid, WCHAR *out, size_t chars)
{
    const WCHAR *end = out + chars - 1;
    WCHAR *p = out;
    if (sid && sid->SubAuthorityCount <= 15) {
        const BYTE *a = sid->IdentifierAuthority.Value;
        *p++ = L'S';
        *p++ = L'-';
        p = put_number(p, end, sid->Revision);
        *p++ = L'-';
        p = put_number(p, end, (DWORD)a[2] << 24 | (DWORD)a[3] << 16 | (DWORD)a[4] << 8 | a[5]);
        for (BYTE i = 0; i < sid->SubAuthorityCount && p < end; i++) {
            *p++ = L'-';
            p = put_number(p, end, sid->SubAuthority[i]);
        }
    }
    *p = 0;
}

long WINAPI lsalookup_LsarLookupSids_s(LSAPR_HANDLE PolicyHandle, LSAPR_SID_ENUM_BUFFER *SidEnumBuffer,
                                       LSAPR_REFERENCED_DOMAIN_LIST **ReferencedDomains,
                                       LSAPR_TRANSLATED_NAMES_EX *TranslatedNames, unsigned long *MappedCount,
                                       unsigned long LookupOptions, unsigned long ClientRevision)
{
    unsigned long count = SidEnumBuffer->SidInfo ? SidEnumBuffer->Entries : 0;
    *ReferencedDomains = nullptr;
    *MappedCount = 0;
    TranslatedNames->Entries = 0;
    TranslatedNames->Names = nullptr;
    LSAPR_REFERENCED_DOMAIN_LIST *list = new_domain_list(count);
    if (!list)
        return (long)STATUS_NO_MEMORY_;
    *ReferencedDomains = list;
    if (!count)
        return 0;
    LSAPR_TRANSLATED_NAME_EX *names = MIDL_user_allocate(count * sizeof(*names));
    if (!names)
        return (long)STATUS_NO_MEMORY_;
    memset(names, 0, count * sizeof(*names));
    TranslatedNames->Names = names;
    TranslatedNames->Entries = count;
    LOOKUP_STATE state;
    lookup_begin(&state);
    for (unsigned long i = 0; i < count; i++) {
        LOOKUP_ANSWER a;
        WCHAR text[192];
        names[i].Use = LsaprSidTypeUnknown;
        names[i].DomainIndex = -1;
        if (!answer_sid(&state, SidEnumBuffer->SidInfo[i].Sid, &a)) {
            sid_text(SidEnumBuffer->SidInfo[i].Sid, text, RTL_NUMBER_OF(text));
            if (!set_string(&names[i].Name, text))
                return (long)STATUS_NO_MEMORY_;
            continue;
        }
        long index = domain_index(list, &a);
        if (index < 0 || !set_string(&names[i].Name, a.Name))
            return (long)STATUS_NO_MEMORY_;
        names[i].Use = (LSAPR_SID_NAME_USE)a.Use;
        names[i].DomainIndex = index;
        ++*MappedCount;
    }
    return lookup_status(*MappedCount, count);
}

long WINAPI lsalookup_LsarLookupNames_s(LSAPR_HANDLE PolicyHandle, unsigned long Count, RPC_UNICODE_STRING *Names,
                                        LSAPR_REFERENCED_DOMAIN_LIST **ReferencedDomains,
                                        LSAPR_TRANSLATED_SIDS_EX2 *TranslatedSids, unsigned long *MappedCount,
                                        unsigned long LookupOptions, unsigned long ClientRevision)
{
    *ReferencedDomains = nullptr;
    *MappedCount = 0;
    TranslatedSids->Entries = 0;
    TranslatedSids->Sids = nullptr;
    LSAPR_REFERENCED_DOMAIN_LIST *list = new_domain_list(Count);
    if (!list)
        return (long)STATUS_NO_MEMORY_;
    *ReferencedDomains = list;
    if (!Count)
        return 0;
    LSAPR_TRANSLATED_SID_EX2 *sids = MIDL_user_allocate(Count * sizeof(*sids));
    if (!sids)
        return (long)STATUS_NO_MEMORY_;
    memset(sids, 0, Count * sizeof(*sids));
    TranslatedSids->Sids = sids;
    TranslatedSids->Entries = Count;
    LOOKUP_STATE state;
    lookup_begin(&state);
    for (unsigned long i = 0; i < Count; i++) {
        LOOKUP_ANSWER a;
        sids[i].Use = LsaprSidTypeUnknown;
        sids[i].DomainIndex = -1;
        if (!answer_name(&state, &Names[i], &a))
            continue;
        long index = domain_index(list, &a);
        sids[i].Sid = copy_sid(a.Sid, 0);
        if (index < 0 || !sids[i].Sid)
            return (long)STATUS_NO_MEMORY_;
        sids[i].Use = (LSAPR_SID_NAME_USE)a.Use;
        sids[i].DomainIndex = index;
        ++*MappedCount;
    }
    return lookup_status(*MappedCount, Count);
}

/*
 * a machine outside a domain, as windows answers it: the account domain is the computer and the sid of its accounts,
 * the dns domain information only names the workgroup.
 */
long WINAPI lsalookup_LsarGetDomainInfo_s(LSAPR_HANDLE PolicyHandle, LSA_LOOKUP_DOMAIN_INFO_CLASS DomainInfoClass,
                                          LSAPR_LOOKUP_DOMAIN_INFO **DomainInfo)
{
    *DomainInfo = nullptr;
    if (!policy_of(PolicyHandle))
        return (long)STATUS_INVALID_HANDLE_;
    if (DomainInfoClass != AccountDomainInformation && DomainInfoClass != DnsDomainInformation)
        return (long)STATUS_INVALID_INFO_CLASS_;
    LSAPR_LOOKUP_DOMAIN_INFO *info = MIDL_user_allocate(sizeof(*info));
    if (!info)
        return (long)STATUS_NO_MEMORY_;
    memset(info, 0, sizeof(*info));
    BOOL ok;
    if (DomainInfoClass == AccountDomainInformation) {
        WCHAR computer[MAX_COMPUTERNAME_LENGTH + 1];
        DWORD n = RTL_NUMBER_OF(computer);
        if (!GetComputerNameW(computer, &n))
            computer[0] = 0;
        ok = set_string(&info->AccountDomainInfo.DomainName, computer);
        info->AccountDomainInfo.DomainSid = domain_sid();
        ok = ok && info->AccountDomainInfo.DomainSid;
    } else {
        ok = set_string(&info->DnsDomainInfo.Name, L"WORKGROUP");
    }
    if (!ok) {
        MIDL_user_free(info->AccountDomainInfo.DomainName.Buffer);
        MIDL_user_free(info->AccountDomainInfo.DomainSid);
        MIDL_user_free(info);
        return (long)STATUS_NO_MEMORY_;
    }
    *DomainInfo = info;
    return 0;
}

static RPC_STATUS RPC_ENTRY allow_local(RPC_IF_HANDLE interface, void *context)
{
    return RPC_S_OK;
}

/* D:(A;;0x1;;;WD)(A;;0x1;;;AC): every local caller connects and calls, app containers included. */
static const BYTE g_endpoint_sd[] = {
    0x01, 0x00, 0x04, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x34, 0x00, 0x02, 0x00, 0x00, 0x00,
    /* everyone */
    0x00, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00,
    /* all application packages */
    0x00, 0x00, 0x18, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x02, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
};

void LsaLookupStart(void)
{
    RPC_STATUS status = RpcServerUseProtseqEpW((RPC_WSTR)L"ncalrpc", RPC_C_PROTSEQ_MAX_REQS_DEFAULT,
                                               (RPC_WSTR)L"lsapolicylookup", (void *)g_endpoint_sd);
    if (!status)
        status = RpcServerRegisterIf3(lsalookup_s_ifspec, nullptr, nullptr,
                                      RPC_IF_AUTOLISTEN | RPC_IF_ALLOW_LOCAL_ONLY, RPC_C_LISTEN_MAX_CALLS_DEFAULT,
                                      (unsigned int)-1, allow_local, (void *)g_endpoint_sd);
    if (status)
        SvcLog("lsalookup: the rpc server did not start: %u", status);
}
