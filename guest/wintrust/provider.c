/*
 * the registry of trust providers, which WinVerifyTrust here does not consult: its checks are built in. an action
 * can be registered, but no provider is ever loaded from the registry.
 */
#include "trust_private.h"

#define PROVIDER_ROOT L"Software\\Microsoft\\Cryptography\\Providers\\Trust\\"

/* a certificate is self signed when its key ids agree, or when it has none and its issuer is its subject. */
BOOL WINAPI WTHelperCertIsSelfSigned(DWORD encoding, CERT_INFO *cert)
{
    BOOL result;
    CRYPT_DATA_BLOB *authority = nullptr, *subject = nullptr;
    DWORD size;
    DWORD flags = CRYPT_DECODE_ALLOC_FLAG | CRYPT_DECODE_NOCOPY_FLAG | CRYPT_DECODE_SHARE_OID_STRING_FLAG;
    PCERT_EXTENSION authority_ext = CertFindExtension(szOID_AUTHORITY_KEY_IDENTIFIER2, cert->cExtension, cert->rgExtension);
    PCERT_EXTENSION subject_ext = CertFindExtension(szOID_SUBJECT_KEY_IDENTIFIER, cert->cExtension, cert->rgExtension);
    if (authority_ext && subject_ext) {
        if (!CryptDecodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, szOID_AUTHORITY_KEY_IDENTIFIER2,
                                 authority_ext->Value.pbData, authority_ext->Value.cbData, flags, nullptr, &authority,
                                 &size))
            authority = nullptr;
        if (!CryptDecodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, szOID_SUBJECT_KEY_IDENTIFIER,
                                 subject_ext->Value.pbData, subject_ext->Value.cbData, flags, nullptr, &subject, &size))
            subject = nullptr;
    }
    if (authority && authority->cbData && subject && subject->cbData)
        result = authority->cbData == subject->cbData && !memcmp(authority->pbData, subject->pbData, authority->cbData);
    else
        result = CertCompareCertificateName(encoding, &cert->Issuer, &cert->Subject);
    if (authority)
        LocalFree(authority);
    if (subject)
        LocalFree(subject);
    return result;
}

/* {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx} in upper case. */
static void format_guid(const GUID *guid, WCHAR out[39])
{
    static const WCHAR digits[] = L"0123456789ABCDEF";
    const BYTE *b = (const BYTE *)guid;
    /* the bytes in the order they print, with the dashes between. */
    static const BYTE order[16] = {3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15};
    int at = 0;
    out[at++] = L'{';
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            out[at++] = L'-';
        out[at++] = digits[b[order[i]] >> 4];
        out[at++] = digits[b[order[i]] & 15];
    }
    out[at++] = L'}';
    out[at] = 0;
}

static BOOL set_provider(const GUID *action, const WCHAR *group, const WCHAR *dll, const WCHAR *function)
{
    WCHAR id[39], path[128];
    if (!action || !group || !dll || !function || wcslen(group) + 39 > RTL_NUMBER_OF(path)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    format_guid(action, id);
    wcscpy(path, group);
    wcscat(path, id);
    HKEY key;
    LSTATUS status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, nullptr, 0, KEY_SET_VALUE | KEY_CREATE_SUB_KEY, nullptr,
                                     &key, nullptr);
    if (status) {
        SetLastError(status);
        return FALSE;
    }
    status = RegSetValueExW(key, L"$DLL", 0, REG_SZ, (const BYTE *)dll, (DWORD)((wcslen(dll) + 1) * sizeof(WCHAR)));
    status |= RegSetValueExW(key, L"$Function", 0, REG_SZ, (const BYTE *)function,
                             (DWORD)((wcslen(function) + 1) * sizeof(WCHAR)));
    RegCloseKey(key);
    return status == 0;
}

BOOL WINAPI WintrustAddActionID(GUID *action, DWORD flags, CRYPT_REGISTER_ACTIONID *info)
{
    /* the seven entries before the cleanup one are required, and the cleanup one is read when the structure has it. */
    if (!info || info->cbStruct < offsetof(CRYPT_REGISTER_ACTIONID, sCleanupProvider)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    static const struct {
        const WCHAR *Group;
        SIZE_T Offset;
    } groups[] = {
        {PROVIDER_ROOT L"Initialization", offsetof(CRYPT_REGISTER_ACTIONID, sInitProvider)},
        {PROVIDER_ROOT L"Message", offsetof(CRYPT_REGISTER_ACTIONID, sObjectProvider)},
        {PROVIDER_ROOT L"Signature", offsetof(CRYPT_REGISTER_ACTIONID, sSignatureProvider)},
        {PROVIDER_ROOT L"Certificate", offsetof(CRYPT_REGISTER_ACTIONID, sCertificateProvider)},
        {PROVIDER_ROOT L"CertCheck", offsetof(CRYPT_REGISTER_ACTIONID, sCertificatePolicyProvider)},
        {PROVIDER_ROOT L"FinalPolicy", offsetof(CRYPT_REGISTER_ACTIONID, sFinalPolicyProvider)},
        {PROVIDER_ROOT L"DiagnosticPolicy", offsetof(CRYPT_REGISTER_ACTIONID, sTestPolicyProvider)},
        {PROVIDER_ROOT L"Cleanup", offsetof(CRYPT_REGISTER_ACTIONID, sCleanupProvider)},
    };
    BOOL ok = TRUE;
    DWORD error = 0;
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(groups); i++) {
        if (i == RTL_NUMBER_OF(groups) - 1 && info->cbStruct < sizeof(*info))
            break;
        const CRYPT_TRUST_REG_ENTRY *entry = (const CRYPT_TRUST_REG_ENTRY *)((const BYTE *)info + groups[i].Offset);
        if (!set_provider(action, groups[i].Group, entry->pwszDLLName, entry->pwszFunctionName) && ok) {
            ok = FALSE;
            error = GetLastError();
        }
    }
    /* with the flag set, a registration that failed fails the call. */
    if (!ok && (flags & 1)) {
        SetLastError(error);
        return FALSE;
    }
    return TRUE;
}

/* no provider is loaded from the registry here, so every action is one that has none. */
BOOL WINAPI WintrustLoadFunctionPointers(GUID *action, CRYPT_PROVIDER_FUNCTIONS *functions)
{
    return FALSE;
}
