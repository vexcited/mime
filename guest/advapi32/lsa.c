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

ADVAPI NTSTATUS WINAPI LsaEnumerateAccountRights(LSA_HANDLE handle, PSID account, PLSA_UNICODE_STRING *rights, PULONG count)
{
    if (!policy_of(handle))
        return STATUS_INVALID_HANDLE;
    if (!account || !rights || !count)
        return STATUS_INVALID_PARAMETER;
    *rights = nullptr;
    *count = 0;
    /* an account with no rights is unknown to the policy. */
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

ADVAPI NTSTATUS WINAPI LsaEnumerateAccountsWithUserRight(LSA_HANDLE handle, PLSA_UNICODE_STRING right, PVOID *buffer,
                                                         PULONG count)
{
    if (!policy_of(handle))
        return STATUS_INVALID_HANDLE;
    if (!buffer || !count)
        return STATUS_INVALID_PARAMETER;
    *buffer = nullptr;
    *count = 0;
    return STATUS_NO_MORE_ENTRIES;
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
    (void)account;
    (void)rights;
    (void)count;
    return policy_of(handle) ? STATUS_ACCESS_DENIED : STATUS_INVALID_HANDLE;
}

ADVAPI NTSTATUS WINAPI LsaRemoveAccountRights(LSA_HANDLE handle, PSID account, BOOLEAN all, PLSA_UNICODE_STRING rights,
                                              ULONG count)
{
    (void)account;
    (void)all;
    (void)rights;
    (void)count;
    return policy_of(handle) ? STATUS_ACCESS_DENIED : STATUS_INVALID_HANDLE;
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
