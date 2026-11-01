/*
 * ncrypt: the key storage router and the Microsoft software key storage
 * provider, whose keys are bcrypt keys. what bcrypt cannot do fails with
 * NTE_NOT_SUPPORTED. named keys persist as their private blob under the
 * user's or the machine's Crypto\Keys directory. the platform crypto
 * provider finds no tpm.
 */
#define NCRYPTAPI
#include <mime/win32/ncrypt.h>

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

SECURITY_STATUS WINAPI NCryptOpenStorageProvider(NCRYPT_PROV_HANDLE *handle, LPCWSTR name, DWORD flags)
{
    if (!handle)
        return NTE_INVALID_PARAMETER;
    *handle = 0;
    if (name && *name && _wcsicmp(name, MS_KEY_STORAGE_PROVIDER)) {
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
static BOOL key_path(LPCWSTR name, DWORD flags, WCHAR *path)
{
    WCHAR base[MAX_PATH];
    LPCWSTR root = (flags & NCRYPT_MACHINE_KEY_FLAG) ? L"ProgramData" : L"APPDATA";
    DWORD got = GetEnvironmentVariableW(root, base, MAX_PATH);
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
        if (g_legacy.export_key(user, 0, LEGACY_PRIVATEKEYBLOB, 0, nullptr, &size) && size &&
            (legacy = HeapAlloc(GetProcessHeap(), 0, size)) &&
            g_legacy.export_key(user, 0, LEGACY_PRIVATEKEYBLOB, 0, legacy, &size)) {
            ULONG cng_size = 0;
            cng = cng_from_legacy(legacy, size, &cng_size);
            result = NTE_BAD_DATA_;
            if (cng) {
                memcpy(key->algorithm, L"RSA", sizeof(L"RSA"));
                key->length = ((const ULONG *)cng)[1];
                key->export_policy = NCRYPT_ALLOW_EXPORT_FLAG | NCRYPT_ALLOW_PLAINTEXT_EXPORT_FLAG;
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
    SECURITY_STATUS status = normalize(g_bcrypt.generate(key->alg, &key->key, key->length, 0));
    if (!status)
        status = normalize(g_bcrypt.finalize(key->key, 0));
    if (!status && key->name)
        status = save_key(key);
    if (!status && (flags & NCRYPT_WRITE_KEY_TO_LEGACY_STORE_FLAG))
        status = legacy_write(key);
    if (status && key->key) {
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
    if (!wcsncmp(type, L"RSA", 3))
        algorithm = L"RSA";
    else if (!wcsncmp(type, L"ECC", 3) && size >= 8) {
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
    if (!algorithm)
        return NTE_NOT_SUPPORTED;
    KEY *key = new_object(KEY_MAGIC, sizeof(*key));
    if (!key)
        return NTE_NO_MEMORY;
    key->provider = provider;
    memcpy(key->algorithm, algorithm, (wcslen(algorithm) + 1) * sizeof(WCHAR));
    SECURITY_STATUS status = open_algorithm(algorithm, &key->alg);
    if (!status)
        status = g_bcrypt.import_pair ? normalize(g_bcrypt.import_pair(key->alg, nullptr, type, &key->key, input,
                                                                        size, 0))
                                      : NTE_NOT_SUPPORTED;
    if (status) {
        close_key(key);
        return status;
    }
    key->finalized = TRUE;
    *out = (NCRYPT_KEY_HANDLE)key;
    return ERROR_SUCCESS;
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

/* the handles of key protectors, protection descriptors and streams come from calls that are not here. */
SECURITY_STATUS WINAPI NCryptCloseKeyProtector(ULONG_PTR protector)
{
    return NTE_INVALID_HANDLE;
}

SECURITY_STATUS WINAPI NCryptCloseProtectionDescriptor(ULONG_PTR descriptor)
{
    return NTE_INVALID_HANDLE;
}

SECURITY_STATUS WINAPI NCryptDuplicateKeyProtectorHandle(ULONG_PTR protector, ULONG_PTR *copy, ULONG_PTR flags)
{
    return NTE_INVALID_HANDLE;
}

SECURITY_STATUS WINAPI NCryptStreamClose(ULONG_PTR stream)
{
    return NTE_INVALID_HANDLE;
}

SECURITY_STATUS WINAPI NCryptStreamUpdate(ULONG_PTR stream, PBYTE data, SIZE_T size, BOOL final)
{
    return NTE_INVALID_HANDLE;
}
