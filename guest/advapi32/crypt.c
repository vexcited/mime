/*
 * the cryptographic service provider functions of CryptoAPI.
 *
 * windows routes these through cryptsp.dll to provider dlls such as
 * rsaenh.dll. mime answers for the microsoft providers itself: contexts,
 * random numbers and imported key blobs. key containers are not stored, so
 * opening one always finds it, empty.
 */
#include "advapi32_private.h"

typedef ULONG_PTR HCRYPTKEY;

#define NTE_BAD_UID ((DWORD)0x80090001L)
#define NTE_BAD_KEY ((DWORD)0x80090003L)
#define NTE_BAD_DATA ((DWORD)0x80090005L)
#define NTE_BAD_VER ((DWORD)0x80090007L)
#define NTE_BAD_FLAGS ((DWORD)0x80090009L)
#define NTE_BAD_TYPE ((DWORD)0x8009000AL)
#define NTE_PROV_TYPE_NOT_DEF ((DWORD)0x80090017L)
#define NTE_KEYSET_NOT_DEF ((DWORD)0x80090019L)
#define NTE_PROV_TYPE_NO_MATCH ((DWORD)0x8009001BL)

#define CRYPT_VERIFYCONTEXT 0xF0000000
#define CRYPT_NEWKEYSET 0x00000008
#define CRYPT_DELETEKEYSET 0x00000010
#define CRYPT_MACHINE_KEYSET 0x00000020
#define CRYPT_SILENT 0x00000040
#define CRYPT_DEFAULT_CONTAINER_OPTIONAL 0x00000080

#define PROV_RSA_FULL 1
#define PROV_RSA_SIG 2
#define PROV_DSS 3
#define PROV_RSA_SCHANNEL 12
#define PROV_DSS_DH 13
#define PROV_DH_SCHANNEL 18
#define PROV_RSA_AES 24

#define SIMPLEBLOB 0x1
#define PUBLICKEYBLOB 0x6
#define PRIVATEKEYBLOB 0x7
#define PLAINTEXTKEYBLOB 0x8

#define PROV_MAGIC 0x564F5250 /* "PROV" */
#define KEY_MAGIC 0x5945484B  /* "KHEY" */

typedef struct BLOBHEADER {
    BYTE bType;
    BYTE bVersion;
    WORD reserved;
    DWORD aiKeyAlg;
} BLOBHEADER;

typedef struct PROVIDER {
    DWORD Magic;
    LONG References;
    DWORD Type;
    DWORD Flags;
} PROVIDER;

typedef struct KEY {
    DWORD Magic;
    PROVIDER *Provider;
    DWORD Algorithm;
    DWORD Size;
    BYTE Blob[];
} KEY;

/* the providers windows installs, with their types. */
static const struct {
    PCWSTR Name;
    DWORD Type;
} g_providers[] = {
    {L"Microsoft Base Cryptographic Provider v1.0", PROV_RSA_FULL},
    {L"Microsoft Enhanced Cryptographic Provider v1.0", PROV_RSA_FULL},
    {L"Microsoft Strong Cryptographic Provider", PROV_RSA_FULL},
    {L"Microsoft Base Smart Card Crypto Provider", PROV_RSA_FULL},
    {L"Microsoft Enhanced RSA and AES Cryptographic Provider", PROV_RSA_AES},
    {L"Microsoft Enhanced RSA and AES Cryptographic Provider (Prototype)", PROV_RSA_AES},
    {L"Microsoft RSA SChannel Cryptographic Provider", PROV_RSA_SCHANNEL},
    {L"Microsoft Base DSS Cryptographic Provider", PROV_DSS},
    {L"Microsoft Base DSS and Diffie-Hellman Cryptographic Provider", PROV_DSS_DH},
    {L"Microsoft Enhanced DSS and Diffie-Hellman Cryptographic Provider", PROV_DSS_DH},
    {L"Microsoft DH SChannel Cryptographic Provider", PROV_DH_SCHANNEL},
};

static PROVIDER *provider(HCRYPTPROV handle)
{
    PROVIDER *p = (PROVIDER *)handle;
    if (!p || p->Magic != PROV_MAGIC) {
        AdvSetError(NTE_BAD_UID);
        return NULL;
    }
    return p;
}

static BOOL type_known(DWORD type)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_providers); i++)
        if (g_providers[i].Type == type)
            return TRUE;
    return FALSE;
}

ADVAPI BOOL WINAPI CryptAcquireContextW(HCRYPTPROV *result, LPCWSTR container, LPCWSTR name, DWORD type,
                                        DWORD flags)
{
    if (!result) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *result = 0;
    if (flags & ~(CRYPT_VERIFYCONTEXT | CRYPT_NEWKEYSET | CRYPT_DELETEKEYSET | CRYPT_MACHINE_KEYSET | CRYPT_SILENT |
                  CRYPT_DEFAULT_CONTAINER_OPTIONAL)) {
        AdvSetError(NTE_BAD_FLAGS);
        return FALSE;
    }
    if (!type_known(type)) {
        AdvSetError(NTE_PROV_TYPE_NOT_DEF);
        return FALSE;
    }
    if (name && *name) {
        ULONG i = 0;
        while (i < RTL_NUMBER_OF(g_providers) && _wcsicmp(name, g_providers[i].Name))
            i++;
        if (i == RTL_NUMBER_OF(g_providers)) {
            AdvSetError(NTE_KEYSET_NOT_DEF);
            return FALSE;
        }
        if (g_providers[i].Type != type) {
            AdvSetError(NTE_PROV_TYPE_NO_MATCH);
            return FALSE;
        }
    }
    if ((flags & CRYPT_VERIFYCONTEXT) == CRYPT_VERIFYCONTEXT && container && *container) {
        AdvSetError(NTE_BAD_FLAGS);
        return FALSE;
    }
    if (flags & CRYPT_DELETEKEYSET)
        return TRUE;
    PROVIDER *p = AdvAlloc(sizeof(*p));
    if (!p) {
        AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    p->Magic = PROV_MAGIC;
    p->References = 1;
    p->Type = type;
    p->Flags = flags;
    *result = (HCRYPTPROV)p;
    return TRUE;
}

ADVAPI BOOL WINAPI CryptAcquireContextA(HCRYPTPROV *result, LPCSTR container, LPCSTR name, DWORD type, DWORD flags)
{
    PWSTR c = AdvAnsiToWide(container), n = AdvAnsiToWide(name);
    BOOL ok = (!container || c) && (!name || n) && CryptAcquireContextW(result, c, n, type, flags);
    AdvFree(c);
    AdvFree(n);
    return ok;
}

ADVAPI BOOL WINAPI CryptContextAddRef(HCRYPTPROV handle, DWORD *reserved, DWORD flags)
{
    PROVIDER *p = provider(handle);
    if (!p)
        return FALSE;
    if (reserved || flags) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    InterlockedIncrement(&p->References);
    return TRUE;
}

ADVAPI BOOL WINAPI CryptReleaseContext(HCRYPTPROV handle, DWORD flags)
{
    PROVIDER *p = provider(handle);
    if (!p)
        return FALSE;
    if (flags) {
        AdvSetError(NTE_BAD_FLAGS);
        return FALSE;
    }
    if (InterlockedDecrement(&p->References) == 0) {
        p->Magic = 0;
        AdvFree(p);
    }
    return TRUE;
}

ADVAPI BOOL WINAPI CryptGenRandom(HCRYPTPROV handle, DWORD length, BYTE *buffer)
{
    if (!provider(handle))
        return FALSE;
    if (!buffer && length) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return AdvGenRandom(buffer, length);
}

/* keeps the blob of a public, private or plaintext key; there is no cipher to use it with yet. */
ADVAPI BOOL WINAPI CryptImportKey(HCRYPTPROV handle, const BYTE *data, DWORD size, HCRYPTKEY public_key, DWORD flags,
                                  HCRYPTKEY *result)
{
    PROVIDER *p = provider(handle);
    if (!p)
        return FALSE;
    if (!data || !result) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    const BLOBHEADER *h = (const BLOBHEADER *)data;
    if (size < sizeof(*h)) {
        AdvSetError(NTE_BAD_DATA);
        return FALSE;
    }
    if (h->bVersion < 2 || h->bVersion > 3) {
        AdvSetError(NTE_BAD_VER);
        return FALSE;
    }
    if (h->bType != PUBLICKEYBLOB && h->bType != PRIVATEKEYBLOB && h->bType != PLAINTEXTKEYBLOB) {
        AdvSetError(NTE_BAD_TYPE);
        return FALSE;
    }
    KEY *k = AdvAlloc(sizeof(*k) + size);
    if (!k) {
        AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    k->Magic = KEY_MAGIC;
    k->Provider = p;
    k->Algorithm = h->aiKeyAlg;
    k->Size = size;
    memcpy(k->Blob, data, size);
    *result = (HCRYPTKEY)k;
    return TRUE;
}

ADVAPI BOOL WINAPI CryptDestroyKey(HCRYPTKEY handle)
{
    KEY *k = (KEY *)handle;
    if (!k || k->Magic != KEY_MAGIC) {
        AdvSetError(NTE_BAD_KEY);
        return FALSE;
    }
    k->Magic = 0;
    SecureZeroMemory(k->Blob, k->Size);
    AdvFree(k);
    return TRUE;
}

/* ---------------------------------------------------------------- hashes */

typedef ULONG_PTR HCRYPTHASH;

#define NTE_BAD_HASH ((DWORD)0x80090002L)
#define NTE_BAD_ALGID ((DWORD)0x80090008L)
#define NTE_BAD_HASH_STATE ((DWORD)0x8009000CL)

#define CALG_MD5 0x8003
#define CALG_SHA1 0x8004
#define CALG_SHA_256 0x800c
#define CALG_SHA_384 0x800d
#define CALG_SHA_512 0x800e

#define HP_ALGID 1
#define HP_HASHVAL 2
#define HP_HASHSIZE 4

#define HASH_MAGIC 0x48534148 /* "HASH" */

/*
 * the data is kept until the value is asked for, then hashed in one pass by
 * crypt32, which implements the algorithms. this costs memory for long
 * inputs but keeps one implementation of each algorithm.
 */
typedef struct HASH {
    DWORD Magic;
    PROVIDER *Provider;
    DWORD Algorithm;
    DWORD Size;
    BYTE *Data;
    DWORD Length;
    DWORD Capacity;
    BOOL Finished;
    BYTE Value[64];
} HASH;

typedef BOOL (WINAPI *HASH_FUNCTION)(HCRYPTPROV, DWORD, DWORD, const BYTE *, DWORD, BYTE *, DWORD *);

static DWORD hash_size(DWORD algorithm)
{
    switch (algorithm) {
    case CALG_MD5:
        return 16;
    case CALG_SHA1:
        return 20;
    case CALG_SHA_256:
        return 32;
    case CALG_SHA_384:
        return 48;
    case CALG_SHA_512:
        return 64;
    }
    return 0;
}

static HASH *hash_object(HCRYPTHASH handle)
{
    HASH *h = (HASH *)handle;
    if (!h || h->Magic != HASH_MAGIC) {
        AdvSetError(NTE_BAD_HASH);
        return NULL;
    }
    return h;
}

ADVAPI BOOL WINAPI CryptCreateHash(HCRYPTPROV handle, DWORD algorithm, HCRYPTKEY key, DWORD flags, HCRYPTHASH *result)
{
    PROVIDER *p = provider(handle);
    if (!p)
        return FALSE;
    if (!result) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (flags) {
        AdvSetError(NTE_BAD_FLAGS);
        return FALSE;
    }
    /* keyed hashes need a cipher implementation, which the provider lacks. */
    DWORD size = hash_size(algorithm);
    if (!size || key) {
        AdvSetError(key ? NTE_BAD_KEY : NTE_BAD_ALGID);
        return FALSE;
    }
    HASH *h = AdvAlloc(sizeof(*h));
    if (!h) {
        AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    memset(h, 0, sizeof(*h));
    h->Magic = HASH_MAGIC;
    h->Provider = p;
    h->Algorithm = algorithm;
    h->Size = size;
    InterlockedIncrement(&p->References);
    *result = (HCRYPTHASH)h;
    return TRUE;
}

ADVAPI BOOL WINAPI CryptHashData(HCRYPTHASH handle, const BYTE *data, DWORD length, DWORD flags)
{
    HASH *h = hash_object(handle);
    if (!h)
        return FALSE;
    if (h->Finished) {
        AdvSetError(NTE_BAD_HASH_STATE);
        return FALSE;
    }
    if (!data && length) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (length > h->Capacity - h->Length) {
        if (length > 0x7FFFFFFF - h->Length) {
            AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        DWORD capacity = h->Capacity ? h->Capacity : 256;
        while (capacity - h->Length < length)
            capacity = capacity <= 0x3FFFFFFF ? capacity * 2 : 0x7FFFFFFF;
        BYTE *grown = AdvAlloc(capacity);
        if (!grown) {
            AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        if (h->Length)
            memcpy(grown, h->Data, h->Length);
        AdvFree(h->Data);
        h->Data = grown;
        h->Capacity = capacity;
    }
    if (length)
        memcpy(h->Data + h->Length, data, length);
    h->Length += length;
    return TRUE;
}

static BOOL finish_hash(HASH *h)
{
    if (h->Finished)
        return TRUE;
    HMODULE crypt32 = LoadLibraryW(L"crypt32.dll");
    HASH_FUNCTION hash = crypt32 ? (HASH_FUNCTION)(void *)GetProcAddress(crypt32, "CryptHashCertificate") : NULL;
    DWORD size = sizeof(h->Value);
    BOOL ok = hash && hash(0, h->Algorithm, 0, h->Data, h->Length, h->Value, &size) && size == h->Size;
    if (crypt32)
        FreeLibrary(crypt32);
    if (!ok) {
        AdvSetError(NTE_BAD_ALGID);
        return FALSE;
    }
    if (h->Data)
        SecureZeroMemory(h->Data, h->Length);
    AdvFree(h->Data);
    h->Data = NULL;
    h->Length = h->Capacity = 0;
    h->Finished = TRUE;
    return TRUE;
}

static BOOL return_param(const void *value, DWORD size, BYTE *data, DWORD *length)
{
    if (!data) {
        *length = size;
        return TRUE;
    }
    if (*length < size) {
        *length = size;
        AdvSetError(ERROR_MORE_DATA);
        return FALSE;
    }
    memcpy(data, value, size);
    *length = size;
    return TRUE;
}

ADVAPI BOOL WINAPI CryptGetHashParam(HCRYPTHASH handle, DWORD param, BYTE *data, DWORD *length, DWORD flags)
{
    HASH *h = hash_object(handle);
    if (!h)
        return FALSE;
    if (!length) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (flags) {
        AdvSetError(NTE_BAD_FLAGS);
        return FALSE;
    }
    switch (param) {
    case HP_ALGID:
        return return_param(&h->Algorithm, sizeof(DWORD), data, length);
    case HP_HASHSIZE:
        return return_param(&h->Size, sizeof(DWORD), data, length);
    case HP_HASHVAL:
        /* a size query leaves the hash open for more data. */
        if (!data)
            return return_param(h->Value, h->Size, NULL, length);
        return finish_hash(h) && return_param(h->Value, h->Size, data, length);
    default:
        AdvSetError(NTE_BAD_TYPE);
        return FALSE;
    }
}

ADVAPI BOOL WINAPI CryptDestroyHash(HCRYPTHASH handle)
{
    HASH *h = hash_object(handle);
    if (!h)
        return FALSE;
    h->Magic = 0;
    if (h->Data)
        SecureZeroMemory(h->Data, h->Length);
    AdvFree(h->Data);
    CryptReleaseContext((HCRYPTPROV)h->Provider, 0);
    SecureZeroMemory(h, sizeof(*h));
    AdvFree(h);
    return TRUE;
}
