/*
 * certificate signature verification over the shared crypto library: RSA with the PKCS #1 v1.5 padding check,
 * RSA-PSS, and ECDSA on the named curves.
 */
#include "crypt32_private.h"
#include <crypto/crypto.h>

#define MAX_LIMBS MIME_BN_LIMBS

/* ------------------------------------------------------------------ keys */

typedef struct {
    const BYTE *modulus;
    DWORD modulus_size;
    const BYTE *exponent;
    DWORD exponent_size;
} rsa_key;

static DWORD parse_rsa_key(const CERT_PUBLIC_KEY_INFO *key, rsa_key *out)
{
    if (!key->Algorithm.pszObjId || strcmp(key->Algorithm.pszObjId, szOID_RSA_RSA))
        return NTE_BAD_ALGID;
    der_reader r = {key->PublicKey.pbData, key->PublicKey.cbData};
    der_item seq, n, e;
    DWORD error = DerExpect(&r, ASN_SEQUENCE, &seq);
    if (error)
        return NTE_BAD_DATA;
    DerOpen(&r, &seq);
    if (DerExpect(&r, ASN_INTEGER, &n) || DerExpect(&r, ASN_INTEGER, &e))
        return NTE_BAD_DATA;
    out->modulus = n.content;
    out->modulus_size = n.length;
    while (out->modulus_size && !*out->modulus) {
        out->modulus++;
        out->modulus_size--;
    }
    out->exponent = e.content;
    out->exponent_size = e.length;
    if (!out->modulus_size || out->modulus_size > MAX_LIMBS * 4 || !e.length)
        return NTE_BAD_DATA;
    return 0;
}

DWORD CrRsaModulusBits(const CERT_PUBLIC_KEY_INFO *key)
{
    rsa_key rsa;
    if (parse_rsa_key(key, &rsa))
        return 0;
    DWORD bits = rsa.modulus_size * 8;
    for (BYTE top = rsa.modulus[0]; !(top & 0x80); top <<= 1)
        bits--;
    return bits;
}

/* ------------------------------------------------------------------ verification */

static const struct {
    LPCSTR sign;
    ALG_ID hash;
    LPCSTR hash_oid;
} sign_algs[] = {
    {szOID_RSA_MD5RSA, CALG_MD5, szOID_RSA_MD5},           {szOID_RSA_SHA1RSA, CALG_SHA1, szOID_OIWSEC_sha1},
    {szOID_OIWSEC_sha1RSASign, CALG_SHA1, szOID_OIWSEC_sha1}, {szOID_RSA_SHA256RSA, CALG_SHA_256, szOID_NIST_sha256},
    {szOID_RSA_SHA384RSA, CALG_SHA_384, szOID_NIST_sha384}, {szOID_RSA_SHA512RSA, CALG_SHA_512, szOID_NIST_sha512},
};

static DWORD check_padding(const BYTE *em, DWORD k, ALG_ID hash_alg, LPCSTR hash_oid, const BYTE *digest)
{
    if (k < 11 || em[0] != 0 || em[1] != 1)
        return NTE_BAD_SIGNATURE;
    DWORD i = 2;
    while (i < k && em[i] == 0xFF)
        i++;
    if (i < 10 || i >= k || em[i] != 0)
        return NTE_BAD_SIGNATURE;
    i++;
    der_reader r = {em + i, k - i};
    der_item info, alg, oid, hash;
    if (DerExpect(&r, ASN_SEQUENCE, &info) || r.n)
        return NTE_BAD_SIGNATURE;
    DerOpen(&r, &info);
    if (DerExpect(&r, ASN_SEQUENCE, &alg) || DerExpect(&r, ASN_OCTETSTRING, &hash) || r.n)
        return NTE_BAD_SIGNATURE;
    DerOpen(&r, &alg);
    char text[64];
    if (DerExpect(&r, ASN_OID, &oid) || DerParseOid(&oid, text, sizeof(text)) || strcmp(text, hash_oid))
        return NTE_BAD_SIGNATURE;
    if (r.n) {
        der_item null;
        if (DerExpect(&r, ASN_NULL, &null) || null.length || r.n)
            return NTE_BAD_SIGNATURE;
    }
    DWORD size = CrHashSize(hash_alg);
    if (hash.length != size || memcmp(hash.content, digest, size))
        return NTE_BAD_SIGNATURE;
    return 0;
}

/* ------------------------------------------------------------------ ecdsa and rsa pss */

static const MimeHashAlg *mime_hash_of(ALG_ID alg)
{
    switch (alg) {
    case CALG_SHA1:
        return &mime_sha1;
    case CALG_SHA_256:
        return &mime_sha256;
    case CALG_SHA_384:
        return &mime_sha384;
    case CALG_SHA_512:
        return &mime_sha512;
    }
    return nullptr;
}

/* a DER INTEGER into a big endian field of size bytes. */
static bool fixed_integer(der_reader *r, BYTE *out, DWORD size)
{
    der_item item;
    if (DerExpect(r, ASN_INTEGER, &item))
        return false;
    const BYTE *p = item.content;
    DWORD n = item.length;
    while (n && !*p) {
        p++;
        n--;
    }
    if (n > size)
        return false;
    memset(out, 0, size - n);
    memcpy(out + size - n, p, n);
    return true;
}

/* RFC 5480 keys and RFC 5758 signatures: the Ecdsa-Sig-Value of r and s. */
static DWORD verify_ecdsa(const CERT_PUBLIC_KEY_INFO *key, ALG_ID hash, const BYTE *data, DWORD data_size,
                          const BYTE *sig, DWORD sig_size)
{
    static const BYTE p256[] = {0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07};
    static const BYTE p384[] = {0x06, 0x05, 0x2B, 0x81, 0x04, 0x00, 0x22};
    static const BYTE p521[] = {0x06, 0x05, 0x2B, 0x81, 0x04, 0x00, 0x23};
    if (!key->Algorithm.pszObjId || strcmp(key->Algorithm.pszObjId, szOID_ECC_PUBLIC_KEY))
        return NTE_BAD_ALGID;
    const CRYPT_OBJID_BLOB *params = &key->Algorithm.Parameters;
    MimeEcKey ec = {};
    if (params->cbData == sizeof(p256) && !memcmp(params->pbData, p256, sizeof(p256)))
        ec.curve = &mime_p256;
    else if (params->cbData == sizeof(p384) && !memcmp(params->pbData, p384, sizeof(p384)))
        ec.curve = &mime_p384;
    else if (params->cbData == sizeof(p521) && !memcmp(params->pbData, p521, sizeof(p521)))
        ec.curve = &mime_p521;
    else
        return NTE_BAD_ALGID;
    DWORD n = mime_ec_size(ec.curve);
    const CRYPT_BIT_BLOB *point = &key->PublicKey;
    if (point->cbData != 1 + 2 * n || point->pbData[0] != 4)
        return NTE_BAD_DATA;
    memcpy(ec.x, point->pbData + 1, n);
    memcpy(ec.y, point->pbData + 1 + n, n);
    der_reader r = {sig, sig_size}, rs;
    der_item seq;
    BYTE raw[132], digest[CR_MAX_HASH];
    if (DerExpect(&r, ASN_SEQUENCE, &seq) || r.n)
        return NTE_BAD_SIGNATURE;
    DerOpen(&rs, &seq);
    if (!fixed_integer(&rs, raw, n) || !fixed_integer(&rs, raw + n, n) || rs.n)
        return NTE_BAD_SIGNATURE;
    CrHash(hash, data, data_size, digest);
    return mime_ecdsa_verify(&ec, digest, CrHashSize(hash), raw, 2 * n) ? NTE_BAD_SIGNATURE : 0;
}

static ALG_ID hash_algorithm(const der_item *identifier)
{
    der_reader r;
    der_item oid;
    char text[64];
    DerOpen(&r, identifier);
    if (DerExpect(&r, ASN_OID, &oid) || DerParseOid(&oid, text, sizeof(text)))
        return 0;
    if (!strcmp(text, szOID_OIWSEC_sha1))
        return CALG_SHA1;
    if (!strcmp(text, szOID_NIST_sha256))
        return CALG_SHA_256;
    if (!strcmp(text, szOID_NIST_sha384))
        return CALG_SHA_384;
    if (!strcmp(text, szOID_NIST_sha512))
        return CALG_SHA_512;
    return 0;
}

/* RFC 4055 RSASSA-PSS-params: hash, MGF1 over the same hash, salt length and trailer 1. defaults are SHA-1 and 20. */
static DWORD pss_params(der_reader *a, ALG_ID *hash, DWORD *salt)
{
    *hash = CALG_SHA1;
    *salt = 20;
    der_item seq, field, inner;
    if (DerExpect(a, ASN_SEQUENCE, &seq))
        return CRYPT_E_ASN1_CORRUPT;
    der_reader r;
    DerOpen(&r, &seq);
    ALG_ID mgf_hash = CALG_SHA1;
    if (DerPeek(&r, ASN_CONSTRUCTED_CONTEXT(0))) {
        der_reader f;
        if (DerExpect(&r, ASN_CONSTRUCTED_CONTEXT(0), &field))
            return CRYPT_E_ASN1_CORRUPT;
        DerOpen(&f, &field);
        if (DerExpect(&f, ASN_SEQUENCE, &inner) || !(*hash = hash_algorithm(&inner)))
            return NTE_BAD_ALGID;
    }
    if (DerPeek(&r, ASN_CONSTRUCTED_CONTEXT(1))) {
        der_reader f, m;
        der_item oid, params;
        char text[64];
        if (DerExpect(&r, ASN_CONSTRUCTED_CONTEXT(1), &field))
            return CRYPT_E_ASN1_CORRUPT;
        DerOpen(&f, &field);
        if (DerExpect(&f, ASN_SEQUENCE, &inner))
            return CRYPT_E_ASN1_CORRUPT;
        DerOpen(&m, &inner);
        if (DerExpect(&m, ASN_OID, &oid) || DerParseOid(&oid, text, sizeof(text)) ||
            strcmp(text, "1.2.840.113549.1.1.8") || DerExpect(&m, ASN_SEQUENCE, &params) ||
            !(mgf_hash = hash_algorithm(&params)))
            return NTE_BAD_ALGID;
    }
    if (DerPeek(&r, ASN_CONSTRUCTED_CONTEXT(2))) {
        der_reader f;
        int value;
        if (DerExpect(&r, ASN_CONSTRUCTED_CONTEXT(2), &field))
            return CRYPT_E_ASN1_CORRUPT;
        DerOpen(&f, &field);
        if (DerExpect(&f, ASN_INTEGER, &inner) || DerParseInt(&inner, &value) || value < 0)
            return CRYPT_E_ASN1_CORRUPT;
        *salt = (DWORD)value;
    }
    if (DerPeek(&r, ASN_CONSTRUCTED_CONTEXT(3))) {
        der_reader f;
        int value;
        if (DerExpect(&r, ASN_CONSTRUCTED_CONTEXT(3), &field))
            return CRYPT_E_ASN1_CORRUPT;
        DerOpen(&f, &field);
        if (DerExpect(&f, ASN_INTEGER, &inner) || DerParseInt(&inner, &value) || value != 1)
            return NTE_BAD_ALGID;
    }
    /* the crypto library masks with the message hash. */
    return mgf_hash == *hash ? 0 : NTE_BAD_ALGID;
}

static DWORD verify_pss(const CERT_PUBLIC_KEY_INFO *key, der_reader *a, const BYTE *data, DWORD data_size,
                        const BYTE *sig, DWORD sig_size)
{
    ALG_ID hash;
    DWORD salt;
    DWORD error = pss_params(a, &hash, &salt);
    if (error)
        return error;
    rsa_key parsed;
    if ((error = parse_rsa_key(key, &parsed)))
        return error;
    MimeRsa *rsa = mime_rsa_new();
    if (!rsa)
        return ERROR_OUTOFMEMORY;
    BYTE digest[CR_MAX_HASH];
    error = NTE_BAD_SIGNATURE;
    if (mime_bn_from_bytes(&rsa->n, parsed.modulus, parsed.modulus_size) &&
        mime_bn_from_bytes(&rsa->e, parsed.exponent, parsed.exponent_size) && !mime_rsa_check(rsa) &&
        CrHash(hash, data, data_size, digest) &&
        !mime_rsa_pss_verify(rsa, mime_hash_of(hash), digest, CrHashSize(hash), salt, sig, sig_size))
        error = 0;
    mime_rsa_free(rsa);
    return error;
}

DWORD CrVerifySignedBlob(const BYTE *encoded, DWORD size, const CERT_PUBLIC_KEY_INFO *key)
{
    der_reader r = {encoded, size};
    der_item outer, tbs, alg, oid, sig;
    if (DerExpect(&r, ASN_SEQUENCE, &outer))
        return CRYPT_E_ASN1_BADTAG;
    DerOpen(&r, &outer);
    if (DerRead(&r, &tbs) || DerExpect(&r, ASN_SEQUENCE, &alg) || DerExpect(&r, ASN_BITSTRING, &sig) ||
        !sig.length || sig.content[0])
        return CRYPT_E_ASN1_CORRUPT;
    der_reader a;
    DerOpen(&a, &alg);
    char text[128];
    if (DerExpect(&a, ASN_OID, &oid) || DerParseOid(&oid, text, sizeof(text)))
        return CRYPT_E_ASN1_CORRUPT;
    static const struct {
        LPCSTR oid;
        ALG_ID hash;
    } ecdsa_algs[] = {{szOID_ECDSA_SHA1, CALG_SHA1},
                      {szOID_ECDSA_SHA256, CALG_SHA_256},
                      {szOID_ECDSA_SHA384, CALG_SHA_384},
                      {szOID_ECDSA_SHA512, CALG_SHA_512}};
    for (DWORD i = 0; i < RTL_NUMBER_OF(ecdsa_algs); i++)
        if (!strcmp(ecdsa_algs[i].oid, text))
            return verify_ecdsa(key, ecdsa_algs[i].hash, tbs.start, tbs.total, sig.content + 1, sig.length - 1);
    if (!strcmp(text, szOID_RSA_SSA_PSS))
        return verify_pss(key, &a, tbs.start, tbs.total, sig.content + 1, sig.length - 1);
    DWORD index = RTL_NUMBER_OF(sign_algs);
    for (DWORD i = 0; i < RTL_NUMBER_OF(sign_algs); i++)
        if (!strcmp(sign_algs[i].sign, text))
            index = i;
    if (index == RTL_NUMBER_OF(sign_algs))
        return NTE_BAD_ALGID;
    rsa_key rsa;
    DWORD error = parse_rsa_key(key, &rsa);
    if (error)
        return error;
    BYTE digest[CR_MAX_HASH];
    CrHash(sign_algs[index].hash, tbs.start, tbs.total, digest);
    MimeBn *buffers = CrAlloc(3 * sizeof(MimeBn));
    if (!buffers)
        return ERROR_OUTOFMEMORY;
    MimeBn *modulus = &buffers[0], *s = &buffers[1], *e = &buffers[2];
    BYTE *em = CrAlloc(rsa.modulus_size);
    error = NTE_BAD_SIGNATURE;
    if (em && sig.length - 1 <= rsa.modulus_size + 1 && mime_bn_from_bytes(modulus, rsa.modulus, rsa.modulus_size) &&
        mime_bn_from_bytes(s, sig.content + 1, sig.length - 1) &&
        mime_bn_from_bytes(e, rsa.exponent, rsa.exponent_size) && (modulus->n && (modulus->d[0] & 1)) &&
        mime_bn_cmp(s, modulus) < 0 && !mime_bn_mod_exp(s, s, e, modulus, false) &&
        mime_bn_to_bytes(s, em, rsa.modulus_size)) {
        error = check_padding(em, rsa.modulus_size, sign_algs[index].hash, sign_algs[index].hash_oid, digest);
    } else if (!em) {
        error = ERROR_OUTOFMEMORY;
    }
    CrFree(em);
    CrFree(buffers);
    return error;
}

/* ------------------------------------------------------------------ exports */

BOOL WINAPI CryptVerifyCertificateSignature(HCRYPTPROV_LEGACY hCryptProv, DWORD dwCertEncodingType,
                                            const BYTE *pbEncoded, DWORD cbEncoded, PCERT_PUBLIC_KEY_INFO pPublicKey)
{
    CRYPT_DATA_BLOB blob = {cbEncoded, (BYTE *)pbEncoded};
    return CryptVerifyCertificateSignatureEx(hCryptProv, dwCertEncodingType, CRYPT_VERIFY_CERT_SIGN_SUBJECT_BLOB,
                                             &blob, CRYPT_VERIFY_CERT_SIGN_ISSUER_PUBKEY, pPublicKey, 0, nullptr);
}

BOOL WINAPI CryptVerifyCertificateSignatureEx(HCRYPTPROV_LEGACY hCryptProv, DWORD dwCertEncodingType,
                                              DWORD dwSubjectType, void *pvSubject, DWORD dwIssuerType,
                                              void *pvIssuer, DWORD dwFlags, void *pvExtra)
{
    const BYTE *data;
    DWORD size;
    if (!pvSubject)
        return CrFail(E_INVALIDARG);
    switch (dwSubjectType) {
    case CRYPT_VERIFY_CERT_SIGN_SUBJECT_BLOB: {
        const CRYPT_DATA_BLOB *blob = pvSubject;
        data = blob->pbData;
        size = blob->cbData;
        break;
    }
    case CRYPT_VERIFY_CERT_SIGN_SUBJECT_CERT: {
        PCCERT_CONTEXT cert = pvSubject;
        data = cert->pbCertEncoded;
        size = cert->cbCertEncoded;
        break;
    }
    case CRYPT_VERIFY_CERT_SIGN_SUBJECT_CRL: {
        PCCRL_CONTEXT crl = pvSubject;
        data = crl->pbCrlEncoded;
        size = crl->cbCrlEncoded;
        break;
    }
    default:
        return CrFail(E_INVALIDARG);
    }
    if (!data || !size)
        return CrFail(CRYPT_E_ASN1_EOD);
    const CERT_PUBLIC_KEY_INFO *key;
    switch (dwIssuerType) {
    case CRYPT_VERIFY_CERT_SIGN_ISSUER_PUBKEY:
        key = pvIssuer;
        break;
    case CRYPT_VERIFY_CERT_SIGN_ISSUER_CERT:
        key = pvIssuer ? &((PCCERT_CONTEXT)pvIssuer)->pCertInfo->SubjectPublicKeyInfo : nullptr;
        break;
    case CRYPT_VERIFY_CERT_SIGN_ISSUER_CHAIN: {
        PCCERT_CHAIN_CONTEXT chain = pvIssuer;
        key = chain && chain->cChain && chain->rgpChain[0]->cElement
                  ? &chain->rgpChain[0]->rgpElement[0]->pCertContext->pCertInfo->SubjectPublicKeyInfo
                  : nullptr;
        break;
    }
    case CRYPT_VERIFY_CERT_SIGN_ISSUER_NULL: {
        /* unsigned content carries the null signature algorithm and no signature. */
        DWORD info_size;
        CERT_SIGNED_CONTENT_INFO *info = CrDecodeAlloc(X509_CERT, data, size, 0, &info_size);
        if (!info)
            return FALSE;
        BOOL ok = !strcmp(info->SignatureAlgorithm.pszObjId, "1.3.6.1.5.5.7.6.2") && !info->Signature.cbData;
        CrFree(info);
        return ok ? TRUE : CrFail(NTE_BAD_SIGNATURE);
    }
    default:
        return CrFail(E_INVALIDARG);
    }
    if (!key)
        return CrFail(E_INVALIDARG);
    DWORD error = CrVerifySignedBlob(data, size, key);
    return error ? CrFail(error) : TRUE;
}
