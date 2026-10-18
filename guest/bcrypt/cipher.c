/*
 * symmetric keys: generation, import and export, duplication, properties and
 * the block and stream modes of BCryptEncrypt and BCryptDecrypt. a key lives
 * in the caller's object buffer like a hash, and keeps its own chaining value
 * for the calls that pass no iv.
 */
#include "bcrypt_private.h"

#define KEY_MIN_OBJECT 62
/* windows' own key header before the provider's object, on x64. */
#define KEY_HEADER_SIZE 32

ULONG BcKeyObjectLength(const ALG_INFO *info, ULONG mode)
{
    return info->id == ID_AES && mode == MODE_GCM ? 3200 : info->object_length;
}

KEY_HEADER *BcKey(BCRYPT_KEY_HANDLE handle)
{
    KEY_HEADER *key = handle;
    if (!key || ((ULONG_PTR)key & 1) || key->magic != KEY_MAGIC || key->size < sizeof(KEY_HEADER) ||
        key->kind < KIND_SYMMETRIC || key->kind > KIND_PAIR)
        return nullptr;
    return key;
}

static ULONG key_size(const ALG_INFO *info)
{
    switch (info->id) {
    case ID_RC4:
        return offsetof(KEY, u) + sizeof(MimeRc4);
    case ID_AES:
        return offsetof(KEY, u) + sizeof(MimeAes);
    case ID_DES:
        return offsetof(KEY, u) + sizeof(MimeDes);
    case ID_DESX:
        return offsetof(KEY, u) + sizeof(MimeDesx);
    default:
        return offsetof(KEY, u) + sizeof(MimeDes3);
    }
}

static NTSTATUS place_key(ALG *alg, PUCHAR object, ULONG size, KEY **result)
{
    if (size < KEY_MIN_OBJECT)
        return STATUS_BUFFER_TOO_SMALL;
    ULONG pad = (ULONG)((((ULONG_PTR)object + 15) & ~(ULONG_PTR)15) - (ULONG_PTR)object);
    /* gcm keys take the length of the other modes too, without precomputed tables. */
    ULONG provider = alg->info->object_length;
    if (size - pad < KEY_HEADER_SIZE + provider || size - pad < key_size(alg->info))
        return STATUS_BUFFER_TOO_SMALL;
    KEY *key = (KEY *)(object + pad);
    memset(key, 0, offsetof(KEY, u));
    key->size = size - pad;
    key->magic = KEY_MAGIC;
    key->alg = alg;
    key->kind = KIND_SYMMETRIC;
    key->mode = alg->mode;
    key->message_block = alg->message_block;
    *result = key;
    return STATUS_SUCCESS;
}

static NTSTATUS schedule(KEY *key, const UCHAR *secret, ULONG size, BOOL truncate)
{
    const ALG_INFO *info = key->alg->info;
    if (truncate && size >= info->key_max)
        size = info->key_max;
    if (size < info->key_min || size > info->key_max ||
        (info->key_increment ? (size - info->key_min) % info->key_increment : size != info->key_min))
        return STATUS_INVALID_PARAMETER;
    memcpy(key->secret, secret, size);
    key->secret_size = size;
    switch (info->id) {
    case ID_RC4:
        mime_rc4_init(&key->u.rc4, secret, size);
        break;
    case ID_AES:
        mime_aes_init(&key->u.aes, secret, size);
        break;
    case ID_DES:
        mime_des_init(&key->u.des, secret);
        break;
    case ID_DESX:
        mime_desx_init(&key->u.desx, secret);
        break;
    default:
        mime_des3_init(&key->u.des3, secret, size);
        break;
    }
    return STATUS_SUCCESS;
}

/* the object buffer the call passed, or one allocated when it passed none. */
static NTSTATUS object_buffer(ALG *alg, PUCHAR *object, ULONG *size, void **heap)
{
    *heap = nullptr;
    if (*object)
        return STATUS_SUCCESS;
    if (*size)
        return STATUS_INVALID_PARAMETER;
    *size = alg->info->iface == BCRYPT_KEY_DERIVATION_INTERFACE ? alg->info->object_length + 62 : BcObjectLength(alg);
    *object = *heap = BcAlloc(*size);
    return *heap ? STATUS_SUCCESS : STATUS_NO_MEMORY;
}

BCRYPTAPI NTSTATUS WINAPI BCryptGenerateSymmetricKey(BCRYPT_ALG_HANDLE hAlgorithm, BCRYPT_KEY_HANDLE *phKey,
                                                     PUCHAR pbKeyObject, ULONG cbKeyObject, PUCHAR pbSecret,
                                                     ULONG cbSecret, ULONG dwFlags)
{
    ALG *alg = BcAlg(hAlgorithm);
    if (!alg)
        return STATUS_INVALID_HANDLE;
    ULONG iface = alg->info->iface;
    void *heap;
    NTSTATUS status = object_buffer(alg, &pbKeyObject, &cbKeyObject, &heap);
    if (status)
        return status;
    if (!phKey || !pbSecret)
        status = STATUS_INVALID_PARAMETER;
    else if (cbKeyObject < KEY_MIN_OBJECT)
        status = STATUS_BUFFER_TOO_SMALL;
    else if (iface != BCRYPT_CIPHER_INTERFACE && iface != BCRYPT_KEY_DERIVATION_INTERFACE)
        status = STATUS_NOT_SUPPORTED;
    if (status) {
        BcFree(heap);
        return status;
    }
    if (iface == BCRYPT_KEY_DERIVATION_INTERFACE) {
        status = dwFlags ? STATUS_INVALID_PARAMETER : BcKdfKeyCreate(alg, phKey, pbKeyObject, cbKeyObject, pbSecret, cbSecret);
        if (status)
            BcFree(heap);
        else
            ((KEY_HEADER *)*phKey)->heap = heap;
        return status;
    }
    KEY *key = nullptr;
    status = dwFlags & ~0x1A0u ? STATUS_INVALID_PARAMETER : place_key(alg, pbKeyObject, cbKeyObject, &key);
    if (!status) {
        key->heap = heap;
        status = schedule(key, pbSecret, cbSecret, !(dwFlags & 0x80));
        if (status)
            key->magic = 0;
    }
    if (status) {
        BcFree(heap);
        return status;
    }
    BcAlgRef(alg);
    *phKey = key;
    return STATUS_SUCCESS;
}

/* the blob of a key data export, and windows' opaque blob, which mime makes the same with its own magic. */
#define OPAQUE_BLOB_MAGIC 0x4D51504F

BCRYPTAPI NTSTATUS WINAPI BCryptImportKey(BCRYPT_ALG_HANDLE hAlgorithm, BCRYPT_KEY_HANDLE hImportKey,
                                          LPCWSTR pszBlobType, BCRYPT_KEY_HANDLE *phKey, PUCHAR pbKeyObject,
                                          ULONG cbKeyObject, PUCHAR pbInput, ULONG cbInput, ULONG dwFlags)
{
    ALG *alg = BcAlg(hAlgorithm);
    if (!alg)
        return STATUS_INVALID_HANDLE;
    KEY *wrap = nullptr;
    if (hImportKey) {
        KEY_HEADER *k = BcKey(hImportKey);
        if (!k || k->kind != KIND_SYMMETRIC)
            return STATUS_INVALID_HANDLE;
        if (!pszBlobType || wcscmp(pszBlobType, BCRYPT_AES_WRAP_KEY_BLOB))
            return STATUS_INVALID_PARAMETER;
        wrap = (KEY *)k;
    }
    void *heap;
    NTSTATUS status = object_buffer(alg, &pbKeyObject, &cbKeyObject, &heap);
    if (status)
        return status;
    if (!phKey || !pbKeyObject || !pszBlobType)
        status = STATUS_INVALID_PARAMETER;
    else if (cbKeyObject < KEY_MIN_OBJECT)
        status = STATUS_BUFFER_TOO_SMALL;
    else if (alg->info->iface != BCRYPT_CIPHER_INTERFACE)
        status = STATUS_NOT_SUPPORTED;
    else if (!pbInput || dwFlags)
        status = STATUS_INVALID_PARAMETER;
    UCHAR secret[64];
    ULONG secret_size = 0;
    if (!status) {
        if (!wcscmp(pszBlobType, BCRYPT_KEY_DATA_BLOB) || !wcscmp(pszBlobType, BCRYPT_OPAQUE_KEY_BLOB)) {
            const BCRYPT_KEY_DATA_BLOB_HEADER *h = (const BCRYPT_KEY_DATA_BLOB_HEADER *)pbInput;
            ULONG magic = !wcscmp(pszBlobType, BCRYPT_KEY_DATA_BLOB) ? BCRYPT_KEY_DATA_BLOB_MAGIC : OPAQUE_BLOB_MAGIC;
            if (cbInput < sizeof(*h) || h->dwMagic != magic || h->dwVersion != BCRYPT_KEY_DATA_BLOB_VERSION1 ||
                h->cbKeyData > cbInput - sizeof(*h) || h->cbKeyData > sizeof(secret))
                status = STATUS_INVALID_PARAMETER;
            else
                memcpy(secret, h + 1, secret_size = h->cbKeyData);
        } else if (!wcscmp(pszBlobType, BCRYPT_AES_WRAP_KEY_BLOB)) {
            if (!wrap || wrap->alg->info->id != ID_AES)
                status = STATUS_INVALID_HANDLE;
            else if (cbInput < 24 || cbInput > sizeof(secret) + 8 || cbInput % 8)
                status = STATUS_INVALID_PARAMETER;
            else if (mime_aes_unwrap(&wrap->u.aes, pbInput, cbInput, secret))
                status = STATUS_AUTH_TAG_MISMATCH;
            else
                secret_size = cbInput - 8;
        } else {
            status = STATUS_INVALID_PARAMETER;
        }
    }
    KEY *key = nullptr;
    if (!status)
        status = place_key(alg, pbKeyObject, cbKeyObject, &key);
    if (!status) {
        key->heap = heap;
        status = schedule(key, secret, secret_size, FALSE);
        if (status)
            key->magic = 0;
    }
    SecureZeroMemory(secret, sizeof(secret));
    if (status) {
        BcFree(heap);
        return status;
    }
    BcAlgRef(alg);
    *phKey = key;
    return STATUS_SUCCESS;
}

static NTSTATUS export_symmetric(KEY *key, KEY_HEADER *wrap, LPCWSTR type, PUCHAR output, ULONG capacity,
                                 ULONG *result)
{
    if (!wcscmp(type, BCRYPT_KEY_DATA_BLOB) || !wcscmp(type, BCRYPT_OPAQUE_KEY_BLOB)) {
        UCHAR blob[sizeof(BCRYPT_KEY_DATA_BLOB_HEADER) + 64];
        BCRYPT_KEY_DATA_BLOB_HEADER *h = (BCRYPT_KEY_DATA_BLOB_HEADER *)blob;
        h->dwMagic = !wcscmp(type, BCRYPT_KEY_DATA_BLOB) ? BCRYPT_KEY_DATA_BLOB_MAGIC : OPAQUE_BLOB_MAGIC;
        h->dwVersion = BCRYPT_KEY_DATA_BLOB_VERSION1;
        h->cbKeyData = key->secret_size;
        memcpy(h + 1, key->secret, key->secret_size);
        NTSTATUS status = BcOutput(blob, sizeof(*h) + key->secret_size, output, capacity, result);
        SecureZeroMemory(blob, sizeof(blob));
        return status;
    }
    if (!wcscmp(type, BCRYPT_AES_WRAP_KEY_BLOB)) {
        if (!wrap || wrap->kind != KIND_SYMMETRIC || wrap->alg->info->id != ID_AES)
            return STATUS_INVALID_HANDLE;
        if (key->secret_size < 16 || key->secret_size % 8)
            return STATUS_INVALID_PARAMETER;
        UCHAR blob[64 + 8];
        mime_aes_wrap(&((KEY *)wrap)->u.aes, key->secret, key->secret_size, blob);
        NTSTATUS status = BcOutput(blob, key->secret_size + 8, output, capacity, result);
        SecureZeroMemory(blob, sizeof(blob));
        return status;
    }
    return STATUS_INVALID_PARAMETER;
}

BCRYPTAPI NTSTATUS WINAPI BCryptExportKey(BCRYPT_KEY_HANDLE hKey, BCRYPT_KEY_HANDLE hExportKey, LPCWSTR pszBlobType,
                                          PUCHAR pbOutput, ULONG cbOutput, ULONG *pcbResult, ULONG dwFlags)
{
    if (!pcbResult || !pszBlobType)
        return STATUS_INVALID_PARAMETER;
    KEY_HEADER *key = BcKey(hKey);
    if (!key)
        return STATUS_INVALID_HANDLE;
    KEY_HEADER *wrap = nullptr;
    if (hExportKey && !(wrap = BcKey(hExportKey)))
        return STATUS_INVALID_HANDLE;
    if (dwFlags)
        return STATUS_INVALID_PARAMETER;
    switch (key->kind) {
    case KIND_SYMMETRIC:
        return export_symmetric((KEY *)key, wrap, pszBlobType, pbOutput, cbOutput, pcbResult);
    case KIND_KDF:
        return BcKdfExport((KDF_KEY *)key, pszBlobType, pbOutput, cbOutput, pcbResult);
    default:
        if (wrap)
            return STATUS_NOT_SUPPORTED;
        return BcPairExport((KEY_PAIR *)key, pszBlobType, pbOutput, cbOutput, pcbResult);
    }
}

BCRYPTAPI NTSTATUS WINAPI BCryptDuplicateKey(BCRYPT_KEY_HANDLE hKey, BCRYPT_KEY_HANDLE *phNewKey, PUCHAR pbKeyObject,
                                             ULONG cbKeyObject, ULONG dwFlags)
{
    KEY_HEADER *key = BcKey(hKey);
    if (!key)
        return STATUS_INVALID_HANDLE;
    if (!phNewKey)
        return STATUS_INVALID_PARAMETER;
    ALG *alg = key->alg;
    void *heap;
    NTSTATUS status = object_buffer(alg, &pbKeyObject, &cbKeyObject, &heap);
    if (status)
        return status;
    if (cbKeyObject < KEY_MIN_OBJECT)
        status = STATUS_BUFFER_TOO_SMALL;
    else if (key->kind == KIND_PAIR)
        status = STATUS_NOT_SUPPORTED;
    else if (dwFlags)
        status = STATUS_INVALID_PARAMETER;
    if (!status && key->kind == KIND_KDF) {
        KDF_KEY *kdf = (KDF_KEY *)key;
        status = BcKdfKeyCreate(alg, phNewKey, pbKeyObject, cbKeyObject, kdf->secret, kdf->secret_size);
        if (!status) {
            KDF_KEY *copy = *phNewKey;
            copy->heap = heap;
            copy->hkdf_hash = kdf->hkdf_hash;
            copy->hkdf_ready = kdf->hkdf_ready;
            memcpy(copy->prk, kdf->prk, sizeof(copy->prk));
            return STATUS_SUCCESS;
        }
    }
    KEY *copy = nullptr;
    if (!status)
        status = place_key(alg, pbKeyObject, cbKeyObject, &copy);
    if (status) {
        BcFree(heap);
        return status;
    }
    memcpy((UCHAR *)copy + offsetof(KEY, kind), (UCHAR *)key + offsetof(KEY, kind),
           key_size(alg->info) - offsetof(KEY, kind));
    copy->heap = heap;
    BcAlgRef(alg);
    *phNewKey = copy;
    return STATUS_SUCCESS;
}

BCRYPTAPI NTSTATUS WINAPI BCryptDestroyKey(BCRYPT_KEY_HANDLE hKey)
{
    KEY_HEADER *key = BcKey(hKey);
    if (!key)
        return STATUS_INVALID_HANDLE;
    ALG *alg = key->alg;
    void *heap = key->heap;
    switch (key->kind) {
    case KIND_SYMMETRIC:
        SecureZeroMemory(key, key_size(alg->info));
        BcFree(heap);
        break;
    case KIND_KDF:
        BcKdfDestroy((KDF_KEY *)key);
        BcFree(heap);
        break;
    default:
        BcPairDestroy((KEY_PAIR *)key);
        break;
    }
    BcAlgRelease(alg);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ properties */

static ULONG key_strength(const KEY *key)
{
    switch (key->alg->info->id) {
    case ID_DES:
        return 56;
    case ID_DESX:
        return 184;
    case ID_3DES:
        return 168;
    case ID_3DES_112:
        return 112;
    }
    return 8 * key->secret_size;
}

NTSTATUS BcKeyProperty(KEY_HEADER *header, LPCWSTR property, PUCHAR output, ULONG capacity, ULONG *result)
{
    if (header->kind == KIND_KDF)
        return BcKdfProperty((KDF_KEY *)header, property, output, capacity, result);
    if (header->kind == KIND_PAIR)
        return BcPairProperty((KEY_PAIR *)header, property, output, capacity, result);
    KEY *key = (KEY *)header;
    const ALG_INFO *info = key->alg->info;
    if (!wcscmp(property, BCRYPT_ALGORITHM_NAME))
        return BcOutput(info->name, (ULONG)(wcslen(info->name) + 1) * sizeof(WCHAR), output, capacity, result);
    BOOL handled;
    NTSTATUS status = BcCipherCommon(key->alg, key->mode, key->message_block, property, output, capacity, result,
                                     &handled);
    if (handled)
        return status;
    ULONG value;
    if (!wcscmp(property, BCRYPT_KEY_LENGTH))
        value = 8 * key->secret_size;
    else if (!wcscmp(property, BCRYPT_KEY_STRENGTH))
        value = key_strength(key);
    else if (!wcscmp(property, BCRYPT_INITIALIZATION_VECTOR))
        return BcOutput(key->iv, info->block, output, capacity, result);
    else
        return STATUS_NOT_SUPPORTED;
    return BcOutput(&value, sizeof(value), output, capacity, result);
}

NTSTATUS BcKeySetProperty(KEY_HEADER *header, LPCWSTR property, PUCHAR input, ULONG size)
{
    if (header->kind == KIND_KDF)
        return BcKdfSetProperty((KDF_KEY *)header, property, input, size);
    if (header->kind == KIND_PAIR)
        return BcPairSetProperty((KEY_PAIR *)header, property, input, size);
    KEY *key = (KEY *)header;
    const ALG_INFO *info = key->alg->info;
    if (!wcscmp(property, BCRYPT_CHAINING_MODE))
        return BcChainingMode(key->alg, &key->mode, &key->message_block, input, size);
    if (!wcscmp(property, BCRYPT_MESSAGE_BLOCK_LENGTH)) {
        if (key->mode != MODE_CFB)
            return STATUS_NOT_SUPPORTED;
        if (size != sizeof(ULONG) || (*(ULONG *)input != 1 && *(ULONG *)input != info->block))
            return STATUS_INVALID_PARAMETER;
        key->message_block = *(ULONG *)input;
        return STATUS_SUCCESS;
    }
    if (!wcscmp(property, BCRYPT_INITIALIZATION_VECTOR)) {
        if (info->id == ID_RC4)
            return STATUS_NOT_SUPPORTED;
        if (size != info->block)
            return STATUS_INVALID_PARAMETER;
        memcpy(key->iv, input, size);
        return STATUS_SUCCESS;
    }
    return STATUS_NOT_SUPPORTED;
}

/* ------------------------------------------------------------------ encryption */

static const MimeCipher *cipher_of(const KEY *key)
{
    switch (key->alg->info->id) {
    case ID_AES:
        return &mime_cipher_aes;
    case ID_DES:
        return &mime_cipher_des;
    case ID_DESX:
        return &mime_cipher_desx;
    default:
        return &mime_cipher_des3;
    }
}

static void run_mode(KEY *key, UCHAR *chain, const UCHAR *in, UCHAR *out, ULONG size, BOOL encrypt)
{
    const MimeCipher *c = cipher_of(key);
    switch (key->mode) {
    case MODE_ECB:
        (encrypt ? mime_ecb_encrypt : mime_ecb_decrypt)(c, &key->u, in, out, size);
        break;
    case MODE_CFB:
        (encrypt ? mime_cfb_encrypt : mime_cfb_decrypt)(c, &key->u, chain, key->message_block, in, out, size);
        break;
    default:
        (encrypt ? mime_cbc_encrypt : mime_cbc_decrypt)(c, &key->u, chain, in, out, size);
        break;
    }
}

static BOOL auth_info_valid(const BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO *info)
{
    return info && info->cbSize >= sizeof(*info) && info->dwInfoVersion == BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO_VERSION &&
           info->pbNonce && !(info->dwFlags & ~3u) &&
           (!(info->dwFlags & 3) || info->pbMacContext);
}

/* gcm and ccm, the data already checked against the buffers. */
static NTSTATUS run_auth(KEY *key, BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO *info, PUCHAR iv, const UCHAR *in, UCHAR *out,
                         ULONG size, BOOL encrypt)
{
    MimeAes *aes = &key->u.aes;
    BOOL chain = info->dwFlags & BCRYPT_AUTH_MODE_CHAIN_CALLS_FLAG;
    if (key->mode == MODE_CCM) {
        if (chain || (info->dwFlags & BCRYPT_AUTH_MODE_IN_PROGRESS_FLAG) || info->pbMacContext ||
            !mime_ccm_valid(info->cbNonce, info->cbTag) || !info->pbTag)
            return STATUS_INVALID_PARAMETER;
        int r = encrypt ? mime_ccm_encrypt(aes, info->pbNonce, info->cbNonce, info->pbAuthData, info->cbAuthData, in,
                                           out, size, info->pbTag, info->cbTag)
                        : mime_ccm_decrypt(aes, info->pbNonce, info->cbNonce, info->pbAuthData, info->cbAuthData, in,
                                           out, size, info->pbTag, info->cbTag);
        return r == MIME_CRYPTO_ERR_SIGNATURE ? STATUS_AUTH_TAG_MISMATCH : r ? STATUS_INVALID_PARAMETER : STATUS_SUCCESS;
    }
    if (!info->cbNonce || (!chain && (info->cbTag < 12 || info->cbTag > 16 || !info->pbTag)))
        return STATUS_INVALID_PARAMETER;
    if (chain && (info->cbMacContext < 16 || !iv))
        return STATUS_INVALID_PARAMETER;
    MimeGcm g;
    mime_gcm_init(&g, aes, info->pbNonce, info->cbNonce);
    if (info->dwFlags & BCRYPT_AUTH_MODE_IN_PROGRESS_FLAG) {
        /* the running state: the counter in the iv buffer, the hash in the mac context. */
        if (!iv)
            return STATUS_INVALID_PARAMETER;
        memcpy(g.counter, iv, 16);
        memcpy(g.s, info->pbMacContext, 16);
        g.aad_size = info->cbAAD;
        g.data_size = info->cbData;
    }
    if (info->pbAuthData && info->cbAuthData && !g.data_size)
        mime_gcm_aad(&g, info->pbAuthData, info->cbAuthData);
    if (size)
        (encrypt ? mime_gcm_encrypt : mime_gcm_decrypt)(&g, aes, in, out, size);
    NTSTATUS status = STATUS_SUCCESS;
    if (chain) {
        memcpy(iv, g.counter, 16);
        memcpy(info->pbMacContext, g.s, 16);
        info->cbAAD = (ULONG)g.aad_size;
        info->cbData = g.data_size;
        info->dwFlags |= BCRYPT_AUTH_MODE_IN_PROGRESS_FLAG;
    } else {
        UCHAR tag[16];
        mime_gcm_tag(&g, aes, tag, info->cbTag);
        if (encrypt) {
            memcpy(info->pbTag, tag, info->cbTag);
        } else if (!mime_ct_equal(tag, info->pbTag, info->cbTag)) {
            mime_wipe(out, size);
            status = STATUS_AUTH_TAG_MISMATCH;
        }
        info->dwFlags &= ~BCRYPT_AUTH_MODE_IN_PROGRESS_FLAG;
        info->cbAAD = 0;
        info->cbData = 0;
    }
    SecureZeroMemory(&g, sizeof(g));
    return status;
}

static NTSTATUS run_rc4(KEY *key, PUCHAR in, ULONG size, PUCHAR iv, PUCHAR out, ULONG capacity, ULONG *result)
{
    *result = size;
    if (!out || !size)
        return STATUS_SUCCESS;
    if (size > capacity)
        return STATUS_BUFFER_TOO_SMALL;
    if (!in || iv)
        return STATUS_INVALID_PARAMETER;
    mime_rc4_crypt(&key->u.rc4, in, out, size);
    return STATUS_SUCCESS;
}

/* after a call the key keeps the chaining value it may continue from, and forgets it once a message is padded. */
static void keep_chain(KEY *key, PUCHAR iv, BOOL padded, BOOL auth, BOOL chained)
{
    ULONG block = key->alg->info->block;
    if (iv) {
        if (!padded || (auth && chained))
            memcpy(key->iv, iv, block);
        else
            memset(key->iv, 0, block);
    } else if (padded || (auth && !chained)) {
        memset(key->iv, 0, block);
    }
}

NTSTATUS BcSymmetricEncrypt(KEY *key, PUCHAR in, ULONG size, void *padding, PUCHAR iv, ULONG iv_size, PUCHAR out,
                            ULONG capacity, ULONG *result, ULONG flags, BOOL encrypt)
{
    const ALG_INFO *info = key->alg->info;
    ULONG mode = key->mode, block = info->block;
    BOOL auth = mode == MODE_CCM || mode == MODE_GCM;
    if (!auth && padding)
        return STATUS_INVALID_PARAMETER;
    if (flags & ~0x67u)
        return STATUS_INVALID_PARAMETER;
    if (info->id == ID_RC4)
        return run_rc4(key, in, size, iv, out, capacity, result);
    BOOL padded = flags & BCRYPT_BLOCK_PADDING;
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO *auth_info = padding;
    *result = size;
    if (size % key->message_block && !(encrypt && padded))
        return STATUS_INVALID_BUFFER_SIZE;
    ULONG body = padded ? size - size % block : size, needed = size;
    if (encrypt && padded) {
        needed = body + block;
        if (needed < body) {
            *result = ~0u;
            return STATUS_INTEGER_OVERFLOW;
        }
    }
    *result = needed;
    if (!out && (!auth || in))
        return STATUS_SUCCESS;
    if (!encrypt && !size && !auth)
        return STATUS_SUCCESS;
    if (encrypt ? needed > capacity : padded ? size < block || size - block > capacity : size > capacity)
        return STATUS_BUFFER_TOO_SMALL;
    if (!in && (!auth || size))
        return STATUS_INVALID_PARAMETER;
    if (encrypt && !body && !auth && !padded)
        return STATUS_SUCCESS;
    UCHAR *chain = key->iv;
    if (iv) {
        if (!auth && iv_size < block)
            return STATUS_INVALID_BUFFER_SIZE;
        if (mode == MODE_ECB)
            return STATUS_INVALID_PARAMETER;
        chain = iv;
    }
    if (auth) {
        if (padded || !auth_info_valid(auth_info))
            return STATUS_INVALID_PARAMETER;
        BOOL chained = auth_info->dwFlags & BCRYPT_AUTH_MODE_CHAIN_CALLS_FLAG;
        if (chained && size % block)
            return STATUS_INVALID_BUFFER_SIZE;
        NTSTATUS status = run_auth(key, auth_info, iv, in, out, size, encrypt);
        if (!status)
            keep_chain(key, iv, FALSE, TRUE, chained);
        return status;
    }
    if (encrypt) {
        UCHAR last[16];
        if (padded) {
            ULONG rest = size - body;
            memcpy(last, in + body, rest);
            memset(last + rest, (int)(block - rest), block - rest);
        }
        run_mode(key, chain, in, out, body, TRUE);
        if (padded)
            run_mode(key, chain, last, out + body, block, TRUE);
        SecureZeroMemory(last, sizeof(last));
    } else if (!padded) {
        run_mode(key, chain, in, out, size, FALSE);
    } else {
        /* the last block goes through a buffer, so the output needs room for the plaintext only. */
        UCHAR last[16];
        run_mode(key, chain, in, out, size - block, FALSE);
        run_mode(key, chain, in + size - block, last, block, FALSE);
        ULONG pad = last[block - 1], bad = pad == 0 || pad > block;
        for (ULONG i = 0; i < block; i++)
            bad |= (i >= block - pad) & (last[i] != pad);
        if (bad) {
            SecureZeroMemory(last, sizeof(last));
            keep_chain(key, iv, TRUE, FALSE, FALSE);
            return STATUS_DATA_ERROR;
        }
        ULONG plain = size - pad;
        *result = plain;
        if (plain > capacity) {
            SecureZeroMemory(last, sizeof(last));
            return STATUS_BUFFER_TOO_SMALL;
        }
        memcpy(out + size - block, last, block - pad);
        SecureZeroMemory(last, sizeof(last));
    }
    keep_chain(key, iv, padded, FALSE, FALSE);
    return STATUS_SUCCESS;
}

BCRYPTAPI NTSTATUS WINAPI BCryptEncrypt(BCRYPT_KEY_HANDLE hKey, PUCHAR pbInput, ULONG cbInput, VOID *pPaddingInfo,
                                        PUCHAR pbIV, ULONG cbIV, PUCHAR pbOutput, ULONG cbOutput, ULONG *pcbResult,
                                        ULONG dwFlags)
{
    if (!pcbResult)
        return STATUS_INVALID_PARAMETER;
    KEY_HEADER *key = BcKey(hKey);
    if (!key)
        return STATUS_INVALID_HANDLE;
    if (key->kind == KIND_SYMMETRIC)
        return BcSymmetricEncrypt((KEY *)key, pbInput, cbInput, pPaddingInfo, pbIV, cbIV, pbOutput, cbOutput, pcbResult,
                                  dwFlags, TRUE);
    if (key->kind == KIND_PAIR && key->alg->info->iface == BCRYPT_ASYMMETRIC_ENCRYPTION_INTERFACE)
        return BcPairEncrypt((KEY_PAIR *)key, pbInput, cbInput, pPaddingInfo, pbOutput, cbOutput, pcbResult, dwFlags,
                             TRUE);
    return STATUS_NOT_SUPPORTED;
}

BCRYPTAPI NTSTATUS WINAPI BCryptDecrypt(BCRYPT_KEY_HANDLE hKey, PUCHAR pbInput, ULONG cbInput, VOID *pPaddingInfo,
                                        PUCHAR pbIV, ULONG cbIV, PUCHAR pbOutput, ULONG cbOutput, ULONG *pcbResult,
                                        ULONG dwFlags)
{
    if (!pcbResult)
        return STATUS_INVALID_PARAMETER;
    KEY_HEADER *key = BcKey(hKey);
    if (!key)
        return STATUS_INVALID_HANDLE;
    if (key->kind == KIND_SYMMETRIC)
        return BcSymmetricEncrypt((KEY *)key, pbInput, cbInput, pPaddingInfo, pbIV, cbIV, pbOutput, cbOutput, pcbResult,
                                  dwFlags, FALSE);
    if (key->kind == KIND_PAIR && key->alg->info->iface == BCRYPT_ASYMMETRIC_ENCRYPTION_INTERFACE)
        return BcPairEncrypt((KEY_PAIR *)key, pbInput, cbInput, pPaddingInfo, pbOutput, cbOutput, pcbResult, dwFlags,
                             FALSE);
    return STATUS_NOT_SUPPORTED;
}
