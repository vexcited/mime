/*
 * rsa signature verification for certificates: a small big number library
 * with montgomery multiplication and the pkcs #1 v1.5 padding check.
 */
#include "crypt32_private.h"

#define MAX_LIMBS 520

typedef struct {
    DWORD n;
    DWORD d[MAX_LIMBS + 2];
} big;

static void big_from_be(big *b, const BYTE *p, DWORD size)
{
    while (size && !*p) {
        p++;
        size--;
    }
    memset(b, 0, sizeof(*b));
    b->n = (size + 3) / 4;
    for (DWORD i = 0; i < size; i++)
        b->d[i / 4] |= (DWORD)p[size - 1 - i] << (8 * (i % 4));
}

static int big_cmp(const DWORD *a, const DWORD *b, DWORD n)
{
    for (DWORD i = n; i; i--)
        if (a[i - 1] != b[i - 1])
            return a[i - 1] < b[i - 1] ? -1 : 1;
    return 0;
}

static void big_sub(DWORD *a, const DWORD *b, DWORD n)
{
    ULONG64 borrow = 0;
    for (DWORD i = 0; i < n; i++) {
        ULONG64 v = (ULONG64)a[i] - b[i] - borrow;
        a[i] = (DWORD)v;
        borrow = (v >> 32) & 1;
    }
}

/* t = a * b * r^-1 mod m, all k limbs; t has room for k + 2 limbs. */
static void mont_mul(DWORD *out, const DWORD *a, const DWORD *b, const DWORD *m, DWORD k, DWORD m0inv, DWORD *t)
{
    memset(t, 0, (k + 2) * sizeof(DWORD));
    for (DWORD i = 0; i < k; i++) {
        ULONG64 c = 0;
        for (DWORD j = 0; j < k; j++) {
            ULONG64 v = (ULONG64)a[j] * b[i] + t[j] + c;
            t[j] = (DWORD)v;
            c = v >> 32;
        }
        ULONG64 v = (ULONG64)t[k] + c;
        t[k] = (DWORD)v;
        t[k + 1] = (DWORD)(v >> 32);
        DWORD q = t[0] * m0inv;
        v = (ULONG64)q * m[0] + t[0];
        c = v >> 32;
        for (DWORD j = 1; j < k; j++) {
            v = (ULONG64)q * m[j] + t[j] + c;
            t[j - 1] = (DWORD)v;
            c = v >> 32;
        }
        v = (ULONG64)t[k] + c;
        t[k - 1] = (DWORD)v;
        t[k] = t[k + 1] + (DWORD)(v >> 32);
    }
    if (t[k] || big_cmp(t, m, k) >= 0)
        big_sub(t, m, k);
    memcpy(out, t, k * sizeof(DWORD));
}

/* computes base^exp mod m into out (k limbs); m must be odd. */
static BOOL mod_exp(DWORD *out, const big *base, const BYTE *exp, DWORD exp_size, const big *m)
{
    DWORD k = m->n;
    if (!k || !(m->d[0] & 1) || base->n > k || (base->n == k && big_cmp(base->d, m->d, k) >= 0))
        return FALSE;
    DWORD *buffer = CrAllocZero((6 * (k + 2)) * sizeof(DWORD));
    if (!buffer)
        return FALSE;
    DWORD *r2 = buffer, *x = r2 + k + 2, *acc = x + k + 2, *t = acc + k + 2, *one = t + k + 2, *tmp = one + k + 2;
    /* -m^-1 mod 2^32 by newton iteration. */
    DWORD inv = 1;
    for (int i = 0; i < 5; i++)
        inv *= 2 - m->d[0] * inv;
    DWORD m0inv = (DWORD)-inv;
    /* r^2 mod m by doubling 1 2 * 32 * k times. */
    memset(r2, 0, (k + 1) * sizeof(DWORD));
    r2[0] = 1;
    for (DWORD i = 0; i < 64 * k; i++) {
        DWORD carry = 0;
        for (DWORD j = 0; j < k; j++) {
            DWORD next = r2[j] >> 31;
            r2[j] = (r2[j] << 1) | carry;
            carry = next;
        }
        if (carry || big_cmp(r2, m->d, k) >= 0)
            big_sub(r2, m->d, k);
    }
    memset(one, 0, k * sizeof(DWORD));
    one[0] = 1;
    memset(tmp, 0, k * sizeof(DWORD));
    memcpy(tmp, base->d, base->n * sizeof(DWORD));
    mont_mul(x, tmp, r2, m->d, k, m0inv, t);
    mont_mul(acc, one, r2, m->d, k, m0inv, t);
    BOOL started = FALSE;
    for (DWORD i = 0; i < exp_size; i++)
        for (int bit = 7; bit >= 0; bit--) {
            if (started)
                mont_mul(acc, acc, acc, m->d, k, m0inv, t);
            if (exp[i] & (1 << bit)) {
                mont_mul(acc, acc, x, m->d, k, m0inv, t);
                started = TRUE;
            }
        }
    mont_mul(out, acc, one, m->d, k, m0inv, t);
    CrFree(buffer);
    return TRUE;
}

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
    big *buffers = CrAlloc(2 * sizeof(big));
    if (!buffers)
        return ERROR_OUTOFMEMORY;
    big *modulus = &buffers[0], *s = &buffers[1];
    big_from_be(modulus, rsa.modulus, rsa.modulus_size);
    big_from_be(s, sig.content + 1, sig.length - 1);
    DWORD k = modulus->n;
    DWORD *m = CrAlloc(k * sizeof(DWORD));
    BYTE *em = CrAlloc(rsa.modulus_size);
    error = NTE_BAD_SIGNATURE;
    if (m && em && sig.length - 1 <= rsa.modulus_size + 1 &&
        mod_exp(m, s, rsa.exponent, rsa.exponent_size, modulus)) {
        for (DWORD i = 0; i < rsa.modulus_size; i++)
            em[rsa.modulus_size - 1 - i] = (BYTE)(m[i / 4] >> (8 * (i % 4)));
        error = check_padding(em, rsa.modulus_size, sign_algs[index].hash, sign_algs[index].hash_oid, digest);
    } else if (!m || !em) {
        error = ERROR_OUTOFMEMORY;
    }
    CrFree(m);
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
