/*
 * CryptoAPI cryptographic service provider functions.
 *
 * windows routes these through cryptsp.dll to provider dlls such as rsaenh.dll. mime implements the microsoft
 * providers itself: contexts, random numbers, imported key blobs and key containers. a container is a file under the
 * Microsoft\Crypto\RSA folder of the user or of the machine, holding the key pairs of key exchange and signature.
 */
#include "advapi32_private.h"
#include <crypto/crypto.h>

typedef ULONG_PTR HCRYPTKEY;

ADVAPI BOOL WINAPI CryptDestroyKey(HCRYPTKEY handle);

ADVAPI BOOL WINAPI GetUserNameA(LPSTR buffer, LPDWORD size);

#define NTE_BAD_UID ((DWORD)0x80090001L)
#define NTE_BAD_KEY ((DWORD)0x80090003L)
#define NTE_BAD_LEN ((DWORD)0x80090004L)
#define NTE_BAD_DATA ((DWORD)0x80090005L)
#define NTE_BAD_VER ((DWORD)0x80090007L)
#define NTE_BAD_FLAGS ((DWORD)0x80090009L)
#define NTE_BAD_TYPE ((DWORD)0x8009000AL)
#define NTE_EXISTS ((DWORD)0x8009000FL)
#define NTE_BAD_KEYSET ((DWORD)0x80090016L)
#define NTE_PROV_TYPE_NOT_DEF ((DWORD)0x80090017L)
#define NTE_KEYSET_NOT_DEF ((DWORD)0x80090019L)
#define NTE_PROV_TYPE_NO_MATCH ((DWORD)0x8009001BL)

#define IMPORT_FLAGS 0x2153 /* CRYPT_EXPORTABLE, CRYPT_USER_PROTECTED, CRYPT_NO_SALT, CRYPT_OAEP and the ipsec flags. */
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
#define AT_KEYEXCHANGE 1
#define AT_SIGNATURE 2
#define CALG_RSA_SIGN 0x2400
#define CALG_RSA_KEYX 0xa400
#define CALG_DES 0x6601
#define CALG_RC2 0x6602
#define CALG_3DES 0x6603
#define CALG_3DES_112 0x6609
#define CALG_AES_128 0x660e
#define CALG_AES_192 0x660f
#define CALG_AES_256 0x6610
#define CALG_RC4 0x6801
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
    ULONG Name;      /* index in g_providers. */
    ULONG Algorithm; /* the next PP_ENUMALGS entry. */
    PWSTR Prompt;
    CHAR Container[MAX_PATH];
    /* a context for verification only has no container to keep keys in. */
    BOOL Persistent;
    ULONG EnumIndex;
    WCHAR ContainerName[MAX_PATH];
} PROVIDER;

typedef struct KEY {
    DWORD Magic;
    PROVIDER *Provider;
    DWORD Algorithm;
    DWORD Size;
    BOOL Exportable;
    /* symmetric key state kept between encrypt calls: mode, chaining value, stream. */
    DWORD Mode;
    BOOL Started;
    BYTE InitialIv[16];
    BYTE Iv[16];
    MimeRc4 Rc4;
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
        return 2; /* the strong provider. */
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

/* ---------------------------------------------------------------- key containers */

#define CONTAINER_MAGIC 0x4B53494D /* "MISK" */
#define SLOT_KEYX 0
#define SLOT_SIGN 1

typedef struct CONTAINER {
    DWORD Magic;
    WCHAR Name[MAX_PATH];
    DWORD Exportable[2];
    DWORD Size[2];
    BYTE *Blob[2]; /* memory only, the blobs follow the header in the file. */
} CONTAINER;

#define CONTAINER_HEADER offsetof(CONTAINER, Blob)

static SRWLOCK g_container_lock = SRWLOCK_INIT;

static BOOL container_dir(const PROVIDER *p, WCHAR *dir)
{
    WCHAR base[MAX_PATH];
    BOOL machine = (p->Flags & CRYPT_MACHINE_KEYSET) != 0;
    DWORD n = GetEnvironmentVariableW(machine ? L"ProgramData" : L"APPDATA", base, MAX_PATH);
    if (!n || n >= MAX_PATH - 64)
        return FALSE;
    _snwprintf(dir, MAX_PATH, L"%ls\\Microsoft\\Crypto\\RSA\\%ls", base, machine ? L"MachineKeys" : L"Keys");
    return TRUE;
}

/* the file is named by a hash of the case folded name. */
static BOOL container_file(const PROVIDER *p, WCHAR *path)
{
    WCHAR dir[MAX_PATH];
    if (!container_dir(p, dir) || wcslen(dir) + 40 >= MAX_PATH)
        return FALSE;
    ULONG64 a = 0xcbf29ce484222325ull, b = 0x84222325cbf29ce4ull;
    for (PCWSTR c = p->ContainerName; *c; c++) {
        WCHAR w = towupper(*c);
        a = (a ^ (w & 0xff)) * 0x100000001b3ull;
        a = (a ^ (w >> 8)) * 0x100000001b3ull;
        b = (b ^ (w >> 8)) * 0x100000001b3ull;
        b = (b ^ (w & 0xff)) * 0x100000001b3ull;
    }
    _snwprintf(path, MAX_PATH, L"%ls\\%016llx%016llx", dir, a, b);
    return TRUE;
}

static void container_free(CONTAINER *c)
{
    for (int i = 0; i < 2; i++) {
        if (c->Blob[i]) {
            SecureZeroMemory(c->Blob[i], c->Size[i]);
            AdvFree(c->Blob[i]);
        }
        c->Blob[i] = nullptr;
    }
}

/* reads the container of the context, with the error set when it is not there. */
static BOOL container_read(const PROVIDER *p, CONTAINER *c)
{
    memset(c, 0, sizeof(*c));
    WCHAR path[MAX_PATH];
    if (!container_file(p, path)) {
        AdvSetError(NTE_BAD_KEYSET);
        return FALSE;
    }
    AcquireSRWLockShared(&g_container_lock);
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    BOOL ok = file != INVALID_HANDLE_VALUE;
    DWORD read;
    if (ok && (!ReadFile(file, c, CONTAINER_HEADER, &read, nullptr) || read != CONTAINER_HEADER ||
               c->Magic != CONTAINER_MAGIC))
        ok = FALSE;
    for (int i = 0; ok && i < 2; i++) {
        if (!c->Size[i])
            continue;
        if (c->Size[i] > 0x100000 || !(c->Blob[i] = AdvAlloc(c->Size[i])) ||
            !ReadFile(file, c->Blob[i], c->Size[i], &read, nullptr) || read != c->Size[i])
            ok = FALSE;
    }
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
    ReleaseSRWLockShared(&g_container_lock);
    if (!ok) {
        container_free(c);
        AdvSetError(NTE_BAD_KEYSET);
    }
    return ok;
}

static BOOL container_write(const PROVIDER *p, CONTAINER *c, BOOL exclusive)
{
    WCHAR path[MAX_PATH], dir[MAX_PATH];
    if (!container_file(p, path) || !container_dir(p, dir)) {
        AdvSetError(NTE_BAD_KEYSET);
        return FALSE;
    }
    c->Magic = CONTAINER_MAGIC;
    lstrcpynW(c->Name, p->ContainerName, MAX_PATH);
    AcquireSRWLockExclusive(&g_container_lock);
    for (WCHAR *at = dir + 3; *at; at++) {
        if (*at == L'\\') {
            *at = 0;
            CreateDirectoryW(dir, nullptr);
            *at = L'\\';
        }
    }
    CreateDirectoryW(dir, nullptr);
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, exclusive ? CREATE_NEW : CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    BOOL ok = file != INVALID_HANDLE_VALUE;
    DWORD error = ok ? 0 : GetLastError(), written;
    if (ok)
        ok = WriteFile(file, c, CONTAINER_HEADER, &written, nullptr);
    for (int i = 0; ok && i < 2; i++)
        if (c->Size[i])
            ok = WriteFile(file, c->Blob[i], c->Size[i], &written, nullptr);
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
    ReleaseSRWLockExclusive(&g_container_lock);
    if (!ok)
        AdvSetError(error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS ? NTE_EXISTS : NTE_BAD_KEYSET);
    return ok;
}

/* replaces the key pair of a slot in the container of the context. */
static BOOL container_store_key(const PROVIDER *p, int slot, const BYTE *blob, DWORD size, BOOL exportable)
{
    CONTAINER c;
    if (!container_read(p, &c))
        return FALSE;
    BYTE *copy = AdvAlloc(size);
    if (!copy) {
        container_free(&c);
        AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    memcpy(copy, blob, size);
    if (c.Blob[slot]) {
        SecureZeroMemory(c.Blob[slot], c.Size[slot]);
        AdvFree(c.Blob[slot]);
    }
    c.Blob[slot] = copy;
    c.Size[slot] = size;
    c.Exportable[slot] = exportable;
    BOOL ok = container_write(p, &c, FALSE);
    container_free(&c);
    return ok;
}

static BOOL container_delete(const PROVIDER *p)
{
    WCHAR path[MAX_PATH];
    if (!container_file(p, path)) {
        AdvSetError(NTE_BAD_KEYSET);
        return FALSE;
    }
    AcquireSRWLockExclusive(&g_container_lock);
    BOOL ok = DeleteFileW(path);
    ReleaseSRWLockExclusive(&g_container_lock);
    if (!ok)
        AdvSetError(NTE_BAD_KEYSET);
    return ok;
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
        lstrcpynW(p->ContainerName, container, MAX_PATH);
    } else if ((flags & CRYPT_VERIFYCONTEXT) != CRYPT_VERIFYCONTEXT) {
        DWORD size = sizeof(p->Container);
        if (!GetUserNameA(p->Container, &size))
            p->Container[0] = 0;
        MultiByteToWideChar(CP_ACP, 0, p->Container, -1, p->ContainerName, MAX_PATH);
    }
    p->Persistent = (flags & CRYPT_VERIFYCONTEXT) != CRYPT_VERIFYCONTEXT;
    if (p->Persistent) {
        CONTAINER existing;
        BOOL found = container_read(p, &existing);
        if (found)
            container_free(&existing);
        if (flags & CRYPT_DELETEKEYSET) {
            BOOL ok = found && container_delete(p);
            AdvFree(p);
            if (!found)
                AdvSetError(NTE_BAD_KEYSET);
            return ok;
        }
        if (flags & CRYPT_NEWKEYSET) {
            CONTAINER empty;
            memset(&empty, 0, sizeof(empty));
            if (found) {
                AdvFree(p);
                AdvSetError(NTE_EXISTS);
                return FALSE;
            }
            if (!container_write(p, &empty, TRUE)) {
                AdvFree(p);
                return FALSE;
            }
        } else if (!found) {
            AdvFree(p);
            AdvSetError(NTE_BAD_KEYSET);
            return FALSE;
        }
    } else if (flags & CRYPT_DELETEKEYSET) {
        AdvFree(p);
        return TRUE;
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
        AdvFree(p->Prompt);
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
static BOOL key_import(HCRYPTPROV handle, const BYTE *data, DWORD size, DWORD flags, HCRYPTKEY public_key, HCRYPTKEY *result)
{
    PROVIDER *p = provider(handle);
    if (!p)
        return FALSE;
    if (!data || !result) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    const BLOBHEADER *h = (const BLOBHEADER *)data;
    /* rsaenh takes exportable, user protected, no salt, oaep and the two ipsec flags, in this order of checks. */
    if (flags & ~IMPORT_FLAGS) {
        AdvSetError(NTE_BAD_FLAGS);
        return FALSE;
    }
    if (size < sizeof(*h)) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (h->bType == PUBLICKEYBLOB && public_key) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (h->bVersion != 2) {
        AdvSetError(NTE_BAD_VER);
        return FALSE;
    }
    if (h->bType != PUBLICKEYBLOB && h->bType != PRIVATEKEYBLOB && h->bType != PLAINTEXTKEYBLOB) {
        AdvSetError(NTE_BAD_TYPE);
        return FALSE;
    }
    /* an RSA public key is for signing or key exchange and holds the whole modulus after the exponent. */
    if (h->bType == PUBLICKEYBLOB && p->Type != PROV_DSS && p->Type != PROV_DSS_DH) {
        if (h->aiKeyAlg != CALG_RSA_SIGN && h->aiKeyAlg != CALG_RSA_KEYX) {
            AdvSetError(NTE_BAD_DATA);
            return FALSE;
        }
        DWORD bits = size >= sizeof(*h) + 8 ? *(const DWORD *)(data + sizeof(*h) + 4) : 0;
        if (size < sizeof(*h) + 12 + (bits + 7) / 8) {
            AdvSetError(NTE_BAD_LEN);
            return FALSE;
        }
    }
    KEY *k = AdvAlloc(sizeof(*k) + size);
    if (!k) {
        AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    memset(k, 0, sizeof(*k));
    k->Magic = KEY_MAGIC;
    k->Provider = p;
    k->Algorithm = h->aiKeyAlg;
    k->Size = size;
    k->Exportable = (flags & 1) != 0;
    memcpy(k->Blob, data, size);
    InterlockedIncrement(&p->References);
    *result = (HCRYPTKEY)k;
    return TRUE;
}

/* a private key of key exchange or signature goes into the container of the context. */
ADVAPI BOOL WINAPI CryptImportKey(HCRYPTPROV handle, const BYTE *data, DWORD size, HCRYPTKEY public_key, DWORD flags,
                                  HCRYPTKEY *result)
{
    if (!key_import(handle, data, size, flags, public_key, result))
        return FALSE;
    PROVIDER *p = (PROVIDER *)handle;
    const BLOBHEADER *h = (const BLOBHEADER *)data;
    if (p->Persistent && h->bType == PRIVATEKEYBLOB && (h->aiKeyAlg == CALG_RSA_KEYX || h->aiKeyAlg == CALG_RSA_SIGN) &&
        !container_store_key(p, h->aiKeyAlg == CALG_RSA_KEYX ? SLOT_KEYX : SLOT_SIGN, data, size, flags & 1)) {
        CryptDestroyKey(*result);
        *result = 0;
        return FALSE;
    }
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
    CryptReleaseContext((HCRYPTPROV)k->Provider, 0);
    AdvFree(k);
    return TRUE;
}

/* the copy keeps the state of the original, chaining value included, and each is released on its own. */
ADVAPI BOOL WINAPI CryptDuplicateKey(HCRYPTKEY handle, DWORD *reserved, DWORD flags, HCRYPTKEY *result)
{
    KEY *k = (KEY *)handle;
    if (!k || k->Magic != KEY_MAGIC) {
        AdvSetError(NTE_BAD_KEY);
        return FALSE;
    }
    if (reserved || flags || !result) {
        AdvSetError(reserved || flags ? NTE_BAD_FLAGS : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    KEY *copy = AdvAlloc(sizeof(*copy) + k->Size);
    if (!copy) {
        AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    memcpy(copy, k, sizeof(*copy) + k->Size);
    InterlockedIncrement(&k->Provider->References);
    *result = (HCRYPTKEY)copy;
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

/* hashes run incrementally on the shared crypto library. */
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
    /* keyed hashes need a cipher, which the provider lacks. */
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

/* the description of a signature is hashed as the bytes of its UTF-16 string, as CryptHashData does, before the value is read. */
static BOOL hash_description(HASH *h, LPCWSTR description)
{
    if (!description)
        return TRUE;
    if (h->Finished) {
        AdvSetError(NTE_BAD_HASH_STATE);
        return FALSE;
    }
    size_t length = wcslen(description);
    if (length)
        h->Hash->update(&h->Context, description, length * sizeof(WCHAR));
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

ADVAPI BOOL WINAPI CryptGetDefaultProviderW(DWORD type, DWORD *reserved, DWORD flags, PWSTR name, DWORD *size)
{
    if (!type || type > 999 || reserved || !size) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (flags > 2) {
        AdvSetError(NTE_BAD_FLAGS);
        return FALSE;
    }
    WCHAR path[96], value[256];
    _snwprintf(path, RTL_NUMBER_OF(path), L"SOFTWARE\\Microsoft\\Cryptography\\Defaults\\Provider Types\\Type %03u", type);
    DWORD bytes = sizeof(value);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, path, L"Name", RRF_RT_REG_SZ, nullptr, value, &bytes)) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return return_param(value, bytes, (BYTE *)name, size);
}

ADVAPI BOOL WINAPI CryptGetDefaultProviderA(DWORD type, DWORD *reserved, DWORD flags, PSTR name, DWORD *size)
{
    if (!size) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR wide[256];
    DWORD bytes = sizeof(wide);
    if (!CryptGetDefaultProviderW(type, reserved, flags, wide, &bytes))
        return FALSE;
    CHAR ansi[768];
    int count = WideCharToMultiByte(CP_ACP, 0, wide, -1, ansi, sizeof(ansi), nullptr, nullptr);
    return count && return_param(ansi, count, (BYTE *)name, size);
}

static HWND g_provider_window;

ADVAPI BOOL WINAPI CryptSetProvParam(HCRYPTPROV handle, DWORD param, const BYTE *data, DWORD flags)
{
    if (param == 1) {
        if (!data) {
            AdvSetError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        memcpy(&g_provider_window, data, sizeof(g_provider_window));
        return TRUE;
    }
    PROVIDER *p = provider(handle);
    if (!p)
        return FALSE;
    if (flags) {
        AdvSetError(NTE_BAD_FLAGS);
        return FALSE;
    }
    if (param == 21) {
        PWSTR prompt = nullptr;
        if (data) {
            SIZE_T size = (wcslen((PCWSTR)data) + 1) * sizeof(WCHAR);
            prompt = AdvAlloc(size);
            if (!prompt) {
                AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
                return FALSE;
            }
            memcpy(prompt, data, size);
        }
        AdvFree(p->Prompt);
        p->Prompt = prompt;
        return TRUE;
    }
    AdvSetError(NTE_BAD_TYPE);
    return FALSE;
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

/* the copy hashes on from the state of the original, and each is destroyed on its own. */
ADVAPI BOOL WINAPI CryptDuplicateHash(HCRYPTHASH handle, DWORD *reserved, DWORD flags, HCRYPTHASH *result)
{
    HASH *h = hash_object(handle);
    if (!h)
        return FALSE;
    if (reserved || flags || !result) {
        AdvSetError(reserved || flags ? NTE_BAD_FLAGS : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    HASH *copy = AdvAlloc(sizeof(*copy));
    if (!copy) {
        AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    memcpy(copy, h, sizeof(*copy));
    InterlockedIncrement(&h->Provider->References);
    *result = (HCRYPTHASH)copy;
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

/* the algorithms the provider implements, with the key lengths it takes, in bits. */
static const struct {
    DWORD Id;
    DWORD Bits;
    DWORD MinBits;
    DWORD MaxBits;
    const char *Name;
    const char *LongName;
} g_algorithms[] = {
    {CALG_RSA_SIGN, 1024, 384, 16384, "RSA_SIGN", "RSA Signature"},
    {CALG_RSA_KEYX, 1024, 384, 16384, "RSA_KEYX", "RSA Key Exchange"},
    {CALG_AES_128, 128, 128, 128, "AES-128", "Advanced Encryption Standard (AES-128)"},
    {CALG_AES_192, 192, 192, 192, "AES-192", "Advanced Encryption Standard (AES-192)"},
    {CALG_AES_256, 256, 256, 256, "AES-256", "Advanced Encryption Standard (AES-256)"},
    {CALG_3DES, 168, 168, 168, "3DES", "Three Key Triple DES"},
    {CALG_3DES_112, 112, 112, 112, "3DES TWO KEY", "Two Key Triple DES"},
    {CALG_DES, 56, 56, 56, "DES", "Data Encryption Standard (DES)"},
    {CALG_RC4, 128, 40, 128, "RC4", "RSA Data Security's RC4"},
    {CALG_SHA_256, 256, 256, 256, "SHA-256", "Secure Hash Algorithm SHA-256"},
    {CALG_SHA_384, 384, 384, 384, "SHA-384", "Secure Hash Algorithm SHA-384"},
    {CALG_SHA_512, 512, 512, 512, "SHA-512", "Secure Hash Algorithm SHA-512"},
    {CALG_SHA1, 160, 160, 160, "SHA-1", "Secure Hash Algorithm (SHA-1)"},
    {CALG_MD5, 128, 128, 128, "MD5", "Message Digest 5 (MD5)"},
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
    case PP_ENUMCONTAINERS: {
        if (flags & CRYPT_FIRST)
            p->EnumIndex = 0;
        WCHAR dir[MAX_PATH], pattern[MAX_PATH];
        if (!container_dir(p, dir)) {
            AdvSetError(ERROR_NO_MORE_ITEMS);
            return FALSE;
        }
        _snwprintf(pattern, MAX_PATH, L"%ls\\*", dir);
        WIN32_FIND_DATAW found;
        HANDLE find = FindFirstFileW(pattern, &found);
        ULONG index = 0;
        BOOL ok = FALSE, more = FALSE;
        CHAR name[MAX_PATH];
        for (BOOL next = find != INVALID_HANDLE_VALUE; next; next = FindNextFileW(find, &found)) {
            if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            WCHAR path[MAX_PATH];
            _snwprintf(path, MAX_PATH, L"%ls\\%ls", dir, found.cFileName);
            CONTAINER c;
            HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            DWORD read = 0;
            BOOL valid = file != INVALID_HANDLE_VALUE && ReadFile(file, &c, CONTAINER_HEADER, &read, nullptr) &&
                         read == CONTAINER_HEADER && c.Magic == CONTAINER_MAGIC;
            if (file != INVALID_HANDLE_VALUE)
                CloseHandle(file);
            if (!valid || index++ < p->EnumIndex)
                continue;
            WideCharToMultiByte(CP_ACP, 0, c.Name, -1, name, sizeof(name), nullptr, nullptr);
            more = TRUE;
            break;
        }
        if (find != INVALID_HANDLE_VALUE)
            FindClose(find);
        if (!more) {
            AdvSetError(ERROR_NO_MORE_ITEMS);
            return FALSE;
        }
        ok = return_param(name, (DWORD)strlen(name) + 1, data, length);
        if (ok && data)
            p->EnumIndex++;
        return ok;
    }
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
            a.dwDefaultLen = g_algorithms[i].Bits;
            a.dwMinLen = g_algorithms[i].MinBits;
            a.dwMaxLen = g_algorithms[i].MaxBits;
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

/* the key pair of a container, as a key of the context. */
ADVAPI BOOL WINAPI CryptGetUserKey(HCRYPTPROV handle, DWORD spec, HCRYPTKEY *key)
{
    PROVIDER *p = provider(handle);
    if (!p)
        return FALSE;
    if (!key) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *key = 0;
    if (spec != AT_KEYEXCHANGE && spec != AT_SIGNATURE) {
        AdvSetError(NTE_NO_KEY);
        return FALSE;
    }
    CONTAINER c;
    if (!p->Persistent || !container_read(p, &c)) {
        AdvSetError(NTE_NO_KEY);
        return FALSE;
    }
    int slot = spec == AT_KEYEXCHANGE ? SLOT_KEYX : SLOT_SIGN;
    BOOL ok = c.Blob[slot] != nullptr && key_import(handle, c.Blob[slot], c.Size[slot], c.Exportable[slot], 0, key);
    if (!c.Blob[slot])
        AdvSetError(NTE_NO_KEY);
    container_free(&c);
    return ok;
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
 * exports the key as the blob it came in, or its public part: an rsa private blob starts with the public header and
 * modulus.
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
    if ((type == PRIVATEKEYBLOB || type == PLAINTEXTKEYBLOB) && !k->Exportable) {
        AdvSetError(NTE_BAD_KEY_STATE);
        return FALSE;
    }
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


#define CRYPT_MODE_CBC 1
#define CRYPT_MODE_ECB 2
#define CRYPT_NO_SALT 0x10

/* a plaintext symmetric key's bytes, or NULL for other keys. */
static const BYTE *symmetric_bytes(const KEY *k, DWORD *size)
{
    const BLOBHEADER *h = (const BLOBHEADER *)k->Blob;
    if (k->Size < sizeof(*h) + 4 || h->bType != PLAINTEXTKEYBLOB)
        return nullptr;
    *size = *(const DWORD *)(k->Blob + sizeof(*h));
    return *size <= k->Size - sizeof(*h) - 4 ? k->Blob + sizeof(*h) + 4 : nullptr;
}

/* a block cipher key's context, held on the stack. block_cipher also gives the cipher and its block size. */
typedef union {
    MimeAes Aes;
    MimeDes Des;
    MimeDes3 Des3;
} BLOCK_KEY;

static BOOL block_cipher(const KEY *k, const MimeCipher **cipher, BLOCK_KEY *context, DWORD *block)
{
    DWORD size;
    const BYTE *bytes = symmetric_bytes(k, &size);
    if (!bytes)
        return FALSE;
    switch (k->Algorithm) {
    case CALG_AES_128:
    case CALG_AES_192:
    case CALG_AES_256:
        if (mime_aes_init(&context->Aes, bytes, size))
            return FALSE;
        *cipher = &mime_cipher_aes;
        break;
    case CALG_DES:
        if (size != 8)
            return FALSE;
        mime_des_init(&context->Des, bytes);
        *cipher = &mime_cipher_des;
        break;
    case CALG_3DES:
    case CALG_3DES_112:
        if (mime_des3_init(&context->Des3, bytes, size))
            return FALSE;
        *cipher = &mime_cipher_des3;
        break;
    default:
        return FALSE;
    }
    *block = (*cipher)->block_size;
    return TRUE;
}

static DWORD key_bits(const KEY *key)
{
    DWORD bytes;
    if (symmetric_bytes(key, &bytes))
        return bytes * 8;
    if ((key->Algorithm == CALG_RSA_SIGN || key->Algorithm == CALG_RSA_KEYX) && key->Size >= sizeof(BLOBHEADER) + 12)
        return *(const DWORD *)(key->Blob + sizeof(BLOBHEADER) + 4);
    return 0;
}

static DWORD key_block_size(const KEY *key)
{
    switch (key->Algorithm) {
    case CALG_DES:
    case CALG_3DES:
    case CALG_3DES_112:
    case CALG_RC2:
        return 8;
    case CALG_AES_128:
    case CALG_AES_192:
    case CALG_AES_256:
        return 16;
    default:
        return 0;
    }
}

ADVAPI BOOL WINAPI CryptGetKeyParam(HCRYPTKEY handle, DWORD param, BYTE *data, DWORD *length, DWORD flags)
{
    KEY *key = key_object(handle);
    if (!key)
        return FALSE;
    if (!length || flags) {
        AdvSetError(flags ? NTE_BAD_FLAGS : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    DWORD value, block = key_block_size(key);
    switch (param) {
    case 1:
        return return_param(key->InitialIv, block, data, length);
    case 3:
        value = block != 0;
        break;
    case 4:
        value = key->Mode ? key->Mode : CRYPT_MODE_CBC;
        break;
    case 7:
        value = key->Algorithm;
        break;
    case 8:
        value = block * 8;
        if (key->Algorithm == CALG_RSA_SIGN || key->Algorithm == CALG_RSA_KEYX)
            value = key_bits(key);
        break;
    case 9:
        value = key_bits(key);
        break;
    default:
        AdvSetError(NTE_BAD_TYPE);
        return FALSE;
    }
    return return_param(&value, sizeof(value), data, length);
}

ADVAPI BOOL WINAPI CryptSetKeyParam(HCRYPTKEY handle, DWORD param, const BYTE *data, DWORD flags)
{
    KEY *key = key_object(handle);
    if (!key)
        return FALSE;
    if (flags || !data) {
        AdvSetError(flags ? NTE_BAD_FLAGS : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    DWORD block = key_block_size(key), value;
    if (param == 1) {
        memcpy(key->InitialIv, data, block);
        return TRUE;
    }
    memcpy(&value, data, sizeof(value));
    if (param == 4) {
        if (!block) {
            AdvSetError(NTE_BAD_KEY);
            return FALSE;
        }
        if (value < 1 || value > 4) {
            AdvSetError(NTE_BAD_DATA);
            return FALSE;
        }
        if (value > CRYPT_MODE_ECB) {
            AdvSetError(ERROR_NOT_SUPPORTED);
            return FALSE;
        }
        key->Mode = value;
        return TRUE;
    }
    if (param == 3) {
        if (value == 1)
            return TRUE;
        AdvSetError(NTE_BAD_DATA);
        return FALSE;
    }
    AdvSetError(NTE_BAD_TYPE);
    return FALSE;
}

static void bn_little_endian(const MimeBn *number, BYTE *bytes, DWORD size)
{
    mime_bn_to_bytes(number, bytes, size);
    for (DWORD i = 0; i < size / 2; i++) {
        BYTE saved = bytes[i];
        bytes[i] = bytes[size - 1 - i];
        bytes[size - 1 - i] = saved;
    }
}

ADVAPI BOOL WINAPI CryptGenKey(HCRYPTPROV handle, DWORD algorithm, DWORD flags, HCRYPTKEY *result)
{
    PROVIDER *p = provider(handle);
    if (!p)
        return FALSE;
    if (!result) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *result = 0;
    if (flags & 0x1fe8) {
        AdvSetError(NTE_BAD_FLAGS);
        return FALSE;
    }
    /* persistent containers, user protection and salted generation are not implemented. */
    if (flags & 0xfffe) {
        AdvSetError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    if (algorithm == AT_KEYEXCHANGE)
        algorithm = CALG_RSA_KEYX;
    if (algorithm == AT_SIGNATURE)
        algorithm = CALG_RSA_SIGN;
    DWORD bits = flags >> 16, bytes;
    BOOL rsa_key = algorithm == CALG_RSA_KEYX || algorithm == CALG_RSA_SIGN;
    switch (algorithm) {
    case CALG_RSA_KEYX:
    case CALG_RSA_SIGN:
        bits = bits ? bits : 1024;
        if (bits < 512 || bits > 16384 || bits % 16) {
            AdvSetError(NTE_BAD_FLAGS);
            return FALSE;
        }
        bytes = bits / 8;
        break;
    case CALG_DES: bytes = 8; break;
    case CALG_3DES: bytes = 24; break;
    case CALG_3DES_112: bytes = 16; break;
    case CALG_AES_128: bytes = 16; break;
    case CALG_AES_192: bytes = 24; break;
    case CALG_AES_256: bytes = 32; break;
    case CALG_RC4:
        bits = bits ? bits : 128;
        if (bits < 40 || bits > 128 || bits % 8) {
            AdvSetError(NTE_BAD_FLAGS);
            return FALSE;
        }
        bytes = bits / 8;
        break;
    default:
        AdvSetError(NTE_BAD_ALGID);
        return FALSE;
    }
    if (!rsa_key && bits && bits != bytes * 8) {
        AdvSetError(NTE_BAD_FLAGS);
        return FALSE;
    }
    DWORD size = sizeof(BLOBHEADER) + (rsa_key ? 12 + bytes * 9 / 2 : 4 + bytes);
    BYTE *blob = AdvAlloc(size);
    if (!blob) {
        AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    memset(blob, 0, size);
    BLOBHEADER *header = (BLOBHEADER *)blob;
    header->bType = rsa_key ? PRIVATEKEYBLOB : PLAINTEXTKEYBLOB;
    header->bVersion = 2;
    header->aiKeyAlg = algorithm;
    BOOL ok = FALSE;
    if (rsa_key) {
        MimeRsa *rsa = mime_rsa_new();
        int status = rsa ? mime_rsa_generate(rsa, bits) : MIME_CRYPTO_ERR_NOMEM;
        if (!status) {
            DWORD *rsa_header = (DWORD *)(blob + sizeof(*header));
            rsa_header[0] = 0x32415352;
            rsa_header[1] = bits;
            rsa_header[2] = 65537;
            BYTE *part = blob + sizeof(*header) + 12;
            const MimeBn *numbers[] = {&rsa->n, &rsa->p, &rsa->q, &rsa->dp, &rsa->dq, &rsa->qinv, &rsa->d};
            for (ULONG i = 0; i < RTL_NUMBER_OF(numbers); i++) {
                DWORD length = i == 0 || i == 6 ? bytes : bytes / 2;
                bn_little_endian(numbers[i], part, length);
                part += length;
            }
            ok = TRUE;
        } else {
            AdvSetError(status == MIME_CRYPTO_ERR_NOMEM ? ERROR_NOT_ENOUGH_MEMORY : NTE_BAD_DATA);
        }
        if (rsa)
            mime_rsa_free(rsa);
    } else {
        *(DWORD *)(blob + sizeof(*header)) = bytes;
        BYTE *material = blob + sizeof(*header) + 4;
        ok = AdvGenRandom(material, bytes);
        if (ok && (algorithm == CALG_DES || algorithm == CALG_3DES || algorithm == CALG_3DES_112))
            for (DWORD i = 0; i < bytes; i++) {
                BYTE value = material[i] & 0xfe;
                material[i] = value | (BYTE)(1 ^ (__builtin_popcount(value) & 1));
            }
    }
    if (ok)
        ok = key_import(handle, blob, size, flags & 1, 0, result);
    /* a key pair of a named container stays in it. */
    if (ok && rsa_key && p->Persistent &&
        !container_store_key(p, algorithm == CALG_RSA_KEYX ? SLOT_KEYX : SLOT_SIGN, blob, size, flags & 1)) {
        CryptDestroyKey(*result);
        *result = 0;
        ok = FALSE;
    }
    SecureZeroMemory(blob, size);
    AdvFree(blob);
    return ok;
}

/*
 * encrypts or decrypts in place like the provider symmetric keys: a block cipher in the key's mode, chained from a zero
 * IV by default, with last block padding, or a stream cipher whose state carries to the next call. a final call pads a
 * short block and decryption strips the padding, so the length changes by up to a block.
 */
static BOOL symmetric(HCRYPTKEY handle, BOOL final, BYTE *data, DWORD *length, DWORD capacity, BOOL encrypt)
{
    KEY *k = key_object(handle);
    if (!k)
        return FALSE;
    if (!length) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    DWORD size;
    const BYTE *bytes = symmetric_bytes(k, &size);
    if (!bytes) {
        AdvSetError(NTE_BAD_KEY);
        return FALSE;
    }
    if (k->Algorithm == CALG_RC4) {
        if (!k->Started) {
            mime_rc4_init(&k->Rc4, bytes, size);
            k->Started = TRUE;
        }
        mime_rc4_crypt(&k->Rc4, data, data, *length);
        if (final)
            k->Started = FALSE;
        return TRUE;
    }
    const MimeCipher *cipher;
    BLOCK_KEY context;
    DWORD block;
    if (!block_cipher(k, &cipher, &context, &block)) {
        AdvSetError(k->Algorithm == CALG_RC2 ? NTE_BAD_ALGID : NTE_BAD_KEY);
        return FALSE;
    }
    if (!k->Started) {
        k->Started = TRUE;
        memcpy(k->Iv, k->InitialIv, sizeof(k->Iv));
        if (!k->Mode)
            k->Mode = CRYPT_MODE_CBC;
    }
    DWORD used = *length;
    if (encrypt) {
        DWORD padded = final ? (used / block + 1) * block : used;
        if (!final && used % block) {
            AdvSetError(NTE_BAD_DATA);
            return FALSE;
        }
        if (padded > capacity) {
            *length = padded;
            AdvSetError(ERROR_MORE_DATA);
            return FALSE;
        }
        if (final)
            memset(data + used, (int)(padded - used), padded - used);
        if (k->Mode == CRYPT_MODE_ECB)
            mime_ecb_encrypt(cipher, &context, data, data, padded);
        else
            mime_cbc_encrypt(cipher, &context, k->Iv, data, data, padded);
        *length = padded;
    } else {
        if (used % block || (final && !used)) {
            AdvSetError(NTE_BAD_DATA);
            return FALSE;
        }
        if (k->Mode == CRYPT_MODE_ECB)
            mime_ecb_decrypt(cipher, &context, data, data, used);
        else
            mime_cbc_decrypt(cipher, &context, k->Iv, data, data, used);
        if (final) {
            DWORD pad = data[used - 1];
            if (!pad || pad > block || pad > used) {
                AdvSetError(NTE_BAD_DATA);
                return FALSE;
            }
            for (DWORD i = used - pad; i < used; i++)
                if (data[i] != pad) {
                    AdvSetError(NTE_BAD_DATA);
                    return FALSE;
                }
            *length = used - pad;
        }
    }
    if (final) {
        k->Started = FALSE;
    }
    return TRUE;
}

ADVAPI BOOL WINAPI CryptDecrypt(HCRYPTKEY handle, HCRYPTHASH hash, BOOL final, DWORD flags, BYTE *data, DWORD *length)
{
    if (hash || flags) {
        AdvSetError(hash ? NTE_BAD_HASH : NTE_BAD_FLAGS);
        return FALSE;
    }
    if (!data && length && key_object(handle)) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return symmetric(handle, final, data, length, length ? *length : 0, FALSE);
}

ADVAPI BOOL WINAPI CryptEncrypt(HCRYPTKEY handle, HCRYPTHASH hash, BOOL final, DWORD flags, BYTE *data, DWORD *length,
                                DWORD size)
{
    if (hash || flags) {
        AdvSetError(hash ? NTE_BAD_HASH : NTE_BAD_FLAGS);
        return FALSE;
    }
    if (!length) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    KEY *k = key_object(handle);
    if (!k)
        return FALSE;
    /* a size query: no data, report the output size. */
    if (!data) {
        DWORD block = 0;
        if (k->Algorithm != CALG_RC4) {
            const MimeCipher *cipher;
            BLOCK_KEY context;
            if (block_cipher(k, &cipher, &context, &block) && final)
                *length = (*length / block + 1) * block;
        }
        return TRUE;
    }
    return symmetric(handle, final, data, length, size, TRUE);
}

/*
 * derives a key from a hash value: the value itself when long enough, else the value stretched by mime_capi_expand.
 * the length comes from the upper word of the flags or from the algorithm, and des keys get their parity set. this
 * finishes the hash.
 */
ADVAPI BOOL WINAPI CryptDeriveKey(HCRYPTPROV handle, DWORD algorithm, HCRYPTHASH base, DWORD flags, HCRYPTKEY *result)
{
    PROVIDER *p = provider(handle);
    if (!p)
        return FALSE;
    HASH *h = hash_object(base);
    if (!h)
        return FALSE;
    if (!result) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    DWORD bits = flags >> 16, bytes;
    switch (algorithm) {
    case CALG_DES:
        bytes = 8;
        break;
    case CALG_3DES:
        bytes = 24;
        break;
    case CALG_3DES_112:
        bytes = 16;
        break;
    case CALG_AES_128:
        bytes = 16;
        break;
    case CALG_AES_192:
        bytes = 24;
        break;
    case CALG_AES_256:
        bytes = 32;
        break;
    case CALG_RC4:
    case CALG_RC2:
        bytes = bits ? (bits + 7) / 8 : 16;
        if (bytes > 56) {
            AdvSetError(NTE_BAD_FLAGS);
            return FALSE;
        }
        break;
    default:
        AdvSetError(NTE_BAD_ALGID);
        return FALSE;
    }
    if (bits && algorithm != CALG_RC4 && algorithm != CALG_RC2 && bits != bytes * 8 && algorithm != CALG_DES) {
        AdvSetError(NTE_BAD_FLAGS);
        return FALSE;
    }
    finish_hash(h);
    DWORD size = sizeof(BLOBHEADER) + 4 + bytes;
    KEY *k = AdvAlloc(sizeof(*k) + size);
    if (!k) {
        AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    memset(k, 0, sizeof(*k));
    BLOBHEADER *blob = (BLOBHEADER *)k->Blob;
    blob->bType = PLAINTEXTKEYBLOB;
    blob->bVersion = 2;
    blob->aiKeyAlg = algorithm;
    *(DWORD *)(k->Blob + sizeof(*blob)) = bytes;
    BYTE *material = k->Blob + sizeof(*blob) + 4;
    mime_capi_expand(h->Hash, h->Value, material, bytes);
    if (algorithm == CALG_DES || algorithm == CALG_3DES || algorithm == CALG_3DES_112) {
        for (DWORD i = 0; i < bytes; i++) {
            BYTE v = material[i] & 0xFE;
            v |= (BYTE)(1 ^ (__builtin_popcount(v) & 1));
            material[i] = v;
        }
    }
    k->Magic = KEY_MAGIC;
    k->Provider = p;
    k->Algorithm = algorithm;
    k->Size = size;
    k->Exportable = (flags & 1) != 0;
    InterlockedIncrement(&p->References);
    *result = (HCRYPTKEY)k;
    return TRUE;
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

static MimeRsa *rsa_from_blob(const KEY *k);
#define CRYPT_NOHASHOID 0x00000001

/* the hash value with the key pair of the hash's container, as PKCS#1 v1.5 with the hash's object identifier
 * unless CRYPT_NOHASHOID is given. the signature is little endian and the hash is finished by it.
 */
ADVAPI BOOL WINAPI CryptSignHashW(HCRYPTHASH handle, DWORD spec, LPCWSTR description, DWORD flags, BYTE *signature,
                                  DWORD *length)
{
    HASH *h = hash_object(handle);
    if (!h)
        return FALSE;
    if (!length) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (flags & ~CRYPT_NOHASHOID) {
        AdvSetError(NTE_BAD_FLAGS);
        return FALSE;
    }
    PROVIDER *p = h->Provider;
    if ((spec != AT_KEYEXCHANGE && spec != AT_SIGNATURE) || !p->Persistent) {
        AdvSetError(NTE_NO_KEY);
        return FALSE;
    }
    CONTAINER c;
    if (!container_read(p, &c)) {
        AdvSetError(NTE_NO_KEY);
        return FALSE;
    }
    int slot = spec == AT_KEYEXCHANGE ? SLOT_KEYX : SLOT_SIGN;
    BOOL ok = FALSE;
    MimeRsa *rsa = nullptr;
    if (c.Blob[slot]) {
        KEY *copy = AdvAlloc(sizeof(*copy) + c.Size[slot]);
        if (copy) {
            memset(copy, 0, sizeof(*copy));
            copy->Size = c.Size[slot];
            memcpy(copy->Blob, c.Blob[slot], c.Size[slot]);
            rsa = rsa_from_blob(copy);
            if (rsa) {
                const BLOBHEADER *header = (const BLOBHEADER *)copy->Blob;
                DWORD bytes = (rsa->bits + 7) / 8, half = bytes / 2;
                const BYTE *primes = copy->Blob + sizeof(*header) + 12 + bytes;
                BYTE *big = AdvAlloc(half);
                BOOL good = header->bType == PRIVATEKEYBLOB && big && sizeof(*header) + 12 + bytes + half * 2 <= copy->Size;
                for (int i = 0; good && i < 2; i++) {
                    for (DWORD j = 0; j < half; j++)
                        big[j] = primes[i * half + half - 1 - j];
                    good = mime_bn_from_bytes(i ? &rsa->q : &rsa->p, big, half);
                }
                AdvFree(big);
                rsa->private_key = good;
                if (!good || mime_rsa_check(rsa) != MIME_CRYPTO_OK) {
                    mime_rsa_free(rsa);
                    rsa = nullptr;
                }
            }
            SecureZeroMemory(copy, sizeof(*copy) + c.Size[slot]);
            AdvFree(copy);
        }
    }
    container_free(&c);
    if (!rsa) {
        AdvSetError(NTE_NO_KEY);
        return FALSE;
    }
    DWORD size = (rsa->bits + 7) / 8;
    if (!signature) {
        *length = size;
        ok = TRUE;
    } else if (*length < size) {
        *length = size;
        AdvSetError(ERROR_MORE_DATA);
    } else {
        BYTE *big = AdvAlloc(size);
        if (!big) {
            AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
        } else if (hash_description(h, description)) {
            finish_hash(h);
            const MimeHashAlg *alg = (flags & CRYPT_NOHASHOID) ? nullptr : h->Hash;
            int status = alg ? mime_rsa_pkcs1_sign(rsa, alg, h->Value, h->Size, big)
                             : mime_rsa_pkcs1_sign_prefix(rsa, nullptr, 0, h->Value, h->Size, big);
            if (status == MIME_CRYPTO_OK) {
                for (DWORD i = 0; i < size; i++)
                    signature[i] = big[size - 1 - i];
                *length = size;
                ok = TRUE;
            } else {
                AdvSetError(NTE_BAD_KEY);
            }
            AdvFree(big);
        }
    }
    mime_rsa_free(rsa);
    return ok;
}

/* the description of the narrow calls is converted with the ansi code page first, as advapi32 does. */
static LPWSTR description_wide(LPCSTR description, WCHAR *buffer, int count)
{
    if (!description)
        return nullptr;
    if (MultiByteToWideChar(CP_ACP, 0, description, -1, buffer, count) <= 0)
        buffer[0] = 0;
    return buffer;
}

ADVAPI BOOL WINAPI CryptSignHashA(HCRYPTHASH handle, DWORD spec, LPCSTR description, DWORD flags, BYTE *signature,
                                  DWORD *length)
{
    WCHAR wide[260];
    return CryptSignHashW(handle, spec, description_wide(description, wide, RTL_NUMBER_OF(wide)), flags, signature, length);
}

/* ---------------------------------------------------------------- signatures */

void *mime_crypto_alloc(size_t size)
{
    return AdvAlloc(size);
}

void mime_crypto_free(void *p)
{
    AdvFree(p);
}

bool mime_crypto_random(void *out, size_t size)
{
    return AdvGenRandom(out, size);
}

#define NTE_BAD_SIGNATURE ((DWORD)0x80090006L)

#define CALG_DSS_SIGN 0x2200
#define DSS1_MAGIC 0x31535344 /* "DSS1" */

/* a number of a key blob or a signature, little endian bytes, into the big numbers of the shared library. */
static BOOL bn_from_little(MimeBn *n, const BYTE *little, DWORD size)
{
    BYTE big[128];
    if (size > sizeof(big))
        return FALSE;
    for (DWORD i = 0; i < size; i++)
        big[i] = little[size - 1 - i];
    return mime_bn_from_bytes(n, big, size);
}

/*
 * verifies a DSA signature of the Base DSS provider: the public key blob is the header, DSSPUBKEY (magic DSS1 and the
 * length of p in bits, 512 to 1024) and then p, q (20 bytes), g and y, all little endian. The signature is r and s of 20
 * bytes each, little endian, over the SHA-1 value of the hash read as a big endian number, as FIPS 186 has it (read
 * from the signatures that the session files of Navisworks carry, which verify with their public keys that way).
 * Only SHA-1 goes with the provider, any other hash is NTE_BAD_ALGID.
 */
static BOOL verify_dss(const KEY *k, HASH *h, const BYTE *signature, DWORD length)
{
    const BLOBHEADER *header = (const BLOBHEADER *)k->Blob;
    if (header->bType != PUBLICKEYBLOB || k->Size < sizeof(*header) + 8 ||
        *(const DWORD *)(k->Blob + sizeof(*header)) != DSS1_MAGIC) {
        AdvSetError(NTE_BAD_KEY);
        return FALSE;
    }
    DWORD bits = *(const DWORD *)(k->Blob + sizeof(*header) + 4), size = bits / 8;
    if (bits < 512 || bits > 1024 || (bits & 63) || k->Size < sizeof(*header) + 8 + 3 * size + 20) {
        AdvSetError(NTE_BAD_KEY);
        return FALSE;
    }
    if (h->Algorithm != CALG_SHA1) {
        AdvSetError(NTE_BAD_ALGID);
        return FALSE;
    }
    if (length != 40) {
        AdvSetError(NTE_BAD_SIGNATURE);
        return FALSE;
    }
    MimeBn *n = AdvAlloc(12 * sizeof(MimeBn));
    if (!n) {
        AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    MimeBn *p = n, *q = n + 1, *g = n + 2, *y = n + 3, *r = n + 4, *s = n + 5, *z = n + 6, *w = n + 7, *u1 = n + 8,
           *u2 = n + 9, *a = n + 10, *b = n + 11;
    const BYTE *at = k->Blob + sizeof(*header) + 8;
    BOOL ok = bn_from_little(p, at, size) && bn_from_little(q, at + size, 20) && bn_from_little(g, at + size + 20, size) &&
              bn_from_little(y, at + 2 * size + 20, size) && bn_from_little(r, signature, 20) &&
              bn_from_little(s, signature + 20, 20);
    finish_hash(h);
    /* r and s are between 0 and q, the value of the hash is taken as it is and reduced with q below. */
    ok = ok && mime_bn_from_bytes(z, h->Value, h->Size) && !mime_bn_is_zero(q) && !mime_bn_is_zero(r) &&
         !mime_bn_is_zero(s) && mime_bn_cmp(r, q) < 0 && mime_bn_cmp(s, q) < 0 &&
         mime_bn_cmp_word(p, 1) > 0 && (p->d[0] & 1) &&
         mime_bn_mod_inverse(w, s, q) == MIME_CRYPTO_OK && mime_bn_mod_mul(u1, z, w, q) == MIME_CRYPTO_OK &&
         mime_bn_mod_mul(u2, r, w, q) == MIME_CRYPTO_OK && mime_bn_mod_exp(a, g, u1, p, FALSE) == MIME_CRYPTO_OK &&
         mime_bn_mod_exp(b, y, u2, p, FALSE) == MIME_CRYPTO_OK && mime_bn_mod_mul(a, a, b, p) == MIME_CRYPTO_OK &&
         mime_bn_divmod(nullptr, b, a, q) == MIME_CRYPTO_OK && mime_bn_cmp(b, r) == 0;
    if (!ok)
        AdvSetError(NTE_BAD_SIGNATURE);
    SecureZeroMemory(n, 12 * sizeof(MimeBn));
    AdvFree(n);
    return ok;
}

/* the public part of an RSA key blob: exponent, then modulus as little endian bytes. */
static MimeRsa *rsa_from_blob(const KEY *k)
{
    const BLOBHEADER *h = (const BLOBHEADER *)k->Blob;
    if ((h->aiKeyAlg != CALG_RSA_KEYX && h->aiKeyAlg != CALG_RSA_SIGN) || k->Size < sizeof(*h) + 12)
        return nullptr;
    DWORD bits = *(const DWORD *)(k->Blob + sizeof(*h) + 4), exponent = *(const DWORD *)(k->Blob + sizeof(*h) + 8);
    DWORD size = (bits + 7) / 8;
    if (bits < 256 || bits > 16384 || sizeof(*h) + 12 + size > k->Size)
        return nullptr;
    BYTE *big = AdvAlloc(size);
    MimeRsa *rsa = mime_rsa_new();
    if (big && rsa) {
        const BYTE *little = k->Blob + sizeof(*h) + 12;
        for (DWORD i = 0; i < size; i++)
            big[i] = little[size - 1 - i];
        mime_bn_set(&rsa->e, exponent);
        if (mime_bn_from_bytes(&rsa->n, big, size) && mime_rsa_check(rsa) == MIME_CRYPTO_OK) {
            AdvFree(big);
            return rsa;
        }
    }
    AdvFree(big);
    if (rsa)
        mime_rsa_free(rsa);
    return nullptr;
}

/*
 * verifies a PKCS#1 v1.5 RSA signature over the hash value, with the hash's object identifier unless CRYPT_NOHASHOID
 * is given. the signature is little endian and reading the value finishes the hash. a signature whose length is not
 * the modulus length, or that does not verify, gives NTE_BAD_SIGNATURE. the description, when there is one, is
 * hashed first as UTF-16. CRYPT_X931_FORMAT is taken by rsaenh and not supported here, the other flags are
 * NTE_BAD_FLAGS before anything else is looked at.
 */
ADVAPI BOOL WINAPI CryptVerifySignatureW(HCRYPTHASH handle, const BYTE *signature, DWORD length, HCRYPTKEY public_key,
                                         LPCWSTR description, DWORD flags)
{
    /* CRYPT_X931_FORMAT is the other flag rsaenh takes and is not supported here. */
    if (flags & ~CRYPT_NOHASHOID) {
        AdvSetError(NTE_BAD_FLAGS);
        return FALSE;
    }
    HASH *h = hash_object(handle);
    if (!h)
        return FALSE;
    KEY *k = (KEY *)public_key;
    if (!k || k->Magic != KEY_MAGIC) {
        AdvSetError(NTE_BAD_KEY);
        return FALSE;
    }
    if (!signature) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (k->Algorithm == CALG_DSS_SIGN)
        return hash_description(h, description) && verify_dss(k, h, signature, length);
    MimeRsa *rsa = rsa_from_blob(k);
    if (!rsa) {
        AdvSetError(NTE_BAD_KEY);
        return FALSE;
    }
    DWORD size = (rsa->bits + 7) / 8;
    BYTE *big = AdvAlloc(size);
    BOOL ok = FALSE;
    if (!big) {
        AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
    } else if (length != size) {
        AdvSetError(NTE_BAD_SIGNATURE);
    } else if (hash_description(h, description)) {
        finish_hash(h);
        for (DWORD i = 0; i < size; i++)
            big[i] = signature[size - 1 - i];
        const MimeHashAlg *alg = (flags & CRYPT_NOHASHOID) ? nullptr : h->Hash;
        int status = alg ? mime_rsa_pkcs1_verify(rsa, alg, h->Value, h->Size, big, size)
                         : mime_rsa_pkcs1_verify_prefix(rsa, nullptr, 0, h->Value, h->Size, big, size);
        ok = status == MIME_CRYPTO_OK;
        if (!ok)
            AdvSetError(NTE_BAD_SIGNATURE);
    }
    AdvFree(big);
    mime_rsa_free(rsa);
    return ok;
}

ADVAPI BOOL WINAPI CryptVerifySignatureA(HCRYPTHASH handle, const BYTE *signature, DWORD length, HCRYPTKEY public_key,
                                         LPCSTR description, DWORD flags)
{
    WCHAR wide[260];
    return CryptVerifySignatureW(handle, signature, length, public_key,
                                 description_wide(description, wide, RTL_NUMBER_OF(wide)), flags);
}
