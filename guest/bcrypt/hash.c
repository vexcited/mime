/*
 * hash, hmac, cmac and gmac objects, BCryptHash, pbkdf2 and the CAPI key derivation. like on windows, a hash lives
 * in the caller's object buffer from its first 16 byte boundary: a header, then the state.
 */
#include "bcrypt_private.h"

#define HASH_MIN_OBJECT 70
/* size of windows' hash header before the provider's state, on x64. */
#define HASH_HEADER 40

/* the encodings a pkcs #1 DigestInfo may carry. */
static const UCHAR oid_md4[] = {0x06, 0x08, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x02, 0x04, 0x05, 0x00};
static const UCHAR oid_md5[] = {0x06, 0x08, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x02, 0x05, 0x05, 0x00};
static const UCHAR oid_sha1[] = {0x06, 0x05, 0x2b, 0x0e, 0x03, 0x02, 0x1a, 0x05, 0x00};
static const UCHAR oid_sha256[] = {0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00};
static const UCHAR oid_sha384[] = {0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x02, 0x05, 0x00};
static const UCHAR oid_sha512[] = {0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03, 0x05, 0x00};

/* each hash has its oid with and without null parameters; md5 also has a bare entry. */
static ULONG oid_list(ULONG id, const UCHAR **oid, ULONG *size)
{
    switch (id) {
    case ID_MD4:
        *oid = oid_md4;
        *size = sizeof(oid_md4);
        return 2;
    case ID_MD5:
        *oid = oid_md5;
        *size = sizeof(oid_md5);
        return 3;
    case ID_SHA1:
        *oid = oid_sha1;
        *size = sizeof(oid_sha1);
        return 2;
    case ID_SHA256:
        *oid = oid_sha256;
        *size = sizeof(oid_sha256);
        return 2;
    case ID_SHA384:
        *oid = oid_sha384;
        *size = sizeof(oid_sha384);
        return 2;
    case ID_SHA512:
        *oid = oid_sha512;
        *size = sizeof(oid_sha512);
        return 2;
    }
    return 0;
}

NTSTATUS BcHashOidList(const ALG_INFO *info, PUCHAR output, ULONG capacity, ULONG *result)
{
    const UCHAR *oid = nullptr;
    ULONG oid_size = 0, count = oid_list(info->id, &oid, &oid_size);
    ULONG lengths[3] = {oid_size, oid_size - 2, 0};
    ULONG size = (ULONG)(sizeof(BCRYPT_OID_LIST) + count * sizeof(BCRYPT_OID));
    for (ULONG i = 0; i < count; i++)
        size += lengths[i];
    *result = size;
    if (!output)
        return STATUS_SUCCESS;
    if (capacity < size)
        return STATUS_BUFFER_TOO_SMALL;
    BCRYPT_OID_LIST *list = (BCRYPT_OID_LIST *)output;
    BCRYPT_OID *entries = (BCRYPT_OID *)(list + 1);
    PUCHAR data = (PUCHAR)(entries + count);
    list->dwOIDCount = count;
    list->pOIDs = entries;
    for (ULONG i = 0; i < count; i++) {
        entries[i].cbOID = lengths[i];
        entries[i].pbOID = lengths[i] ? data : nullptr;
        memcpy(data, oid, lengths[i]);
        data += lengths[i];
    }
    return STATUS_SUCCESS;
}

HASH *BcHash(BCRYPT_HASH_HANDLE handle)
{
    HASH *hash = handle;
    if (!hash || ((ULONG_PTR)hash & 1) || hash->magic != HASH_MAGIC || hash->size < sizeof(HASH))
        return nullptr;
    return hash;
}

static BOOL is_hmac(const ALG *alg)
{
    return (alg->flags & BCRYPT_ALG_HANDLE_HMAC_FLAG) && alg->info->hash;
}

static ULONG state_size(const ALG *alg)
{
    if (alg->info->id == ID_AES_CMAC)
        return sizeof(MimeCmac);
    if (alg->info->id == ID_AES_GMAC)
        return sizeof(GMAC);
    return (ULONG)(is_hmac(alg) ? mime_hmac_size(alg->info->hash) : mime_hash_ctx_size(alg->info->hash));
}

static ULONG digest_size(const ALG *alg)
{
    return alg->info->hash ? alg->info->hash->digest_size : 16;
}

static NTSTATUS start(HASH *hash, PUCHAR secret, ULONG secret_size)
{
    ALG *alg = hash->alg;
    void *state = hash->state;
    switch (alg->info->id) {
    case ID_AES_CMAC:
        return mime_cmac_init(state, secret, secret_size) ? STATUS_INVALID_BUFFER_SIZE : STATUS_SUCCESS;
    case ID_AES_GMAC: {
        GMAC *g = state;
        memset(g, 0, sizeof(*g));
        if (mime_aes_init(&g->aes, secret, secret_size))
            return STATUS_INVALID_BUFFER_SIZE;
        g->iv_size = 12;
        return STATUS_SUCCESS;
    }
    }
    if (is_hmac(alg))
        mime_hmac_init(state, alg->info->hash, secret, secret_size);
    else
        alg->info->hash->init(state);
    return STATUS_SUCCESS;
}

/* whole blocks are authenticated as they come, the tail on finish. */
static void gmac_update(GMAC *g, const UCHAR *data, ULONG size)
{
    if (!g->started) {
        mime_gcm_init(&g->gcm, &g->aes, g->iv, g->iv_size);
        g->tail_used = 0;
        g->started = TRUE;
    }
    while (size) {
        ULONG n = 16 - g->tail_used < size ? 16 - g->tail_used : size;
        memcpy(g->tail + g->tail_used, data, n);
        g->tail_used += n;
        data += n;
        size -= n;
        if (g->tail_used == 16) {
            mime_gcm_aad(&g->gcm, g->tail, 16);
            g->tail_used = 0;
        }
    }
}

static void gmac_final(GMAC *g, UCHAR *tag)
{
    gmac_update(g, nullptr, 0);
    if (g->tail_used)
        mime_gcm_aad(&g->gcm, g->tail, g->tail_used);
    mime_gcm_tag(&g->gcm, &g->aes, tag, 16);
    g->started = FALSE;
}

static NTSTATUS place(ALG *alg, PUCHAR object, ULONG size, HASH **result)
{
    if (size < HASH_MIN_OBJECT)
        return STATUS_BUFFER_TOO_SMALL;
    ULONG pad = (ULONG)((((ULONG_PTR)object + 15) & ~(ULONG_PTR)15) - (ULONG_PTR)object);
    ULONG provider = is_hmac(alg) ? alg->info->hmac_object_length : alg->info->object_length;
    ULONG need = state_size(alg);
    if (size - pad < HASH_HEADER + provider || size - pad < sizeof(HASH) + need)
        return STATUS_BUFFER_TOO_SMALL;
    HASH *hash = (HASH *)(object + pad);
    memset(hash, 0, sizeof(HASH));
    hash->size = size - pad;
    hash->magic = HASH_MAGIC;
    hash->alg = alg;
    hash->state_size = need;
    *result = hash;
    return STATUS_SUCCESS;
}

BCRYPTAPI NTSTATUS WINAPI BCryptCreateHash(BCRYPT_ALG_HANDLE hAlgorithm, BCRYPT_HASH_HANDLE *phHash,
                                           PUCHAR pbHashObject, ULONG cbHashObject, PUCHAR pbSecret, ULONG cbSecret,
                                           ULONG dwFlags)
{
    ALG *alg = BcAlg(hAlgorithm);
    if (!alg)
        return STATUS_INVALID_HANDLE;
    void *heap = nullptr;
    if (!pbHashObject) {
        if (cbHashObject)
            return STATUS_INVALID_PARAMETER;
        if (alg->info->iface != BCRYPT_HASH_INTERFACE)
            return STATUS_NOT_SUPPORTED;
        cbHashObject = BcObjectLength(alg);
        pbHashObject = heap = BcAlloc(cbHashObject);
        if (!heap)
            return STATUS_NO_MEMORY;
    }
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    HASH *hash = nullptr;
    if (!phHash)
        goto failed;
    status = cbHashObject < HASH_MIN_OBJECT ? STATUS_BUFFER_TOO_SMALL : STATUS_SUCCESS;
    if (!status && alg->info->iface != BCRYPT_HASH_INTERFACE)
        status = STATUS_NOT_SUPPORTED;
    if (!status && (dwFlags & ~BCRYPT_HASH_REUSABLE_FLAG))
        status = STATUS_INVALID_PARAMETER;
    if (!status && !is_hmac(alg) && alg->info->hash && (pbSecret || cbSecret))
        status = STATUS_INVALID_PARAMETER;
    if (!status)
        status = place(alg, pbHashObject, cbHashObject, &hash);
    if (!status) {
        hash->heap = heap;
        hash->reusable = (dwFlags | alg->flags) & BCRYPT_HASH_REUSABLE_FLAG ? TRUE : FALSE;
        status = start(hash, pbSecret, cbSecret);
        if (status)
            hash->magic = 0;
    }
    if (status)
        goto failed;
    BcAlgRef(alg);
    *phHash = hash;
    return STATUS_SUCCESS;
failed:
    BcFree(heap);
    return status;
}

BCRYPTAPI NTSTATUS WINAPI BCryptHashData(BCRYPT_HASH_HANDLE hHash, PUCHAR pbInput, ULONG cbInput, ULONG dwFlags)
{
    HASH *hash = BcHash(hHash);
    if (!hash)
        return STATUS_INVALID_HANDLE;
    if (dwFlags & ~4u)
        return STATUS_INVALID_PARAMETER;
    if (hash->finished)
        return STATUS_INVALID_HANDLE;
    if (dwFlags)
        return STATUS_INVALID_PARAMETER;
    if (!cbInput)
        return STATUS_SUCCESS;
    if (!pbInput)
        return STATUS_INVALID_PARAMETER;
    ALG *alg = hash->alg;
    void *state = hash->state;
    switch (alg->info->id) {
    case ID_AES_CMAC:
        mime_cmac_update(state, pbInput, cbInput);
        break;
    case ID_AES_GMAC:
        gmac_update(state, pbInput, cbInput);
        break;
    default:
        if (is_hmac(alg))
            mime_hmac_update(state, pbInput, cbInput);
        else
            alg->info->hash->update(state, pbInput, cbInput);
    }
    return STATUS_SUCCESS;
}

BCRYPTAPI NTSTATUS WINAPI BCryptFinishHash(BCRYPT_HASH_HANDLE hHash, PUCHAR pbOutput, ULONG cbOutput, ULONG dwFlags)
{
    HASH *hash = BcHash(hHash);
    if (!hash)
        return STATUS_INVALID_HANDLE;
    if (dwFlags & ~1u)
        return STATUS_INVALID_PARAMETER;
    if (hash->finished)
        return STATUS_INVALID_HANDLE;
    ALG *alg = hash->alg;
    if (dwFlags || !pbOutput || cbOutput != digest_size(alg))
        return STATUS_INVALID_PARAMETER;
    void *state = hash->state;
    switch (alg->info->id) {
    case ID_AES_CMAC:
        mime_cmac_final(state, pbOutput);
        break;
    case ID_AES_GMAC:
        gmac_final(state, pbOutput);
        break;
    default:
        if (is_hmac(alg)) {
            mime_hmac_final(state, pbOutput);
        } else {
            alg->info->hash->final(state, pbOutput);
            alg->info->hash->init(state);
        }
    }
    if (!hash->reusable)
        hash->finished = TRUE;
    return STATUS_SUCCESS;
}

BCRYPTAPI NTSTATUS WINAPI BCryptDuplicateHash(BCRYPT_HASH_HANDLE hHash, BCRYPT_HASH_HANDLE *phNewHash,
                                              PUCHAR pbHashObject, ULONG cbHashObject, ULONG dwFlags)
{
    HASH *hash = BcHash(hHash);
    if (!hash)
        return STATUS_INVALID_HANDLE;
    ALG *alg = hash->alg;
    void *heap = nullptr;
    if (!pbHashObject) {
        if (cbHashObject)
            return STATUS_INVALID_PARAMETER;
        cbHashObject = BcObjectLength(alg);
        pbHashObject = heap = BcAlloc(cbHashObject);
        if (!heap)
            return STATUS_NO_MEMORY;
    }
    NTSTATUS status = !phNewHash ? STATUS_INVALID_PARAMETER : dwFlags ? STATUS_INVALID_PARAMETER : STATUS_SUCCESS;
    HASH *copy = nullptr;
    if (!status)
        status = place(alg, pbHashObject, cbHashObject, &copy);
    if (status) {
        BcFree(heap);
        return status;
    }
    copy->heap = heap;
    copy->reusable = hash->reusable;
    copy->finished = hash->finished;
    memcpy(copy->state, hash->state, hash->state_size);
    BcAlgRef(alg);
    *phNewHash = copy;
    return STATUS_SUCCESS;
}

BCRYPTAPI NTSTATUS WINAPI BCryptDestroyHash(BCRYPT_HASH_HANDLE hHash)
{
    HASH *hash = BcHash(hHash);
    if (!hash)
        return STATUS_INVALID_PARAMETER;
    ALG *alg = hash->alg;
    void *heap = hash->heap;
    SecureZeroMemory(hash, sizeof(HASH) + hash->state_size);
    BcFree(heap);
    BcAlgRelease(alg);
    return STATUS_SUCCESS;
}

BCRYPTAPI NTSTATUS WINAPI BCryptHash(BCRYPT_ALG_HANDLE hAlgorithm, PUCHAR pbSecret, ULONG cbSecret, PUCHAR pbInput,
                                     ULONG cbInput, PUCHAR pbOutput, ULONG cbOutput)
{
    BCRYPT_HASH_HANDLE hash = nullptr;
    NTSTATUS status = BCryptCreateHash(hAlgorithm, &hash, nullptr, 0, pbSecret, cbSecret, 0);
    if (!status)
        status = BCryptHashData(hash, pbInput, cbInput, 0);
    if (!status)
        status = BCryptFinishHash(hash, pbOutput, cbOutput, 0);
    if (hash)
        BCryptDestroyHash(hash);
    return status;
}

NTSTATUS BcHashProperty(HASH *hash, LPCWSTR property, PUCHAR output, ULONG capacity, ULONG *result)
{
    ALG *alg = hash->alg;
    ULONG value;
    if (!wcscmp(property, BCRYPT_ALGORITHM_NAME))
        return BcOutput(alg->info->name, (ULONG)(wcslen(alg->info->name) + 1) * sizeof(WCHAR), output, capacity,
                        result);
    if (!wcscmp(property, BCRYPT_HASH_LENGTH))
        value = digest_size(alg);
    else if (!wcscmp(property, BCRYPT_IS_KEYED_HASH))
        value = is_hmac(alg) || !alg->info->hash;
    else if (!wcscmp(property, BCRYPT_IS_REUSABLE_HASH))
        value = hash->reusable;
    else if (!wcscmp(property, BCRYPT_HASH_OID_LIST))
        return BcHashOidList(alg->info, output, capacity, result);
    else
        return STATUS_NOT_SUPPORTED;
    return BcOutput(&value, sizeof(value), output, capacity, result);
}

/* only the gmac nonce is settable, before or between messages. */
NTSTATUS BcHashSetProperty(HASH *hash, LPCWSTR property, PUCHAR input, ULONG size)
{
    if (!input)
        return STATUS_INVALID_PARAMETER;
    if (hash->alg->info->id != ID_AES_GMAC || wcscmp(property, BCRYPT_INITIALIZATION_VECTOR))
        return STATUS_NOT_SUPPORTED;
    GMAC *g = (GMAC *)hash->state;
    if (!size || size > sizeof(g->iv))
        return STATUS_INVALID_PARAMETER;
    memcpy(g->iv, input, size);
    g->iv_size = size;
    g->started = FALSE;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ derivations over hashes */

BCRYPTAPI NTSTATUS WINAPI BCryptDeriveKeyPBKDF2(BCRYPT_ALG_HANDLE hPrf, PUCHAR pbPassword, ULONG cbPassword,
                                                PUCHAR pbSalt, ULONG cbSalt, ULONGLONG cIterations,
                                                PUCHAR pbDerivedKey, ULONG cbDerivedKey, ULONG dwFlags)
{
    if (!cIterations || dwFlags || (!pbPassword && cbPassword) || (!pbSalt && cbSalt) || !pbDerivedKey ||
        !cbDerivedKey)
        return STATUS_INVALID_PARAMETER;
    ALG *alg = BcAlg(hPrf);
    if (!alg || alg->info->iface != BCRYPT_HASH_INTERFACE || !is_hmac(alg))
        return STATUS_INVALID_HANDLE;
    mime_pbkdf2(alg->info->hash, pbPassword, cbPassword, pbSalt, cbSalt, cIterations, pbDerivedKey, cbDerivedKey);
    return STATUS_SUCCESS;
}

/* CryptDeriveKey's expansion of a finished hash value, for keys longer than the digest. */
BCRYPTAPI NTSTATUS WINAPI BCryptDeriveKeyCapi(BCRYPT_HASH_HANDLE hHash, BCRYPT_ALG_HANDLE hTargetAlg,
                                              PUCHAR pbDerivedKey, ULONG cbDerivedKey, ULONG dwFlags)
{
    HASH *hash = BcHash(hHash);
    if (!hash)
        return STATUS_INVALID_HANDLE;
    if (hTargetAlg && !BcAlg(hTargetAlg))
        return STATUS_INVALID_HANDLE;
    const MimeHashAlg *alg = hash->alg->info->hash;
    if (!alg || dwFlags || !pbDerivedKey || cbDerivedKey > 2 * alg->digest_size)
        return STATUS_INVALID_PARAMETER;
    UCHAR value[MIME_HASH_MAX_DIGEST];
    NTSTATUS status = BCryptFinishHash(hHash, value, alg->digest_size, 0);
    if (!status)
        mime_capi_expand(alg, value, pbDerivedKey, cbDerivedKey);
    SecureZeroMemory(value, sizeof(value));
    return status;
}
