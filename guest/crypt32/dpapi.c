/*
 * data protection: CryptProtectData, CryptUnprotectData, their master keys, and CryptProtectMemory.
 *
 * windows does this in lsass, which keeps per user master keys of 64 random bytes in
 * %APPDATA%\Microsoft\Protect\<sid>, each file encrypted with a key derived from the user's password, and the
 * machine's in System32\Microsoft\Protect\S-1-5-18\User under the lsa secret DPAPI_SYSTEM. mime has no lsass, so
 * crypt32 does the work in the calling process, with the same files and blob format.
 *
 * mime users have no password, so their master keys are protected as windows protects a blank password account:
 * the pre-key is HMAC-SHA1 of the user's sid, keyed with SHA-1 of the empty UTF-16 password. it is stable as long
 * as the sid is, survives restarts and copies of drive_c, and protects nothing from whoever holds the profile.
 * machine keys use SHA-1 of the installation's MachineGuid in place of DPAPI_SYSTEM, binding local machine data
 * to this installation.
 */
#include "crypt32_private.h"
#include <crypto/crypto.h>

#ifndef TOKEN_QUERY
#define TOKEN_QUERY 0x0008
#endif
#define TOKEN_USER_CLASS 1
#define TOKEN_STATISTICS_CLASS 10

#define CALG_AES_128_ 0x660E
#define CALG_AES_192_ 0x660F
#define CALG_AES_256_ 0x6610
#define CALG_3DES_ 0x6603
#define CALG_SHA_512_ 0x800E

#define BLOB_SALT 32
#define BLOB_HMAC_KEY 32
#define MASTER_KEY_ROUNDS 8000
#define MASTER_KEY_LIFETIME (90ull * 24 * 3600 * 10000000)

/* {df9d8cd0-1501-11d1-8c7a-00c04fc297eb}, the provider of every blob. */
static const GUID g_provider = {0xdf9d8cd0, 0x1501, 0x11d1, {0x8c, 0x7a, 0x00, 0xc0, 0x4f, 0xc2, 0x97, 0xeb}};

/* ------------------------------------------------------------------ the crypto library's hooks */

typedef BOOLEAN(WINAPI *RTL_GEN_RANDOM)(PVOID, ULONG);

static INIT_ONCE g_random_once = INIT_ONCE_STATIC_INIT;
static RTL_GEN_RANDOM g_random;

static BOOL WINAPI load_random(INIT_ONCE *once, void *param, void **context)
{
    HMODULE advapi32 = LoadLibraryW(L"advapi32.dll");
    if (advapi32)
        g_random = (RTL_GEN_RANDOM)(void *)GetProcAddress(advapi32, "SystemFunction036");
    return TRUE;
}

void *mime_crypto_alloc(size_t size)
{
    return CrAlloc(size);
}

void mime_crypto_free(void *p)
{
    CrFree(p);
}

bool mime_crypto_random(void *out, size_t size)
{
    InitOnceExecuteOnce(&g_random_once, load_random, nullptr, nullptr);
    return g_random && g_random(out, (ULONG)size);
}

/* ------------------------------------------------------------------ identity */

static void guid_text(const GUID *g, WCHAR *out)
{
    _snwprintf(out, 37, L"%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", g->Data1, g->Data2, g->Data3,
               g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6],
               g->Data4[7]);
    out[36] = 0;
}

/* the token user's sid as text, S-R-I-S1-S2... */
static BOOL user_sid(WCHAR *out, DWORD capacity)
{
    HANDLE token;
    if (!NT_SUCCESS(NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY, &token)))
        return FALSE;
    BYTE buffer[256];
    ULONG size;
    NTSTATUS status = NtQueryInformationToken(token, TOKEN_USER_CLASS, buffer, sizeof(buffer), &size);
    NtClose(token);
    if (!NT_SUCCESS(status))
        return FALSE;
    const BYTE *sid = *(const BYTE **)buffer;
    BYTE count = sid[1];
    ULONG64 authority = 0;
    for (int i = 0; i < 6; i++)
        authority = authority << 8 | sid[2 + i];
    int n = _snwprintf(out, capacity, L"S-%u-%llu", sid[0], authority);
    for (BYTE i = 0; n > 0 && (DWORD)n < capacity && i < count; i++) {
        DWORD sub;
        memcpy(&sub, sid + 8 + 4 * i, 4);
        int m = _snwprintf(out + n, capacity - n, L"-%u", sub);
        if (m < 0)
            return FALSE;
        n += m;
    }
    return n > 0 && (DWORD)n < capacity;
}

static BOOL machine_guid(WCHAR *out, DWORD capacity)
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY,
                      &key))
        return FALSE;
    DWORD size = capacity * sizeof(WCHAR), type;
    LONG error = RegQueryValueExW(key, L"MachineGuid", nullptr, &type, (BYTE *)out, &size);
    RegCloseKey(key);
    if (error || type != REG_SZ || size < sizeof(WCHAR))
        return FALSE;
    out[size / sizeof(WCHAR) - 1] = 0;
    return TRUE;
}

/* ------------------------------------------------------------------ master keys */

typedef struct {
    GUID guid;
    BYTE key[64];
} MASTER_KEY;

typedef struct CACHED_KEY {
    struct CACHED_KEY *next;
    BOOL machine;
    MASTER_KEY key;
} CACHED_KEY;

static CACHED_KEY *g_keys;
static SRWLOCK g_keys_lock = SRWLOCK_INIT;

/* the directory of the keys and the pre-key their files are encrypted with. */
static BOOL key_store(BOOL machine, WCHAR *dir, DWORD capacity, BYTE prekey[20])
{
    WCHAR sid[128];
    BYTE password[20];
    if (machine) {
        WCHAR guid[64];
        if (!machine_guid(guid, RTL_NUMBER_OF(guid)))
            return FALSE;
        mime_hash(&mime_sha1, guid, wcslen(guid) * sizeof(WCHAR), password);
        wcscpy(sid, L"S-1-5-18");
#ifdef _WIN64
        UINT n = GetSystemDirectoryW(dir, capacity);
#else
        /* the keys are the system's, which the lsass of Windows holds for programs of either width, so a 32-bit program
           names the real System32 and not the folder it is sent to. */
        UINT n = GetWindowsDirectoryW(dir, capacity);
        if (n && n + 10 < capacity) {
            wcscpy(dir + n, L"\\Sysnative");
            n += 10;
        }
#endif
        if (!n || n >= capacity || _snwprintf(dir + n, capacity - n, L"\\Microsoft\\Protect\\S-1-5-18\\User") < 0)
            return FALSE;
    } else {
        if (!user_sid(sid, RTL_NUMBER_OF(sid)))
            return FALSE;
        /* sha-1 of the empty password. */
        mime_hash(&mime_sha1, L"", 0, password);
        WCHAR appdata[MAX_PATH];
        DWORD n = GetEnvironmentVariableW(L"APPDATA", appdata, RTL_NUMBER_OF(appdata));
        if (!n || n >= RTL_NUMBER_OF(appdata)) {
            n = GetEnvironmentVariableW(L"USERPROFILE", appdata, RTL_NUMBER_OF(appdata));
            if (!n || n >= RTL_NUMBER_OF(appdata) - 20)
                return FALSE;
            wcscat(appdata, L"\\AppData\\Roaming");
        }
        if (_snwprintf(dir, capacity, L"%ls\\Microsoft\\Protect\\%ls", appdata, sid) < 0)
            return FALSE;
    }
    dir[capacity - 1] = 0;
    /* the sid with its terminator, as windows mixes it in. */
    mime_hmac(&mime_sha1, password, sizeof(password), sid, (wcslen(sid) + 1) * sizeof(WCHAR), prekey);
    SecureZeroMemory(password, sizeof(password));
    return TRUE;
}

static BOOL create_directories(const WCHAR *path)
{
    WCHAR partial[MAX_PATH * 2];
    SIZE_T length = wcslen(path);
    if (length >= RTL_NUMBER_OF(partial))
        return FALSE;
    for (SIZE_T i = 3; i <= length; i++) {
        if (path[i] != L'\\' && path[i])
            continue;
        memcpy(partial, path, i * sizeof(WCHAR));
        partial[i] = 0;
        if (!CreateDirectoryW(partial, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
            return FALSE;
    }
    return TRUE;
}

static BOOL read_file(const WCHAR *path, BYTE *buffer, DWORD capacity, DWORD *size)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    BOOL ok = ReadFile(file, buffer, capacity, size, nullptr);
    CloseHandle(file);
    return ok;
}

static BOOL write_file(const WCHAR *path, const BYTE *data, DWORD size, DWORD disposition)
{
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, disposition,
                              FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    DWORD written;
    BOOL ok = WriteFile(file, data, size, &written, nullptr) && written == size;
    CloseHandle(file);
    return ok;
}

/*
 * the master key file: a header with the key's guid in text and the lengths
 * of its sections, then the master key section: version 2, a salt, the
 * rounds of pbkdf2, the hash and cipher, and the encrypted hmac salt, hmac
 * and key. a credential history section follows with no history.
 */
#pragma pack(push, 4)
typedef struct {
    DWORD version;
    DWORD reserved1;
    DWORD reserved2;
    WCHAR guid[36];
    DWORD unused1;
    DWORD unused2;
    DWORD policy;
    ULONG64 master_key_size;
    ULONG64 backup_key_size;
    ULONG64 credhist_size;
    ULONG64 domain_key_size;
} MASTER_KEY_FILE;

typedef struct {
    DWORD version;
    BYTE salt[16];
    DWORD rounds;
    DWORD hash;
    DWORD cipher;
    BYTE encrypted[16 + 64 + 64];
} MASTER_KEY_SECTION;
#pragma pack(pop)

static_assert(sizeof(MASTER_KEY_FILE) == 128, "master key header");

/* the key and iv that encrypt a master key: PBKDF2 of the pre-key, per the public DPAPI descriptions. */
static void master_key_cipher(const BYTE prekey[20], const MASTER_KEY_SECTION *section, MimeAes *aes, BYTE iv[16])
{
    BYTE derived[48];
    mime_pbkdf2(&mime_sha512, prekey, 20, section->salt, sizeof(section->salt), section->rounds, derived,
                sizeof(derived));
    mime_aes_init(aes, derived, 32);
    memcpy(iv, derived + 32, 16);
    SecureZeroMemory(derived, sizeof(derived));
}

/* the check over a master key: hmac of the key under hmac of the hmac salt, both keyed from the pre-key. */
static void master_key_hmac(const BYTE prekey[20], const BYTE *salt, const BYTE *key, BYTE *out)
{
    BYTE inner[64];
    mime_hmac(&mime_sha512, prekey, 20, salt, 16, inner);
    mime_hmac(&mime_sha512, inner, sizeof(inner), key, 64, out);
    SecureZeroMemory(inner, sizeof(inner));
}

static BOOL load_master_key(const WCHAR *dir, const BYTE prekey[20], const GUID *guid, MASTER_KEY *out)
{
    WCHAR path[MAX_PATH * 2], name[37];
    guid_text(guid, name);
    _snwprintf(path, RTL_NUMBER_OF(path), L"%ls\\%ls", dir, name);
    path[RTL_NUMBER_OF(path) - 1] = 0;
    BYTE file[1024];
    DWORD size;
    if (!read_file(path, file, sizeof(file), &size) || size < sizeof(MASTER_KEY_FILE) + sizeof(MASTER_KEY_SECTION))
        return FALSE;
    const MASTER_KEY_FILE *header = (const MASTER_KEY_FILE *)file;
    MASTER_KEY_SECTION section;
    memcpy(&section, file + sizeof(*header), sizeof(section));
    if (header->version != 2 || header->master_key_size < sizeof(section) || section.version != 2 ||
        section.hash != CALG_SHA_512_ || section.cipher != CALG_AES_256_ || !section.rounds)
        return FALSE;
    MimeAes aes;
    BYTE iv[16], plain[sizeof(section.encrypted)], check[64];
    master_key_cipher(prekey, &section, &aes, iv);
    mime_cbc_decrypt(&mime_cipher_aes, &aes, iv, section.encrypted, plain, sizeof(plain));
    master_key_hmac(prekey, plain, plain + 80, check);
    BOOL ok = mime_ct_equal(check, plain + 16, 64);
    if (ok) {
        out->guid = *guid;
        memcpy(out->key, plain + 80, 64);
    }
    SecureZeroMemory(&aes, sizeof(aes));
    SecureZeroMemory(plain, sizeof(plain));
    return ok;
}

static BOOL create_master_key(const WCHAR *dir, const BYTE prekey[20], MASTER_KEY *out)
{
    BYTE file[sizeof(MASTER_KEY_FILE) + sizeof(MASTER_KEY_SECTION) + 20] = {};
    MASTER_KEY_FILE *header = (MASTER_KEY_FILE *)file;
    MASTER_KEY_SECTION section = {.version = 2, .rounds = MASTER_KEY_ROUNDS, .hash = CALG_SHA_512_,
                                  .cipher = CALG_AES_256_};
    BYTE plain[sizeof(section.encrypted)];
    if (!mime_crypto_random(&out->guid, sizeof(out->guid)) || !mime_crypto_random(out->key, sizeof(out->key)) ||
        !mime_crypto_random(section.salt, sizeof(section.salt)) || !mime_crypto_random(plain, 16))
        return FALSE;
    /* a random version 4 guid. */
    out->guid.Data3 = (out->guid.Data3 & 0x0FFF) | 0x4000;
    out->guid.Data4[0] = (out->guid.Data4[0] & 0x3F) | 0x80;
    master_key_hmac(prekey, plain, out->key, plain + 16);
    memcpy(plain + 80, out->key, 64);
    MimeAes aes;
    BYTE iv[16];
    master_key_cipher(prekey, &section, &aes, iv);
    mime_cbc_encrypt(&mime_cipher_aes, &aes, iv, plain, section.encrypted, sizeof(plain));
    SecureZeroMemory(&aes, sizeof(aes));
    SecureZeroMemory(plain, sizeof(plain));
    WCHAR name[37];
    guid_text(&out->guid, name);
    header->version = 2;
    memcpy(header->guid, name, sizeof(header->guid));
    header->policy = 4;
    header->master_key_size = sizeof(section);
    header->credhist_size = 20;
    memcpy(file + sizeof(*header), &section, sizeof(section));
    /* the credential history section: version 3 and no history guid. */
    file[sizeof(*header) + sizeof(section)] = 3;
    WCHAR path[MAX_PATH * 2];
    _snwprintf(path, RTL_NUMBER_OF(path), L"%ls\\%ls", dir, name);
    path[RTL_NUMBER_OF(path) - 1] = 0;
    return create_directories(dir) && write_file(path, file, sizeof(file), CREATE_NEW);
}

static BOOL cached_key(BOOL machine, const GUID *guid, MASTER_KEY *out)
{
    BOOL found = FALSE;
    AcquireSRWLockShared(&g_keys_lock);
    for (CACHED_KEY *k = g_keys; k && !found; k = k->next)
        if (k->machine == machine && !memcmp(&k->key.guid, guid, sizeof(GUID))) {
            *out = k->key;
            found = TRUE;
        }
    ReleaseSRWLockShared(&g_keys_lock);
    return found;
}

static void cache_key(BOOL machine, const MASTER_KEY *key)
{
    CACHED_KEY *k = CrAlloc(sizeof(*k));
    if (!k)
        return;
    k->machine = machine;
    k->key = *key;
    AcquireSRWLockExclusive(&g_keys_lock);
    k->next = g_keys;
    g_keys = k;
    ReleaseSRWLockExclusive(&g_keys_lock);
}

static BOOL find_master_key(BOOL machine, const GUID *guid, MASTER_KEY *out)
{
    if (cached_key(machine, guid, out))
        return TRUE;
    WCHAR dir[MAX_PATH * 2];
    BYTE prekey[20];
    if (!key_store(machine, dir, RTL_NUMBER_OF(dir), prekey))
        return FALSE;
    BOOL ok = load_master_key(dir, prekey, guid, out);
    SecureZeroMemory(prekey, sizeof(prekey));
    if (ok)
        cache_key(machine, out);
    return ok;
}

/* the key the Preferred file names while it has not expired, else a new key it then names. */
static BOOL preferred_master_key(BOOL machine, MASTER_KEY *out)
{
    WCHAR dir[MAX_PATH * 2], path[MAX_PATH * 2];
    BYTE prekey[20];
    if (!key_store(machine, dir, RTL_NUMBER_OF(dir), prekey))
        return FALSE;
    _snwprintf(path, RTL_NUMBER_OF(path), L"%ls\\Preferred", dir);
    path[RTL_NUMBER_OF(path) - 1] = 0;
    /* processes creating the first key at once must agree on one. */
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\DataProtectionMasterKeys");
    if (mutex)
        WaitForSingleObject(mutex, INFINITE);
    BYTE preferred[24];
    DWORD size;
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    ULONG64 time = (ULONG64)now.dwHighDateTime << 32 | now.dwLowDateTime, expires;
    BOOL ok = FALSE;
    if (read_file(path, preferred, sizeof(preferred), &size) && size == sizeof(preferred)) {
        memcpy(&expires, preferred + 16, 8);
        if (expires > time) {
            GUID guid;
            memcpy(&guid, preferred, sizeof(guid));
            ok = cached_key(machine, &guid, out) || load_master_key(dir, prekey, &guid, out);
        }
    }
    if (!ok && create_master_key(dir, prekey, out)) {
        expires = time + MASTER_KEY_LIFETIME;
        memcpy(preferred, &out->guid, sizeof(GUID));
        memcpy(preferred + 16, &expires, 8);
        ok = write_file(path, preferred, sizeof(preferred), CREATE_ALWAYS);
    }
    if (ok && !cached_key(machine, &out->guid, &(MASTER_KEY){}))
        cache_key(machine, out);
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    SecureZeroMemory(prekey, sizeof(prekey));
    return ok;
}

/* ------------------------------------------------------------------ blobs */

static const MimeHashAlg *blob_hash(DWORD alg)
{
    switch (alg) {
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

static DWORD blob_key_size(DWORD alg)
{
    switch (alg) {
    case CALG_AES_128_:
        return 16;
    case CALG_AES_192_:
        return 24;
    case CALG_AES_256_:
        return 32;
    case CALG_3DES_:
        return 24;
    }
    return 0;
}

/* HMAC keyed with the master key's SHA-1 over a salt, then the entropy: the session key and the signing key. */
static void blob_key(const MimeHashAlg *hash, const MASTER_KEY *key, const BYTE *salt, DWORD salt_size,
                     const DATA_BLOB *entropy, BYTE *out)
{
    BYTE digest[20];
    MimeHmac h;
    mime_hash(&mime_sha1, key->key, sizeof(key->key), digest);
    mime_hmac_init(&h, hash, digest, sizeof(digest));
    mime_hmac_update(&h, salt, salt_size);
    if (entropy && entropy->pbData)
        mime_hmac_update(&h, entropy->pbData, entropy->cbData);
    mime_hmac_final(&h, out);
    SecureZeroMemory(&h, sizeof(h));
    SecureZeroMemory(digest, sizeof(digest));
}

/* a block cipher over the session key, cbc with a zero iv. */
static BOOL blob_cipher(DWORD alg, const MimeHashAlg *hash, const BYTE *session, BOOL encrypt, BYTE *data, DWORD size)
{
    DWORD key_size = blob_key_size(alg);
    BYTE key[2 * MIME_HASH_MAX_DIGEST], iv[16] = {};
    mime_capi_expand(hash, session, key, key_size);
    union {
        MimeAes aes;
        MimeDes3 des3;
    } schedule;
    const MimeCipher *cipher = &mime_cipher_aes;
    if (alg == CALG_3DES_) {
        mime_des3_init(&schedule.des3, key, key_size);
        cipher = &mime_cipher_des3;
    } else {
        mime_aes_init(&schedule.aes, key, key_size);
    }
    (encrypt ? mime_cbc_encrypt : mime_cbc_decrypt)(cipher, &schedule, iv, data, data, size);
    SecureZeroMemory(&schedule, sizeof(schedule));
    SecureZeroMemory(key, sizeof(key));
    return TRUE;
}

static void put(BYTE **p, const void *data, DWORD size)
{
    memcpy(*p, data, size);
    *p += size;
}

static void put32(BYTE **p, DWORD value)
{
    put(p, &value, 4);
}

/* the prompt applies only when it asks for one; without ui it is skipped, or refused when ui is forbidden. */
static DWORD prompt_flags(const CRYPTPROTECT_PROMPTSTRUCT *prompt)
{
    return prompt ? prompt->dwPromptFlags & (CRYPTPROTECT_PROMPT_ON_UNPROTECT | CRYPTPROTECT_PROMPT_ON_PROTECT |
                                             CRYPTPROTECT_PROMPT_STRONG | CRYPTPROTECT_PROMPT_REQUIRE_STRONG)
                  : 0;
}

BOOL WINAPI CryptProtectData(DATA_BLOB *pDataIn, LPCWSTR szDataDescr, DATA_BLOB *pOptionalEntropy, PVOID pvReserved,
                             CRYPTPROTECT_PROMPTSTRUCT *pPromptStruct, DWORD dwFlags, DATA_BLOB *pDataOut)
{
    if (!pDataOut || !pDataIn || !pDataIn->pbData)
        return CrFail(ERROR_INVALID_PARAMETER);
    pDataOut->cbData = 0;
    pDataOut->pbData = nullptr;
    DWORD prompt = prompt_flags(pPromptStruct);
    if ((prompt & (CRYPTPROTECT_PROMPT_ON_PROTECT | CRYPTPROTECT_PROMPT_ON_UNPROTECT)) &&
        (dwFlags & CRYPTPROTECT_UI_FORBIDDEN))
        return CrFail(ERROR_PASSWORD_RESTRICTION);
    if (!szDataDescr)
        szDataDescr = L"";
    BOOL machine = (dwFlags & CRYPTPROTECT_LOCAL_MACHINE) != 0;
    MASTER_KEY key;
    if (!preferred_master_key(machine, &key))
        return CrFail(NTE_BAD_KEY_STATE);
    const MimeHashAlg *hash = &mime_sha512;
    DWORD description = (DWORD)(wcslen(szDataDescr) + 1) * sizeof(WCHAR);
    DWORD padded = (pDataIn->cbData / 16 + 1) * 16;
    DWORD size = 4 + 16 + 4 + 16 + 4 + 4 + description + 4 + 4 + 4 + BLOB_SALT + 4 + 4 + 4 + 4 + BLOB_HMAC_KEY + 4 +
                 padded + 4 + hash->digest_size;
    BYTE *blob = LocalAlloc(LMEM_FIXED, size);
    if (!blob) {
        SecureZeroMemory(&key, sizeof(key));
        return CrFail(ERROR_OUTOFMEMORY);
    }
    BYTE salt[BLOB_SALT], hmac_key[BLOB_HMAC_KEY], session[MIME_HASH_MAX_DIGEST];
    if (!mime_crypto_random(salt, sizeof(salt)) || !mime_crypto_random(hmac_key, sizeof(hmac_key))) {
        LocalFree(blob);
        SecureZeroMemory(&key, sizeof(key));
        return CrFail(NTE_FAIL);
    }
    BYTE *p = blob;
    put32(&p, 1);
    put(&p, &g_provider, sizeof(GUID));
    put32(&p, 1);
    put(&p, &key.guid, sizeof(GUID));
    put32(&p, (dwFlags & (CRYPTPROTECT_LOCAL_MACHINE | CRYPTPROTECT_SYSTEM)) | prompt);
    put32(&p, description);
    put(&p, szDataDescr, description);
    put32(&p, CALG_AES_256_);
    put32(&p, 256);
    put32(&p, sizeof(salt));
    put(&p, salt, sizeof(salt));
    put32(&p, 0);
    put32(&p, CALG_SHA_512_);
    put32(&p, 512);
    put32(&p, sizeof(hmac_key));
    put(&p, hmac_key, sizeof(hmac_key));
    put32(&p, padded);
    BYTE *data = p;
    memcpy(data, pDataIn->pbData, pDataIn->cbData);
    memset(data + pDataIn->cbData, (int)(padded - pDataIn->cbData), padded - pDataIn->cbData);
    blob_key(hash, &key, salt, sizeof(salt), pOptionalEntropy, session);
    blob_cipher(CALG_AES_256_, hash, session, TRUE, data, padded);
    p += padded;
    /* the signature covers the blob from the master key version to the end of the data. */
    blob_key(hash, &key, hmac_key, sizeof(hmac_key), pOptionalEntropy, session);
    put32(&p, hash->digest_size);
    mime_hmac(hash, session, hash->digest_size, blob + 20, (SIZE_T)(data + padded - (blob + 20)), p);
    SecureZeroMemory(session, sizeof(session));
    SecureZeroMemory(&key, sizeof(key));
    pDataOut->cbData = size;
    pDataOut->pbData = blob;
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

/* a reader over the blob that fails once it would run past the end. */
typedef struct {
    const BYTE *p;
    DWORD left;
    BOOL bad;
} READER;

static const BYTE *take(READER *r, DWORD size)
{
    if (r->bad || size > r->left) {
        r->bad = TRUE;
        return nullptr;
    }
    const BYTE *p = r->p;
    r->p += size;
    r->left -= size;
    return p;
}

static DWORD take32(READER *r)
{
    const BYTE *p = take(r, 4);
    DWORD v = 0;
    if (p)
        memcpy(&v, p, 4);
    return v;
}

BOOL WINAPI CryptUnprotectData(DATA_BLOB *pDataIn, LPWSTR *ppszDataDescr, DATA_BLOB *pOptionalEntropy,
                               PVOID pvReserved, CRYPTPROTECT_PROMPTSTRUCT *pPromptStruct, DWORD dwFlags,
                               DATA_BLOB *pDataOut)
{
    if (!pDataOut || !pDataIn || !pDataIn->pbData || pDataIn->cbData < 0x34)
        return CrFail(ERROR_INVALID_PARAMETER);
    pDataOut->cbData = 0;
    pDataOut->pbData = nullptr;
    if (ppszDataDescr)
        *ppszDataDescr = nullptr;
    READER r = {pDataIn->pbData, pDataIn->cbData};
    GUID provider, guid;
    DWORD version = take32(&r);
    const BYTE *field = take(&r, 16);
    if (field)
        memcpy(&provider, field, 16);
    const BYTE *signed_start = r.p;
    DWORD key_version = take32(&r);
    field = take(&r, 16);
    if (field)
        memcpy(&guid, field, 16);
    DWORD flags = take32(&r), description_size = take32(&r);
    const WCHAR *description = (const WCHAR *)take(&r, description_size);
    DWORD crypt_alg = take32(&r);
    take32(&r);
    DWORD salt_size = take32(&r);
    const BYTE *salt = take(&r, salt_size);
    DWORD hmac_key_size = take32(&r);
    take(&r, hmac_key_size);
    DWORD hash_alg = take32(&r);
    take32(&r);
    DWORD hmac2_size = take32(&r);
    const BYTE *hmac2 = take(&r, hmac2_size);
    DWORD data_size = take32(&r);
    const BYTE *data = take(&r, data_size);
    const BYTE *signed_end = r.p;
    DWORD sign_size = take32(&r);
    const BYTE *sign = take(&r, sign_size);
    if (r.bad || version != 1 || key_version != 1 || memcmp(&provider, &g_provider, sizeof(GUID)) ||
        description_size < sizeof(WCHAR) || description_size % sizeof(WCHAR) ||
        description[description_size / sizeof(WCHAR) - 1])
        return CrFail(ERROR_INVALID_DATA);
    DWORD prompt = flags & (CRYPTPROTECT_PROMPT_ON_UNPROTECT | CRYPTPROTECT_PROMPT_ON_PROTECT |
                            CRYPTPROTECT_PROMPT_STRONG | CRYPTPROTECT_PROMPT_REQUIRE_STRONG);
    if (prompt & (CRYPTPROTECT_PROMPT_ON_UNPROTECT | CRYPTPROTECT_PROMPT_ON_PROTECT)) {
        if (dwFlags & CRYPTPROTECT_UI_FORBIDDEN)
            return CrFail(ERROR_PASSWORD_RESTRICTION);
    } else if (prompt & CRYPTPROTECT_PROMPT_STRONG) {
        return CrFail(ERROR_INVALID_PARAMETER);
    }
    const MimeHashAlg *hash = blob_hash(hash_alg);
    DWORD block = crypt_alg == CALG_3DES_ ? 8 : 16;
    if (!hash || !blob_key_size(crypt_alg) || !data_size || data_size % block || sign_size != hash->digest_size)
        return CrFail(ERROR_INVALID_DATA);
    BOOL machine = (flags & CRYPTPROTECT_LOCAL_MACHINE) != 0;
    MASTER_KEY key;
    if (!find_master_key(machine, &guid, &key))
        return CrFail(NTE_BAD_KEY_STATE);
    BYTE session[MIME_HASH_MAX_DIGEST], check[MIME_HASH_MAX_DIGEST];
    blob_key(hash, &key, hmac2, hmac2_size, pOptionalEntropy, session);
    mime_hmac(hash, session, hash->digest_size, signed_start, (SIZE_T)(signed_end - signed_start), check);
    if (!mime_ct_equal(check, sign, sign_size)) {
        SecureZeroMemory(&key, sizeof(key));
        SecureZeroMemory(session, sizeof(session));
        return CrFail(ERROR_INVALID_DATA);
    }
    BYTE *plain = LocalAlloc(LMEM_FIXED, data_size);
    if (!plain) {
        SecureZeroMemory(&key, sizeof(key));
        return CrFail(ERROR_OUTOFMEMORY);
    }
    memcpy(plain, data, data_size);
    blob_key(hash, &key, salt, salt_size, pOptionalEntropy, session);
    blob_cipher(crypt_alg, hash, session, FALSE, plain, data_size);
    SecureZeroMemory(session, sizeof(session));
    BYTE pad = plain[data_size - 1];
    BOOL bad = !pad || pad > block;
    for (DWORD i = 0; i < block && !bad; i++)
        bad |= i >= block - pad && plain[data_size - block + i] != pad;
    if (bad) {
        SecureZeroMemory(plain, data_size);
        LocalFree(plain);
        SecureZeroMemory(&key, sizeof(key));
        return CrFail(ERROR_INVALID_DATA);
    }
    if (ppszDataDescr) {
        *ppszDataDescr = LocalAlloc(LMEM_FIXED, description_size);
        if (*ppszDataDescr)
            memcpy(*ppszDataDescr, description, description_size);
    }
    pDataOut->cbData = data_size - pad;
    pDataOut->pbData = plain;
    DWORD error = ERROR_SUCCESS;
    if (dwFlags & CRYPTPROTECT_VERIFY_PROTECTION) {
        /* data under a key that is no longer the preferred one asks to be protected again. */
        MASTER_KEY current;
        if (preferred_master_key(machine, &current) && memcmp(&current.guid, &key.guid, sizeof(GUID)))
            error = CRYPT_I_NEW_PROTECTION_REQUIRED;
        SecureZeroMemory(&current, sizeof(current));
    }
    SecureZeroMemory(&key, sizeof(key));
    SetLastError(error);
    return TRUE;
}

/* master keys never change owner in mime: there is no password to change. */
BOOL WINAPI CryptUpdateProtectedState(PSID pOldSid, LPCWSTR pwszOldPassword, DWORD dwFlags, DWORD *pdwSuccessCount,
                                      DWORD *pdwFailureCount)
{
    if (pdwSuccessCount)
        *pdwSuccessCount = 0;
    if (pdwFailureCount)
        *pdwFailureCount = 0;
    return TRUE;
}

/* ------------------------------------------------------------------ memory */

/*
 * windows encrypts these blocks in the kernel with per process, per logon session or per boot keys. mime derives
 * all three from a boot secret, SHA-256 of the machine guid and boot time, mixed with the process id and creation
 * time or the logon id: as stable as windows' keys, with no state to keep.
 */
static BOOL memory_key(DWORD flags, MimeAes *aes, BYTE iv[16])
{
    struct {
        LARGE_INTEGER BootTime;
        LARGE_INTEGER CurrentTime;
        BYTE rest[32];
    } day;
    WCHAR guid[64] = {};
    if (!NT_SUCCESS(NtQuerySystemInformation(SystemTimeOfDayInformation, &day, sizeof(day), nullptr)))
        return FALSE;
    machine_guid(guid, RTL_NUMBER_OF(guid));
    BYTE boot[32], key[32], context[32] = {};
    MimeHashCtx ctx;
    mime_sha256.init(&ctx);
    mime_sha256.update(&ctx, "mime memory protection", 22);
    mime_sha256.update(&ctx, guid, sizeof(guid));
    mime_sha256.update(&ctx, &day.BootTime, sizeof(day.BootTime));
    mime_sha256.final(&ctx, boot);
    DWORD context_size = 4;
    memcpy(context, &flags, 4);
    if (flags == CRYPTPROTECTMEMORY_SAME_PROCESS) {
        struct {
            LARGE_INTEGER CreateTime, ExitTime, KernelTime, UserTime;
        } times = {};
        ULONG64 pid = GetCurrentProcessId();
        NtQueryInformationProcess(NtCurrentProcess(), ProcessTimes, &times, sizeof(times), nullptr);
        memcpy(context + 4, &pid, 8);
        memcpy(context + 12, &times.CreateTime, 8);
        context_size = 20;
    } else if (flags == CRYPTPROTECTMEMORY_SAME_LOGON) {
        HANDLE token;
        BYTE statistics[64] = {};
        ULONG size;
        if (NT_SUCCESS(NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY, &token))) {
            NtQueryInformationToken(token, TOKEN_STATISTICS_CLASS, statistics, sizeof(statistics), &size);
            NtClose(token);
        }
        /* the authentication id, the logon session's luid. */
        memcpy(context + 4, statistics + 8, 8);
        context_size = 12;
    }
    mime_hmac(&mime_sha256, boot, sizeof(boot), context, context_size, key);
    mime_aes_init(aes, key, sizeof(key));
    mime_hash(&mime_sha256, key, sizeof(key), boot);
    memcpy(iv, boot, 16);
    SecureZeroMemory(key, sizeof(key));
    SecureZeroMemory(boot, sizeof(boot));
    return TRUE;
}

static BOOL protect_memory(LPVOID data, DWORD size, DWORD flags, BOOL encrypt)
{
    if (flags > CRYPTPROTECTMEMORY_SAME_LOGON || size % CRYPTPROTECTMEMORY_BLOCK_SIZE || (!data && size))
        return CrFail(ERROR_INVALID_PARAMETER);
    if (!size)
        return TRUE;
    MimeAes aes;
    BYTE iv[16];
    if (!memory_key(flags, &aes, iv))
        return CrFail(ERROR_INVALID_PARAMETER);
    (encrypt ? mime_cbc_encrypt : mime_cbc_decrypt)(&mime_cipher_aes, &aes, iv, data, data, size);
    SecureZeroMemory(&aes, sizeof(aes));
    return TRUE;
}

BOOL WINAPI CryptProtectMemory(LPVOID pDataIn, DWORD cbDataIn, DWORD dwFlags)
{
    return protect_memory(pDataIn, cbDataIn, dwFlags, TRUE);
}

BOOL WINAPI CryptUnprotectMemory(LPVOID pDataIn, DWORD cbDataIn, DWORD dwFlags)
{
    return protect_memory(pDataIn, cbDataIn, dwFlags, FALSE);
}
