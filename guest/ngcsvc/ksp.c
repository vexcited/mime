#include "ngcsvc_private.h"
#include "ngcsvc/ngcsvc.h"

#define NCRYPT_SILENT_FLAG_ 0x40
#define NCRYPT_DO_NOT_FINALIZE_FLAG_ 0x400
#define NCRYPT_AUTHORITY_KEY_FLAG_ 0x100
#define NCRYPT_PREFER_VBS_FLAG_ 0x20000
#define NCRYPT_REQUIRE_VBS_FLAG_ 0x40000
#define NCRYPTBUFFER_PKCS_KEY_NAME_ 45
#define NCRYPT_CLAIM_AUTHORITY_AND_SUBJECT_ 2
#define NTE_INVALID_HANDLE_ ((LONG)0x80090026)
#define RSA_PUBLIC_MAGIC 0x31415352
#define RSA_FULL_PRIVATE_MAGIC 0x33415352
#define KEY_BITS 2048
#define SOFTWARE_PROVIDER L"Microsoft Software Key Storage Provider"
#define PLATFORM_PROVIDER L"Microsoft Platform Crypto Provider"
#define PROVIDER_NAME L"Microsoft Passport Key Storage Provider"

NCRYPTAPI SECURITY_STATUS WINAPI NCryptImportKey(NCRYPT_PROV_HANDLE provider, NCRYPT_KEY_HANDLE import_key, LPCWSTR type,
                                                 void *parameters, NCRYPT_KEY_HANDLE *key, PBYTE data, DWORD size,
                                                 DWORD flags);
NCRYPTAPI SECURITY_STATUS WINAPI NCryptExportKey(NCRYPT_KEY_HANDLE key, NCRYPT_KEY_HANDLE export_key, LPCWSTR type,
                                                 void *parameters, PBYTE output, DWORD size, DWORD *result, DWORD flags);
NCRYPTAPI SECURITY_STATUS WINAPI NCryptVerifyClaim(NCRYPT_KEY_HANDLE subject, NCRYPT_KEY_HANDLE authority, DWORD type,
                                                   void *parameters, PBYTE claim, DWORD size, void *output, DWORD flags);

typedef struct RSA_BLOB {
    ULONG Magic, BitLength, cbPublicExp, cbModulus, cbPrime1, cbPrime2;
} RSA_BLOB;

typedef struct PROVIDER {
    LONG References, Keys;
    ULONGLONG Window;
    BOOLEAN WindowSet, WindowShort;
} PROVIDER;

/*
 * the only key an account without windows hello can hold: NgcEphemeralKey of ngcsvc.dll, a key without a name of the
 * software provider, which the service opens for itself. a named key lives in a container of the account, and there
 * is none.
 */
typedef struct KEY {
    PROVIDER *Provider;
    NCRYPT_PROV_HANDLE Software;
    NCRYPT_KEY_HANDLE Key;
    DWORD Usage;
    BOOLEAN Silent, Created, Finalized;
} KEY;

static CRITICAL_SECTION g_lock;

void NgcKspInit(void)
{
    static LONG once;
    if (!InterlockedExchange(&once, 1))
        InitializeCriticalSection(&g_lock);
}

void *WINAPI MIDL_user_allocate(size_t size)
{
    return LocalAlloc(LMEM_FIXED, size);
}

void WINAPI MIDL_user_free(void *p)
{
    LocalFree(p);
}

/* the service tells a failure by an exception, as SecurityStatusErrorContract of ngcsvc.dll under its stubs. */
static long leave(LONG status)
{
    LeaveCriticalSection(&g_lock);
    if (status < 0)
        RpcRaiseException((RPC_STATUS)status);
    return status;
}

static BOOL cancelled(PRPC_ASYNC_STATE async, handle_t h)
{
    if (RpcServerTestCancel(h))
        return FALSE;
    RpcAsyncAbortCall(async, ERROR_CANCELLED);
    return TRUE;
}

static void complete(PRPC_ASYNC_STATE async, LONG status)
{
    LeaveCriticalSection(&g_lock);
    if (status < 0)
        RpcAsyncAbortCall(async, (ULONG)status);
    else
        RpcAsyncCompleteCall(async, &status);
}

static LONG export_data(const void *data, DWORD size, BYTE *output, DWORD room, unsigned long *result)
{
    *result = size;
    if (!output)
        return 0;
    if (size > room)
        return NTE_BUFFER_TOO_SMALL;
    memcpy(output, data, size);
    return 0;
}

static BOOL sized(const void *buffer, DWORD size)
{
    return buffer ? size != 0 : size == 0;
}

static void release_provider(PROVIDER *p)
{
    if (!--p->References)
        LocalFree(p);
}

static void free_key(KEY *k)
{
    if (k->Key)
        NCryptFreeObject(k->Key);
    if (k->Software)
        NCryptFreeObject(k->Software);
    if (k->Provider) {
        k->Provider->Keys--;
        release_provider(k->Provider);
    }
    LocalFree(k);
}

static LONG new_key(PROVIDER *p, BOOL silent, KEY **key)
{
    KEY *k = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, sizeof(*k));
    if (!k)
        return NTE_NO_MEMORY;
    k->Silent = silent != 0;
    LONG status = NCryptOpenStorageProvider(&k->Software, SOFTWARE_PROVIDER, 0);
    if (status < 0) {
        k->Software = 0;
        free_key(k);
        return status;
    }
    *key = k;
    return 0;
}

static void keep_key(PROVIDER *p, KEY *k)
{
    k->Provider = p;
    p->References++;
    p->Keys++;
}

/*
 * what a named key answers on an account without a container, in the order of NgcUserIdKey::Create, Open, Import
 * and NgcAuthorityKey::Open: the name, the caller's right to it, then for a new key that it names no container but a
 * domain and a user, then the algorithm. an authority key may be another user's.
 */
static LONG named_key(const WCHAR *text, BOOL fresh, BOOL authority, const WCHAR *algorithm)
{
    static const WCHAR *const algorithms[] = {L"RSA",        L"ECDSA_P256", L"ECDSA_P384", L"ECDSA_P521",
                                              L"ECDH_P256", L"ECDH_P384",  L"ECDH_P521"};
    static const GUID none;
    NGC_KEY_NAME name;
    BOOL other = FALSE;
    LONG status = NgcParseKeyName(text, &name);
    if (status < 0)
        return NgcStatusFromHresult(status);
    status = NgcCheckKeyAccess(&name, &other);
    if (status < 0 && !(authority && other))
        goto done;
    status = NTE_BAD_KEYSET;
    if (fresh) {
        if (memcmp(&name.Container, &none, sizeof(none)) || !name.Domain || !name.User)
            status = NTE_INVALID_PARAMETER;
        else if (algorithm) {
            size_t i = 0;
            while (i < RTL_NUMBER_OF(algorithms) && _wcsicmp(algorithm, algorithms[i]))
                i++;
            if (i == RTL_NUMBER_OF(algorithms))
                status = NTE_INVALID_PARAMETER;
        }
    }
done:
    NgcFreeKeyName(&name);
    return status;
}

long WINAPI NgcKsp_OpenStorageProvider_s(handle_t h, NGC_KSP_PROVIDER *phProvider)
{
    PROVIDER *p = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, sizeof(*p));
    if (!p)
        RpcRaiseException((RPC_STATUS)NTE_NO_MEMORY);
    p->References = 1;
    *phProvider = p;
    return 0;
}

long WINAPI NgcKsp_EnumKeys_s(handle_t h, NGC_KSP_PROVIDER hProvider, unsigned short *pszScope,
                              NGC_KSP_RPC_KEY_NAME **ppKeyName, NGC_KSP_ENUM_STATE *ppEnumState, unsigned long dwFlags)
{
    *ppKeyName = nullptr;
    if (dwFlags & ~NCRYPT_SILENT_FLAG_)
        RpcRaiseException((RPC_STATUS)NTE_BAD_FLAGS);
    RpcRaiseException((RPC_STATUS)(pszScope ? NTE_INVALID_PARAMETER : NTE_NO_MORE_ITEMS));
}

long WINAPI NgcKsp_FreeBuffer_s(handle_t h, NGC_KSP_ENUM_STATE *ppEnumState)
{
    if (!*ppEnumState)
        RpcRaiseException((RPC_STATUS)NTE_INVALID_PARAMETER);
    LocalFree(*ppEnumState);
    *ppEnumState = nullptr;
    return 0;
}

long WINAPI NgcKsp_FreeProvider_s(handle_t h, NGC_KSP_PROVIDER *phProvider)
{
    if (!*phProvider)
        RpcRaiseException((RPC_STATUS)NTE_INVALID_PARAMETER);
    EnterCriticalSection(&g_lock);
    release_provider(*phProvider);
    *phProvider = nullptr;
    return leave(0);
}

long WINAPI NgcKsp_FreeKey_s(handle_t h, NGC_KSP_KEY *phKey)
{
    if (!*phKey)
        RpcRaiseException((RPC_STATUS)NTE_INVALID_PARAMETER);
    EnterCriticalSection(&g_lock);
    free_key(*phKey);
    *phKey = nullptr;
    return leave(0);
}

long WINAPI NgcKsp_OpenKey_s(handle_t h, NGC_KSP_PROVIDER hProvider, NGC_KSP_KEY *phKey, unsigned short *pszKeyName,
                             unsigned long dwLegacyKeySpec, unsigned long dwFlags)
{
    *phKey = nullptr;
    if (dwLegacyKeySpec)
        RpcRaiseException((RPC_STATUS)NTE_INVALID_PARAMETER);
    if (dwFlags & ~(NCRYPT_SILENT_FLAG_ | NCRYPT_AUTHORITY_KEY_FLAG_ | 0x20))
        RpcRaiseException((RPC_STATUS)NTE_BAD_FLAGS);
    EnterCriticalSection(&g_lock);
    return leave(named_key(pszKeyName, FALSE, (dwFlags & NCRYPT_AUTHORITY_KEY_FLAG_) != 0, nullptr));
}

/* NgcEphemeralKey::Create: an rsa key of 2048 bits for signing, in the software provider. */
static LONG create_key(PROVIDER *p, const WCHAR *algorithm, BOOL silent, KEY **key)
{
    KEY *k = nullptr;
    LONG status = new_key(p, silent, &k);
    if (status < 0)
        return NgcStatusFromHresult(status);
    DWORD bits = KEY_BITS, usage = 2;
    if (_wcsicmp(algorithm, L"RSA"))
        status = NTE_INVALID_PARAMETER;
    else
        status = NCryptCreatePersistedKey(k->Software, &k->Key, algorithm, nullptr, 0, silent ? NCRYPT_SILENT_FLAG_ : 0);
    if (status >= 0)
        status = NCryptSetProperty(k->Key, L"Length", (BYTE *)&bits, sizeof(bits), 0);
    if (status >= 0)
        status = NCryptSetProperty(k->Key, L"Key Usage", (BYTE *)&usage, sizeof(usage), 0);
    if (status < 0) {
        free_key(k);
        return status;
    }
    k->Usage = usage;
    k->Created = TRUE;
    keep_key(p, k);
    *key = k;
    return 0;
}

long WINAPI NgcKsp_CreatePersistedKey_s(handle_t h, NGC_KSP_PROVIDER hProvider, NGC_KSP_KEY *phKey,
                                        unsigned short *pszAlgId, unsigned short *pszKeyName,
                                        unsigned long dwLegacyKeySpec, unsigned long dwFlags)
{
    *phKey = nullptr;
    if (dwLegacyKeySpec)
        RpcRaiseException((RPC_STATUS)NTE_INVALID_PARAMETER);
    if (dwFlags & ~(0x20 | NCRYPT_SILENT_FLAG_ | 0x80 | NCRYPT_PREFER_VBS_FLAG_ | NCRYPT_REQUIRE_VBS_FLAG_))
        RpcRaiseException((RPC_STATUS)NTE_BAD_FLAGS);
    EnterCriticalSection(&g_lock);
    if (pszKeyName)
        return leave(named_key(pszKeyName, TRUE, FALSE, pszAlgId));
    return leave(create_key(hProvider, pszAlgId, dwFlags & NCRYPT_SILENT_FLAG_, (KEY **)phKey));
}

long WINAPI NgcKsp_GetProviderProperty_s(handle_t h, NGC_KSP_PROVIDER hProvider, unsigned short *pszProperty,
                                         byte *pbOutput, unsigned long cbOutput, unsigned long *pcbResult,
                                         unsigned long dwFlags)
{
    PROVIDER *p = hProvider;
    if (!sized(pbOutput, cbOutput))
        RpcRaiseException((RPC_STATUS)NTE_INVALID_PARAMETER);
    if (dwFlags & ~NCRYPT_SILENT_FLAG_)
        RpcRaiseException((RPC_STATUS)NTE_BAD_FLAGS);
    EnterCriticalSection(&g_lock);
    DWORD number = 0;
    LONG status = NTE_NOT_SUPPORTED;
    if (!_wcsicmp(pszProperty, L"HWND Handle")) {
        /* a window given as 4 bytes comes back as 4. */
        if (!p->WindowSet)
            status = NTE_NO_KEY;
        else
            status = export_data(&p->Window, p->WindowShort ? 4 : 8, pbOutput, cbOutput, pcbResult);
        return leave(status);
    }
    if (!_wcsicmp(pszProperty, L"Version"))
        number = 0x10000;
    else if (!_wcsicmp(pszProperty, L"Impl Type"))
        number = 3;
    else if (!_wcsicmp(pszProperty, L"Max Name Length"))
        number = NGC_MAX_NAME;
    else if (!_wcsicmp(pszProperty, L"Name"))
        return leave(export_data(PROVIDER_NAME, sizeof(PROVIDER_NAME), pbOutput, cbOutput, pcbResult));
    else if (!_wcsicmp(pszProperty, L"NgcContainerSecureId"))
        return leave((LONG)0x80070490);
    else
        return leave(status);
    return leave(export_data(&number, sizeof(number), pbOutput, cbOutput, pcbResult));
}

long WINAPI NgcKsp_SetProviderProperty_s(handle_t h, NGC_KSP_PROVIDER hProvider, unsigned short *pszProperty,
                                         byte *pbInput, unsigned long cbInput, unsigned long dwFlags)
{
    PROVIDER *p = hProvider;
    if (!pbInput || cbInput - 1 > 0xFFFFF)
        RpcRaiseException((RPC_STATUS)NTE_INVALID_PARAMETER);
    if (dwFlags & ~NCRYPT_SILENT_FLAG_)
        RpcRaiseException((RPC_STATUS)NTE_BAD_FLAGS);
    if (!_wcsicmp(pszProperty, L"PinCacheFreeApplicationTicket") || !_wcsicmp(pszProperty, L"PinCacheFlags") ||
        !_wcsicmp(pszProperty, L"Use Context"))
        return 0;
    if (_wcsicmp(pszProperty, L"HWND Handle"))
        RpcRaiseException((RPC_STATUS)NTE_NOT_SUPPORTED);
    if (cbInput != 4 && cbInput != 8)
        RpcRaiseException((RPC_STATUS)NTE_BAD_DATA);
    EnterCriticalSection(&g_lock);
    p->Window = cbInput == 4 ? (ULONGLONG)(LONGLONG) * (LONG *)pbInput : *(ULONGLONG *)pbInput;
    p->WindowShort = cbInput == 4;
    p->WindowSet = TRUE;
    /* the window is handed to every key of the provider and the answer of the last one is returned: a key without a
     * name does not take it. */
    return leave(p->Keys ? NTE_NOT_SUPPORTED : 0);
}

/* NgcKspKey::DefaultGetProperty: the same for every key, whatever was imported. */
static LONG key_property(KEY *k, const WCHAR *name, BYTE *output, DWORD room, unsigned long *result)
{
    DWORD numbers[4] = {0};
    if (!_wcsicmp(name, L"Export Policy"))
        return export_data(numbers, 4, output, room, result);
    if (!_wcsicmp(name, L"Algorithm Group") || !_wcsicmp(name, L"Algorithm Name"))
        return export_data(L"RSA", sizeof(L"RSA"), output, room, result);
    if (!_wcsicmp(name, L"Key Usage"))
        return export_data(&k->Usage, 4, output, room, result);
    numbers[0] = KEY_BITS;
    if (!_wcsicmp(name, L"Length"))
        return export_data(numbers, 4, output, room, result);
    if (_wcsicmp(name, L"Lengths"))
        return NTE_NOT_SUPPORTED;
    numbers[1] = numbers[3] = KEY_BITS;
    return export_data(numbers, sizeof(numbers), output, room, result);
}

long WINAPI NgcKsp_GetKeyProperty_s(handle_t h, NGC_KSP_KEY hKey, unsigned short *pszProperty, byte *pbOutput,
                                    unsigned long cbOutput, unsigned long *pcbResult, unsigned long dwFlags)
{
    if (!sized(pbOutput, cbOutput))
        RpcRaiseException((RPC_STATUS)NTE_INVALID_PARAMETER);
    unsigned long flags = dwFlags & ~NCRYPT_SILENT_FLAG_;
    BOOL bad;
    if (!_wcsicmp(pszProperty, L"NgcGestureRetryCount") || !_wcsicmp(pszProperty, L"NgcDeviceWipeRetryCount") ||
        !_wcsicmp(pszProperty, L"NgcSoftLockoutInfo") || !_wcsicmp(pszProperty, L"NgcGestureCharacteristics"))
        bad = flags != 1;
    else if (!_wcsicmp(pszProperty, L"Security Descr"))
        bad = (flags & ~0xFul) != 0;
    else
        bad = flags != 0;
    if (bad)
        RpcRaiseException((RPC_STATUS)NTE_BAD_FLAGS);
    EnterCriticalSection(&g_lock);
    return leave(key_property(hKey, pszProperty, pbOutput, cbOutput, pcbResult));
}

long WINAPI NgcKsp_SetKeyProperty_s(handle_t h, NGC_KSP_KEY hKey, unsigned short *pszProperty, byte *pbInput,
                                    unsigned long cbInput, unsigned long dwFlags)
{
    if (cbInput > 0x100000)
        RpcRaiseException((RPC_STATUS)NTE_INVALID_PARAMETER);
    if (dwFlags & ~NCRYPT_SILENT_FLAG_)
        RpcRaiseException((RPC_STATUS)NTE_BAD_FLAGS);
    RpcRaiseException((RPC_STATUS)NTE_NOT_SUPPORTED);
}

static LONG finalize_key(KEY *k, BOOL silent)
{
    if (!k->Created || k->Finalized || !k->Key)
        return NTE_BAD_KEY_STATE;
    LONG status = NCryptFinalizeKey(k->Key, silent || k->Silent ? NCRYPT_SILENT_FLAG_ : 0);
    if (status >= 0)
        k->Finalized = TRUE;
    return status;
}

void WINAPI NgcKsp_FinalizeKey_s(PRPC_ASYNC_STATE async, handle_t h, NGC_KSP_KEY hKey, unsigned long dwFlags)
{
    if (cancelled(async, h))
        return;
    EnterCriticalSection(&g_lock);
    complete(async, dwFlags & ~NCRYPT_SILENT_FLAG_ ? NTE_BAD_FLAGS : finalize_key(hKey, dwFlags & NCRYPT_SILENT_FLAG_));
}

void WINAPI NgcKsp_Decrypt_s(PRPC_ASYNC_STATE async, handle_t h, NGC_KSP_KEY hKey, byte *pbInput, unsigned long cbInput,
                             NGC_RPC_PADDING_INFO *pPaddingInfo, byte *pbOutput, unsigned long cbOutput,
                             unsigned long *pcbResult, unsigned long dwFlags)
{
    if (cancelled(async, h))
        return;
    EnterCriticalSection(&g_lock);
    LONG status = NTE_NOT_SUPPORTED;
    if (pPaddingInfo || !pbInput || !cbInput || !sized(pbOutput, cbOutput))
        status = NTE_INVALID_PARAMETER;
    else if ((dwFlags & ~NCRYPT_SILENT_FLAG_) != 2)
        status = NTE_BAD_FLAGS;
    else if (!pbOutput) {
        *pcbResult = cbInput;
        status = 0;
    }
    complete(async, status);
}

static DWORD hash_length(const WCHAR *algorithm)
{
    static const struct {
        const WCHAR *Name;
        DWORD Length;
    } hashes[] = {{L"SHA1", 20}, {L"SHA256", 32}, {L"SHA384", 48}, {L"SHA512", 64}};
    for (size_t i = 0; i < RTL_NUMBER_OF(hashes); i++)
        if (!_wcsicmp(algorithm, hashes[i].Name))
            return hashes[i].Length;
    return 0;
}

void WINAPI NgcKsp_SignHash_s(PRPC_ASYNC_STATE async, handle_t h, NGC_KSP_KEY hKey, NGC_RPC_PADDING_INFO *pPaddingInfo,
                              byte *pbHashValue, unsigned long cbHashValue, byte *pbSignature, unsigned long cbSignature,
                              unsigned long *pcbResult, unsigned long dwFlags)
{
    if (cancelled(async, h))
        return;
    EnterCriticalSection(&g_lock);
    unsigned long flags = dwFlags & ~NCRYPT_SILENT_FLAG_;
    LONG status = NTE_NOT_SUPPORTED;
    if (!pbHashValue || !cbHashValue || !sized(pbSignature, cbSignature))
        status = NTE_INVALID_PARAMETER;
    else if (flags != 2 && flags && !(dwFlags & 0x90008))
        status = NTE_BAD_FLAGS;
    else if (!pbSignature) {
        *pcbResult = KEY_BITS / 8;
        status = 0;
    } else if (pPaddingInfo && pPaddingInfo->pszAlgId) {
        DWORD length = hash_length(pPaddingInfo->pszAlgId);
        if (length && (dwFlags & 0xA) && length != cbHashValue)
            status = NTE_INVALID_PARAMETER;
    }
    complete(async, status);
}

long WINAPI NgcKsp_DeleteKey_s(handle_t h, NGC_KSP_KEY *phKey, unsigned long dwFlags)
{
    if (!*phKey)
        RpcRaiseException((RPC_STATUS)NTE_INVALID_PARAMETER);
    if (dwFlags & ~NCRYPT_SILENT_FLAG_)
        RpcRaiseException((RPC_STATUS)NTE_BAD_FLAGS);
    RpcRaiseException((RPC_STATUS)NTE_NOT_SUPPORTED);
}

/* ConvertPrivateBlobToRsaFullKey: the software provider reads the blob, whatever its kind, and gives the full one. */
static LONG full_private_blob(const WCHAR *type, NGC_RPC_BUFFER_DESC *parameters, BYTE *data, DWORD size, DWORD flags,
                              BYTE **blob, DWORD *blob_size)
{
    NCRYPT_PROV_HANDLE provider = 0;
    NCRYPT_KEY_HANDLE key = 0;
    DWORD policy = 3, needed = 0;
    BYTE *full = nullptr;
    LONG status = NCryptOpenStorageProvider(&provider, SOFTWARE_PROVIDER, 0);
    if (status < 0)
        return status;
    status = NCryptImportKey(provider, 0, type, parameters, &key, data, size, flags | NCRYPT_DO_NOT_FINALIZE_FLAG_);
    if (status < 0)
        key = 0;
    if (status >= 0)
        status = NCryptSetProperty(key, L"Export Policy", (BYTE *)&policy, sizeof(policy), 0);
    if (status >= 0)
        status = NCryptFinalizeKey(key, 0);
    if (status >= 0)
        status = NCryptExportKey(key, 0, L"RSAFULLPRIVATEBLOB", nullptr, nullptr, 0, &needed, 0);
    if (status >= 0 && !(full = LocalAlloc(LMEM_FIXED, needed)))
        status = NTE_NO_MEMORY;
    if (status >= 0)
        status = NCryptExportKey(key, 0, L"RSAFULLPRIVATEBLOB", nullptr, full, needed, &needed, 0);
    if (status >= 0 && (needed < sizeof(RSA_BLOB) || ((RSA_BLOB *)full)->Magic != RSA_FULL_PRIVATE_MAGIC))
        status = NTE_INVALID_PARAMETER;
    if (key)
        NCryptFreeObject(key);
    NCryptFreeObject(provider);
    if (status < 0) {
        if (full)
            LocalFree(full);
        return status;
    }
    *blob = full;
    *blob_size = needed;
    return 0;
}

static BOOL private_type(const WCHAR *type)
{
    static const WCHAR *const types[] = {L"PRIVATEBLOB",     L"RSAPRIVATEBLOB", L"RSAFULLPRIVATEBLOB",
                                         L"CAPIPRIVATEBLOB", L"PKCS7_ENVELOPE", L"PKCS8_PRIVATEKEY"};
    for (size_t i = 0; i < RTL_NUMBER_OF(types); i++)
        if (!_wcsicmp(type, types[i]))
            return TRUE;
    return FALSE;
}

/*
 * the import of the service: a private rsa key or a public rsa key of 2048 bits, or a public elliptic key, each
 * without a name. the name of a key comes as a buffer of the parameter list, once, and the other buffers go on to the
 * software provider.
 */
static LONG import_key(PROVIDER *p, const WCHAR *type, NGC_RPC_BUFFER_DESC *parameters, BYTE *data, DWORD size,
                       DWORD flags, KEY **key)
{
    const WCHAR *name = nullptr;
    NGC_RPC_BUFFER_DESC rest = {0};
    BYTE *blob = nullptr;
    KEY *k = nullptr;
    BOOL silent = flags & NCRYPT_SILENT_FLAG_;
    LONG status = 0;
    if (parameters && parameters->cBuffers) {
        if (!parameters->pBuffers)
            return NTE_INVALID_PARAMETER;
        rest.ulVersion = parameters->ulVersion;
        rest.pBuffers = LocalAlloc(LMEM_FIXED, parameters->cBuffers * sizeof(NGC_RPC_BUFFER));
        if (!rest.pBuffers)
            return NTE_NO_MEMORY;
        for (ULONG i = 0; i < parameters->cBuffers && status >= 0; i++) {
            NGC_RPC_BUFFER *b = &parameters->pBuffers[i];
            if (b->BufferType != NCRYPTBUFFER_PKCS_KEY_NAME_)
                rest.pBuffers[rest.cBuffers++] = *b;
            else if (name || !b->pvBuffer || !b->cbBuffer)
                status = NTE_INVALID_PARAMETER;
            else
                name = (const WCHAR *)b->pvBuffer;
        }
        if (status < 0)
            goto done;
    }
    if (private_type(type)) {
        DWORD blob_size = size;
        if (size >= 4 && *(ULONG *)data == RSA_FULL_PRIVATE_MAGIC)
            blob = data;
        else if ((status = full_private_blob(type, rest.cBuffers ? &rest : nullptr, data, size, flags, &blob,
                                             &blob_size)) < 0)
            goto done;
        if (blob_size < sizeof(RSA_BLOB) || ((RSA_BLOB *)blob)->cbModulus * 8 != KEY_BITS)
            status = NTE_NOT_SUPPORTED;
        else if (name)
            status = named_key(name, TRUE, FALSE, nullptr);
        else if ((status = NgcStatusFromHresult(new_key(p, silent, &k))) >= 0) {
            DWORD usage = 2;
            status = NCryptImportKey(k->Software, 0, L"RSAFULLPRIVATEBLOB", nullptr, &k->Key, blob, blob_size,
                                     NCRYPT_DO_NOT_FINALIZE_FLAG_ | (k->Silent ? NCRYPT_SILENT_FLAG_ : 0));
            if (status < 0)
                k->Key = 0;
            else
                status = NCryptSetProperty(k->Key, L"Key Usage", (BYTE *)&usage, sizeof(usage), 0);
            k->Usage = usage;
            k->Created = TRUE;
            if (status >= 0 && !(flags & NCRYPT_DO_NOT_FINALIZE_FLAG_))
                status = finalize_key(k, silent);
        }
        if (blob == data)
            blob = nullptr;
    } else if (!_wcsicmp(type, L"RSAPUBLICBLOB")) {
        RSA_BLOB *rsa = (RSA_BLOB *)data;
        if (name)
            status = NTE_BAD_TYPE;
        else if (flags & NCRYPT_DO_NOT_FINALIZE_FLAG_)
            status = NTE_BAD_FLAGS;
        else if (size < sizeof(*rsa))
            status = (LONG)0x8007007A;
        else if (rsa->Magic != RSA_PUBLIC_MAGIC)
            status = NGC_E_INVALIDARG;
        else if ((ULONGLONG)rsa->cbPublicExp + rsa->cbModulus + sizeof(*rsa) > size)
            status = (LONG)0x8007007A;
        else if (rsa->cbModulus * 8 != KEY_BITS)
            status = NTE_NOT_SUPPORTED;
        else if ((status = NgcStatusFromHresult(new_key(p, silent, &k))) >= 0) {
            /* the key is rebuilt from the exponent and the modulus alone. */
            DWORD blob_size = sizeof(*rsa) + rsa->cbPublicExp + rsa->cbModulus;
            if (!(blob = LocalAlloc(LMEM_FIXED, blob_size)))
                status = NTE_NO_MEMORY;
            else {
                RSA_BLOB head = {RSA_PUBLIC_MAGIC, KEY_BITS, rsa->cbPublicExp, rsa->cbModulus, 0, 0};
                memcpy(blob, &head, sizeof(head));
                memcpy(blob + sizeof(head), rsa + 1, blob_size - sizeof(head));
                status = NCryptImportKey(k->Software, 0, L"RSAPUBLICBLOB", nullptr, &k->Key, blob, blob_size,
                                         k->Silent ? NCRYPT_SILENT_FLAG_ : 0);
                if (status < 0)
                    k->Key = 0;
            }
            k->Usage = 2;
            k->Created = k->Finalized = TRUE;
        }
    } else if (!_wcsicmp(type, L"ECCPUBLICBLOB")) {
        if (flags & NCRYPT_DO_NOT_FINALIZE_FLAG_)
            status = NTE_BAD_FLAGS;
        else if (name)
            status = NTE_NOT_SUPPORTED;
        else if ((status = NgcStatusFromHresult(new_key(p, silent, &k))) >= 0) {
            status = NCryptImportKey(k->Software, 0, type, nullptr, &k->Key, data, size, flags);
            if (status < 0)
                k->Key = 0;
            k->Created = k->Finalized = TRUE;
        }
    } else {
        status = NTE_BAD_TYPE;
    }
    if (status >= 0 && k) {
        keep_key(p, k);
        *key = k;
        k = nullptr;
    }
done:
    if (k)
        free_key(k);
    if (blob)
        LocalFree(blob);
    if (rest.pBuffers)
        LocalFree(rest.pBuffers);
    return status;
}

long WINAPI NgcKsp_ImportKey_s(handle_t h, NGC_KSP_PROVIDER hProvider, unsigned short *pszBlobType,
                               NGC_RPC_BUFFER_DESC *pParameterList, NGC_KSP_KEY *phKey, byte *pbData,
                               unsigned long cbData, unsigned long dwFlags)
{
    *phKey = nullptr;
    if (!pbData || !cbData)
        RpcRaiseException((RPC_STATUS)NTE_INVALID_PARAMETER);
    if (dwFlags & ~(0x20 | NCRYPT_SILENT_FLAG_ | 0x80 | NCRYPT_DO_NOT_FINALIZE_FLAG_))
        RpcRaiseException((RPC_STATUS)NTE_BAD_FLAGS);
    EnterCriticalSection(&g_lock);
    return leave(import_key(hProvider, pszBlobType, pParameterList, pbData, cbData, dwFlags, (KEY **)phKey));
}

/* NgcEphemeralKey::ExportPublicKey: what the software provider gives for the key. */
static LONG public_blob(KEY *k, const WCHAR *type, BYTE **blob, DWORD *size)
{
    if (!k->Key)
        return NTE_BAD_KEY_STATE;
    LONG status = NCryptExportKey(k->Key, 0, type, nullptr, nullptr, 0, size, 0);
    if (status < 0)
        return status;
    if (!(*blob = LocalAlloc(LMEM_FIXED, *size ? *size : 1)))
        return NTE_NO_MEMORY;
    status = NCryptExportKey(k->Key, 0, type, nullptr, *blob, *size, size, 0);
    if (status < 0) {
        LocalFree(*blob);
        *blob = nullptr;
    }
    return status;
}

void WINAPI NgcKsp_ExportKey_s(PRPC_ASYNC_STATE async, handle_t h, NGC_KSP_KEY hKey, unsigned short *pszBlobType,
                               byte *pbOutput, unsigned long cbOutput, unsigned long *pcbResult, unsigned long dwFlags)
{
    if (cancelled(async, h))
        return;
    EnterCriticalSection(&g_lock);
    BYTE *blob = nullptr;
    DWORD size = 0;
    LONG status = NTE_NOT_SUPPORTED;
    if (!sized(pbOutput, cbOutput))
        status = NTE_INVALID_PARAMETER;
    else if (dwFlags & ~NCRYPT_SILENT_FLAG_)
        status = NTE_BAD_FLAGS;
    else if (!_wcsicmp(pszBlobType, L"RSAPUBLICBLOB") && (status = public_blob(hKey, pszBlobType, &blob, &size)) >= 0) {
        status = export_data(blob, size, pbOutput, cbOutput, pcbResult);
        LocalFree(blob);
    }
    complete(async, status);
}

void WINAPI NgcKsp_SecretAgreement_s(PRPC_ASYNC_STATE async, handle_t h, NGC_KSP_KEY hPrivKey, NGC_KSP_KEY hPubKey,
                                     NGC_KSP_SECRET *phSecret, unsigned long dwFlags)
{
    if (cancelled(async, h))
        return;
    EnterCriticalSection(&g_lock);
    *phSecret = nullptr;
    complete(async, dwFlags & ~NCRYPT_SILENT_FLAG_ ? NTE_BAD_FLAGS : NTE_NOT_SUPPORTED);
}

/* no key agrees on a secret, so no secret handle exists and these two are never reached with one. */
void WINAPI NgcKsp_DeriveKey_s(PRPC_ASYNC_STATE async, handle_t h, NGC_KSP_SECRET hSecret, unsigned short *pwszKDF,
                               NGC_RPC_BUFFER_DESC *pParameterList, byte *pbDerivedKey, unsigned long cbDerivedKey,
                               unsigned long *pcbResult, unsigned long dwFlags)
{
    if (cancelled(async, h))
        return;
    EnterCriticalSection(&g_lock);
    complete(async, hSecret ? NTE_INVALID_HANDLE_ : NTE_INVALID_PARAMETER);
}

void WINAPI NgcKsp_FreeSecret_s(PRPC_ASYNC_STATE async, handle_t h, NGC_KSP_SECRET *phSecret)
{
    if (cancelled(async, h))
        return;
    EnterCriticalSection(&g_lock);
    LONG status = *phSecret ? 0 : NTE_INVALID_PARAMETER;
    if (*phSecret)
        LocalFree(*phSecret);
    *phSecret = nullptr;
    complete(async, status);
}

/* NgcKspProvider::CreateClaim wants the subject and the authority to be keys of one user and one container: a key
 * without a name has neither. */
void WINAPI NgcKsp_CreateClaim_s(PRPC_ASYNC_STATE async, handle_t h, NGC_KSP_PROVIDER hProvider, NGC_KSP_KEY hSubjectKey,
                                 NGC_KSP_KEY hAuthorityKey, NGC_RPC_BUFFER_DESC *pParameterList, byte *pbClaimBlob,
                                 unsigned long cbClaimBlob, unsigned long *pcbResult, unsigned long dwFlags)
{
    if (cancelled(async, h))
        return;
    EnterCriticalSection(&g_lock);
    complete(async, NTE_INVALID_PARAMETER);
}

/*
 * as the service: the public halves of both keys go to the platform provider, which checks the claim. without a tpm
 * that provider does not open.
 */
static LONG verify_claim(KEY *subject, KEY *authority, NGC_RPC_BUFFER_DESC *parameters, BYTE *claim, DWORD size,
                         NGC_RPC_BUFFER_DESC *output, DWORD flags)
{
    BYTE *authority_blob = nullptr, *subject_blob = nullptr;
    DWORD authority_size = 0, subject_size = 0;
    NCRYPT_PROV_HANDLE provider = 0;
    NCRYPT_KEY_HANDLE authority_key = 0, subject_key = 0;
    NGC_RPC_BUFFER_DESC result = {0};
    LONG status = public_blob(authority, L"RSAPUBLICBLOB", &authority_blob, &authority_size);
    if (status >= 0)
        status = public_blob(subject, L"RSAPUBLICBLOB", &subject_blob, &subject_size);
    flags |= 0x1000;
    if (status >= 0 && (status = NCryptOpenStorageProvider(&provider, PLATFORM_PROVIDER, flags)) < 0)
        provider = 0;
    if (status >= 0 && (status = NCryptImportKey(provider, 0, L"RSAPUBLICBLOB", nullptr, &authority_key, authority_blob,
                                                 authority_size, flags)) < 0)
        authority_key = 0;
    if (status >= 0 && (status = NCryptImportKey(provider, 0, L"RSAPUBLICBLOB", nullptr, &subject_key, subject_blob,
                                                 subject_size, flags)) < 0)
        subject_key = 0;
    if (status >= 0)
        status = NCryptVerifyClaim(subject_key, authority_key, NCRYPT_CLAIM_AUTHORITY_AND_SUBJECT_, parameters, claim,
                                   size, &result, flags);
    if (status >= 0) {
        /* the answer of the provider is copied into memory the stub frees. */
        output->ulVersion = result.ulVersion;
        output->pBuffers = result.cBuffers ? MIDL_user_allocate(result.cBuffers * sizeof(NGC_RPC_BUFFER)) : nullptr;
        for (ULONG i = 0; output->pBuffers && i < result.cBuffers; i++) {
            NGC_RPC_BUFFER *from = &result.pBuffers[i], *to = &output->pBuffers[i];
            to->BufferType = from->BufferType;
            to->pvBuffer = from->cbBuffer ? MIDL_user_allocate(from->cbBuffer) : nullptr;
            to->cbBuffer = to->pvBuffer ? from->cbBuffer : 0;
            if (to->pvBuffer)
                memcpy(to->pvBuffer, from->pvBuffer, from->cbBuffer);
            output->cBuffers = i + 1;
        }
        if (result.pBuffers)
            NCryptFreeBuffer(result.pBuffers);
    }
    if (subject_key)
        NCryptFreeObject(subject_key);
    if (authority_key)
        NCryptFreeObject(authority_key);
    if (provider)
        NCryptFreeObject(provider);
    if (subject_blob)
        LocalFree(subject_blob);
    if (authority_blob)
        LocalFree(authority_blob);
    return status;
}

long WINAPI NgcKsp_VerifyClaim_s(handle_t h, NGC_KSP_KEY hSubjectKey, NGC_KSP_KEY hAuthorityKey,
                                 NGC_RPC_BUFFER_DESC *pParameterList, byte *pbClaimBlob, unsigned long cbClaimBlob,
                                 NGC_RPC_BUFFER_DESC *pOutput, unsigned long dwFlags)
{
    memset(pOutput, 0, sizeof(*pOutput));
    if (!pbClaimBlob || !cbClaimBlob)
        RpcRaiseException((RPC_STATUS)NTE_INVALID_PARAMETER);
    if (dwFlags & ~(NCRYPT_SILENT_FLAG_ | 0x1000))
        RpcRaiseException((RPC_STATUS)NTE_BAD_FLAGS);
    EnterCriticalSection(&g_lock);
    return leave(verify_claim(hSubjectKey, hAuthorityKey, pParameterList, pbClaimBlob, cbClaimBlob, pOutput, dwFlags));
}

void WINAPI NgcKsp_AuthenticatedReqResp_s(PRPC_ASYNC_STATE async, handle_t h, NGC_KSP_KEY hKey, byte *pbInput,
                                          unsigned long cbInput, byte *pbOutput, unsigned long cbOutput,
                                          unsigned long *pcbResult, unsigned long dwFlags)
{
    if (cancelled(async, h))
        return;
    EnterCriticalSection(&g_lock);
    complete(async, !pbInput || !cbInput || !sized(pbOutput, cbOutput) ? NTE_INVALID_PARAMETER : NTE_NOT_SUPPORTED);
}

void WINAPI NGC_KSP_PROVIDER_rundown(void *context)
{
    EnterCriticalSection(&g_lock);
    release_provider(context);
    LeaveCriticalSection(&g_lock);
}

void WINAPI NGC_KSP_ENUM_STATE_rundown(void *context)
{
    LocalFree(context);
}

void WINAPI NGC_KSP_KEY_rundown(void *context)
{
    EnterCriticalSection(&g_lock);
    free_key(context);
    LeaveCriticalSection(&g_lock);
}

void WINAPI NGC_KSP_SECRET_rundown(void *context)
{
    LocalFree(context);
}
