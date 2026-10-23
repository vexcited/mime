/*
 * sspi's sasl wrappers. a sasl profile names the package behind a mechanism; a windows 11 installation has only GSSAPI,
 * the Kerberos package, under SecurityProviders\SaslProfiles in the SYSTEM hive.
 */
#include "secur32_private.h"

#define SASL_OPTION_SEND_SIZE 1
#define SASL_OPTION_RECV_SIZE 2
#define SASL_OPTION_AUTHZ_STRING 3
#define SASL_OPTION_AUTHZ_PROCESSING 4

static const struct {
    PCWSTR Profile;
    PCWSTR Package;
} g_profiles[] = {{L"GSSAPI", L"Kerberos"}};

/* defaults for the next sasl contexts, set when SaslSetContextOption gets no context. */
static ULONG g_send_size, g_recv_size, g_authz_processing;

SECURITYAPI SECURITY_STATUS SEC_ENTRY SaslEnumerateProfilesW(LPWSTR *list, ULONG *count)
{
    if (!list || !count)
        return SEC_E_INVALID_PARAMETER;
    *count = RTL_NUMBER_OF(g_profiles);
    SIZE_T size = sizeof(WCHAR);
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_profiles); i++)
        size += (wcslen(g_profiles[i].Profile) + 1) * sizeof(WCHAR);
    PWSTR out = LocalAlloc(LMEM_FIXED, size);
    if (!out)
        return SEC_E_INSUFFICIENT_MEMORY;
    PWSTR at = out;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_profiles); i++) {
        SIZE_T n = wcslen(g_profiles[i].Profile) + 1;
        memcpy(at, g_profiles[i].Profile, n * sizeof(WCHAR));
        at += n;
    }
    *at = 0;
    *list = out;
    return SEC_E_OK;
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY SaslEnumerateProfilesA(LPSTR *list, ULONG *count)
{
    if (!list || !count)
        return SEC_E_INVALID_PARAMETER;
    *count = RTL_NUMBER_OF(g_profiles);
    SIZE_T size = 1;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_profiles); i++)
        size += wcslen(g_profiles[i].Profile) + 1;
    PSTR out = LocalAlloc(LMEM_FIXED, size);
    if (!out)
        return SEC_E_INSUFFICIENT_MEMORY;
    PSTR at = out;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_profiles); i++) {
        PCWSTR p = g_profiles[i].Profile;
        do
            *at++ = (CHAR)*p;
        while (*p++);
    }
    *at = 0;
    *list = out;
    return SEC_E_OK;
}

static const SEC_PACKAGE *profile_package(PCWSTR profile)
{
    for (ULONG i = 0; profile && i < RTL_NUMBER_OF(g_profiles); i++) {
        if (!_wcsicmp(g_profiles[i].Profile, profile))
            return SecFindPackageW(g_profiles[i].Package);
    }
    return nullptr;
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY SaslGetProfilePackageW(LPWSTR profile, PSecPkgInfoW *info)
{
    return SecCopyPackageInfoW(profile_package(profile), info);
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY SaslGetProfilePackageA(LPSTR profile, PSecPkgInfoA *info)
{
    PWSTR wide = SecAnsiToWide(profile);
    const SEC_PACKAGE *package = profile_package(wide);
    SecHeapFree(wide);
    return SecCopyPackageInfoA(package, info);
}

/*
 * a gss token is matched by the mechanism oid at its start, and a token starting with a negotiation response, 0xA0,
 * matches the first prefix. the prefixes are the kerberos oid and its legacy microsoft form. the returned info is
 * static and must not be freed.
 */
static const UCHAR g_kerberos_oid[] = {0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x12, 0x01, 0x02, 0x02};
static const UCHAR g_kerberos_legacy_oid[] = {0x06, 0x09, 0x2A, 0x86, 0x48, 0x82, 0xF7, 0x12, 0x01, 0x02, 0x02};
static SecPkgInfoW g_kerberos_w = {0x028F3BBF, 1, 16, 48000, (SEC_WCHAR *)L"Kerberos",
                                   (SEC_WCHAR *)L"Microsoft Kerberos V1.0"};
static SecPkgInfoA g_kerberos_a = {0x028F3BBF, 1, 16, 48000, (SEC_CHAR *)"Kerberos",
                                   (SEC_CHAR *)"Microsoft Kerberos V1.0"};

static SECURITY_STATUS identify(PSecBufferDesc input)
{
    if (!input || !input->pBuffers)
        return SEC_E_INVALID_TOKEN;
    PSecBuffer token = nullptr;
    for (ULONG i = 0; i < input->cBuffers && !token; i++) {
        if ((input->pBuffers[i].BufferType & ~SECBUFFER_ATTRMASK) == SECBUFFER_TOKEN)
            token = &input->pBuffers[i];
    }
    if (!token || !token->cbBuffer || !token->pvBuffer)
        return SEC_E_INVALID_TOKEN;
    const UCHAR *p = token->pvBuffer;
    ULONG left = token->cbBuffer - 1;
    if (p[0] == 0xA0)
        return SEC_E_OK;
    if (p[0] != 0x60 || left < 1)
        return SEC_E_INVALID_TOKEN;
    ULONG length = p[1];
    left--;
    p += 2;
    if (length & 0x80) {
        ULONG bytes = length & 0x7F;
        if (bytes + 1 > left || bytes > 4)
            return SEC_E_INVALID_TOKEN;
        p += bytes;
        left -= bytes;
    }
    if (left < 2 || p[0] != 0x06 || (ULONG)p[1] + 2 > left)
        return SEC_E_INVALID_TOKEN;
    ULONG oid = p[1] + 2;
    if (oid >= sizeof(g_kerberos_oid) &&
        (!memcmp(p, g_kerberos_oid, sizeof(g_kerberos_oid)) ||
         !memcmp(p, g_kerberos_legacy_oid, sizeof(g_kerberos_legacy_oid))))
        return SEC_E_OK;
    return SEC_E_INVALID_TOKEN;
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY SaslIdentifyPackageW(PSecBufferDesc input, PSecPkgInfoW *info)
{
    SECURITY_STATUS status = identify(input);
    if (status == SEC_E_OK && info)
        *info = &g_kerberos_w;
    return status;
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY SaslIdentifyPackageA(PSecBufferDesc input, PSecPkgInfoA *info)
{
    SECURITY_STATUS status = identify(input);
    if (status == SEC_E_OK && info)
        *info = &g_kerberos_a;
    return status;
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY SaslInitializeSecurityContextW(PCredHandle credential, PCtxtHandle context,
                                                                     LPWSTR target, ULONG flags, ULONG reserved1,
                                                                     ULONG representation, PSecBufferDesc input,
                                                                     ULONG reserved2, PCtxtHandle new_context,
                                                                     PSecBufferDesc output, PULONG attributes,
                                                                     PTimeStamp expiry)
{
    return InitializeSecurityContextW(credential, context, target, flags, reserved1, representation, input, reserved2,
                                      new_context, output, attributes, expiry);
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY SaslInitializeSecurityContextA(PCredHandle credential, PCtxtHandle context,
                                                                     LPSTR target, ULONG flags, ULONG reserved1,
                                                                     ULONG representation, PSecBufferDesc input,
                                                                     ULONG reserved2, PCtxtHandle new_context,
                                                                     PSecBufferDesc output, PULONG attributes,
                                                                     PTimeStamp expiry)
{
    return InitializeSecurityContextA(credential, context, target, flags, reserved1, representation, input, reserved2,
                                      new_context, output, attributes, expiry);
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY SaslAcceptSecurityContext(PCredHandle credential, PCtxtHandle context,
                                                                PSecBufferDesc input, ULONG flags,
                                                                ULONG representation, PCtxtHandle new_context,
                                                                PSecBufferDesc output, PULONG attributes,
                                                                PTimeStamp expiry)
{
    return AcceptSecurityContext(credential, context, input, flags, representation, new_context, output, attributes,
                                 expiry);
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY SaslGetContextOption(PCtxtHandle context, ULONG option, PVOID value, ULONG size,
                                                           PULONG needed)
{
    return SEC_E_INVALID_HANDLE;
}

/* there are never sasl contexts, so the sizes and authz processing only set the defaults. */
SECURITYAPI SECURITY_STATUS SEC_ENTRY SaslSetContextOption(PCtxtHandle context, ULONG option, PVOID value, ULONG size)
{
    ULONG *target;
    switch (option) {
    case SASL_OPTION_SEND_SIZE:
        target = &g_send_size;
        break;
    case SASL_OPTION_RECV_SIZE:
        target = &g_recv_size;
        break;
    case SASL_OPTION_AUTHZ_PROCESSING:
        target = &g_authz_processing;
        break;
    case SASL_OPTION_AUTHZ_STRING:
        return SEC_E_INVALID_HANDLE;
    default:
        return SEC_E_UNSUPPORTED_FUNCTION;
    }
    if (size < sizeof(ULONG))
        return SEC_E_BUFFER_TOO_SMALL;
    if (!value)
        return SEC_E_INVALID_PARAMETER;
    *target = *(const ULONG *)value;
    return SEC_E_OK;
}
