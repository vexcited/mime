/*
 * the cryptographic service provider functions of CryptoAPI.
 *
 * windows routes these through cryptsp.dll to provider dlls such as
 * rsaenh.dll. mime answers for the microsoft providers itself: contexts,
 * random numbers and imported key blobs. key containers are not stored, so
 * opening one always finds it, empty.
 */
#include "advapi32_private.h"
#include <crypto/crypto.h>

typedef ULONG_PTR HCRYPTKEY;

ADVAPI BOOL WINAPI GetUserNameA(LPSTR buffer, LPDWORD size);

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
    ULONG Name;      /* index in g_providers */
    ULONG Algorithm; /* the next PP_ENUMALGS entry */
    CHAR Container[MAX_PATH];
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
        return nullptr;
    }
    return p;
}

/* the provider windows picks for a type when none is named. */
static ULONG default_provider(DWORD type)
{
    if (type == PROV_RSA_FULL)
        return 2; /* the strong provider */
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_providers); i++)
        if (g_providers[i].Type == type)
            return i;
    return 0;
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
    ULONG index = default_provider(type);
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
        index = i;
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
    memset(p, 0, sizeof(*p));
    p->Magic = PROV_MAGIC;
    p->References = 1;
    p->Type = type;
    p->Flags = flags;
    p->Name = index;
    /* without a name the container is the user's, as on windows. */
    if (container && *container) {
        WideCharToMultiByte(CP_ACP, 0, container, -1, p->Container, sizeof(p->Container), nullptr, nullptr);
    } else if ((flags & CRYPT_VERIFYCONTEXT) != CRYPT_VERIFYCONTEXT) {
        DWORD size = sizeof(p->Container);
        if (!GetUserNameA(p->Container, &size))
            p->Container[0] = 0;
    }
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

/* the hashes run as the data comes, on the shared crypto library. */
typedef struct HASH {
    DWORD Magic;
    PROVIDER *Provider;
    DWORD Algorithm;
    DWORD Size;
    const MimeHashAlg *Hash;
    MimeHashCtx Context;
    BOOL Finished;
    BYTE Value[64];
} HASH;

static const MimeHashAlg *hash_algorithm(DWORD algorithm)
{
    switch (algorithm) {
    case CALG_MD5:
        return &mime_md5;
    case CALG_SHA1:
        return &mime_sha1;
    case CALG_SHA_256:
        return &mime_sha256;
    case CALG_SHA_384:
        return &mime_sha384;
    case CALG_SHA_512:
        return &mime_sha512;
    }
    return nullptr;
}

static HASH *hash_object(HCRYPTHASH handle)
{
    HASH *h = (HASH *)handle;
    if (!h || h->Magic != HASH_MAGIC) {
        AdvSetError(NTE_BAD_HASH);
        return nullptr;
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
    const MimeHashAlg *alg = hash_algorithm(algorithm);
    if (!alg || key) {
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
    h->Size = alg->digest_size;
    h->Hash = alg;
    alg->init(&h->Context);
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
    if (length)
        h->Hash->update(&h->Context, data, length);
    return TRUE;
}

static BOOL finish_hash(HASH *h)
{
    if (!h->Finished) {
        h->Hash->final(&h->Context, h->Value);
        SecureZeroMemory(&h->Context, sizeof(h->Context));
        h->Finished = TRUE;
    }
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
            return return_param(h->Value, h->Size, nullptr, length);
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
    CryptReleaseContext((HCRYPTPROV)h->Provider, 0);
    SecureZeroMemory(h, sizeof(*h));
    AdvFree(h);
    return TRUE;
}

/* ---------------------------------------------------------------- provider parameters and keys */

#define NTE_NO_KEY ((DWORD)0x8009000DL)
#define NTE_BAD_KEY_STATE ((DWORD)0x8009000BL)

#define PP_ENUMALGS 1
#define PP_ENUMCONTAINERS 2
#define PP_IMPTYPE 3
#define PP_NAME 4
#define PP_VERSION 5
#define PP_CONTAINER 6
#define PP_PROVTYPE 16
#define PP_ENUMALGS_EX 22
#define PP_KEYSET_TYPE 27
#define PP_UNIQUE_CONTAINER 36
#define PP_KEYSPEC 39
#define CRYPT_FIRST 1
#define CRYPT_IMPL_SOFTWARE 2
#define AT_KEYEXCHANGE 1
#define AT_SIGNATURE 2
#define CALG_RSA_SIGN 0x2400
#define CALG_RSA_KEYX 0xa400
#define RSA1_MAGIC 0x31415352 /* "RSA1" */

typedef struct PROV_ENUMALGS {
    DWORD aiAlgid;
    DWORD dwBitLen;
    DWORD dwNameLen;
    CHAR szName[20];
} PROV_ENUMALGS;

typedef struct PROV_ENUMALGS_EX {
    DWORD aiAlgid;
    DWORD dwDefaultLen;
    DWORD dwMinLen;
    DWORD dwMaxLen;
    DWORD dwProtocols;
    DWORD dwNameLen;
    CHAR szName[20];
    DWORD dwLongNameLen;
    CHAR szLongName[40];
} PROV_ENUMALGS_EX;

/* the algorithms the provider implements: hashes, since it has no cipher. */
static const struct {
    DWORD Id;
    DWORD Bits;
    const char *Name;
    const char *LongName;
} g_algorithms[] = {
    {CALG_SHA_256, 256, "SHA-256", "Secure Hash Algorithm SHA-256"},
    {CALG_SHA_384, 384, "SHA-384", "Secure Hash Algorithm SHA-384"},
    {CALG_SHA_512, 512, "SHA-512", "Secure Hash Algorithm SHA-512"},
    {CALG_SHA1, 160, "SHA-1", "Secure Hash Algorithm (SHA-1)"},
    {CALG_MD5, 128, "MD5", "Message Digest 5 (MD5)"},
};

ADVAPI BOOL WINAPI CryptGetProvParam(HCRYPTPROV handle, DWORD param, BYTE *data, DWORD *length, DWORD flags)
{
    PROVIDER *p = provider(handle);
    if (!p)
        return FALSE;
    if (!length) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    DWORD value;
    switch (param) {
    case PP_NAME: {
        CHAR name[128];
        int n = WideCharToMultiByte(CP_ACP, 0, g_providers[p->Name].Name, -1, name, sizeof(name), nullptr, nullptr);
        return return_param(name, (DWORD)n, data, length);
    }
    case PP_CONTAINER:
    case PP_UNIQUE_CONTAINER:
        return return_param(p->Container, (DWORD)strlen(p->Container) + 1, data, length);
    case PP_IMPTYPE:
        value = CRYPT_IMPL_SOFTWARE;
        return return_param(&value, sizeof(value), data, length);
    case PP_VERSION:
        value = 0x200;
        return return_param(&value, sizeof(value), data, length);
    case PP_PROVTYPE:
        value = p->Type;
        return return_param(&value, sizeof(value), data, length);
    case PP_KEYSPEC:
        value = AT_KEYEXCHANGE | AT_SIGNATURE;
        return return_param(&value, sizeof(value), data, length);
    case PP_KEYSET_TYPE:
        value = p->Flags & CRYPT_MACHINE_KEYSET;
        return return_param(&value, sizeof(value), data, length);
    case PP_ENUMCONTAINERS:
        /* key containers are never stored. */
        AdvSetError(ERROR_NO_MORE_ITEMS);
        return FALSE;
    case PP_ENUMALGS:
    case PP_ENUMALGS_EX: {
        if (flags & CRYPT_FIRST)
            p->Algorithm = 0;
        if (p->Algorithm >= RTL_NUMBER_OF(g_algorithms)) {
            AdvSetError(ERROR_NO_MORE_ITEMS);
            return FALSE;
        }
        ULONG i = p->Algorithm;
        BOOL ok;
        if (param == PP_ENUMALGS) {
            PROV_ENUMALGS a = {g_algorithms[i].Id, g_algorithms[i].Bits, (DWORD)strlen(g_algorithms[i].Name) + 1};
            strcpy(a.szName, g_algorithms[i].Name);
            ok = return_param(&a, sizeof(a), data, length);
        } else {
            PROV_ENUMALGS_EX a = {};
            a.aiAlgid = g_algorithms[i].Id;
            a.dwDefaultLen = a.dwMinLen = a.dwMaxLen = g_algorithms[i].Bits;
            a.dwNameLen = (DWORD)strlen(g_algorithms[i].Name) + 1;
            strcpy(a.szName, g_algorithms[i].Name);
            a.dwLongNameLen = (DWORD)strlen(g_algorithms[i].LongName) + 1;
            strcpy(a.szLongName, g_algorithms[i].LongName);
            ok = return_param(&a, sizeof(a), data, length);
        }
        if (ok && data)
            p->Algorithm++;
        return ok;
    }
    default:
        AdvSetError(NTE_BAD_TYPE);
        return FALSE;
    }
}

ADVAPI BOOL WINAPI CryptEnumProvidersW(DWORD index, DWORD *reserved, DWORD flags, DWORD *type, LPWSTR name,
                                       DWORD *size)
{
    if (reserved || !size) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (flags) {
        AdvSetError(NTE_BAD_FLAGS);
        return FALSE;
    }
    if (index >= RTL_NUMBER_OF(g_providers)) {
        AdvSetError(ERROR_NO_MORE_ITEMS);
        return FALSE;
    }
    if (type)
        *type = g_providers[index].Type;
    DWORD bytes = (DWORD)(wcslen(g_providers[index].Name) + 1) * sizeof(WCHAR);
    return return_param(g_providers[index].Name, bytes, (BYTE *)name, size);
}

ADVAPI BOOL WINAPI CryptEnumProvidersA(DWORD index, DWORD *reserved, DWORD flags, DWORD *type, LPSTR name, DWORD *size)
{
    if (reserved || !size) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (index >= RTL_NUMBER_OF(g_providers)) {
        AdvSetError(ERROR_NO_MORE_ITEMS);
        return FALSE;
    }
    CHAR ansi[128];
    int n = WideCharToMultiByte(CP_ACP, 0, g_providers[index].Name, -1, ansi, sizeof(ansi), nullptr, nullptr);
    if (type)
        *type = g_providers[index].Type;
    return return_param(ansi, (DWORD)n, (BYTE *)name, size);
}

/* containers are never stored, so they hold no key. */
ADVAPI BOOL WINAPI CryptGetUserKey(HCRYPTPROV handle, DWORD spec, HCRYPTKEY *key)
{
    if (!provider(handle))
        return FALSE;
    if (!key) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *key = 0;
    AdvSetError(NTE_NO_KEY);
    return FALSE;
}

static KEY *key_object(HCRYPTKEY handle)
{
    KEY *k = (KEY *)handle;
    if (!k || k->Magic != KEY_MAGIC) {
        AdvSetError(NTE_BAD_KEY);
        return nullptr;
    }
    return k;
}

/*
 * a key goes out as the blob it came in, or as its public part: an rsa
 * private blob starts with the public key's header and modulus.
 */
ADVAPI BOOL WINAPI CryptExportKey(HCRYPTKEY handle, HCRYPTKEY wrap, DWORD type, DWORD flags, BYTE *data, DWORD *length)
{
    KEY *k = key_object(handle);
    if (!k)
        return FALSE;
    if (!length) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (wrap) {
        AdvSetError(NTE_BAD_KEY);
        return FALSE;
    }
    const BLOBHEADER *h = (const BLOBHEADER *)k->Blob;
    if (type == h->bType)
        return return_param(k->Blob, k->Size, data, length);
    if (type == PUBLICKEYBLOB && h->bType == PRIVATEKEYBLOB &&
        (h->aiKeyAlg == CALG_RSA_KEYX || h->aiKeyAlg == CALG_RSA_SIGN) && k->Size >= sizeof(*h) + 12) {
        DWORD bits = *(const DWORD *)(k->Blob + sizeof(*h) + 4);
        DWORD size = sizeof(*h) + 12 + bits / 8;
        if (size > k->Size) {
            AdvSetError(NTE_BAD_DATA);
            return FALSE;
        }
        BYTE *blob = AdvAlloc(size);
        if (!blob) {
            AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        memcpy(blob, k->Blob, size);
        ((BLOBHEADER *)blob)->bType = PUBLICKEYBLOB;
        *(DWORD *)(blob + sizeof(*h)) = RSA1_MAGIC;
        BOOL ok = return_param(blob, size, data, length);
        AdvFree(blob);
        return ok;
    }
    AdvSetError(NTE_BAD_TYPE);
    return FALSE;
}

/* the provider has no cipher: keys import and export but do not encrypt. */
ADVAPI BOOL WINAPI CryptDecrypt(HCRYPTKEY handle, HCRYPTHASH hash, BOOL final, DWORD flags, BYTE *data, DWORD *length)
{
    if (!key_object(handle))
        return FALSE;
    AdvSetError(NTE_BAD_ALGID);
    return FALSE;
}

ADVAPI BOOL WINAPI CryptEncrypt(HCRYPTKEY handle, HCRYPTHASH hash, BOOL final, DWORD flags, BYTE *data, DWORD *length,
                                DWORD size)
{
    if (!key_object(handle))
        return FALSE;
    AdvSetError(NTE_BAD_ALGID);
    return FALSE;
}

/* only the value can be set: a signature over a hash computed elsewhere. */
ADVAPI BOOL WINAPI CryptSetHashParam(HCRYPTHASH handle, DWORD param, const BYTE *data, DWORD flags)
{
    HASH *h = hash_object(handle);
    if (!h)
        return FALSE;
    if (!data) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (flags) {
        AdvSetError(NTE_BAD_FLAGS);
        return FALSE;
    }
    if (param != HP_HASHVAL) {
        AdvSetError(NTE_BAD_TYPE);
        return FALSE;
    }
    memcpy(h->Value, data, h->Size);
    SecureZeroMemory(&h->Context, sizeof(h->Context));
    h->Finished = TRUE;
    return TRUE;
}

/* signing takes the container's key, and containers hold none. */
ADVAPI BOOL WINAPI CryptSignHashW(HCRYPTHASH handle, DWORD spec, LPCWSTR description, DWORD flags, BYTE *signature,
                                  DWORD *length)
{
    if (!hash_object(handle))
        return FALSE;
    if (!length) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    AdvSetError(NTE_NO_KEY);
    return FALSE;
}

ADVAPI BOOL WINAPI CryptSignHashA(HCRYPTHASH handle, DWORD spec, LPCSTR description, DWORD flags, BYTE *signature,
                                  DWORD *length)
{
    return CryptSignHashW(handle, spec, nullptr, flags, signature, length);
}
