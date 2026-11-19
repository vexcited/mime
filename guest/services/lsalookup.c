/*
 * the lookup interface of the lsa over ncalrpc:[lsapolicylookup], which genuine sechost's LsaLookupOpenLocalPolicy,
 * LsaLookupGetDomainInfo and LsaLookupClose speak; windows serves it from lsass, mime from the manager, like the rest of
 * what lsass does here, see lsa.c. answers measured with lsaq.c of scratch/dlls/shellexec in the research repository.
 */
#include "services.h"
#include <mime/win32/rpc.h>
#include "services/lsalookup.h"

#define LSA_POLICY_MAGIC 0x4c4f504c /* "LPOL" */
#define STATUS_INVALID_HANDLE_ 0xC0000008
#define STATUS_INVALID_INFO_CLASS_ 0xC0000003
#define STATUS_NO_MEMORY_ 0xC0000017
#define STATUS_NOT_IMPLEMENTED_ 0xC0000002
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

long WINAPI lsalookup_LsarLookupNames_s(handle_t h)
{
    return (long)STATUS_NOT_IMPLEMENTED_;
}

long WINAPI lsalookup_LsarLookupSids_s(handle_t h)
{
    return (long)STATUS_NOT_IMPLEMENTED_;
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
