/*
 * server certificates for the tls engine: crypt32 decodes them into a memory store, extracts the leaf's public key and
 * checks the chain with its ssl policy. crypt32 is loaded on the first handshake, so programs that never use tls do not
 * pay for it.
 */
#include "schannel.h"

#ifndef CERT_E_EXPIRED
#define CERT_E_EXPIRED ((HRESULT)0x800B0101L)
#endif
#ifndef CERT_E_VALIDITYPERIODNESTING
#define CERT_E_VALIDITYPERIODNESTING ((HRESULT)0x800B0102L)
#endif
#ifndef CERT_E_UNTRUSTEDROOT
#define CERT_E_UNTRUSTEDROOT ((HRESULT)0x800B0109L)
#endif
#ifndef CERT_E_CHAINING
#define CERT_E_CHAINING ((HRESULT)0x800B010AL)
#endif
#ifndef CERT_E_WRONG_USAGE
#define CERT_E_WRONG_USAGE ((HRESULT)0x800B0110L)
#endif

static struct {
    typeof(CertOpenStore) *OpenStore;
    typeof(CertCloseStore) *CloseStore;
    typeof(CertAddEncodedCertificateToStore) *AddEncoded;
    typeof(CertDuplicateCertificateContext) *Duplicate;
    typeof(CertFreeCertificateContext) *FreeContext;
    typeof(CertGetCertificateChain) *GetChain;
    typeof(CertFreeCertificateChain) *FreeChain;
    typeof(CertVerifyCertificateChainPolicy) *VerifyPolicy;
} crypt32;

static INIT_ONCE crypt32_once = INIT_ONCE_STATIC_INIT;

static BOOL WINAPI load_crypt32(INIT_ONCE *once, void *parameter, void **context)
{
    HMODULE module = LoadLibraryW(L"crypt32.dll");
    if (!module)
        return TRUE;
    crypt32.OpenStore = (void *)GetProcAddress(module, "CertOpenStore");
    crypt32.CloseStore = (void *)GetProcAddress(module, "CertCloseStore");
    crypt32.AddEncoded = (void *)GetProcAddress(module, "CertAddEncodedCertificateToStore");
    crypt32.Duplicate = (void *)GetProcAddress(module, "CertDuplicateCertificateContext");
    crypt32.FreeContext = (void *)GetProcAddress(module, "CertFreeCertificateContext");
    crypt32.GetChain = (void *)GetProcAddress(module, "CertGetCertificateChain");
    crypt32.FreeChain = (void *)GetProcAddress(module, "CertFreeCertificateChain");
    crypt32.VerifyPolicy = (void *)GetProcAddress(module, "CertVerifyCertificateChainPolicy");
    return TRUE;
}

static bool have_crypt32(void)
{
    InitOnceExecuteOnce(&crypt32_once, load_crypt32, nullptr, nullptr);
    return crypt32.OpenStore && crypt32.CloseStore && crypt32.AddEncoded && crypt32.Duplicate &&
           crypt32.FreeContext && crypt32.GetChain && crypt32.FreeChain && crypt32.VerifyPolicy;
}

/* ------------------------------------------------------------------ crypto library hooks */

typedef BOOLEAN(WINAPI *RTL_GEN_RANDOM)(PVOID, ULONG);

static INIT_ONCE random_once = INIT_ONCE_STATIC_INIT;
static RTL_GEN_RANDOM gen_random;

static BOOL WINAPI load_random(INIT_ONCE *once, void *parameter, void **context)
{
    HMODULE advapi32 = LoadLibraryW(L"advapi32.dll");
    if (advapi32)
        gen_random = (RTL_GEN_RANDOM)(void *)GetProcAddress(advapi32, "SystemFunction036");
    return TRUE;
}

bool SchRandom(void *out, SIZE_T size)
{
    InitOnceExecuteOnce(&random_once, load_random, nullptr, nullptr);
    BYTE *p = out;
    while (size) {
        ULONG n = size > 0x10000 ? 0x10000 : (ULONG)size;
        if (!gen_random || !gen_random(p, n))
            return false;
        p += n;
        size -= n;
    }
    return true;
}

void *mime_crypto_alloc(size_t size)
{
    return SecHeapAlloc(size);
}

void mime_crypto_free(void *p)
{
    SecHeapFree(p);
}

bool mime_crypto_random(void *out, size_t size)
{
    return SchRandom(out, size);
}

/* ------------------------------------------------------------------ certificates */

SECURITY_STATUS SchLoadCertificates(TLS_CERTS *certs, const BYTE *const *der, const SIZE_T *sizes, DWORD count)
{
    SchFreeCertificates(certs);
    if (!have_crypt32())
        return SEC_E_INTERNAL_ERROR;
    certs->Store = crypt32.OpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, nullptr);
    if (!certs->Store)
        return SEC_E_INSUFFICIENT_MEMORY;
    for (DWORD i = 0; i < count; i++) {
        PCCERT_CONTEXT added = nullptr;
        if (!crypt32.AddEncoded(certs->Store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, der[i], (DWORD)sizes[i],
                                CERT_STORE_ADD_ALWAYS, i ? nullptr : &added)) {
            /* only the leaf must decode; a broken intermediate only breaks the chain. */
            if (!i) {
                SchFreeCertificates(certs);
                return SEC_E_CERT_UNKNOWN;
            }
            continue;
        }
        if (!i)
            certs->Leaf = added;
    }
    return SEC_E_OK;
}

void SchFreeCertificates(TLS_CERTS *certs)
{
    if (certs->Leaf)
        crypt32.FreeContext(certs->Leaf);
    if (certs->Store)
        crypt32.CloseStore(certs->Store, 0);
    certs->Leaf = nullptr;
    certs->Store = nullptr;
}

PCCERT_CONTEXT SchDuplicate(PCCERT_CONTEXT cert)
{
    return crypt32.Duplicate(cert);
}

void SchFreeCertificate(PCCERT_CONTEXT cert)
{
    crypt32.FreeContext(cert);
}

/* one der element of the given tag. */
static bool der_element(const BYTE **p, SIZE_T *left, BYTE tag, const BYTE **content, SIZE_T *size)
{
    if (*left < 2 || (*p)[0] != tag)
        return false;
    SIZE_T length = (*p)[1], header = 2;
    if (length & 0x80) {
        SIZE_T bytes = length & 0x7F;
        if (!bytes || bytes > 3 || *left < 2 + bytes)
            return false;
        length = 0;
        for (SIZE_T i = 0; i < bytes; i++)
            length = length << 8 | (*p)[2 + i];
        header += bytes;
    }
    if (*left - header < length)
        return false;
    *content = *p + header;
    *size = length;
    *p += header + length;
    *left -= header + length;
    return true;
}

/* RSAPublicKey, rfc 8017 a.1.1: a SEQUENCE of modulus and exponent. */
static SECURITY_STATUS rsa_key(const CRYPT_BIT_BLOB *bits, TLS_PEER_KEY *key)
{
    const BYTE *p = bits->pbData, *seq, *n, *e;
    SIZE_T left = bits->cbData, seq_size, n_size, e_size;
    if (!der_element(&p, &left, 0x30, &seq, &seq_size) ||
        !der_element(&seq, &seq_size, 0x02, &n, &n_size) || !der_element(&seq, &seq_size, 0x02, &e, &e_size))
        return SEC_E_CERT_UNKNOWN;
    while (n_size && !n[0]) {
        n++;
        n_size--;
    }
    while (e_size && !e[0]) {
        e++;
        e_size--;
    }
    if (n_size < 128 || n_size > 2048 || !e_size || e_size > 8)
        return SEC_E_CERT_UNKNOWN;
    MimeRsa *rsa = mime_rsa_new();
    if (!rsa)
        return SEC_E_INSUFFICIENT_MEMORY;
    if (!mime_bn_from_bytes(&rsa->n, n, n_size) || !mime_bn_from_bytes(&rsa->e, e, e_size) || mime_rsa_check(rsa)) {
        mime_rsa_free(rsa);
        return SEC_E_CERT_UNKNOWN;
    }
    key->Type = 1;
    key->Rsa = rsa;
    return SEC_E_OK;
}

/* an uncompressed point on the named curve of the parameters. */
static SECURITY_STATUS ec_key(const CRYPT_ALGORITHM_IDENTIFIER *algorithm, const CRYPT_BIT_BLOB *bits,
                              TLS_PEER_KEY *key)
{
    static const BYTE p256[] = {0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07};
    static const BYTE p384[] = {0x06, 0x05, 0x2B, 0x81, 0x04, 0x00, 0x22};
    static const BYTE p521[] = {0x06, 0x05, 0x2B, 0x81, 0x04, 0x00, 0x23};
    const CRYPT_OBJID_BLOB *params = &algorithm->Parameters;
    const MimeEcCurve *curve = nullptr;
    if (params->cbData == sizeof(p256) && !memcmp(params->pbData, p256, sizeof(p256)))
        curve = &mime_p256;
    else if (params->cbData == sizeof(p384) && !memcmp(params->pbData, p384, sizeof(p384)))
        curve = &mime_p384;
    else if (params->cbData == sizeof(p521) && !memcmp(params->pbData, p521, sizeof(p521)))
        curve = &mime_p521;
    if (!curve)
        return SEC_E_CERT_UNKNOWN;
    UINT size = mime_ec_size(curve);
    if (bits->cbData != 1 + 2 * size || bits->pbData[0] != 4)
        return SEC_E_CERT_UNKNOWN;
    memset(&key->Ec, 0, sizeof(key->Ec));
    key->Ec.curve = curve;
    memcpy(key->Ec.x, bits->pbData + 1, size);
    memcpy(key->Ec.y, bits->pbData + 1 + size, size);
    if (mime_ec_check_public(&key->Ec))
        return SEC_E_CERT_UNKNOWN;
    key->Type = 2;
    return SEC_E_OK;
}

SECURITY_STATUS SchLeafKey(const TLS_CERTS *certs, TLS_PEER_KEY *key)
{
    SchFreeKey(key);
    if (!certs->Leaf)
        return SEC_E_CERT_UNKNOWN;
    const CERT_PUBLIC_KEY_INFO *info = &certs->Leaf->pCertInfo->SubjectPublicKeyInfo;
    LPCSTR oid = info->Algorithm.pszObjId;
    if (!oid)
        return SEC_E_CERT_UNKNOWN;
    if (!strcmp(oid, szOID_RSA_RSA))
        return rsa_key(&info->PublicKey, key);
    if (!strcmp(oid, szOID_ECC_PUBLIC_KEY))
        return ec_key(&info->Algorithm, &info->PublicKey, key);
    return SEC_E_CERT_UNKNOWN;
}

void SchFreeKey(TLS_PEER_KEY *key)
{
    if (key->Rsa)
        mime_rsa_free(key->Rsa);
    memset(key, 0, sizeof(*key));
}

/* ------------------------------------------------------------------ validation */

SECURITY_STATUS SchValidateChain(const TLS_CERTS *certs, PCWSTR server, ULONG flags, BYTE *alert)
{
    *alert = 46;
    if (!certs->Leaf || !have_crypt32())
        return SEC_E_CERT_UNKNOWN;
    static char server_auth[] = szOID_PKIX_KP_SERVER_AUTH;
    LPSTR usages[] = {server_auth};
    CERT_CHAIN_PARA para = {sizeof(para)};
    para.RequestedUsage.dwType = USAGE_MATCH_TYPE_OR;
    para.RequestedUsage.Usage.cUsageIdentifier = 1;
    para.RequestedUsage.Usage.rgpszUsageIdentifier = usages;
    DWORD chain_flags = 0;
    if (flags & SCH_CRED_REVOCATION_CHECK_END_CERT)
        chain_flags |= CERT_CHAIN_REVOCATION_CHECK_END_CERT;
    if (flags & SCH_CRED_REVOCATION_CHECK_CHAIN)
        chain_flags |= CERT_CHAIN_REVOCATION_CHECK_CHAIN;
    if (flags & SCH_CRED_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT)
        chain_flags |= CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;
    PCCERT_CHAIN_CONTEXT chain = nullptr;
    if (!crypt32.GetChain(nullptr, certs->Leaf, nullptr, certs->Store, &para, chain_flags, nullptr, &chain))
        return SEC_E_CERT_UNKNOWN;

    SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl = {};
    ssl.cbSize = sizeof(ssl);
    ssl.dwAuthType = AUTHTYPE_SERVER;
    ssl.pwszServerName = (WCHAR *)server;
    if (flags & (SCH_CRED_IGNORE_NO_REVOCATION_CHECK | SCH_CRED_IGNORE_REVOCATION_OFFLINE))
        ssl.fdwChecks |= SECURITY_FLAG_IGNORE_REVOCATION;
    CERT_CHAIN_POLICY_PARA policy = {sizeof(policy)};
    policy.pvExtraPolicyPara = &ssl;
    CERT_CHAIN_POLICY_STATUS result = {sizeof(result)};
    BOOL ok = crypt32.VerifyPolicy(CERT_CHAIN_POLICY_SSL, chain, &policy, &result);
    crypt32.FreeChain(chain);
    if (!ok)
        return SEC_E_CERT_UNKNOWN;
    switch ((HRESULT)result.dwError) {
    case 0:
        return SEC_E_OK;
    case CERT_E_UNTRUSTEDROOT:
    case CERT_E_CHAINING:
        *alert = 48;
        return SEC_E_UNTRUSTED_ROOT;
    case CERT_E_EXPIRED:
    case CERT_E_VALIDITYPERIODNESTING:
        *alert = 45;
        return SEC_E_CERT_EXPIRED;
    case CERT_E_CN_NO_MATCH:
        *alert = 42;
        return SEC_E_WRONG_PRINCIPAL;
    case CERT_E_WRONG_USAGE:
        *alert = 43;
        return SEC_E_CERT_WRONG_USAGE;
    case CRYPT_E_REVOKED:
        *alert = 44;
        return CRYPT_E_REVOKED;
    case CRYPT_E_REVOCATION_OFFLINE:
    case CRYPT_E_NO_REVOCATION_CHECK:
        return (SECURITY_STATUS)result.dwError;
    default:
        *alert = 42;
        return SEC_E_CERT_UNKNOWN;
    }
}
