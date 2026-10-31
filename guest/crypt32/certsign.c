/*
 * signing certificates: the public key of a provider's key as a certificate has it, the signature of the encoded
 * certificate, and self signed certificates. a key is a legacy provider's, which advapi32 holds, or a key of ncrypt;
 * the handle says which. legacy signatures are little endian as CryptSignHash makes them and the encoder of a signed
 * certificate turns them over, ncrypt's rsa signatures are made the same way, and an ecdsa signature is the der
 * encoding of its two numbers that the encoder leaves as it is.
 */
#include "crypt32_private.h"

#define PUBLICKEYBLOB_ 6
#define NCRYPT_PAD_PKCS1_FLAG 2
#ifndef NTE_BAD_KEY
#define NTE_BAD_KEY ((DWORD)0x80090003L)
#endif
#ifndef NTE_FAIL
#define NTE_FAIL ((DWORD)0x80090020L)
#endif

typedef BOOL(WINAPI *GET_USER_KEY)(HCRYPTPROV, DWORD, HCRYPTKEY *);
typedef BOOL(WINAPI *EXPORT_KEY)(HCRYPTKEY, HCRYPTKEY, DWORD, DWORD, BYTE *, DWORD *);
typedef BOOL(WINAPI *DESTROY_KEY)(HCRYPTKEY);
typedef BOOL(WINAPI *CREATE_HASH)(HCRYPTPROV, ALG_ID, HCRYPTKEY, DWORD, HCRYPTHASH *);
typedef BOOL(WINAPI *HASH_DATA)(HCRYPTHASH, const BYTE *, DWORD, DWORD);
typedef BOOL(WINAPI *SIGN_HASH)(HCRYPTHASH, DWORD, LPCWSTR, DWORD, BYTE *, DWORD *);
typedef BOOL(WINAPI *DESTROY_HASH)(HCRYPTHASH);
typedef BOOL(WINAPI *IS_KEY_HANDLE)(ULONG_PTR);
typedef LONG(WINAPI *NCRYPT_EXPORT)(ULONG_PTR, ULONG_PTR, LPCWSTR, void *, BYTE *, DWORD, DWORD *, DWORD);
typedef LONG(WINAPI *NCRYPT_SIGN)(ULONG_PTR, void *, BYTE *, DWORD, BYTE *, DWORD, DWORD *, DWORD);
typedef BOOLEAN(WINAPI *GEN_RANDOM)(PVOID, ULONG);

static struct {
    GET_USER_KEY get_user_key;
    EXPORT_KEY export_key;
    DESTROY_KEY destroy_key;
    CREATE_HASH create_hash;
    HASH_DATA hash_data;
    SIGN_HASH sign_hash;
    DESTROY_HASH destroy_hash;
    IS_KEY_HANDLE is_key;
    NCRYPT_EXPORT ncrypt_export;
    NCRYPT_SIGN ncrypt_sign;
    GEN_RANDOM random;
} g_api;

static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

static BOOL WINAPI load(INIT_ONCE *once, void *param, void **context)
{
    HMODULE advapi32 = LoadLibraryW(L"advapi32.dll");
    if (advapi32) {
        g_api.get_user_key = (GET_USER_KEY)(void *)GetProcAddress(advapi32, "CryptGetUserKey");
        g_api.export_key = (EXPORT_KEY)(void *)GetProcAddress(advapi32, "CryptExportKey");
        g_api.destroy_key = (DESTROY_KEY)(void *)GetProcAddress(advapi32, "CryptDestroyKey");
        g_api.create_hash = (CREATE_HASH)(void *)GetProcAddress(advapi32, "CryptCreateHash");
        g_api.hash_data = (HASH_DATA)(void *)GetProcAddress(advapi32, "CryptHashData");
        g_api.sign_hash = (SIGN_HASH)(void *)GetProcAddress(advapi32, "CryptSignHashW");
        g_api.destroy_hash = (DESTROY_HASH)(void *)GetProcAddress(advapi32, "CryptDestroyHash");
        g_api.random = (GEN_RANDOM)(void *)GetProcAddress(advapi32, "SystemFunction036");
    }
    HMODULE ncrypt = LoadLibraryW(L"ncrypt.dll");
    if (ncrypt) {
        g_api.is_key = (IS_KEY_HANDLE)(void *)GetProcAddress(ncrypt, "NCryptIsKeyHandle");
        g_api.ncrypt_export = (NCRYPT_EXPORT)(void *)GetProcAddress(ncrypt, "NCryptExportKey");
        g_api.ncrypt_sign = (NCRYPT_SIGN)(void *)GetProcAddress(ncrypt, "NCryptSignHash");
    }
    return TRUE;
}

static void init(void)
{
    InitOnceExecuteOnce(&g_once, load, nullptr, nullptr);
}

static BOOL is_cng(ULONG_PTR handle)
{
    return g_api.is_key && g_api.is_key(handle);
}

/* ------------------------------------------------------------------ the public key */

static const BYTE null_parameters[2] = {ASN_NULL, 0};

static void reverse(BYTE *out, const BYTE *in, DWORD size)
{
    for (DWORD i = 0; i < size; i++)
        out[i] = in[size - 1 - i];
}

/* the rsa public key of a cng key, as the der of its modulus and exponent. */
static BYTE *cng_rsa_public(ULONG_PTR key, DWORD *size, LONG *error)
{
    DWORD need = 0;
    *error = g_api.ncrypt_export ? g_api.ncrypt_export(key, 0, L"RSAPUBLICBLOB", nullptr, nullptr, 0, &need, 0)
                                 : NTE_NOT_SUPPORTED;
    if (*error)
        return nullptr;
    BYTE *blob = CrAlloc(need);
    if (!blob) {
        *error = NTE_NO_MEMORY;
        return nullptr;
    }
    *error = g_api.ncrypt_export(key, 0, L"RSAPUBLICBLOB", nullptr, blob, need, &need, 0);
    if (*error || need < 24) {
        CrFree(blob);
        if (!*error)
            *error = NTE_BAD_KEY;
        return nullptr;
    }
    /* magic, bit length, the sizes of the exponent and the modulus, then both big endian. */
    DWORD exponent_size = ((DWORD *)blob)[2], modulus_size = ((DWORD *)blob)[3];
    if (24 + exponent_size + modulus_size > need) {
        CrFree(blob);
        *error = NTE_BAD_KEY;
        return nullptr;
    }
    BYTE *modulus = CrAlloc(modulus_size ? modulus_size : 1), *exponent = CrAlloc(exponent_size ? exponent_size : 1);
    der_writer w;
    DerWriteInit(&w);
    if (modulus && exponent) {
        reverse(modulus, blob + 24 + exponent_size, modulus_size);
        reverse(exponent, blob + 24, exponent_size);
        DWORD mark = DerBegin(&w, ASN_SEQUENCE);
        DerWriteIntLe(&w, modulus, modulus_size, TRUE);
        DerWriteIntLe(&w, exponent, exponent_size, TRUE);
        DerEnd(&w, mark);
    }
    CrFree(modulus);
    CrFree(exponent);
    CrFree(blob);
    if (w.error || !w.data) {
        DerWriteFree(&w);
        *error = NTE_NO_MEMORY;
        return nullptr;
    }
    BYTE *out = w.data;
    *size = w.size;
    return out;
}

/* the point of an ecdsa or ecdh public key, with the object identifier of its curve. */
static BYTE *cng_ec_public(ULONG_PTR key, DWORD *size, LPCSTR *curve, LONG *error)
{
    DWORD need = 0;
    *error = g_api.ncrypt_export ? g_api.ncrypt_export(key, 0, L"ECCPUBLICBLOB", nullptr, nullptr, 0, &need, 0)
                                 : NTE_NOT_SUPPORTED;
    if (*error)
        return nullptr;
    BYTE *blob = CrAlloc(need);
    if (!blob) {
        *error = NTE_NO_MEMORY;
        return nullptr;
    }
    *error = g_api.ncrypt_export(key, 0, L"ECCPUBLICBLOB", nullptr, blob, need, &need, 0);
    DWORD part = need >= 8 ? ((DWORD *)blob)[1] : 0;
    if (*error || !part || 8 + 2 * part > need) {
        CrFree(blob);
        if (!*error)
            *error = NTE_BAD_KEY;
        return nullptr;
    }
    *curve = part == 32 ? "1.2.840.10045.3.1.7" : part == 48 ? "1.3.132.0.34" : part == 66 ? "1.3.132.0.35" : nullptr;
    BYTE *point = CrAlloc(1 + 2 * part);
    if (!point || !*curve) {
        CrFree(blob);
        CrFree(point);
        *error = *curve ? NTE_NO_MEMORY : NTE_NOT_SUPPORTED;
        return nullptr;
    }
    point[0] = 4;
    memcpy(point + 1, blob + 8, 2 * part);
    CrFree(blob);
    *size = 1 + 2 * part;
    return point;
}

/* the der of the public key of a legacy provider's key. */
static BYTE *capi_public(HCRYPTPROV prov, DWORD spec, DWORD *size)
{
    HCRYPTKEY key = 0;
    if (!g_api.get_user_key || !g_api.export_key)
        return CrFail(NTE_PROVIDER_DLL_FAIL), nullptr;
    if (!g_api.get_user_key(prov, spec, &key))
        return nullptr;
    DWORD length = 0;
    BYTE *blob = nullptr, *der = nullptr;
    if (g_api.export_key(key, 0, PUBLICKEYBLOB_, 0, nullptr, &length) && (blob = CrAlloc(length)) &&
        g_api.export_key(key, 0, PUBLICKEYBLOB_, 0, blob, &length))
        der = CrEncodeAlloc(RSA_CSP_PUBLICKEYBLOB, blob, 0, size);
    DWORD error = GetLastError();
    CrFree(blob);
    if (g_api.destroy_key)
        g_api.destroy_key(key);
    if (!der)
        SetLastError(error);
    return der;
}

BOOL WINAPI CryptExportPublicKeyInfoEx(ULONG_PTR hCryptProvOrNCryptKey, DWORD dwKeySpec, DWORD dwCertEncodingType,
                                       LPSTR pszPublicKeyObjId, DWORD dwFlags, void *pvAuxInfo,
                                       PCERT_PUBLIC_KEY_INFO pInfo, DWORD *pcbInfo)
{
    if (!pcbInfo)
        return CrFail(ERROR_INVALID_PARAMETER);
    if (!(dwCertEncodingType & (X509_ASN_ENCODING | PKCS_7_ASN_ENCODING)))
        return CrFail(ERROR_FILE_NOT_FOUND);
    if (!hCryptProvOrNCryptKey)
        return CrFail(ERROR_INVALID_PARAMETER);
    init();
    BYTE *key = nullptr;
    DWORD key_size = 0;
    LPCSTR oid = pszPublicKeyObjId ? pszPublicKeyObjId : szOID_RSA_RSA;
    der_writer parameters;
    DerWriteInit(&parameters);
    const BYTE *parameter_bytes = null_parameters;
    DWORD parameter_size = sizeof(null_parameters);
    if (is_cng(hCryptProvOrNCryptKey)) {
        LONG error;
        key = cng_rsa_public(hCryptProvOrNCryptKey, &key_size, &error);
        if (!key) {
            LPCSTR curve = nullptr;
            key = cng_ec_public(hCryptProvOrNCryptKey, &key_size, &curve, &error);
            if (key) {
                oid = pszPublicKeyObjId ? pszPublicKeyObjId : szOID_ECC_PUBLIC_KEY;
                DerWriteOid(&parameters, curve);
                parameter_bytes = parameters.data;
                parameter_size = parameters.size;
            }
        }
        if (!key) {
            DerWriteFree(&parameters);
            return CrFail((DWORD)error);
        }
    } else {
        key = capi_public(hCryptProvOrNCryptKey, dwKeySpec ? dwKeySpec : AT_KEYEXCHANGE, &key_size);
        if (!key && !dwKeySpec)
            key = capi_public(hCryptProvOrNCryptKey, AT_SIGNATURE, &key_size);
        if (!key) {
            DerWriteFree(&parameters);
            return FALSE;
        }
    }
    DWORD oid_size = (DWORD)strlen(oid) + 1;
    DWORD need = (sizeof(CERT_PUBLIC_KEY_INFO) + 7) & ~7u;
    DWORD oid_at = need, parameters_at = (need += (oid_size + 7) & ~7u);
    DWORD key_at = (need += (parameter_size + 7) & ~7u);
    need += key_size;
    BOOL ok = TRUE;
    if (pInfo && *pcbInfo >= need) {
        BYTE *base = (BYTE *)pInfo;
        memset(pInfo, 0, sizeof(*pInfo));
        memcpy(base + oid_at, oid, oid_size);
        memcpy(base + parameters_at, parameter_bytes, parameter_size);
        memcpy(base + key_at, key, key_size);
        pInfo->Algorithm.pszObjId = (LPSTR)(base + oid_at);
        pInfo->Algorithm.Parameters.cbData = parameter_size;
        pInfo->Algorithm.Parameters.pbData = base + parameters_at;
        pInfo->PublicKey.cbData = key_size;
        pInfo->PublicKey.pbData = base + key_at;
        pInfo->PublicKey.cUnusedBits = 0;
    } else if (pInfo) {
        ok = CrFail(ERROR_MORE_DATA);
    }
    *pcbInfo = need;
    CrFree(key);
    DerWriteFree(&parameters);
    return ok;
}

BOOL WINAPI CryptExportPublicKeyInfo(ULONG_PTR hCryptProvOrNCryptKey, DWORD dwKeySpec, DWORD dwCertEncodingType,
                                     PCERT_PUBLIC_KEY_INFO pInfo, DWORD *pcbInfo)
{
    return CryptExportPublicKeyInfoEx(hCryptProvOrNCryptKey, dwKeySpec, dwCertEncodingType, nullptr, 0, nullptr, pInfo,
                                      pcbInfo);
}

/* ------------------------------------------------------------------ signing */

typedef struct {
    LPCSTR oid;
    ALG_ID hash;
    LPCWSTR hash_name;
    BOOL ecdsa;
} SIGNATURE_ALGORITHM;

static const SIGNATURE_ALGORITHM *find_algorithm(LPCSTR oid)
{
    static const SIGNATURE_ALGORITHM table[] = {
        {"1.2.840.113549.1.1.4", CALG_MD5, L"MD5", FALSE},   {"1.2.840.113549.1.1.5", CALG_SHA1, L"SHA1", FALSE},
        {"1.3.14.3.2.29", CALG_SHA1, L"SHA1", FALSE},        {"1.2.840.113549.1.1.11", CALG_SHA_256, L"SHA256", FALSE},
        {"1.2.840.113549.1.1.12", CALG_SHA_384, L"SHA384", FALSE},
        {"1.2.840.113549.1.1.13", CALG_SHA_512, L"SHA512", FALSE},
        {"1.2.840.10045.4.1", CALG_SHA1, L"SHA1", TRUE},     {"1.2.840.10045.4.3.2", CALG_SHA_256, L"SHA256", TRUE},
        {"1.2.840.10045.4.3.3", CALG_SHA_384, L"SHA384", TRUE},
        {"1.2.840.10045.4.3.4", CALG_SHA_512, L"SHA512", TRUE},
    };
    for (DWORD i = 0; oid && i < RTL_NUMBER_OF(table); i++)
        if (!strcmp(table[i].oid, oid))
            return &table[i];
    return nullptr;
}

/* the der of an ecdsa signature, from the two numbers a key makes. */
static BYTE *ecdsa_der(const BYTE *rs, DWORD size, DWORD *out_size)
{
    der_writer w;
    DerWriteInit(&w);
    DWORD half = size / 2;
    BYTE *r = CrAlloc(half), *s = CrAlloc(half);
    if (r && s) {
        reverse(r, rs, half);
        reverse(s, rs + half, half);
        DWORD mark = DerBegin(&w, ASN_SEQUENCE);
        DerWriteIntLe(&w, r, half, TRUE);
        DerWriteIntLe(&w, s, half, TRUE);
        DerEnd(&w, mark);
    }
    CrFree(r);
    CrFree(s);
    if (w.error || !w.data) {
        DerWriteFree(&w);
        return nullptr;
    }
    *out_size = w.size;
    return w.data;
}

/* the signature of the bytes, a block the caller frees. */
static BYTE *make_signature(ULONG_PTR key, DWORD spec, const SIGNATURE_ALGORITHM *algorithm, const BYTE *data,
                            DWORD data_size, DWORD *size)
{
    init();
    if (is_cng(key)) {
        BYTE digest[64];
        if (!CrHash(algorithm->hash, data, data_size, digest)) {
            CrFail(NTE_BAD_ALGID);
            return nullptr;
        }
        DWORD digest_size = CrHashSize(algorithm->hash);
        struct {
            LPCWSTR algorithm;
        } padding = {algorithm->hash_name};
        DWORD need = 0;
        void *pad = algorithm->ecdsa ? nullptr : &padding;
        DWORD flags = algorithm->ecdsa ? 0 : NCRYPT_PAD_PKCS1_FLAG;
        LONG error = g_api.ncrypt_sign ? g_api.ncrypt_sign(key, pad, digest, digest_size, nullptr, 0, &need, flags)
                                       : NTE_NOT_SUPPORTED;
        BYTE *signature = error ? nullptr : CrAlloc(need);
        if (!error && signature)
            error = g_api.ncrypt_sign(key, pad, digest, digest_size, signature, need, &need, flags);
        if (error || !signature) {
            CrFree(signature);
            CrFail(error ? (DWORD)error : NTE_NO_MEMORY);
            return nullptr;
        }
        if (algorithm->ecdsa) {
            BYTE *der = ecdsa_der(signature, need, size);
            CrFree(signature);
            return der;
        }
        BYTE *flipped = CrAlloc(need);
        if (flipped)
            reverse(flipped, signature, need);
        CrFree(signature);
        *size = need;
        return flipped;
    }
    if (algorithm->ecdsa) {
        CrFail(NTE_BAD_ALGID);
        return nullptr;
    }
    HCRYPTHASH hash = 0;
    if (!g_api.create_hash || !g_api.hash_data || !g_api.sign_hash) {
        CrFail(NTE_PROVIDER_DLL_FAIL);
        return nullptr;
    }
    if (!g_api.create_hash(key, algorithm->hash, 0, 0, &hash))
        return nullptr;
    BYTE *signature = nullptr;
    DWORD need = 0;
    if (g_api.hash_data(hash, data, data_size, 0) && g_api.sign_hash(hash, spec, nullptr, 0, nullptr, &need) &&
        (signature = CrAlloc(need)) && !g_api.sign_hash(hash, spec, nullptr, 0, signature, &need)) {
        CrFree(signature);
        signature = nullptr;
    }
    DWORD error = GetLastError();
    if (g_api.destroy_hash)
        g_api.destroy_hash(hash);
    if (!signature) {
        SetLastError(error ? error : ERROR_OUTOFMEMORY);
        return nullptr;
    }
    *size = need;
    return signature;
}

BOOL WINAPI CryptSignCertificate(ULONG_PTR hCryptProvOrNCryptKey, DWORD dwKeySpec, DWORD dwCertEncodingType,
                                 const BYTE *pbEncodedToBeSigned, DWORD cbEncodedToBeSigned,
                                 PCRYPT_ALGORITHM_IDENTIFIER pSignatureAlgorithm, const void *pvHashAuxInfo,
                                 BYTE *pbSignature, DWORD *pcbSignature)
{
    if (!pcbSignature || !pSignatureAlgorithm || !pbEncodedToBeSigned)
        return CrFail(ERROR_INVALID_PARAMETER);
    if (!(dwCertEncodingType & (X509_ASN_ENCODING | PKCS_7_ASN_ENCODING)))
        return CrFail(ERROR_FILE_NOT_FOUND);
    const SIGNATURE_ALGORITHM *algorithm = find_algorithm(pSignatureAlgorithm->pszObjId);
    if (!algorithm)
        return CrFail(CRYPT_E_UNKNOWN_ALGO);
    if (!hCryptProvOrNCryptKey)
        return CrFail(ERROR_INVALID_PARAMETER);
    DWORD size = 0;
    BYTE *signature = make_signature(hCryptProvOrNCryptKey, dwKeySpec, algorithm, pbEncodedToBeSigned,
                                     cbEncodedToBeSigned, &size);
    if (!signature)
        return FALSE;
    BOOL ok = TRUE;
    if (pbSignature && *pcbSignature >= size)
        memcpy(pbSignature, signature, size);
    else if (pbSignature)
        ok = CrFail(ERROR_MORE_DATA);
    *pcbSignature = size;
    if (!pbSignature && algorithm->ecdsa)
        *pcbSignature += 6;
    CrFree(signature);
    return ok;
}

BOOL WINAPI CryptSignAndEncodeCertificate(ULONG_PTR hCryptProvOrNCryptKey, DWORD dwKeySpec, DWORD dwCertEncodingType,
                                          LPCSTR lpszStructType, const void *pvStructInfo,
                                          PCRYPT_ALGORITHM_IDENTIFIER pSignatureAlgorithm, const void *pvHashAuxInfo,
                                          BYTE *pbEncoded, DWORD *pcbEncoded)
{
    if (!pcbEncoded || !pSignatureAlgorithm || !pvStructInfo)
        return CrFail(ERROR_INVALID_PARAMETER);
    const SIGNATURE_ALGORITHM *algorithm = find_algorithm(pSignatureAlgorithm->pszObjId);
    if (!algorithm)
        return CrFail(CRYPT_E_UNKNOWN_ALGO);
    DWORD tbs_size = 0;
    BYTE *tbs = nullptr;
    if (!CryptEncodeObjectEx(dwCertEncodingType, lpszStructType, pvStructInfo, CRYPT_ENCODE_ALLOC_FLAG, nullptr, &tbs,
                             &tbs_size))
        return FALSE;
    DWORD size = 0;
    BYTE *signature = make_signature(hCryptProvOrNCryptKey, dwKeySpec, algorithm, tbs, tbs_size, &size);
    if (!signature) {
        DWORD error = GetLastError();
        LocalFree(tbs);
        return CrFail(error);
    }
    CERT_SIGNED_CONTENT_INFO info = {};
    info.ToBeSigned.cbData = tbs_size;
    info.ToBeSigned.pbData = tbs;
    info.SignatureAlgorithm = *pSignatureAlgorithm;
    /* an rsa signature algorithm has null parameters in a certificate. */
    if (!pSignatureAlgorithm->Parameters.cbData && !algorithm->ecdsa) {
        info.SignatureAlgorithm.Parameters.cbData = sizeof(null_parameters);
        info.SignatureAlgorithm.Parameters.pbData = (BYTE *)null_parameters;
    }
    info.Signature.cbData = size;
    info.Signature.pbData = signature;
    info.Signature.cUnusedBits = 0;
    DWORD encoded_size = 0;
    BYTE *encoded = CrEncodeAlloc(X509_CERT, &info, algorithm->ecdsa ? CRYPT_ENCODE_NO_SIGNATURE_BYTE_REVERSAL_FLAG : 0,
                                  &encoded_size);
    DWORD error = GetLastError();
    LocalFree(tbs);
    CrFree(signature);
    if (!encoded)
        return CrFail(error);
    BOOL ok = TRUE;
    if (pbEncoded && *pcbEncoded >= encoded_size)
        memcpy(pbEncoded, encoded, encoded_size);
    else if (pbEncoded)
        ok = CrFail(ERROR_MORE_DATA);
    *pcbEncoded = encoded_size;
    /* an ecdsa signature differs in length from one signing to the next, so the size asked for is a bound. */
    if (!pbEncoded && algorithm->ecdsa)
        *pcbEncoded += 6;
    CrFree(encoded);
    return ok;
}

/* ------------------------------------------------------------------ self signed certificates */

PCCERT_CONTEXT WINAPI CertCreateSelfSignCertificate(ULONG_PTR hCryptProvOrNCryptKey, PCERT_NAME_BLOB pSubjectIssuerBlob,
                                                    DWORD dwFlags, PCRYPT_KEY_PROV_INFO pKeyProvInfo,
                                                    PCRYPT_ALGORITHM_IDENTIFIER pSignatureAlgorithm,
                                                    const SYSTEMTIME *pStartTime, const SYSTEMTIME *pEndTime,
                                                    PCERT_EXTENSIONS pExtensions)
{
    if (!pSubjectIssuerBlob || !pSubjectIssuerBlob->pbData || !pSubjectIssuerBlob->cbData) {
        SetLastError(E_INVALIDARG);
        return nullptr;
    }
    init();
    FILETIME start, end;
    if (pStartTime) {
        if (!SystemTimeToFileTime(pStartTime, &start))
            return nullptr;
    } else {
        GetSystemTimeAsFileTime(&start);
    }
    if (pEndTime) {
        if (!SystemTimeToFileTime(pEndTime, &end))
            return nullptr;
    } else {
        ULONGLONG t = ((ULONGLONG)start.dwHighDateTime << 32 | start.dwLowDateTime) + 365ull * 24 * 3600 * 10000000ull;
        end.dwLowDateTime = (DWORD)t;
        end.dwHighDateTime = (DWORD)(t >> 32);
    }
    CERT_KEY_CONTEXT held = {};
    ULONG_PTR key = hCryptProvOrNCryptKey;
    DWORD spec = pKeyProvInfo && pKeyProvInfo->dwKeySpec != CERT_NCRYPT_KEY_SPEC ? pKeyProvInfo->dwKeySpec : 0;
    if (!key) {
        /* without a key the information says where it is. */
        if (!pKeyProvInfo) {
            SetLastError(E_INVALIDARG);
            return nullptr;
        }
        if (!CrAcquireKeyFromInfo(pKeyProvInfo, &held))
            return nullptr;
        key = held.dwKeySpec == CERT_NCRYPT_KEY_SPEC ? held.hNCryptKey : held.hCryptProv;
        if (held.dwKeySpec != CERT_NCRYPT_KEY_SPEC)
            spec = held.dwKeySpec;
    }
    static const CRYPT_ALGORITHM_IDENTIFIER default_algorithm = {(LPSTR)szOID_RSA_SHA1RSA, {0, nullptr}};
    CRYPT_ALGORITHM_IDENTIFIER algorithm = pSignatureAlgorithm ? *pSignatureAlgorithm : default_algorithm;
    const SIGNATURE_ALGORITHM *known = find_algorithm(algorithm.pszObjId);
    if (!algorithm.Parameters.cbData && known && !known->ecdsa) {
        algorithm.Parameters.cbData = sizeof(null_parameters);
        algorithm.Parameters.pbData = (BYTE *)null_parameters;
    }
    PCCERT_CONTEXT cert = nullptr;
    DWORD key_info_size = 0;
    CERT_PUBLIC_KEY_INFO *public_key = nullptr;
    BYTE *encoded = nullptr;
    DWORD encoded_size = 0;
    BYTE serial[16];
    if (!CryptExportPublicKeyInfoEx(key, spec, X509_ASN_ENCODING, nullptr, 0, nullptr, nullptr, &key_info_size))
        goto done;
    public_key = CrAlloc(key_info_size);
    if (!public_key) {
        SetLastError(ERROR_OUTOFMEMORY);
        goto done;
    }
    if (!CryptExportPublicKeyInfoEx(key, spec, X509_ASN_ENCODING, nullptr, 0, nullptr, public_key, &key_info_size))
        goto done;
    /* a random serial number that stays positive in der, as the number is kept least significant byte first. */
    if (!g_api.random || !g_api.random(serial, sizeof(serial))) {
        SetLastError(NTE_FAIL);
        goto done;
    }
    serial[sizeof(serial) - 1] &= 0x7F;
    serial[0] |= 1;
    {
        CERT_INFO info = {};
        info.dwVersion = CERT_V3;
        info.SerialNumber.cbData = sizeof(serial);
        info.SerialNumber.pbData = serial;
        info.SignatureAlgorithm = algorithm;
        info.Issuer = *pSubjectIssuerBlob;
        info.Subject = *pSubjectIssuerBlob;
        info.NotBefore = start;
        info.NotAfter = end;
        info.SubjectPublicKeyInfo = *public_key;
        if (pExtensions) {
            info.cExtension = pExtensions->cExtension;
            info.rgExtension = pExtensions->rgExtension;
        }
        if (dwFlags & CERT_CREATE_SELFSIGN_NO_SIGN) {
            /* the certificate with no signature: the content, the algorithm and an empty signature. */
            DWORD tbs_size = 0;
            BYTE *tbs = nullptr;
            if (!CryptEncodeObjectEx(X509_ASN_ENCODING, X509_CERT_TO_BE_SIGNED, &info, CRYPT_ENCODE_ALLOC_FLAG, nullptr,
                                     &tbs, &tbs_size))
                goto done;
            CERT_SIGNED_CONTENT_INFO content = {};
            content.ToBeSigned.cbData = tbs_size;
            content.ToBeSigned.pbData = tbs;
            content.SignatureAlgorithm = algorithm;
            encoded = CrEncodeAlloc(X509_CERT, &content, 0, &encoded_size);
            LocalFree(tbs);
        } else {
            if (!CryptSignAndEncodeCertificate(key, spec, X509_ASN_ENCODING, X509_CERT_TO_BE_SIGNED, &info, &algorithm,
                                               nullptr, nullptr, &encoded_size))
                goto done;
            encoded = CrAlloc(encoded_size);
            if (encoded && !CryptSignAndEncodeCertificate(key, spec, X509_ASN_ENCODING, X509_CERT_TO_BE_SIGNED, &info,
                                                          &algorithm, nullptr, encoded, &encoded_size)) {
                CrFree(encoded);
                encoded = nullptr;
            }
        }
    }
    if (!encoded) {
        if (!GetLastError())
            SetLastError(ERROR_OUTOFMEMORY);
        goto done;
    }
    cert = CertCreateCertificateContext(X509_ASN_ENCODING, encoded, encoded_size);
    if (cert && pKeyProvInfo && !(dwFlags & CERT_CREATE_SELFSIGN_NO_KEY_INFO) &&
        !CertSetCertificateContextProperty(cert, CERT_KEY_PROV_INFO_PROP_ID, 0, pKeyProvInfo)) {
        DWORD error = GetLastError();
        CertFreeCertificateContext(cert);
        SetLastError(error);
        cert = nullptr;
    }
done:;
    DWORD error = GetLastError();
    CrFree(encoded);
    CrFree(public_key);
    if (held.hCryptProv)
        CrReleaseKeyContext(&held);
    SetLastError(error);
    return cert;
}
