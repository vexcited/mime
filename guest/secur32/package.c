/*
 * the security package table, package queries and the dispatch tables.
 */
#include "secur32_private.h"

/*
 * the packages of a windows 11 installation, in the order lsa loads them:
 * negotiate is built into lsasrv, then come lsasrv's default list,
 * kerberos msv1_0 tspkg pku2u cloudAP wdigest schannel, the alias lsasrv
 * makes of schannel, and credssp.dll which sspicli loads in process. the
 * values are what each package's SpGetInfo fills in build 26300.
 */
const SEC_PACKAGE SecPackages[] = {
    /* lsasrv NegGetInfo: the default capabilities; cbMaxToken is the largest
       of the negotiable packages plus 256, so kerberos' 48000 gives 48256. */
    {L"Negotiate", L"Microsoft Package Negotiator", 0x00883BB3, 1, 9, 48256, SEC_KIND_NEGOTIATE},
    /* negoexts SpGetInfo; SpInitialize sets the token size to 12000. */
    {L"NegoExtender", L"NegoExtender Security Package", 0x00913913, 1, 30, 12000, SEC_KIND_EXTENDER},
    /* kerberos SpGetInfo; the token size is the documented MaxTokenSize default. */
    {L"Kerberos", L"Microsoft Kerberos V1.0", 0x028F3BBF, 1, 16, 48000, SEC_KIND_KERBEROS},
    /* msv1_0 SpGetInfo, without credential guard. */
    {L"NTLM", L"NTLM Security Package", 0x02882B37, 1, 10, 2888, SEC_KIND_NTLM},
    {L"TSSSP", L"TS Service Security Package", 0x00810230, 1, 22, 13000, SEC_KIND_TSSSP},
    /* pku2u SpInitialize sets the token size to 12000. */
    {L"pku2u", L"PKU2U Security Package", 0x00A11113, 1, 31, 12000, SEC_KIND_PKU2U},
    {L"CloudAP", L"Cloud AP Security Package", 0x00602000, 1, 36, 0, SEC_KIND_CLOUDAP},
    {L"WDigest", L"Digest Authentication for Windows", 0x00800304, 1, 21, 4096, SEC_KIND_DIGEST},
    {L"Schannel", L"Schannel Security Package", 0x004107B3, 1, 14, 24576, SEC_KIND_SCHANNEL},
    {L"Microsoft Unified Security Protocol Provider", L"Schannel Security Package", 0x004107B3, 1, 14, 24576,
     SEC_KIND_SCHANNEL},
    /* lsasrv LoadPackages copies the schannel package under this name. */
    {L"Default TLS SSP", L"Schannel Security Package", 0x004107B3, 1, 14, 24576, SEC_KIND_SCHANNEL},
    /* credssp InitMaxToken: the larger of negotiate plus 200 and 1024, plus schannel, 48456 + 24576. */
    {L"CREDSSP", L"Microsoft CredSSP Security Provider", 0x00810733, 1, SECPKG_ID_NONE, 73032, SEC_KIND_CREDSSP},
};

const ULONG SecPackageCount = RTL_NUMBER_OF(SecPackages);
const ULONG SecLsaPackageCount = RTL_NUMBER_OF(SecPackages) - 1;

static bool ascii_equal(PCSTR a, PCWSTR w)
{
    for (;; a++, w++) {
        WCHAR c = (UCHAR)*a;
        if (towlower(c) != towlower(*w))
            return false;
        if (!c)
            return true;
    }
}

const SEC_PACKAGE *SecFindPackageW(PCWSTR name)
{
    if (!name)
        return nullptr;
    for (ULONG i = 0; i < SecPackageCount; i++) {
        if (!_wcsicmp(SecPackages[i].Name, name))
            return &SecPackages[i];
    }
    return nullptr;
}

const SEC_PACKAGE *SecFindPackageA(PCSTR name)
{
    if (!name)
        return nullptr;
    for (ULONG i = 0; i < SecPackageCount; i++) {
        if (ascii_equal(name, SecPackages[i].Name))
            return &SecPackages[i];
    }
    return nullptr;
}

static SIZE_T copy_wide(PWSTR out, PCWSTR text)
{
    SIZE_T n = wcslen(text) + 1;
    memcpy(out, text, n * sizeof(WCHAR));
    return n;
}

static SIZE_T copy_narrow(PSTR out, PCWSTR text)
{
    SIZE_T i = 0;
    do
        out[i] = (CHAR)text[i];
    while (text[i++]);
    return i;
}

static SIZE_T strings_size(const SEC_PACKAGE *p, SIZE_T unit)
{
    return (wcslen(p->Name) + wcslen(p->Comment) + 2) * unit;
}

static void fill_w(SecPkgInfoW *info, const SEC_PACKAGE *p, PWSTR *strings)
{
    info->fCapabilities = p->Capabilities;
    info->wVersion = p->Version;
    info->wRPCID = p->RpcId;
    info->cbMaxToken = p->MaxToken;
    info->Name = *strings;
    *strings += copy_wide(*strings, p->Name);
    info->Comment = *strings;
    *strings += copy_wide(*strings, p->Comment);
}

static void fill_a(SecPkgInfoA *info, const SEC_PACKAGE *p, PSTR *strings)
{
    info->fCapabilities = p->Capabilities;
    info->wVersion = p->Version;
    info->wRPCID = p->RpcId;
    info->cbMaxToken = p->MaxToken;
    info->Name = *strings;
    *strings += copy_narrow(*strings, p->Name);
    info->Comment = *strings;
    *strings += copy_narrow(*strings, p->Comment);
}

/* one LocalAlloc block per package, freed with FreeContextBuffer, like SecCopyPackageInfoToUserW. */
SECURITY_STATUS SecCopyPackageInfoW(const SEC_PACKAGE *package, PSecPkgInfoW *info)
{
    if (!package)
        return SEC_E_SECPKG_NOT_FOUND;
    if (!info)
        return SEC_E_INVALID_PARAMETER;
    SecPkgInfoW *out = LocalAlloc(LPTR, sizeof(SecPkgInfoW) + strings_size(package, sizeof(WCHAR)));
    *info = out;
    if (!out)
        return SEC_E_INSUFFICIENT_MEMORY;
    PWSTR strings = (PWSTR)(out + 1);
    fill_w(out, package, &strings);
    return SEC_E_OK;
}

SECURITY_STATUS SecCopyPackageInfoA(const SEC_PACKAGE *package, PSecPkgInfoA *info)
{
    if (!package)
        return SEC_E_SECPKG_NOT_FOUND;
    if (!info)
        return SEC_E_INVALID_PARAMETER;
    SecPkgInfoA *out = LocalAlloc(LPTR, sizeof(SecPkgInfoA) + strings_size(package, sizeof(CHAR)));
    *info = out;
    if (!out)
        return SEC_E_INSUFFICIENT_MEMORY;
    PSTR strings = (PSTR)(out + 1);
    fill_a(out, package, &strings);
    return SEC_E_OK;
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY EnumerateSecurityPackagesW(ULONG *count, PSecPkgInfoW *info)
{
    if (!count || !info)
        return SEC_E_INVALID_PARAMETER;
    *count = 0;
    *info = nullptr;
    SIZE_T size = SecPackageCount * sizeof(SecPkgInfoW);
    for (ULONG i = 0; i < SecPackageCount; i++)
        size += strings_size(&SecPackages[i], sizeof(WCHAR));
    SecPkgInfoW *out = LocalAlloc(LMEM_FIXED, size);
    if (!out)
        return SEC_E_INSUFFICIENT_MEMORY;
    PWSTR strings = (PWSTR)(out + SecPackageCount);
    for (ULONG i = 0; i < SecPackageCount; i++)
        fill_w(&out[i], &SecPackages[i], &strings);
    *count = SecPackageCount;
    *info = out;
    return SEC_E_OK;
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY EnumerateSecurityPackagesA(ULONG *count, PSecPkgInfoA *info)
{
    if (!count || !info)
        return SEC_E_INVALID_PARAMETER;
    *count = 0;
    *info = nullptr;
    SIZE_T size = SecPackageCount * sizeof(SecPkgInfoA);
    for (ULONG i = 0; i < SecPackageCount; i++)
        size += strings_size(&SecPackages[i], sizeof(CHAR));
    SecPkgInfoA *out = LocalAlloc(LMEM_FIXED, size);
    if (!out)
        return SEC_E_INSUFFICIENT_MEMORY;
    PSTR strings = (PSTR)(out + SecPackageCount);
    for (ULONG i = 0; i < SecPackageCount; i++)
        fill_a(&out[i], &SecPackages[i], &strings);
    *count = SecPackageCount;
    *info = out;
    return SEC_E_OK;
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY QuerySecurityPackageInfoW(SEC_WCHAR *name, PSecPkgInfoW *info)
{
    return SecCopyPackageInfoW(SecFindPackageW(name), info);
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY QuerySecurityPackageInfoA(SEC_CHAR *name, PSecPkgInfoA *info)
{
    return SecCopyPackageInfoA(SecFindPackageA(name), info);
}

/* everything secur32 hands out comes from LocalAlloc, and sspicli frees unknown blocks the same way. */
SECURITYAPI SECURITY_STATUS SEC_ENTRY FreeContextBuffer(void *buffer)
{
    LocalFree(buffer);
    return SEC_E_OK;
}

/* lsa loads new packages; mime has no lsa to load them into. */
SECURITYAPI SECURITY_STATUS SEC_ENTRY AddSecurityPackageW(LPWSTR name, PSECURITY_PACKAGE_OPTIONS options)
{
    return SEC_E_UNSUPPORTED_FUNCTION;
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY AddSecurityPackageA(LPSTR name, PSECURITY_PACKAGE_OPTIONS options)
{
    return SEC_E_UNSUPPORTED_FUNCTION;
}

/* sspicli answers this for every package. */
SECURITYAPI SECURITY_STATUS SEC_ENTRY DeleteSecurityPackageW(LPWSTR name)
{
    return SEC_E_UNSUPPORTED_FUNCTION;
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY DeleteSecurityPackageA(LPSTR name)
{
    return SEC_E_UNSUPPORTED_FUNCTION;
}

/* ------------------------------------------------------------------ dispatch tables */

/*
 * sspicli hands out version 1 tables: the slots up to DecryptMessage, with
 * the legacy seal and unseal slots holding EncryptMessage and DecryptMessage
 * and no SetContextAttributes or later entries.
 */
static SecurityFunctionTableW g_table_w = {
    .dwVersion = SECURITY_SUPPORT_PROVIDER_INTERFACE_VERSION,
    .EnumerateSecurityPackagesW = EnumerateSecurityPackagesW,
    .QueryCredentialsAttributesW = QueryCredentialsAttributesW,
    .AcquireCredentialsHandleW = AcquireCredentialsHandleW,
    .FreeCredentialsHandle = FreeCredentialsHandle,
    .InitializeSecurityContextW = InitializeSecurityContextW,
    .AcceptSecurityContext = AcceptSecurityContext,
    .CompleteAuthToken = CompleteAuthToken,
    .DeleteSecurityContext = DeleteSecurityContext,
    .ApplyControlToken = ApplyControlToken,
    .QueryContextAttributesW = QueryContextAttributesW,
    .ImpersonateSecurityContext = ImpersonateSecurityContext,
    .RevertSecurityContext = RevertSecurityContext,
    .MakeSignature = MakeSignature,
    .VerifySignature = VerifySignature,
    .FreeContextBuffer = FreeContextBuffer,
    .QuerySecurityPackageInfoW = QuerySecurityPackageInfoW,
    .Reserved3 = (void *)EncryptMessage,
    .Reserved4 = (void *)DecryptMessage,
    .ExportSecurityContext = ExportSecurityContext,
    .ImportSecurityContextW = ImportSecurityContextW,
    .AddCredentialsW = AddCredentialsW,
    .QuerySecurityContextToken = QuerySecurityContextToken,
    .EncryptMessage = EncryptMessage,
    .DecryptMessage = DecryptMessage,
};

static SecurityFunctionTableA g_table_a = {
    .dwVersion = SECURITY_SUPPORT_PROVIDER_INTERFACE_VERSION,
    .EnumerateSecurityPackagesA = EnumerateSecurityPackagesA,
    .QueryCredentialsAttributesA = QueryCredentialsAttributesA,
    .AcquireCredentialsHandleA = AcquireCredentialsHandleA,
    .FreeCredentialsHandle = FreeCredentialsHandle,
    .InitializeSecurityContextA = InitializeSecurityContextA,
    .AcceptSecurityContext = AcceptSecurityContext,
    .CompleteAuthToken = CompleteAuthToken,
    .DeleteSecurityContext = DeleteSecurityContext,
    .ApplyControlToken = ApplyControlToken,
    .QueryContextAttributesA = QueryContextAttributesA,
    .ImpersonateSecurityContext = ImpersonateSecurityContext,
    .RevertSecurityContext = RevertSecurityContext,
    .MakeSignature = MakeSignature,
    .VerifySignature = VerifySignature,
    .FreeContextBuffer = FreeContextBuffer,
    .QuerySecurityPackageInfoA = QuerySecurityPackageInfoA,
    .Reserved3 = (void *)EncryptMessage,
    .Reserved4 = (void *)DecryptMessage,
    .ExportSecurityContext = ExportSecurityContext,
    .ImportSecurityContextA = ImportSecurityContextA,
    .AddCredentialsA = AddCredentialsA,
    .QuerySecurityContextToken = QuerySecurityContextToken,
    .EncryptMessage = EncryptMessage,
    .DecryptMessage = DecryptMessage,
};

SECURITYAPI PSecurityFunctionTableW SEC_ENTRY InitSecurityInterfaceW(void)
{
    return &g_table_w;
}

SECURITYAPI PSecurityFunctionTableA SEC_ENTRY InitSecurityInterfaceA(void)
{
    return &g_table_a;
}
