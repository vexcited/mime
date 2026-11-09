/*
 * the data protection of cng, dpapi-ng: protection descriptors, key protectors, secrets and streams.
 *
 * a descriptor names who can open a secret: rules of a provider and a value joined by AND, those groups joined by OR.
 * the providers a machine without a domain can serve are LOCAL=user, LOCAL=logon, LOCAL=machine and SID=, a sid of
 * the caller or of a group the caller belongs to. their blobs are made here with a secret of the user or of the
 * machine kept in the registry, they are not the cms messages Windows makes and only open in mime. the providers that
 * need certificates, key files or credential vaults fail as unsupported.
 */
#define NCRYPTAPI
#include <mime/win32/ncrypt.h>
#include <mime/win32/bcrypt.h>

#define STATUS_NOT_FOUND_ ((SECURITY_STATUS)0xC0000225L)
#define NCRYPT_NAMED_DESCRIPTOR_FLAG 0x00000001
#define NCRYPT_UNPROTECT_NO_DECRYPT 0x00000001
#define DESCRIPTOR_SIZE 40
#define PROTECTOR_SIZE 72
#define STREAM_SIZE 192
#define MAX_RULES 8
#define MAX_GROUPS 8
#define BLOB_MAGIC 0x474E5044u

typedef struct {
    ULONG cbSize;
    void *(WINAPI *pfnAlloc)(SIZE_T);
    void(WINAPI *pfnFree)(void *);
} ALLOC_PARA;

typedef struct {
    WCHAR *name;
    WCHAR *value;
} RULE;

typedef struct {
    ULONG count;
    RULE rules[MAX_RULES];
} GROUP;

/* the size field comes first, the rest is the parsed descriptor and the text it came from. */
typedef struct {
    ULONG size;
    ULONG flags;
    ULONG group_count;
    GROUP *groups;
    WCHAR *text;
} DESCRIPTOR;

typedef struct {
    ULONG size;
    LONG references;
    WCHAR *name;
    void *context;
} PROTECTOR;

typedef struct {
    ULONGLONG size;
    ULONG kind;
    PVOID callback;
    PVOID context;
    DESCRIPTOR *descriptor;
    BYTE *data;
    SIZE_T used, capacity;
    BOOL done;
} STREAM;

typedef struct {
    PVOID callback;
    PVOID context;
} STREAM_INFO;

static void *WINAPI default_alloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), 0, size);
}

static void WINAPI default_free(void *block)
{
    HeapFree(GetProcessHeap(), 0, block);
}

static const ALLOC_PARA g_default_alloc = {sizeof(ALLOC_PARA), default_alloc, default_free};

/* a handle is only looked at when its memory can be read. */
static BOOL readable(const void *pointer, SIZE_T size)
{
    MEMORY_BASIC_INFORMATION info;
    if (!pointer || !VirtualQuery(pointer, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) || !info.Protect)
        return FALSE;
    return (const BYTE *)pointer + size <= (const BYTE *)info.BaseAddress + info.RegionSize;
}

static SECURITY_STATUS hresult_from_win32(LONG error)
{
    return error > 0 ? (SECURITY_STATUS)((USHORT)error | 0x80070000u) : (SECURITY_STATUS)error;
}

/* ------------------------------------------------------------------ the parsed descriptor */

static BOOL known_provider(const WCHAR *name)
{
    static const WCHAR *const providers[] = {L"SID", L"LOCAL", L"CERTIFICATE", L"KEYFILE", L"WEBCREDENTIALS",
                                             L"VAULTCREDENTIALS", L"LOCKEDCREDENTIALS"};
    for (UINT i = 0; i < RTL_NUMBER_OF(providers); i++)
        if (!lstrcmpiW(name, providers[i]))
            return TRUE;
    return FALSE;
}

static void free_descriptor_body(DESCRIPTOR *descriptor)
{
    HeapFree(GetProcessHeap(), 0, descriptor->groups);
    HeapFree(GetProcessHeap(), 0, descriptor->text);
    descriptor->groups = nullptr;
    descriptor->text = nullptr;
}

/* the rules in a group are checked for the providers that exist, a certificate is named once. */
static SECURITY_STATUS parse_descriptor(const WCHAR *source, DESCRIPTOR *descriptor)
{
    SIZE_T length = lstrlenW(source) + 1;
    descriptor->text = HeapAlloc(GetProcessHeap(), 0, length * sizeof(WCHAR));
    descriptor->groups = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(GROUP) * MAX_GROUPS);
    if (!descriptor->text || !descriptor->groups) {
        free_descriptor_body(descriptor);
        return NTE_NO_MEMORY;
    }
    memcpy(descriptor->text, source, length * sizeof(WCHAR));
    WCHAR *at = descriptor->text;
    ULONG group = 0;
    BOOL certificate = FALSE;
    SECURITY_STATUS status = NTE_INVALID_PARAMETER;
    for (;;) {
        while (*at == ' ' || *at == '\t')
            at++;
        if (!*at)
            break;
        WCHAR *name = at;
        while (*at && *at != '=' && *at != ' ')
            at++;
        if (*at != '=' || at == name)
            goto failed;
        *at++ = 0;
        WCHAR *value = at;
        while (*at && *at != ' ' && *at != '\t')
            at++;
        if (at == value)
            goto failed;
        BOOL end = !*at;
        *at = 0;
        if (!end)
            at++;
        if (!known_provider(name)) {
            status = STATUS_NOT_FOUND_;
            goto failed;
        }
        if (!lstrcmpiW(name, L"CERTIFICATE")) {
            if (certificate) {
                status = NTE_NOT_SUPPORTED;
                goto failed;
            }
            certificate = TRUE;
        }
        GROUP *current = &descriptor->groups[group];
        if (current->count >= MAX_RULES) {
            status = NTE_BAD_DATA;
            goto failed;
        }
        current->rules[current->count].name = name;
        current->rules[current->count].value = value;
        current->count++;
        if (end)
            break;
        while (*at == ' ' || *at == '\t')
            at++;
        if (!*at)
            break;
        WCHAR *word = at;
        while (*at && *at != ' ')
            at++;
        BOOL more = *at != 0;
        *at = 0;
        if (more)
            at++;
        if (!lstrcmpiW(word, L"OR")) {
            if (++group >= MAX_GROUPS) {
                status = NTE_BAD_DATA;
                goto failed;
            }
        } else if (lstrcmpiW(word, L"AND")) {
            status = NTE_BAD_DATA;
            goto failed;
        }
    }
    if (!descriptor->groups[0].count)
        goto failed;
    descriptor->group_count = group + 1;
    return ERROR_SUCCESS;
failed:
    free_descriptor_body(descriptor);
    return status;
}

static DESCRIPTOR *new_descriptor(const WCHAR *source, ULONG flags, SECURITY_STATUS *status)
{
    DESCRIPTOR *descriptor = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*descriptor));
    if (!descriptor) {
        *status = NTE_NO_MEMORY;
        return nullptr;
    }
    descriptor->size = DESCRIPTOR_SIZE;
    descriptor->flags = flags;
    *status = parse_descriptor(source, descriptor);
    if (*status) {
        HeapFree(GetProcessHeap(), 0, descriptor);
        return nullptr;
    }
    return descriptor;
}

static BOOL valid_descriptor(const void *handle)
{
    return readable(handle, sizeof(DESCRIPTOR)) && ((const DESCRIPTOR *)handle)->size == DESCRIPTOR_SIZE;
}

/*
 * the text of the rules of a group joined by AND, the groups by OR. windows joins them without a space for the text it
 * gives back, a blob keeps spaces so the text can be read again.
 */
static WCHAR *descriptor_string(const DESCRIPTOR *descriptor, const ALLOC_PARA *alloc, BOOL spaced)
{
    SIZE_T chars = 1;
    for (ULONG g = 0; g < descriptor->group_count; g++) {
        if (g)
            chars += spaced ? 4 : 2;
        for (ULONG r = 0; r < descriptor->groups[g].count; r++) {
            if (r)
                chars += spaced ? 5 : 3;
            chars += lstrlenW(descriptor->groups[g].rules[r].name) + lstrlenW(descriptor->groups[g].rules[r].value) + 1;
        }
    }
    WCHAR *text = alloc->pfnAlloc(chars * sizeof(WCHAR));
    if (!text)
        return nullptr;
    WCHAR *at = text;
    for (ULONG g = 0; g < descriptor->group_count; g++) {
        if (g) {
            if (spaced)
                *at++ = ' ';
            *at++ = 'O';
            *at++ = 'R';
            if (spaced)
                *at++ = ' ';
        }
        for (ULONG r = 0; r < descriptor->groups[g].count; r++) {
            if (r) {
                if (spaced)
                    *at++ = ' ';
                *at++ = 'A';
                *at++ = 'N';
                *at++ = 'D';
                if (spaced)
                    *at++ = ' ';
            }
            const RULE *rule = &descriptor->groups[g].rules[r];
            for (const WCHAR *c = rule->name; *c;)
                *at++ = *c++;
            *at++ = '=';
            for (const WCHAR *c = rule->value; *c;)
                *at++ = *c++;
        }
    }
    *at = 0;
    return text;
}

/* ------------------------------------------------------------------ protectors */

SECURITY_STATUS WINAPI NCryptOpenKeyProtector(NCRYPT_HANDLE *handle, LPCWSTR name, DWORD flags)
{
    if (!handle || !name || !*name)
        return NTE_INVALID_PARAMETER;
    *handle = 0;
    if (!known_provider(name))
        return STATUS_NOT_FOUND_;
    PROTECTOR *protector = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*protector));
    if (!protector)
        return NTE_NO_MEMORY;
    SIZE_T length = lstrlenW(name) + 1;
    protector->name = HeapAlloc(GetProcessHeap(), 0, length * sizeof(WCHAR));
    if (!protector->name) {
        HeapFree(GetProcessHeap(), 0, protector);
        return NTE_NO_MEMORY;
    }
    memcpy(protector->name, name, length * sizeof(WCHAR));
    protector->size = PROTECTOR_SIZE;
    protector->references = 1;
    protector->context = protector;
    *handle = (NCRYPT_HANDLE)protector;
    return ERROR_SUCCESS;
}

static BOOL valid_protector(const void *handle)
{
    return readable(handle, sizeof(PROTECTOR)) && ((const PROTECTOR *)handle)->size == PROTECTOR_SIZE;
}

SECURITY_STATUS WINAPI NCryptCloseKeyProtector(ULONG_PTR handle)
{
    if (!valid_protector((void *)handle))
        return NTE_INVALID_HANDLE;
    PROTECTOR *protector = (PROTECTOR *)handle;
    if (InterlockedDecrement(&protector->references) <= 0) {
        protector->size = 0;
        HeapFree(GetProcessHeap(), 0, protector->name);
        HeapFree(GetProcessHeap(), 0, protector);
    }
    return ERROR_SUCCESS;
}

SECURITY_STATUS WINAPI NCryptDuplicateKeyProtectorHandle(ULONG_PTR handle, ULONG_PTR *copy, ULONG_PTR flags)
{
    if (!valid_protector((void *)handle) || !copy)
        return NTE_INVALID_HANDLE;
    InterlockedIncrement(&((PROTECTOR *)handle)->references);
    *copy = handle;
    return ERROR_SUCCESS;
}

/* the protectors wrap keys through their own provider interface, a software provider has none. */
SECURITY_STATUS WINAPI NCryptProtectKey(ULONG_PTR protector, ULONG_PTR a2, ULONG_PTR a3, ULONG_PTR a4, ULONG_PTR a5,
                                        ULONG_PTR a6, ULONG_PTR a7, ULONG_PTR a8)
{
    if (!valid_protector((void *)protector))
        return NTE_INVALID_HANDLE;
    return NTE_NOT_SUPPORTED;
}

SECURITY_STATUS WINAPI NCryptUnprotectKey(ULONG_PTR protector, ULONG_PTR a2, ULONG_PTR a3, ULONG_PTR a4, ULONG_PTR a5,
                                          ULONG_PTR a6, ULONG_PTR a7)
{
    if (!valid_protector((void *)protector))
        return NTE_INVALID_HANDLE;
    return NTE_NOT_SUPPORTED;
}

/* ------------------------------------------------------------------ named descriptors */

#define NAMED_DESCRIPTORS L"Software\\Microsoft\\Cryptography\\NProtect\\NamedDescriptors"

typedef LONG(WINAPI *REG_OPEN)(HKEY, const WCHAR *, DWORD, DWORD, HKEY *);
typedef LONG(WINAPI *REG_CREATE)(HKEY, const WCHAR *, DWORD, WCHAR *, DWORD, DWORD, PVOID, HKEY *, DWORD *);
typedef LONG(WINAPI *REG_QUERY)(HKEY, const WCHAR *, DWORD *, DWORD *, BYTE *, DWORD *);
typedef LONG(WINAPI *REG_SET)(HKEY, const WCHAR *, DWORD, DWORD, const BYTE *, DWORD);
typedef LONG(WINAPI *REG_DELETE)(HKEY, const WCHAR *);
typedef LONG(WINAPI *REG_CLOSE)(HKEY);

static struct {
    REG_OPEN open;
    REG_CREATE create;
    REG_QUERY query;
    REG_SET set;
    REG_DELETE remove;
    REG_CLOSE close;
} g_registry;

static BOOL load_registry(void)
{
    if (g_registry.close)
        return TRUE;
    HMODULE module = LoadLibraryW(L"advapi32.dll");
    if (!module)
        return FALSE;
    g_registry.open = (REG_OPEN)GetProcAddress(module, "RegOpenKeyExW");
    g_registry.create = (REG_CREATE)GetProcAddress(module, "RegCreateKeyExW");
    g_registry.query = (REG_QUERY)GetProcAddress(module, "RegQueryValueExW");
    g_registry.set = (REG_SET)GetProcAddress(module, "RegSetValueExW");
    g_registry.remove = (REG_DELETE)GetProcAddress(module, "RegDeleteValueW");
    g_registry.close = (REG_CLOSE)GetProcAddress(module, "RegCloseKey");
    return g_registry.open && g_registry.create && g_registry.query && g_registry.set && g_registry.remove &&
           g_registry.close;
}

static HKEY scope_key(DWORD flags)
{
    return (flags & NCRYPT_MACHINE_KEY_FLAG) ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
}

SECURITY_STATUS WINAPI NCryptRegisterProtectionDescriptorName(LPCWSTR name, LPCWSTR descriptor, DWORD flags)
{
    if (!name || !*name)
        return NTE_INVALID_PARAMETER;
    if (flags & ~NCRYPT_MACHINE_KEY_FLAG)
        return NTE_BAD_FLAGS;
    if (!load_registry())
        return NTE_NOT_SUPPORTED;
    HKEY key;
    DWORD disposition;
    LONG error = g_registry.create(scope_key(flags), NAMED_DESCRIPTORS, 0, nullptr, 0, 0x20006, nullptr, &key,
                                   &disposition);
    if (error)
        return hresult_from_win32(error);
    if (descriptor && *descriptor)
        error = g_registry.set(key, name, 0, REG_SZ, (const BYTE *)descriptor, (lstrlenW(descriptor) + 1) * 2);
    else
        error = g_registry.remove(key, name);
    g_registry.close(key);
    return hresult_from_win32(error);
}

SECURITY_STATUS WINAPI NCryptQueryProtectionDescriptorName(LPCWSTR name, WCHAR *descriptor, SIZE_T *size, DWORD flags)
{
    SIZE_T capacity = *size;
    *size = 0;
    if (!name || !*name)
        return NTE_INVALID_PARAMETER;
    if (flags & ~NCRYPT_MACHINE_KEY_FLAG)
        return NTE_BAD_FLAGS;
    if (!load_registry())
        return NTE_NOT_SUPPORTED;
    HKEY key;
    LONG error = g_registry.open(scope_key(flags), NAMED_DESCRIPTORS, 0, 0x20019, &key);
    if (error)
        return hresult_from_win32(error);
    DWORD type, bytes = (DWORD)(capacity * 2);
    error = g_registry.query(key, name, nullptr, &type, (BYTE *)descriptor, &bytes);
    g_registry.close(key);
    if (error)
        return hresult_from_win32(error);
    *size = bytes / 2;
    if (descriptor && bytes / 2)
        descriptor[bytes / 2 - 1] = 0;
    return ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ descriptor handles */

SECURITY_STATUS WINAPI NCryptCreateProtectionDescriptor(LPCWSTR text, DWORD flags, NCRYPT_HANDLE *handle)
{
    if (!handle)
        return NTE_INVALID_PARAMETER;
    *handle = 0;
    if (!text || !*text)
        return NTE_INVALID_PARAMETER;
    if (flags & ~(NCRYPT_NAMED_DESCRIPTOR_FLAG | NCRYPT_MACHINE_KEY_FLAG))
        return NTE_BAD_FLAGS;
    WCHAR *named = nullptr;
    if (flags & NCRYPT_NAMED_DESCRIPTOR_FLAG) {
        SIZE_T chars = 0;
        SECURITY_STATUS status = NCryptQueryProtectionDescriptorName(text, nullptr, &chars, flags & ~1u);
        if (status)
            return status;
        if (chars < 2)
            return NTE_NOT_FOUND;
        named = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, chars * 2 + 2);
        if (!named)
            return NTE_NO_MEMORY;
        status = NCryptQueryProtectionDescriptorName(text, named, &chars, flags & ~1u);
        if (status) {
            HeapFree(GetProcessHeap(), 0, named);
            return status;
        }
        text = named;
    }
    SECURITY_STATUS status;
    DESCRIPTOR *descriptor = new_descriptor(text, flags, &status);
    HeapFree(GetProcessHeap(), 0, named);
    if (descriptor)
        *handle = (NCRYPT_HANDLE)descriptor;
    return status;
}

SECURITY_STATUS WINAPI NCryptCloseProtectionDescriptor(ULONG_PTR handle)
{
    if (!valid_descriptor((void *)handle))
        return NTE_INVALID_HANDLE;
    DESCRIPTOR *descriptor = (DESCRIPTOR *)handle;
    descriptor->size = 0;
    free_descriptor_body(descriptor);
    HeapFree(GetProcessHeap(), 0, descriptor);
    return ERROR_SUCCESS;
}

SECURITY_STATUS WINAPI NCryptGetProtectionDescriptorInfo(NCRYPT_HANDLE handle, const ALLOC_PARA *alloc, DWORD type,
                                                         void **info)
{
    if (!info)
        return NTE_INVALID_PARAMETER;
    if (!valid_descriptor((void *)handle))
        return NTE_INVALID_HANDLE;
    *info = nullptr;
    if (!alloc)
        alloc = &g_default_alloc;
    if (type != 1)
        return NTE_NOT_SUPPORTED;
    WCHAR *text = descriptor_string((DESCRIPTOR *)handle, alloc, FALSE);
    if (!text)
        return NTE_NO_MEMORY;
    *info = text;
    return ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ the cryptography under the blobs */

typedef LONG(WINAPI *B_OPEN)(PVOID *, const WCHAR *, const WCHAR *, ULONG);
typedef LONG(WINAPI *B_CLOSE)(PVOID, ULONG);
typedef LONG(WINAPI *B_RANDOM)(PVOID, PUCHAR, ULONG, ULONG);
typedef LONG(WINAPI *B_HASH)(PVOID, PUCHAR, ULONG, PUCHAR, ULONG, PUCHAR, ULONG);
typedef LONG(WINAPI *B_SET)(PVOID, const WCHAR *, PUCHAR, ULONG, ULONG);
typedef LONG(WINAPI *B_KEY)(PVOID, PVOID *, PUCHAR, ULONG, PUCHAR, ULONG, ULONG);
typedef LONG(WINAPI *B_CRYPT)(PVOID, PUCHAR, ULONG, PVOID, PUCHAR, ULONG, PUCHAR, ULONG, ULONG *, ULONG);
typedef LONG(WINAPI *B_DESTROY)(PVOID);

static struct {
    B_OPEN open;
    B_CLOSE close;
    B_RANDOM random;
    B_HASH hash;
    B_SET set;
    B_KEY key;
    B_CRYPT encrypt, decrypt;
    B_DESTROY destroy;
} g_cng;

static BOOL load_cng(void)
{
    if (g_cng.destroy)
        return TRUE;
    HMODULE module = LoadLibraryW(L"bcrypt.dll");
    if (!module)
        return FALSE;
    g_cng.open = (B_OPEN)GetProcAddress(module, "BCryptOpenAlgorithmProvider");
    g_cng.close = (B_CLOSE)GetProcAddress(module, "BCryptCloseAlgorithmProvider");
    g_cng.random = (B_RANDOM)GetProcAddress(module, "BCryptGenRandom");
    g_cng.hash = (B_HASH)GetProcAddress(module, "BCryptHash");
    g_cng.set = (B_SET)GetProcAddress(module, "BCryptSetProperty");
    g_cng.key = (B_KEY)GetProcAddress(module, "BCryptGenerateSymmetricKey");
    g_cng.encrypt = (B_CRYPT)GetProcAddress(module, "BCryptEncrypt");
    g_cng.decrypt = (B_CRYPT)GetProcAddress(module, "BCryptDecrypt");
    g_cng.destroy = (B_DESTROY)GetProcAddress(module, "BCryptDestroyKey");
    return g_cng.open && g_cng.close && g_cng.random && g_cng.hash && g_cng.set && g_cng.key && g_cng.encrypt &&
           g_cng.decrypt && g_cng.destroy;
}

/* sha256 of a buffer, through the pseudo handle of the algorithm. */
static BOOL sha256(const BYTE *data, ULONG size, BYTE *digest)
{
    return g_cng.hash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, (PUCHAR)data, size, digest, 32) >= 0;
}

#define GCM_NONCE 12
#define GCM_TAG 16

/* aes-256 in gcm mode: the nonce is in front of the output and the tag at its end. */
static SECURITY_STATUS seal(const BYTE *key, const BYTE *aad, ULONG aad_size, const BYTE *input, ULONG size, BYTE *output)
{
    PVOID alg = nullptr, handle = nullptr;
    SECURITY_STATUS status = NTE_NOT_SUPPORTED;
    if (g_cng.open(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) < 0)
        return status;
    if (g_cng.set(alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0) >= 0 &&
        g_cng.key(alg, &handle, nullptr, 0, (PUCHAR)key, 32, 0) >= 0) {
        BYTE nonce[GCM_NONCE];
        if (g_cng.random(nullptr, nonce, sizeof(nonce), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
            status = NTE_NOT_SUPPORTED;
        } else {
            BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
            BCRYPT_INIT_AUTH_MODE_INFO(info);
            info.pbNonce = nonce;
            info.cbNonce = sizeof(nonce);
            info.pbAuthData = (PUCHAR)aad;
            info.cbAuthData = aad_size;
            info.pbTag = output + GCM_NONCE + size;
            info.cbTag = GCM_TAG;
            ULONG done = 0;
            status = g_cng.encrypt(handle, (PUCHAR)input, size, &info, nullptr, 0, output + GCM_NONCE, size, &done, 0) >= 0
                         ? ERROR_SUCCESS
                         : NTE_BAD_DATA;
            memcpy(output, nonce, sizeof(nonce));
        }
    }
    if (handle)
        g_cng.destroy(handle);
    g_cng.close(alg, 0);
    return status;
}

static SECURITY_STATUS unseal(const BYTE *key, const BYTE *aad, ULONG aad_size, const BYTE *input, ULONG size, BYTE *output)
{
    if (size < GCM_NONCE + GCM_TAG)
        return NTE_BAD_DATA;
    ULONG content = size - GCM_NONCE - GCM_TAG;
    PVOID alg = nullptr, handle = nullptr;
    SECURITY_STATUS status = NTE_NOT_SUPPORTED;
    if (g_cng.open(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) < 0)
        return status;
    if (g_cng.set(alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0) >= 0 &&
        g_cng.key(alg, &handle, nullptr, 0, (PUCHAR)key, 32, 0) >= 0) {
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
        BCRYPT_INIT_AUTH_MODE_INFO(info);
        info.pbNonce = (PUCHAR)input;
        info.cbNonce = GCM_NONCE;
        info.pbAuthData = (PUCHAR)aad;
        info.cbAuthData = aad_size;
        info.pbTag = (PUCHAR)input + GCM_NONCE + content;
        info.cbTag = GCM_TAG;
        ULONG done = 0;
        status = g_cng.decrypt(handle, (PUCHAR)input + GCM_NONCE, content, &info, nullptr, 0, output, content, &done, 0) >= 0
                     ? ERROR_SUCCESS
                     : NTE_BAD_DATA;
    }
    if (handle)
        g_cng.destroy(handle);
    g_cng.close(alg, 0);
    return status;
}

/* ------------------------------------------------------------------ the secrets of the user and of the machine */

#define SECRET_KEY L"Software\\Microsoft\\Cryptography\\NProtect\\Secrets"

/* 32 random bytes kept in the registry of the user or the machine, made on first use. */
static SECURITY_STATUS scope_secret(BOOL machine, BYTE *secret)
{
    if (!load_registry())
        return NTE_NOT_SUPPORTED;
    HKEY key;
    DWORD disposition;
    LONG error = g_registry.create(machine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, SECRET_KEY, 0, nullptr, 0,
                                   0x20019 | 0x2, nullptr, &key, &disposition);
    if (error)
        return hresult_from_win32(error);
    DWORD type, size = 32;
    error = g_registry.query(key, L"Secret", nullptr, &type, secret, &size);
    if (error || size != 32) {
        if (g_cng.random(nullptr, secret, 32, BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
            g_registry.close(key);
            return NTE_NOT_SUPPORTED;
        }
        error = g_registry.set(key, L"Secret", 0, REG_BINARY, secret, 32);
    } else {
        error = 0;
    }
    g_registry.close(key);
    return hresult_from_win32(error);
}

typedef BOOL(WINAPI *CONVERT_SID)(const WCHAR *, PVOID *);
typedef BOOL(WINAPI *CHECK_MEMBERSHIP)(HANDLE, PVOID, BOOL *);

/* whether the caller is the sid or a member of it, which is what opens a secret protected to a sid. */
static SECURITY_STATUS check_sid(const WCHAR *text, BOOL need_membership, BOOL *member)
{
    HMODULE module = LoadLibraryW(L"advapi32.dll");
    CONVERT_SID convert = module ? (CONVERT_SID)GetProcAddress(module, "ConvertStringSidToSidW") : nullptr;
    CHECK_MEMBERSHIP check = module ? (CHECK_MEMBERSHIP)GetProcAddress(module, "CheckTokenMembership") : nullptr;
    if (!convert || !check)
        return NTE_NOT_SUPPORTED;
    PVOID sid;
    if (!convert(text, &sid))
        return NTE_INVALID_PARAMETER;
    *member = TRUE;
    if (need_membership && !check(nullptr, sid, member))
        *member = FALSE;
    LocalFree(sid);
    return ERROR_SUCCESS;
}

/* what one rule contributes to the key of its group, and whether the caller may use it. */
static SECURITY_STATUS rule_secret(const RULE *rule, BOOL unprotecting, BYTE *secret)
{
    if (!lstrcmpiW(rule->name, L"LOCAL")) {
        BOOL machine = !lstrcmpiW(rule->value, L"machine");
        if (!machine && lstrcmpiW(rule->value, L"user") && lstrcmpiW(rule->value, L"logon"))
            return NTE_INVALID_PARAMETER;
        return scope_secret(machine, secret);
    }
    if (!lstrcmpiW(rule->name, L"SID")) {
        BOOL member;
        SECURITY_STATUS status = check_sid(rule->value, unprotecting, &member);
        if (status)
            return status;
        if (!member)
            return hresult_from_win32(ERROR_ACCESS_DENIED);
        BYTE machine[32];
        status = scope_secret(TRUE, machine);
        if (status)
            return status;
        BYTE buffer[32 + 2 * 200];
        SIZE_T length = lstrlenW(rule->value);
        if (length > 200)
            return NTE_INVALID_PARAMETER;
        memcpy(buffer, machine, 32);
        memcpy(buffer + 32, rule->value, length * 2);
        return sha256(buffer, (ULONG)(32 + length * 2), secret) ? ERROR_SUCCESS : NTE_NOT_SUPPORTED;
    }
    return NTE_NOT_SUPPORTED;
}

/* the key of a group of rules joined by AND is the hash of their secrets in order. */
static SECURITY_STATUS group_key(const DESCRIPTOR *descriptor, ULONG index, BOOL unprotecting, BYTE *key)
{
    BYTE material[MAX_RULES * 32];
    const GROUP *group = &descriptor->groups[index];
    for (ULONG i = 0; i < group->count; i++) {
        SECURITY_STATUS status = rule_secret(&group->rules[i], unprotecting, material + 32 * i);
        if (status)
            return status;
    }
    return sha256(material, 32 * group->count, key) ? ERROR_SUCCESS : NTE_NOT_SUPPORTED;
}

/*
 * the blob: the marker, the descriptor text, the content encrypted with a random key, and for each group the key
 * sealed with the key of the group. the descriptor text is authenticated with the content.
 */
static SECURITY_STATUS protect(const DESCRIPTOR *descriptor, const BYTE *data, ULONG size, const ALLOC_PARA *alloc,
                               BYTE **blob, ULONG *blob_size)
{
    if (!load_cng())
        return NTE_NOT_SUPPORTED;
    WCHAR *text = descriptor_string(descriptor, &g_default_alloc, TRUE);
    if (!text)
        return NTE_NO_MEMORY;
    ULONG text_bytes = (lstrlenW(text) + 1) * 2;
    const ULONG sealed_key = GCM_NONCE + 32 + GCM_TAG;
    ULONG total = 4 + 4 + 4 + text_bytes + 4 + descriptor->group_count * sealed_key + 4 + GCM_NONCE + size + GCM_TAG;
    BYTE *out = alloc->pfnAlloc(total);
    SECURITY_STATUS status = out ? ERROR_SUCCESS : NTE_NO_MEMORY;
    BYTE content_key[32];
    if (!status && g_cng.random(nullptr, content_key, 32, BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        status = NTE_NOT_SUPPORTED;
    if (!status) {
        BYTE *at = out;
        *(ULONG *)at = BLOB_MAGIC;
        *(ULONG *)(at + 4) = 1;
        *(ULONG *)(at + 8) = text_bytes;
        memcpy(at + 12, text, text_bytes);
        at += 12 + text_bytes;
        *(ULONG *)at = descriptor->group_count;
        at += 4;
        for (ULONG g = 0; g < descriptor->group_count && !status; g++) {
            BYTE key[32];
            status = group_key(descriptor, g, FALSE, key);
            if (!status)
                status = seal(key, (BYTE *)text, text_bytes, content_key, 32, at);
            at += sealed_key;
        }
        if (!status) {
            *(ULONG *)at = size;
            at += 4;
            status = seal(content_key, (BYTE *)text, text_bytes, data, size, at);
        }
    }
    memset(content_key, 0, sizeof(content_key));
    HeapFree(GetProcessHeap(), 0, text);
    if (status) {
        if (out)
            alloc->pfnFree(out);
        return status;
    }
    *blob = out;
    *blob_size = total;
    return ERROR_SUCCESS;
}

/* opens the blob with the first group the caller can open. */
static SECURITY_STATUS unprotect(const BYTE *blob, ULONG blob_size, ULONG flags, const ALLOC_PARA *alloc,
                                 DESCRIPTOR **opened, BYTE **data, ULONG *size)
{
    if (!load_cng())
        return NTE_NOT_SUPPORTED;
    if (blob_size < 16 || *(const ULONG *)blob != BLOB_MAGIC || *(const ULONG *)(blob + 4) != 1)
        return NTE_BAD_DATA;
    ULONG text_bytes = *(const ULONG *)(blob + 8);
    if (text_bytes < 2 || (text_bytes & 1) || text_bytes > blob_size - 16)
        return NTE_BAD_DATA;
    const WCHAR *text = (const WCHAR *)(blob + 12);
    if (text[text_bytes / 2 - 1])
        return NTE_BAD_DATA;
    SECURITY_STATUS status;
    DESCRIPTOR *descriptor = new_descriptor(text, 0, &status);
    if (!descriptor)
        return status == STATUS_NOT_FOUND_ ? NTE_BAD_DATA : status;
    const BYTE *at = blob + 12 + text_bytes;
    ULONG left = blob_size - 12 - text_bytes;
    const ULONG sealed_key = GCM_NONCE + 32 + GCM_TAG;
    if (left < 4 || *(const ULONG *)at != descriptor->group_count ||
        left - 4 < descriptor->group_count * sealed_key + 4 + GCM_NONCE + GCM_TAG) {
        NCryptCloseProtectionDescriptor((ULONG_PTR)descriptor);
        return NTE_BAD_DATA;
    }
    at += 4;
    const BYTE *sealed = at;
    at += descriptor->group_count * sealed_key;
    ULONG content = *(const ULONG *)at;
    at += 4;
    if (content > blob_size - (ULONG)(at - blob) - GCM_NONCE - GCM_TAG) {
        NCryptCloseProtectionDescriptor((ULONG_PTR)descriptor);
        return NTE_BAD_DATA;
    }
    if (flags & NCRYPT_UNPROTECT_NO_DECRYPT) {
        *opened = descriptor;
        return ERROR_SUCCESS;
    }
    BYTE content_key[32];
    status = hresult_from_win32(ERROR_ACCESS_DENIED);
    for (ULONG g = 0; g < descriptor->group_count; g++) {
        BYTE key[32];
        SECURITY_STATUS group = group_key(descriptor, g, TRUE, key);
        if (!group)
            group = unseal(key, (const BYTE *)text, text_bytes, sealed + g * sealed_key, sealed_key, content_key);
        if (!group) {
            status = ERROR_SUCCESS;
            break;
        }
        if (g == 0 || status == (SECURITY_STATUS)hresult_from_win32(ERROR_ACCESS_DENIED))
            status = group;
    }
    BYTE *out = nullptr;
    if (!status) {
        out = alloc->pfnAlloc(content ? content : 1);
        status = out ? unseal(content_key, (const BYTE *)text, text_bytes, at, content + GCM_NONCE + GCM_TAG, out)
                     : NTE_NO_MEMORY;
    }
    memset(content_key, 0, sizeof(content_key));
    if (status) {
        if (out)
            alloc->pfnFree(out);
        NCryptCloseProtectionDescriptor((ULONG_PTR)descriptor);
        return status;
    }
    *data = out;
    *size = content;
    *opened = descriptor;
    return ERROR_SUCCESS;
}

SECURITY_STATUS WINAPI NCryptProtectSecret(NCRYPT_HANDLE handle, DWORD flags, const BYTE *data, ULONG size,
                                           const ALLOC_PARA *alloc, HWND window, BYTE **blob, ULONG *blob_size)
{
    *blob = nullptr;
    *blob_size = 0;
    if (!data || !size)
        return NTE_INVALID_PARAMETER;
    if (flags & ~NCRYPT_SILENT_FLAG)
        return NTE_BAD_FLAGS;
    if (!valid_descriptor((void *)handle))
        return NTE_INVALID_HANDLE;
    if (!alloc)
        alloc = &g_default_alloc;
    else if (alloc->cbSize != sizeof(ALLOC_PARA))
        return NTE_INVALID_PARAMETER;
    return protect((DESCRIPTOR *)handle, data, size, alloc, blob, blob_size);
}

SECURITY_STATUS WINAPI NCryptUnprotectSecret(NCRYPT_HANDLE *handle, DWORD flags, const BYTE *blob, ULONG blob_size,
                                             const ALLOC_PARA *alloc, HWND window, BYTE **data, ULONG *size)
{
    if (!blob || !blob_size || !data || !size)
        return NTE_INVALID_PARAMETER;
    if (handle)
        *handle = 0;
    *data = nullptr;
    *size = 0;
    if (flags & ~(NCRYPT_SILENT_FLAG | NCRYPT_UNPROTECT_NO_DECRYPT))
        return NTE_BAD_FLAGS;
    if (!alloc)
        alloc = &g_default_alloc;
    DESCRIPTOR *descriptor = nullptr;
    SECURITY_STATUS status = unprotect(blob, blob_size, flags, alloc, &descriptor, data, size);
    if (status)
        return status;
    if ((flags & NCRYPT_UNPROTECT_NO_DECRYPT) && handle)
        *handle = (NCRYPT_HANDLE)descriptor;
    else
        NCryptCloseProtectionDescriptor((ULONG_PTR)descriptor);
    return ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ streams */

static STREAM *new_stream(ULONG kind, PVOID callback, PVOID context, const DESCRIPTOR *descriptor)
{
    STREAM *stream = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*stream));
    if (!stream)
        return nullptr;
    stream->size = STREAM_SIZE;
    stream->kind = kind;
    stream->callback = callback;
    stream->context = context;
    if (descriptor) {
        SECURITY_STATUS status;
        WCHAR *text = descriptor_string(descriptor, &g_default_alloc, TRUE);
        stream->descriptor = text ? new_descriptor(text, descriptor->flags, &status) : nullptr;
        HeapFree(GetProcessHeap(), 0, text);
        if (!stream->descriptor) {
            HeapFree(GetProcessHeap(), 0, stream);
            return nullptr;
        }
    }
    return stream;
}

static BOOL valid_stream(const void *handle)
{
    return readable(handle, sizeof(STREAM)) && ((const STREAM *)handle)->size == STREAM_SIZE;
}

SECURITY_STATUS WINAPI NCryptStreamOpenToProtect(NCRYPT_HANDLE descriptor, DWORD flags, HWND window,
                                                 const STREAM_INFO *info, NCRYPT_HANDLE *stream)
{
    if (!stream || !info || !info->callback)
        return NTE_INVALID_PARAMETER;
    if (!valid_descriptor((void *)descriptor))
        return NTE_INVALID_HANDLE;
    if (flags & 0xDFFFFF3F)
        return NTE_BAD_FLAGS;
    STREAM *opened = new_stream(1, info->callback, info->context, (DESCRIPTOR *)descriptor);
    if (!opened)
        return NTE_NO_MEMORY;
    *stream = (NCRYPT_HANDLE)opened;
    return ERROR_SUCCESS;
}

static SECURITY_STATUS open_to_unprotect(const STREAM_INFO *info, DWORD flags, DWORD mask, ULONG kind, NCRYPT_HANDLE *stream)
{
    if (!stream || !info || !info->callback)
        return NTE_INVALID_PARAMETER;
    if (flags & mask)
        return NTE_BAD_FLAGS;
    STREAM *opened = new_stream(kind, info->callback, info->context, nullptr);
    if (!opened)
        return NTE_NO_MEMORY;
    *stream = (NCRYPT_HANDLE)opened;
    return ERROR_SUCCESS;
}

SECURITY_STATUS WINAPI NCryptStreamOpenToUnprotect(const STREAM_INFO *info, DWORD flags, HWND window,
                                                   NCRYPT_HANDLE *stream)
{
    return open_to_unprotect(info, flags, 0xDFFFFF3F, 0, stream);
}

SECURITY_STATUS WINAPI NCryptStreamOpenToUnprotectEx(const STREAM_INFO *info, DWORD flags, HWND window,
                                                     NCRYPT_HANDLE *stream)
{
    return open_to_unprotect(info, flags, 0xFFFFFF3E, 2, stream);
}

static void free_stream(STREAM *stream)
{
    stream->size = 0;
    if (stream->descriptor)
        NCryptCloseProtectionDescriptor((ULONG_PTR)stream->descriptor);
    HeapFree(GetProcessHeap(), 0, stream->data);
    HeapFree(GetProcessHeap(), 0, stream);
}

SECURITY_STATUS WINAPI NCryptStreamClose(ULONG_PTR handle)
{
    if (!valid_stream((void *)handle))
        return NTE_INVALID_HANDLE;
    free_stream((STREAM *)handle);
    return ERROR_SUCCESS;
}

typedef LONG(WINAPI *STREAM_OUTPUT)(PVOID, const BYTE *, SIZE_T, BOOL);
typedef LONG(WINAPI *STREAM_OUTPUT_EX)(PVOID, NCRYPT_HANDLE, const BYTE *, SIZE_T, BOOL);

/* the data of a stream is gathered and handled whole at the last update, the callback gets the result once. */
SECURITY_STATUS WINAPI NCryptStreamUpdate(ULONG_PTR handle, PBYTE data, SIZE_T size, BOOL final)
{
    if (!valid_stream((void *)handle))
        return NTE_INVALID_HANDLE;
    STREAM *stream = (STREAM *)handle;
    if ((!data && size) || stream->done)
        return NTE_INVALID_PARAMETER;
    if (size) {
        if (stream->used + size > stream->capacity) {
            SIZE_T capacity = (stream->used + size) * 2;
            BYTE *grown = HeapAlloc(GetProcessHeap(), 0, capacity);
            if (!grown)
                return NTE_NO_MEMORY;
            if (stream->used)
                memcpy(grown, stream->data, stream->used);
            HeapFree(GetProcessHeap(), 0, stream->data);
            stream->data = grown;
            stream->capacity = capacity;
        }
        memcpy(stream->data + stream->used, data, size);
        stream->used += size;
    }
    if (!final)
        return ERROR_SUCCESS;
    stream->done = TRUE;
    BYTE *out = nullptr;
    ULONG out_size = 0;
    SECURITY_STATUS status;
    DESCRIPTOR *opened = nullptr;
    if (stream->kind == 1) {
        status = stream->used ? protect(stream->descriptor, stream->data, (ULONG)stream->used, &g_default_alloc, &out,
                                        &out_size)
                              : NTE_INVALID_PARAMETER;
    } else {
        status = unprotect(stream->data, (ULONG)stream->used, 0, &g_default_alloc, &opened, &out, &out_size);
    }
    if (status)
        return status;
    LONG result;
    if (stream->kind == 2)
        result = ((STREAM_OUTPUT_EX)stream->callback)(stream->context, (NCRYPT_HANDLE)opened, out, out_size, TRUE);
    else
        result = ((STREAM_OUTPUT)stream->callback)(stream->context, out, out_size, TRUE);
    if (opened && stream->kind != 2)
        NCryptCloseProtectionDescriptor((ULONG_PTR)opened);
    HeapFree(GetProcessHeap(), 0, out);
    return result;
}

/* ------------------------------------------------------------------ legacy handles */

typedef BOOL(WINAPI *GET_PROV_PARAM)(ULONG_PTR, DWORD, BYTE *, DWORD *, DWORD);

/* a capi provider and key are looked at, converting them to cng keys needs the capi key export. */
SECURITY_STATUS WINAPI NCryptTranslateHandle(NCRYPT_PROV_HANDLE *provider, NCRYPT_KEY_HANDLE *key,
                                             ULONG_PTR legacy_provider, ULONG_PTR legacy_key, DWORD spec, DWORD flags)
{
    if (!key || !legacy_provider)
        return NTE_INVALID_PARAMETER;
    if (flags)
        return NTE_BAD_FLAGS;
    HMODULE module = LoadLibraryW(L"advapi32.dll");
    GET_PROV_PARAM get = module ? (GET_PROV_PARAM)GetProcAddress(module, "CryptGetProvParam") : nullptr;
    DWORD length = 0;
    if (!get)
        return NTE_NOT_SUPPORTED;
    /* a provider handle that is not memory is not one. */
    if (!readable((void *)legacy_provider, sizeof(void *)))
        return (SECURITY_STATUS)0x80090001;
    if (!get(legacy_provider, 4, nullptr, &length, 0))
        return (SECURITY_STATUS)GetLastError();
    return NTE_NOT_SUPPORTED;
}
