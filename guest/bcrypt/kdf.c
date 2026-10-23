/*
 * key derivation keys and BCryptKeyDerivation: pbkdf2, sp 800-108 counter mode, the CAPI derivation, hkdf, the tls
 * prfs and the sp 800-56a concatenation.
 */
#include "bcrypt_private.h"

#define KDF_MAX_SECRET 2048

NTSTATUS BcKdfKeyCreate(ALG *alg, BCRYPT_KEY_HANDLE *handle, PUCHAR object, ULONG object_size, PUCHAR secret,
                        ULONG secret_size)
{
    if (!secret && secret_size)
        return STATUS_INVALID_PARAMETER;
    ULONG pad = (ULONG)((((ULONG_PTR)object + 15) & ~(ULONG_PTR)15) - (ULONG_PTR)object);
    if (object_size - pad < 32 + alg->info->object_length || object_size - pad < sizeof(KDF_KEY))
        return STATUS_BUFFER_TOO_SMALL;
    if (secret_size > KDF_MAX_SECRET)
        return STATUS_INVALID_PARAMETER;
    KDF_KEY *key = (KDF_KEY *)(object + pad);
    memset(key, 0, sizeof(*key));
    key->secret = BcAlloc(secret_size ? secret_size : 1);
    if (!key->secret)
        return STATUS_NO_MEMORY;
    memcpy(key->secret, secret, secret_size);
    key->size = object_size - pad;
    key->magic = KEY_MAGIC;
    key->alg = alg;
    key->kind = KIND_KDF;
    key->secret_size = secret_size;
    BcAlgRef(alg);
    *handle = key;
    return STATUS_SUCCESS;
}

void BcKdfDestroy(KDF_KEY *key)
{
    if (key->secret) {
        SecureZeroMemory(key->secret, key->secret_size);
        BcFree(key->secret);
    }
    SecureZeroMemory(key, sizeof(*key));
}

NTSTATUS BcKdfProperty(KDF_KEY *key, LPCWSTR property, PUCHAR output, ULONG capacity, ULONG *result)
{
    const ALG_INFO *info = key->alg->info;
    if (!wcscmp(property, BCRYPT_ALGORITHM_NAME))
        return BcOutput(info->name, (ULONG)(wcslen(info->name) + 1) * sizeof(WCHAR), output, capacity, result);
    if (!wcscmp(property, BCRYPT_KEY_LENGTH) || !wcscmp(property, BCRYPT_KEY_STRENGTH)) {
        ULONG bits = 8 * key->secret_size;
        return BcOutput(&bits, sizeof(bits), output, capacity, result);
    }
    return STATUS_NOT_SUPPORTED;
}

/* hkdf takes its hash, then either a salt to extract with or the extracted key itself. */
NTSTATUS BcKdfSetProperty(KDF_KEY *key, LPCWSTR property, PUCHAR input, ULONG size)
{
    if (key->alg->info->id != ID_HKDF)
        return STATUS_NOT_SUPPORTED;
    if (!wcscmp(property, BCRYPT_HKDF_HASH_ALGORITHM)) {
        const WCHAR *name = (const WCHAR *)input;
        if (size < sizeof(WCHAR) || name[size / sizeof(WCHAR) - 1] || key->hkdf_ready)
            return STATUS_INVALID_PARAMETER;
        const MimeHashAlg *hash = BcHashByName(name);
        if (!hash)
            return STATUS_NOT_SUPPORTED;
        key->hkdf_hash = hash;
        return STATUS_SUCCESS;
    }
    BOOL salt = !wcscmp(property, BCRYPT_HKDF_SALT_AND_FINALIZE);
    if (!salt && wcscmp(property, BCRYPT_HKDF_PRK_AND_FINALIZE))
        return STATUS_NOT_SUPPORTED;
    if (!key->hkdf_hash || key->hkdf_ready)
        return STATUS_INVALID_PARAMETER;
    if (salt) {
        mime_hkdf_extract(key->hkdf_hash, input, size, key->secret, key->secret_size, key->prk);
    } else {
        if (key->secret_size != key->hkdf_hash->digest_size)
            return STATUS_INVALID_PARAMETER;
        memcpy(key->prk, key->secret, key->secret_size);
    }
    key->hkdf_ready = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS BcKdfExport(KDF_KEY *key, LPCWSTR type, PUCHAR output, ULONG capacity, ULONG *result)
{
    if (wcscmp(type, BCRYPT_KEY_DATA_BLOB))
        return STATUS_NOT_SUPPORTED;
    ULONG size = sizeof(BCRYPT_KEY_DATA_BLOB_HEADER) + key->secret_size;
    *result = size;
    if (!output)
        return STATUS_SUCCESS;
    if (capacity < size)
        return STATUS_BUFFER_TOO_SMALL;
    BCRYPT_KEY_DATA_BLOB_HEADER *h = (BCRYPT_KEY_DATA_BLOB_HEADER *)output;
    h->dwMagic = BCRYPT_KEY_DATA_BLOB_MAGIC;
    h->dwVersion = BCRYPT_KEY_DATA_BLOB_VERSION1;
    h->cbKeyData = key->secret_size;
    memcpy(h + 1, key->secret, key->secret_size);
    return STATUS_SUCCESS;
}

static const MimeHashAlg *named_hash(const BCryptBufferDesc *list, const MimeHashAlg *fallback, BOOL *bad)
{
    const BCryptBuffer *b = BcParameter(list, KDF_HASH_ALGORITHM);
    *bad = FALSE;
    if (!b)
        return fallback;
    const WCHAR *name = b->pvBuffer;
    ULONG chars = b->cbBuffer / sizeof(WCHAR);
    const MimeHashAlg *hash = name && chars && !name[chars - 1] ? BcHashByName(name) : nullptr;
    *bad = !hash;
    return hash;
}

static const void *buffer_data(const BCryptBuffer *b)
{
    return b ? b->pvBuffer : nullptr;
}

static ULONG buffer_size(const BCryptBuffer *b)
{
    return b ? b->cbBuffer : 0;
}

BCRYPTAPI NTSTATUS WINAPI BCryptKeyDerivation(BCRYPT_KEY_HANDLE hKey, BCryptBufferDesc *pParameterList,
                                              PUCHAR pbDerivedKey, ULONG cbDerivedKey, ULONG *pcbResult,
                                              ULONG dwFlags)
{
    if (!pcbResult || !pbDerivedKey || !cbDerivedKey)
        return STATUS_INVALID_PARAMETER;
    KEY_HEADER *header = BcKey(hKey);
    if (!header)
        return STATUS_INVALID_HANDLE;
    if (header->kind != KIND_KDF)
        return STATUS_NOT_SUPPORTED;
    if (dwFlags)
        return STATUS_INVALID_PARAMETER;
    KDF_KEY *key = (KDF_KEY *)header;
    BOOL bad;
    const BCryptBufferDesc *list = pParameterList;
    switch (key->alg->info->id) {
    case ID_PBKDF2: {
        const MimeHashAlg *hash = named_hash(list, nullptr, &bad);
        const BCryptBuffer *salt = BcParameter(list, KDF_SALT), *count = BcParameter(list, KDF_ITERATION_COUNT);
        if (!hash || !count || count->cbBuffer != sizeof(ULONGLONG) || !*(ULONGLONG *)count->pvBuffer)
            return STATUS_INVALID_PARAMETER;
        mime_pbkdf2(hash, key->secret, key->secret_size, buffer_data(salt), buffer_size(salt),
                    *(ULONGLONG *)count->pvBuffer, pbDerivedKey, cbDerivedKey);
        break;
    }
    case ID_SP800108: {
        const MimeHashAlg *hash = named_hash(list, nullptr, &bad);
        if (!hash)
            return STATUS_INVALID_PARAMETER;
        const BCryptBuffer *label = BcParameter(list, KDF_LABEL), *context = BcParameter(list, KDF_CONTEXT);
        mime_sp800108_hmac(hash, key->secret, key->secret_size, buffer_data(label), buffer_size(label),
                           buffer_data(context), buffer_size(context), pbDerivedKey, cbDerivedKey);
        break;
    }
    case ID_CAPI_KDF: {
        const MimeHashAlg *hash = named_hash(list, nullptr, &bad);
        if (!hash || cbDerivedKey > 2 * hash->digest_size)
            return STATUS_INVALID_PARAMETER;
        UCHAR value[MIME_HASH_MAX_DIGEST];
        mime_hash(hash, key->secret, key->secret_size, value);
        mime_capi_expand(hash, value, pbDerivedKey, cbDerivedKey);
        SecureZeroMemory(value, sizeof(value));
        break;
    }
    case ID_HKDF: {
        if (!key->hkdf_ready)
            return STATUS_INVALID_PARAMETER;
        const BCryptBuffer *info = BcParameter(list, KDF_HKDF_INFO);
        if (mime_hkdf_expand(key->hkdf_hash, key->prk, key->hkdf_hash->digest_size, buffer_data(info),
                             buffer_size(info), pbDerivedKey, cbDerivedKey))
            return STATUS_INVALID_PARAMETER;
        break;
    }
    case ID_TLS11_KDF:
    case ID_TLS12_KDF: {
        const BCryptBuffer *label = BcParameter(list, KDF_TLS_PRF_LABEL), *seed = BcParameter(list, KDF_TLS_PRF_SEED);
        const MimeHashAlg *hash = nullptr;
        if (key->alg->info->id == ID_TLS12_KDF) {
            hash = named_hash(list, &mime_sha256, &bad);
            if (bad)
                return STATUS_INVALID_PARAMETER;
        }
        if (!label || !seed)
            return STATUS_INVALID_PARAMETER;
        mime_tls_prf(hash, key->secret, key->secret_size, label->pvBuffer, label->cbBuffer, seed->pvBuffer,
                     seed->cbBuffer, pbDerivedKey, cbDerivedKey);
        break;
    }
    case ID_SP80056A: {
        const MimeHashAlg *hash = named_hash(list, &mime_sha256, &bad);
        if (bad || !BcParameter(list, KDF_ALGORITHMID) || !BcParameter(list, KDF_PARTYUINFO) ||
            !BcParameter(list, KDF_PARTYVINFO))
            return STATUS_INVALID_PARAMETER;
        static const ULONG order[] = {KDF_ALGORITHMID, KDF_PARTYUINFO, KDF_PARTYVINFO, KDF_SUPPPUBINFO,
                                      KDF_SUPPPRIVINFO};
        UCHAR block[MIME_HASH_MAX_DIGEST];
        for (ULONG counter = 1, done = 0; done < cbDerivedKey; counter++) {
            MimeHashCtx ctx;
            UCHAR c[4] = {(UCHAR)(counter >> 24), (UCHAR)(counter >> 16), (UCHAR)(counter >> 8), (UCHAR)counter};
            hash->init(&ctx);
            hash->update(&ctx, c, 4);
            hash->update(&ctx, key->secret, key->secret_size);
            for (ULONG i = 0; i < RTL_NUMBER_OF(order); i++)
                for (ULONG j = 0; list && j < list->cBuffers; j++)
                    if (list->pBuffers[j].BufferType == order[i])
                        hash->update(&ctx, list->pBuffers[j].pvBuffer, list->pBuffers[j].cbBuffer);
            hash->final(&ctx, block);
            ULONG n = cbDerivedKey - done < hash->digest_size ? cbDerivedKey - done : hash->digest_size;
            memcpy(pbDerivedKey + done, block, n);
            done += n;
        }
        SecureZeroMemory(block, sizeof(block));
        break;
    }
    default:
        return STATUS_NOT_SUPPORTED;
    }
    *pcbResult = cbDerivedKey;
    return STATUS_SUCCESS;
}
