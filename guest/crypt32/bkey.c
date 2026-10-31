/* CryptExportPublicKeyInfoFromBCryptKeyHandle: the public key of a cng key as the key info of a certificate. */
#include "crypt32_private.h"

#define BCRYPT_RSAPUBLIC_MAGIC_ 0x31415352
#define BCRYPT_ECDSA_PUBLIC_P256_MAGIC_ 0x31534345
#define BCRYPT_ECDSA_PUBLIC_P384_MAGIC_ 0x33534345
#define BCRYPT_ECDSA_PUBLIC_P521_MAGIC_ 0x35534345
#define BCRYPT_ECDH_PUBLIC_P256_MAGIC_ 0x314B4345
#define BCRYPT_ECDH_PUBLIC_P384_MAGIC_ 0x334B4345
#define BCRYPT_ECDH_PUBLIC_P521_MAGIC_ 0x354B4345

typedef LONG(WINAPI *EXPORT_BCRYPT_KEY)(PVOID, PVOID, LPCWSTR, PUCHAR, ULONG, ULONG *, ULONG);

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
