/*
 * the object identifier table: CryptFindOIDInfo and the algorithm id
 * conversions. the rdn attribute rows, their names and the string types
 * they allow were read from crypt32.dll of windows 11.
 */
#include "crypt32_private.h"

static const DWORD types_ia5[] = {CERT_RDN_IA5_STRING, 0};
static const DWORD types_printable[] = {CERT_RDN_PRINTABLE_STRING, 0};
static const DWORD types_numeric[] = {CERT_RDN_NUMERIC_STRING, 0};
static const DWORD types_ia5_utf8[] = {CERT_RDN_IA5_STRING, CERT_RDN_UTF8_STRING, 0};
static const DWORD types_utf8[] = {CERT_RDN_UTF8_STRING, 0};
static const DWORD sign_rsa[] = {CALG_RSA_SIGN, 0};
static const DWORD sign_ecdsa[] = {CALG_OID_INFO_CNG_ONLY, 0};

#define EXTRA(a) {sizeof(a), (BYTE *)(a)}
#define ROW(oid, name, group, value) {sizeof(CRYPT_OID_INFO), oid, name, group, {value}, {0, NULL}, NULL, NULL}
#define ROWX(oid, name, group, value, extra) {sizeof(CRYPT_OID_INFO), oid, name, group, {value}, EXTRA(extra), NULL, NULL}
#define ROWC(oid, name, group, value, extra, cng, cng2)                                                          \
    {sizeof(CRYPT_OID_INFO), oid, name, group, {value}, EXTRA(extra), cng, cng2}

static const CRYPT_OID_INFO oid_table[] = {
    /* hashes. */
    {sizeof(CRYPT_OID_INFO), szOID_OIWSEC_sha1, L"sha1", 1, {CALG_SHA1}, {0, NULL}, L"SHA1", NULL},
    {sizeof(CRYPT_OID_INFO), szOID_RSA_MD5, L"md5", 1, {CALG_MD5}, {0, NULL}, L"MD5", NULL},
    {sizeof(CRYPT_OID_INFO), szOID_NIST_sha256, L"sha256", 1, {CALG_SHA_256}, {0, NULL}, L"SHA256", NULL},
    {sizeof(CRYPT_OID_INFO), szOID_NIST_sha384, L"sha384", 1, {CALG_SHA_384}, {0, NULL}, L"SHA384", NULL},
    {sizeof(CRYPT_OID_INFO), szOID_NIST_sha512, L"sha512", 1, {CALG_SHA_512}, {0, NULL}, L"SHA512", NULL},
    /* public keys. */
    {sizeof(CRYPT_OID_INFO), szOID_RSA_RSA, L"RSA", 3, {CALG_RSA_KEYX}, {0, NULL}, L"RSA", NULL},
    {sizeof(CRYPT_OID_INFO), "1.2.840.10040.4.1", L"DSA", 3, {CALG_DSS_SIGN}, {0, NULL}, L"DSA", NULL},
    {sizeof(CRYPT_OID_INFO), szOID_ECC_PUBLIC_KEY, L"ECC", 3, {CALG_OID_INFO_PARAMETERS}, {0, NULL}, L"ECC", NULL},
    /* signatures. */
    ROWC(szOID_RSA_SHA256RSA, L"sha256RSA", 4, CALG_SHA_256, sign_rsa, L"SHA256", L"RSA"),
    ROWC(szOID_RSA_SHA384RSA, L"sha384RSA", 4, CALG_SHA_384, sign_rsa, L"SHA384", L"RSA"),
    ROWC(szOID_RSA_SHA512RSA, L"sha512RSA", 4, CALG_SHA_512, sign_rsa, L"SHA512", L"RSA"),
    ROWC(szOID_RSA_SHA1RSA, L"sha1RSA", 4, CALG_SHA1, sign_rsa, L"SHA1", L"RSA"),
    ROWC(szOID_OIWSEC_sha1RSASign, L"sha1RSA", 4, CALG_SHA1, sign_rsa, L"SHA1", L"RSA"),
    ROWC(szOID_RSA_MD5RSA, L"md5RSA", 4, CALG_MD5, sign_rsa, L"MD5", L"RSA"),
    ROWC(szOID_RSA_SSA_PSS, L"RSASSA-PSS", 4, CALG_OID_INFO_PARAMETERS, sign_rsa, L"CryptOIDInfoParameters",
         L"RSA"),
    ROWC(szOID_ECDSA_SHA1, L"sha1ECDSA", 4, CALG_SHA1, sign_ecdsa, L"SHA1", L"ECDSA"),
    ROWC(szOID_ECDSA_SHA256, L"sha256ECDSA", 4, CALG_OID_INFO_CNG_ONLY, sign_ecdsa, L"SHA256", L"ECDSA"),
    ROWC(szOID_ECDSA_SHA384, L"sha384ECDSA", 4, CALG_OID_INFO_CNG_ONLY, sign_ecdsa, L"SHA384", L"ECDSA"),
    ROWC(szOID_ECDSA_SHA512, L"sha512ECDSA", 4, CALG_OID_INFO_CNG_ONLY, sign_ecdsa, L"SHA512", L"ECDSA"),
    /* relative distinguished name attributes. */
    ROW(szOID_COMMON_NAME, L"CN", 5, 0),
    ROW(szOID_LOCALITY_NAME, L"L", 5, 0),
    ROW(szOID_ORGANIZATION_NAME, L"O", 5, 0),
    ROW(szOID_ORGANIZATIONAL_UNIT_NAME, L"OU", 5, 0),
    ROWX(szOID_RSA_emailAddr, L"E", 5, 0, types_ia5),
    ROWX(szOID_RSA_emailAddr, L"Email", 5, 0, types_ia5),
    ROWX(szOID_COUNTRY_NAME, L"C", 5, 0, types_printable),
    ROW(szOID_STATE_OR_PROVINCE_NAME, L"S", 5, 0),
    ROW(szOID_STATE_OR_PROVINCE_NAME, L"ST", 5, 0),
    ROW(szOID_STREET_ADDRESS, L"STREET", 5, 0),
    ROW(szOID_TITLE, L"T", 5, 0),
    ROW(szOID_TITLE, L"Title", 5, 0),
    ROW(szOID_GIVEN_NAME, L"G", 5, 0),
    ROW(szOID_GIVEN_NAME, L"GN", 5, 0),
    ROW(szOID_GIVEN_NAME, L"GivenName", 5, 0),
    ROW(szOID_INITIALS, L"I", 5, 0),
    ROW(szOID_INITIALS, L"Initials", 5, 0),
    ROW(szOID_SUR_NAME, L"SN", 5, 0),
    ROWX(szOID_DEVICE_SERIAL_NUMBER, L"SERIALNUMBER", 5, 0, types_printable),
    ROWX(szOID_DOMAIN_COMPONENT, L"DC", 5, 0, types_ia5_utf8),
    ROW(szOID_DESCRIPTION, L"Description", 5, 0),
    ROW("2.5.4.17", L"PostalCode", 5, 0),
    ROW("2.5.4.18", L"POBox", 5, 0),
    ROWX("2.5.4.20", L"Phone", 5, 0, types_printable),
    ROWX("2.5.4.24", L"X21Address", 5, 0, types_numeric),
    ROW("2.5.4.46", L"dnQualifier", 5, 0),
    ROWX("2.23.133.2.1", L"TPMManufacturer", 5, 0, types_utf8),
    ROWX("2.23.133.2.2", L"TPMModel", 5, 0, types_utf8),
    ROWX("2.23.133.2.3", L"TPMVersion", 5, 0, types_utf8),
    /* extensions and attributes. */
    ROW(szOID_AUTHORITY_KEY_IDENTIFIER, L"Authority Key Identifier", 6, 0),
    ROW(szOID_KEY_ATTRIBUTES, L"Key Attributes", 6, 0),
    ROW(szOID_SUBJECT_ALT_NAME, L"Subject Alternative Name", 6, 0),
    ROW(szOID_ISSUER_ALT_NAME, L"Issuer Alternative Name", 6, 0),
    ROW(szOID_BASIC_CONSTRAINTS, L"Basic Constraints", 6, 0),
    ROW(szOID_SUBJECT_KEY_IDENTIFIER, L"Subject Key Identifier", 6, 0),
    ROW(szOID_KEY_USAGE, L"Key Usage", 6, 0),
    ROW(szOID_SUBJECT_ALT_NAME2, L"Subject Alternative Name", 6, 0),
    ROW(szOID_ISSUER_ALT_NAME2, L"Issuer Alternative Name", 6, 0),
    ROW(szOID_BASIC_CONSTRAINTS2, L"Basic Constraints", 6, 0),
    ROW(szOID_CRL_NUMBER, L"CRL Number", 6, 0),
    ROW(szOID_CRL_REASON_CODE, L"CRL Reason Code", 6, 0),
    ROW("2.5.29.31", L"CRL Distribution Points", 6, 0),
    ROW("2.5.29.32", L"Certificate Policies", 6, 0),
    ROW(szOID_AUTHORITY_KEY_IDENTIFIER2, L"Authority Key Identifier", 6, 0),
    ROW(szOID_ENHANCED_KEY_USAGE, L"Enhanced Key Usage", 6, 0),
    ROW("1.3.6.1.5.5.7.1.1", L"Authority Information Access", 6, 0),
    /* enhanced key usages. */
    ROW(szOID_PKIX_KP_SERVER_AUTH, L"Server Authentication", 7, 0),
    ROW(szOID_PKIX_KP_CLIENT_AUTH, L"Client Authentication", 7, 0),
    ROW(szOID_PKIX_KP_CODE_SIGNING, L"Code Signing", 7, 0),
    ROW(szOID_PKIX_KP_EMAIL_PROTECTION, L"Secure Email", 7, 0),
    ROW(szOID_PKIX_KP_TIMESTAMP_SIGNING, L"Time Stamping", 7, 0),
    ROW(szOID_PKIX_KP_OCSP_SIGNING, L"OCSP Signing", 7, 0),
    ROW(szOID_ANY_ENHANCED_KEY_USAGE, L"Any Purpose", 7, 0),
};

static BOOL group_matches(const CRYPT_OID_INFO *info, DWORD group)
{
    group &= 0xFFFF;
    return !group || info->dwGroupId == group;
}

PCCRYPT_OID_INFO CrFindOid(LPCSTR oid, DWORD group)
{
    for (DWORD i = 0; i < RTL_NUMBER_OF(oid_table); i++)
        if (group_matches(&oid_table[i], group) && !strcmp(oid_table[i].pszOID, oid))
            return &oid_table[i];
    return NULL;
}

PCCRYPT_OID_INFO CrFindOidByName(LPCWSTR name, DWORD group)
{
    for (DWORD i = 0; i < RTL_NUMBER_OF(oid_table); i++)
        if (group_matches(&oid_table[i], group) && !_wcsicmp(oid_table[i].pwszName, name))
            return &oid_table[i];
    return NULL;
}

PCCRYPT_OID_INFO WINAPI CryptFindOIDInfo(DWORD dwKeyType, void *pvKey, DWORD dwGroupId)
{
    if (!pvKey)
        return NULL;
    for (DWORD i = 0; i < RTL_NUMBER_OF(oid_table); i++) {
        const CRYPT_OID_INFO *info = &oid_table[i];
        if (!group_matches(info, dwGroupId))
            continue;
        switch (dwKeyType) {
        case CRYPT_OID_INFO_OID_KEY:
            if (!strcmp(info->pszOID, pvKey))
                return info;
            break;
        case CRYPT_OID_INFO_NAME_KEY:
            if (!_wcsicmp(info->pwszName, pvKey))
                return info;
            break;
        case CRYPT_OID_INFO_ALGID_KEY:
            if (info->Algid == *(const ALG_ID *)pvKey && info->Algid != CALG_OID_INFO_CNG_ONLY &&
                info->Algid != CALG_OID_INFO_PARAMETERS)
                return info;
            break;
        case CRYPT_OID_INFO_SIGN_KEY: {
            const ALG_ID *pair = pvKey;
            if (info->dwGroupId == CRYPT_SIGN_ALG_OID_GROUP_ID && info->Algid == pair[0] &&
                info->ExtraInfo.cbData >= sizeof(DWORD) && *(const DWORD *)info->ExtraInfo.pbData == pair[1])
                return info;
            break;
        }
        case CRYPT_OID_INFO_CNG_ALGID_KEY:
            if (info->pwszCNGAlgid && !_wcsicmp(info->pwszCNGAlgid, pvKey))
                return info;
            break;
        case CRYPT_OID_INFO_CNG_SIGN_KEY: {
            LPCWSTR *pair = pvKey;
            if (info->dwGroupId == CRYPT_SIGN_ALG_OID_GROUP_ID && info->pwszCNGAlgid && info->pwszCNGExtraAlgid &&
                !_wcsicmp(info->pwszCNGAlgid, pair[0]) && !_wcsicmp(info->pwszCNGExtraAlgid, pair[1]))
                return info;
            break;
        }
        default:
            return NULL;
        }
    }
    return NULL;
}

DWORD WINAPI CertOIDToAlgId(LPCSTR pszObjId)
{
    if (!pszObjId)
        return 0;
    for (DWORD i = 0; i < RTL_NUMBER_OF(oid_table); i++)
        if (oid_table[i].dwGroupId != CRYPT_RDN_ATTR_OID_GROUP_ID && !strcmp(oid_table[i].pszOID, pszObjId))
            return oid_table[i].Algid;
    return 0;
}

LPCSTR WINAPI CertAlgIdToOID(DWORD dwAlgId)
{
    ALG_ID alg = dwAlgId;
    PCCRYPT_OID_INFO info = CryptFindOIDInfo(CRYPT_OID_INFO_ALGID_KEY, &alg, 0);
    return info ? info->pszOID : NULL;
}

LPVOID WINAPI CryptMemAlloc(ULONG cbSize)
{
    return LocalAlloc(LMEM_FIXED, cbSize);
}

LPVOID WINAPI CryptMemRealloc(LPVOID pv, ULONG cbSize)
{
    return LocalReAlloc(pv, cbSize, LMEM_MOVEABLE);
}

void WINAPI CryptMemFree(LPVOID pv)
{
    LocalFree(pv);
}
