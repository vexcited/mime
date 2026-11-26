/*
 * the policy interface of the lsa over ncalrpc:[LSARPC_ENDPOINT], which sechost speaks for the credentials of the
 * credential manager, served by cred.c, and genuine advapi32 for the names, the numbers and the descriptions of
 * privileges; windows serves it from lsass, mime from the manager, see lsalookup.c. answers measured on windows. a
 * procedure that is not served says which one a program called.
 */
#include "services.h"
#include <mime/win32/rpc.h>
#include "services/lsarpc.h"

#define LSAR_POLICY_MAGIC 0x5250534c /* "LSPR" */
#define STATUS_INVALID_HANDLE_ 0xC0000008
#define STATUS_NO_MEMORY_ 0xC0000017
#define STATUS_NOT_IMPLEMENTED_ 0xC0000002
#define STATUS_NO_SUCH_PRIVILEGE_ 0xC0000060
#define STATUS_INVALID_PARAMETER_ 0xC000000D

typedef struct LSAR_POLICY {
    DWORD Magic;
    DWORD Access;
} LSAR_POLICY;

/* the privileges of windows 11, whose numbers are fixed. the descriptions are the english ones of the image. */
static const struct {
    DWORD Value;
    const WCHAR *Name, *Display;
} g_privileges[] = {
    {2, L"SeCreateTokenPrivilege", L"Create a token object"},
    {3, L"SeAssignPrimaryTokenPrivilege", L"Replace a process level token"},
    {4, L"SeLockMemoryPrivilege", L"Lock pages in memory"},
    {5, L"SeIncreaseQuotaPrivilege", L"Adjust memory quotas for a process"},
    {6, L"SeMachineAccountPrivilege", L"Add workstations to domain"},
    {7, L"SeTcbPrivilege", L"Act as part of the operating system"},
    {8, L"SeSecurityPrivilege", L"Manage auditing and security log"},
    {9, L"SeTakeOwnershipPrivilege", L"Take ownership of files or other objects"},
    {10, L"SeLoadDriverPrivilege", L"Load and unload device drivers"},
    {11, L"SeSystemProfilePrivilege", L"Profile system performance"},
    {12, L"SeSystemtimePrivilege", L"Change the system time"},
    {13, L"SeProfileSingleProcessPrivilege", L"Profile single process"},
    {14, L"SeIncreaseBasePriorityPrivilege", L"Increase scheduling priority"},
    {15, L"SeCreatePagefilePrivilege", L"Create a pagefile"},
    {16, L"SeCreatePermanentPrivilege", L"Create permanent shared objects"},
    {17, L"SeBackupPrivilege", L"Back up files and directories"},
    {18, L"SeRestorePrivilege", L"Restore files and directories"},
    {19, L"SeShutdownPrivilege", L"Shut down the system"},
    {20, L"SeDebugPrivilege", L"Debug programs"},
    {21, L"SeAuditPrivilege", L"Generate security audits"},
    {22, L"SeSystemEnvironmentPrivilege", L"Modify firmware environment values"},
    {23, L"SeChangeNotifyPrivilege", L"Bypass traverse checking"},
    {24, L"SeRemoteShutdownPrivilege", L"Force shutdown from a remote system"},
    {25, L"SeUndockPrivilege", L"Remove computer from docking station"},
    {26, L"SeSyncAgentPrivilege", L"Synchronize directory service data"},
    {27, L"SeEnableDelegationPrivilege", L"Enable computer and user accounts to be trusted for delegation"},
    {28, L"SeManageVolumePrivilege", L"Perform volume maintenance tasks"},
    {29, L"SeImpersonatePrivilege", L"Impersonate a client after authentication"},
    {30, L"SeCreateGlobalPrivilege", L"Create global objects"},
    {31, L"SeTrustedCredManAccessPrivilege", L"Access Credential Manager as a trusted caller"},
    {32, L"SeRelabelPrivilege", L"Modify an object label"},
    {33, L"SeIncreaseWorkingSetPrivilege", L"Increase a process working set"},
    {34, L"SeTimeZonePrivilege", L"Change the time zone"},
    {35, L"SeCreateSymbolicLinkPrivilege", L"Create symbolic links"},
    {36, L"SeDelegateSessionUserImpersonatePrivilege", L"Obtain an impersonation token for another user in the same session"},
};

static LSAR_POLICY *policy_of(LSAR_HANDLE handle)
{
    LSAR_POLICY *p = handle;
    return p && p->Magic == LSAR_POLICY_MAGIC ? p : nullptr;
}

static void policy_free(void *handle)
{
    LSAR_POLICY *p = policy_of(handle);
    if (p) {
        p->Magic = 0;
        SvcFree(p);
    }
}

void WINAPI LSAR_HANDLE_rundown(void *handle)
{
    policy_free(handle);
}

long WINAPI lsarpc_LsarOpenPolicy2_s(LSAPR_SERVER_NAME SystemName, LSAPR_OBJECT_ATTRIBUTES *ObjectAttributes,
                                     unsigned long DesiredAccess, LSAR_HANDLE *PolicyHandle)
{
    LSAR_POLICY *p = SvcAllocZero(sizeof(*p));
    if (!p)
        return (long)STATUS_NO_MEMORY_;
    p->Magic = LSAR_POLICY_MAGIC;
    p->Access = DesiredAccess;
    *PolicyHandle = p;
    return 0;
}

long WINAPI lsarpc_LsarClose_s(LSAR_HANDLE *ObjectHandle)
{
    if (!policy_of(*ObjectHandle))
        return (long)STATUS_INVALID_HANDLE_;
    policy_free(*ObjectHandle);
    *ObjectHandle = nullptr;
    return 0;
}

/* a name as a program gave it: its length counts, no terminator is promised. */
static BOOL same_name(const RPC_UNICODE_STRING *given, const WCHAR *name)
{
    size_t n = wcslen(name);
    return given->Buffer && given->Length == n * sizeof(WCHAR) && !_wcsnicmp(given->Buffer, name, n);
}

static RPC_UNICODE_STRING *new_string(const WCHAR *text)
{
    size_t n = wcslen(text);
    RPC_UNICODE_STRING *out = MIDL_user_allocate(sizeof(*out));
    WCHAR *buffer = out ? MIDL_user_allocate((n + 1) * sizeof(WCHAR)) : nullptr;
    if (!buffer) {
        MIDL_user_free(out);
        return nullptr;
    }
    memcpy(buffer, text, (n + 1) * sizeof(WCHAR));
    out->Length = (unsigned short)(n * sizeof(WCHAR));
    out->MaximumLength = (unsigned short)((n + 1) * sizeof(WCHAR));
    out->Buffer = buffer;
    return out;
}

long WINAPI lsarpc_LsarLookupPrivilegeValue_s(LSAR_HANDLE PolicyHandle, RPC_UNICODE_STRING *Name, RPC_LUID *Value)
{
    Value->LowPart = 0;
    Value->HighPart = 0;
    if (!policy_of(PolicyHandle))
        return (long)STATUS_INVALID_HANDLE_;
    for (size_t i = 0; i < RTL_NUMBER_OF(g_privileges); i++)
        if (same_name(Name, g_privileges[i].Name)) {
            Value->LowPart = g_privileges[i].Value;
            return 0;
        }
    return (long)STATUS_NO_SUCH_PRIVILEGE_;
}

long WINAPI lsarpc_LsarLookupPrivilegeName_s(LSAR_HANDLE PolicyHandle, RPC_LUID *Value, RPC_UNICODE_STRING **Name)
{
    *Name = nullptr;
    if (!policy_of(PolicyHandle))
        return (long)STATUS_INVALID_HANDLE_;
    for (size_t i = 0; i < RTL_NUMBER_OF(g_privileges); i++)
        if (!Value->HighPart && Value->LowPart == g_privileges[i].Value) {
            *Name = new_string(g_privileges[i].Name);
            return *Name ? 0 : (long)STATUS_NO_MEMORY_;
        }
    return (long)STATUS_NO_SUCH_PRIVILEGE_;
}

long WINAPI lsarpc_LsarLookupPrivilegeDisplayName_s(LSAR_HANDLE PolicyHandle, RPC_UNICODE_STRING *Name,
                                                    short ClientLanguage, short ClientSystemDefaultLanguage,
                                                    RPC_UNICODE_STRING **DisplayName, unsigned short *LanguageReturned)
{
    *DisplayName = nullptr;
    *LanguageReturned = 0;
    if (!policy_of(PolicyHandle))
        return (long)STATUS_INVALID_HANDLE_;
    for (size_t i = 0; i < RTL_NUMBER_OF(g_privileges); i++)
        if (same_name(Name, g_privileges[i].Name)) {
            *DisplayName = new_string(g_privileges[i].Display);
            *LanguageReturned = 0x0409;
            return *DisplayName ? 0 : (long)STATUS_NO_MEMORY_;
        }
    return (long)STATUS_NO_SUCH_PRIVILEGE_;
}

_Static_assert(sizeof(CRED_RECORD) == __builtin_offsetof(LSAPR_ENCRYPTED_CREDENTIAL, ClearCredentialBlobSize), "the credential of cred.c is the interface's");
_Static_assert(sizeof(CRED_ATTRIBUTE) == sizeof(LSAPR_CREDENTIAL_ATTRIBUTE), "the attribute of cred.c is the interface's");
_Static_assert(sizeof(CRED_TARGET) == sizeof(LSAPR_CRED_TARGET_INFO), "the target of cred.c is the interface's");

/* the credentials of the credential manager, kept by cred.c. */
long WINAPI lsarpc_CredrWrite_s(LSAPR_SERVER_NAME ServerName, LSAPR_ENCRYPTED_CREDENTIAL *Credential, unsigned long Flags)
{
    return CredStoreWrite((const CRED_RECORD *)Credential, Credential->ClearCredentialBlobSize, Flags);
}

long WINAPI lsarpc_CredrRead_s(LSAPR_SERVER_NAME ServerName, unsigned short *TargetName, unsigned long Type,
                               unsigned long Flags, LSAPR_ENCRYPTED_CREDENTIAL **Credential)
{
    *Credential = nullptr;
    LSAPR_ENCRYPTED_CREDENTIAL *c = MIDL_user_allocate(sizeof(*c));
    if (!c)
        return (long)STATUS_NO_MEMORY_;
    memset(c, 0, sizeof(*c));
    DWORD clear = 0;
    long status = CredStoreRead(TargetName, Type, Flags, (CRED_RECORD *)c, &clear);
    if (status) {
        MIDL_user_free(c);
        return status;
    }
    c->ClearCredentialBlobSize = clear;
    *Credential = c;
    return 0;
}

/* the records of the store as the array of a call, which takes them over. */
static long credential_array(CRED_RECORD *items, DWORD *sizes, DWORD count, LSAPR_CREDENTIAL_ARRAY *CredentialArray)
{
    long status = 0;
    LSAPR_ENCRYPTED_CREDENTIAL **list = MIDL_user_allocate(count * sizeof(*list));
    DWORD made = 0;
    for (; list && made < count; made++) {
        list[made] = MIDL_user_allocate(sizeof(**list));
        if (!list[made])
            break;
        memcpy(list[made], &items[made], sizeof(items[made]));
        list[made]->ClearCredentialBlobSize = sizes[made];
    }
    if (!list || made < count) {
        for (DWORD i = 0; i < count; i++) {
            if (i >= made)
                CredFreeRecord(&items[i]);
            else
                CredFreeRecord((CRED_RECORD *)list[i]);
            if (list && i < made)
                MIDL_user_free(list[i]);
        }
        MIDL_user_free(list);
        status = (long)STATUS_NO_MEMORY_;
    } else {
        CredentialArray->CredentialCount = count;
        CredentialArray->Credentials = list;
    }
    SvcFree(items);
    SvcFree(sizes);
    return status;
}

long WINAPI lsarpc_CredrEnumerate_s(LSAPR_SERVER_NAME ServerName, unsigned short *Filter, unsigned long Flags,
                                    LSAPR_CREDENTIAL_ARRAY *CredentialArray)
{
    CRED_RECORD *items = nullptr;
    DWORD *sizes = nullptr, count = 0;
    CredentialArray->CredentialCount = 0;
    CredentialArray->Credentials = nullptr;
    long status = CredStoreEnumerate(Filter, Flags, &items, &sizes, &count);
    return status ? status : credential_array(items, sizes, count, CredentialArray);
}

long WINAPI lsarpc_CredrWriteDomainCredentials_s(LSAPR_SERVER_NAME ServerName, LSAPR_CRED_TARGET_INFO *TargetInfo,
                                                 LSAPR_ENCRYPTED_CREDENTIAL *Credential, unsigned long Flags)
{
    return CredStoreWriteDomain((const CRED_TARGET *)TargetInfo, (const CRED_RECORD *)Credential,
                                Credential->ClearCredentialBlobSize, Flags);
}

long WINAPI lsarpc_CredrReadDomainCredentials_s(LSAPR_SERVER_NAME ServerName, LSAPR_CRED_TARGET_INFO *TargetInfo,
                                                unsigned long Flags, LSAPR_CREDENTIAL_ARRAY *CredentialArray)
{
    CRED_RECORD *items = nullptr;
    DWORD *sizes = nullptr, count = 0;
    CredentialArray->CredentialCount = 0;
    CredentialArray->Credentials = nullptr;
    long status = CredStoreReadDomain((const CRED_TARGET *)TargetInfo, Flags, &items, &sizes, &count);
    return status ? status : credential_array(items, sizes, count, CredentialArray);
}

long WINAPI lsarpc_CredrGetTargetInfo_s(LSAPR_SERVER_NAME ServerName, unsigned short *TargetName, unsigned long Flags,
                                        LSAPR_CRED_TARGET_INFO **TargetInfo)
{
    *TargetInfo = nullptr;
    return CredStoreTargetInfo(TargetName, Flags);
}

long WINAPI lsarpc_CredrGetSessionTypes_s(LSAPR_SERVER_NAME ServerName, unsigned long MaximumPersistCount,
                                          unsigned long *MaximumPersist)
{
    return CredStoreSessionTypes(MaximumPersistCount, (DWORD *)MaximumPersist);
}

long WINAPI lsarpc_CredrFindBestCredential_s(LSAPR_SERVER_NAME ServerName, unsigned short *TargetName,
                                             unsigned long Type, unsigned long Flags,
                                             LSAPR_ENCRYPTED_CREDENTIAL **Credential)
{
    *Credential = nullptr;
    LSAPR_ENCRYPTED_CREDENTIAL *c = MIDL_user_allocate(sizeof(*c));
    if (!c)
        return (long)STATUS_NO_MEMORY_;
    memset(c, 0, sizeof(*c));
    DWORD clear = 0;
    long status = CredStoreFindBest(TargetName, Type, Flags, (CRED_RECORD *)c, &clear);
    if (status) {
        MIDL_user_free(c);
        return status;
    }
    c->ClearCredentialBlobSize = clear;
    *Credential = c;
    return 0;
}

/* procedure 94, CredrBackupCredentials: refused before its arguments are read. */
long WINAPI lsarpc_Opnum94_s(handle_t h)
{
    return CredStoreBackup();
}

long WINAPI lsarpc_CredrDelete_s(LSAPR_SERVER_NAME ServerName, unsigned short *TargetName, unsigned long Type,
                                 unsigned long Flags)
{
    return CredStoreDelete(TargetName, Type, Flags);
}

#define X(number)                                                          \
    long WINAPI lsarpc_Opnum##number##_s(handle_t h)                       \
    {                                                                      \
        SvcLog("lsarpc: procedure %u is not served", (unsigned)number);    \
        return (long)STATUS_NOT_IMPLEMENTED_;                              \
    }
#define UNSERVED \
    X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) X(10) X(11) X(12) \
    X(13) X(14) X(15) X(16) X(17) X(18) X(19) X(20) X(21) X(22) X(23) X(24) \
    X(25) X(26) X(27) X(28) X(29) X(30) X(34) X(35) X(36) X(37) X(38) X(39) \
    X(40) X(41) X(42) X(43) X(45) X(46) X(47) X(48) X(49) X(50) X(51) X(52) \
    X(53) X(54) X(55) X(56) X(57) X(58) X(59) \
    X(67) X(68) X(70) X(71) X(72) X(73) X(74) X(75) X(76) \
    X(77) X(78) X(79) X(80) X(81) X(83) X(84) X(85) X(86) X(87) X(88) \
    X(89) X(90) X(91) X(92) X(93) X(95) X(96) X(97) X(98) X(99) X(100) \
    X(101) X(102) X(103) X(104) X(105) X(106) X(107) X(108) X(109) X(110) X(111) X(112) \
    X(113) X(114) X(115) X(116) X(117) X(118) X(119) X(120) X(121) X(122) X(123) X(124) \
    X(125) X(126) X(127) X(128) X(129) X(130) X(131) X(132) X(133) X(134) X(135)
UNSERVED
#undef X

static RPC_STATUS RPC_ENTRY allow_local(RPC_IF_HANDLE interface, void *context)
{
    return RPC_S_OK;
}

/* D:(A;;0x1;;;WD)(A;;0x1;;;AC): every local caller connects and calls, app containers included. */
static const BYTE g_endpoint_sd[] = {
    0x01, 0x00, 0x04, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x34, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00, 0x18, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x02, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
};

void LsaPolicyStart(void)
{
    RPC_STATUS status = RpcServerUseProtseqEpW((RPC_WSTR)L"ncalrpc", RPC_C_PROTSEQ_MAX_REQS_DEFAULT,
                                               (RPC_WSTR)L"LSARPC_ENDPOINT", (void *)g_endpoint_sd);
    if (!status)
        status = RpcServerRegisterIf3(lsarpc_s_ifspec, nullptr, nullptr, RPC_IF_AUTOLISTEN | RPC_IF_ALLOW_LOCAL_ONLY,
                                      RPC_C_LISTEN_MAX_CALLS_DEFAULT, (unsigned int)-1, allow_local,
                                      (void *)g_endpoint_sd);
    if (status)
        SvcLog("lsarpc: the rpc server did not start: %u", status);
}
