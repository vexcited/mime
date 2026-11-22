/*
 * the data protection interface of the lsa over ncalrpc:[protected_storage], which dpapi.dll and mime's crypt32 speak for
 * CryptProtectData and CryptUnprotectData. the blob and the master key files have the layout of windows, read with
 * dpx.c and dpdec.py of scratch/g2/lsa in the research repository: aes 256 in cbc under a key made of the master
 * key, the salt and the entropy with sha 512, then a sha 512 hmac over the blob. a master key is 64 random bytes in a file
 * named by its id, the user's below AppData\Roaming\Microsoft\Protect\<sid>, the machine's below
 * System32\Microsoft\Protect\S-1-5-18 and the system account's in User below that, with the id of the current one in the
 * file Preferred.
 */
#include "services.h"
#include <mime/win32/rpc.h>
#include <crypto/crypto.h>
#include "services/dpapi.h"

#define DPAPI_VERSION 1
#define DPAPI_LOCAL_MACHINE 0x4
#define DPAPI_SYSTEM 0x20000000
#define CALG_AES_256_ 0x6610
#define CALG_SHA_512_ 0x800e
#define NTE_BAD_KEY_STATE_ 0x8009000b
#define MASTER_KEY_SIZE 64
#define LOCAL_KEY_SIZE 32
#define PROTECTION_KEY_SIZE 20
#define MASTER_KEY_ROUNDS 8000
#define SYSTEM_SECRET L"SECURITY\\Policy\\Secrets\\DPAPI_SYSTEM\\CurrVal"
#define SALT_SIZE 32
#define SIGN_SIZE 64
#define PROFILE_LIST L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList\\"
#define PROTECT L"\\Microsoft\\Protect\\"
#define SYSTEM_SID L"S-1-5-18"

#define MEMORY_SAME_LOGON 2

BOOLEAN WINAPI SystemFunction036(PVOID buffer, ULONG length);
NTSTATUS WINAPI SystemFunction040(PVOID memory, ULONG length, ULONG flags);
NTSTATUS WINAPI SystemFunction041(PVOID memory, ULONG length, ULONG flags);

static const GUID g_provider = {0xdf9d8cd0, 0x1501, 0x11d1, {0x8c, 0x7a, 0x00, 0xc0, 0x4f, 0xc2, 0x97, 0xeb}};

void *mime_crypto_alloc(size_t size) { return SvcAllocZero(size); }
void mime_crypto_free(void *p) { SvcFree(p); }
bool mime_crypto_random(void *out, size_t size) { return SystemFunction036(out, (ULONG)size); }

/* whose keys a call uses: the machine's, the caller's account's, or the system's own when the caller has no profile. */
enum { SCOPE_MACHINE, SCOPE_USER, SCOPE_SYSTEM };

typedef struct KEY_PLACE {
    int Scope;
    WCHAR Sid[96];
    WCHAR Folder[MAX_PATH + 160];
} KEY_PLACE;

static BOOL system_folder(BOOL user, WCHAR *out, size_t chars)
{
    UINT n = GetSystemDirectoryW(out, (UINT)chars);
    if (!n || n + 64 >= chars)
        return FALSE;
    wcscat(out, PROTECT SYSTEM_SID);
    if (user)
        wcscat(out, L"\\User");
    return TRUE;
}

/* the folder of the master keys of the caller, or of the machine. */
static BOOL key_place(BOOL machine, KEY_PLACE *place)
{
    WCHAR profile[MAX_PATH] = L"";
    place->Scope = machine ? SCOPE_MACHINE : SCOPE_SYSTEM;
    wcscpy(place->Sid, SYSTEM_SID);
    /* the caller is the identity the call came with: a service that impersonates its client protects for the client. */
    if (!machine && RpcImpersonateClient(nullptr) == RPC_S_OK) {
        HANDLE token = nullptr;
        BYTE user[96];
        DWORD size = 0;
        WCHAR *text = nullptr;
        BOOL known = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token) &&
                     GetTokenInformation(token, TokenUser, user, sizeof(user), &size) &&
                     ConvertSidToStringSidW(*(PSID *)user, &text) && wcslen(text) < RTL_NUMBER_OF(place->Sid);
        RpcRevertToSelf();
        if (known) {
            WCHAR key[200];
            DWORD bytes = sizeof(profile), type;
            wcscpy(key, PROFILE_LIST);
            wcscat(key, text);
            if (RegGetValueW(HKEY_LOCAL_MACHINE, key, L"ProfileImagePath", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, &type,
                             profile, &bytes))
                profile[0] = 0;
            if (profile[0])
                wcscpy(place->Sid, text);
        }
        if (text)
            LocalFree(text);
        if (token)
            CloseHandle(token);
    }
    if (!profile[0])
        return system_folder(!machine, place->Folder, MAX_PATH + 96);
    if (wcslen(profile) + wcslen(place->Sid) + 64 >= MAX_PATH + 96)
        return FALSE;
    place->Scope = SCOPE_USER;
    wcscpy(place->Folder, profile);
    wcscat(place->Folder, L"\\AppData\\Roaming" PROTECT);
    wcscat(place->Folder, place->Sid);
    return TRUE;
}

static void make_folders(WCHAR *path)
{
    for (WCHAR *p = wcschr(path + 3, L'\\'); p; p = wcschr(p + 1, L'\\')) {
        *p = 0;
        CreateDirectoryW(path, nullptr);
        *p = L'\\';
    }
    CreateDirectoryW(path, nullptr);
}

static DWORD read_file(const WCHAR *path, void *out, DWORD room)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return 0;
    DWORD got = 0;
    if (!ReadFile(file, out, room, &got, nullptr))
        got = 0;
    CloseHandle(file);
    return got;
}

static BOOL write_file(const WCHAR *path, const void *data, DWORD size, DWORD attributes)
{
    /* a hidden system file is only replaced by one with the same attributes. */
    SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, attributes, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    DWORD put = 0;
    BOOL ok = WriteFile(file, data, size, &put, nullptr) && put == size;
    CloseHandle(file);
    return ok;
}

static void guid_text(const GUID *id, WCHAR out[37])
{
    static const WCHAR digits[] = L"0123456789abcdef";
    const BYTE *b = (const BYTE *)id;
    static const BYTE order[16] = {3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15};
    WCHAR *p = out;
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            *p++ = L'-';
        *p++ = digits[b[order[i]] >> 4];
        *p++ = digits[b[order[i]] & 15];
    }
    *p = 0;
}

static void key_path(const WCHAR *folder, const GUID *id, WCHAR *out)
{
    wcscpy(out, folder);
    wcscat(out, L"\\");
    guid_text(id, out + wcslen(out));
}

/*
 * the secret of the security authority that seals the keys of the machine: twenty bytes for the keys of the machine and
 * twenty for the keys of the system account, after a version. windows keeps it at this place sealed with the key of its
 * secrets, which mime does not have: here the value is the secret itself, in a key only the system opens.
 */
static BOOL system_secret(BYTE machine[PROTECTION_KEY_SIZE], BYTE user[PROTECTION_KEY_SIZE])
{
    static SRWLOCK lock = SRWLOCK_INIT;
    struct {
        DWORD Version;
        BYTE Machine[PROTECTION_KEY_SIZE], User[PROTECTION_KEY_SIZE];
    } secret = {};
    PSECURITY_DESCRIPTOR sd = nullptr;
    HKEY key = nullptr;
    BOOL ok = FALSE;
    AcquireSRWLockExclusive(&lock);
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;CI;GA;;;SY)", 1, &sd, nullptr)) {
        SECURITY_ATTRIBUTES attributes = {sizeof(attributes), sd, FALSE};
        if (!RegCreateKeyExW(HKEY_LOCAL_MACHINE, SYSTEM_SECRET, 0, nullptr, 0, KEY_READ | KEY_WRITE, &attributes, &key,
                             nullptr)) {
            DWORD size = sizeof(secret), type = 0;
            ok = !RegQueryValueExW(key, nullptr, nullptr, &type, (BYTE *)&secret, &size) && size == sizeof(secret) &&
                 secret.Version == 1;
            if (!ok) {
                secret.Version = 1;
                ok = SystemFunction036(secret.Machine, 2 * PROTECTION_KEY_SIZE) &&
                     !RegSetValueExW(key, nullptr, 0, REG_NONE, (const BYTE *)&secret, sizeof(secret));
            }
            RegCloseKey(key);
        }
        LocalFree(sd);
    }
    ReleaseSRWLockExclusive(&lock);
    memcpy(machine, secret.Machine, PROTECTION_KEY_SIZE);
    memcpy(user, secret.User, PROTECTION_KEY_SIZE);
    mime_wipe(&secret, sizeof(secret));
    return ok;
}

/* the key of an account's files: the sha1 of its password, here the empty one mime's accounts have, mixed with its sid. */
static void account_key(const WCHAR *sid, const void *password, size_t password_size, BYTE out[PROTECTION_KEY_SIZE])
{
    BYTE hashed[20];
    mime_hash(&mime_sha1, password, password_size, hashed);
    mime_hmac(&mime_sha1, hashed, sizeof(hashed), sid, (wcslen(sid) + 1) * sizeof(WCHAR), out);
    mime_wipe(hashed, sizeof(hashed));
}

/* the keys a file of a place can be sealed with, the one new files get first. the others are of former versions. */
static DWORD protection_keys(const KEY_PLACE *place, BYTE keys[4][PROTECTION_KEY_SIZE])
{
    if (place->Scope == SCOPE_USER) {
        account_key(place->Sid, L"", 0, keys[0]);
        return 1;
    }
    BYTE machine[PROTECTION_KEY_SIZE], user[PROTECTION_KEY_SIZE];
    if (!system_secret(machine, user))
        return 0;
    memcpy(keys[0], place->Scope == SCOPE_MACHINE ? machine : user, PROTECTION_KEY_SIZE);
    memcpy(keys[1], place->Scope == SCOPE_MACHINE ? user : machine, PROTECTION_KEY_SIZE);
    /* mime's crypt32 sealed the machine's keys with the installation's id before it called this service. */
    WCHAR guid[64];
    DWORD bytes = sizeof(guid);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid", RRF_RT_REG_SZ, nullptr,
                     guid, &bytes))
        return 2;
    account_key(SYSTEM_SID, guid, wcslen(guid) * sizeof(WCHAR), keys[2]);
    return 3;
}

/*
 * the file of a master key, as windows writes it: a head, then the key sealed with the protection key, a second secret
 * sealed the same way and the id of the credential history. a sealed block is a salt, a count of rounds, the hash and the
 * cipher, then under aes 256 in cbc a salt, the hmac of the secret keyed by the hmac of that salt, and the secret.
 */
#pragma pack(push, 4)
typedef struct MASTER_KEY_HEAD {
    DWORD Version, Reserved1, Reserved2;
    WCHAR Id[36];
    DWORD Reserved3, Reserved4, Policy;
    ULONG64 MasterSize, BackupSize, HistorySize, DomainSize;
} MASTER_KEY_HEAD;

typedef struct SEALED_HEAD {
    DWORD Version;
    BYTE Salt[16];
    DWORD Rounds, Hash, Cipher;
} SEALED_HEAD;

typedef struct HISTORY_BLOCK {
    DWORD Version;
    GUID Id;
} HISTORY_BLOCK;
#pragma pack(pop)

_Static_assert(sizeof(MASTER_KEY_HEAD) == 128, "master key file head");
_Static_assert(sizeof(SEALED_HEAD) == 32, "sealed block head");
_Static_assert(sizeof(HISTORY_BLOCK) == 20, "credential history block");

#define SEALED_SIZE(secret) (sizeof(SEALED_HEAD) + 16 + 64 + (secret))

/*
 * the key and vector of a sealed block. windows stretches the protection key with a pbkdf2 whose every round hashes the
 * running xor instead of the round before, so the result is not the one of pbkdf2. mime's crypt32 wrote files with the
 * real pbkdf2 before it called this service.
 */
static void stretch(const BYTE key[PROTECTION_KEY_SIZE], const SEALED_HEAD *head, BOOL standard, BYTE out[48])
{
    if (standard) {
        mime_pbkdf2(&mime_sha512, key, PROTECTION_KEY_SIZE, head->Salt, sizeof(head->Salt), head->Rounds, out, 48);
        return;
    }
    BYTE first[sizeof(head->Salt) + 4], derived[64], round[64];
    memcpy(first, head->Salt, sizeof(head->Salt));
    first[16] = first[17] = first[18] = 0;
    first[19] = 1;
    mime_hmac(&mime_sha512, key, PROTECTION_KEY_SIZE, first, sizeof(first), derived);
    for (DWORD i = 1; i < head->Rounds; i++) {
        mime_hmac(&mime_sha512, key, PROTECTION_KEY_SIZE, derived, sizeof(derived), round);
        for (int k = 0; k < 64; k++)
            derived[k] ^= round[k];
    }
    memcpy(out, derived, 48);
    mime_wipe(derived, sizeof(derived));
    mime_wipe(round, sizeof(round));
}

static void sealed_mac(const BYTE key[PROTECTION_KEY_SIZE], const BYTE salt[16], const BYTE *secret, DWORD size,
                       BYTE out[64])
{
    BYTE inner[64];
    mime_hmac(&mime_sha512, key, PROTECTION_KEY_SIZE, salt, 16, inner);
    mime_hmac(&mime_sha512, inner, sizeof(inner), secret, size, out);
    mime_wipe(inner, sizeof(inner));
}

static BOOL seal_block(const BYTE key[PROTECTION_KEY_SIZE], const BYTE *secret, DWORD size, BYTE *out)
{
    SEALED_HEAD *head = (SEALED_HEAD *)out;
    BYTE *body = out + sizeof(*head), derived[48];
    head->Version = 2;
    head->Rounds = MASTER_KEY_ROUNDS;
    head->Hash = CALG_SHA_512_;
    head->Cipher = CALG_AES_256_;
    if (!SystemFunction036(head->Salt, sizeof(head->Salt)) || !SystemFunction036(body, 16))
        return FALSE;
    sealed_mac(key, body, secret, size, body + 16);
    memcpy(body + 80, secret, size);
    stretch(key, head, FALSE, derived);
    MimeAes aes;
    mime_aes_init(&aes, derived, 32);
    mime_cbc_encrypt(&mime_cipher_aes, &aes, derived + 32, body, body, 80 + size);
    mime_wipe(derived, sizeof(derived));
    mime_wipe(&aes, sizeof(aes));
    return TRUE;
}

static BOOL unseal_block(const BYTE key[PROTECTION_KEY_SIZE], const BYTE *block, DWORD block_size, BOOL standard, BYTE *secret,
                   DWORD size)
{
    SEALED_HEAD head;
    BYTE body[80 + MASTER_KEY_SIZE], derived[48], mac[64];
    if (block_size != SEALED_SIZE(size) || size > MASTER_KEY_SIZE)
        return FALSE;
    memcpy(&head, block, sizeof(head));
    if (head.Version != 2 || head.Hash != CALG_SHA_512_ || head.Cipher != CALG_AES_256_ || !head.Rounds ||
        head.Rounds > 1000000)
        return FALSE;
    stretch(key, &head, standard, derived);
    MimeAes aes;
    mime_aes_init(&aes, derived, 32);
    mime_cbc_decrypt(&mime_cipher_aes, &aes, derived + 32, block + sizeof(head), body, 80 + size);
    sealed_mac(key, body, body + 80, size, mac);
    BOOL ok = mime_ct_equal(mac, body + 16, sizeof(mac));
    if (ok)
        memcpy(secret, body + 80, size);
    mime_wipe(derived, sizeof(derived));
    mime_wipe(&aes, sizeof(aes));
    mime_wipe(body, sizeof(body));
    return ok;
}

/* the id of the credential history of an account, the file CREDHIST next to the folder of its keys. */
static void history_id(const KEY_PLACE *place, GUID *id)
{
    struct {
        DWORD Version;
        GUID Id;
        DWORD Next;
    } history = {};
    WCHAR path[MAX_PATH + 240];
    memset(id, 0, sizeof(*id));
    if (place->Scope != SCOPE_USER)
        return;
    wcscpy(path, place->Folder);
    WCHAR *end = wcsrchr(path, L'\\');
    wcscpy(end, L"\\CREDHIST");
    if (read_file(path, &history, sizeof(history)) != sizeof(history) || history.Version != 1) {
        history.Version = 1;
        history.Next = 0;
        if (UuidCreate(&history.Id) != RPC_S_OK ||
            !write_file(path, &history, sizeof(history), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))
            return;
    }
    *id = history.Id;
}

static BOOL write_master_key(const KEY_PLACE *place, const BYTE protection[PROTECTION_KEY_SIZE], const GUID *id,
                             const BYTE key[MASTER_KEY_SIZE])
{
    BYTE file[sizeof(MASTER_KEY_HEAD) + SEALED_SIZE(MASTER_KEY_SIZE) + SEALED_SIZE(LOCAL_KEY_SIZE) + sizeof(HISTORY_BLOCK)] = {};
    MASTER_KEY_HEAD *head = (MASTER_KEY_HEAD *)file;
    WCHAR text[37], path[MAX_PATH + 240];
    BYTE local[LOCAL_KEY_SIZE];
    guid_text(id, text);
    head->Version = 2;
    memcpy(head->Id, text, sizeof(head->Id));
    head->Policy = place->Scope == SCOPE_USER ? 5 : 6;
    head->MasterSize = SEALED_SIZE(MASTER_KEY_SIZE);
    head->BackupSize = SEALED_SIZE(LOCAL_KEY_SIZE);
    head->HistorySize = sizeof(HISTORY_BLOCK);
    BYTE *at = file + sizeof(*head);
    /* the second block holds a secret of 32 bytes. what windows seals it with is not known, here it is the protection key. */
    if (!SystemFunction036(local, sizeof(local)) || !seal_block(protection, key, MASTER_KEY_SIZE, at) ||
        !seal_block(protection, local, sizeof(local), at + SEALED_SIZE(MASTER_KEY_SIZE)))
        return FALSE;
    HISTORY_BLOCK history = {3};
    history_id(place, &history.Id);
    memcpy(at + SEALED_SIZE(MASTER_KEY_SIZE) + SEALED_SIZE(LOCAL_KEY_SIZE), &history, sizeof(history));
    key_path(place->Folder, id, path);
    BOOL ok = write_file(path, file, sizeof(file), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    mime_wipe(file, sizeof(file));
    mime_wipe(local, sizeof(local));
    return ok;
}

/*
 * the master key of a file. a file of windows' layout opens with the protection key of its place. the files of former
 * versions open too, and are written again as windows writes them: the bare 64 bytes this service kept, and the files of
 * mime's crypt32, which had windows' layout with the stretching of pbkdf2 and its own key for the machine.
 */
static BOOL read_master_key(const KEY_PLACE *place, const GUID *id, BYTE key[MASTER_KEY_SIZE])
{
    BYTE file[1024], keys[4][PROTECTION_KEY_SIZE];
    WCHAR path[MAX_PATH + 240];
    key_path(place->Folder, id, path);
    DWORD size = read_file(path, file, sizeof(file));
    DWORD count = size ? protection_keys(place, keys) : 0;
    BOOL found = FALSE, current = FALSE;
    if (size == MASTER_KEY_SIZE && count) {
        memcpy(key, file, MASTER_KEY_SIZE);
        found = TRUE;
    } else if (size >= sizeof(MASTER_KEY_HEAD) + SEALED_SIZE(MASTER_KEY_SIZE)) {
        const MASTER_KEY_HEAD *head = (const MASTER_KEY_HEAD *)file;
        if (head->Version == 2 && head->MasterSize == SEALED_SIZE(MASTER_KEY_SIZE)) {
            for (DWORD i = 0; i < count && !found; i++) {
                for (int standard = 0; standard < 2 && !found; standard++) {
                    found = unseal_block(keys[i], file + sizeof(*head), SEALED_SIZE(MASTER_KEY_SIZE), standard, key,
                                   MASTER_KEY_SIZE);
                    current = found && !i && !standard;
                }
            }
        }
    }
    if (found && !current)
        write_master_key(place, keys[0], id, key);
    mime_wipe(keys, sizeof(keys));
    mime_wipe(file, sizeof(file));
    return found;
}

/* the master key with this id, or the current one, made when there is none yet. */
static DWORD master_key(BOOL machine, const GUID *wanted, GUID *id, BYTE key[MASTER_KEY_SIZE])
{
    KEY_PLACE *place = SvcAllocZero(2 * sizeof(*place));
    if (!place)
        return ERROR_NOT_ENOUGH_MEMORY;
    DWORD error = ERROR_PATH_NOT_FOUND;
    if (!key_place(machine, place))
        goto done;
    if (wanted) {
        *id = *wanted;
        error = ERROR_SUCCESS;
        if (read_master_key(place, id, key))
            goto done;
        /* the keys of the machine and of the system account shared one folder, then mime's crypt32 kept the machine's in the other. */
        KEY_PLACE *other = place + 1;
        other->Scope = place->Scope == SCOPE_MACHINE ? SCOPE_SYSTEM : SCOPE_MACHINE;
        wcscpy(other->Sid, SYSTEM_SID);
        if (place->Scope == SCOPE_USER || !system_folder(other->Scope == SCOPE_SYSTEM, other->Folder, MAX_PATH + 96) ||
            !read_master_key(other, id, key))
            error = NTE_BAD_KEY_STATE_;
        goto done;
    }
    struct {
        GUID Id;
        FILETIME Expires;
    } preferred = {};
    WCHAR preferred_path[MAX_PATH + 240];
    wcscpy(preferred_path, place->Folder);
    wcscat(preferred_path, L"\\Preferred");
    error = ERROR_SUCCESS;
    if (read_file(preferred_path, &preferred, sizeof(preferred)) == sizeof(preferred) &&
        read_master_key(place, &preferred.Id, key)) {
        *id = preferred.Id;
        goto done;
    }
    make_folders(place->Folder);
    BYTE keys[4][PROTECTION_KEY_SIZE];
    error = ERROR_NOT_ENOUGH_MEMORY;
    if (!protection_keys(place, keys) || UuidCreate(&preferred.Id) != RPC_S_OK || !SystemFunction036(key, MASTER_KEY_SIZE))
        goto done;
    /* windows renews a master key after ninety days. the old ones stay, as theirs do. */
    GetSystemTimeAsFileTime(&preferred.Expires);
    ULONGLONG expires = ((ULONGLONG)preferred.Expires.dwHighDateTime << 32 | preferred.Expires.dwLowDateTime) +
                        90ull * 24 * 3600 * 10000000;
    preferred.Expires.dwLowDateTime = (DWORD)expires;
    preferred.Expires.dwHighDateTime = (DWORD)(expires >> 32);
    error = ERROR_SUCCESS;
    if (!write_master_key(place, keys[0], &preferred.Id, key) ||
        !write_file(preferred_path, &preferred, sizeof(preferred), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))
        error = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
    mime_wipe(keys, sizeof(keys));
    *id = preferred.Id;
done:
    SvcFree(place);
    return error;
}

/*
 * the data crosses sealed by RtlEncryptMemory for the caller's logon session, padded to eight bytes with the count of
 * the padding in each of its bytes, read in CryptProtectDataNoUI of dpapi.dll. the session is the caller's, so the
 * server opens and seals as the caller.
 */
static DWORD session_crypt(BYTE *data, DWORD size, BOOL seal)
{
    if (!size || size % 8)
        return ERROR_INVALID_PARAMETER;
    RPC_STATUS status = RpcImpersonateClient(nullptr);
    if (status)
        return status;
    NTSTATUS result = seal ? SystemFunction040(data, size, MEMORY_SAME_LOGON)
                           : SystemFunction041(data, size, MEMORY_SAME_LOGON);
    RpcRevertToSelf();
    return result < 0 ? ERROR_INVALID_DATA : ERROR_SUCCESS;
}

static void hmac_of(const BYTE hashed[20], const BYTE *a, DWORD a_size, const BYTE *b, DWORD b_size, const BYTE *c,
                    DWORD c_size, BYTE out[64])
{
    MimeHmac h;
    mime_hmac_init(&h, &mime_sha512, hashed, 20);
    mime_hmac_update(&h, a, a_size);
    if (b_size)
        mime_hmac_update(&h, b, b_size);
    if (c_size)
        mime_hmac_update(&h, c, c_size);
    mime_hmac_final(&h, out);
    mime_wipe(&h, sizeof(h));
}

static void put32(BYTE **at, DWORD value)
{
    memcpy(*at, &value, 4);
    *at += 4;
}

static void put(BYTE **at, const void *data, DWORD size)
{
    memcpy(*at, data, size);
    *at += size;
}

/* a blob of data under the current master key of the caller or the machine. the security authority's own blobs say so. */
DWORD DpapiProtect(DWORD flags, const WCHAR *description, const BYTE *in, DWORD in_size, const BYTE *entropy,
                   DWORD entropy_size, BYTE **out, DWORD *out_size)
{
    *out = nullptr;
    *out_size = 0;
    BOOL machine = (flags & DPAPI_LOCAL_MACHINE) != 0;
    GUID id;
    BYTE key[MASTER_KEY_SIZE], hashed[20], session[64], salt[SALT_SIZE], sign_key[SALT_SIZE], sign[SIGN_SIZE];
    DWORD error = master_key(machine, nullptr, &id, key);
    if (error)
        return error;
    if (!SystemFunction036(salt, sizeof(salt)) || !SystemFunction036(sign_key, sizeof(sign_key)))
        return ERROR_NOT_ENOUGH_MEMORY;
    DWORD text = (DWORD)((wcslen(description) + 1) * sizeof(WCHAR));
    DWORD padded = (in_size / 16 + 1) * 16;
    DWORD total = 44 + 4 + text + 8 + 4 + SALT_SIZE + 4 + 8 + 4 + SALT_SIZE + 4 + padded + 4 + SIGN_SIZE;
    BYTE *blob = MIDL_user_allocate(total);
    if (!blob)
        return ERROR_NOT_ENOUGH_MEMORY;
    BYTE *at = blob;
    put32(&at, DPAPI_VERSION);
    put(&at, &g_provider, sizeof(GUID));
    BYTE *signed_from = at;
    put32(&at, DPAPI_VERSION);
    put(&at, &id, sizeof(GUID));
    put32(&at, flags & (DPAPI_LOCAL_MACHINE | DPAPI_SYSTEM));
    put32(&at, text);
    put(&at, description, text);
    put32(&at, CALG_AES_256_);
    put32(&at, 256);
    put32(&at, SALT_SIZE);
    put(&at, salt, SALT_SIZE);
    put32(&at, 0);
    put32(&at, CALG_SHA_512_);
    put32(&at, 512);
    put32(&at, SALT_SIZE);
    put(&at, sign_key, SALT_SIZE);
    put32(&at, padded);
    mime_hash(&mime_sha1, key, MASTER_KEY_SIZE, hashed);
    hmac_of(hashed, salt, SALT_SIZE, entropy, entropy ? entropy_size : 0, nullptr, 0, session);
    MimeAes aes;
    BYTE iv[16] = {};
    mime_aes_init(&aes, session, 32);
    memcpy(at, in, in_size);
    memset(at + in_size, (int)(padded - in_size), padded - in_size);
    mime_cbc_encrypt(&mime_cipher_aes, &aes, iv, at, at, padded);
    at += padded;
    hmac_of(hashed, sign_key, SALT_SIZE, entropy, entropy ? entropy_size : 0, signed_from, (DWORD)(at - signed_from),
            sign);
    put32(&at, SIGN_SIZE);
    put(&at, sign, SIGN_SIZE);
    mime_wipe(key, sizeof(key));
    mime_wipe(hashed, sizeof(hashed));
    mime_wipe(session, sizeof(session));
    mime_wipe(&aes, sizeof(aes));
    *out = blob;
    *out_size = total;
    return ERROR_SUCCESS;
}

unsigned long WINAPI dpapi_SSCryptProtectData_s(handle_t h, byte **out, unsigned long *out_size, byte *in,
                                                unsigned long in_size, unsigned short *description, byte *entropy,
                                                unsigned long entropy_size, SSCRYPTPROTECTDATA_PROMPTSTRUCT *prompt,
                                                unsigned long flags, byte *password, unsigned long password_size)
{
    *out = nullptr;
    *out_size = 0;
    if (in_size > 0x40000000)
        return ERROR_INVALID_PARAMETER;
    DWORD opened = session_crypt(in, in_size, FALSE);
    if (opened)
        return opened;
    BYTE padding = in[in_size - 1];
    if (!padding || padding > 8)
        return ERROR_INVALID_PARAMETER;
    DWORD size = 0;
    DWORD error = DpapiProtect(flags & DPAPI_LOCAL_MACHINE, (const WCHAR *)description, in, in_size - padding, entropy,
                               entropy ? entropy_size : 0, out, &size);
    *out_size = size;
    return error;
}

/* a counted field of the blob, nothing when it runs past the end. */
static const BYTE *field(const BYTE **at, const BYTE *end, DWORD *size)
{
    if (end - *at < 4)
        return nullptr;
    memcpy(size, *at, 4);
    *at += 4;
    if ((DWORD)(end - *at) < *size)
        return nullptr;
    const BYTE *data = *at;
    *at += *size;
    return data;
}

/*
 * the data of a blob, in a block with room for the padding of the way back. a blob the security authority made for
 * itself only opens for it: windows answers ERROR_INVALID_DATA to a program that asks.
 */
DWORD DpapiUnprotect(const BYTE *in, DWORD in_size, const BYTE *entropy, DWORD entropy_size, BOOL system, BYTE **out,
                     DWORD *out_size, WCHAR **description)
{
    *out = nullptr;
    *out_size = 0;
    if (description)
        *description = nullptr;
    const BYTE *at = in, *end = in + in_size;
    DWORD version, key_version, blob_flags, text_size, numbers[2], salt_size, unused_size, sign_key_size, data_size,
        sign_size;
    GUID id;
    if (in_size < 44 + 4)
        return ERROR_INVALID_PARAMETER;
    memcpy(&version, at, 4);
    if (memcmp(at + 4, &g_provider, sizeof(GUID)))
        return ERROR_INVALID_PARAMETER;
    if (version != DPAPI_VERSION)
        return ERROR_INVALID_DATA;
    const BYTE *signed_from = at + 20;
    memcpy(&key_version, at + 20, 4);
    memcpy(&id, at + 24, sizeof(GUID));
    memcpy(&blob_flags, at + 40, 4);
    at += 44;
    const BYTE *text = field(&at, end, &text_size);
    if (!text || end - at < 8)
        return ERROR_INVALID_DATA;
    memcpy(numbers, at, 8);
    at += 8;
    const BYTE *salt = field(&at, end, &salt_size);
    if (!salt || numbers[0] != CALG_AES_256_ || numbers[1] != 256 || !field(&at, end, &unused_size) || end - at < 8)
        return ERROR_INVALID_DATA;
    memcpy(numbers, at, 8);
    at += 8;
    const BYTE *sign_key = field(&at, end, &sign_key_size);
    const BYTE *data = sign_key ? field(&at, end, &data_size) : nullptr;
    const BYTE *signed_to = at;
    const BYTE *sign = data ? field(&at, end, &sign_size) : nullptr;
    if (!sign || numbers[0] != CALG_SHA_512_ || numbers[1] != 512 || sign_size != SIGN_SIZE || !data_size ||
        data_size % 16 || text_size % 2 || key_version != DPAPI_VERSION)
        return ERROR_INVALID_DATA;
    BYTE key[MASTER_KEY_SIZE], hashed[20], session[64], expected[SIGN_SIZE];
    if ((blob_flags & DPAPI_SYSTEM) && !system)
        return ERROR_INVALID_DATA;
    DWORD error = master_key((blob_flags & DPAPI_LOCAL_MACHINE) != 0, &id, &id, key);
    if (error)
        return error;
    mime_hash(&mime_sha1, key, MASTER_KEY_SIZE, hashed);
    mime_wipe(key, sizeof(key));
    hmac_of(hashed, sign_key, sign_key_size, entropy, entropy ? entropy_size : 0, signed_from,
            (DWORD)(signed_to - signed_from), expected);
    if (!mime_ct_equal(expected, sign, SIGN_SIZE)) {
        /* mime's crypt32 signed with a key made of the salt and the entropy before it called this service. */
        BYTE former[64];
        hmac_of(hashed, sign_key, sign_key_size, entropy, entropy ? entropy_size : 0, nullptr, 0, former);
        mime_hmac(&mime_sha512, former, sizeof(former), signed_from, (size_t)(signed_to - signed_from), expected);
        mime_wipe(former, sizeof(former));
        if (!mime_ct_equal(expected, sign, SIGN_SIZE)) {
            /*
             * this service once took the first 64 bytes of a file of mime's crypt32 for the key it held: the head of
             * the file, which is its version and the start of its name.
             */
            struct {
                DWORD Version, Reserved1, Reserved2;
                WCHAR Id[26];
            } head = {2, 0, 0, {}};
            WCHAR name[37];
            guid_text(&id, name);
            memcpy(head.Id, name, sizeof(head.Id));
            mime_hash(&mime_sha1, &head, sizeof(head), hashed);
            hmac_of(hashed, sign_key, sign_key_size, entropy, entropy ? entropy_size : 0, signed_from,
                    (DWORD)(signed_to - signed_from), expected);
            if (!mime_ct_equal(expected, sign, SIGN_SIZE)) {
                mime_wipe(hashed, sizeof(hashed));
                return ERROR_INVALID_DATA;
            }
        }
    }
    hmac_of(hashed, salt, salt_size, entropy, entropy ? entropy_size : 0, nullptr, 0, session);
    mime_wipe(hashed, sizeof(hashed));
    BYTE *plain = MIDL_user_allocate(data_size + 8);
    WCHAR *text_copy = MIDL_user_allocate(text_size + sizeof(WCHAR));
    if (!plain || !text_copy) {
        MIDL_user_free(plain);
        MIDL_user_free(text_copy);
        mime_wipe(session, sizeof(session));
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    MimeAes aes;
    BYTE iv[16] = {};
    mime_aes_init(&aes, session, 32);
    mime_cbc_decrypt(&mime_cipher_aes, &aes, iv, data, plain, data_size);
    mime_wipe(session, sizeof(session));
    mime_wipe(&aes, sizeof(aes));
    BYTE pad = plain[data_size - 1];
    if (!pad || pad > 16) {
        MIDL_user_free(plain);
        MIDL_user_free(text_copy);
        return ERROR_INVALID_DATA;
    }
    memcpy(text_copy, text, text_size);
    text_copy[text_size / sizeof(WCHAR)] = 0;
    if (description)
        *description = text_copy;
    else
        MIDL_user_free(text_copy);
    *out = plain;
    *out_size = data_size - pad;
    return ERROR_SUCCESS;
}

unsigned long WINAPI dpapi_SSCryptUnprotectData_s(handle_t h, byte **out, unsigned long *out_size, byte *in,
                                                  unsigned long in_size, unsigned short **description, byte *entropy,
                                                  unsigned long entropy_size, SSCRYPTPROTECTDATA_PROMPTSTRUCT *prompt,
                                                  unsigned long flags, byte *password, unsigned long password_size)
{
    BYTE *plain = nullptr;
    DWORD plain_size = 0;
    WCHAR *text = nullptr;
    *out = nullptr;
    *out_size = 0;
    *description = nullptr;
    DWORD error = DpapiUnprotect(in, in_size, entropy, entropy ? entropy_size : 0, FALSE, &plain, &plain_size, &text);
    if (error)
        return error;
    DWORD sealed_size = (plain_size / 8 + 1) * 8;
    memset(plain + plain_size, (int)(sealed_size - plain_size), sealed_size - plain_size);
    DWORD sealed = session_crypt(plain, sealed_size, TRUE);
    if (sealed) {
        mime_wipe(plain, sealed_size);
        MIDL_user_free(plain);
        MIDL_user_free(text);
        return sealed;
    }
    *description = (unsigned short *)text;
    *out = plain;
    *out_size = sealed_size;
    return ERROR_SUCCESS;
}

unsigned long WINAPI dpapi_SSCryptUpdateProtectedState_s(handle_t h)
{
    SvcLog("dpapi: SSCryptUpdateProtectedState is not served");
    return ERROR_CALL_NOT_IMPLEMENTED;
}

static RPC_STATUS RPC_ENTRY allow_local(RPC_IF_HANDLE interface, void *context)
{
    return RPC_S_OK;
}

/* D:(A;;0x1;;;WD)(A;;0x1;;;AC): every local caller connects and calls, app containers included. */
static const BYTE g_endpoint_sd[] = {
    0x01, 0x00, 0x04, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x34, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00, 0x18, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x02, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
};

void DpapiStart(void)
{
    RPC_STATUS status = RpcServerUseProtseqEpW((RPC_WSTR)L"ncalrpc", RPC_C_PROTSEQ_MAX_REQS_DEFAULT,
                                               (RPC_WSTR)L"protected_storage", (void *)g_endpoint_sd);
    if (!status)
        status = RpcServerRegisterIf3(dpapi_s_ifspec, nullptr, nullptr, RPC_IF_AUTOLISTEN | RPC_IF_ALLOW_LOCAL_ONLY,
                                      RPC_C_LISTEN_MAX_CALLS_DEFAULT, (unsigned int)-1, allow_local,
                                      (void *)g_endpoint_sd);
    if (status)
        SvcLog("dpapi: the rpc server did not start: %u", status);
}
