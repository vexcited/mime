/* the public key of a cng key as the key info of a certificate, and back. */
#include "crypt32_private.h"

#define BCRYPT_RSAPUBLIC_MAGIC_ 0x31415352
#define BCRYPT_ECDSA_PUBLIC_P256_MAGIC_ 0x31534345
#define BCRYPT_ECDSA_PUBLIC_P384_MAGIC_ 0x33534345
#define BCRYPT_ECDSA_PUBLIC_P521_MAGIC_ 0x35534345
#define BCRYPT_ECDH_PUBLIC_P256_MAGIC_ 0x314B4345
#define BCRYPT_ECDH_PUBLIC_P384_MAGIC_ 0x334B4345
#define BCRYPT_ECDH_PUBLIC_P521_MAGIC_ 0x354B4345

#define CRYPT_OID_INFO_PUBKEY_ENCRYPT_KEY_FLAG_ 0x40000000

typedef LONG(WINAPI *EXPORT_BCRYPT_KEY)(PVOID, PVOID, LPCWSTR, PUCHAR, ULONG, ULONG *, ULONG);
typedef LONG(WINAPI *OPEN_BCRYPT_ALGORITHM)(PVOID *, LPCWSTR, LPCWSTR, ULONG);
typedef LONG(WINAPI *CLOSE_BCRYPT_ALGORITHM)(PVOID, ULONG);
typedef LONG(WINAPI *IMPORT_BCRYPT_KEY_PAIR)(PVOID, PVOID, LPCWSTR, PVOID *, PUCHAR, ULONG, ULONG);

/* an unsigned big endian number as an asn.1 integer. */
static void write_big_endian(der_writer *w, const BYTE *data, DWORD size)
{
    while (size > 1 && !*data) {
        data++;
        size--;
    }
    if (!size) {
        BYTE zero = 0;
        DerWriteTlv(w, ASN_INTEGER, &zero, 1);
        return;
    }
    DWORD mark = DerBegin(w, ASN_INTEGER);
    if (*data & 0x80) {
        BYTE zero = 0;
        DerWriteBytes(w, &zero, 1);
    }
    DerWriteBytes(w, data, size);
    DerEnd(w, mark);
}

WINCRYPT32API BOOL WINAPI CryptExportPublicKeyInfoFromBCryptKeyHandle(PVOID key, DWORD encoding, LPSTR oid, DWORD flags, void *aux,
                                                        PCERT_PUBLIC_KEY_INFO info, DWORD *size)
{
    if (!key || !size)
        return CrFail(E_INVALIDARG);
    if (flags || aux)
        return CrFail(ERROR_INVALID_PARAMETER);
    if ((encoding & 0xFFFF) != X509_ASN_ENCODING)
        return CrFail(ERROR_FILE_NOT_FOUND);
    HMODULE bcrypt = LoadLibraryW(L"bcrypt.dll");
    EXPORT_BCRYPT_KEY export_key = bcrypt ? (EXPORT_BCRYPT_KEY)GetProcAddress(bcrypt, "BCryptExportKey") : nullptr;
    if (!export_key)
        return CrFail(ERROR_PROC_NOT_FOUND);
    ULONG blob_size = 0;
    LONG status = export_key(key, nullptr, L"PUBLICBLOB", nullptr, 0, &blob_size, 0);
    BYTE *blob = status >= 0 ? CrAlloc(blob_size) : nullptr;
    if (blob)
        status = export_key(key, nullptr, L"PUBLICBLOB", blob, blob_size, &blob_size, 0);
    if (status < 0 || !blob || blob_size < 8) {
        CrFree(blob);
        FreeLibrary(bcrypt);
        return CrFail(status < 0 ? (DWORD)status : ERROR_OUTOFMEMORY);
    }
    FreeLibrary(bcrypt);
    ULONG magic = *(ULONG *)blob;
    der_writer parameters, key_bits;
    DerWriteInit(&parameters);
    DerWriteInit(&key_bits);
    LPCSTR algorithm = oid;
    BOOL ok = TRUE;
    if (magic == BCRYPT_RSAPUBLIC_MAGIC_) {
        /* the key blob: the sizes of the exponent and modulus, then both, big endian. */
        ULONG exponent = ((ULONG *)blob)[2], modulus = ((ULONG *)blob)[3];
        if (blob_size < 24 + (SIZE_T)exponent + modulus) {
            ok = FALSE;
        } else {
            DWORD mark = DerBegin(&key_bits, ASN_SEQUENCE);
            write_big_endian(&key_bits, blob + 24 + exponent, modulus);
            write_big_endian(&key_bits, blob + 24, exponent);
            DerEnd(&key_bits, mark);
            DerWriteTlv(&parameters, ASN_NULL, nullptr, 0);
            if (!algorithm)
                algorithm = szOID_RSA_RSA;
        }
    } else if (magic == BCRYPT_ECDSA_PUBLIC_P256_MAGIC_ || magic == BCRYPT_ECDH_PUBLIC_P256_MAGIC_ ||
               magic == BCRYPT_ECDSA_PUBLIC_P384_MAGIC_ || magic == BCRYPT_ECDH_PUBLIC_P384_MAGIC_ ||
               magic == BCRYPT_ECDSA_PUBLIC_P521_MAGIC_ || magic == BCRYPT_ECDH_PUBLIC_P521_MAGIC_) {
        ULONG coordinate = ((ULONG *)blob)[1];
        if (blob_size < 8 + 2 * (SIZE_T)coordinate) {
            ok = FALSE;
        } else {
            LPCSTR curve = (magic & 0xFF000000) == 0x31000000 ? "1.2.840.10045.3.1.7"
                           : (magic & 0xFF000000) == 0x33000000 ? "1.3.132.0.34" : "1.3.132.0.35";
            DerWriteOid(&parameters, curve);
            BYTE point = 4;
            DerWriteBytes(&key_bits, &point, 1);
            DerWriteBytes(&key_bits, blob + 8, 2 * coordinate);
            if (!algorithm)
                algorithm = szOID_ECC_PUBLIC_KEY;
        }
    } else {
        ok = FALSE;
    }
    CrFree(blob);
    if (!ok || parameters.error || key_bits.error) {
        DerWriteFree(&parameters);
        DerWriteFree(&key_bits);
        return CrFail(ok ? ERROR_OUTOFMEMORY : NTE_BAD_ALGID);
    }
    DWORD oid_size = (DWORD)strlen(algorithm) + 1;
    DWORD total = (DWORD)sizeof(CERT_PUBLIC_KEY_INFO) + oid_size + parameters.size + key_bits.size;
    if (info && *size >= total) {
        BYTE *at = (BYTE *)(info + 1);
        memset(info, 0, sizeof(*info));
        info->Algorithm.pszObjId = (LPSTR)at;
        memcpy(at, algorithm, oid_size);
        at += oid_size;
        info->Algorithm.Parameters.cbData = parameters.size;
        info->Algorithm.Parameters.pbData = at;
        memcpy(at, parameters.data, parameters.size);
        at += parameters.size;
        info->PublicKey.cbData = key_bits.size;
        info->PublicKey.pbData = at;
        info->PublicKey.cUnusedBits = 0;
        memcpy(at, key_bits.data, key_bits.size);
    }
    DWORD capacity = *size;
    *size = total;
    DerWriteFree(&parameters);
    DerWriteFree(&key_bits);
    if (!info)
        return TRUE;
    if (capacity < total)
        return CrFail(ERROR_MORE_DATA);
    return TRUE;
}

/* the bytes of an unsigned big endian number, without its leading zeros. */
static const BYTE *strip_zeros(const der_item *item, DWORD *size)
{
    const BYTE *data = item->content;
    *size = item->length;
    while (*size > 1 && !*data) {
        data++;
        (*size)--;
    }
    return data;
}

/* an rsa key as BCRYPT_RSAPUBLIC_BLOB: magic, bits, exponent and modulus sizes, then both, big endian. */
static BYTE *rsa_blob(const CERT_PUBLIC_KEY_INFO *info, ULONG *size)
{
    der_reader r = {info->PublicKey.pbData, info->PublicKey.cbData};
    der_item seq, n, e;
    if (DerExpect(&r, ASN_SEQUENCE, &seq))
        return nullptr;
    DerOpen(&r, &seq);
    if (DerExpect(&r, ASN_INTEGER, &n) || DerExpect(&r, ASN_INTEGER, &e))
        return nullptr;
    DWORD modulus_size, exponent_size;
    const BYTE *modulus = strip_zeros(&n, &modulus_size), *exponent = strip_zeros(&e, &exponent_size);
    if (!modulus_size || !*modulus || !exponent_size)
        return nullptr;
    DWORD bits = modulus_size * 8;
    for (BYTE top = modulus[0]; !(top & 0x80); top <<= 1)
        bits--;
    *size = 24 + exponent_size + modulus_size;
    BYTE *blob = CrAlloc(*size);
    if (!blob)
        return nullptr;
    ULONG *header = (ULONG *)blob;
    header[0] = BCRYPT_RSAPUBLIC_MAGIC_;
    header[1] = bits;
    header[2] = exponent_size;
    header[3] = modulus_size;
    header[4] = header[5] = 0;
    memcpy(blob + 24, exponent, exponent_size);
    memcpy(blob + 24 + exponent_size, modulus, modulus_size);
    return blob;
}

/* an uncompressed point on a named curve as BCRYPT_ECCPUBLIC_BLOB: magic, coordinate size, then x and y. */
static BYTE *ec_blob(const CERT_PUBLIC_KEY_INFO *info, BOOL exchange, LPCWSTR *algorithm, ULONG *size)
{
    der_reader r = {info->Algorithm.Parameters.pbData, info->Algorithm.Parameters.cbData};
    der_item oid;
    char curve[32];
    if (DerExpect(&r, ASN_OID, &oid) || DerParseOid(&oid, curve, sizeof(curve)))
        return nullptr;
    ULONG magic, coordinate;
    if (!strcmp(curve, "1.2.840.10045.3.1.7")) {
        magic = exchange ? BCRYPT_ECDH_PUBLIC_P256_MAGIC_ : BCRYPT_ECDSA_PUBLIC_P256_MAGIC_;
        *algorithm = exchange ? L"ECDH_P256" : L"ECDSA_P256";
        coordinate = 32;
    } else if (!strcmp(curve, "1.3.132.0.34")) {
        magic = exchange ? BCRYPT_ECDH_PUBLIC_P384_MAGIC_ : BCRYPT_ECDSA_PUBLIC_P384_MAGIC_;
        *algorithm = exchange ? L"ECDH_P384" : L"ECDSA_P384";
        coordinate = 48;
    } else if (!strcmp(curve, "1.3.132.0.35")) {
        magic = exchange ? BCRYPT_ECDH_PUBLIC_P521_MAGIC_ : BCRYPT_ECDSA_PUBLIC_P521_MAGIC_;
        *algorithm = exchange ? L"ECDH_P521" : L"ECDSA_P521";
        coordinate = 66;
    } else {
        return nullptr;
    }
    const BYTE *point = info->PublicKey.pbData;
    if (info->PublicKey.cbData != 1 + 2 * coordinate || point[0] != 4)
        return nullptr;
    *size = 8 + 2 * coordinate;
    BYTE *blob = CrAlloc(*size);
    if (!blob)
        return nullptr;
    ((ULONG *)blob)[0] = magic;
    ((ULONG *)blob)[1] = coordinate;
    memcpy(blob + 8, point + 1, 2 * coordinate);
    return blob;
}

WINCRYPT32API BOOL WINAPI CryptImportPublicKeyInfoEx2(DWORD encoding, PCERT_PUBLIC_KEY_INFO info, DWORD flags, void *aux,
                                                     PVOID *key)
{
    if (!info || !key)
        return CrFail(E_INVALIDARG);
    if (aux)
        return CrFail(ERROR_INVALID_PARAMETER);
    if ((encoding & 0xFFFF) != X509_ASN_ENCODING)
        return CrFail(ERROR_FILE_NOT_FOUND);
    *key = nullptr;
    LPCSTR oid = info->Algorithm.pszObjId;
    LPCWSTR algorithm = nullptr, type = nullptr;
    ULONG size = 0;
    BYTE *blob = nullptr;
    if (oid && !strcmp(oid, szOID_RSA_RSA)) {
        algorithm = L"RSA";
        type = L"RSAPUBLICBLOB";
        blob = rsa_blob(info, &size);
    } else if (oid && !strcmp(oid, szOID_ECC_PUBLIC_KEY)) {
        type = L"ECCPUBLICBLOB";
        blob = ec_blob(info, (flags & CRYPT_OID_INFO_PUBKEY_ENCRYPT_KEY_FLAG_) != 0, &algorithm, &size);
    } else {
        return CrFail(NTE_BAD_ALGID);
    }
    if (!blob)
        return CrFail(NTE_BAD_DATA);
    /* bcrypt stays loaded for the key. */
    HMODULE bcrypt = LoadLibraryW(L"bcrypt.dll");
    OPEN_BCRYPT_ALGORITHM open = bcrypt ? (OPEN_BCRYPT_ALGORITHM)GetProcAddress(bcrypt, "BCryptOpenAlgorithmProvider")
                                        : nullptr;
    CLOSE_BCRYPT_ALGORITHM close = bcrypt ? (CLOSE_BCRYPT_ALGORITHM)GetProcAddress(bcrypt, "BCryptCloseAlgorithmProvider")
                                          : nullptr;
    IMPORT_BCRYPT_KEY_PAIR import = bcrypt ? (IMPORT_BCRYPT_KEY_PAIR)GetProcAddress(bcrypt, "BCryptImportKeyPair")
                                           : nullptr;
    if (!open || !close || !import) {
        CrFree(blob);
        return CrFail(ERROR_PROC_NOT_FOUND);
    }
    PVOID provider;
    LONG status = open(&provider, algorithm, nullptr, 0);
    if (status >= 0) {
        /* the key keeps its provider alive. */
        status = import(provider, nullptr, type, key, blob, size, 0);
        close(provider, 0);
    }
    CrFree(blob);
    return status >= 0 ? TRUE : CrFail(RtlNtStatusToDosError(status));
}
