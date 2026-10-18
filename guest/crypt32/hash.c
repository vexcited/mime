/*
 * the hashes certificates use, md5, sha-1 and the sha-2 family, from the
 * shared crypto library, and the certificate hashing exports.
 */
#include "crypt32_private.h"
#include <crypto/crypto.h>

static const MimeHashAlg *crypto_hash(ALG_ID alg)
{
    switch (alg) {
    case CALG_MD5:
        return &mime_md5;
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

DWORD CrHashSize(ALG_ID alg)
{
    const MimeHashAlg *hash = crypto_hash(alg);
    return hash ? hash->digest_size : 0;
}

BOOL CrHash(ALG_ID alg, const BYTE *data, DWORD size, BYTE *out)
{
    const MimeHashAlg *hash = crypto_hash(alg);
    if (!hash)
        return FALSE;
    mime_hash(hash, data, size, out);
    return TRUE;
}

ALG_ID CrHashFromOid(LPCSTR oid)
{
    static const struct {
        LPCSTR oid;
        ALG_ID alg;
    } map[] = {
        {szOID_RSA_MD5, CALG_MD5},       {szOID_OIWSEC_sha1, CALG_SHA1},      {szOID_NIST_sha256, CALG_SHA_256},
        {szOID_NIST_sha384, CALG_SHA_384}, {szOID_NIST_sha512, CALG_SHA_512},
    };
    for (DWORD i = 0; i < RTL_NUMBER_OF(map); i++)
        if (!strcmp(map[i].oid, oid))
            return map[i].alg;
    return 0;
}

/* ------------------------------------------------------------------ exports */

BOOL WINAPI CryptHashCertificate(HCRYPTPROV_LEGACY hCryptProv, ALG_ID Algid, DWORD dwFlags, const BYTE *pbEncoded,
                                 DWORD cbEncoded, BYTE *pbComputedHash, DWORD *pcbComputedHash)
{
    if (!pcbComputedHash)
        return CrFail(ERROR_INVALID_PARAMETER);
    if (!Algid)
        Algid = CALG_SHA1;
    DWORD n = CrHashSize(Algid);
    if (!n)
        return CrFail(NTE_BAD_ALGID);
    if (!pbComputedHash) {
        *pcbComputedHash = n;
        return TRUE;
    }
    if (*pcbComputedHash < n) {
        *pcbComputedHash = n;
        return CrFail(ERROR_MORE_DATA);
    }
    if (!pbEncoded && cbEncoded)
        return CrFail(ERROR_INVALID_PARAMETER);
    CrHash(Algid, pbEncoded, cbEncoded, pbComputedHash);
    *pcbComputedHash = n;
    return TRUE;
}

BOOL WINAPI CryptHashCertificate2(LPCWSTR pwszCNGHashAlgid, DWORD dwFlags, void *pvReserved, const BYTE *pbEncoded,
                                  DWORD cbEncoded, BYTE *pbComputedHash, DWORD *pcbComputedHash)
{
    static const struct {
        LPCWSTR name;
        ALG_ID alg;
    } map[] = {{L"MD5", CALG_MD5},         {L"SHA1", CALG_SHA1},         {L"SHA256", CALG_SHA_256},
               {L"SHA384", CALG_SHA_384}, {L"SHA512", CALG_SHA_512}};
    if (!pwszCNGHashAlgid)
        return CrFail(ERROR_INVALID_PARAMETER);
    for (DWORD i = 0; i < RTL_NUMBER_OF(map); i++)
        if (!_wcsicmp(map[i].name, pwszCNGHashAlgid))
            return CryptHashCertificate(0, map[i].alg, 0, pbEncoded, cbEncoded, pbComputedHash, pcbComputedHash);
    return CrFail(NTE_BAD_ALGID);
}

BOOL WINAPI CryptHashPublicKeyInfo(HCRYPTPROV_LEGACY hCryptProv, ALG_ID Algid, DWORD dwFlags,
                                   DWORD dwCertEncodingType, PCERT_PUBLIC_KEY_INFO pInfo, BYTE *pbComputedHash,
                                   DWORD *pcbComputedHash)
{
    if (!pInfo)
        return CrFail(ERROR_INVALID_PARAMETER);
    DWORD size;
    BYTE *encoded = CrEncodeAlloc(X509_PUBLIC_KEY_INFO, pInfo, 0, &size);
    if (!encoded)
        return FALSE;
    BOOL ok = CryptHashCertificate(hCryptProv, Algid ? Algid : CALG_MD5, dwFlags, encoded, size, pbComputedHash,
                                   pcbComputedHash);
    CrFree(encoded);
    return ok;
}

BOOL WINAPI CryptHashToBeSigned(HCRYPTPROV_LEGACY hCryptProv, DWORD dwCertEncodingType, const BYTE *pbEncoded,
                                DWORD cbEncoded, BYTE *pbComputedHash, DWORD *pcbComputedHash)
{
    DWORD size;
    CERT_SIGNED_CONTENT_INFO *info = CrDecodeAlloc(X509_CERT, pbEncoded, cbEncoded, 0, &size);
    if (!info)
        return FALSE;
    BOOL ok = CryptHashCertificate(hCryptProv, CALG_MD5, 0, info->ToBeSigned.pbData, info->ToBeSigned.cbData,
                                   pbComputedHash, pcbComputedHash);
    CrFree(info);
    return ok;
}
