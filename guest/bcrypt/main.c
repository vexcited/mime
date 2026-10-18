/*
 * bcrypt.dll, the cng primitives: algorithm providers, their properties and
 * enumeration. windows' bcrypt routes every call to the provider dll the
 * registry names, bcryptprimitives.dll for the microsoft primitive provider;
 * mime's bcrypt is that provider itself, built on guest/lib/crypto.
 */
#include "bcrypt_private.h"

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

void *BcAlloc(SIZE_T size)
{
    return RtlAllocateHeap(NtCurrentTeb()->ProcessEnvironmentBlock->ProcessHeap, 0, size);
}

void *BcAllocZero(SIZE_T size)
{
    return RtlAllocateHeap(NtCurrentTeb()->ProcessEnvironmentBlock->ProcessHeap, HEAP_ZERO_MEMORY, size);
}

void BcFree(void *p)
{
    if (p)
        RtlFreeHeap(NtCurrentTeb()->ProcessEnvironmentBlock->ProcessHeap, 0, p);
}

NTSTATUS BcStatus(int crypto_status)
{
    switch (crypto_status) {
    case MIME_CRYPTO_OK:
        return STATUS_SUCCESS;
    case MIME_CRYPTO_ERR_SIGNATURE:
        return STATUS_INVALID_SIGNATURE;
    case MIME_CRYPTO_ERR_NOMEM:
        return STATUS_NO_MEMORY;
    case MIME_CRYPTO_ERR_RANDOM:
        return STATUS_UNSUCCESSFUL;
    default:
        return STATUS_INVALID_PARAMETER;
    }
}

/* the convention of every query: the size always, the value when there is room. */
NTSTATUS BcOutput(const void *value, ULONG size, PUCHAR output, ULONG capacity, ULONG *result)
{
    *result = size;
    if (!output)
        return STATUS_SUCCESS;
    if (capacity < size)
        return STATUS_BUFFER_TOO_SMALL;
    memcpy(output, value, size);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ algorithms */

#define MODES_BLOCK ((1u << MODE_CBC) | (1u << MODE_ECB) | (1u << MODE_CFB))
#define MODES_AES (MODES_BLOCK | (1u << MODE_CCM) | (1u << MODE_GCM))

/*
 * the primitive provider's algorithms in the order windows' registry lists
 * them, with the object lengths of bcryptprimitives.dll of build 26300.
 */
static const ALG_INFO g_algorithms[] = {
    {L"RC4", ID_RC4, BCRYPT_CIPHER_INTERFACE, nullptr, 592, 0, 1, 1, 64, 1, 1u << MODE_NA, MODE_NA},
    {L"AES", ID_AES, BCRYPT_CIPHER_INTERFACE, nullptr, 592, 0, 16, 16, 32, 8, MODES_AES, MODE_CBC},
    {L"DES", ID_DES, BCRYPT_CIPHER_INTERFACE, nullptr, 480, 0, 8, 8, 8, 0, MODES_BLOCK, MODE_CBC},
    {L"DESX", ID_DESX, BCRYPT_CIPHER_INTERFACE, nullptr, 512, 0, 8, 24, 24, 0, MODES_BLOCK, MODE_CBC},
    {L"3DES", ID_3DES, BCRYPT_CIPHER_INTERFACE, nullptr, 496, 0, 8, 24, 24, 0, MODES_BLOCK, MODE_CBC},
    {L"3DES_112", ID_3DES_112, BCRYPT_CIPHER_INTERFACE, nullptr, 480, 0, 8, 16, 16, 0, MODES_BLOCK, MODE_CBC},
    {L"SHA1", ID_SHA1, BCRYPT_HASH_INTERFACE, &mime_sha1, 256, 352},
    {L"MD4", ID_MD4, BCRYPT_HASH_INTERFACE, &mime_md4, 240, 240},
    {L"MD5", ID_MD5, BCRYPT_HASH_INTERFACE, &mime_md5, 240, 304},
    {L"SHA256", ID_SHA256, BCRYPT_HASH_INTERFACE, &mime_sha256, 256, 352},
    {L"SHA384", ID_SHA384, BCRYPT_HASH_INTERFACE, &mime_sha384, 352, 512},
    {L"SHA512", ID_SHA512, BCRYPT_HASH_INTERFACE, &mime_sha512, 352, 512},
    {L"AES-GMAC", ID_AES_GMAC, BCRYPT_HASH_INTERFACE, nullptr, 2848, 2848, 16},
    {L"AES-CMAC", ID_AES_CMAC, BCRYPT_HASH_INTERFACE, nullptr, 736, 736, 16},
    {L"RSA", ID_RSA, BCRYPT_ASYMMETRIC_ENCRYPTION_INTERFACE},
    {L"DH", ID_DH, BCRYPT_SECRET_AGREEMENT_INTERFACE},
    {L"ECDH_P256", ID_ECDH_P256, BCRYPT_SECRET_AGREEMENT_INTERFACE, .curve = &mime_p256},
    {L"ECDH_P384", ID_ECDH_P384, BCRYPT_SECRET_AGREEMENT_INTERFACE, .curve = &mime_p384},
    {L"ECDH_P521", ID_ECDH_P521, BCRYPT_SECRET_AGREEMENT_INTERFACE, .curve = &mime_p521},
    {L"ECDH", ID_ECDH, BCRYPT_SECRET_AGREEMENT_INTERFACE},
    {L"RSA_SIGN", ID_RSA_SIGN, BCRYPT_SIGNATURE_INTERFACE},
    {L"ECDSA_P256", ID_ECDSA_P256, BCRYPT_SIGNATURE_INTERFACE, .curve = &mime_p256},
    {L"ECDSA_P384", ID_ECDSA_P384, BCRYPT_SIGNATURE_INTERFACE, .curve = &mime_p384},
    {L"ECDSA_P521", ID_ECDSA_P521, BCRYPT_SIGNATURE_INTERFACE, .curve = &mime_p521},
    {L"ECDSA", ID_ECDSA, BCRYPT_SIGNATURE_INTERFACE},
    {L"RNG", ID_RNG, BCRYPT_RNG_INTERFACE},
    {L"FIPS186DSARNG", ID_RNG_FIPS186, BCRYPT_RNG_INTERFACE},
    {L"DUALECRNG", ID_RNG_DUAL_EC, BCRYPT_RNG_INTERFACE},
    {L"SP800_108_CTR_HMAC", ID_SP800108, BCRYPT_KEY_DERIVATION_INTERFACE, nullptr, 640},
    {L"SP800_56A_CONCAT", ID_SP80056A, BCRYPT_KEY_DERIVATION_INTERFACE, nullptr, 640},
    {L"PBKDF2", ID_PBKDF2, BCRYPT_KEY_DERIVATION_INTERFACE, nullptr, 640},
    {L"CAPI_KDF", ID_CAPI_KDF, BCRYPT_KEY_DERIVATION_INTERFACE, nullptr, 640},
    {L"TLS1_1_KDF", ID_TLS11_KDF, BCRYPT_KEY_DERIVATION_INTERFACE, nullptr, 640},
    {L"TLS1_2_KDF", ID_TLS12_KDF, BCRYPT_KEY_DERIVATION_INTERFACE, nullptr, 640},
    {L"HKDF", ID_HKDF, BCRYPT_KEY_DERIVATION_INTERFACE, nullptr, 640},
};

const ALG_INFO *BcFindAlg(LPCWSTR name)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_algorithms); i++)
        if (!_wcsicmp(g_algorithms[i].name, name))
            return &g_algorithms[i];
    return nullptr;
}

const ALG_INFO *BcInfoById(ULONG id)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_algorithms); i++)
        if (g_algorithms[i].id == id)
            return &g_algorithms[i];
    return nullptr;
}

/* the hashes a parameter or padding names, as windows' providers accept them. */
const MimeHashAlg *BcHashByName(LPCWSTR name)
{
    const ALG_INFO *info = name ? BcFindAlg(name) : nullptr;
    return info ? info->hash : nullptr;
}

/* ------------------------------------------------------------------ handles */

/* the pseudo handles of index h >> 4: the name, the open flags and the chaining mode, 0 for the default. */
static const struct {
    const WCHAR *name;
    ULONG flags;
    ULONG mode;
} g_pseudo[0x4B] = {
    [0x00] = {L"MD2", 0x60},       [0x01] = {L"MD4", 0x60},        [0x02] = {L"MD5", 0x60},
    [0x03] = {L"SHA1", 0x60},      [0x04] = {L"SHA256", 0x60},     [0x05] = {L"SHA384", 0x60},
    [0x06] = {L"SHA512", 0x60},    [0x07] = {L"RC4"},              [0x08] = {L"RNG"},
    [0x09] = {L"MD5", 0x28},       [0x0a] = {L"SHA1", 0x28},       [0x0b] = {L"SHA256", 0x28},
    [0x0c] = {L"SHA384", 0x28},    [0x0d] = {L"SHA512", 0x28},     [0x0e] = {L"RSA"},
    [0x0f] = {L"ECDSA"},           [0x10] = {L"AES-CMAC", 0x20},   [0x11] = {L"AES-GMAC", 0x20},
    [0x12] = {L"MD2", 0x28},       [0x13] = {L"MD4", 0x28},        [0x14] = {L"3DES", 0, MODE_CBC},
    [0x15] = {L"3DES", 0, MODE_ECB}, [0x16] = {L"3DES", 0, MODE_CFB}, [0x17] = {L"3DES_112", 0, MODE_CBC},
    [0x18] = {L"3DES_112", 0, MODE_ECB}, [0x19] = {L"3DES_112", 0, MODE_CFB}, [0x1a] = {L"AES", 0, MODE_CBC},
    [0x1b] = {L"AES", 0, MODE_ECB}, [0x1c] = {L"AES", 0, MODE_CFB}, [0x1d] = {L"AES", 0, MODE_CCM},
    [0x1e] = {L"AES", 0, MODE_GCM}, [0x1f] = {L"DES", 0, MODE_CBC}, [0x20] = {L"DES", 0, MODE_ECB},
    [0x21] = {L"DES", 0, MODE_CFB}, [0x22] = {L"DESX", 0, MODE_CBC}, [0x23] = {L"DESX", 0, MODE_ECB},
    [0x24] = {L"DESX", 0, MODE_CFB}, [0x25] = {L"RC2", 0, MODE_CBC}, [0x26] = {L"RC2", 0, MODE_ECB},
    [0x27] = {L"RC2", 0, MODE_CFB}, [0x28] = {L"DH"},              [0x29] = {L"ECDH"},
    [0x2a] = {L"ECDH_P256"},       [0x2b] = {L"ECDH_P384"},        [0x2c] = {L"ECDH_P521"},
    [0x2d] = {L"DSA"},             [0x2e] = {L"ECDSA_P256"},       [0x2f] = {L"ECDSA_P384"},
    [0x30] = {L"ECDSA_P521"},      [0x31] = {L"RSA_SIGN"},         [0x32] = {L"CAPI_KDF"},
    [0x33] = {L"PBKDF2"},          [0x34] = {L"SP800_108_CTR_HMAC"}, [0x35] = {L"SP800_56A_CONCAT"},
    [0x36] = {L"TLS1_1_KDF"},      [0x37] = {L"TLS1_2_KDF"},       [0x38] = {L"XTS-AES"},
    [0x39] = {L"HKDF"},            [0x3a] = {L"CHACHA20_POLY1305"},
};

static ALG g_pseudo_algs[0x4B];
static volatile LONG g_pseudo_ready[0x4B];
static SRWLOCK g_pseudo_lock = SRWLOCK_INIT;

static NTSTATUS open_alg(ALG *alg, const ALG_INFO *info, ULONG flags, ULONG mode);

static ALG *pseudo_alg(ULONG_PTR handle)
{
    ULONG index = (ULONG)(handle >> 4);
    if (index >= RTL_NUMBER_OF(g_pseudo) || !g_pseudo[index].name)
        return nullptr;
    if (g_pseudo_ready[index] == 1)
        return &g_pseudo_algs[index];
    if (g_pseudo_ready[index] == 2)
        return nullptr;
    AcquireSRWLockExclusive(&g_pseudo_lock);
    if (!g_pseudo_ready[index]) {
        const ALG_INFO *info = BcFindAlg(g_pseudo[index].name);
        ALG *alg = &g_pseudo_algs[index];
        ULONG flags = g_pseudo[index].flags & (BCRYPT_ALG_HANDLE_HMAC_FLAG | BCRYPT_HASH_REUSABLE_FLAG);
        if (info && !open_alg(alg, info, flags, g_pseudo[index].mode)) {
            alg->flags |= ALG_FLAG_PSEUDO;
            InterlockedExchange(&g_pseudo_ready[index], 1);
        } else {
            InterlockedExchange(&g_pseudo_ready[index], 2);
        }
    }
    ReleaseSRWLockExclusive(&g_pseudo_lock);
    return g_pseudo_ready[index] == 1 ? &g_pseudo_algs[index] : nullptr;
}

ALG *BcAlg(BCRYPT_ALG_HANDLE handle)
{
    ULONG_PTR value = (ULONG_PTR)handle;
    if (!value)
        return nullptr;
    if ((value & 0xF) == 1)
        return pseudo_alg(value);
    ALG *alg = handle;
    return alg->size == sizeof(ALG) && alg->magic == ALG_MAGIC ? alg : nullptr;
}

void BcAlgRef(ALG *alg)
{
    if (!(alg->flags & ALG_FLAG_PSEUDO))
        InterlockedIncrement(&alg->refs);
}

void BcAlgRelease(ALG *alg)
{
    if (!(alg->flags & ALG_FLAG_PSEUDO) && !InterlockedDecrement(&alg->refs)) {
        SecureZeroMemory(alg, sizeof(*alg));
        BcFree(alg);
    }
}

static NTSTATUS open_alg(ALG *alg, const ALG_INFO *info, ULONG flags, ULONG mode)
{
    switch (info->iface) {
    case BCRYPT_HASH_INTERFACE:
        if (flags & ~(BCRYPT_ALG_HANDLE_HMAC_FLAG | BCRYPT_HASH_REUSABLE_FLAG | BCRYPT_MULTI_FLAG))
            return STATUS_INVALID_PARAMETER;
        /* cmac is keyed already, gmac takes no hmac flag. */
        if (info->id == ID_AES_CMAC)
            flags &= ~BCRYPT_ALG_HANDLE_HMAC_FLAG;
        else if (info->id == ID_AES_GMAC && (flags & BCRYPT_ALG_HANDLE_HMAC_FLAG))
            return STATUS_NOT_FOUND;
        break;
    case BCRYPT_CIPHER_INTERFACE:
        if (flags & ~(BCRYPT_ALG_HANDLE_HMAC_FLAG | BCRYPT_MULTI_FLAG))
            return STATUS_INVALID_PARAMETER;
        break;
    default:
        if (flags & ~BCRYPT_ALG_HANDLE_HMAC_FLAG)
            return STATUS_INVALID_PARAMETER;
        flags = 0;
        break;
    }
    memset(alg, 0, sizeof(*alg));
    alg->size = sizeof(*alg);
    alg->magic = ALG_MAGIC;
    alg->flags = flags & (BCRYPT_ALG_HANDLE_HMAC_FLAG | BCRYPT_HASH_REUSABLE_FLAG);
    alg->refs = 1;
    alg->info = info;
    alg->curve = info->curve;
    if (info->iface == BCRYPT_CIPHER_INTERFACE) {
        alg->mode = mode ? mode : info->mode;
        alg->message_block = alg->mode == MODE_CBC || alg->mode == MODE_ECB ? info->block : 1;
    }
    return STATUS_SUCCESS;
}

BCRYPTAPI NTSTATUS WINAPI BCryptOpenAlgorithmProvider(BCRYPT_ALG_HANDLE *phAlgorithm, LPCWSTR pszAlgId,
                                                      LPCWSTR pszImplementation, ULONG dwFlags)
{
    if (!phAlgorithm || !pszAlgId || (dwFlags & BCRYPT_PROV_DISPATCH))
        return STATUS_INVALID_PARAMETER;
    if (pszImplementation && _wcsicmp(pszImplementation, MS_PRIMITIVE_PROVIDER))
        return STATUS_NOT_FOUND;
    const ALG_INFO *info = BcFindAlg(pszAlgId);
    if (!info)
        return STATUS_NOT_FOUND;
    ALG *alg = BcAlloc(sizeof(ALG));
    if (!alg)
        return STATUS_NO_MEMORY;
    NTSTATUS status = open_alg(alg, info, dwFlags, 0);
    if (status) {
        BcFree(alg);
        return status;
    }
    *phAlgorithm = alg;
    return STATUS_SUCCESS;
}

BCRYPTAPI NTSTATUS WINAPI BCryptCloseAlgorithmProvider(BCRYPT_ALG_HANDLE hAlgorithm, ULONG dwFlags)
{
    ALG *alg = BcAlg(hAlgorithm);
    if (!alg || (alg->flags & ALG_FLAG_PSEUDO))
        return STATUS_INVALID_HANDLE;
    BcAlgRelease(alg);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ enumeration */

BCRYPTAPI NTSTATUS WINAPI BCryptEnumAlgorithms(ULONG dwAlgOperations, ULONG *pAlgCount,
                                               BCRYPT_ALGORITHM_IDENTIFIER **ppAlgList, ULONG dwFlags)
{
    if (dwAlgOperations >= 0x100 || !pAlgCount || !ppAlgList || dwFlags)
        return STATUS_INVALID_PARAMETER;
    *pAlgCount = 0;
    *ppAlgList = nullptr;
    ULONG count = 0;
    SIZE_T chars = 0;
    for (ULONG pass = 0; pass < 2; pass++) {
        BCRYPT_ALGORITHM_IDENTIFIER *list = pass ? *ppAlgList : nullptr;
        WCHAR *text = pass ? (WCHAR *)(list + count) : nullptr;
        ULONG n = 0;
        for (ULONG iface = 1; iface <= BCRYPT_KEY_DERIVATION_INTERFACE; iface++) {
            if (dwAlgOperations && !(dwAlgOperations & (1u << (iface - 1))))
                continue;
            for (ULONG i = 0; i < RTL_NUMBER_OF(g_algorithms); i++) {
                if (g_algorithms[i].iface != iface)
                    continue;
                SIZE_T length = wcslen(g_algorithms[i].name) + 1;
                if (pass) {
                    memcpy(text, g_algorithms[i].name, length * sizeof(WCHAR));
                    list[n].pszName = text;
                    list[n].dwClass = iface;
                    list[n].dwFlags = 0;
                    text += length;
                } else {
                    chars += length;
                }
                n++;
            }
        }
        if (!pass) {
            count = n;
            if (!count)
                return STATUS_SUCCESS;
            *ppAlgList = BcAlloc(count * sizeof(BCRYPT_ALGORITHM_IDENTIFIER) + chars * sizeof(WCHAR));
            if (!*ppAlgList)
                return STATUS_NO_MEMORY;
        }
    }
    *pAlgCount = count;
    return STATUS_SUCCESS;
}

BCRYPTAPI NTSTATUS WINAPI BCryptEnumProviders(LPCWSTR pszAlgId, ULONG *pImplCount, BCRYPT_PROVIDER_NAME **ppImplList,
                                              ULONG dwFlags)
{
    if (!pszAlgId || !*pszAlgId || !pImplCount || !ppImplList || dwFlags)
        return STATUS_INVALID_PARAMETER;
    *pImplCount = 0;
    *ppImplList = nullptr;
    if (!BcFindAlg(pszAlgId))
        return STATUS_NOT_FOUND;
    static const WCHAR name[] = MS_PRIMITIVE_PROVIDER;
    BCRYPT_PROVIDER_NAME *list = BcAlloc(sizeof(*list) + sizeof(name));
    if (!list)
        return STATUS_NO_MEMORY;
    list->pszProviderName = (LPWSTR)(list + 1);
    memcpy(list + 1, name, sizeof(name));
    *pImplCount = 1;
    *ppImplList = list;
    return STATUS_SUCCESS;
}

BCRYPTAPI VOID WINAPI BCryptFreeBuffer(PVOID pvBuffer)
{
    BcFree(pvBuffer);
}

BCRYPTAPI NTSTATUS WINAPI BCryptGetFipsAlgorithmMode(BOOLEAN *pfEnabled)
{
    if (!pfEnabled)
        return STATUS_INVALID_PARAMETER;
    *pfEnabled = FALSE;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ properties */

/* what bcrypt adds to the provider's object length: its own header and room to align. */
ULONG BcObjectLength(const ALG *alg)
{
    const ALG_INFO *info = alg->info;
    if (info->iface == BCRYPT_HASH_INTERFACE)
        return ((alg->flags & BCRYPT_ALG_HANDLE_HMAC_FLAG) ? info->hmac_object_length : info->object_length) + 70;
    return BcKeyObjectLength(info, alg->mode) + 62;
}

static const WCHAR *const g_mode_names[] = {BCRYPT_CHAIN_MODE_NA,  BCRYPT_CHAIN_MODE_CBC, BCRYPT_CHAIN_MODE_ECB,
                                            BCRYPT_CHAIN_MODE_CFB, BCRYPT_CHAIN_MODE_CCM, BCRYPT_CHAIN_MODE_GCM};

NTSTATUS BcChainingMode(ALG *alg, ULONG *mode, ULONG *message_block, PUCHAR input, ULONG size)
{
    ULONG index = 0;
    while (index < RTL_NUMBER_OF(g_mode_names) && wcscmp(g_mode_names[index], (const WCHAR *)input))
        index++;
    if (index == RTL_NUMBER_OF(g_mode_names))
        return STATUS_INVALID_PARAMETER;
    if (!(alg->info->modes & (1u << index)))
        return STATUS_NOT_SUPPORTED;
    *mode = index;
    *message_block = index == MODE_CBC || index == MODE_ECB ? alg->info->block : 1;
    return STATUS_SUCCESS;
}

/* the properties every cipher object answers, algorithm or key. */
NTSTATUS BcCipherCommon(const ALG *alg, ULONG mode, ULONG message_block, LPCWSTR property, PUCHAR output,
                        ULONG capacity, ULONG *result, BOOL *handled)
{
    *handled = TRUE;
    if (!wcscmp(property, BCRYPT_BLOCK_LENGTH))
        return BcOutput(&alg->info->block, sizeof(ULONG), output, capacity, result);
    if (!wcscmp(property, BCRYPT_MESSAGE_BLOCK_LENGTH))
        return BcOutput(&message_block, sizeof(ULONG), output, capacity, result);
    if (!wcscmp(property, BCRYPT_CHAINING_MODE)) {
        /* windows copies a fixed 32 bytes, every name being 16 characters with its terminator. */
        WCHAR name[16] = {};
        wcscpy(name, g_mode_names[mode]);
        return BcOutput(name, sizeof(name), output, capacity, result);
    }
    *handled = FALSE;
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS alg_property(ALG *alg, LPCWSTR property, PUCHAR output, ULONG capacity, ULONG *result)
{
    const ALG_INFO *info = alg->info;
    if (!wcscmp(property, BCRYPT_ALGORITHM_NAME))
        return BcOutput(info->name, (ULONG)(wcslen(info->name) + 1) * sizeof(WCHAR), output, capacity, result);
    switch (info->iface) {
    case BCRYPT_HASH_INTERFACE: {
        ULONG value;
        BOOL keyed = (alg->flags & BCRYPT_ALG_HANDLE_HMAC_FLAG) || info->id == ID_AES_CMAC || info->id == ID_AES_GMAC;
        if (!wcscmp(property, BCRYPT_OBJECT_LENGTH)) {
            value = BcObjectLength(alg);
        } else if (!wcscmp(property, BCRYPT_HASH_LENGTH)) {
            value = info->hash ? info->hash->digest_size : 16;
        } else if (!wcscmp(property, BCRYPT_HASH_BLOCK_LENGTH)) {
            value = info->hash ? info->hash->block_size : 16;
        } else if (!wcscmp(property, BCRYPT_IS_KEYED_HASH)) {
            value = keyed;
        } else if (!wcscmp(property, BCRYPT_IS_REUSABLE_HASH)) {
            value = (alg->flags & BCRYPT_HASH_REUSABLE_FLAG) != 0;
        } else if (!wcscmp(property, BCRYPT_HASH_OID_LIST)) {
            return BcHashOidList(info, output, capacity, result);
        } else {
            return STATUS_NOT_SUPPORTED;
        }
        return BcOutput(&value, sizeof(value), output, capacity, result);
    }
    case BCRYPT_CIPHER_INTERFACE: {
        BOOL handled;
        NTSTATUS status = BcCipherCommon(alg, alg->mode, alg->message_block, property, output, capacity, result,
                                         &handled);
        if (handled)
            return status;
        if (!wcscmp(property, BCRYPT_OBJECT_LENGTH)) {
            ULONG value = BcObjectLength(alg);
            return BcOutput(&value, sizeof(value), output, capacity, result);
        }
        if (!wcscmp(property, BCRYPT_KEY_LENGTHS)) {
            BCRYPT_KEY_LENGTHS_STRUCT lengths = {8 * info->key_min, 8 * info->key_max, 8 * info->key_increment};
            return BcOutput(&lengths, sizeof(lengths), output, capacity, result);
        }
        if (!wcscmp(property, BCRYPT_BLOCK_SIZE_LIST))
            return BcOutput(&info->block, sizeof(ULONG), output, capacity, result);
        if (!wcscmp(property, BCRYPT_AUTH_TAG_LENGTH) && info->id == ID_AES &&
            (alg->mode == MODE_CCM || alg->mode == MODE_GCM)) {
            BCRYPT_AUTH_TAG_LENGTHS_STRUCT tags = alg->mode == MODE_CCM ? (BCRYPT_AUTH_TAG_LENGTHS_STRUCT){4, 16, 2}
                                                                       : (BCRYPT_AUTH_TAG_LENGTHS_STRUCT){12, 16, 1};
            return BcOutput(&tags, sizeof(tags), output, capacity, result);
        }
        return STATUS_NOT_SUPPORTED;
    }
    case BCRYPT_ASYMMETRIC_ENCRYPTION_INTERFACE:
    case BCRYPT_SIGNATURE_INTERFACE:
        if (info->id == ID_RSA || info->id == ID_RSA_SIGN) {
            if (!wcscmp(property, BCRYPT_PADDING_SCHEMES)) {
                ULONG value = info->id == ID_RSA_SIGN ? BCRYPT_SUPPORTED_PAD_PKCS1_SIG | BCRYPT_SUPPORTED_PAD_PSS
                                                      : BCRYPT_SUPPORTED_PAD_PKCS1_ENC | BCRYPT_SUPPORTED_PAD_PKCS1_SIG |
                                                            BCRYPT_SUPPORTED_PAD_OAEP | BCRYPT_SUPPORTED_PAD_PSS;
                return BcOutput(&value, sizeof(value), output, capacity, result);
            }
            if (!wcscmp(property, BCRYPT_KEY_LENGTHS)) {
                BCRYPT_KEY_LENGTHS_STRUCT lengths = {512, 16384, 8};
                return BcOutput(&lengths, sizeof(lengths), output, capacity, result);
            }
            return STATUS_NOT_SUPPORTED;
        }
        [[fallthrough]];
    case BCRYPT_SECRET_AGREEMENT_INTERFACE:
        if (info->id == ID_DH) {
            if (!wcscmp(property, BCRYPT_KEY_LENGTHS)) {
                BCRYPT_KEY_LENGTHS_STRUCT lengths = {512, 4096, 8};
                return BcOutput(&lengths, sizeof(lengths), output, capacity, result);
            }
            return STATUS_NOT_SUPPORTED;
        }
        if (!wcscmp(property, BCRYPT_KEY_LENGTHS)) {
            BCRYPT_KEY_LENGTHS_STRUCT lengths = {112, 528, 1};
            if (alg->curve)
                lengths = (BCRYPT_KEY_LENGTHS_STRUCT){mime_ec_bits(alg->curve), mime_ec_bits(alg->curve), 0};
            return BcOutput(&lengths, sizeof(lengths), output, capacity, result);
        }
        if (!wcscmp(property, BCRYPT_ECC_CURVE_NAME_LIST) || alg->curve)
            return BcEccCurveProperty(alg->curve, property, output, capacity, result);
        if (!wcscmp(property, BCRYPT_ECC_CURVE_NAME) || !wcscmp(property, BCRYPT_ECC_PARAMETERS))
            return STATUS_INVALID_PARAMETER;
        return STATUS_NOT_SUPPORTED;
    case BCRYPT_KEY_DERIVATION_INTERFACE:
        if (!wcscmp(property, BCRYPT_KEY_LENGTHS)) {
            BCRYPT_KEY_LENGTHS_STRUCT lengths = {0, 16384, 8};
            return BcOutput(&lengths, sizeof(lengths), output, capacity, result);
        }
        if (!wcscmp(property, BCRYPT_OBJECT_LENGTH)) {
            ULONG value = info->object_length + 62;
            return BcOutput(&value, sizeof(value), output, capacity, result);
        }
        return STATUS_NOT_SUPPORTED;
    default:
        return STATUS_NOT_SUPPORTED;
    }
}

BCRYPTAPI NTSTATUS WINAPI BCryptGetProperty(BCRYPT_HANDLE hObject, LPCWSTR pszProperty, PUCHAR pbOutput,
                                            ULONG cbOutput, ULONG *pcbResult, ULONG dwFlags)
{
    if (!pszProperty || !*pszProperty || !pcbResult)
        return STATUS_INVALID_PARAMETER;
    ALG *alg = BcAlg(hObject);
    HASH *hash = alg ? nullptr : BcHash(hObject);
    KEY_HEADER *key = alg || hash ? nullptr : BcKey(hObject);
    const ALG *owner = alg ? alg : hash ? hash->alg : key ? key->alg : nullptr;
    if (!owner)
        return STATUS_INVALID_HANDLE;
    if (!wcscmp(pszProperty, BCRYPT_PROVIDER_HANDLE)) {
        BCRYPT_ALG_HANDLE provider = (BCRYPT_ALG_HANDLE)owner;
        return BcOutput(&provider, sizeof(provider), pbOutput, cbOutput, pcbResult);
    }
    if (!wcscmp(pszProperty, BCRYPT_PRIMITIVE_TYPE))
        return BcOutput(&owner->info->iface, sizeof(ULONG), pbOutput, cbOutput, pcbResult);
    if (alg)
        return alg_property(alg, pszProperty, pbOutput, cbOutput, pcbResult);
    if (hash)
        return BcHashProperty(hash, pszProperty, pbOutput, cbOutput, pcbResult);
    return BcKeyProperty(key, pszProperty, pbOutput, cbOutput, pcbResult);
}

BCRYPTAPI NTSTATUS WINAPI BCryptSetProperty(BCRYPT_HANDLE hObject, LPCWSTR pszProperty, PUCHAR pbInput, ULONG cbInput,
                                            ULONG dwFlags)
{
    if (!pszProperty)
        return STATUS_INVALID_PARAMETER;
    ALG *alg = BcAlg(hObject);
    if (alg) {
        if (alg->flags & ALG_FLAG_PSEUDO)
            return STATUS_ACCESS_DENIED;
        if (!pbInput || dwFlags)
            return STATUS_INVALID_PARAMETER;
        const ALG_INFO *info = alg->info;
        if (info->iface == BCRYPT_CIPHER_INTERFACE) {
            if (!wcscmp(pszProperty, BCRYPT_CHAINING_MODE))
                return BcChainingMode(alg, &alg->mode, &alg->message_block, pbInput, cbInput);
            if (!wcscmp(pszProperty, BCRYPT_MESSAGE_BLOCK_LENGTH)) {
                if (alg->mode != MODE_CFB)
                    return STATUS_NOT_SUPPORTED;
                if (cbInput != sizeof(ULONG) || (*(ULONG *)pbInput != 1 && *(ULONG *)pbInput != info->block))
                    return STATUS_INVALID_PARAMETER;
                alg->message_block = *(ULONG *)pbInput;
                return STATUS_SUCCESS;
            }
            return STATUS_NOT_SUPPORTED;
        }
        if ((info->id == ID_ECDH || info->id == ID_ECDSA) && !wcscmp(pszProperty, BCRYPT_ECC_CURVE_NAME)) {
            const MimeEcCurve *curve = BcCurveByName((LPCWSTR)pbInput);
            if (!curve)
                return STATUS_NOT_SUPPORTED;
            alg->curve = curve;
            return STATUS_SUCCESS;
        }
        return STATUS_NOT_SUPPORTED;
    }
    HASH *hash = BcHash(hObject);
    if (hash)
        return BcHashSetProperty(hash, pszProperty, pbInput, cbInput);
    KEY_HEADER *key = BcKey(hObject);
    if (!key)
        return STATUS_INVALID_HANDLE;
    if (!pbInput || dwFlags)
        return STATUS_INVALID_PARAMETER;
    return BcKeySetProperty(key, pszProperty, pbInput, cbInput);
}

/* ------------------------------------------------------------------ what mime does not provide */

/*
 * the configuration of cng contexts and provider registration: mime has one
 * built in provider and no configuration store, so these answer as windows
 * does when nothing is registered.
 */
BCRYPTAPI NTSTATUS WINAPI BCryptAddContextFunction(ULONG table, LPCWSTR context, ULONG iface, LPCWSTR function,
                                                   ULONG position)
{
    return STATUS_NOT_SUPPORTED;
}

BCRYPTAPI NTSTATUS WINAPI BCryptAddContextFunctionProvider(ULONG table, LPCWSTR context, ULONG iface,
                                                           LPCWSTR function, LPCWSTR provider, ULONG position)
{
    return STATUS_NOT_SUPPORTED;
}

BCRYPTAPI NTSTATUS WINAPI BCryptConfigureContext(ULONG table, LPCWSTR context, void *config)
{
    return STATUS_NOT_SUPPORTED;
}

BCRYPTAPI NTSTATUS WINAPI BCryptConfigureContextFunction(ULONG table, LPCWSTR context, ULONG iface,
                                                         LPCWSTR function, void *config)
{
    return STATUS_NOT_SUPPORTED;
}

BCRYPTAPI NTSTATUS WINAPI BCryptCreateContext(ULONG table, LPCWSTR context, void *config)
{
    return STATUS_NOT_SUPPORTED;
}

BCRYPTAPI NTSTATUS WINAPI BCryptDeleteContext(ULONG table, LPCWSTR context)
{
    return STATUS_NOT_FOUND;
}

BCRYPTAPI NTSTATUS WINAPI BCryptEnumContextFunctionProviders(ULONG table, LPCWSTR context, ULONG iface,
                                                             LPCWSTR function, ULONG *size, void **buffer)
{
    return STATUS_NOT_FOUND;
}

BCRYPTAPI NTSTATUS WINAPI BCryptEnumContextFunctions(ULONG table, LPCWSTR context, ULONG iface, ULONG *size,
                                                     void **buffer)
{
    return STATUS_NOT_FOUND;
}

BCRYPTAPI NTSTATUS WINAPI BCryptEnumContexts(ULONG table, ULONG *size, void **buffer)
{
    return STATUS_NOT_FOUND;
}

BCRYPTAPI NTSTATUS WINAPI BCryptEnumRegisteredProviders(ULONG *size, void **buffer)
{
    return STATUS_NOT_FOUND;
}

BCRYPTAPI NTSTATUS WINAPI BCryptQueryContextConfiguration(ULONG table, LPCWSTR context, ULONG *size, void **buffer)
{
    return STATUS_NOT_FOUND;
}

BCRYPTAPI NTSTATUS WINAPI BCryptQueryContextFunctionConfiguration(ULONG table, LPCWSTR context, ULONG iface,
                                                                  LPCWSTR function, ULONG *size, void **buffer)
{
    return STATUS_NOT_FOUND;
}

BCRYPTAPI NTSTATUS WINAPI BCryptQueryContextFunctionProperty(ULONG table, LPCWSTR context, ULONG iface,
                                                             LPCWSTR function, LPCWSTR property, ULONG *size,
                                                             PUCHAR *buffer)
{
    return STATUS_NOT_FOUND;
}

BCRYPTAPI NTSTATUS WINAPI BCryptQueryProviderRegistration(LPCWSTR provider, ULONG mode, ULONG iface, ULONG *size,
                                                          void **buffer)
{
    return STATUS_NOT_FOUND;
}

BCRYPTAPI NTSTATUS WINAPI BCryptRegisterConfigChangeNotify(HANDLE *event)
{
    if (!event)
        return STATUS_INVALID_PARAMETER;
    *event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    return *event ? STATUS_SUCCESS : STATUS_NO_MEMORY;
}

BCRYPTAPI NTSTATUS WINAPI BCryptUnregisterConfigChangeNotify(HANDLE event)
{
    return event && CloseHandle(event) ? STATUS_SUCCESS : STATUS_INVALID_HANDLE;
}

BCRYPTAPI NTSTATUS WINAPI BCryptRegisterProvider(LPCWSTR provider, ULONG flags, void *registration)
{
    return STATUS_ACCESS_DENIED;
}

BCRYPTAPI NTSTATUS WINAPI BCryptUnregisterProvider(LPCWSTR provider)
{
    return STATUS_NOT_FOUND;
}

BCRYPTAPI NTSTATUS WINAPI BCryptRemoveContextFunction(ULONG table, LPCWSTR context, ULONG iface, LPCWSTR function)
{
    return STATUS_NOT_FOUND;
}

BCRYPTAPI NTSTATUS WINAPI BCryptRemoveContextFunctionProvider(ULONG table, LPCWSTR context, ULONG iface,
                                                              LPCWSTR function, LPCWSTR provider)
{
    return STATUS_NOT_FOUND;
}

BCRYPTAPI NTSTATUS WINAPI BCryptResolveProviders(LPCWSTR context, ULONG iface, LPCWSTR function, LPCWSTR provider,
                                                 ULONG mode, ULONG flags, ULONG *size, void **buffer)
{
    return STATUS_NOT_SUPPORTED;
}

BCRYPTAPI NTSTATUS WINAPI BCryptSetAuditingInterface(void *audit)
{
    return STATUS_SUCCESS;
}

BCRYPTAPI NTSTATUS WINAPI BCryptSetContextFunctionProperty(ULONG table, LPCWSTR context, ULONG iface,
                                                           LPCWSTR function, LPCWSTR property, ULONG size,
                                                           PUCHAR value)
{
    return STATUS_NOT_SUPPORTED;
}

/* multiple hashes at once, and the post-quantum key encapsulation. */
BCRYPTAPI NTSTATUS WINAPI BCryptCreateMultiHash(BCRYPT_ALG_HANDLE alg, BCRYPT_HASH_HANDLE *hash, ULONG count,
                                                PUCHAR object, ULONG size, PUCHAR secret, ULONG secret_size,
                                                ULONG flags)
{
    return BcAlg(alg) ? STATUS_NOT_SUPPORTED : STATUS_INVALID_HANDLE;
}

BCRYPTAPI NTSTATUS WINAPI BCryptProcessMultiOperations(BCRYPT_HANDLE handle, ULONG type, void *operations,
                                                       ULONG size, ULONG flags)
{
    return STATUS_NOT_SUPPORTED;
}

BCRYPTAPI NTSTATUS WINAPI BCryptEncapsulate(BCRYPT_KEY_HANDLE key, PUCHAR secret, ULONG secret_size, ULONG *secret_result,
                                            PUCHAR ciphertext, ULONG ciphertext_size, ULONG *ciphertext_result,
                                            ULONG flags)
{
    return BcKey(key) ? STATUS_NOT_SUPPORTED : STATUS_INVALID_HANDLE;
}

BCRYPTAPI NTSTATUS WINAPI BCryptDecapsulate(BCRYPT_KEY_HANDLE key, PUCHAR ciphertext, ULONG ciphertext_size,
                                            PUCHAR secret, ULONG secret_size, ULONG *result, ULONG flags)
{
    return BcKey(key) ? STATUS_NOT_SUPPORTED : STATUS_INVALID_HANDLE;
}
