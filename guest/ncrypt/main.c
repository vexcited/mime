/*
 * ncrypt: the key storage router and the Microsoft software key storage
 * provider, whose keys are bcrypt keys. what bcrypt cannot do fails with
 * NTE_NOT_SUPPORTED. named keys persist as their private blob under the
 * user's or the machine's Crypto\Keys directory. the platform crypto
 * provider finds no tpm.
 */
#define NCRYPTAPI
#include <mime/win32/ncrypt.h>
#include <crypto/crypto.h>

#define PROVIDER_MAGIC 0x44440001
#define KEY_MAGIC 0x44440002
#define SECRET_MAGIC 0x44440003
#define TBS_E_TPM_NOT_FOUND ((SECURITY_STATUS)0x8028400FL)
#define STATUS_NOT_FOUND_ ((SECURITY_STATUS)0xC0000225L)
#define FILE_MAGIC 0x50534B4D

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }


/* ------------------------------------------------------------------ bcrypt */

typedef void *BCRYPT_HANDLE;
static struct {
    LONG (WINAPI *open)(BCRYPT_HANDLE *, LPCWSTR, LPCWSTR, ULONG);
    LONG (WINAPI *close)(BCRYPT_HANDLE, ULONG);
    LONG (WINAPI *generate)(BCRYPT_HANDLE, BCRYPT_HANDLE *, ULONG, ULONG);
    LONG (WINAPI *finalize)(BCRYPT_HANDLE, ULONG);
    LONG (WINAPI *destroy)(BCRYPT_HANDLE);
    LONG (WINAPI *export_key)(BCRYPT_HANDLE, BCRYPT_HANDLE, LPCWSTR, PUCHAR, ULONG, ULONG *, ULONG);
    LONG (WINAPI *import_pair)(BCRYPT_HANDLE, BCRYPT_HANDLE, LPCWSTR, BCRYPT_HANDLE *, PUCHAR, ULONG, ULONG);
    LONG (WINAPI *sign)(BCRYPT_HANDLE, void *, PUCHAR, ULONG, PUCHAR, ULONG, ULONG *, ULONG);
    LONG (WINAPI *verify)(BCRYPT_HANDLE, void *, PUCHAR, ULONG, PUCHAR, ULONG, ULONG);
    LONG (WINAPI *encrypt)(BCRYPT_HANDLE, PUCHAR, ULONG, void *, PUCHAR, ULONG, PUCHAR, ULONG, ULONG *, ULONG);
    LONG (WINAPI *decrypt)(BCRYPT_HANDLE, PUCHAR, ULONG, void *, PUCHAR, ULONG, PUCHAR, ULONG, ULONG *, ULONG);
    LONG (WINAPI *agree)(BCRYPT_HANDLE, BCRYPT_HANDLE, BCRYPT_HANDLE *, ULONG);
    LONG (WINAPI *derive)(BCRYPT_HANDLE, LPCWSTR, void *, PUCHAR, ULONG, ULONG *, ULONG);
    LONG (WINAPI *destroy_secret)(BCRYPT_HANDLE);
} g_bcrypt;

static INIT_ONCE g_bcrypt_once = INIT_ONCE_STATIC_INIT;

static BOOL WINAPI load_bcrypt(INIT_ONCE *once, void *param, void **context)
{
    HMODULE module = LoadLibraryW(L"bcrypt.dll");
    if (!module)
        return TRUE;
    static const char *const names[] = {
        "BCryptOpenAlgorithmProvider", "BCryptCloseAlgorithmProvider", "BCryptGenerateKeyPair",
        "BCryptFinalizeKeyPair", "BCryptDestroyKey", "BCryptExportKey", "BCryptImportKeyPair", "BCryptSignHash",
        "BCryptVerifySignature", "BCryptEncrypt", "BCryptDecrypt", "BCryptSecretAgreement", "BCryptDeriveKey",
        "BCryptDestroySecret",
    };
    FARPROC *slots = (FARPROC *)&g_bcrypt;
    for (UINT i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        slots[i] = GetProcAddress(module, names[i]);
    return TRUE;
}

static void bcrypt(void)
{
    InitOnceExecuteOnce(&g_bcrypt_once, load_bcrypt, nullptr, nullptr);
}

/* maps a bcrypt NTSTATUS to the NTE code the router returns. */
static SECURITY_STATUS normalize(LONG status)
{
    switch ((ULONG)status) {
    case 0: return ERROR_SUCCESS;
    case 0xC0000008: return NTE_INVALID_HANDLE;
    case 0xC000000D: return NTE_INVALID_PARAMETER;
    case 0xC0000017: case 0xC000009A: return NTE_NO_MEMORY;
    case 0xC0000023: return NTE_BUFFER_TOO_SMALL;
    case 0xC00000BB: return NTE_NOT_SUPPORTED;
    case 0xC0000225: return NTE_NOT_SUPPORTED;
    }
    return status;
}

/* ------------------------------------------------------------------ objects */

typedef struct OBJECT {
    DWORD magic;
    struct OBJECT *next;
} OBJECT;

typedef struct {
    OBJECT header;
} PROVIDER;

typedef struct {
    OBJECT header;
    PROVIDER *provider;
    WCHAR algorithm[32];
    LPWSTR name;
    DWORD length, export_policy, key_usage, flags;
    BOOL finalized;
    BCRYPT_HANDLE alg, key;
    /* the key spec of a key that lives, or is to live, in a container of the legacy store: 0, AT_KEYEXCHANGE or AT_SIGNATURE. */
    DWORD legacy_spec;
    BOOL from_legacy;
    /* the pair came with NCryptImportKey, which a later NCryptFinalizeKey keeps. */
    BOOL imported;
} KEY;

typedef struct {
    OBJECT header;
    BCRYPT_HANDLE secret;
} SECRET;

static SRWLOCK g_lock = SRWLOCK_INIT;
static OBJECT *g_objects;

static void *new_object(DWORD magic, SIZE_T size)
{
    OBJECT *object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
    if (!object)
        return nullptr;
    object->magic = magic;
    AcquireSRWLockExclusive(&g_lock);
    object->next = g_objects;
    g_objects = object;
    ReleaseSRWLockExclusive(&g_lock);
    return object;
}

static void *find_object(NCRYPT_HANDLE handle, DWORD magic)
{
    void *found = nullptr;
    AcquireSRWLockShared(&g_lock);
    for (OBJECT *object = g_objects; object; object = object->next)
        if ((NCRYPT_HANDLE)object == handle && (!magic || object->magic == magic)) {
            found = object;
            break;
        }
    ReleaseSRWLockShared(&g_lock);
    return found;
}

static void free_object(OBJECT *object)
{
    AcquireSRWLockExclusive(&g_lock);
    for (OBJECT **link = &g_objects; *link; link = &(*link)->next)
        if (*link == object) {
            *link = object->next;
            break;
        }
    ReleaseSRWLockExclusive(&g_lock);
    object->magic = 0;
    HeapFree(GetProcessHeap(), 0, object);
}

static void close_key(KEY *key)
{
    if (key->key && g_bcrypt.destroy)
        g_bcrypt.destroy(key->key);
    if (key->alg && g_bcrypt.close)
        g_bcrypt.close(key->alg, 0);
    HeapFree(GetProcessHeap(), 0, key->name);
    free_object(&key->header);
}

/* ------------------------------------------------------------------ algorithms */

static const struct {
    LPCWSTR name, group, blob;
    DWORD length;
} g_algorithms[] = {
    {L"RSA", L"RSA", L"RSAFULLPRIVATEBLOB", 2048},
    {L"ECDSA_P256", L"ECDSA", L"ECCPRIVATEBLOB", 256},
    {L"ECDSA_P384", L"ECDSA", L"ECCPRIVATEBLOB", 384},
    {L"ECDSA_P521", L"ECDSA", L"ECCPRIVATEBLOB", 521},
    {L"ECDH_P256", L"ECDH", L"ECCPRIVATEBLOB", 256},
    {L"ECDH_P384", L"ECDH", L"ECCPRIVATEBLOB", 384},
    {L"ECDH_P521", L"ECDH", L"ECCPRIVATEBLOB", 521},
    {L"DSA", L"DSA", L"DSAPRIVATEBLOB", 1024},
    {L"DH", L"DH", L"DHPRIVATEBLOB", 1024},
};

static int find_algorithm(LPCWSTR name)
{
    for (UINT i = 0; name && i < sizeof(g_algorithms) / sizeof(g_algorithms[0]); i++)
        if (!_wcsicmp(name, g_algorithms[i].name))
            return (int)i;
    return -1;
}

/* opens an algorithm from the provider's list with bcrypt. */
static SECURITY_STATUS open_algorithm(LPCWSTR name, BCRYPT_HANDLE *alg)
{
    *alg = nullptr;
    if (find_algorithm(name) < 0)
        return NTE_NOT_SUPPORTED;
    bcrypt();
    if (!g_bcrypt.open)
        return NTE_NOT_SUPPORTED;
    return normalize(g_bcrypt.open(alg, g_algorithms[find_algorithm(name)].name, nullptr, 0));
}

/* ------------------------------------------------------------------ providers */

/*
 * the rsa providers of capi whose containers the software key storage provider also opens, as crypt32 asks it to when
 * it prefers a cng handle for a certificate whose key is in one of them.
 */
static BOOL legacy_provider_name(LPCWSTR name)
{
    static const LPCWSTR names[] = {
        L"Microsoft Base Cryptographic Provider v1.0",
        L"Microsoft Enhanced Cryptographic Provider v1.0",
        L"Microsoft Strong Cryptographic Provider",
        L"Microsoft Enhanced RSA and AES Cryptographic Provider",
        L"Microsoft Enhanced RSA and AES Cryptographic Provider (Prototype)",
        L"Microsoft RSA SChannel Cryptographic Provider",
    };
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(names); i++)
        if (!_wcsicmp(name, names[i]))
            return TRUE;
    return FALSE;
}

SECURITY_STATUS WINAPI NCryptOpenStorageProvider(NCRYPT_PROV_HANDLE *handle, LPCWSTR name, DWORD flags)
{
    if (!handle)
        return NTE_INVALID_PARAMETER;
    *handle = 0;
    if (name && *name && _wcsicmp(name, MS_KEY_STORAGE_PROVIDER) && !legacy_provider_name(name)) {
        if (!_wcsicmp(name, MS_PLATFORM_CRYPTO_PROVIDER))
            return (flags & ~0x1040u) ? NTE_BAD_FLAGS : TBS_E_TPM_NOT_FOUND;
        return STATUS_NOT_FOUND_;
    }
    PROVIDER *provider = new_object(PROVIDER_MAGIC, sizeof(*provider));
    if (!provider)
        return NTE_NO_MEMORY;
    *handle = (NCRYPT_PROV_HANDLE)provider;
    return ERROR_SUCCESS;
}

SECURITY_STATUS WINAPI NCryptEnumStorageProviders(DWORD *count, NCryptProviderName **list, DWORD flags)
{
    static const LPCWSTR names[] = {MS_KEY_STORAGE_PROVIDER, MS_PLATFORM_CRYPTO_PROVIDER};
    if (!count || !list)
        return NTE_INVALID_PARAMETER;
    SIZE_T size = sizeof(names) / sizeof(names[0]) * sizeof(NCryptProviderName);
    for (UINT i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        size += (wcslen(names[i]) + 1) * sizeof(WCHAR);
    BYTE *buffer = HeapAlloc(GetProcessHeap(), 0, size);
    if (!buffer)
        return NTE_NO_MEMORY;
    NCryptProviderName *entries = (NCryptProviderName *)buffer;
    WCHAR *strings = (WCHAR *)(entries + sizeof(names) / sizeof(names[0]));
    for (UINT i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        SIZE_T length = wcslen(names[i]) + 1;
        memcpy(strings, names[i], length * sizeof(WCHAR));
        entries[i].pszName = strings;
        entries[i].pszComment = nullptr;
        strings += length;
    }
    *count = sizeof(names) / sizeof(names[0]);
    *list = entries;
    return ERROR_SUCCESS;
}

SECURITY_STATUS WINAPI NCryptFreeBuffer(PVOID buffer)
{
    if (buffer)
        HeapFree(GetProcessHeap(), 0, buffer);
    return ERROR_SUCCESS;
}

SECURITY_STATUS WINAPI NCryptFreeObject(NCRYPT_HANDLE handle)
{
    OBJECT *object = find_object(handle, 0);
    if (!object)
        return NTE_INVALID_HANDLE;
    if (object->magic == KEY_MAGIC) {
        close_key((KEY *)object);
    } else if (object->magic == SECRET_MAGIC) {
        SECRET *secret = (SECRET *)object;
        if (secret->secret && g_bcrypt.destroy_secret)
            g_bcrypt.destroy_secret(secret->secret);
        free_object(object);
    } else {
        free_object(object);
    }
    return ERROR_SUCCESS;
}

SECURITY_STATUS WINAPI NCryptIsAlgSupported(NCRYPT_PROV_HANDLE handle, LPCWSTR algorithm, DWORD flags)
{
    if (!find_object(handle, PROVIDER_MAGIC))
        return NTE_INVALID_HANDLE;
    if (!algorithm)
        return NTE_INVALID_PARAMETER;
    BCRYPT_HANDLE alg;
    SECURITY_STATUS status = open_algorithm(algorithm, &alg);
    if (!status)
        g_bcrypt.close(alg, 0);
    return status;
}

BOOL WINAPI NCryptIsKeyHandle(NCRYPT_KEY_HANDLE handle)
{
    return find_object(handle, KEY_MAGIC) != nullptr;
}

/* ------------------------------------------------------------------ persistence */

/* a key's file is its name in hex under the user's or the machine's Microsoft\Crypto\Keys. */
NTSYSAPI NTSTATUS NTAPI RtlConvertSidToUnicodeString(PUNICODE_STRING out, PSID sid, BOOLEAN allocate);

/*
 * the roaming application data of the user the thread acts for: an impersonated user's is in its profile, which a
 * service reaches its callers' keys through, as windows finds the folder from the token. the process's otherwise.
 */
static DWORD user_appdata(WCHAR *base, DWORD size)
{
    HANDLE token;
    /* as the service where it may, else as the user it acts for, whose token a service account cannot always open. */
    if (NT_SUCCESS(NtOpenThreadToken(NtCurrentThread(), 0x0008 /* TOKEN_QUERY */, TRUE, &token)) ||
        NT_SUCCESS(NtOpenThreadToken(NtCurrentThread(), 0x0008 /* TOKEN_QUERY */, FALSE, &token))) {
        BYTE buffer[sizeof(PSID) * 2 + 68];
        ULONG got = 0;
        WCHAR text[96], key[200];
        UNICODE_STRING sid = {0, sizeof(text), text};
        BOOL found = FALSE;
        if (NT_SUCCESS(NtQueryInformationToken(token, 1 /* TokenUser */, buffer, sizeof(buffer), &got)) &&
            NT_SUCCESS(RtlConvertSidToUnicodeString(&sid, *(PSID *)buffer, FALSE))) {
            text[sid.Length / sizeof(WCHAR)] = 0;
            _snwprintf(key, RTL_NUMBER_OF(key), L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList\\%ls", text);
            key[RTL_NUMBER_OF(key) - 1] = 0;
            DWORD bytes = size * sizeof(WCHAR);
            found = !RegGetValueW(HKEY_LOCAL_MACHINE, key, L"ProfileImagePath", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
                                  nullptr, base, &bytes);
        }
        NtClose(token);
        if (found && wcslen(base) + 17 < size) {
            wcscat(base, L"\\AppData\\Roaming");
            return (DWORD)wcslen(base);
        }
    }
    return GetEnvironmentVariableW(L"APPDATA", base, size);
}

static BOOL key_path(LPCWSTR name, DWORD flags, WCHAR *path)
{
    WCHAR base[MAX_PATH];
    DWORD got = (flags & NCRYPT_MACHINE_KEY_FLAG) ? GetEnvironmentVariableW(L"ProgramData", base, MAX_PATH)
                                                  : user_appdata(base, MAX_PATH);
    if (!got || got >= MAX_PATH)
        return FALSE;
    SIZE_T length = wcslen(base);
    static const WCHAR tail[] = L"\\Microsoft\\Crypto\\Keys\\";
    SIZE_T count = wcslen(name);
    /* a long name is kept under a hash of it, the file holds the name, as windows names its files. */
    BOOL hashed = count > 24;
    if (length + wcslen(tail) + (hashed ? 33 : count * 4) + 1 >= MAX_PATH)
        return FALSE;
    memcpy(path, base, length * sizeof(WCHAR));
    memcpy(path + length, tail, sizeof(tail));
    length += wcslen(tail);
    if (hashed) {
        ULONG64 a = 0xcbf29ce484222325ull, b = 0x84222325cbf29ce4ull;
        for (LPCWSTR p = name; *p; p++) {
            a = (a ^ (*p & 0xff)) * 0x100000001b3ull;
            a = (a ^ (*p >> 8)) * 0x100000001b3ull;
            b = (b ^ (*p >> 8)) * 0x100000001b3ull;
            b = (b ^ (*p & 0xff)) * 0x100000001b3ull;
        }
        path[length++] = 'x';
        for (int shift = 60; shift >= 0; shift -= 4)
            path[length++] = L"0123456789abcdef"[(a >> shift) & 15];
        for (int shift = 60; shift >= 0; shift -= 4)
            path[length++] = L"0123456789abcdef"[(b >> shift) & 15];
    } else {
        for (LPCWSTR p = name; *p; p++)
            for (int shift = 12; shift >= 0; shift -= 4)
                path[length++] = L"0123456789abcdef"[(*p >> shift) & 15];
    }
    path[length] = 0;
    return TRUE;
}

static void make_directories(WCHAR *path)
{
    for (WCHAR *p = path + 3; *p; p++)
        if (*p == '\\') {
            *p = 0;
            CreateDirectoryW(path, nullptr);
            *p = '\\';
        }
}

typedef struct {
    DWORD magic, length, export_policy, key_usage, blob_size;
    WCHAR algorithm[32];
} KEY_FILE;

static SECURITY_STATUS save_key(KEY *key)
{
    WCHAR path[MAX_PATH];
    if (!key_path(key->name, key->flags, path))
        return NTE_INVALID_PARAMETER;
    int index = find_algorithm(key->algorithm);
    ULONG size = 0;
    LONG status = g_bcrypt.export_key ? g_bcrypt.export_key(key->key, nullptr, g_algorithms[index].blob, nullptr, 0,
                                                            &size, 0)
                                      : (LONG)0xC00000BB;
    if (status)
        return normalize(status);
    BYTE *blob = HeapAlloc(GetProcessHeap(), 0, size);
    if (!blob)
        return NTE_NO_MEMORY;
    status = g_bcrypt.export_key(key->key, nullptr, g_algorithms[index].blob, blob, size, &size, 0);
    SECURITY_STATUS result = normalize(status);
    if (!result) {
        make_directories(path);
        HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        KEY_FILE header = {FILE_MAGIC, key->length, key->export_policy, key->key_usage, size};
        memcpy(header.algorithm, key->algorithm, sizeof(header.algorithm));
        DWORD written;
        if (file == INVALID_HANDLE_VALUE || !WriteFile(file, &header, sizeof(header), &written, nullptr) ||
            !WriteFile(file, blob, size, &written, nullptr) ||
            !WriteFile(file, key->name, (DWORD)((wcslen(key->name) + 1) * sizeof(WCHAR)), &written, nullptr))
            result = NTE_PERM;
        if (file != INVALID_HANDLE_VALUE)
            CloseHandle(file);
    }
    SecureZeroMemory(blob, size);
    HeapFree(GetProcessHeap(), 0, blob);
    return result;
}

static SECURITY_STATUS load_key(KEY *key)
{
    WCHAR path[MAX_PATH];
    if (!key_path(key->name, key->flags, path))
        return NTE_BAD_KEYSET;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return NTE_BAD_KEYSET;
    KEY_FILE header;
    DWORD read;
    SECURITY_STATUS result = NTE_BAD_KEYSET;
    BYTE *blob = nullptr;
    if (ReadFile(file, &header, sizeof(header), &read, nullptr) && read == sizeof(header) &&
        header.magic == FILE_MAGIC && header.blob_size < 0x100000 && find_algorithm(header.algorithm) >= 0) {
        blob = HeapAlloc(GetProcessHeap(), 0, header.blob_size ? header.blob_size : 1);
        if (blob && ReadFile(file, blob, header.blob_size, &read, nullptr) && read == header.blob_size)
            result = ERROR_SUCCESS;
    }
    CloseHandle(file);
    if (!result) {
        memcpy(key->algorithm, header.algorithm, sizeof(key->algorithm));
        key->algorithm[31] = 0;
        key->length = header.length;
        key->export_policy = header.export_policy;
        key->key_usage = header.key_usage;
        result = open_algorithm(key->algorithm, &key->alg);
        if (!result) {
            LONG status = g_bcrypt.import_pair ? g_bcrypt.import_pair(key->alg, nullptr,
                                                                      g_algorithms[find_algorithm(key->algorithm)].blob,
                                                                      &key->key, blob, header.blob_size, 0)
                                               : (LONG)0xC00000BB;
            result = normalize(status);
        }
        key->finalized = !result;
    }
    if (blob) {
        SecureZeroMemory(blob, header.blob_size);
        HeapFree(GetProcessHeap(), 0, blob);
    }
    return result;
}

/* ------------------------------------------------------------------ keys */


/* ------------------------------------------------------------------ the legacy store */

/*
 * a key can also live in a key container of the legacy provider of the same name. the key storage provider writes it
 * there when a key is finalized with NCRYPT_WRITE_KEY_TO_LEGACY_STORE_FLAG and reads it from there when a key is opened
 * with a key spec, through the legacy functions of advapi32, which keep the container.
 */
#define NCRYPT_WRITE_KEY_TO_LEGACY_STORE_FLAG 0x00000200
#define NCRYPT_NO_KEY_VALIDATION 0x00000008
#define NCRYPT_DO_NOT_FINALIZE_FLAG_ 0x00000400
#define NTE_BAD_DATA_ ((SECURITY_STATUS)0x80090005L)
#define LEGACY_AT_KEYEXCHANGE 1
#define LEGACY_AT_SIGNATURE 2
#define LEGACY_CALG_RSA_KEYX 0xA400
#define LEGACY_CALG_RSA_SIGN 0x2400
#define LEGACY_PRIVATEKEYBLOB 7
#define LEGACY_PROV_RSA_FULL 1
#define LEGACY_NEWKEYSET 0x8
#define LEGACY_DELETEKEYSET 0x10
#define LEGACY_MACHINE_KEYSET 0x20
#define LEGACY_SILENT 0x40
#define LEGACY_EXPORTABLE 0x1
#define RSA_FULLPRIVATE_MAGIC 0x33415352
#define RSA_LEGACY_MAGIC 0x32415352

static struct {
    BOOL (WINAPI *acquire)(ULONG_PTR *, LPCWSTR, LPCWSTR, DWORD, DWORD);
    BOOL (WINAPI *release)(ULONG_PTR, DWORD);
    BOOL (WINAPI *import_key)(ULONG_PTR, const BYTE *, DWORD, ULONG_PTR, DWORD, ULONG_PTR *);
    BOOL (WINAPI *get_user_key)(ULONG_PTR, DWORD, ULONG_PTR *);
    BOOL (WINAPI *export_key)(ULONG_PTR, ULONG_PTR, DWORD, DWORD, BYTE *, DWORD *);
    BOOL (WINAPI *destroy_key)(ULONG_PTR);
} g_legacy;

static INIT_ONCE g_legacy_once = INIT_ONCE_STATIC_INIT;

static BOOL WINAPI load_legacy(INIT_ONCE *once, void *param, void **context)
{
    HMODULE module = LoadLibraryW(L"advapi32.dll");
    if (!module)
        return TRUE;
    g_legacy.acquire = (void *)GetProcAddress(module, "CryptAcquireContextW");
    g_legacy.release = (void *)GetProcAddress(module, "CryptReleaseContext");
    g_legacy.import_key = (void *)GetProcAddress(module, "CryptImportKey");
    g_legacy.get_user_key = (void *)GetProcAddress(module, "CryptGetUserKey");
    g_legacy.export_key = (void *)GetProcAddress(module, "CryptExportKey");
    g_legacy.destroy_key = (void *)GetProcAddress(module, "CryptDestroyKey");
    return TRUE;
}

static BOOL legacy_available(void)
{
    InitOnceExecuteOnce(&g_legacy_once, load_legacy, nullptr, nullptr);
    return g_legacy.acquire && g_legacy.release && g_legacy.import_key && g_legacy.get_user_key && g_legacy.export_key &&
           g_legacy.destroy_key;
}

static void copy_reversed(BYTE *to, const BYTE *from, ULONG n)
{
    for (ULONG i = 0; i < n; i++)
        to[i] = from[n - 1 - i];
}

/* the private blob of the legacy provider from the big endian one of cng, or NULL when the key is not an rsa pair. */
static BYTE *legacy_from_cng(const BYTE *blob, ULONG size, DWORD spec, ULONG *out_size)
{
    if (size < 24)
        return nullptr;
    const ULONG *h = (const ULONG *)blob;
    ULONG bits = h[1], exp = h[2], mod = h[3], p = h[4], q = h[5];
    if (h[0] != RSA_FULLPRIVATE_MAGIC || exp == 0 || exp > 4 || p != q || mod != (bits + 7) / 8 || p * 2 != mod ||
        size != 24 + exp + mod * 2 + p * 5)
        return nullptr;
    ULONG total = 8 + 12 + mod * 2 + p * 5;
    BYTE *out = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, total);
    if (!out)
        return nullptr;
    out[0] = LEGACY_PRIVATEKEYBLOB;
    out[1] = 2;
    *(ULONG *)(out + 4) = spec == LEGACY_AT_SIGNATURE ? LEGACY_CALG_RSA_SIGN : LEGACY_CALG_RSA_KEYX;
    *(ULONG *)(out + 8) = RSA_LEGACY_MAGIC;
    *(ULONG *)(out + 12) = bits;
    ULONG exponent = 0;
    for (ULONG i = 0; i < exp; i++)
        exponent = exponent << 8 | blob[24 + i];
    *(ULONG *)(out + 16) = exponent;
    const BYTE *from = blob + 24 + exp;
    BYTE *to = out + 20;
    /* modulus, the two primes, the two exponents and the coefficient, then the private exponent. */
    ULONG lengths[] = {mod, p, p, p, p, p, mod};
    for (int i = 0; i < 7; i++) {
        copy_reversed(to, from, lengths[i]);
        to += lengths[i];
        from += lengths[i];
    }
    *out_size = total;
    return out;
}

/* the cng blob of an rsa pair from the legacy private blob. */
static BYTE *cng_from_legacy(const BYTE *blob, ULONG size, ULONG *out_size)
{
    if (size < 20 || blob[0] != LEGACY_PRIVATEKEYBLOB || *(const ULONG *)(blob + 8) != RSA_LEGACY_MAGIC)
        return nullptr;
    ULONG bits = *(const ULONG *)(blob + 12), exponent = *(const ULONG *)(blob + 16);
    ULONG mod = (bits + 7) / 8, p = mod / 2;
    if (bits < 256 || bits > 16384 || bits % 16 || size != 20 + mod * 2 + p * 5)
        return nullptr;
    ULONG total = 24 + 4 + mod * 2 + p * 5;
    BYTE *out = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, total);
    if (!out)
        return nullptr;
    ULONG *h = (ULONG *)out;
    h[0] = RSA_FULLPRIVATE_MAGIC;
    h[1] = bits;
    h[2] = 4;
    h[3] = mod;
    h[4] = p;
    h[5] = p;
    BYTE *to = out + 24;
    to[0] = (BYTE)(exponent >> 24);
    to[1] = (BYTE)(exponent >> 16);
    to[2] = (BYTE)(exponent >> 8);
    to[3] = (BYTE)exponent;
    to += 4;
    const BYTE *from = blob + 20;
    ULONG lengths[] = {mod, p, p, p, p, p, mod};
    for (int i = 0; i < 7; i++) {
        copy_reversed(to, from, lengths[i]);
        to += lengths[i];
        from += lengths[i];
    }
    *out_size = total;
    return out;
}

static DWORD legacy_context_flags(const KEY *key, DWORD extra)
{
    return extra | LEGACY_SILENT | ((key->flags & NCRYPT_MACHINE_KEY_FLAG) ? LEGACY_MACHINE_KEYSET : 0);
}

/* writes the finalized key into the container of its name in the legacy store. */
static SECURITY_STATUS legacy_write(KEY *key)
{
    if (!legacy_available())
        return NTE_NOT_SUPPORTED;
    int index = find_algorithm(key->algorithm);
    if (index != 0 || !key->name)
        return NTE_NOT_SUPPORTED;
    ULONG size = 0;
    if (!g_bcrypt.export_key || normalize(g_bcrypt.export_key(key->key, nullptr, g_algorithms[index].blob, nullptr, 0, &size, 0)))
        return NTE_NOT_SUPPORTED;
    BYTE *cng = HeapAlloc(GetProcessHeap(), 0, size);
    if (!cng)
        return NTE_NO_MEMORY;
    SECURITY_STATUS result = normalize(g_bcrypt.export_key(key->key, nullptr, g_algorithms[index].blob, cng, size, &size, 0));
    ULONG legacy_size = 0;
    BYTE *legacy = result ? nullptr : legacy_from_cng(cng, size, key->legacy_spec, &legacy_size);
    SecureZeroMemory(cng, size);
    HeapFree(GetProcessHeap(), 0, cng);
    if (!result && !legacy)
        result = NTE_BAD_DATA_;
    if (!result) {
        ULONG_PTR provider = 0;
        if (!g_legacy.acquire(&provider, key->name, nullptr, LEGACY_PROV_RSA_FULL, legacy_context_flags(key, LEGACY_NEWKEYSET)) &&
            ((SECURITY_STATUS)GetLastError() != NTE_EXISTS ||
             !g_legacy.acquire(&provider, key->name, nullptr, LEGACY_PROV_RSA_FULL, legacy_context_flags(key, 0))))
            result = GetLastError();
        if (!result) {
            ULONG_PTR imported = 0;
            if (g_legacy.import_key(provider, legacy, legacy_size, 0, LEGACY_EXPORTABLE, &imported))
                g_legacy.destroy_key(imported);
            else
                result = GetLastError();
            g_legacy.release(provider, 0);
        }
    }
    if (legacy) {
        SecureZeroMemory(legacy, legacy_size);
        HeapFree(GetProcessHeap(), 0, legacy);
    }
    return result;
}

/* the key of a container of the legacy store, as a key of this provider. */
/*
 * the private blob a container of the legacy store keeps for a key spec, read from its file as the key storage provider
 * of windows reads the files of capi: a key that cannot be exported is still the container's. the layout and the name
 * of the file are those of the containers of advapi32.
 */
#define CONTAINER_MAGIC_ 0x4B53494D
typedef struct {
    DWORD magic;
    WCHAR name[MAX_PATH];
    DWORD exportable[2];
    DWORD size[2];
} CONTAINER_HEADER;

static BYTE *container_blob(const KEY *key, DWORD spec, DWORD *size)
{
    WCHAR base[MAX_PATH], path[MAX_PATH];
    BOOL machine = (key->flags & NCRYPT_MACHINE_KEY_FLAG) != 0;
    DWORD n = machine ? GetEnvironmentVariableW(L"ProgramData", base, MAX_PATH) : user_appdata(base, MAX_PATH);
    if (!n || n >= MAX_PATH - 80)
        return nullptr;
    ULONG64 a = 0xcbf29ce484222325ull, b = 0x84222325cbf29ce4ull;
    for (LPCWSTR c = key->name; *c; c++) {
        WCHAR w = towupper(*c);
        a = (a ^ (w & 0xff)) * 0x100000001b3ull;
        a = (a ^ (w >> 8)) * 0x100000001b3ull;
        b = (b ^ (w >> 8)) * 0x100000001b3ull;
        b = (b ^ (w & 0xff)) * 0x100000001b3ull;
    }
    _snwprintf(path, MAX_PATH, L"%ls\\Microsoft\\Crypto\\RSA\\%ls\\%016llx%016llx", base, machine ? L"MachineKeys" : L"Keys",
               a, b);
    path[MAX_PATH - 1] = 0;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return nullptr;
    CONTAINER_HEADER h;
    DWORD read;
    BYTE *blob = nullptr;
    int slot = spec == LEGACY_AT_KEYEXCHANGE ? 0 : 1;
    if (ReadFile(file, &h, sizeof(h), &read, nullptr) && read == sizeof(h) && h.magic == CONTAINER_MAGIC_ &&
        h.size[0] < 0x100000 && h.size[1] < 0x100000 && h.size[slot]) {
        /* the blobs end the file; the header before them holds the pointers of advapi32, of the size of its process. */
        LARGE_INTEGER file_size = {};
        GetFileSizeEx(file, &file_size);
        LARGE_INTEGER at = {.QuadPart = file_size.QuadPart - h.size[0] - h.size[1] + (slot ? h.size[0] : 0)};
        blob = HeapAlloc(GetProcessHeap(), 0, h.size[slot]);
        if (blob && (!SetFilePointerEx(file, at, nullptr, FILE_BEGIN) ||
                     !ReadFile(file, blob, h.size[slot], &read, nullptr) || read != h.size[slot])) {
            HeapFree(GetProcessHeap(), 0, blob);
            blob = nullptr;
        }
        if (blob)
            *size = h.size[slot];
    }
    CloseHandle(file);
    return blob;
}

static SECURITY_STATUS legacy_read(KEY *key, DWORD spec)
{
    if (!legacy_available())
        return NTE_NOT_SUPPORTED;
    ULONG_PTR provider = 0, user = 0;
    if (!g_legacy.acquire(&provider, key->name, nullptr, LEGACY_PROV_RSA_FULL, legacy_context_flags(key, 0)))
        return NTE_BAD_KEYSET;
    SECURITY_STATUS result = NTE_BAD_KEYSET;
    BYTE *legacy = nullptr, *cng = nullptr;
    DWORD size = 0;
    if (g_legacy.get_user_key(provider, spec, &user)) {
        result = NTE_NOT_SUPPORTED;
        BOOL exported = g_legacy.export_key(user, 0, LEGACY_PRIVATEKEYBLOB, 0, nullptr, &size) && size &&
                        (legacy = HeapAlloc(GetProcessHeap(), 0, size)) &&
                        g_legacy.export_key(user, 0, LEGACY_PRIVATEKEYBLOB, 0, legacy, &size);
        if (!exported) {
            if (legacy)
                HeapFree(GetProcessHeap(), 0, legacy);
            size = 0;
            legacy = container_blob(key, spec, &size);
        }
        if (legacy) {
            ULONG cng_size = 0;
            cng = cng_from_legacy(legacy, size, &cng_size);
            result = NTE_BAD_DATA_;
            if (cng) {
                memcpy(key->algorithm, L"RSA", sizeof(L"RSA"));
                key->length = ((const ULONG *)cng)[1];
                key->export_policy = exported ? NCRYPT_ALLOW_EXPORT_FLAG | NCRYPT_ALLOW_PLAINTEXT_EXPORT_FLAG : 0;
                key->key_usage = 3;
                result = open_algorithm(key->algorithm, &key->alg);
                if (!result)
                    result = normalize(g_bcrypt.import_pair
                                           ? g_bcrypt.import_pair(key->alg, nullptr, g_algorithms[0].blob, &key->key, cng,
                                                                  cng_size, 0)
                                           : (LONG)0xC00000BB);
                if (!result) {
                    key->finalized = TRUE;
                    key->legacy_spec = spec;
                    key->from_legacy = TRUE;
                }
                SecureZeroMemory(cng, cng_size);
                HeapFree(GetProcessHeap(), 0, cng);
            }
        }
        if (legacy) {
            SecureZeroMemory(legacy, size);
            HeapFree(GetProcessHeap(), 0, legacy);
        }
        g_legacy.destroy_key(user);
    }
    g_legacy.release(provider, 0);
    return result;
}

static void legacy_delete(KEY *key)
{
    ULONG_PTR provider = 0;
    if (legacy_available() && key->name)
        g_legacy.acquire(&provider, key->name, nullptr, LEGACY_PROV_RSA_FULL, legacy_context_flags(key, LEGACY_DELETEKEYSET));
}

SECURITY_STATUS WINAPI NCryptCreatePersistedKey(NCRYPT_PROV_HANDLE handle, NCRYPT_KEY_HANDLE *out, LPCWSTR algorithm,
                                                LPCWSTR name, DWORD spec, DWORD flags)
{
    if (!out)
        return NTE_INVALID_PARAMETER;
    PROVIDER *provider = find_object(handle, PROVIDER_MAGIC);
    if (!provider)
        return NTE_INVALID_HANDLE;
    *out = 0;
    if (!algorithm)
        return NTE_INVALID_PARAMETER;
    if (flags & ~(NCRYPT_MACHINE_KEY_FLAG | NCRYPT_OVERWRITE_KEY_FLAG | NCRYPT_SILENT_FLAG))
        return NTE_BAD_FLAGS;
    if (spec != 0 && spec != LEGACY_AT_KEYEXCHANGE && spec != LEGACY_AT_SIGNATURE)
        return NTE_INVALID_PARAMETER;
    int index = find_algorithm(algorithm);
    if (index < 0)
        return NTE_NOT_SUPPORTED;
    WCHAR path[MAX_PATH];
    if (name && key_path(name, flags, path) && !(flags & NCRYPT_OVERWRITE_KEY_FLAG) &&
        GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
        return NTE_EXISTS;
    KEY *key = new_object(KEY_MAGIC, sizeof(*key));
    if (!key)
        return NTE_NO_MEMORY;
    key->provider = provider;
    memcpy(key->algorithm, g_algorithms[index].name, (wcslen(g_algorithms[index].name) + 1) * sizeof(WCHAR));
    key->length = g_algorithms[index].length;
    key->flags = flags;
    key->legacy_spec = spec;
    SECURITY_STATUS status = open_algorithm(algorithm, &key->alg);
    if (!status && name) {
        SIZE_T size = (wcslen(name) + 1) * sizeof(WCHAR);
        key->name = HeapAlloc(GetProcessHeap(), 0, size);
        if (key->name)
            memcpy(key->name, name, size);
        else
            status = NTE_NO_MEMORY;
    }
    if (status) {
        close_key(key);
        return status;
    }
    *out = (NCRYPT_KEY_HANDLE)key;
    return ERROR_SUCCESS;
}

SECURITY_STATUS WINAPI NCryptFinalizeKey(NCRYPT_KEY_HANDLE handle, DWORD flags)
{
    KEY *key = find_object(handle, KEY_MAGIC);
    if (!key)
        return NTE_INVALID_HANDLE;
    if (key->finalized)
        return NTE_BAD_KEY_STATE;
    if (flags & ~(NCRYPT_WRITE_KEY_TO_LEGACY_STORE_FLAG | NCRYPT_SILENT_FLAG | NCRYPT_NO_KEY_VALIDATION))
        return NTE_BAD_FLAGS;
    if ((flags & NCRYPT_WRITE_KEY_TO_LEGACY_STORE_FLAG) && (!key->legacy_spec || !key->name))
        return NTE_INVALID_PARAMETER;
    if (!g_bcrypt.generate || !g_bcrypt.finalize)
        return NTE_NOT_SUPPORTED;
    SECURITY_STATUS status = ERROR_SUCCESS;
    /* an imported key waited for its properties: it keeps the pair it came with. */
    if (!key->imported) {
        status = normalize(g_bcrypt.generate(key->alg, &key->key, key->length, 0));
        if (!status)
            status = normalize(g_bcrypt.finalize(key->key, 0));
    }
    if (!status && key->name)
        status = save_key(key);
    if (!status && (flags & NCRYPT_WRITE_KEY_TO_LEGACY_STORE_FLAG))
        status = legacy_write(key);
    if (status && key->key && !key->imported) {
        g_bcrypt.destroy(key->key);
        key->key = nullptr;
    }
    key->finalized = !status;
    return status;
}

SECURITY_STATUS WINAPI NCryptOpenKey(NCRYPT_PROV_HANDLE handle, NCRYPT_KEY_HANDLE *out, LPCWSTR name, DWORD spec,
                                     DWORD flags)
{
    if (!out)
        return NTE_INVALID_PARAMETER;
    PROVIDER *provider = find_object(handle, PROVIDER_MAGIC);
    if (!provider)
        return NTE_INVALID_HANDLE;
    *out = 0;
    if (!name)
        return NTE_INVALID_PARAMETER;
    KEY *key = new_object(KEY_MAGIC, sizeof(*key));
    if (!key)
        return NTE_NO_MEMORY;
    key->provider = provider;
    key->flags = flags & NCRYPT_MACHINE_KEY_FLAG;
    SIZE_T size = (wcslen(name) + 1) * sizeof(WCHAR);
    key->name = HeapAlloc(GetProcessHeap(), 0, size);
    SECURITY_STATUS status = key->name ? ERROR_SUCCESS : NTE_NO_MEMORY;
    if (!status && spec != 0 && spec != LEGACY_AT_KEYEXCHANGE && spec != LEGACY_AT_SIGNATURE)
        status = NTE_INVALID_PARAMETER;
    if (!status) {
        memcpy(key->name, name, size);
        status = spec ? legacy_read(key, spec) : load_key(key);
    }
    if (status) {
        close_key(key);
        return status;
    }
    *out = (NCRYPT_KEY_HANDLE)key;
    return ERROR_SUCCESS;
}

SECURITY_STATUS WINAPI NCryptDeleteKey(NCRYPT_KEY_HANDLE handle, DWORD flags)
{
    KEY *key = find_object(handle, KEY_MAGIC);
    if (!key)
        return NTE_INVALID_HANDLE;
    WCHAR path[MAX_PATH];
    if (key->from_legacy)
        legacy_delete(key);
    else if (key->name && key_path(key->name, key->flags, path) && !DeleteFileW(path) &&
             GetLastError() != ERROR_FILE_NOT_FOUND)
        return NTE_PERM;
    close_key(key);
    return ERROR_SUCCESS;
}

typedef struct {
    LPWSTR name;
    DWORD flags;
} KEY_ENUM;

/* the keys of this store, one per call, NTE_NO_MORE_ITEMS after the last. */
SECURITY_STATUS WINAPI NCryptEnumKeys(NCRYPT_PROV_HANDLE handle, LPCWSTR scope, NCryptKeyName **out, PVOID *state,
                                      DWORD flags)
{
    if (!find_object(handle, PROVIDER_MAGIC))
        return NTE_INVALID_HANDLE;
    if (!out || !state)
        return NTE_INVALID_PARAMETER;
    *out = nullptr;
    WCHAR pattern[MAX_PATH];
    if (!key_path(L"", flags, pattern) || wcslen(pattern) + 2 >= MAX_PATH)
        return NTE_NO_MORE_ITEMS;
    pattern[wcslen(pattern) + 1] = 0;
    pattern[wcslen(pattern)] = '*';
    /* the state is a block of the heap that the caller frees, holding how far the listing went. */
    ULONG_PTR index = *state ? *(ULONG_PTR *)*state : 0, n = 0;
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE)
        return NTE_NO_MORE_ITEMS;
    SECURITY_STATUS status = NTE_NO_MORE_ITEMS;
    do {
        if (n++ != index)
            continue;
        LPCWSTR hex = data.cFileName + 5;
        SIZE_T length = wcslen(hex) / 4;
        WCHAR stored[MAX_PATH];
        BOOL named_by_file = data.cFileName[0] == 'x';
        if (named_by_file) {
            /* the name of a hashed file is at its end. */
            WCHAR full[MAX_PATH];
            wcscpy(full, pattern);
            full[wcslen(full) - 1] = 0;
            if (wcslen(full) + wcslen(data.cFileName) + 1 >= MAX_PATH)
                continue;
            wcscat(full, data.cFileName);
            HANDLE named = CreateFileW(full, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            KEY_FILE head;
            DWORD got = 0;
            stored[0] = 0;
            if (named != INVALID_HANDLE_VALUE) {
                if (ReadFile(named, &head, sizeof(head), &got, nullptr) && got == sizeof(head) &&
                    SetFilePointer(named, head.blob_size, nullptr, FILE_CURRENT) != INVALID_SET_FILE_POINTER)
                    ReadFile(named, stored, sizeof(stored) - sizeof(WCHAR), &got, nullptr);
                CloseHandle(named);
            }
            stored[MAX_PATH - 1] = 0;
            if (!stored[0])
                continue;
            length = wcslen(stored);
        }
        NCryptKeyName *entry = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*entry) + (length + 1) * 2 +
                                                                                 32 * sizeof(WCHAR));
        if (!entry) {
            status = NTE_NO_MEMORY;
            break;
        }
        entry->pszName = (LPWSTR)(entry + 1);
        for (SIZE_T i = 0; i < length; i++) {
            WCHAR c = 0;
            if (named_by_file) {
                c = stored[i];
            } else {
                for (int j = 0; j < 4; j++) {
                    WCHAR d = hex[i * 4 + j];
                    c = (WCHAR)(c << 4 | (d >= 'a' ? d - 'a' + 10 : d - '0'));
                }
            }
            entry->pszName[i] = c;
        }
        entry->pszAlgid = entry->pszName + length + 1;
        KEY probe = {.name = entry->pszName, .flags = flags & NCRYPT_MACHINE_KEY_FLAG};
        WCHAR path[MAX_PATH];
        HANDLE file = key_path(probe.name, probe.flags, path)
                          ? CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr)
                          : INVALID_HANDLE_VALUE;
        KEY_FILE header = {0};
        DWORD read;
        if (file != INVALID_HANDLE_VALUE) {
            ReadFile(file, &header, sizeof(header), &read, nullptr);
            CloseHandle(file);
        }
        memcpy(entry->pszAlgid, header.algorithm, sizeof(header.algorithm));
        entry->pszAlgid[31] = 0;
        entry->dwFlags = flags & NCRYPT_MACHINE_KEY_FLAG;
        if (!*state && !(*state = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ULONG_PTR)))) {
            HeapFree(GetProcessHeap(), 0, entry);
            status = NTE_NO_MEMORY;
            break;
        }
        *(ULONG_PTR *)*state = index + 1;
        *out = entry;
        status = ERROR_SUCCESS;
        break;
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return status;
}

/* ------------------------------------------------------------------ properties */

static SECURITY_STATUS give(const void *value, DWORD size, PBYTE output, DWORD capacity, DWORD *result)
{
    if (!result)
        return NTE_INVALID_PARAMETER;
    *result = size;
    if (!output)
        return ERROR_SUCCESS;
    if (capacity < size)
        return NTE_BUFFER_TOO_SMALL;
    memcpy(output, value, size);
    return ERROR_SUCCESS;
}

static SECURITY_STATUS give_string(LPCWSTR value, PBYTE output, DWORD capacity, DWORD *result)
{
    return give(value, (DWORD)(wcslen(value) + 1) * sizeof(WCHAR), output, capacity, result);
}

SECURITY_STATUS WINAPI NCryptGetProperty(NCRYPT_HANDLE handle, LPCWSTR property, PBYTE output, DWORD size,
                                         DWORD *result, DWORD flags)
{
    OBJECT *object = find_object(handle, 0);
    if (!object || object->magic == SECRET_MAGIC)
        return NTE_INVALID_HANDLE;
    if (!property || !result)
        return NTE_INVALID_PARAMETER;
    DWORD value;
    if (object->magic == PROVIDER_MAGIC) {
        if (!wcscmp(property, NCRYPT_NAME_PROPERTY))
            return give_string(MS_KEY_STORAGE_PROVIDER, output, size, result);
        if (!wcscmp(property, NCRYPT_IMPL_TYPE_PROPERTY)) {
            value = NCRYPT_IMPL_SOFTWARE_FLAG;
            return give(&value, sizeof(value), output, size, result);
        }
        return NTE_NOT_SUPPORTED;
    }
    KEY *key = (KEY *)object;
    int index = find_algorithm(key->algorithm);
    if (!wcscmp(property, NCRYPT_ALGORITHM_PROPERTY))
        return give_string(key->algorithm, output, size, result);
    if (!wcscmp(property, NCRYPT_ALGORITHM_GROUP_PROPERTY))
        return give_string(index >= 0 ? g_algorithms[index].group : L"", output, size, result);
    if (!wcscmp(property, NCRYPT_NAME_PROPERTY) || !wcscmp(property, NCRYPT_UNIQUE_NAME_PROPERTY)) {
        if (!key->name)
            return NTE_NOT_FOUND;
        return give_string(key->name, output, size, result);
    }
    if (!wcscmp(property, NCRYPT_LENGTH_PROPERTY))
        return give(&key->length, sizeof(DWORD), output, size, result);
    if (!wcscmp(property, NCRYPT_EXPORT_POLICY_PROPERTY))
        return give(&key->export_policy, sizeof(DWORD), output, size, result);
    if (!wcscmp(property, NCRYPT_KEY_USAGE_PROPERTY))
        return give(&key->key_usage, sizeof(DWORD), output, size, result);
    if (!wcscmp(property, NCRYPT_KEY_TYPE_PROPERTY)) {
        value = key->flags & NCRYPT_MACHINE_KEY_FLAG ? 1 : 0;
        return give(&value, sizeof(value), output, size, result);
    }
    if (!wcscmp(property, NCRYPT_IMPL_TYPE_PROPERTY)) {
        value = NCRYPT_IMPL_SOFTWARE_FLAG;
        return give(&value, sizeof(value), output, size, result);
    }
    if (!wcscmp(property, NCRYPT_PROVIDER_HANDLE_PROPERTY)) {
        NCRYPT_PROV_HANDLE provider = (NCRYPT_PROV_HANDLE)key->provider;
        return give(&provider, sizeof(provider), output, size, result);
    }
    return NTE_NOT_SUPPORTED;
}

/* only creation properties can be set, and only until the key is finalized. */
SECURITY_STATUS WINAPI NCryptSetProperty(NCRYPT_HANDLE handle, LPCWSTR property, PBYTE input, DWORD size,
                                         DWORD flags)
{
    OBJECT *object = find_object(handle, 0);
    if (!object || object->magic == SECRET_MAGIC)
        return NTE_INVALID_HANDLE;
    if (!property || (!input && size))
        return NTE_INVALID_PARAMETER;
    if (object->magic == PROVIDER_MAGIC)
        return NTE_NOT_SUPPORTED;
    KEY *key = (KEY *)object;
    DWORD *target = nullptr;
    if (!wcscmp(property, NCRYPT_LENGTH_PROPERTY))
        target = &key->length;
    else if (!wcscmp(property, NCRYPT_EXPORT_POLICY_PROPERTY))
        target = &key->export_policy;
    else if (!wcscmp(property, NCRYPT_KEY_USAGE_PROPERTY))
        target = &key->key_usage;
    else
        return NTE_NOT_SUPPORTED;
    if (size != sizeof(DWORD))
        return NTE_INVALID_PARAMETER;
    if (key->finalized)
        return NTE_PERM;
    memcpy(target, input, sizeof(DWORD));
    return ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ operations */

static KEY *usable_key(NCRYPT_KEY_HANDLE handle, SECURITY_STATUS *status)
{
    KEY *key = find_object(handle, KEY_MAGIC);
    *status = !key ? NTE_INVALID_HANDLE : !key->finalized ? NTE_BAD_KEY_STATE : ERROR_SUCCESS;
    return *status ? nullptr : key;
}

static BOOL private_blob(LPCWSTR type)
{
    return !wcscmp(type, L"RSAPRIVATEBLOB") || !wcscmp(type, L"RSAFULLPRIVATEBLOB") ||
           !wcscmp(type, L"ECCPRIVATEBLOB") || !wcscmp(type, L"DSAPRIVATEBLOB") || !wcscmp(type, L"DHPRIVATEBLOB") ||
           !wcscmp(type, L"PKCS8_PRIVATEKEY");
}

SECURITY_STATUS WINAPI NCryptExportKey(NCRYPT_KEY_HANDLE handle, NCRYPT_KEY_HANDLE wrap, LPCWSTR type, void *params,
                                       PBYTE output, DWORD size, DWORD *result, DWORD flags)
{
    SECURITY_STATUS status;
    KEY *key = usable_key(handle, &status);
    if (!key)
        return status;
    if (!type || !result)
        return NTE_INVALID_PARAMETER;
    if (wrap)
        return NTE_NOT_SUPPORTED;
    if (private_blob(type) && !(key->export_policy & NCRYPT_ALLOW_PLAINTEXT_EXPORT_FLAG))
        return NTE_NOT_SUPPORTED;
    if (!g_bcrypt.export_key)
        return NTE_NOT_SUPPORTED;
    ULONG done = 0;
    status = normalize(g_bcrypt.export_key(key->key, nullptr, type, output, size, &done, 0));
    *result = done;
    return status;
}

/* ------------------------------------------------------------------ pkcs #8 */

/* a der element: its tag, its contents and the position after it, or false at the end or on bad data. */
typedef struct {
    const BYTE *p;
    SIZE_T left;
} DER;

static BOOL der_next(DER *d, BYTE *tag, DER *contents)
{
    if (d->left < 2)
        return FALSE;
    *tag = d->p[0];
    SIZE_T length = d->p[1], at = 2;
    if (length & 0x80) {
        SIZE_T bytes = length & 0x7f;
        if (!bytes || bytes > 4 || d->left < 2 + bytes)
            return FALSE;
        length = 0;
        for (SIZE_T i = 0; i < bytes; i++)
            length = length << 8 | d->p[2 + i];
        at += bytes;
    }
    if (d->left - at < length)
        return FALSE;
    contents->p = d->p + at;
    contents->left = length;
    d->p += at + length;
    d->left -= at + length;
    return TRUE;
}

static BOOL der_expect(DER *d, BYTE want, DER *contents)
{
    BYTE tag;
    return der_next(d, &tag, contents) && tag == want;
}

/* an unsigned integer without its leading zero. */
static DER der_unsigned(DER v)
{
    while (v.left > 1 && !v.p[0]) {
        v.p++;
        v.left--;
    }
    return v;
}

static BOOL put_padded(BYTE **at, BYTE *end, DER v, SIZE_T size)
{
    v = der_unsigned(v);
    if (v.left > size || *at + size > end)
        return FALSE;
    memset(*at, 0, size - v.left);
    memcpy(*at + size - v.left, v.p, v.left);
    *at += size;
    return TRUE;
}

/* an RSAPrivateKey as a BCRYPT_RSAFULLPRIVATE_BLOB. */
static BYTE *rsa_blob(DER key, ULONG *size)
{
    DER seq, f[9];
    if (!der_expect(&key, 0x30, &seq))
        return nullptr;
    for (int i = 0; i < 9; i++)
        if (!der_expect(&seq, 0x02, &f[i]))
            return nullptr;
    DER n = der_unsigned(f[1]), e = der_unsigned(f[2]), p = der_unsigned(f[4]), q = der_unsigned(f[5]);
    ULONG cn = (ULONG)n.left, ce = (ULONG)e.left, cp = (ULONG)p.left, cq = (ULONG)q.left;
    *size = 24 + ce + cn + cp + cq + cp + cq + cp + cn;
    BYTE *blob = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, *size);
    if (!blob)
        return nullptr;
    ULONG header[6] = {0x33415352, cn * 8, ce, cn, cp, cq};
    memcpy(blob, header, sizeof(header));
    BYTE *at = blob + 24, *end = blob + *size;
    BOOL ok = put_padded(&at, end, f[2], ce) && put_padded(&at, end, f[1], cn) && put_padded(&at, end, f[4], cp) &&
              put_padded(&at, end, f[5], cq) && put_padded(&at, end, f[6], cp) && put_padded(&at, end, f[7], cq) &&
              put_padded(&at, end, f[8], cp) && put_padded(&at, end, f[3], cn);
    if (!ok) {
        SecureZeroMemory(blob, *size);
        HeapFree(GetProcessHeap(), 0, blob);
        return nullptr;
    }
    return blob;
}

/* an ECPrivateKey with its public point as a BCRYPT_ECCPRIVATE_BLOB of the ecdh magic of its curve. */
static BYTE *ec_blob(DER key, DER curve, LPCWSTR *algorithm, ULONG *size)
{
    static const BYTE p256[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07};
    static const BYTE p384[] = {0x2B, 0x81, 0x04, 0x00, 0x22};
    static const BYTE p521[] = {0x2B, 0x81, 0x04, 0x00, 0x23};
    ULONG bytes = 0, magic = 0;
    if (curve.left == sizeof(p256) && !memcmp(curve.p, p256, sizeof(p256))) {
        bytes = 32, magic = 0x324B4345, *algorithm = L"ECDH_P256";
    } else if (curve.left == sizeof(p384) && !memcmp(curve.p, p384, sizeof(p384))) {
        bytes = 48, magic = 0x344B4345, *algorithm = L"ECDH_P384";
    } else if (curve.left == sizeof(p521) && !memcmp(curve.p, p521, sizeof(p521))) {
        bytes = 66, magic = 0x364B4345, *algorithm = L"ECDH_P521";
    } else
        return nullptr;
    DER seq, version, d, point = {};
    if (!der_expect(&key, 0x30, &seq) || !der_expect(&seq, 0x02, &version) || !der_expect(&seq, 0x04, &d))
        return nullptr;
    while (seq.left) {
        BYTE tag;
        DER item;
        if (!der_next(&seq, &tag, &item))
            return nullptr;
        if (tag == 0xA1 && !der_expect(&item, 0x03, &point))
            return nullptr;
    }
    /* the public point is needed, as a bit string of no unused bits holding an uncompressed point. */
    if (point.left != 2 + 2 * bytes || point.p[0] || point.p[1] != 4 || d.left > bytes)
        return nullptr;
    *size = 8 + 3 * bytes;
    BYTE *blob = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, *size);
    if (!blob)
        return nullptr;
    ULONG header[2] = {magic, bytes};
    memcpy(blob, header, sizeof(header));
    memcpy(blob + 8, point.p + 2, 2 * bytes);
    memcpy(blob + 8 + 2 * bytes + bytes - d.left, d.p, d.left);
    return blob;
}

/* a PrivateKeyInfo of rsa or of an elliptic curve, as the bcrypt blob of the algorithm it names. */
static BYTE *pkcs8_blob(const BYTE *input, DWORD input_size, LPCWSTR *algorithm, LPCWSTR *type, ULONG *size)
{
    static const BYTE rsa_oid[] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01};
    static const BYTE ec_oid[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01};
    DER all = {input, input_size}, info, version, alg, oid, key, params = {};
    if (!der_expect(&all, 0x30, &info) || !der_expect(&info, 0x02, &version) || !der_expect(&info, 0x30, &alg) ||
        !der_expect(&alg, 0x06, &oid) || !der_expect(&info, 0x04, &key))
        return nullptr;
    BYTE tag;
    if (alg.left && der_next(&alg, &tag, &params) && tag != 0x06)
        params.left = 0;
    if (oid.left == sizeof(rsa_oid) && !memcmp(oid.p, rsa_oid, sizeof(rsa_oid))) {
        *algorithm = L"RSA";
        *type = L"RSAFULLPRIVATEBLOB";
        return rsa_blob(key, size);
    }
    if (oid.left == sizeof(ec_oid) && !memcmp(oid.p, ec_oid, sizeof(ec_oid))) {
        *type = L"ECCPRIVATEBLOB";
        return ec_blob(key, params, algorithm, size);
    }
    return nullptr;
}

typedef struct {
    ULONG cbBuffer;
    ULONG BufferType;
    PVOID pvBuffer;
} NCRYPT_BUFFER_;

typedef struct {
    ULONG ulVersion;
    ULONG cBuffers;
    NCRYPT_BUFFER_ *pBuffers;
} NCRYPT_BUFFER_DESC_;

/* the name an import asks for, NCRYPTBUFFER_PKCS_KEY_NAME in its parameter list. */
static LPCWSTR import_name(const void *params)
{
    const NCRYPT_BUFFER_DESC_ *desc = params;
    if (!desc || !desc->pBuffers)
        return nullptr;
    for (ULONG i = 0; i < desc->cBuffers; i++)
        if (desc->pBuffers[i].BufferType == 45 && desc->pBuffers[i].pvBuffer && desc->pBuffers[i].cbBuffer >= 2)
            return desc->pBuffers[i].pvBuffer;
    return nullptr;
}

/*
 * an EncryptedPrivateKeyInfo, decrypted with the password crypt32 hands in NCRYPTBUFFER_PKCS_SECRET: pbes2 with pbkdf2
 * and aes or triple des, or the pkcs #12 scheme of sha1 and triple des. the plain PrivateKeyInfo, or nothing.
 */
static BOOL oid_is(DER oid, const BYTE *want, SIZE_T size)
{
    return oid.left == size && !memcmp(oid.p, want, size);
}

static const MimeHashAlg *hmac_of(DER oid)
{
    static const BYTE sha1[] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x02, 0x07};
    static const BYTE sha256[] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x02, 0x09};
    static const BYTE sha384[] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x02, 0x0A};
    static const BYTE sha512[] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x02, 0x0B};
    if (oid_is(oid, sha1, sizeof(sha1)))
        return &mime_sha1;
    if (oid_is(oid, sha256, sizeof(sha256)))
        return &mime_sha256;
    if (oid_is(oid, sha384, sizeof(sha384)))
        return &mime_sha384;
    if (oid_is(oid, sha512, sizeof(sha512)))
        return &mime_sha512;
    return nullptr;
}

static ULONG64 der_number(DER v)
{
    ULONG64 n = 0;
    v = der_unsigned(v);
    for (SIZE_T i = 0; i < v.left && i < 8; i++)
        n = n << 8 | v.p[i];
    return n;
}

/* rfc 7292 b.2: the key or iv of an id, from the password as a big endian bmp string with its end. */
static void pkcs12_kdf(const WCHAR *password, DER salt, ULONG64 iterations, BYTE id, BYTE *out, SIZE_T size)
{
    enum { V = 64, U = 20 };
    SIZE_T plen = (wcslen(password) + 1) * 2;
    BYTE pass[512];
    if (plen > sizeof(pass))
        plen = sizeof(pass);
    for (SIZE_T i = 0; i < plen / 2; i++) {
        pass[2 * i] = (BYTE)(password[i] >> 8);
        pass[2 * i + 1] = (BYTE)password[i];
    }
    SIZE_T s_len = salt.left ? V * ((salt.left + V - 1) / V) : 0, p_len = V * ((plen + V - 1) / V);
    BYTE I[V * 10];
    if (s_len + p_len > sizeof(I))
        return;
    for (SIZE_T i = 0; i < s_len; i++)
        I[i] = salt.p[i % salt.left];
    for (SIZE_T i = 0; i < p_len; i++)
        I[s_len + i] = pass[i % plen];
    BYTE D[V], A[U], B[V];
    memset(D, id, V);
    for (SIZE_T done = 0; done < size;) {
        BYTE buffer[V + sizeof(I)];
        memcpy(buffer, D, V);
        memcpy(buffer + V, I, s_len + p_len);
        mime_hash(&mime_sha1, buffer, V + s_len + p_len, A);
        for (ULONG64 c = 1; c < iterations; c++)
            mime_hash(&mime_sha1, A, U, A);
        SIZE_T n = size - done < U ? size - done : U;
        memcpy(out + done, A, n);
        done += n;
        if (done >= size)
            break;
        for (SIZE_T i = 0; i < V; i++)
            B[i] = A[i % U];
        for (SIZE_T j = 0; j < (s_len + p_len) / V; j++) {
            unsigned carry = 1;
            for (SIZE_T k = V; k-- > 0;) {
                carry += I[j * V + k] + B[k];
                I[j * V + k] = (BYTE)carry;
                carry >>= 8;
            }
        }
    }
    SecureZeroMemory(pass, sizeof(pass));
    SecureZeroMemory(I, sizeof(I));
}

static BYTE *pkcs8_decrypt(const BYTE *input, DWORD input_size, const WCHAR *password, ULONG *plain_size)
{
    static const BYTE pbes2[] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x05, 0x0D};
    static const BYTE pbkdf2[] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x05, 0x0C};
    static const BYTE des3[] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x03, 0x07};
    static const BYTE aes[] = {0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x01};
    static const BYTE pkcs12_des3[] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x0C, 0x01, 0x03};
    DER all = {input, input_size}, info, alg, oid, params, data;
    if (!password || !der_expect(&all, 0x30, &info) || !der_expect(&info, 0x30, &alg) ||
        !der_expect(&alg, 0x06, &oid) || !der_expect(&alg, 0x30, &params) || !der_expect(&info, 0x04, &data) ||
        !data.left)
        return nullptr;
    BYTE key[32], iv[16];
    SIZE_T key_size = 0, block = 8;
    BOOL use_aes = FALSE;
    if (oid_is(oid, pbes2, sizeof(pbes2))) {
        DER kdf, kdf_oid, kdf_params, scheme, scheme_oid, salt, count, next, prf_oid;
        if (!der_expect(&params, 0x30, &kdf) || !der_expect(&kdf, 0x06, &kdf_oid) ||
            !oid_is(kdf_oid, pbkdf2, sizeof(pbkdf2)) || !der_expect(&kdf, 0x30, &kdf_params) ||
            !der_expect(&kdf_params, 0x04, &salt) || !der_expect(&kdf_params, 0x02, &count) ||
            !der_expect(&params, 0x30, &scheme) || !der_expect(&scheme, 0x06, &scheme_oid))
            return nullptr;
        const MimeHashAlg *prf = &mime_sha1;
        BYTE tag;
        while (kdf_params.left && der_next(&kdf_params, &tag, &next)) {
            if (tag == 0x30 && der_expect(&next, 0x06, &prf_oid) && !(prf = hmac_of(prf_oid)))
                return nullptr;
        }
        DER iv_der;
        if (!der_expect(&scheme, 0x04, &iv_der))
            return nullptr;
        if (oid_is(scheme_oid, des3, sizeof(des3)) && iv_der.left == 8)
            key_size = 24;
        else if (scheme_oid.left == sizeof(aes) + 1 && !memcmp(scheme_oid.p, aes, sizeof(aes)) && iv_der.left == 16) {
            BYTE last = scheme_oid.p[sizeof(aes)];
            key_size = last == 0x02 ? 16 : last == 0x16 ? 24 : last == 0x2A ? 32 : 0;
            use_aes = TRUE;
            block = 16;
        }
        if (!key_size)
            return nullptr;
        memcpy(iv, iv_der.p, iv_der.left);
        char utf8[512];
        int n = WideCharToMultiByte(CP_UTF8, 0, password, -1, utf8, sizeof(utf8), nullptr, nullptr);
        if (n <= 0)
            return nullptr;
        mime_pbkdf2(prf, utf8, (size_t)(n - 1), salt.p, salt.left, der_number(count), key, key_size);
        SecureZeroMemory(utf8, sizeof(utf8));
    } else if (oid_is(oid, pkcs12_des3, sizeof(pkcs12_des3))) {
        DER salt, count;
        if (!der_expect(&params, 0x04, &salt) || !der_expect(&params, 0x02, &count))
            return nullptr;
        key_size = 24;
        pkcs12_kdf(password, salt, der_number(count), 1, key, 24);
        pkcs12_kdf(password, salt, der_number(count), 2, iv, 8);
    } else
        return nullptr;
    if (data.left % block)
        return nullptr;
    BYTE *plain = HeapAlloc(GetProcessHeap(), 0, data.left);
    if (!plain)
        return nullptr;
    if (use_aes) {
        MimeAes context;
        mime_aes_init(&context, key, key_size);
        mime_cbc_decrypt(&mime_cipher_aes, &context, iv, data.p, plain, data.left);
        SecureZeroMemory(&context, sizeof(context));
    } else {
        MimeDes3 context;
        mime_des3_init(&context, key, key_size);
        mime_cbc_decrypt(&mime_cipher_des3, &context, iv, data.p, plain, data.left);
        SecureZeroMemory(&context, sizeof(context));
    }
    SecureZeroMemory(key, sizeof(key));
    BYTE pad = plain[data.left - 1];
    BOOL ok = pad && pad <= block;
    for (SIZE_T i = 0; ok && i < pad; i++)
        ok = plain[data.left - 1 - i] == pad;
    if (!ok) {
        SecureZeroMemory(plain, data.left);
        HeapFree(GetProcessHeap(), 0, plain);
        return nullptr;
    }
    *plain_size = (ULONG)(data.left - pad);
    return plain;
}

/* the password an import of an encrypted key gives, NCRYPTBUFFER_PKCS_SECRET. */
static const WCHAR *import_secret(const void *params)
{
    const NCRYPT_BUFFER_DESC_ *desc = params;
    if (!desc || !desc->pBuffers)
        return nullptr;
    for (ULONG i = 0; i < desc->cBuffers; i++)
        if (desc->pBuffers[i].BufferType == 46 && desc->pBuffers[i].pvBuffer)
            return desc->pBuffers[i].pvBuffer;
    return nullptr;
}

SECURITY_STATUS WINAPI NCryptImportKey(NCRYPT_PROV_HANDLE handle, NCRYPT_KEY_HANDLE unwrap, LPCWSTR type,
                                       void *params, NCRYPT_KEY_HANDLE *out, PBYTE input, DWORD size, DWORD flags)
{
    PROVIDER *provider = find_object(handle, PROVIDER_MAGIC);
    if (!provider)
        return NTE_INVALID_HANDLE;
    if (!out || !type || !input)
        return NTE_INVALID_PARAMETER;
    *out = 0;
    if (unwrap)
        return NTE_NOT_SUPPORTED;
    LPCWSTR algorithm = nullptr;
    BYTE *converted = nullptr;
    ULONG converted_size = 0;
    if (!wcscmp(type, L"PKCS8_PRIVATEKEY")) {
        LPCWSTR blob_type = nullptr;
        ULONG plain_size = 0;
        BYTE *plain = pkcs8_decrypt(input, size, import_secret(params), &plain_size);
        converted = pkcs8_blob(plain ? plain : input, plain ? plain_size : size, &algorithm, &blob_type, &converted_size);
        if (plain) {
            SecureZeroMemory(plain, plain_size);
            HeapFree(GetProcessHeap(), 0, plain);
        }
        if (!converted)
            return NTE_BAD_DATA_;
        input = converted;
        size = converted_size;
        type = blob_type;
    }
    /* the legacy blobs of the legacy store and the generic ones are told apart by their magic. */
    BOOL legacy_rsa = !wcscmp(type, L"CAPIPUBLICBLOB") || !wcscmp(type, L"CAPIPRIVATEBLOB");
    BOOL generic = !wcscmp(type, L"PUBLICBLOB") || !wcscmp(type, L"PRIVATEBLOB");
    if (!wcsncmp(type, L"RSA", 3) || legacy_rsa)
        algorithm = L"RSA";
    else if (generic && size >= 4) {
        DWORD magic = *(const DWORD *)input;
        if (magic == 0x31415352 || magic == 0x32415352 || magic == 0x33415352)
            algorithm = L"RSA";
    } else if (!wcsncmp(type, L"ECC", 3) && size >= 8) {
        DWORD magic = *(const DWORD *)input;
        BOOL dh = magic == 0x314B4345 || magic == 0x324B4345 || magic == 0x334B4345 || magic == 0x344B4345 ||
                  magic == 0x354B4345 || magic == 0x364B4345;
        DWORD bytes = ((const DWORD *)input)[1];
        if (bytes == 32)
            algorithm = dh ? L"ECDH_P256" : L"ECDSA_P256";
        else if (bytes == 48)
            algorithm = dh ? L"ECDH_P384" : L"ECDSA_P384";
        else
            algorithm = dh ? L"ECDH_P521" : L"ECDSA_P521";
    }
    if (!algorithm) {
        HeapFree(GetProcessHeap(), 0, converted);
        return NTE_NOT_SUPPORTED;
    }
    KEY *key = new_object(KEY_MAGIC, sizeof(*key));
    if (!key) {
        HeapFree(GetProcessHeap(), 0, converted);
        return NTE_NO_MEMORY;
    }
    key->provider = provider;
    key->flags = flags & NCRYPT_MACHINE_KEY_FLAG;
    memcpy(key->algorithm, algorithm, (wcslen(algorithm) + 1) * sizeof(WCHAR));
    SECURITY_STATUS status = open_algorithm(algorithm, &key->alg);
    if (!status)
        status = g_bcrypt.import_pair ? normalize(g_bcrypt.import_pair(key->alg, nullptr, type, &key->key, input,
                                                                        size, 0))
                                      : NTE_NOT_SUPPORTED;
    if (converted) {
        SecureZeroMemory(converted, converted_size);
        HeapFree(GetProcessHeap(), 0, converted);
    }
    /* a named import persists, now or when the caller finalizes it after setting its properties. */
    LPCWSTR name = import_name(params);
    if (!status && name) {
        SIZE_T bytes = (wcslen(name) + 1) * sizeof(WCHAR);
        key->name = HeapAlloc(GetProcessHeap(), 0, bytes);
        if (key->name)
            memcpy(key->name, name, bytes);
        else
            status = NTE_NO_MEMORY;
        WCHAR path[MAX_PATH];
        if (!status && !(flags & NCRYPT_OVERWRITE_KEY_FLAG) && key_path(name, key->flags, path) &&
            GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
            status = NTE_EXISTS;
    }
    if (!status) {
        int index = find_algorithm(algorithm);
        key->length = index >= 0 ? g_algorithms[index].length : 0;
        if (!wcscmp(algorithm, L"RSA") && size >= 8)
            key->length = ((const ULONG *)input)[1];
        key->key_usage = 3;
    }
    if (status) {
        close_key(key);
        return status;
    }
    key->imported = TRUE;
    key->finalized = !(flags & NCRYPT_DO_NOT_FINALIZE_FLAG_);
    if (key->finalized && key->name)
        status = save_key(key);
    if (status) {
        close_key(key);
        return status;
    }
    *out = (NCRYPT_KEY_HANDLE)key;
    return ERROR_SUCCESS;
}

/*
 * a key of an elliptic curve from a pfx or pkcs #8 is an ecdh key of the software provider, which signs as well: its
 * pair signs as the ecdsa key of the same curve.
 */
static SECURITY_STATUS ecdh_sign(KEY *key, PBYTE hash, DWORD hash_size, PBYTE signature, DWORD size, ULONG *done)
{
    ULONG length = 0;
    LONG status = g_bcrypt.export_key(key->key, nullptr, L"ECCPRIVATEBLOB", nullptr, 0, &length, 0);
    if (status)
        return normalize(status);
    BYTE *blob = HeapAlloc(GetProcessHeap(), 0, length);
    if (!blob)
        return NTE_NO_MEMORY;
    status = g_bcrypt.export_key(key->key, nullptr, L"ECCPRIVATEBLOB", blob, length, &length, 0);
    BCRYPT_HANDLE alg = nullptr, pair = nullptr;
    if (!status) {
        /* ECKn becomes ECSn, the same curve. */
        blob[2] = 'S';
        status = open_algorithm(key->algorithm[6] == '2' ? L"ECDSA_P256" : key->algorithm[6] == '3' ? L"ECDSA_P384"
                                                                                                      : L"ECDSA_P521",
                                &alg);
    }
    if (!status)
        status = g_bcrypt.import_pair(alg, nullptr, L"ECCPRIVATEBLOB", &pair, blob, length, 0);
    if (!status)
        status = g_bcrypt.sign(pair, nullptr, hash, hash_size, signature, size, done, 0);
    if (pair)
        g_bcrypt.destroy(pair);
    if (alg && g_bcrypt.close)
        g_bcrypt.close(alg, 0);
    SecureZeroMemory(blob, length);
    HeapFree(GetProcessHeap(), 0, blob);
    return normalize(status);
}

SECURITY_STATUS WINAPI NCryptSignHash(NCRYPT_KEY_HANDLE handle, void *padding, PBYTE hash, DWORD hash_size,
                                      PBYTE signature, DWORD size, DWORD *result, DWORD flags)
{
    SECURITY_STATUS status;
    KEY *key = usable_key(handle, &status);
    if (!key)
        return status;
    if (!hash || !result)
        return NTE_INVALID_PARAMETER;
    if (!g_bcrypt.sign)
        return NTE_NOT_SUPPORTED;
    ULONG done = 0;
    if (!wcsncmp(key->algorithm, L"ECDH_", 5))
        status = flags ? NTE_INVALID_PARAMETER : ecdh_sign(key, hash, hash_size, signature, size, &done);
    else
        status = normalize(g_bcrypt.sign(key->key, padding, hash, hash_size, signature, size, &done, flags));
    *result = done;
    return status;
}

SECURITY_STATUS WINAPI NCryptVerifySignature(NCRYPT_KEY_HANDLE handle, void *padding, PBYTE hash, DWORD hash_size,
                                             PBYTE signature, DWORD size, DWORD flags)
{
    SECURITY_STATUS status;
    KEY *key = usable_key(handle, &status);
    if (!key)
        return status;
    if (!hash || !signature)
        return NTE_INVALID_PARAMETER;
    if (!g_bcrypt.verify)
        return NTE_NOT_SUPPORTED;
    return normalize(g_bcrypt.verify(key->key, padding, hash, hash_size, signature, size, flags));
}

SECURITY_STATUS WINAPI NCryptEncrypt(NCRYPT_KEY_HANDLE handle, PBYTE input, DWORD size, void *padding, PBYTE output,
                                     DWORD capacity, DWORD *result, DWORD flags)
{
    SECURITY_STATUS status;
    KEY *key = usable_key(handle, &status);
    if (!key)
        return status;
    if (!result)
        return NTE_INVALID_PARAMETER;
    if (!g_bcrypt.encrypt)
        return NTE_NOT_SUPPORTED;
    ULONG done = 0;
    status = normalize(g_bcrypt.encrypt(key->key, input, size, padding, nullptr, 0, output, capacity, &done, flags));
    *result = done;
    return status;
}

SECURITY_STATUS WINAPI NCryptDecrypt(NCRYPT_KEY_HANDLE handle, PBYTE input, DWORD size, void *padding, PBYTE output,
                                     DWORD capacity, DWORD *result, DWORD flags)
{
    SECURITY_STATUS status;
    KEY *key = usable_key(handle, &status);
    if (!key)
        return status;
    if (!result)
        return NTE_INVALID_PARAMETER;
    if (!g_bcrypt.decrypt)
        return NTE_NOT_SUPPORTED;
    ULONG done = 0;
    status = normalize(g_bcrypt.decrypt(key->key, input, size, padding, nullptr, 0, output, capacity, &done, flags));
    *result = done;
    return status;
}

SECURITY_STATUS WINAPI NCryptSecretAgreement(NCRYPT_KEY_HANDLE private_handle, NCRYPT_KEY_HANDLE public_handle,
                                             NCRYPT_SECRET_HANDLE *out, DWORD flags)
{
    SECURITY_STATUS status;
    if (!out)
        return NTE_INVALID_PARAMETER;
    *out = 0;
    KEY *mine = usable_key(private_handle, &status);
    if (!mine)
        return status;
    KEY *theirs = usable_key(public_handle, &status);
    if (!theirs)
        return status;
    if (!g_bcrypt.agree)
        return NTE_NOT_SUPPORTED;
    SECRET *secret = new_object(SECRET_MAGIC, sizeof(*secret));
    if (!secret)
        return NTE_NO_MEMORY;
    status = normalize(g_bcrypt.agree(mine->key, theirs->key, &secret->secret, 0));
    if (status) {
        free_object(&secret->header);
        return status;
    }
    *out = (NCRYPT_SECRET_HANDLE)secret;
    return ERROR_SUCCESS;
}

SECURITY_STATUS WINAPI NCryptDeriveKey(NCRYPT_SECRET_HANDLE handle, LPCWSTR kdf, void *params, PBYTE output,
                                       DWORD size, DWORD *result, ULONG flags)
{
    SECRET *secret = find_object(handle, SECRET_MAGIC);
    if (!secret)
        return NTE_INVALID_HANDLE;
    if (!kdf || !result)
        return NTE_INVALID_PARAMETER;
    if (!g_bcrypt.derive)
        return NTE_NOT_SUPPORTED;
    ULONG done = 0;
    SECURITY_STATUS status = normalize(g_bcrypt.derive(secret->secret, kdf, params, output, size, &done, flags));
    *result = done;
    return status;
}

/* attestation needs a tpm. */
SECURITY_STATUS WINAPI NCryptCreateClaim(NCRYPT_KEY_HANDLE subject, NCRYPT_KEY_HANDLE authority, DWORD type,
                                         void *params, PBYTE output, DWORD size, DWORD *result, DWORD flags)
{
    if (!find_object(subject, KEY_MAGIC))
        return NTE_INVALID_HANDLE;
    return NTE_NOT_SUPPORTED;
}

SECURITY_STATUS WINAPI NCryptVerifyClaim(NCRYPT_KEY_HANDLE subject, NCRYPT_KEY_HANDLE authority, DWORD type,
                                         void *params, PBYTE claim, DWORD size, void *output, DWORD flags)
{
    if (!find_object(subject, KEY_MAGIC))
        return NTE_INVALID_HANDLE;
    return NTE_NOT_SUPPORTED;
}

SECURITY_STATUS WINAPI NCryptNotifyChangeKey(NCRYPT_PROV_HANDLE handle, HANDLE *event, DWORD flags)
{
    if (!find_object(handle, PROVIDER_MAGIC))
        return NTE_INVALID_HANDLE;
    return NTE_NOT_SUPPORTED;
}

/* ------------------------------------------------------------------ algorithms of the provider */

typedef struct {
    LPWSTR name;
    DWORD class_, operations, flags;
} ALGORITHM_NAME;

/* the algorithms the provider makes keys of, with the classes and operations Windows lists them with. */
static const struct {
    LPCWSTR name;
    DWORD class_, operations;
} g_listed[] = {
    {L"RSA", 3, 0x14}, {L"DH", 4, 0x8}, {L"DSA", 5, 0x10}, {L"ECDH_P256", 4, 0x8}, {L"ECDH_P384", 4, 0x8},
    {L"ECDH_P521", 4, 0x8}, {L"ECDSA_P256", 5, 0x10}, {L"ECDSA_P384", 5, 0x10}, {L"ECDSA_P521", 5, 0x10},
};

SECURITY_STATUS WINAPI NCryptEnumAlgorithms(NCRYPT_PROV_HANDLE handle, DWORD operations, DWORD *count,
                                            ALGORITHM_NAME **list, DWORD flags)
{
    if (!find_object(handle, PROVIDER_MAGIC))
        return NTE_INVALID_HANDLE;
    if (!count || !list)
        return NTE_INVALID_PARAMETER;
    *count = 0;
    *list = nullptr;
    if (flags & ~NCRYPT_SILENT_FLAG)
        return NTE_BAD_FLAGS;
    UINT selected = 0;
    SIZE_T size = 0;
    for (UINT i = 0; i < sizeof(g_listed) / sizeof(g_listed[0]); i++)
        if (!operations || (operations & g_listed[i].operations)) {
            size += sizeof(ALGORITHM_NAME) + (wcslen(g_listed[i].name) + 1) * sizeof(WCHAR);
            selected++;
        }
    if (!selected)
        return operations ? NTE_NOT_SUPPORTED : ERROR_SUCCESS;
    ALGORITHM_NAME *names = HeapAlloc(GetProcessHeap(), 0, size);
    if (!names)
        return NTE_NO_MEMORY;
    WCHAR *strings = (WCHAR *)(names + selected);
    UINT next = 0;
    for (UINT i = 0; i < sizeof(g_listed) / sizeof(g_listed[0]); i++) {
        if (operations && !(operations & g_listed[i].operations))
            continue;
        SIZE_T length = wcslen(g_listed[i].name) + 1;
        memcpy(strings, g_listed[i].name, length * sizeof(WCHAR));
        names[next].name = strings;
        names[next].class_ = g_listed[i].class_;
        /* a key agreement can sign as well, as the provider lists it. */
        names[next].operations = g_listed[i].operations == 0x8 ? 0x18 : g_listed[i].operations;
        names[next].flags = 0;
        strings += length;
        next++;
    }
    *count = selected;
    *list = names;
    return ERROR_SUCCESS;
}

/* the key encapsulation and the derivation from a key are not in the provider, which has no such interface. */
SECURITY_STATUS WINAPI NCryptEncapsulate(NCRYPT_KEY_HANDLE key, ULONG_PTR a2, ULONG_PTR a3, ULONG_PTR a4, ULONG_PTR a5,
                                         ULONG_PTR a6, ULONG_PTR a7, ULONG_PTR flags)
{
    if (!a4 || !a7)
        return NTE_INVALID_PARAMETER;
    if (!find_object(key, KEY_MAGIC))
        return NTE_INVALID_HANDLE;
    return NTE_NOT_SUPPORTED;
}

SECURITY_STATUS WINAPI NCryptDecapsulate(NCRYPT_KEY_HANDLE key, ULONG_PTR a2, ULONG_PTR a3, ULONG_PTR a4, ULONG_PTR a5,
                                         ULONG_PTR a6, ULONG_PTR flags)
{
    if (!a6)
        return NTE_INVALID_PARAMETER;
    if (!find_object(key, KEY_MAGIC))
        return NTE_INVALID_HANDLE;
    return NTE_NOT_SUPPORTED;
}

SECURITY_STATUS WINAPI NCryptKeyDerivation(NCRYPT_KEY_HANDLE key, ULONG_PTR parameters, PBYTE output, DWORD size,
                                           DWORD *result, DWORD flags)
{
    if (!result || !output || !size)
        return NTE_INVALID_PARAMETER;
    if (!find_object(key, KEY_MAGIC))
        return NTE_INVALID_HANDLE;
    return NTE_NOT_SUPPORTED;
}

SECURITY_STATUS WINAPI NCryptSetAuditingInterface(void *audit)
{
    return ERROR_SUCCESS;
}
