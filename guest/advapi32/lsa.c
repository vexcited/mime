/*
 * lsa lookups: names to sids and back, their domains, and the policy naming this machine's domain. there is no
 * lsass: the answers are those of LookupAccountSid and LookupAccountName, in the lsa structures. windows has these
 * in sechost.dll, which forwards them to lsass.
 */
#include "advapi32_private.h"

#define STATUS_NONE_MAPPED_ ((NTSTATUS)0xC0000073L)
#define STATUS_RPC_SERVER_UNAVAILABLE_ ((NTSTATUS)0xC0020017L)

typedef UNICODE_STRING LSA_UNICODE_STRING, *PLSA_UNICODE_STRING;
typedef PVOID LSA_HANDLE, *PLSA_HANDLE;

typedef struct _LSA_OBJECT_ATTRIBUTES {
    ULONG Length;
    HANDLE RootDirectory;
    PLSA_UNICODE_STRING ObjectName;
    ULONG Attributes;
    PVOID SecurityDescriptor;
    PVOID SecurityQualityOfService;
} LSA_OBJECT_ATTRIBUTES, *PLSA_OBJECT_ATTRIBUTES;

typedef struct _LSA_TRUST_INFORMATION {
    LSA_UNICODE_STRING Name;
    PSID Sid;
} LSA_TRUST_INFORMATION;

typedef struct _LSA_REFERENCED_DOMAIN_LIST {
    ULONG Entries;
    LSA_TRUST_INFORMATION *Domains;
} LSA_REFERENCED_DOMAIN_LIST;

typedef struct _LSA_TRANSLATED_SID2 {
    SID_NAME_USE Use;
    PSID Sid;
    LONG DomainIndex;
    ULONG Flags;
} LSA_TRANSLATED_SID2;

typedef struct _LSA_TRANSLATED_NAME {
    SID_NAME_USE Use;
    LSA_UNICODE_STRING Name;
    LONG DomainIndex;
} LSA_TRANSLATED_NAME;

/* the policy information classes answered here. */
#define POLICY_PRIMARY_DOMAIN_INFORMATION 3
#define POLICY_ACCOUNT_DOMAIN_INFORMATION 5
#define POLICY_LSA_SERVER_ROLE_INFORMATION 6
#define POLICY_DNS_DOMAIN_INFORMATION 12
#define POLICY_DNS_DOMAIN_INFORMATION_INT 13

/* policy handle specific rights. */
#define POLICY_VIEW_LOCAL_INFORMATION 0x1
#define POLICY_VIEW_AUDIT_INFORMATION 0x2
#define POLICY_GET_PRIVATE_INFORMATION 0x4
#define POLICY_LOOKUP_NAMES 0x800

typedef struct POLICY_OBJECT {
    ULONG Signature;
    ACCESS_MASK Access;
} POLICY_OBJECT;

#define POLICY_SIGNATURE 0x4C534150 /* "LSAP" */

ADVAPI BOOL WINAPI CheckTokenMembership(HANDLE token, PSID sid, PBOOL member);
ADVAPI BOOL WINAPI LookupAccountNameA(LPCSTR system, LPCSTR account, PSID sid, LPDWORD sid_size, LPSTR domain,
                                      LPDWORD domain_size, PSID_NAME_USE use);
ADVAPI BOOL WINAPI LookupAccountSidA(LPCSTR system, PSID sid, LPSTR name, LPDWORD name_size, LPSTR domain,
                                     LPDWORD domain_size, PSID_NAME_USE use);

/* ------------------------------------------------------------------ handles and memory */

/* lsa results are freed with LsaFreeMemory and come from the process heap. */
static PVOID lsa_alloc(SIZE_T size)
{
    return AdvAllocZero(size);
}

static POLICY_OBJECT *policy_of(LSA_HANDLE handle)
{
    POLICY_OBJECT *p = handle;
    return p && p->Signature == POLICY_SIGNATURE ? p : nullptr;
}

static BOOL is_administrator(void)
{
    BYTE storage[SECURITY_MAX_SID_SIZE];
    BOOL member = FALSE;
    AdvMakeSid(storage, 5, 2, 32, 544);
    return CheckTokenMembership(nullptr, (PSID)storage, &member) && member;
}

ADVAPI NTSTATUS WINAPI LsaOpenPolicy(PLSA_UNICODE_STRING system, PLSA_OBJECT_ATTRIBUTES attributes, ACCESS_MASK access,
                                     PLSA_HANDLE handle)
{
    if (!handle)
        return STATUS_INVALID_PARAMETER;
    *handle = nullptr;
    if (!attributes)
        return STATUS_INVALID_PARAMETER;
    if (system && system->Buffer && system->Length) {
        WCHAR name[260];
        ULONG chars = system->Length / sizeof(WCHAR);
        if (chars >= RTL_NUMBER_OF(name))
            return STATUS_RPC_SERVER_UNAVAILABLE_;
        memcpy(name, system->Buffer, chars * sizeof(WCHAR));
        name[chars] = 0;
        if (!AdvIsLocalSystem(name))
            return STATUS_RPC_SERVER_UNAVAILABLE_;
    }
    /* viewing local information and looking up names is open to all, the rest needs an administrator. */
    static const ACCESS_MASK policy_rights = 0x000007FF, everyone = POLICY_VIEW_LOCAL_INFORMATION | POLICY_LOOKUP_NAMES;
    static const ACCESS_MASK generic_write = 0x10000000 | 0x40000000; /* GENERIC_ALL, GENERIC_WRITE */
    if (((access & policy_rights & ~everyone) || (access & generic_write)) && !is_administrator())
        return STATUS_ACCESS_DENIED;
    POLICY_OBJECT *p = lsa_alloc(sizeof(*p));
    if (!p)
        return STATUS_NO_MEMORY;
    p->Signature = POLICY_SIGNATURE;
    p->Access = access;
    *handle = p;
    return STATUS_SUCCESS;
}

ADVAPI NTSTATUS WINAPI LsaClose(LSA_HANDLE handle)
{
    POLICY_OBJECT *p = policy_of(handle);
    if (!p)
        return STATUS_INVALID_HANDLE;
    p->Signature = 0;
    AdvFree(p);
    return STATUS_SUCCESS;
}

ADVAPI NTSTATUS WINAPI LsaFreeMemory(PVOID buffer)
{
    AdvFree(buffer);
    return STATUS_SUCCESS;
}

ADVAPI NTSTATUS WINAPI LsaLookupOpenLocalPolicy(PLSA_OBJECT_ATTRIBUTES attributes, ACCESS_MASK access, PLSA_HANDLE handle)
{
    return LsaOpenPolicy(nullptr, attributes, access, handle);
}

ADVAPI NTSTATUS WINAPI LsaLookupClose(LSA_HANDLE handle)
{
    return LsaClose(handle);
}

ADVAPI NTSTATUS WINAPI LsaLookupFreeMemory(PVOID buffer)
{
    return LsaFreeMemory(buffer);
}

/* ------------------------------------------------------------------ lookup domains */

#define MAX_DOMAINS 64

typedef struct DOMAINS {
    ULONG Count;
    struct {
        WCHAR Name[256];
        BYTE Sid[SECURITY_MAX_SID_SIZE];
        BOOL HasSid;
    } Entry[MAX_DOMAINS];
} DOMAINS;

/* an account's domain is its sid minus the last sub authority; a domain's own sid is itself. */
static BOOL domain_sid(const SID *sid, SID_NAME_USE use, BYTE *out)
{
    DWORD length = AdvSidLength(sid);
    memcpy(out, sid, length);
    if (use == SidTypeDomain)
        return TRUE;
    SID *domain = (SID *)out;
    if (!domain->SubAuthorityCount)
        return FALSE;
    domain->SubAuthorityCount--;
    return TRUE;
}

/* the domain's index in the list, added when new; -1 for accounts without one, like everyone or local. */
static LONG domain_index(DOMAINS *list, const WCHAR *name, const SID *sid, SID_NAME_USE use)
{
    if (!name[0])
        return -1;
    for (ULONG i = 0; i < list->Count; i++)
        if (!_wcsicmp(list->Entry[i].Name, name))
            return (LONG)i;
    if (list->Count == MAX_DOMAINS)
        return -1;
    ULONG i = list->Count++;
    SIZE_T n = wcslen(name);
    if (n >= RTL_NUMBER_OF(list->Entry[i].Name))
        n = RTL_NUMBER_OF(list->Entry[i].Name) - 1;
    memcpy(list->Entry[i].Name, name, n * sizeof(WCHAR));
    list->Entry[i].Name[n] = 0;
    list->Entry[i].HasSid = sid && domain_sid(sid, use, list->Entry[i].Sid);
    return (LONG)i;
}

static SIZE_T align4(SIZE_T n)
{
    return (n + 3) & ~(SIZE_T)3;
}

/* the list as lsa returns it: entries, then each domain's name and sid, in one block. */
static LSA_REFERENCED_DOMAIN_LIST *build_domains(const DOMAINS *list)
{
    SIZE_T size = sizeof(LSA_REFERENCED_DOMAIN_LIST) + list->Count * sizeof(LSA_TRUST_INFORMATION);
    for (ULONG i = 0; i < list->Count; i++) {
        size += align4(wcslen(list->Entry[i].Name) * sizeof(WCHAR));
        if (list->Entry[i].HasSid)
            size += align4(AdvSidLength((const SID *)list->Entry[i].Sid));
    }
    LSA_REFERENCED_DOMAIN_LIST *out = lsa_alloc(size);
    if (!out)
        return nullptr;
    out->Entries = list->Count;
    out->Domains = (LSA_TRUST_INFORMATION *)(out + 1);
    BYTE *at = (BYTE *)(out->Domains + list->Count);
    for (ULONG i = 0; i < list->Count; i++) {
        SIZE_T bytes = wcslen(list->Entry[i].Name) * sizeof(WCHAR);
        memcpy(at, list->Entry[i].Name, bytes);
        out->Domains[i].Name.Buffer = (PWSTR)at;
        out->Domains[i].Name.Length = out->Domains[i].Name.MaximumLength = (USHORT)bytes;
        at += align4(bytes);
        if (list->Entry[i].HasSid) {
            DWORD length = AdvSidLength((const SID *)list->Entry[i].Sid);
            memcpy(at, list->Entry[i].Sid, length);
            out->Domains[i].Sid = at;
            at += align4(length);
        }
    }
    return out;
}

/* ------------------------------------------------------------------ names to sids */

static NTSTATUS lookup_names(LSA_HANDLE handle, ULONG count, PLSA_UNICODE_STRING names,
                             LSA_REFERENCED_DOMAIN_LIST **domains_out, LSA_TRANSLATED_SID2 **sids_out)
{
    if (!domains_out || !sids_out)
        return STATUS_INVALID_PARAMETER;
    *domains_out = nullptr;
    *sids_out = nullptr;
    if (!policy_of(handle))
        return STATUS_INVALID_HANDLE;
    if (!count || !names)
        return STATUS_INVALID_PARAMETER;
    if (count > 0x10000)
        return STATUS_INVALID_PARAMETER;
    DOMAINS *list = AdvAllocZero(sizeof(*list));
    BYTE (*found)[SECURITY_MAX_SID_SIZE] = AdvAllocZero((SIZE_T)count * SECURITY_MAX_SID_SIZE);
    LSA_TRANSLATED_SID2 *temp = AdvAllocZero((SIZE_T)count * sizeof(*temp));
    if (!list || !found || !temp) {
        AdvFree(list);
        AdvFree(found);
        AdvFree(temp);
        return STATUS_NO_MEMORY;
    }
    ULONG mapped = 0;
    SIZE_T sid_bytes = 0;
    for (ULONG i = 0; i < count; i++) {
        temp[i].Use = SidTypeUnknown;
        temp[i].DomainIndex = -1;
        WCHAR name[300];
        ULONG chars = names[i].Length / sizeof(WCHAR);
        if (!names[i].Buffer || !chars || chars >= RTL_NUMBER_OF(name))
            continue;
        memcpy(name, names[i].Buffer, chars * sizeof(WCHAR));
        name[chars] = 0;
        WCHAR domain[256];
        DWORD sid_size = SECURITY_MAX_SID_SIZE, domain_size = RTL_NUMBER_OF(domain);
        SID_NAME_USE use;
        if (!LookupAccountNameW(nullptr, name, found[i], &sid_size, domain, &domain_size, &use))
            continue;
        temp[i].Use = use;
        temp[i].DomainIndex = domain_index(list, domain, (const SID *)found[i], use);
        temp[i].Sid = found[i];
        sid_bytes += align4(sid_size);
        mapped++;
    }
    NTSTATUS status = mapped == count ? STATUS_SUCCESS : mapped ? STATUS_SOME_NOT_MAPPED : STATUS_NONE_MAPPED_;
    LSA_REFERENCED_DOMAIN_LIST *referenced = build_domains(list);
    LSA_TRANSLATED_SID2 *sids = lsa_alloc((SIZE_T)count * sizeof(*sids) + sid_bytes);
    if (!referenced || !sids) {
        AdvFree(referenced);
        AdvFree(sids);
        status = STATUS_NO_MEMORY;
    } else {
        BYTE *at = (BYTE *)(sids + count);
        for (ULONG i = 0; i < count; i++) {
            sids[i] = temp[i];
            if (temp[i].Sid) {
                DWORD length = AdvSidLength((const SID *)found[i]);
                memcpy(at, found[i], length);
                sids[i].Sid = at;
                at += align4(length);
            }
        }
        *domains_out = referenced;
        *sids_out = sids;
    }
    AdvFree(list);
    AdvFree(found);
    AdvFree(temp);
    return status;
}

ADVAPI NTSTATUS WINAPI LsaLookupNames2(LSA_HANDLE handle, ULONG flags, ULONG count, PLSA_UNICODE_STRING names,
                                       LSA_REFERENCED_DOMAIN_LIST **domains, LSA_TRANSLATED_SID2 **sids)
{
    (void)flags;
    return lookup_names(handle, count, names, domains, sids);
}

ADVAPI NTSTATUS WINAPI LsaLookupTranslateNames(LSA_HANDLE handle, ULONG flags, ULONG count, PLSA_UNICODE_STRING names,
                                               LSA_REFERENCED_DOMAIN_LIST **domains, LSA_TRANSLATED_SID2 **sids)
{
    (void)flags;
    return lookup_names(handle, count, names, domains, sids);
}

/* ------------------------------------------------------------------ sids to names */

static NTSTATUS lookup_sids(LSA_HANDLE handle, ULONG count, PSID *sids, LSA_REFERENCED_DOMAIN_LIST **domains_out,
                            LSA_TRANSLATED_NAME **names_out)
{
    if (!domains_out || !names_out)
        return STATUS_INVALID_PARAMETER;
    *domains_out = nullptr;
    *names_out = nullptr;
    if (!policy_of(handle))
        return STATUS_INVALID_HANDLE;
    if (!count || !sids || count > 0x10000)
        return STATUS_INVALID_PARAMETER;
    DOMAINS *list = AdvAllocZero(sizeof(*list));
    WCHAR (*texts)[256] = AdvAllocZero((SIZE_T)count * sizeof(*texts));
    LSA_TRANSLATED_NAME *temp = AdvAllocZero((SIZE_T)count * sizeof(*temp));
    if (!list || !texts || !temp) {
        AdvFree(list);
        AdvFree(texts);
        AdvFree(temp);
        return STATUS_NO_MEMORY;
    }
    ULONG mapped = 0;
    SIZE_T text_bytes = 0;
    for (ULONG i = 0; i < count; i++) {
        temp[i].Use = SidTypeUnknown;
        temp[i].DomainIndex = -1;
        if (!sids[i] || !AdvSidValid(sids[i]))
            continue;
        WCHAR domain[256];
        DWORD name_size = RTL_NUMBER_OF(texts[i]), domain_size = RTL_NUMBER_OF(domain);
        SID_NAME_USE use;
        if (!LookupAccountSidW(nullptr, sids[i], texts[i], &name_size, domain, &domain_size, &use))
            continue;
        temp[i].Use = use;
        temp[i].DomainIndex = domain_index(list, domain, sids[i], use);
        text_bytes += align4(wcslen(texts[i]) * sizeof(WCHAR));
        mapped++;
    }
    NTSTATUS status = mapped == count ? STATUS_SUCCESS : mapped ? STATUS_SOME_NOT_MAPPED : STATUS_NONE_MAPPED_;
    LSA_REFERENCED_DOMAIN_LIST *referenced = build_domains(list);
    LSA_TRANSLATED_NAME *names = lsa_alloc((SIZE_T)count * sizeof(*names) + text_bytes);
    if (!referenced || !names) {
        AdvFree(referenced);
        AdvFree(names);
        status = STATUS_NO_MEMORY;
    } else {
        BYTE *at = (BYTE *)(names + count);
        for (ULONG i = 0; i < count; i++) {
            names[i] = temp[i];
            if (temp[i].Use != SidTypeUnknown) {
                SIZE_T bytes = wcslen(texts[i]) * sizeof(WCHAR);
                memcpy(at, texts[i], bytes);
                names[i].Name.Buffer = (PWSTR)at;
                names[i].Name.Length = names[i].Name.MaximumLength = (USHORT)bytes;
                at += align4(bytes);
            }
        }
        *domains_out = referenced;
        *names_out = names;
    }
    AdvFree(list);
    AdvFree(texts);
    AdvFree(temp);
    return status;
}

ADVAPI NTSTATUS WINAPI LsaLookupSids(LSA_HANDLE handle, ULONG count, PSID *sids, LSA_REFERENCED_DOMAIN_LIST **domains,
                                     LSA_TRANSLATED_NAME **names)
{
    return lookup_sids(handle, count, sids, domains, names);
}

ADVAPI NTSTATUS WINAPI LsaLookupSids2(LSA_HANDLE handle, ULONG options, ULONG count, PSID *sids,
                                      LSA_REFERENCED_DOMAIN_LIST **domains, LSA_TRANSLATED_NAME **names)
{
    (void)options;
    return lookup_sids(handle, count, sids, domains, names);
}

ADVAPI NTSTATUS WINAPI LsaLookupTranslateSids(LSA_HANDLE handle, ULONG count, PSID *sids,
                                              LSA_REFERENCED_DOMAIN_LIST **domains, LSA_TRANSLATED_NAME **names)
{
    return lookup_sids(handle, count, sids, domains, names);
}

/* ------------------------------------------------------------------ policy information */

typedef struct POLICY_ACCOUNT_DOMAIN_INFO {
    LSA_UNICODE_STRING DomainName;
    PSID DomainSid;
} POLICY_ACCOUNT_DOMAIN_INFO;

typedef struct POLICY_DNS_DOMAIN_INFO {
    LSA_UNICODE_STRING Name;
    LSA_UNICODE_STRING DnsDomainName;
    LSA_UNICODE_STRING DnsForestName;
    GUID DomainGuid;
    PSID Sid;
} POLICY_DNS_DOMAIN_INFO;

/* places a string after a structure, in the block holding both. */
static void put_string(LSA_UNICODE_STRING *s, BYTE **at, const WCHAR *text)
{
    SIZE_T bytes = wcslen(text) * sizeof(WCHAR);
    memcpy(*at, text, bytes);
    s->Buffer = (PWSTR)*at;
    s->Length = s->MaximumLength = (USHORT)bytes;
    *at += align4(bytes + sizeof(WCHAR));
}

ADVAPI NTSTATUS WINAPI LsaQueryInformationPolicy(LSA_HANDLE handle, ULONG klass, PVOID *buffer)
{
    POLICY_OBJECT *p = policy_of(handle);
    if (!p)
        return STATUS_INVALID_HANDLE;
    if (!buffer)
        return STATUS_INVALID_PARAMETER;
    *buffer = nullptr;
    WCHAR computer[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD chars = RTL_NUMBER_OF(computer);
    BYTE machine[SECURITY_MAX_SID_SIZE];
    switch (klass) {
    case POLICY_ACCOUNT_DOMAIN_INFORMATION: {
        /* the machine is the domain of its own accounts. */
        if (!(p->Access & (POLICY_VIEW_LOCAL_INFORMATION | 0x10000000 | 0x80000000)))
            return STATUS_ACCESS_DENIED;
        if (!GetComputerNameW(computer, &chars) || !AdvMachineSid(machine))
            return STATUS_UNSUCCESSFUL;
        SIZE_T size = sizeof(POLICY_ACCOUNT_DOMAIN_INFO) + align4((chars + 1) * sizeof(WCHAR)) + AdvSidLength((SID *)machine);
        POLICY_ACCOUNT_DOMAIN_INFO *info = lsa_alloc(size);
        if (!info)
            return STATUS_NO_MEMORY;
        BYTE *at = (BYTE *)(info + 1);
        put_string(&info->DomainName, &at, computer);
        memcpy(at, machine, AdvSidLength((SID *)machine));
        info->DomainSid = at;
        *buffer = info;
        return STATUS_SUCCESS;
    }
    case POLICY_PRIMARY_DOMAIN_INFORMATION: {
        /* a machine outside a domain is in a workgroup, which has no sid. */
        if (!(p->Access & (POLICY_VIEW_LOCAL_INFORMATION | 0x10000000 | 0x80000000)))
            return STATUS_ACCESS_DENIED;
        POLICY_ACCOUNT_DOMAIN_INFO *info = lsa_alloc(sizeof(*info) + 32);
        if (!info)
            return STATUS_NO_MEMORY;
        BYTE *at = (BYTE *)(info + 1);
        put_string(&info->DomainName, &at, L"WORKGROUP");
        *buffer = info;
        return STATUS_SUCCESS;
    }
    case POLICY_DNS_DOMAIN_INFORMATION:
    case POLICY_DNS_DOMAIN_INFORMATION_INT: {
        if (!(p->Access & (POLICY_VIEW_LOCAL_INFORMATION | 0x10000000 | 0x80000000)))
            return STATUS_ACCESS_DENIED;
        POLICY_DNS_DOMAIN_INFO *info = lsa_alloc(sizeof(*info) + 32);
        if (!info)
            return STATUS_NO_MEMORY;
        BYTE *at = (BYTE *)(info + 1);
        put_string(&info->Name, &at, L"WORKGROUP");
        *buffer = info;
        return STATUS_SUCCESS;
    }
    case POLICY_LSA_SERVER_ROLE_INFORMATION: {
        ULONG *role = lsa_alloc(sizeof(ULONG));
        if (!role)
            return STATUS_NO_MEMORY;
        *role = 3; /* PolicyServerRolePrimary */
        *buffer = role;
        return STATUS_SUCCESS;
    }
    default:
        /* the audit, quota, trust and other classes need the policy database. */
        return klass >= 1 && klass <= 14 ? STATUS_ACCESS_DENIED : STATUS_INVALID_PARAMETER;
    }
}

ADVAPI NTSTATUS WINAPI LsaSetInformationPolicy(LSA_HANDLE handle, ULONG klass, PVOID buffer)
{
    (void)klass;
    (void)buffer;
    return policy_of(handle) ? STATUS_ACCESS_DENIED : STATUS_INVALID_HANDLE;
}

/* the domain this machine's lookups answer for, as the lookup handle gives it. */
ADVAPI NTSTATUS WINAPI LsaLookupGetDomainInfo(LSA_HANDLE handle, ULONG klass, PVOID *info)
{
    return LsaQueryInformationPolicy(handle, klass, info);
}

/* ------------------------------------------------------------------ local account lookups */

ADVAPI BOOL WINAPI LookupAccountNameLocalW(LPCWSTR account, PSID sid, LPDWORD sid_size, LPWSTR domain,
                                           LPDWORD domain_size, PSID_NAME_USE use)
{
    return LookupAccountNameW(nullptr, account, sid, sid_size, domain, domain_size, use);
}

ADVAPI BOOL WINAPI LookupAccountNameLocalA(LPCSTR account, PSID sid, LPDWORD sid_size, LPSTR domain,
                                           LPDWORD domain_size, PSID_NAME_USE use)
{
    return LookupAccountNameA(nullptr, account, sid, sid_size, domain, domain_size, use);
}

ADVAPI BOOL WINAPI LookupAccountSidLocalW(PSID sid, LPWSTR name, LPDWORD name_size, LPWSTR domain,
                                          LPDWORD domain_size, PSID_NAME_USE use)
{
    return LookupAccountSidW(nullptr, sid, name, name_size, domain, domain_size, use);
}

ADVAPI BOOL WINAPI LookupAccountSidLocalA(PSID sid, LPSTR name, LPDWORD name_size, LPSTR domain,
                                          LPDWORD domain_size, PSID_NAME_USE use)
{
    return LookupAccountSidA(nullptr, sid, name, name_size, domain, domain_size, use);
}

/* ------------------------------------------------------------------ rights and secrets: no policy database */

/*
 * the rights of an account are kept in one key of the registry, a subkey per sid with the names of its privileges and
 * logon rights. an account that holds none has no subkey, which is how the policy tells it is unknown.
 */
#define POLICY_CREATE_ACCOUNT 0x10
#define RIGHTS_KEY L"SYSTEM\\CurrentControlSet\\Control\\Lsa\\Rights"
#define STATUS_NO_SUCH_PRIVILEGE_ ((NTSTATUS)0xC0000060L)

typedef struct LSA_ENUMERATION_INFORMATION_ {
    PSID Sid;
} LSA_ENUMERATION_INFORMATION_;

ADVAPI BOOL WINAPI LookupPrivilegeValueW(LPCWSTR system, LPCWSTR name, PLUID luid);
ADVAPI BOOL WINAPI LookupPrivilegeNameW(LPCWSTR system, PLUID luid, LPWSTR name, LPDWORD size);

static const PCWSTR g_logon_rights[] = {
    L"SeInteractiveLogonRight", L"SeNetworkLogonRight", L"SeBatchLogonRight", L"SeServiceLogonRight",
    L"SeDenyInteractiveLogonRight", L"SeDenyNetworkLogonRight", L"SeDenyBatchLogonRight", L"SeDenyServiceLogonRight",
    L"SeRemoteInteractiveLogonRight", L"SeDenyRemoteInteractiveLogonRight",
};

static BOOL has_access(POLICY_OBJECT *policy, ACCESS_MASK needed)
{
    /* GENERIC_ALL and MAXIMUM_ALLOWED. */
    return (policy->Access & (0x10000000 | 0x02000000)) || (policy->Access & needed) == needed;
}

/* the name of a right as the system spells it, or nothing when the system has no such right. */
static BOOL canonical_right(const LSA_UNICODE_STRING *right, WCHAR *out, DWORD chars)
{
    WCHAR name[96];
    if (!right || !right->Buffer || !right->Length || right->Length / sizeof(WCHAR) >= RTL_NUMBER_OF(name))
        return FALSE;
    memcpy(name, right->Buffer, right->Length);
    name[right->Length / sizeof(WCHAR)] = 0;
    for (DWORD i = 0; i < RTL_NUMBER_OF(g_logon_rights); i++) {
        if (!_wcsicmp(name, g_logon_rights[i]) && wcslen(g_logon_rights[i]) < chars) {
            wcscpy(out, g_logon_rights[i]);
            return TRUE;
        }
    }
    LUID luid;
    DWORD size = chars;
    return LookupPrivilegeValueW(nullptr, name, &luid) && LookupPrivilegeNameW(nullptr, &luid, out, &size);
}

static BOOL rights_key(PSID sid, BOOL create, HKEY *key)
{
    LPWSTR text;
    WCHAR path[300];
    if (!ConvertSidToStringSidW(sid, &text))
        return FALSE;
    _snwprintf(path, RTL_NUMBER_OF(path), RIGHTS_KEY L"\\%ls", text);
    AdvFree(text);
    if (create)
        return !RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, key, nullptr);
    return !RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_ALL_ACCESS, key);
}

/* the rights of an open account as a multi string with room for one more name. */
static PWSTR read_rights(HKEY key)
{
    DWORD size = 0, type;
    if (RegQueryValueExW(key, L"Rights", nullptr, &type, nullptr, &size) || type != REG_MULTI_SZ)
        size = 0;
    PWSTR list = AdvAllocZero(size + 256 * sizeof(WCHAR));
    if (list && size)
        RegQueryValueExW(key, L"Rights", nullptr, &type, (BYTE *)list, &size);
    return list;
}

static BOOL holds_right(PCWSTR list, PCWSTR right)
{
    for (; *list; list += wcslen(list) + 1)
        if (!_wcsicmp(list, right))
            return TRUE;
    return FALSE;
}

static SIZE_T rights_chars(PCWSTR list)
{
    SIZE_T n = 0;
    for (; list[n]; n += wcslen(list + n) + 1)
        ;
    return n;
}

ADVAPI NTSTATUS WINAPI LsaEnumerateAccountRights(LSA_HANDLE handle, PSID account, PLSA_UNICODE_STRING *rights, PULONG count)
{
    POLICY_OBJECT *policy = policy_of(handle);
    if (!policy)
        return STATUS_INVALID_HANDLE;
    if (!account || !rights || !count)
        return STATUS_INVALID_PARAMETER;
    *rights = nullptr;
    *count = 0;
    if (!has_access(policy, POLICY_LOOKUP_NAMES))
        return STATUS_ACCESS_DENIED;
    HKEY key;
    /* an account with no rights is unknown to the policy. */
    if (!rights_key(account, FALSE, &key))
        return STATUS_OBJECT_NAME_NOT_FOUND;
    PWSTR list = read_rights(key);
    RegCloseKey(key);
    if (!list)
        return STATUS_NO_MEMORY;
    ULONG n = 0;
    SIZE_T bytes = 0;
    for (PCWSTR at = list; *at; at += wcslen(at) + 1) {
        n++;
        bytes += sizeof(LSA_UNICODE_STRING) + (wcslen(at) + 1) * sizeof(WCHAR);
    }
    if (!n) {
        AdvFree(list);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    LSA_UNICODE_STRING *out = lsa_alloc(bytes);
    if (!out) {
        AdvFree(list);
        return STATUS_NO_MEMORY;
    }
    WCHAR *text = (WCHAR *)(out + n);
    ULONG i = 0;
    for (PCWSTR at = list; *at; at += wcslen(at) + 1, i++) {
        SIZE_T length = wcslen(at);
        memcpy(text, at, (length + 1) * sizeof(WCHAR));
        out[i].Length = (USHORT)(length * sizeof(WCHAR));
        out[i].MaximumLength = (USHORT)((length + 1) * sizeof(WCHAR));
        out[i].Buffer = text;
        text += length + 1;
    }
    AdvFree(list);
    *rights = out;
    *count = n;
    return STATUS_SUCCESS;
}

ADVAPI NTSTATUS WINAPI LsaEnumerateAccountsWithUserRight(LSA_HANDLE handle, PLSA_UNICODE_STRING right, PVOID *buffer,
                                                         PULONG count)
{
    POLICY_OBJECT *policy = policy_of(handle);
    if (!policy)
        return STATUS_INVALID_HANDLE;
    if (!buffer || !count)
        return STATUS_INVALID_PARAMETER;
    *buffer = nullptr;
    *count = 0;
    if (!has_access(policy, POLICY_LOOKUP_NAMES | POLICY_VIEW_LOCAL_INFORMATION))
        return STATUS_ACCESS_DENIED;
    WCHAR wanted[96];
    if (right && !canonical_right(right, wanted, RTL_NUMBER_OF(wanted)))
        return STATUS_NO_SUCH_PRIVILEGE_;
    if (!right)
        return STATUS_INVALID_PARAMETER;
    HKEY root;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, RIGHTS_KEY, 0, KEY_READ, &root))
        return STATUS_NO_MORE_ENTRIES;
    DWORD capacity = 0, found = 0;
    PSID *sids = nullptr;
    for (DWORD i = 0;; i++) {
        WCHAR name[200];
        DWORD n = RTL_NUMBER_OF(name);
        if (RegEnumKeyExW(root, i, name, &n, nullptr, nullptr, nullptr, nullptr))
            break;
        HKEY key;
        if (RegOpenKeyExW(root, name, 0, KEY_READ, &key))
            continue;
        PWSTR list = read_rights(key);
        RegCloseKey(key);
        PSID sid = nullptr;
        if (list && holds_right(list, wanted) && ConvertStringSidToSidW(name, &sid)) {
            if (found == capacity) {
                PSID *bigger = AdvAllocZero((capacity ? capacity * 2 : 8) * sizeof(PSID));
                if (bigger && sids)
                    memcpy(bigger, sids, found * sizeof(PSID));
                AdvFree(sids);
                sids = bigger;
                capacity = capacity ? capacity * 2 : 8;
            }
            if (sids)
                sids[found++] = sid;
            else
                LocalFree(sid);
        }
        AdvFree(list);
    }
    RegCloseKey(root);
    if (!found) {
        AdvFree(sids);
        return STATUS_NO_MORE_ENTRIES;
    }
    SIZE_T bytes = found * sizeof(LSA_ENUMERATION_INFORMATION_);
    for (DWORD i = 0; i < found; i++)
        bytes += 8 + 4 * (SIZE_T)((BYTE *)sids[i])[1];
    LSA_ENUMERATION_INFORMATION_ *out = lsa_alloc(bytes);
    NTSTATUS status = out ? STATUS_SUCCESS : STATUS_NO_MEMORY;
    BYTE *at = (BYTE *)(out + found);
    for (DWORD i = 0; out && i < found; i++) {
        SIZE_T length = 8 + 4 * (SIZE_T)((BYTE *)sids[i])[1];
        memcpy(at, sids[i], length);
        out[i].Sid = at;
        at += length;
    }
    for (DWORD i = 0; i < found; i++)
        LocalFree(sids[i]);
    AdvFree(sids);
    if (out) {
        *buffer = out;
        *count = found;
    }
    return status;
}

/* a machine outside any domain trusts none. */
ADVAPI NTSTATUS WINAPI LsaEnumerateTrustedDomains(LSA_HANDLE handle, PULONG context, PVOID *buffer, ULONG maximum,
                                                  PULONG count)
{
    if (!policy_of(handle))
        return STATUS_INVALID_HANDLE;
    if (!buffer)
        return STATUS_INVALID_PARAMETER;
    *buffer = nullptr;
    if (count)
        *count = 0;
    return STATUS_NO_MORE_ENTRIES;
}

ADVAPI NTSTATUS WINAPI LsaAddAccountRights(LSA_HANDLE handle, PSID account, PLSA_UNICODE_STRING rights, ULONG count)
{
    POLICY_OBJECT *policy = policy_of(handle);
    if (!policy)
        return STATUS_INVALID_HANDLE;
    if (!account || (count && !rights))
        return STATUS_INVALID_PARAMETER;
    if (!has_access(policy, POLICY_CREATE_ACCOUNT | POLICY_LOOKUP_NAMES))
        return STATUS_ACCESS_DENIED;
    /* every name is checked before the account changes. */
    WCHAR (*names)[96] = AdvAllocZero((count ? count : 1) * sizeof(*names));
    if (!names)
        return STATUS_NO_MEMORY;
    for (ULONG i = 0; i < count; i++) {
        if (!canonical_right(&rights[i], names[i], RTL_NUMBER_OF(names[i]))) {
            AdvFree(names);
            return STATUS_NO_SUCH_PRIVILEGE_;
        }
    }
    HKEY key;
    if (!rights_key(account, TRUE, &key)) {
        AdvFree(names);
        return STATUS_INVALID_PARAMETER;
    }
    PWSTR list = read_rights(key);
    SIZE_T used = list ? rights_chars(list) : 0;
    SIZE_T room = list ? used + 1 + count * 96 + 2 : 0;
    PWSTR next = list ? AdvAllocZero(room * sizeof(WCHAR)) : nullptr;
    if (!next) {
        AdvFree(list);
        AdvFree(names);
        RegCloseKey(key);
        return STATUS_NO_MEMORY;
    }
    memcpy(next, list, used * sizeof(WCHAR));
    for (ULONG i = 0; i < count; i++) {
        if (holds_right(next, names[i]))
            continue;
        SIZE_T at = rights_chars(next), length = wcslen(names[i]);
        memcpy(next + at, names[i], (length + 1) * sizeof(WCHAR));
        next[at + length + 1] = 0;
    }
    SIZE_T chars = rights_chars(next) + 1;
    if (chars == 1)
        chars = 2;
    RegSetValueExW(key, L"Rights", 0, REG_MULTI_SZ, (const BYTE *)next, (DWORD)(chars * sizeof(WCHAR)));
    RegCloseKey(key);
    AdvFree(next);
    AdvFree(list);
    AdvFree(names);
    return STATUS_SUCCESS;
}

ADVAPI NTSTATUS WINAPI LsaRemoveAccountRights(LSA_HANDLE handle, PSID account, BOOLEAN all, PLSA_UNICODE_STRING rights,
                                              ULONG count)
{
    POLICY_OBJECT *policy = policy_of(handle);
    if (!policy)
        return STATUS_INVALID_HANDLE;
    if (!account || (!all && count && !rights))
        return STATUS_INVALID_PARAMETER;
    if (!has_access(policy, POLICY_LOOKUP_NAMES))
        return STATUS_ACCESS_DENIED;
    HKEY key;
    if (!rights_key(account, FALSE, &key))
        return STATUS_OBJECT_NAME_NOT_FOUND;
    PWSTR list = read_rights(key);
    if (!list) {
        RegCloseKey(key);
        return STATUS_NO_MEMORY;
    }
    NTSTATUS status = STATUS_SUCCESS;
    PWSTR next = AdvAllocZero((rights_chars(list) + 4) * sizeof(WCHAR));
    if (!next) {
        status = STATUS_NO_MEMORY;
    } else if (!all) {
        memcpy(next, list, (rights_chars(list) + 1) * sizeof(WCHAR));
        for (ULONG i = 0; i < count && !status; i++) {
            WCHAR name[96];
            if (!canonical_right(&rights[i], name, RTL_NUMBER_OF(name)) || !holds_right(next, name)) {
                status = STATUS_NO_SUCH_PRIVILEGE_;
                break;
            }
            PWSTR out = next;
            for (PWSTR at = next; *at;) {
                SIZE_T n = wcslen(at) + 1;
                PWSTR following = at + n;
                if (_wcsicmp(at, name)) {
                    memmove(out, at, n * sizeof(WCHAR));
                    out += n;
                }
                at = following;
            }
            *out = 0;
            out[1] = 0;
        }
    }
    if (!status) {
        if (all || !*next) {
            LPWSTR text;
            WCHAR path[300];
            RegCloseKey(key);
            key = nullptr;
            if (ConvertSidToStringSidW(account, &text)) {
                _snwprintf(path, RTL_NUMBER_OF(path), RIGHTS_KEY L"\\%ls", text);
                AdvFree(text);
                RegDeleteKeyW(HKEY_LOCAL_MACHINE, path);
            }
        } else {
            RegSetValueExW(key, L"Rights", 0, REG_MULTI_SZ, (const BYTE *)next, (DWORD)((rights_chars(next) + 1) * sizeof(WCHAR)));
        }
    }
    if (key)
        RegCloseKey(key);
    AdvFree(next);
    AdvFree(list);
    return status;
}

ADVAPI NTSTATUS WINAPI LsaCreateSecret(LSA_HANDLE handle, PLSA_UNICODE_STRING name, ACCESS_MASK access, PLSA_HANDLE secret)
{
    (void)name;
    (void)access;
    if (secret)
        *secret = nullptr;
    return policy_of(handle) ? STATUS_ACCESS_DENIED : STATUS_INVALID_HANDLE;
}

ADVAPI NTSTATUS WINAPI LsaOpenSecret(LSA_HANDLE handle, PLSA_UNICODE_STRING name, ACCESS_MASK access, PLSA_HANDLE secret)
{
    (void)name;
    (void)access;
    if (secret)
        *secret = nullptr;
    return policy_of(handle) ? STATUS_OBJECT_NAME_NOT_FOUND : STATUS_INVALID_HANDLE;
}

ADVAPI NTSTATUS WINAPI LsaQuerySecret(LSA_HANDLE secret, PLSA_UNICODE_STRING *current, PLARGE_INTEGER current_time,
                                      PLSA_UNICODE_STRING *old, PLARGE_INTEGER old_time)
{
    (void)secret;
    (void)current;
    (void)current_time;
    (void)old;
    (void)old_time;
    return STATUS_INVALID_HANDLE;
}

ADVAPI NTSTATUS WINAPI LsaSetSecret(LSA_HANDLE secret, PLSA_UNICODE_STRING current, PLSA_UNICODE_STRING old)
{
    (void)secret;
    (void)current;
    (void)old;
    return STATUS_INVALID_HANDLE;
}

ADVAPI NTSTATUS WINAPI LsaDelete(LSA_HANDLE object)
{
    (void)object;
    return STATUS_INVALID_HANDLE;
}

ADVAPI NTSTATUS WINAPI LsaStorePrivateData(LSA_HANDLE handle, PLSA_UNICODE_STRING key, PLSA_UNICODE_STRING data)
{
    (void)key;
    (void)data;
    return policy_of(handle) ? STATUS_ACCESS_DENIED : STATUS_INVALID_HANDLE;
}

ADVAPI NTSTATUS WINAPI LsaRetrievePrivateData(LSA_HANDLE handle, PLSA_UNICODE_STRING key, PLSA_UNICODE_STRING *data)
{
    (void)key;
    if (data)
        *data = nullptr;
    return policy_of(handle) ? STATUS_OBJECT_NAME_NOT_FOUND : STATUS_INVALID_HANDLE;
}

/* ------------------------------------------------------------------ the internal client lookups of sechost */

#define LSA_LOOKUP_WKSTA 1

static ULONG mapped_names(ULONG count, const LSA_TRANSLATED_SID2 *sids)
{
    ULONG mapped = 0;
    for (ULONG i = 0; sids && i < count; i++)
        if (sids[i].Use != SidTypeUnknown && sids[i].Use != SidTypeInvalid)
            mapped++;
    return mapped;
}

static ULONG mapped_sids(ULONG count, const LSA_TRANSLATED_NAME *names)
{
    ULONG mapped = 0;
    for (ULONG i = 0; names && i < count; i++)
        if (names[i].Use != SidTypeUnknown && names[i].Use != SidTypeInvalid)
            mapped++;
    return mapped;
}

/* what LsaLookupNames2 calls; mapped gets how many names were translated and version the revision of the call. */
ADVAPI NTSTATUS WINAPI LsaICLookupNames(LSA_HANDLE handle, ULONG flags, ULONG count, PLSA_UNICODE_STRING names,
                                        LSA_REFERENCED_DOMAIN_LIST **domains, LSA_TRANSLATED_SID2 **sids, ULONG level,
                                        BOOLEAN options, ULONG *mapped, ULONG *version)
{
    (void)options;
    if (!domains || !sids || !mapped)
        return STATUS_INVALID_PARAMETER;
    if (version)
        *version = 2;
    *mapped = 0;
    if (level != LSA_LOOKUP_WKSTA)
        return STATUS_NOT_SUPPORTED;
    NTSTATUS status = LsaLookupNames2(handle, flags, count, names, domains, sids);
    if (NT_SUCCESS(status) || status == STATUS_SOME_NOT_MAPPED)
        *mapped = mapped_names(count, *sids);
    return status;
}

ADVAPI NTSTATUS WINAPI LsaICLookupSids(LSA_HANDLE handle, ULONG count, PSID *sids,
                                       LSA_REFERENCED_DOMAIN_LIST **domains, LSA_TRANSLATED_NAME **names, ULONG level,
                                       ULONG options, ULONG *mapped, ULONG *version)
{
    if (!domains || !names || !mapped)
        return STATUS_INVALID_PARAMETER;
    if (version)
        *version = 2;
    *mapped = 0;
    if (level != LSA_LOOKUP_WKSTA)
        return STATUS_NOT_SUPPORTED;
    NTSTATUS status = LsaLookupSids2(handle, options, count, sids, domains, names);
    if (NT_SUCCESS(status) || status == STATUS_SOME_NOT_MAPPED)
        *mapped = mapped_sids(count, *names);
    return status;
}

/* a lookup at a named server with credentials: the credentials mean nothing to this machine and no other is reachable. */
static NTSTATUS creds_server(PCWSTR server)
{
    if (!server)
        return STATUS_INVALID_PARAMETER;
    return AdvIsLocalSystem(server) ? STATUS_SUCCESS : STATUS_RPC_SERVER_UNAVAILABLE_;
}

ADVAPI NTSTATUS WINAPI LsaICLookupNamesWithCreds(PCWSTR server, PCWSTR user, ULONG user_type, ULONG flags,
                                                 PVOID credentials, ULONG authn_service, ULONG count,
                                                 PLSA_UNICODE_STRING names, LSA_REFERENCED_DOMAIN_LIST **domains,
                                                 LSA_TRANSLATED_SID2 **sids, ULONG level, ULONG *mapped)
{
    (void)user;
    (void)user_type;
    (void)credentials;
    (void)authn_service;
    if (!server || !domains || !sids || !mapped)
        return STATUS_INVALID_PARAMETER;
    *domains = nullptr;
    *sids = nullptr;
    *mapped = 0;
    NTSTATUS status = creds_server(server);
    if (!NT_SUCCESS(status))
        return status;
    LSA_OBJECT_ATTRIBUTES attributes = {sizeof(attributes)};
    LSA_HANDLE policy;
    status = LsaOpenPolicy(nullptr, &attributes, POLICY_LOOKUP_NAMES, &policy);
    if (!NT_SUCCESS(status))
        return status;
    ULONG version;
    status = LsaICLookupNames(policy, flags, count, names, domains, sids, level, 0, mapped, &version);
    LsaClose(policy);
    return status;
}

ADVAPI NTSTATUS WINAPI LsaICLookupSidsWithCreds(PCWSTR server, PCWSTR user, ULONG user_type, ULONG flags,
                                                PVOID credentials, ULONG authn_service, ULONG count, PSID *sids,
                                                LSA_REFERENCED_DOMAIN_LIST **domains, LSA_TRANSLATED_NAME **names,
                                                ULONG level, ULONG *mapped)
{
    (void)user;
    (void)user_type;
    (void)credentials;
    (void)authn_service;
    if (!server || !domains || !names || !mapped)
        return STATUS_INVALID_PARAMETER;
    *domains = nullptr;
    *names = nullptr;
    *mapped = 0;
    NTSTATUS status = creds_server(server);
    if (!NT_SUCCESS(status))
        return status;
    LSA_OBJECT_ATTRIBUTES attributes = {sizeof(attributes)};
    LSA_HANDLE policy;
    status = LsaOpenPolicy(nullptr, &attributes, POLICY_LOOKUP_NAMES, &policy);
    if (!NT_SUCCESS(status))
        return status;
    ULONG version;
    status = LsaICLookupSids(policy, count, sids, domains, names, level, flags, mapped, &version);
    LsaClose(policy);
    return status;
}

/* opening a policy by server name and credentials. only the machine itself answers. */
ADVAPI NTSTATUS WINAPI LsaIOpenPolicyWithCreds(PCWSTR server, PCWSTR user, ULONG user_type, PLSA_OBJECT_ATTRIBUTES attributes,
                                               ACCESS_MASK access, PLSA_HANDLE handle)
{
    (void)user;
    (void)user_type;
    if (!handle)
        return STATUS_INVALID_PARAMETER;
    *handle = nullptr;
    if (!attributes || !server)
        return STATUS_INVALID_PARAMETER;
    NTSTATUS status = creds_server(server);
    if (!NT_SUCCESS(status))
        return status;
    return LsaOpenPolicy(nullptr, attributes, access, handle);
}

/* the kinds of account lsass tells apart: this machine has the local ones. */
#define LSA_LOCAL_USER_ACCOUNT 1

ADVAPI NTSTATUS WINAPI LsaLookupUserAccountType(PSID sid, ULONG *type)
{
    if (!type)
        return STATUS_INVALID_PARAMETER;
    *type = 0;
    BYTE own[SECURITY_MAX_SID_SIZE], machine[SECURITY_MAX_SID_SIZE];
    if (!sid) {
        if (!AdvCurrentUserSid(own))
            return STATUS_UNSUCCESSFUL;
        sid = (PSID)own;
    }
    if (!AdvSidValid(sid))
        return STATUS_INVALID_SID;
    const SID *user = sid;
    const SID *domain = AdvMachineSid(machine) ? (const SID *)machine : nullptr;
    if (!domain || user->SubAuthorityCount != domain->SubAuthorityCount + 1 ||
        memcmp(&user->IdentifierAuthority, &domain->IdentifierAuthority, sizeof(user->IdentifierAuthority)) ||
        memcmp(user->SubAuthority, domain->SubAuthority, 4 * domain->SubAuthorityCount))
        return STATUS_NONE_MAPPED_;
    *type = LSA_LOCAL_USER_ACCOUNT;
    return STATUS_SUCCESS;
}

/* mappings of capability sids to names are kept by lsass, which this machine does not have. */
ADVAPI NTSTATUS WINAPI LsaLookupManageSidNameMapping(ULONG operation, PVOID input, PVOID *output)
{
    (void)operation;
    (void)input;
    (void)output;
    return STATUS_NOT_IMPLEMENTED;
}
