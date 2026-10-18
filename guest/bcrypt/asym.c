/*
 * key pairs: rsa, the nist curves for ecdh and ecdsa, and finite field
 * diffie-hellman. generation in two steps, the blob formats of import and
 * export, signatures, rsa encryption, secret agreement and the derivations
 * of an agreed secret.
 */
#include "bcrypt_private.h"

#define CAPI_PUBLICKEYBLOB 0x6
#define CAPI_PRIVATEKEYBLOB 0x7
#define CAPI_RSA1 0x31415352
#define CAPI_RSA2 0x32415352
#define CALG_RSA_KEYX_ 0xA400
#define CALG_RSA_SIGN_ 0x2400

typedef struct {
    BYTE bType;
    BYTE bVersion;
    WORD reserved;
    ULONG aiKeyAlg;
    ULONG magic;
    ULONG bitlen;
    ULONG pubexp;
} CAPI_RSA_HEADER;

static BOOL is_rsa(const ALG_INFO *info)
{
    return info->id == ID_RSA || info->id == ID_RSA_SIGN;
}

static BOOL is_dh(const ALG_INFO *info)
{
    return info->id == ID_DH;
}

static BOOL is_ecdsa(const ALG_INFO *info)
{
    return info->id >= ID_ECDSA && info->id <= ID_ECDSA_P521;
}

static BOOL is_ec(const ALG_INFO *info)
{
    return (info->id >= ID_ECDH && info->id <= ID_ECDH_P521) || is_ecdsa(info);
}

/* ------------------------------------------------------------------ curves */

static const struct {
    const WCHAR *name;
    const MimeEcCurve *curve;
} g_curves[] = {
    {L"nistP256", &mime_p256},  {L"nistP384", &mime_p384},  {L"nistP521", &mime_p521},
    {L"secP256r1", &mime_p256}, {L"secP384r1", &mime_p384}, {L"secP521r1", &mime_p521},
};

const MimeEcCurve *BcCurveByName(LPCWSTR name)
{
    for (ULONG i = 0; name && i < RTL_NUMBER_OF(g_curves); i++)
        if (!_wcsicmp(g_curves[i].name, name))
            return g_curves[i].curve;
    return nullptr;
}

static const WCHAR *curve_name(const MimeEcCurve *curve)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_curves); i++)
        if (g_curves[i].curve == curve)
            return g_curves[i].name;
    return L"";
}

NTSTATUS BcEccCurveProperty(const MimeEcCurve *curve, LPCWSTR property, PUCHAR output, ULONG capacity, ULONG *result)
{
    if (!wcscmp(property, BCRYPT_ECC_CURVE_NAME_LIST)) {
        ULONG count = RTL_NUMBER_OF(g_curves), size = sizeof(BCRYPT_ECC_CURVE_NAMES) + count * sizeof(LPWSTR);
        for (ULONG i = 0; i < count; i++)
            size += (ULONG)(wcslen(g_curves[i].name) + 1) * sizeof(WCHAR);
        *result = size;
        if (!output)
            return STATUS_SUCCESS;
        if (capacity < size)
            return STATUS_BUFFER_TOO_SMALL;
        BCRYPT_ECC_CURVE_NAMES *names = (BCRYPT_ECC_CURVE_NAMES *)output;
        LPWSTR *list = (LPWSTR *)(names + 1);
        WCHAR *text = (WCHAR *)(list + count);
        names->dwEccCurveNames = count;
        names->pEccCurveNames = list;
        for (ULONG i = 0; i < count; i++) {
            SIZE_T n = wcslen(g_curves[i].name) + 1;
            memcpy(text, g_curves[i].name, n * sizeof(WCHAR));
            list[i] = text;
            text += n;
        }
        return STATUS_SUCCESS;
    }
    if (!curve)
        return STATUS_INVALID_PARAMETER;
    if (!wcscmp(property, BCRYPT_ECC_CURVE_NAME)) {
        const WCHAR *name = curve_name(curve);
        return BcOutput(name, (ULONG)(wcslen(name) + 1) * sizeof(WCHAR), output, capacity, result);
    }
    if (!wcscmp(property, BCRYPT_ECC_PARAMETERS)) {
        ULONG field = mime_ec_size(curve), size = sizeof(BCRYPT_ECC_PARAMETER_HEADER) + 6 * field + 1;
        *result = size;
        if (!output)
            return STATUS_SUCCESS;
        if (capacity < size)
            return STATUS_BUFFER_TOO_SMALL;
        BCRYPT_ECC_PARAMETER_HEADER *h = (BCRYPT_ECC_PARAMETER_HEADER *)output;
        h->dwVersion = 1;
        h->dwCurveType = 1;
        h->dwCurveGenerationAlgId = 0;
        h->cbFieldLength = field;
        h->cbSubgroupOrder = field;
        h->cbCofactor = 1;
        h->cbSeed = 0;
        PUCHAR p = (PUCHAR)(h + 1);
        mime_ec_parameters(curve, p, p + field, p + 2 * field, p + 3 * field, p + 4 * field, p + 5 * field);
        p[6 * field] = 1;
        return STATUS_SUCCESS;
    }
    return STATUS_NOT_SUPPORTED;
}

/* the magics of the named curves, public then private, for ecdh then ecdsa. */
static ULONG ec_magic(const MimeEcCurve *curve, BOOL ecdsa, BOOL private_key)
{
    ULONG index = curve == &mime_p256 ? 1 : curve == &mime_p384 ? 3 : 5;
    return (ecdsa ? 0x00534345 : 0x004B4345) | (('0' + index + (private_key ? 1 : 0)) << 24);
}

/* the curve a blob magic names, null for the generic magics, and whether it holds a private key. */
static BOOL ec_magic_parse(ULONG magic, BOOL ecdsa, const MimeEcCurve **curve, BOOL *private_key)
{
    const MimeEcCurve *curves[3] = {&mime_p256, &mime_p384, &mime_p521};
    ULONG base = ecdsa ? 0x00534345 : 0x004B4345;
    if ((magic & 0x00FFFFFF) == base) {
        ULONG digit = (magic >> 24) - '1';
        if (digit < 6) {
            *curve = curves[digit / 2];
            *private_key = digit & 1;
            return TRUE;
        }
    }
    ULONG generic_public = ecdsa ? BCRYPT_ECDSA_PUBLIC_GENERIC_MAGIC : BCRYPT_ECDH_PUBLIC_GENERIC_MAGIC;
    ULONG generic_private = ecdsa ? BCRYPT_ECDSA_PRIVATE_GENERIC_MAGIC : BCRYPT_ECDH_PRIVATE_GENERIC_MAGIC;
    if (magic == generic_public || magic == generic_private) {
        *curve = nullptr;
        *private_key = magic == generic_private;
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ key objects */

static KEY_PAIR *new_pair(ALG *alg)
{
    KEY_PAIR *key = BcAllocZero(sizeof(KEY_PAIR));
    if (!key)
        return nullptr;
    key->size = sizeof(KEY_PAIR);
    key->magic = KEY_MAGIC;
    key->alg = alg;
    key->kind = KIND_PAIR;
    key->curve = alg->curve;
    const ALG_INFO *info = alg->info;
    BOOL ok = TRUE;
    if (is_rsa(info))
        ok = (key->rsa = mime_rsa_new()) != nullptr;
    else if (is_dh(info))
        ok = (key->dh = mime_dh_new()) != nullptr;
    else
        ok = (key->ec = BcAllocZero(sizeof(MimeEcKey))) != nullptr;
    if (!ok) {
        BcFree(key);
        return nullptr;
    }
    return key;
}

void BcPairDestroy(KEY_PAIR *key)
{
    mime_rsa_free(key->rsa);
    mime_dh_free(key->dh);
    if (key->ec) {
        SecureZeroMemory(key->ec, sizeof(*key->ec));
        BcFree(key->ec);
    }
    SecureZeroMemory(key, sizeof(*key));
    BcFree(key);
}

static BOOL pair_iface(const ALG_INFO *info)
{
    return info->iface == BCRYPT_ASYMMETRIC_ENCRYPTION_INTERFACE || info->iface == BCRYPT_SECRET_AGREEMENT_INTERFACE ||
           info->iface == BCRYPT_SIGNATURE_INTERFACE;
}

BCRYPTAPI NTSTATUS WINAPI BCryptGenerateKeyPair(BCRYPT_ALG_HANDLE hAlgorithm, BCRYPT_KEY_HANDLE *phKey,
                                                ULONG dwLength, ULONG dwFlags)
{
    ALG *alg = BcAlg(hAlgorithm);
    if (!alg)
        return STATUS_INVALID_HANDLE;
    if (!phKey)
        return STATUS_INVALID_PARAMETER;
    const ALG_INFO *info = alg->info;
    if (!pair_iface(info))
        return STATUS_NOT_SUPPORTED;
    if (is_rsa(info)) {
        ULONG minimum = dwFlags & 0x10000 ? 384 : 512;
        if ((dwFlags & ~0x10000u) || dwLength < minimum || dwLength > 16384 || dwLength % 8)
            return STATUS_INVALID_PARAMETER;
    } else if (is_dh(info)) {
        if (dwFlags || (dwLength && (dwLength % 8 || dwLength < 512 || dwLength > 4096)))
            return STATUS_INVALID_PARAMETER;
        if (!dwLength)
            dwLength = 1024;
    } else {
        if (dwFlags || (dwLength && (!alg->curve || dwLength != mime_ec_bits(alg->curve))))
            return STATUS_INVALID_PARAMETER;
        dwLength = alg->curve ? mime_ec_bits(alg->curve) : 0;
    }
    KEY_PAIR *key = new_pair(alg);
    if (!key)
        return STATUS_NO_MEMORY;
    key->bits = dwLength;
    BcAlgRef(alg);
    *phKey = key;
    return STATUS_SUCCESS;
}

BCRYPTAPI NTSTATUS WINAPI BCryptFinalizeKeyPair(BCRYPT_KEY_HANDLE hKey, ULONG dwFlags)
{
    KEY_HEADER *header = BcKey(hKey);
    if (!header)
        return STATUS_INVALID_HANDLE;
    if (header->kind != KIND_PAIR)
        return STATUS_NOT_SUPPORTED;
    KEY_PAIR *key = (KEY_PAIR *)header;
    const ALG_INFO *info = key->alg->info;
    if (key->finalized)
        return STATUS_INVALID_HANDLE;
    if (dwFlags && !(is_dh(info) && dwFlags == 24))
        return STATUS_INVALID_PARAMETER;
    int status;
    if (is_rsa(info)) {
        status = mime_rsa_generate(key->rsa, key->bits);
    } else if (is_dh(info)) {
        status = MIME_CRYPTO_OK;
        if (!key->dh->size)
            status = mime_dh_group(key->dh, key->bits);
        if (!status)
            status = mime_dh_generate(key->dh);
    } else {
        if (!key->curve)
            return STATUS_INVALID_PARAMETER;
        status = mime_ec_generate(key->ec, key->curve);
        key->bits = mime_ec_bits(key->curve);
    }
    if (status)
        return BcStatus(status);
    key->finalized = TRUE;
    key->has_private = TRUE;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ import */

static BOOL load(MimeBn *bn, const UCHAR **p, ULONG size)
{
    BOOL ok = mime_bn_from_bytes(bn, *p, size);
    *p += size;
    return ok;
}

/* a little endian field of a capi blob. */
static BOOL load_le(MimeBn *bn, const UCHAR **p, ULONG size)
{
    UCHAR buffer[2048];
    if (size > sizeof(buffer))
        return FALSE;
    for (ULONG i = 0; i < size; i++)
        buffer[i] = (*p)[size - 1 - i];
    *p += size;
    BOOL ok = mime_bn_from_bytes(bn, buffer, size);
    SecureZeroMemory(buffer, size);
    return ok;
}

static NTSTATUS import_rsa(KEY_PAIR *key, LPCWSTR type, PUCHAR input, ULONG size, ULONG flags)
{
    MimeRsa *rsa = key->rsa;
    const UCHAR *p;
    if (!wcscmp(type, LEGACY_RSAPUBLIC_BLOB) || !wcscmp(type, LEGACY_RSAPRIVATE_BLOB)) {
        const CAPI_RSA_HEADER *h = (const CAPI_RSA_HEADER *)input;
        BOOL private_key = !wcscmp(type, LEGACY_RSAPRIVATE_BLOB);
        if (size < sizeof(*h) || h->magic != (private_key ? CAPI_RSA2 : CAPI_RSA1) ||
            h->bType != (private_key ? CAPI_PRIVATEKEYBLOB : CAPI_PUBLICKEYBLOB))
            return NTE_BAD_DATA_STATUS;
        ULONG n = (h->bitlen + 7) / 8, half = (h->bitlen + 15) / 16;
        if (h->bitlen < 256 || h->bitlen > 16384)
            return NTE_NOT_SUPPORTED_STATUS;
        if (size < sizeof(*h) + n + (private_key ? 5 * half + n : 0))
            return NTE_BAD_DATA_STATUS;
        p = (const UCHAR *)(h + 1);
        mime_bn_set(&rsa->e, h->pubexp);
        BOOL ok = load_le(&rsa->n, &p, n);
        if (private_key)
            ok = ok && load_le(&rsa->p, &p, half) && load_le(&rsa->q, &p, half) && load_le(&rsa->dp, &p, half) &&
                 load_le(&rsa->dq, &p, half) && load_le(&rsa->qinv, &p, half) && load_le(&rsa->d, &p, n);
        if (!ok)
            return NTE_BAD_DATA_STATUS;
        rsa->private_key = private_key;
    } else {
        const BCRYPT_RSAKEY_BLOB *h = (const BCRYPT_RSAKEY_BLOB *)input;
        BOOL public_blob = !wcscmp(type, BCRYPT_RSAPUBLIC_BLOB) || !wcscmp(type, BCRYPT_PUBLIC_KEY_BLOB);
        BOOL full = !wcscmp(type, BCRYPT_RSAFULLPRIVATE_BLOB);
        BOOL private_blob = full || !wcscmp(type, BCRYPT_RSAPRIVATE_BLOB) || !wcscmp(type, BCRYPT_PRIVATE_KEY_BLOB);
        if (!public_blob && !private_blob)
            return STATUS_NOT_SUPPORTED;
        if (size < sizeof(*h))
            return NTE_BAD_DATA_STATUS;
        ULONG magic = public_blob ? BCRYPT_RSAPUBLIC_MAGIC : full ? BCRYPT_RSAFULLPRIVATE_MAGIC : BCRYPT_RSAPRIVATE_MAGIC;
        /* a private blob of the full kind imports as a private one too. */
        if (h->Magic != magic && !(private_blob && !full && h->Magic == BCRYPT_RSAFULLPRIVATE_MAGIC))
            return NTE_BAD_DATA_STATUS;
        if (h->BitLength < 256 || h->BitLength > 16384)
            return NTE_NOT_SUPPORTED_STATUS;
        if (h->cbPublicExp > 8 || h->cbModulus > 2048 || h->cbPrime1 > 1024 || h->cbPrime2 > 1024)
            return NTE_BAD_DATA_STATUS;
        ULONGLONG need = sizeof(*h) + (ULONGLONG)h->cbPublicExp + h->cbModulus;
        if (public_blob) {
            if (h->cbPrime1 || h->cbPrime2 || size != need)
                return NTE_BAD_DATA_STATUS;
        } else {
            need += h->cbPrime1 + h->cbPrime2;
            if (h->Magic == BCRYPT_RSAFULLPRIVATE_MAGIC)
                need += 2ull * h->cbPrime1 + h->cbPrime2 + h->cbModulus;
            if (size < need)
                return NTE_BAD_DATA_STATUS;
        }
        p = (const UCHAR *)(h + 1);
        BOOL ok = load(&rsa->e, &p, h->cbPublicExp) && load(&rsa->n, &p, h->cbModulus);
        if (private_blob) {
            ok = ok && load(&rsa->p, &p, h->cbPrime1) && load(&rsa->q, &p, h->cbPrime2);
            if (h->Magic == BCRYPT_RSAFULLPRIVATE_MAGIC)
                ok = ok && load(&rsa->dp, &p, h->cbPrime1) && load(&rsa->dq, &p, h->cbPrime2) &&
                     load(&rsa->qinv, &p, h->cbPrime1) && load(&rsa->d, &p, h->cbModulus);
        }
        if (!ok)
            return NTE_BAD_DATA_STATUS;
        rsa->private_key = private_blob;
    }
    if (mime_rsa_check(rsa))
        return STATUS_INVALID_PARAMETER;
    key->bits = rsa->bits;
    key->has_private = rsa->private_key;
    return STATUS_SUCCESS;
}

static NTSTATUS import_ec(KEY_PAIR *key, LPCWSTR type, PUCHAR input, ULONG size, ULONG flags)
{
    const ALG_INFO *info = key->alg->info;
    BOOL public_blob = !wcscmp(type, BCRYPT_ECCPUBLIC_BLOB) || !wcscmp(type, BCRYPT_PUBLIC_KEY_BLOB);
    BOOL private_blob = !wcscmp(type, BCRYPT_ECCPRIVATE_BLOB) || !wcscmp(type, BCRYPT_PRIVATE_KEY_BLOB);
    if (!public_blob && !private_blob)
        return STATUS_NOT_SUPPORTED;
    const BCRYPT_ECCKEY_BLOB *h = (const BCRYPT_ECCKEY_BLOB *)input;
    if (size < sizeof(*h))
        return STATUS_INVALID_PARAMETER;
    const MimeEcCurve *curve;
    BOOL private_magic;
    if (!ec_magic_parse(h->dwMagic, is_ecdsa(info), &curve, &private_magic) || private_magic != private_blob)
        return STATUS_INVALID_PARAMETER;
    if (key->alg->curve) {
        if (curve && curve != key->alg->curve)
            return STATUS_INVALID_PARAMETER;
        curve = key->alg->curve;
    }
    if (!curve)
        return STATUS_INVALID_PARAMETER;
    ULONG field = mime_ec_size(curve);
    if (h->cbKey != field || size < sizeof(*h) + (private_blob ? 3 : 2) * field)
        return STATUS_INVALID_PARAMETER;
    MimeEcKey *ec = key->ec;
    const UCHAR *p = (const UCHAR *)(h + 1);
    ec->curve = curve;
    memcpy(ec->x, p, field);
    memcpy(ec->y, p + field, field);
    if (private_blob) {
        memcpy(ec->d, p + 2 * field, field);
        ec->private_key = true;
        BOOL zero = TRUE;
        for (ULONG i = 0; i < 2 * field; i++)
            zero &= !p[i];
        /* a private blob may leave the public point out. */
        if (zero) {
            if (mime_ec_derive_public(ec))
                return STATUS_INVALID_PARAMETER;
        } else if (!(flags & BCRYPT_NO_KEY_VALIDATION)) {
            MimeEcKey check = *ec;
            if (mime_ec_derive_public(&check) || memcmp(check.x, ec->x, field) || memcmp(check.y, ec->y, field)) {
                SecureZeroMemory(&check, sizeof(check));
                return STATUS_INVALID_PARAMETER;
            }
            SecureZeroMemory(&check, sizeof(check));
        }
    } else if (mime_ec_check_public(ec)) {
        return STATUS_INVALID_PARAMETER;
    }
    key->curve = curve;
    key->bits = mime_ec_bits(curve);
    key->has_private = private_blob;
    return STATUS_SUCCESS;
}

static NTSTATUS import_dh(KEY_PAIR *key, LPCWSTR type, PUCHAR input, ULONG size)
{
    BOOL public_blob = !wcscmp(type, BCRYPT_DH_PUBLIC_BLOB) || !wcscmp(type, BCRYPT_PUBLIC_KEY_BLOB);
    BOOL private_blob = !wcscmp(type, BCRYPT_DH_PRIVATE_BLOB) || !wcscmp(type, BCRYPT_PRIVATE_KEY_BLOB);
    if (!public_blob && !private_blob)
        return STATUS_NOT_SUPPORTED;
    const BCRYPT_DH_KEY_BLOB *h = (const BCRYPT_DH_KEY_BLOB *)input;
    if (size < sizeof(*h) || h->dwMagic != (private_blob ? BCRYPT_DH_PRIVATE_MAGIC : BCRYPT_DH_PUBLIC_MAGIC))
        return STATUS_INVALID_PARAMETER;
    ULONG n = h->cbKey;
    if (n < 64 || n > 512 || size < sizeof(*h) + (private_blob ? 4 : 3) * n)
        return STATUS_INVALID_PARAMETER;
    MimeDh *dh = key->dh;
    const UCHAR *p = (const UCHAR *)(h + 1);
    if (!load(&dh->p, &p, n) || !load(&dh->g, &p, n) || !load(&dh->y, &p, n) ||
        (private_blob && !load(&dh->x, &p, n)))
        return STATUS_INVALID_PARAMETER;
    if (!(dh->p.d[0] & 1) || mime_bn_cmp_word(&dh->g, 2) < 0 || mime_bn_cmp(&dh->y, &dh->p) >= 0)
        return STATUS_INVALID_PARAMETER;
    dh->size = n;
    dh->private_key = private_blob;
    key->bits = 8 * n;
    key->has_private = private_blob;
    return STATUS_SUCCESS;
}

BCRYPTAPI NTSTATUS WINAPI BCryptImportKeyPair(BCRYPT_ALG_HANDLE hAlgorithm, BCRYPT_KEY_HANDLE hImportKey,
                                              LPCWSTR pszBlobType, BCRYPT_KEY_HANDLE *phKey, PUCHAR pbInput,
                                              ULONG cbInput, ULONG dwFlags)
{
    ALG *alg = BcAlg(hAlgorithm);
    if (!alg)
        return STATUS_INVALID_HANDLE;
    const ALG_INFO *info = alg->info;
    if (!pair_iface(info))
        return STATUS_NOT_SUPPORTED;
    if (!phKey || !pbInput || !pszBlobType || (dwFlags & ~BCRYPT_NO_KEY_VALIDATION))
        return STATUS_INVALID_PARAMETER;
    if (hImportKey)
        return STATUS_NOT_SUPPORTED;
    KEY_PAIR *key = new_pair(alg);
    if (!key)
        return STATUS_NO_MEMORY;
    NTSTATUS status;
    if (is_rsa(info))
        status = import_rsa(key, pszBlobType, pbInput, cbInput, dwFlags);
    else if (is_dh(info))
        status = import_dh(key, pszBlobType, pbInput, cbInput);
    else
        status = import_ec(key, pszBlobType, pbInput, cbInput, dwFlags);
    if (status) {
        BcPairDestroy(key);
        return status;
    }
    key->finalized = TRUE;
    BcAlgRef(alg);
    *phKey = key;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ export */

static ULONG bn_size(const MimeBn *bn)
{
    return (mime_bn_bits(bn) + 7) / 8;
}

static void store(PUCHAR *p, const MimeBn *bn, ULONG size)
{
    mime_bn_to_bytes(bn, *p, size);
    *p += size;
}

static void store_le(PUCHAR *p, const MimeBn *bn, ULONG size)
{
    mime_bn_to_bytes(bn, *p, size);
    for (ULONG i = 0; i < size / 2; i++) {
        UCHAR t = (*p)[i];
        (*p)[i] = (*p)[size - 1 - i];
        (*p)[size - 1 - i] = t;
    }
    *p += size;
}

static NTSTATUS export_blob(PUCHAR blob, ULONG size, PUCHAR output, ULONG capacity, ULONG *result)
{
    NTSTATUS status = BcOutput(blob, size, output, capacity, result);
    SecureZeroMemory(blob, size);
    BcFree(blob);
    return status;
}

static NTSTATUS export_rsa(KEY_PAIR *key, LPCWSTR type, PUCHAR output, ULONG capacity, ULONG *result)
{
    MimeRsa *rsa = key->rsa;
    ULONG n = bn_size(&rsa->n), e = bn_size(&rsa->e), p1 = bn_size(&rsa->p), p2 = bn_size(&rsa->q);
    BOOL legacy_public = !wcscmp(type, LEGACY_RSAPUBLIC_BLOB), legacy_private = !wcscmp(type, LEGACY_RSAPRIVATE_BLOB);
    BOOL public_blob = !wcscmp(type, BCRYPT_RSAPUBLIC_BLOB) || !wcscmp(type, BCRYPT_PUBLIC_KEY_BLOB);
    BOOL full = !wcscmp(type, BCRYPT_RSAFULLPRIVATE_BLOB);
    BOOL private_blob = full || !wcscmp(type, BCRYPT_RSAPRIVATE_BLOB) || !wcscmp(type, BCRYPT_PRIVATE_KEY_BLOB);
    if (!public_blob && !private_blob && !legacy_public && !legacy_private)
        return STATUS_NOT_SUPPORTED;
    if ((private_blob || legacy_private) && (!key->has_private || !p1))
        return STATUS_INVALID_PARAMETER;
    ULONG size;
    PUCHAR blob, p;
    if (legacy_public || legacy_private) {
        ULONG half = (rsa->bits + 15) / 16;
        n = (rsa->bits + 7) / 8;
        if (e > 4)
            return STATUS_INVALID_PARAMETER;
        size = sizeof(CAPI_RSA_HEADER) + n + (legacy_private ? 5 * half + n : 0);
        if (!(blob = BcAllocZero(size)))
            return STATUS_NO_MEMORY;
        CAPI_RSA_HEADER *h = (CAPI_RSA_HEADER *)blob;
        h->bType = legacy_private ? CAPI_PRIVATEKEYBLOB : CAPI_PUBLICKEYBLOB;
        h->bVersion = 2;
        h->aiKeyAlg = key->alg->info->id == ID_RSA_SIGN ? CALG_RSA_SIGN_ : CALG_RSA_KEYX_;
        h->magic = legacy_private ? CAPI_RSA2 : CAPI_RSA1;
        h->bitlen = rsa->bits;
        h->pubexp = rsa->e.n ? rsa->e.d[0] : 0;
        p = (PUCHAR)(h + 1);
        store_le(&p, &rsa->n, n);
        if (legacy_private) {
            store_le(&p, &rsa->p, half);
            store_le(&p, &rsa->q, half);
            store_le(&p, &rsa->dp, half);
            store_le(&p, &rsa->dq, half);
            store_le(&p, &rsa->qinv, half);
            store_le(&p, &rsa->d, n);
        }
        return export_blob(blob, size, output, capacity, result);
    }
    size = sizeof(BCRYPT_RSAKEY_BLOB) + e + n + (private_blob ? p1 + p2 : 0) + (full ? 2 * p1 + p2 + n : 0);
    if (!(blob = BcAllocZero(size)))
        return STATUS_NO_MEMORY;
    BCRYPT_RSAKEY_BLOB *h = (BCRYPT_RSAKEY_BLOB *)blob;
    h->Magic = public_blob ? BCRYPT_RSAPUBLIC_MAGIC : full ? BCRYPT_RSAFULLPRIVATE_MAGIC : BCRYPT_RSAPRIVATE_MAGIC;
    h->BitLength = rsa->bits;
    h->cbPublicExp = e;
    h->cbModulus = n;
    h->cbPrime1 = private_blob ? p1 : 0;
    h->cbPrime2 = private_blob ? p2 : 0;
    p = (PUCHAR)(h + 1);
    store(&p, &rsa->e, e);
    store(&p, &rsa->n, n);
    if (private_blob) {
        store(&p, &rsa->p, p1);
        store(&p, &rsa->q, p2);
    }
    if (full) {
        store(&p, &rsa->dp, p1);
        store(&p, &rsa->dq, p2);
        store(&p, &rsa->qinv, p1);
        store(&p, &rsa->d, n);
    }
    return export_blob(blob, size, output, capacity, result);
}

static NTSTATUS export_ec(KEY_PAIR *key, LPCWSTR type, PUCHAR output, ULONG capacity, ULONG *result)
{
    BOOL public_blob = !wcscmp(type, BCRYPT_ECCPUBLIC_BLOB) || !wcscmp(type, BCRYPT_PUBLIC_KEY_BLOB);
    BOOL private_blob = !wcscmp(type, BCRYPT_ECCPRIVATE_BLOB) || !wcscmp(type, BCRYPT_PRIVATE_KEY_BLOB);
    if (!public_blob && !private_blob)
        return STATUS_NOT_SUPPORTED;
    if (private_blob && !key->has_private)
        return STATUS_INVALID_PARAMETER;
    ULONG field = mime_ec_size(key->curve), size = sizeof(BCRYPT_ECCKEY_BLOB) + (private_blob ? 3 : 2) * field;
    PUCHAR blob = BcAlloc(size);
    if (!blob)
        return STATUS_NO_MEMORY;
    BCRYPT_ECCKEY_BLOB *h = (BCRYPT_ECCKEY_BLOB *)blob;
    h->dwMagic = ec_magic(key->curve, is_ecdsa(key->alg->info), private_blob);
    h->cbKey = field;
    PUCHAR p = (PUCHAR)(h + 1);
    memcpy(p, key->ec->x, field);
    memcpy(p + field, key->ec->y, field);
    if (private_blob)
        memcpy(p + 2 * field, key->ec->d, field);
    return export_blob(blob, size, output, capacity, result);
}

static NTSTATUS export_dh(KEY_PAIR *key, LPCWSTR type, PUCHAR output, ULONG capacity, ULONG *result)
{
    BOOL public_blob = !wcscmp(type, BCRYPT_DH_PUBLIC_BLOB) || !wcscmp(type, BCRYPT_PUBLIC_KEY_BLOB);
    BOOL private_blob = !wcscmp(type, BCRYPT_DH_PRIVATE_BLOB) || !wcscmp(type, BCRYPT_PRIVATE_KEY_BLOB);
    if (!public_blob && !private_blob)
        return STATUS_NOT_SUPPORTED;
    if (private_blob && !key->has_private)
        return STATUS_INVALID_PARAMETER;
    MimeDh *dh = key->dh;
    ULONG n = dh->size, size = sizeof(BCRYPT_DH_KEY_BLOB) + (private_blob ? 4 : 3) * n;
    PUCHAR blob = BcAlloc(size);
    if (!blob)
        return STATUS_NO_MEMORY;
    BCRYPT_DH_KEY_BLOB *h = (BCRYPT_DH_KEY_BLOB *)blob;
    h->dwMagic = private_blob ? BCRYPT_DH_PRIVATE_MAGIC : BCRYPT_DH_PUBLIC_MAGIC;
    h->cbKey = n;
    PUCHAR p = (PUCHAR)(h + 1);
    store(&p, &dh->p, n);
    store(&p, &dh->g, n);
    store(&p, &dh->y, n);
    if (private_blob)
        store(&p, &dh->x, n);
    return export_blob(blob, size, output, capacity, result);
}

NTSTATUS BcPairExport(KEY_PAIR *key, LPCWSTR type, PUCHAR output, ULONG capacity, ULONG *result)
{
    if (!key->finalized)
        return STATUS_INVALID_HANDLE;
    const ALG_INFO *info = key->alg->info;
    if (is_rsa(info))
        return export_rsa(key, type, output, capacity, result);
    if (is_dh(info))
        return export_dh(key, type, output, capacity, result);
    return export_ec(key, type, output, capacity, result);
}

/* ------------------------------------------------------------------ properties */

NTSTATUS BcPairProperty(KEY_PAIR *key, LPCWSTR property, PUCHAR output, ULONG capacity, ULONG *result)
{
    const ALG_INFO *info = key->alg->info;
    ULONG value;
    if (!wcscmp(property, BCRYPT_ALGORITHM_NAME))
        return BcOutput(info->name, (ULONG)(wcslen(info->name) + 1) * sizeof(WCHAR), output, capacity, result);
    if (!wcscmp(property, BCRYPT_KEY_LENGTH) || !wcscmp(property, BCRYPT_KEY_STRENGTH) ||
        !wcscmp(property, BCRYPT_PUBLIC_KEY_LENGTH)) {
        value = key->bits;
    } else if (is_rsa(info) && (!wcscmp(property, BCRYPT_BLOCK_LENGTH) || !wcscmp(property, BCRYPT_SIGNATURE_LENGTH))) {
        value = (key->bits + 7) / 8;
    } else if (is_ec(info) && !wcscmp(property, BCRYPT_SIGNATURE_LENGTH) && key->curve) {
        value = 2 * mime_ec_size(key->curve);
    } else if (is_ec(info) && (!wcscmp(property, BCRYPT_ECC_CURVE_NAME) || !wcscmp(property, BCRYPT_ECC_PARAMETERS))) {
        return BcEccCurveProperty(key->curve, property, output, capacity, result);
    } else if (is_dh(info) && !wcscmp(property, BCRYPT_DH_PARAMETERS)) {
        MimeDh *dh = key->dh;
        if (!dh->size)
            return STATUS_INVALID_PARAMETER;
        ULONG size = sizeof(BCRYPT_DH_PARAMETER_HEADER) + 2 * dh->size;
        *result = size;
        if (!output)
            return STATUS_SUCCESS;
        if (capacity < size)
            return STATUS_BUFFER_TOO_SMALL;
        BCRYPT_DH_PARAMETER_HEADER *h = (BCRYPT_DH_PARAMETER_HEADER *)output;
        h->cbLength = size;
        h->dwMagic = BCRYPT_DH_PARAMETERS_MAGIC;
        h->cbKeyLength = dh->size;
        PUCHAR p = (PUCHAR)(h + 1);
        store(&p, &dh->p, dh->size);
        store(&p, &dh->g, dh->size);
        return STATUS_SUCCESS;
    } else {
        return STATUS_NOT_SUPPORTED;
    }
    return BcOutput(&value, sizeof(value), output, capacity, result);
}

NTSTATUS BcPairSetProperty(KEY_PAIR *key, LPCWSTR property, PUCHAR input, ULONG size)
{
    const ALG_INFO *info = key->alg->info;
    if (is_dh(info) && !wcscmp(property, BCRYPT_DH_PARAMETERS)) {
        if (key->finalized)
            return STATUS_INVALID_HANDLE;
        const BCRYPT_DH_PARAMETER_HEADER *h = (const BCRYPT_DH_PARAMETER_HEADER *)input;
        if (size < sizeof(*h) || h->cbLength != size || h->dwMagic != BCRYPT_DH_PARAMETERS_MAGIC ||
            h->cbKeyLength != key->bits / 8 || size != sizeof(*h) + 2 * h->cbKeyLength)
            return STATUS_INVALID_PARAMETER;
        MimeDh *dh = key->dh;
        const UCHAR *p = (const UCHAR *)(h + 1);
        if (!load(&dh->p, &p, h->cbKeyLength) || !load(&dh->g, &p, h->cbKeyLength) || !(dh->p.d[0] & 1) ||
            mime_bn_cmp_word(&dh->g, 2) < 0 || mime_bn_cmp(&dh->g, &dh->p) >= 0)
            return STATUS_INVALID_PARAMETER;
        mime_bn_zero(&dh->q);
        dh->size = h->cbKeyLength;
        return STATUS_SUCCESS;
    }
    if (is_ec(info) && !wcscmp(property, BCRYPT_ECC_CURVE_NAME)) {
        if (key->finalized || key->alg->info->curve)
            return STATUS_INVALID_PARAMETER;
        const MimeEcCurve *curve = BcCurveByName((LPCWSTR)input);
        if (!curve)
            return STATUS_NOT_SUPPORTED;
        key->curve = curve;
        key->bits = mime_ec_bits(curve);
        return STATUS_SUCCESS;
    }
    return STATUS_NOT_SUPPORTED;
}

/* ------------------------------------------------------------------ rsa encryption */

NTSTATUS BcPairEncrypt(KEY_PAIR *key, PUCHAR input, ULONG size, void *padding, PUCHAR output, ULONG capacity,
                       ULONG *result, ULONG flags, BOOL encrypt)
{
    *result = 0;
    if (flags & ~7u)
        return STATUS_INVALID_PARAMETER;
    if (!key->finalized || (!encrypt && !key->has_private))
        return STATUS_INVALID_HANDLE;
    MimeRsa *rsa = key->rsa;
    ULONG k = (rsa->bits + 7) / 8;
    const BCRYPT_OAEP_PADDING_INFO *oaep = padding;
    const MimeHashAlg *hash = nullptr;
    if (flags & BCRYPT_PAD_OAEP) {
        if (!oaep || !oaep->pszAlgId || !(hash = BcHashByName(oaep->pszAlgId)))
            return STATUS_INVALID_PARAMETER;
    }
    BOOL raw = !flags || (flags & BCRYPT_PAD_NONE);
    if (encrypt) {
        *result = k;
        if (!output)
            return STATUS_SUCCESS;
        if (capacity < k)
            return STATUS_BUFFER_TOO_SMALL;
        if (!input)
            return STATUS_INVALID_PARAMETER;
        int status;
        if (raw) {
            if (size < k)
                return STATUS_INVALID_PARAMETER;
            status = mime_rsa_public(rsa, input, output);
        } else if (flags & BCRYPT_PAD_PKCS1) {
            status = mime_rsa_pkcs1_encrypt(rsa, input, size, output);
        } else {
            status = mime_rsa_oaep_encrypt(rsa, hash, oaep->pbLabel, oaep->cbLabel, input, size, output);
        }
        return BcStatus(status);
    }
    if (!input || size < k)
        return STATUS_INVALID_PARAMETER;
    if (raw) {
        *result = k;
        if (!output)
            return STATUS_SUCCESS;
        if (capacity < k)
            return STATUS_BUFFER_TOO_SMALL;
        return BcStatus(mime_rsa_private(rsa, input, output));
    }
    PUCHAR plain = BcAlloc(k);
    if (!plain)
        return STATUS_NO_MEMORY;
    size_t length = 0;
    int r = flags & BCRYPT_PAD_PKCS1 ? mime_rsa_pkcs1_decrypt(rsa, input, plain, &length)
                                     : mime_rsa_oaep_decrypt(rsa, hash, oaep->pbLabel, oaep->cbLabel, input, plain, &length);
    NTSTATUS status = r ? STATUS_INVALID_PARAMETER : BcOutput(plain, (ULONG)length, output, capacity, result);
    SecureZeroMemory(plain, k);
    BcFree(plain);
    return status;
}

/* ------------------------------------------------------------------ signatures */

/* the DigestInfo prefixes a pkcs #1 signature may carry for a hash: with null parameters, without, and none for md5. */
static ULONG digest_prefixes(const ALG_INFO *info, ULONG hash_size, UCHAR prefixes[3][32], ULONG sizes[3])
{
    UCHAR list[256];
    ULONG list_size;
    if (BcHashOidList(info, list, sizeof(list), &list_size))
        return 0;
    BCRYPT_OID_LIST *oids = (BCRYPT_OID_LIST *)list;
    ULONG count = 0;
    for (ULONG i = 0; i < oids->dwOIDCount && i < 3; i++) {
        BCRYPT_OID *oid = &oids->pOIDs[i];
        if (!oid->cbOID) {
            sizes[count++] = 0;
            continue;
        }
        UCHAR *p = prefixes[count];
        p[0] = 0x30;
        p[1] = (UCHAR)(4 + oid->cbOID + hash_size);
        p[2] = 0x30;
        p[3] = (UCHAR)oid->cbOID;
        memcpy(p + 4, oid->pbOID, oid->cbOID);
        p[4 + oid->cbOID] = 0x04;
        p[5 + oid->cbOID] = (UCHAR)hash_size;
        sizes[count++] = 6 + oid->cbOID;
    }
    return count;
}

BCRYPTAPI NTSTATUS WINAPI BCryptSignHash(BCRYPT_KEY_HANDLE hKey, VOID *pPaddingInfo, PUCHAR pbInput, ULONG cbInput,
                                         PUCHAR pbOutput, ULONG cbOutput, ULONG *pcbResult, ULONG dwFlags)
{
    if (!pcbResult)
        return STATUS_INVALID_PARAMETER;
    KEY_HEADER *header = BcKey(hKey);
    if (!header)
        return STATUS_INVALID_HANDLE;
    const ALG_INFO *info = header->alg->info;
    if (header->kind != KIND_PAIR || (!is_rsa(info) && !is_ecdsa(info)))
        return STATUS_NOT_SUPPORTED;
    KEY_PAIR *key = (KEY_PAIR *)header;
    if (is_ecdsa(info)) {
        if (!pbInput || !cbInput || pPaddingInfo || (dwFlags & ~0xAu))
            return STATUS_INVALID_PARAMETER;
        if (!key->finalized || !key->has_private)
            return STATUS_INVALID_HANDLE;
        ULONG size = 2 * mime_ec_size(key->curve);
        *pcbResult = size;
        if (!pbOutput)
            return STATUS_SUCCESS;
        if (cbOutput < size)
            return STATUS_BUFFER_TOO_SMALL;
        return BcStatus(mime_ecdsa_sign(key->ec, pbInput, cbInput, pbOutput));
    }
    if (dwFlags & ~(ULONG)(BCRYPT_PAD_PKCS1 | BCRYPT_PAD_PSS) || !(dwFlags & (BCRYPT_PAD_PKCS1 | BCRYPT_PAD_PSS)))
        return STATUS_INVALID_PARAMETER;
    if (!key->finalized || !key->has_private)
        return STATUS_INVALID_HANDLE;
    ULONG k = (key->bits + 7) / 8;
    *pcbResult = k;
    if (!pbOutput)
        return STATUS_SUCCESS;
    if (!pPaddingInfo || !pbInput || !cbInput)
        return STATUS_INVALID_PARAMETER;
    if (cbOutput < k)
        return STATUS_BUFFER_TOO_SMALL;
    if (dwFlags & BCRYPT_PAD_PKCS1) {
        const BCRYPT_PKCS1_PADDING_INFO *pkcs1 = pPaddingInfo;
        if (!pkcs1->pszAlgId)
            return BcStatus(mime_rsa_pkcs1_sign_prefix(key->rsa, nullptr, 0, pbInput, cbInput, pbOutput));
        const ALG_INFO *hash = BcFindAlg(pkcs1->pszAlgId);
        if (!hash || !hash->hash)
            return STATUS_NOT_SUPPORTED;
        if (cbInput != hash->hash->digest_size)
            return STATUS_INVALID_PARAMETER;
        UCHAR prefixes[3][32];
        ULONG sizes[3];
        if (!digest_prefixes(hash, cbInput, prefixes, sizes))
            return STATUS_NOT_SUPPORTED;
        return BcStatus(mime_rsa_pkcs1_sign_prefix(key->rsa, prefixes[0], sizes[0], pbInput, cbInput, pbOutput));
    }
    const BCRYPT_PSS_PADDING_INFO *pss = pPaddingInfo;
    const MimeHashAlg *hash = pss->pszAlgId ? BcHashByName(pss->pszAlgId) : nullptr;
    if (!hash || cbInput != hash->digest_size || pss->cbSalt > 0x4000)
        return STATUS_INVALID_PARAMETER;
    return BcStatus(mime_rsa_pss_sign(key->rsa, hash, pbInput, cbInput, pss->cbSalt, pbOutput));
}

BCRYPTAPI NTSTATUS WINAPI BCryptVerifySignature(BCRYPT_KEY_HANDLE hKey, VOID *pPaddingInfo, PUCHAR pbHash,
                                                ULONG cbHash, PUCHAR pbSignature, ULONG cbSignature, ULONG dwFlags)
{
    KEY_HEADER *header = BcKey(hKey);
    if (!header)
        return STATUS_INVALID_HANDLE;
    if (!pbSignature)
        return STATUS_INVALID_PARAMETER;
    const ALG_INFO *info = header->alg->info;
    if (header->kind != KIND_PAIR || (!is_rsa(info) && !is_ecdsa(info)))
        return STATUS_NOT_SUPPORTED;
    KEY_PAIR *key = (KEY_PAIR *)header;
    if (!key->finalized)
        return STATUS_INVALID_HANDLE;
    if (is_ecdsa(info)) {
        if (!pbHash || !cbHash || pPaddingInfo || (dwFlags & ~0xAu))
            return STATUS_INVALID_PARAMETER;
        if (cbSignature != 2 * mime_ec_size(key->curve))
            return STATUS_INVALID_PARAMETER;
        return mime_ecdsa_verify(key->ec, pbHash, cbHash, pbSignature, cbSignature) ? STATUS_INVALID_SIGNATURE
                                                                                     : STATUS_SUCCESS;
    }
    if ((dwFlags & ~0x1Au) || !(dwFlags & (BCRYPT_PAD_PKCS1 | BCRYPT_PAD_PSS)))
        return STATUS_INVALID_PARAMETER;
    if (!pPaddingInfo || !pbHash || !cbHash)
        return STATUS_INVALID_PARAMETER;
    ULONG k = (key->bits + 7) / 8;
    if (cbSignature < k)
        return STATUS_INVALID_PARAMETER;
    if (dwFlags & (BCRYPT_PAD_PKCS1 | BCRYPT_PAD_PKCS1_OPTIONAL_HASH_OID)) {
        if (cbSignature > k)
            return STATUS_INVALID_SIGNATURE;
        const BCRYPT_PKCS1_PADDING_INFO *pkcs1 = pPaddingInfo;
        UCHAR prefixes[3][32];
        ULONG sizes[3], count = 1;
        sizes[0] = 0;
        if (pkcs1->pszAlgId) {
            const ALG_INFO *hash = BcFindAlg(pkcs1->pszAlgId);
            if (!hash || !hash->hash)
                return STATUS_NOT_SUPPORTED;
            if (cbHash != hash->hash->digest_size)
                return STATUS_INVALID_PARAMETER;
            count = digest_prefixes(hash, cbHash, prefixes, sizes);
        }
        for (ULONG i = 0; i < count; i++)
            if (!mime_rsa_pkcs1_verify_prefix(key->rsa, sizes[i] ? prefixes[i] : nullptr, sizes[i], pbHash, cbHash,
                                              pbSignature, cbSignature))
                return STATUS_SUCCESS;
        if ((dwFlags & BCRYPT_PAD_PKCS1_OPTIONAL_HASH_OID) &&
            !mime_rsa_pkcs1_verify_prefix(key->rsa, nullptr, 0, pbHash, cbHash, pbSignature, cbSignature))
            return STATUS_SUCCESS;
        return STATUS_INVALID_SIGNATURE;
    }
    const BCRYPT_PSS_PADDING_INFO *pss = pPaddingInfo;
    const MimeHashAlg *hash = pss->pszAlgId ? BcHashByName(pss->pszAlgId) : nullptr;
    if (!hash || cbHash > 0x4000 || pss->cbSalt > 0x4000)
        return STATUS_INVALID_PARAMETER;
    return mime_rsa_pss_verify(key->rsa, hash, pbHash, cbHash, pss->cbSalt, pbSignature, cbSignature)
               ? STATUS_INVALID_SIGNATURE
               : STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ secret agreement */

static SECRET *secret_object(BCRYPT_SECRET_HANDLE handle)
{
    SECRET *secret = handle;
    return secret && !((ULONG_PTR)secret & 1) && secret->magic == SECRET_MAGIC ? secret : nullptr;
}

BCRYPTAPI NTSTATUS WINAPI BCryptSecretAgreement(BCRYPT_KEY_HANDLE hPrivKey, BCRYPT_KEY_HANDLE hPubKey,
                                                BCRYPT_SECRET_HANDLE *phAgreedSecret, ULONG dwFlags)
{
    KEY_HEADER *mine = BcKey(hPrivKey), *theirs = BcKey(hPubKey);
    if (!mine || !theirs)
        return STATUS_INVALID_HANDLE;
    if (!phAgreedSecret || dwFlags)
        return STATUS_INVALID_PARAMETER;
    if (mine->kind != KIND_PAIR || theirs->kind != KIND_PAIR ||
        mine->alg->info->iface != BCRYPT_SECRET_AGREEMENT_INTERFACE ||
        theirs->alg->info->iface != BCRYPT_SECRET_AGREEMENT_INTERFACE)
        return STATUS_NOT_SUPPORTED;
    KEY_PAIR *a = (KEY_PAIR *)mine, *b = (KEY_PAIR *)theirs;
    if (!a->finalized || !b->finalized || !a->has_private)
        return STATUS_INVALID_HANDLE;
    BOOL dh = is_dh(a->alg->info);
    if (dh != is_dh(b->alg->info) || (!dh && a->curve != b->curve) || (dh && mime_bn_cmp(&a->dh->p, &b->dh->p)))
        return STATUS_INVALID_PARAMETER;
    ULONG length = dh ? a->dh->size : mime_ec_size(a->curve);
    SECRET *secret = BcAllocZero(sizeof(SECRET) + length);
    if (!secret)
        return STATUS_NO_MEMORY;
    secret->size = sizeof(SECRET) + length;
    secret->magic = SECRET_MAGIC;
    secret->alg = a->alg;
    secret->length = length;
    int status = dh ? mime_dh_agree(a->dh, &b->dh->y, secret->value) : mime_ecdh(a->ec, b->ec, secret->value);
    if (status) {
        BcFree(secret);
        return STATUS_INVALID_PARAMETER;
    }
    BcAlgRef(a->alg);
    *phAgreedSecret = secret;
    return STATUS_SUCCESS;
}

BCRYPTAPI NTSTATUS WINAPI BCryptDestroySecret(BCRYPT_SECRET_HANDLE hSecret)
{
    SECRET *secret = secret_object(hSecret);
    if (!secret)
        return STATUS_INVALID_HANDLE;
    ALG *alg = secret->alg;
    SecureZeroMemory(secret, secret->size);
    BcFree(secret);
    BcAlgRelease(alg);
    return STATUS_SUCCESS;
}

const BCryptBuffer *BcParameter(const BCryptBufferDesc *list, ULONG type)
{
    for (ULONG i = 0; list && i < list->cBuffers; i++)
        if (list->pBuffers[i].BufferType == type)
            return &list->pBuffers[i];
    return nullptr;
}

/* a hash named by a parameter, the default when absent; the name must end in its buffer. */
static const MimeHashAlg *parameter_hash(const BCryptBufferDesc *list, const MimeHashAlg *fallback, BOOL *bad)
{
    const BCryptBuffer *b = BcParameter(list, KDF_HASH_ALGORITHM);
    *bad = FALSE;
    if (!b)
        return fallback;
    const WCHAR *name = b->pvBuffer;
    ULONG chars = b->cbBuffer / sizeof(WCHAR);
    if (!name || chars < 1 || name[chars - 1]) {
        *bad = TRUE;
        return nullptr;
    }
    const MimeHashAlg *hash = BcHashByName(name);
    *bad = !hash;
    return hash;
}

static void hash_buffers(const BCryptBufferDesc *list, ULONG type, void (*update)(void *, const void *, size_t),
                         void *ctx)
{
    for (ULONG i = 0; list && i < list->cBuffers; i++)
        if (list->pBuffers[i].BufferType == type)
            update(ctx, list->pBuffers[i].pvBuffer, list->pBuffers[i].cbBuffer);
}

typedef struct {
    MimeHashCtx ctx;
    const MimeHashAlg *alg;
} HASH_RUN;

static void hash_update(void *ctx, const void *data, size_t size)
{
    HASH_RUN *run = ctx;
    run->alg->update(&run->ctx, data, size);
}

static void hmac_update(void *ctx, const void *data, size_t size)
{
    mime_hmac_update(ctx, data, size);
}

BCRYPTAPI NTSTATUS WINAPI BCryptDeriveKey(BCRYPT_SECRET_HANDLE hSharedSecret, LPCWSTR pwszKDF,
                                          BCryptBufferDesc *pParameterList, PUCHAR pbDerivedKey, ULONG cbDerivedKey,
                                          ULONG *pcbResult, ULONG dwFlags)
{
    if (!hSharedSecret || !pwszKDF || !pcbResult || (dwFlags & ~1u))
        return STATUS_INVALID_PARAMETER;
    SECRET *secret = secret_object(hSharedSecret);
    if (!secret)
        return STATUS_INVALID_HANDLE;
    if (secret->used)
        return STATUS_INVALID_PARAMETER;
    const UCHAR *z = secret->value;
    ULONG length = secret->length;
    BOOL bad;
    UCHAR out[MIME_HASH_MAX_DIGEST * 2];
    if (!wcscmp(pwszKDF, BCRYPT_KDF_RAW_SECRET)) {
        /* the secret in little endian order, its most significant bytes when truncated. */
        ULONG n = length;
        if (pbDerivedKey && cbDerivedKey) {
            if (n > cbDerivedKey)
                n = cbDerivedKey;
            for (ULONG i = 0; i < n; i++)
                pbDerivedKey[n - 1 - i] = z[i];
        }
        *pcbResult = n;
        return STATUS_SUCCESS;
    }
    if (!wcscmp(pwszKDF, BCRYPT_KDF_HASH) || !wcscmp(pwszKDF, BCRYPT_KDF_HMAC)) {
        const MimeHashAlg *hash = parameter_hash(pParameterList, &mime_sha1, &bad);
        if (bad)
            return STATUS_INVALID_PARAMETER;
        ULONG size = hash->digest_size;
        if (!pbDerivedKey || !cbDerivedKey) {
            *pcbResult = size;
            return STATUS_SUCCESS;
        }
        if (!wcscmp(pwszKDF, BCRYPT_KDF_HASH)) {
            HASH_RUN run = {.alg = hash};
            hash->init(&run.ctx);
            hash_buffers(pParameterList, KDF_SECRET_PREPEND, hash_update, &run);
            hash->update(&run.ctx, z, length);
            hash_buffers(pParameterList, KDF_SECRET_APPEND, hash_update, &run);
            hash->final(&run.ctx, out);
        } else {
            const BCryptBuffer *key = BcParameter(pParameterList, KDF_HMAC_KEY);
            if ((dwFlags & KDF_USE_SECRET_AS_HMAC_KEY_FLAG) ? key != nullptr : key == nullptr)
                return STATUS_INVALID_PARAMETER;
            MimeHmac h;
            if (key)
                mime_hmac_init(&h, hash, key->pvBuffer, key->cbBuffer);
            else
                mime_hmac_init(&h, hash, z, length);
            hash_buffers(pParameterList, KDF_SECRET_PREPEND, hmac_update, &h);
            mime_hmac_update(&h, z, length);
            hash_buffers(pParameterList, KDF_SECRET_APPEND, hmac_update, &h);
            mime_hmac_final(&h, out);
            SecureZeroMemory(&h, sizeof(h));
        }
        if (size > cbDerivedKey)
            size = cbDerivedKey;
        memcpy(pbDerivedKey, out, size);
        *pcbResult = size;
        SecureZeroMemory(out, sizeof(out));
        return STATUS_SUCCESS;
    }
    if (!wcscmp(pwszKDF, BCRYPT_KDF_TLS_PRF)) {
        const BCryptBuffer *label = BcParameter(pParameterList, KDF_TLS_PRF_LABEL);
        const BCryptBuffer *seed = BcParameter(pParameterList, KDF_TLS_PRF_SEED);
        const BCryptBuffer *version = BcParameter(pParameterList, KDF_TLS_PRF_PROTOCOL);
        if (!label || !seed || seed->cbBuffer != 64)
            return STATUS_INVALID_PARAMETER;
        ULONG protocol = version && version->cbBuffer == sizeof(ULONG) ? *(ULONG *)version->pvBuffer : 0x303;
        const MimeHashAlg *hash = nullptr;
        if (protocol >= 0x303 || protocol == 0xFEFD) {
            hash = parameter_hash(pParameterList, &mime_sha256, &bad);
            if (bad)
                return STATUS_INVALID_PARAMETER;
        }
        if (!pbDerivedKey || !cbDerivedKey) {
            *pcbResult = 48;
            return STATUS_SUCCESS;
        }
        mime_tls_prf(hash, z, length, label->pvBuffer, label->cbBuffer, seed->pvBuffer, seed->cbBuffer, pbDerivedKey,
                     cbDerivedKey);
        *pcbResult = cbDerivedKey;
        return STATUS_SUCCESS;
    }
    if (!wcscmp(pwszKDF, BCRYPT_KDF_SP80056A_CONCAT)) {
        const MimeHashAlg *hash = parameter_hash(pParameterList, &mime_sha256, &bad);
        if (bad || !BcParameter(pParameterList, KDF_ALGORITHMID) || !BcParameter(pParameterList, KDF_PARTYUINFO) ||
            !BcParameter(pParameterList, KDF_PARTYVINFO))
            return STATUS_INVALID_PARAMETER;
        if (!pbDerivedKey || !cbDerivedKey) {
            *pcbResult = hash->digest_size;
            return STATUS_SUCCESS;
        }
        static const ULONG order[] = {KDF_ALGORITHMID, KDF_PARTYUINFO, KDF_PARTYVINFO, KDF_SUPPPUBINFO,
                                      KDF_SUPPPRIVINFO};
        for (ULONG counter = 1, done = 0; done < cbDerivedKey; counter++) {
            HASH_RUN run = {.alg = hash};
            UCHAR c[4] = {(UCHAR)(counter >> 24), (UCHAR)(counter >> 16), (UCHAR)(counter >> 8), (UCHAR)counter};
            hash->init(&run.ctx);
            hash->update(&run.ctx, c, 4);
            hash->update(&run.ctx, z, length);
            for (ULONG i = 0; i < RTL_NUMBER_OF(order); i++)
                hash_buffers(pParameterList, order[i], hash_update, &run);
            hash->final(&run.ctx, out);
            ULONG n = cbDerivedKey - done < hash->digest_size ? cbDerivedKey - done : hash->digest_size;
            memcpy(pbDerivedKey + done, out, n);
            done += n;
        }
        *pcbResult = cbDerivedKey;
        /* the concatenation kdf consumes the secret, as windows does. */
        secret->used = TRUE;
        SecureZeroMemory(out, sizeof(out));
        return STATUS_SUCCESS;
    }
    if (!wcscmp(pwszKDF, BCRYPT_KDF_HKDF)) {
        const MimeHashAlg *hash = parameter_hash(pParameterList, nullptr, &bad);
        if (!hash)
            return STATUS_INVALID_PARAMETER;
        if (!pbDerivedKey || !cbDerivedKey) {
            *pcbResult = hash->digest_size;
            return STATUS_SUCCESS;
        }
        const BCryptBuffer *salt = BcParameter(pParameterList, KDF_HKDF_SALT);
        const BCryptBuffer *info = BcParameter(pParameterList, KDF_HKDF_INFO);
        UCHAR prk[MIME_HASH_MAX_DIGEST];
        mime_hkdf_extract(hash, salt ? salt->pvBuffer : nullptr, salt ? salt->cbBuffer : 0, z, length, prk);
        int status = mime_hkdf_expand(hash, prk, hash->digest_size, info ? info->pvBuffer : nullptr,
                                      info ? info->cbBuffer : 0, pbDerivedKey, cbDerivedKey);
        SecureZeroMemory(prk, sizeof(prk));
        if (status)
            return STATUS_INVALID_PARAMETER;
        *pcbResult = cbDerivedKey;
        return STATUS_SUCCESS;
    }
    return STATUS_NOT_SUPPORTED;
}
