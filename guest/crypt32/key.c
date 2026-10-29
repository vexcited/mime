/*
 * CryptAcquireCertificatePrivateKey: opens the key a certificate's properties name, through advapi32's legacy
 * providers or ncrypt's key storage providers, in crypt32's order and with its checks.
 */
#include "crypt32_private.h"

#define PP_CLIENT_HWND 1
#define PUBLICKEYBLOB_ 6
#define MS_KEY_STORAGE_PROVIDER_ L"Microsoft Software Key Storage Provider"
#define NCRYPT_SILENT_FLAG_ 0x40

typedef BOOL(WINAPI *ACQUIRE_CONTEXT)(HCRYPTPROV *, LPCWSTR, LPCWSTR, DWORD, DWORD);
typedef BOOL(WINAPI *RELEASE_CONTEXT)(HCRYPTPROV, DWORD);
typedef BOOL(WINAPI *SET_PROV_PARAM)(HCRYPTPROV, DWORD, const BYTE *, DWORD);
typedef BOOL(WINAPI *GET_USER_KEY)(HCRYPTPROV, DWORD, HCRYPTKEY *);
typedef BOOL(WINAPI *EXPORT_KEY)(HCRYPTKEY, HCRYPTKEY, DWORD, DWORD, BYTE *, DWORD *);
typedef BOOL(WINAPI *DESTROY_KEY)(HCRYPTKEY);
typedef BOOL(WINAPI *IMPORT_KEY)(HCRYPTPROV, const BYTE *, DWORD, HCRYPTKEY, DWORD, HCRYPTKEY *);
typedef LONG(WINAPI *OPEN_PROVIDER)(ULONG_PTR *, LPCWSTR, DWORD);
typedef LONG(WINAPI *OPEN_KEY)(ULONG_PTR, ULONG_PTR *, LPCWSTR, DWORD, DWORD);
typedef LONG(WINAPI *FREE_OBJECT)(ULONG_PTR);
typedef LONG(WINAPI *NCRYPT_EXPORT)(ULONG_PTR, ULONG_PTR, LPCWSTR, void *, BYTE *, DWORD, DWORD *, DWORD);
typedef BOOL(WINAPI *CREATE_HASH)(HCRYPTPROV, ALG_ID, HCRYPTKEY, DWORD, HCRYPTHASH *);
typedef BOOL(WINAPI *HASH_DATA)(HCRYPTHASH, const BYTE *, DWORD, DWORD);
typedef BOOL(WINAPI *SIGN_HASH)(HCRYPTHASH, DWORD, LPCWSTR, DWORD, BYTE *, DWORD *);
typedef BOOL(WINAPI *DESTROY_HASH)(HCRYPTHASH);

static struct {
    ACQUIRE_CONTEXT acquire;
    RELEASE_CONTEXT release;
    SET_PROV_PARAM set_param;
    GET_USER_KEY get_user_key;
    EXPORT_KEY export_key;
    DESTROY_KEY destroy_key;
    IMPORT_KEY import_key;
    OPEN_PROVIDER open_provider;
    OPEN_KEY open_key;
    FREE_OBJECT free_object;
    NCRYPT_EXPORT ncrypt_export;
    CREATE_HASH create_hash;
    HASH_DATA hash_data;
    SIGN_HASH sign_hash;
    DESTROY_HASH destroy_hash;
} g_api;

static INIT_ONCE g_api_once = INIT_ONCE_STATIC_INIT;

static BOOL WINAPI load_api(INIT_ONCE *once, void *param, void **context)
{
    HMODULE advapi32 = LoadLibraryW(L"advapi32.dll");
    if (advapi32) {
        g_api.acquire = (ACQUIRE_CONTEXT)(void *)GetProcAddress(advapi32, "CryptAcquireContextW");
        g_api.release = (RELEASE_CONTEXT)(void *)GetProcAddress(advapi32, "CryptReleaseContext");
        g_api.set_param = (SET_PROV_PARAM)(void *)GetProcAddress(advapi32, "CryptSetProvParam");
        g_api.get_user_key = (GET_USER_KEY)(void *)GetProcAddress(advapi32, "CryptGetUserKey");
        g_api.export_key = (EXPORT_KEY)(void *)GetProcAddress(advapi32, "CryptExportKey");
        g_api.destroy_key = (DESTROY_KEY)(void *)GetProcAddress(advapi32, "CryptDestroyKey");
        g_api.import_key = (IMPORT_KEY)(void *)GetProcAddress(advapi32, "CryptImportKey");
        g_api.create_hash = (CREATE_HASH)(void *)GetProcAddress(advapi32, "CryptCreateHash");
        g_api.hash_data = (HASH_DATA)(void *)GetProcAddress(advapi32, "CryptHashData");
        g_api.sign_hash = (SIGN_HASH)(void *)GetProcAddress(advapi32, "CryptSignHashW");
        g_api.destroy_hash = (DESTROY_HASH)(void *)GetProcAddress(advapi32, "CryptDestroyHash");
    }
    HMODULE ncrypt = LoadLibraryW(L"ncrypt.dll");
    if (ncrypt) {
        g_api.open_provider = (OPEN_PROVIDER)(void *)GetProcAddress(ncrypt, "NCryptOpenStorageProvider");
        g_api.open_key = (OPEN_KEY)(void *)GetProcAddress(ncrypt, "NCryptOpenKey");
        g_api.free_object = (FREE_OBJECT)(void *)GetProcAddress(ncrypt, "NCryptFreeObject");
        g_api.ncrypt_export = (NCRYPT_EXPORT)(void *)GetProcAddress(ncrypt, "NCryptExportKey");
    }
    return TRUE;
}

static void release_key(CERT_KEY_CONTEXT *key)
{
    if (!key->hCryptProv)
        return;
    DWORD error = GetLastError();
    if (key->dwKeySpec == CERT_NCRYPT_KEY_SPEC) {
        if (g_api.free_object)
            g_api.free_object(key->hNCryptKey);
    } else if (g_api.release) {
        g_api.release(key->hCryptProv, 0);
    }
    key->hCryptProv = 0;
    SetLastError(error);
}

/* the legacy path: CryptAcquireContextW on the container, the base providers opened as their enhanced ones. */
static BOOL acquire_capi(DWORD flags, const CRYPT_KEY_PROV_INFO *info, HWND window, CERT_KEY_CONTEXT *key)
{
    if (!g_api.acquire)
        return CrFail(NTE_PROVIDER_DLL_FAIL);
    DWORD context_flags = info->dwFlags & ~1u;
    if (flags & CRYPT_ACQUIRE_SILENT_FLAG)
        context_flags = (info->dwFlags & ~0x41u) | 0x40;
    key->dwKeySpec = info->dwKeySpec;
    LPCWSTR provider = info->pwszProvName;
    if (info->dwProvType == 1 && (!provider || !*provider ||
                                  !_wcsicmp(provider, L"Microsoft Base Cryptographic Provider v1.0")))
        provider = L"Microsoft Enhanced Cryptographic Provider v1.0";
    else if (info->dwProvType == 13 &&
             (!provider || !*provider ||
              !_wcsicmp(provider, L"Microsoft Base DSS and Diffie-Hellman Cryptographic Provider")))
        provider = L"Microsoft Enhanced DSS and Diffie-Hellman Cryptographic Provider";
    if (!g_api.acquire(&key->hCryptProv, info->pwszContainerName, provider, info->dwProvType, context_flags)) {
        key->hCryptProv = 0;
        return FALSE;
    }
    if (window && g_api.set_param && !g_api.set_param(key->hCryptProv, PP_CLIENT_HWND, (const BYTE *)&window, 0)) {
        release_key(key);
        return FALSE;
    }
    for (DWORD i = 0; i < info->cProvParam; i++)
        if (!g_api.set_param || !g_api.set_param(key->hCryptProv, info->rgProvParam[i].dwParam,
                                                 info->rgProvParam[i].pbData, info->rgProvParam[i].dwFlags)) {
            release_key(key);
            return FALSE;
        }
    return TRUE;
}

/* the cng path: the key storage provider by name, the container's key by spec. */
static BOOL acquire_cng(DWORD flags, const CRYPT_KEY_PROV_INFO *info, CERT_KEY_CONTEXT *key)
{
    key->dwKeySpec = CERT_NCRYPT_KEY_SPEC;
    if (!g_api.open_provider || !g_api.open_key || !g_api.free_object)
        return CrFail(NTE_PROVIDER_DLL_FAIL);
    LPCWSTR provider = info->pwszProvName && *info->pwszProvName ? info->pwszProvName : MS_KEY_STORAGE_PROVIDER_;
    ULONG_PTR handle = 0, k = 0;
    LONG error = g_api.open_provider(&handle, provider, 0);
    if (!error) {
        DWORD key_flags = info->dwFlags & 0x20;
        if ((flags | info->dwFlags) & CRYPT_ACQUIRE_SILENT_FLAG)
            key_flags |= NCRYPT_SILENT_FLAG_;
        error = g_api.open_key(handle, &k, info->pwszContainerName, info->dwKeySpec == CERT_NCRYPT_KEY_SPEC ? 0
                                                                                                           : info->dwKeySpec,
                               key_flags);
        g_api.free_object(handle);
    }
    if (error)
        return CrFail((DWORD)error);
    key->hNCryptKey = k;
    return TRUE;
}

/* ------------------------------------------------------------------ public key comparison */

/* the rsa modulus and exponent of the certificate's key, big endian, or its ec point. */
static BOOL cert_key(PCCERT_CONTEXT cert, const BYTE **modulus, DWORD *modulus_size, const BYTE **exponent,
                     DWORD *exponent_size, BOOL *rsa)
{
    const CERT_PUBLIC_KEY_INFO *info = &cert->pCertInfo->SubjectPublicKeyInfo;
    *rsa = info->Algorithm.pszObjId && !strcmp(info->Algorithm.pszObjId, szOID_RSA_RSA);
    if (!*rsa) {
        *modulus = info->PublicKey.pbData;
        *modulus_size = info->PublicKey.cbData;
        return *modulus_size > 1 && **modulus == 4;
    }
    der_reader r = {info->PublicKey.pbData, info->PublicKey.cbData};
    der_item seq, n, e;
    if (DerExpect(&r, ASN_SEQUENCE, &seq))
        return FALSE;
    DerOpen(&r, &seq);
    if (DerExpect(&r, ASN_INTEGER, &n) || DerExpect(&r, ASN_INTEGER, &e))
        return FALSE;
    *modulus = n.content;
    *modulus_size = n.length;
    while (*modulus_size && !**modulus) {
        (*modulus)++;
        (*modulus_size)--;
    }
    *exponent = e.content;
    *exponent_size = e.length;
    return TRUE;
}

static BOOL equal_reversed(const BYTE *be, DWORD size, const BYTE *le, DWORD le_size)
{
    for (DWORD i = 0; i < le_size; i++) {
        BYTE expected = i < size ? be[size - 1 - i] : 0;
        if (le[i] != expected)
            return FALSE;
    }
    return size <= le_size;
}

static BOOL same_public_key(PCCERT_CONTEXT cert, const CERT_KEY_CONTEXT *key)
{
    const BYTE *modulus = nullptr, *exponent = nullptr;
    DWORD modulus_size = 0, exponent_size = 0;
    BOOL rsa, same = FALSE;
    if (!cert_key(cert, &modulus, &modulus_size, &exponent, &exponent_size, &rsa))
        return FALSE;
    BYTE blob[4096];
    DWORD size = sizeof(blob);
    if (key->dwKeySpec == CERT_NCRYPT_KEY_SPEC) {
        LPCWSTR type = rsa ? L"RSAPUBLICBLOB" : L"ECCPUBLICBLOB";
        if (!g_api.ncrypt_export || g_api.ncrypt_export(key->hNCryptKey, 0, type, nullptr, blob, size, &size, 0))
            return FALSE;
        const DWORD *h = (const DWORD *)blob;
        if (rsa) {
            /* magic, bits, public exponent, modulus and prime sizes, then the exponent and the modulus. */
            if (size < 24 || h[3] != modulus_size || 24 + h[2] + h[3] > size)
                return FALSE;
            const BYTE *e = blob + 24, *n = e + h[2];
            DWORD e_size = h[2];
            while (exponent_size && !*exponent) {
                exponent++;
                exponent_size--;
            }
            while (e_size && !*e) {
                e++;
                e_size--;
            }
            same = !memcmp(n, modulus, modulus_size) && e_size == exponent_size && !memcmp(e, exponent, e_size);
        } else {
            same = size >= 8 && 2 * h[1] == modulus_size - 1 && 8 + 2 * h[1] <= size &&
                   !memcmp(blob + 8, modulus + 1, modulus_size - 1);
        }
        return same;
    }
    if (!rsa || !g_api.get_user_key || !g_api.export_key)
        return FALSE;
    HCRYPTKEY user;
    if (!g_api.get_user_key(key->hCryptProv, key->dwKeySpec, &user))
        return FALSE;
    if (g_api.export_key(user, 0, PUBLICKEYBLOB_, 0, blob, &size) && size >= 20) {
        DWORD bits, pubexp;
        memcpy(&bits, blob + 12, 4);
        memcpy(&pubexp, blob + 16, 4);
        DWORD e = 0;
        for (DWORD i = 0; i < exponent_size && i < 4; i++)
            e = e << 8 | exponent[i];
        same = (bits + 7) / 8 == modulus_size && pubexp == e && 20 + modulus_size <= size &&
               equal_reversed(modulus, modulus_size, blob + 20, modulus_size);
    }
    if (g_api.destroy_key)
        g_api.destroy_key(user);
    return same;
}

/* ------------------------------------------------------------------ the export */

static BOOL acquire_key(PCCERT_CONTEXT cert, DWORD flags, const CRYPT_KEY_PROV_INFO *info, HWND window,
                        CERT_KEY_CONTEXT *key, BOOL *mismatch)
{
    if (!info->dwProvType) {
        if (!(flags & CRYPT_ACQUIRE_NCRYPT_KEY_FLAGS_MASK))
            return CrFail(NTE_BAD_PROV_TYPE);
        flags = (flags & ~CRYPT_ACQUIRE_NCRYPT_KEY_FLAGS_MASK) | CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG;
    }
    if ((flags & CRYPT_ACQUIRE_PREFER_NCRYPT_KEY_FLAG) && (info->dwProvType == 3 || info->dwProvType == 13))
        flags = (flags & ~(CRYPT_ACQUIRE_PREFER_NCRYPT_KEY_FLAG | CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG)) |
                CRYPT_ACQUIRE_ALLOW_NCRYPT_KEY_FLAG;
    BOOL ok;
    if (flags & (CRYPT_ACQUIRE_PREFER_NCRYPT_KEY_FLAG | CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG)) {
        ok = acquire_cng(flags, info, key);
        if (!ok && !(flags & CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG))
            ok = acquire_capi(flags, info, window, key);
    } else {
        ok = acquire_capi(flags, info, window, key);
        if (!ok && (flags & CRYPT_ACQUIRE_NCRYPT_KEY_FLAGS_MASK))
            ok = acquire_cng(flags, info, key);
    }
    if (!ok) {
        release_key(key);
        return FALSE;
    }
    if ((flags & CRYPT_ACQUIRE_COMPARE_KEY_FLAG) && !same_public_key(cert, key)) {
        *mismatch = TRUE;
        release_key(key);
        return CrFail(NTE_BAD_PUBLIC_KEY);
    }
    return TRUE;
}

/* a key cached on the certificate, with the kind of handle the flags accept. */
static BOOL cached_key(PCCERT_CONTEXT cert, DWORD flags, CERT_KEY_CONTEXT *key)
{
    DWORD size = sizeof(*key);
    if (CertGetCertificateContextProperty(cert, CERT_KEY_CONTEXT_PROP_ID, key, &size) && key->hCryptProv) {
        if (key->dwKeySpec == CERT_NCRYPT_KEY_SPEC ? (flags & CRYPT_ACQUIRE_NCRYPT_KEY_FLAGS_MASK) != 0
                                                    : !(flags & CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG))
            return TRUE;
    }
    NCRYPT_KEY_HANDLE handle;
    size = sizeof(handle);
    if ((flags & CRYPT_ACQUIRE_NCRYPT_KEY_FLAGS_MASK) &&
        CertGetCertificateContextProperty(cert, CERT_NCRYPT_KEY_HANDLE_PROP_ID, &handle, &size) &&
        size == sizeof(handle) && handle) {
        key->hNCryptKey = handle;
        key->dwKeySpec = CERT_NCRYPT_KEY_SPEC;
        return TRUE;
    }
    HCRYPTPROV prov;
    size = sizeof(prov);
    if (!(flags & CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG) &&
        CertGetCertificateContextProperty(cert, CERT_KEY_PROV_HANDLE_PROP_ID, &prov, &size) && prov) {
        DWORD spec = AT_KEYEXCHANGE;
        size = sizeof(spec);
        CertGetCertificateContextProperty(cert, CERT_KEY_SPEC_PROP_ID, &spec, &size);
        key->hCryptProv = prov;
        key->dwKeySpec = spec;
        return TRUE;
    }
    return FALSE;
}

BOOL WINAPI CryptAcquireCertificatePrivateKey(PCCERT_CONTEXT pCert, DWORD dwFlags, void *pvParameters,
                                              HCRYPTPROV_OR_NCRYPT_KEY_HANDLE *phCryptProvOrNCryptKey,
                                              DWORD *pdwKeySpec, BOOL *pfCallerFreeProvOrNCryptKey)
{
    if ((dwFlags & (CRYPT_ACQUIRE_SILENT_FLAG | CRYPT_ACQUIRE_WINDOW_HANDLE_FLAG)) ==
        (CRYPT_ACQUIRE_SILENT_FLAG | CRYPT_ACQUIRE_WINDOW_HANDLE_FLAG))
        return CrFail(ERROR_INVALID_PARAMETER);
    if (!pCert || !phCryptProvOrNCryptKey)
        return CrFail(E_INVALIDARG);
    InitOnceExecuteOnce(&g_api_once, load_api, nullptr, nullptr);
    HWND window = (dwFlags & CRYPT_ACQUIRE_WINDOW_HANDLE_FLAG) && pvParameters ? *(HWND *)pvParameters : nullptr;
    DWORD flags = dwFlags & ~CRYPT_ACQUIRE_NO_HEALING;
    CERT_KEY_CONTEXT key = {sizeof(CERT_KEY_CONTEXT)};
    if ((flags & (CRYPT_ACQUIRE_CACHE_FLAG | CRYPT_ACQUIRE_USE_PROV_INFO_FLAG)) && cached_key(pCert, flags, &key)) {
        *phCryptProvOrNCryptKey = key.hCryptProv;
        if (pdwKeySpec)
            *pdwKeySpec = key.dwKeySpec;
        if (pfCallerFreeProvOrNCryptKey)
            *pfCallerFreeProvOrNCryptKey = FALSE;
        return TRUE;
    }
    DWORD size = 0;
    if (!CertGetCertificateContextProperty(pCert, CERT_KEY_PROV_INFO_PROP_ID, nullptr, &size)) {
        /* windows would look the key up by the certificate's key identifier; mime keeps no such index. */
        return CrFail(dwFlags & CRYPT_ACQUIRE_NO_HEALING ? CRYPT_E_NOT_FOUND : CRYPT_E_NO_KEY_PROPERTY);
    }
    CRYPT_KEY_PROV_INFO *info = CrAlloc(size);
    if (!info)
        return CrFail(ERROR_OUTOFMEMORY);
    BOOL mismatch = FALSE, ok = CertGetCertificateContextProperty(pCert, CERT_KEY_PROV_INFO_PROP_ID, info, &size) &&
                               acquire_key(pCert, flags, info, window, &key, &mismatch);
    BOOL cache = ok && ((flags & CRYPT_ACQUIRE_CACHE_FLAG) ||
                        ((flags & CRYPT_ACQUIRE_USE_PROV_INFO_FLAG) && (info->dwFlags & CERT_SET_KEY_CONTEXT_PROP_ID)));
    CrFree(info);
    if (!ok)
        return FALSE;
    if (cache && !CertSetCertificateContextProperty(pCert, CERT_KEY_CONTEXT_PROP_ID, 0, &key)) {
        release_key(&key);
        return FALSE;
    }
    *phCryptProvOrNCryptKey = key.hCryptProv;
    if (pdwKeySpec)
        *pdwKeySpec = key.dwKeySpec;
    if (pfCallerFreeProvOrNCryptKey)
        *pfCallerFreeProvOrNCryptKey = !cache;
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

/*
 * only RSA keys are imported: the modulus and exponent become a public key blob for the provider. the algorithm is
 * the one given, or key exchange by default. mime's providers have no use for other algorithms.
 */
BOOL WINAPI CryptImportPublicKeyInfoEx(HCRYPTPROV hCryptProv, DWORD dwCertEncodingType, PCERT_PUBLIC_KEY_INFO pInfo,
                                       ALG_ID aiKeyAlg, DWORD dwFlags, void *pvAuxInfo, HCRYPTKEY *phKey)
{
    if (!pInfo || !phKey || dwFlags || pvAuxInfo)
        return CrFail(dwFlags || pvAuxInfo ? ERROR_INVALID_PARAMETER : E_INVALIDARG);
    if ((dwCertEncodingType & 0xFFFF) != X509_ASN_ENCODING)
        return CrFail(ERROR_FILE_NOT_FOUND);
    *phKey = 0;
    InitOnceExecuteOnce(&g_api_once, load_api, nullptr, nullptr);
    if (!g_api.import_key)
        return CrFail(ERROR_PROC_NOT_FOUND);
    if (!pInfo->Algorithm.pszObjId || strcmp(pInfo->Algorithm.pszObjId, szOID_RSA_RSA))
        return CrFail(NTE_BAD_ALGID);
    der_reader r = {pInfo->PublicKey.pbData, pInfo->PublicKey.cbData};
    der_item seq, n, e;
    if (DerExpect(&r, ASN_SEQUENCE, &seq))
        return CrFail(NTE_BAD_DATA);
    DerOpen(&r, &seq);
    if (DerExpect(&r, ASN_INTEGER, &n) || DerExpect(&r, ASN_INTEGER, &e))
        return CrFail(NTE_BAD_DATA);
    const BYTE *modulus = n.content;
    DWORD modulus_size = n.length;
    while (modulus_size > 1 && !*modulus) {
        modulus++;
        modulus_size--;
    }
    if (!modulus_size || e.length == 0 || e.length > 4)
        return CrFail(NTE_BAD_DATA);
    DWORD exponent = 0;
    for (DWORD i = 0; i < e.length; i++)
        exponent = exponent << 8 | e.content[i];
    DWORD bits = modulus_size * 8;
    for (BYTE top = modulus[0]; top && !(top & 0x80); top <<= 1)
        bits--;
    DWORD bytes = (bits + 7) / 8;
    if (bytes < modulus_size)
        modulus += modulus_size - bytes;
    else if (bytes > modulus_size)
        bytes = modulus_size;
    DWORD size = 8 + 12 + bytes;
    BYTE *blob = CrAlloc(size);
    if (!blob)
        return CrFail(ERROR_OUTOFMEMORY);
    blob[0] = PUBLICKEYBLOB;
    blob[1] = 2;
    *(WORD *)(blob + 2) = 0;
    *(DWORD *)(blob + 4) = aiKeyAlg ? aiKeyAlg : CALG_RSA_KEYX;
    memcpy(blob + 8, "RSA1", 4);
    *(DWORD *)(blob + 12) = bits;
    *(DWORD *)(blob + 16) = exponent;
    for (DWORD i = 0; i < bytes; i++)
        blob[20 + i] = modulus[bytes - 1 - i];
    BOOL ok = g_api.import_key(hCryptProv, blob, size, 0, 0, phKey);
    CrFree(blob);
    return ok;
}

BOOL WINAPI CryptImportPublicKeyInfo(HCRYPTPROV hCryptProv, DWORD dwCertEncodingType, PCERT_PUBLIC_KEY_INFO pInfo,
                                     HCRYPTKEY *phKey)
{
    return CryptImportPublicKeyInfoEx(hCryptProv, dwCertEncodingType, pInfo, 0, 0, nullptr, phKey);
}

/* signs the digest of data with a legacy provider's key. the signature comes out as the provider makes it, little endian. */
BOOL CrSignData(HCRYPTPROV provider, DWORD key_spec, ALG_ID alg, const BYTE *data, DWORD size, BYTE *signature,
                DWORD *signature_size)
{
    InitOnceExecuteOnce(&g_api_once, load_api, nullptr, nullptr);
    if (!g_api.create_hash || !g_api.hash_data || !g_api.sign_hash || !g_api.destroy_hash)
        return CrFail(NTE_PROVIDER_DLL_FAIL);
    HCRYPTHASH hash;
    if (!g_api.create_hash(provider, alg, 0, 0, &hash))
        return FALSE;
    BOOL ok = g_api.hash_data(hash, data, size, 0) && g_api.sign_hash(hash, key_spec, nullptr, 0, signature, signature_size);
    DWORD error = GetLastError();
    g_api.destroy_hash(hash);
    SetLastError(error);
    return ok;
}

/* gives back what CryptAcquireCertificatePrivateKey handed out, when the caller owns it. */
void CrReleaseProvider(HCRYPTPROV provider)
{
    InitOnceExecuteOnce(&g_api_once, load_api, nullptr, nullptr);
    if (g_api.release)
        g_api.release(provider, 0);
}
