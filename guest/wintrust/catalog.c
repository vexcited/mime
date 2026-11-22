/* catalog file hashes and lookup in the catalogs of the system files, which the SystemCatalogs feature installs. */
#include "trust_private.h"
#include <crypto/crypto.h>

#define CATADMIN_MAGIC 184
#define SHA1_SIZE 20
#define SHA256_SIZE 32

/* the hashes of a context are of the algorithm it was acquired for, sha1 when none was named. */
typedef struct CAT_ADMIN {
    DWORD Magic;
    GUID Subsystem;
    DWORD HashSize;
} CAT_ADMIN;

/* the catalog the hash was found in. Windows keeps one per lookup until it is released. */
typedef struct CAT_INFO {
    WCHAR Path[MAX_PATH];
} CAT_INFO;

/* the driver signing subsystem catalog store, where the system files' catalogs live. */
#define CATROOT L"C:\\Windows\\System32\\CatRoot\\{F750E6C3-38EE-11D1-85E5-00C04FC295EE}\\"

/*
 * windows keeps a database of the hashes its catalogs name, so a lookup costs it no read of them. here the catalogs
 * are read once for the process: every octet string of the size of a sha1 or sha256 digest is noted by its first
 * bytes with the catalog and the place it stands at, and a lookup compares the whole digest there. the store is read
 * again when a catalog was added to it.
 */
typedef struct CAT_ENTRY {
    ULONG64 Key;
    DWORD Catalog;
    DWORD Offset;
} CAT_ENTRY;

#define CAT_ENTRY_LIMIT 0x400000

static SRWLOCK g_index_lock = SRWLOCK_INIT;
static CAT_ENTRY *g_entries;
static DWORD g_entry_count;
static WCHAR (*g_catalogs)[MAX_PATH];
static DWORD g_catalog_count;
static FILETIME g_index_stamp;

static BOOL entry_before(const CAT_ENTRY *a, const CAT_ENTRY *b)
{
    return a->Key != b->Key ? a->Key < b->Key : a->Catalog != b->Catalog ? a->Catalog < b->Catalog : a->Offset < b->Offset;
}

static void sift(CAT_ENTRY *e, DWORD at, DWORD count)
{
    for (;;) {
        DWORD child = at * 2 + 1;
        if (child >= count)
            return;
        if (child + 1 < count && entry_before(&e[child], &e[child + 1]))
            child++;
        if (!entry_before(&e[at], &e[child]))
            return;
        CAT_ENTRY t = e[at];
        e[at] = e[child];
        e[child] = t;
        at = child;
    }
}

static void sort_entries(CAT_ENTRY *e, DWORD count)
{
    for (DWORD i = count / 2; i-- > 0;)
        sift(e, i, count);
    for (DWORD i = count; i-- > 1;) {
        CAT_ENTRY t = e[0];
        e[0] = e[i];
        e[i] = t;
        sift(e, 0, i);
    }
}

static BOOL store_stamp(FILETIME *stamp)
{
    WIN32_FILE_ATTRIBUTE_DATA about;
    WCHAR folder[MAX_PATH] = CATROOT;
    folder[wcslen(folder) - 1] = 0;
    if (!GetFileAttributesExW(folder, GetFileExInfoStandard, &about))
        return FALSE;
    *stamp = about.ftLastWriteTime;
    return TRUE;
}

/* called with the lock held. */
static void build_index(void)
{
    if (g_entries)
        VirtualFree(g_entries, 0, MEM_RELEASE);
    if (g_catalogs)
        VirtualFree(g_catalogs, 0, MEM_RELEASE);
    g_entries = nullptr;
    g_catalogs = nullptr;
    g_entry_count = g_catalog_count = 0;
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW(CATROOT L"*.cat", &found);
    if (search == INVALID_HANDLE_VALUE)
        return;
    DWORD room = 0x4000;
    g_entries = VirtualAlloc(nullptr, (SIZE_T)CAT_ENTRY_LIMIT * sizeof(CAT_ENTRY), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    g_catalogs = VirtualAlloc(nullptr, (SIZE_T)room * sizeof(*g_catalogs), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    BYTE *bytes = nullptr;
    DWORD held = 0;
    do {
        if (!g_entries || !g_catalogs || g_catalog_count == room)
            break;
        WCHAR *path = g_catalogs[g_catalog_count];
        if (_snwprintf(path, MAX_PATH, CATROOT L"%s", found.cFileName) >= MAX_PATH)
            continue;
        HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                  OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            continue;
        DWORD size = GetFileSize(file, nullptr), got = 0;
        if (size != INVALID_FILE_SIZE && size > held) {
            if (bytes)
                VirtualFree(bytes, 0, MEM_RELEASE);
            held = (size + 0xFFFFF) & ~0xFFFFFu;
            bytes = VirtualAlloc(nullptr, held, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        }
        if (size != INVALID_FILE_SIZE && bytes && ReadFile(file, bytes, size, &got, nullptr)) {
            for (DWORD i = 0; i + 2 + SHA1_SIZE <= got && g_entry_count < CAT_ENTRY_LIMIT; i++) {
                if (bytes[i] != 4 || (bytes[i + 1] != SHA1_SIZE && bytes[i + 1] != SHA256_SIZE) || i + 2 + bytes[i + 1] > got)
                    continue;
                CAT_ENTRY *e = &g_entries[g_entry_count++];
                memcpy(&e->Key, bytes + i + 2, sizeof(e->Key));
                e->Catalog = g_catalog_count;
                e->Offset = i;
            }
            g_catalog_count++;
        }
        CloseHandle(file);
    } while (FindNextFileW(search, &found));
    FindClose(search);
    if (bytes)
        VirtualFree(bytes, 0, MEM_RELEASE);
    if (g_entries)
        sort_entries(g_entries, g_entry_count);
}

static BOOL catalog_holds(DWORD catalog, DWORD offset, const BYTE *hash, DWORD hash_size)
{
    BYTE there[2 + SHA256_SIZE];
    DWORD got = 0;
    HANDLE file = CreateFileW(g_catalogs[catalog], GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    BOOL ok = SetFilePointer(file, (LONG)offset, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
              ReadFile(file, there, 2 + hash_size, &got, nullptr) && got == 2 + hash_size && there[0] == 4 &&
              there[1] == hash_size && !memcmp(there + 2, hash, hash_size);
    CloseHandle(file);
    return ok;
}

/* the first catalog that names the hash, past the one given when there is one. FALSE when none does. */
static BOOL find_catalog(const BYTE *hash, DWORD hash_size, PCWSTR after, WCHAR path[MAX_PATH])
{
    if (hash_size != SHA1_SIZE && hash_size != SHA256_SIZE)
        return FALSE;
    BOOL found = FALSE;
    FILETIME stamp = {};
    AcquireSRWLockExclusive(&g_index_lock);
    if (!store_stamp(&stamp) || !g_entries || CompareFileTime(&stamp, &g_index_stamp)) {
        build_index();
        g_index_stamp = stamp;
    }
    ULONG64 key;
    memcpy(&key, hash, sizeof(key));
    DWORD low = 0, high = g_entry_count;
    while (low < high) {
        DWORD middle = low + (high - low) / 2;
        if (g_entries[middle].Key < key)
            low = middle + 1;
        else
            high = middle;
    }
    BOOL skipping = after != nullptr;
    DWORD last = ~0u;
    for (DWORD i = low; g_entries && i < g_entry_count && g_entries[i].Key == key && !found; i++) {
        const CAT_ENTRY *e = &g_entries[i];
        if (e->Catalog == last || !catalog_holds(e->Catalog, e->Offset, hash, hash_size))
            continue;
        last = e->Catalog;
        if (skipping) {
            /* the catalogs stand in the order the store lists them, so the one given before ends the part to skip. */
            if (!_wcsicmp(g_catalogs[e->Catalog], after))
                skipping = FALSE;
            continue;
        }
        wcscpy(path, g_catalogs[e->Catalog]);
        found = TRUE;
    }
    ReleaseSRWLockExclusive(&g_index_lock);
    return found;
}

/* defaults to the driver signing catalog subsystem. */
static const GUID g_driver_action = {0xf750e6c3, 0x38ee, 0x11d1, {0x85, 0xe5, 0x00, 0xc0, 0x4f, 0xc2, 0x95, 0xee}};

WINTRUSTAPI BOOL WINAPI CryptCATAdminAcquireContext2(HCATADMIN *admin, const GUID *subsystem, PCWSTR algorithm,
                                                    const void *policy, DWORD flags)
{
    if (!admin || flags || (algorithm && _wcsicmp(algorithm, L"SHA1") && _wcsicmp(algorithm, L"SHA256"))) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    CAT_ADMIN *a = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, sizeof(*a));
    if (!a) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    a->Magic = CATADMIN_MAGIC;
    a->Subsystem = subsystem ? *subsystem : g_driver_action;
    a->HashSize = algorithm && !_wcsicmp(algorithm, L"SHA256") ? SHA256_SIZE : SHA1_SIZE;
    *admin = a;
    return TRUE;
}

WINTRUSTAPI BOOL WINAPI CryptCATAdminAcquireContext(HCATADMIN *admin, const GUID *subsystem, DWORD flags)
{
    return CryptCATAdminAcquireContext2(admin, subsystem, nullptr, nullptr, flags);
}

static BOOL valid_admin(HCATADMIN admin)
{
    return admin && ((CAT_ADMIN *)admin)->Magic == CATADMIN_MAGIC;
}

WINTRUSTAPI BOOL WINAPI CryptCATAdminReleaseContext(HCATADMIN admin, DWORD flags)
{
    if (!valid_admin(admin)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ((CAT_ADMIN *)admin)->Magic = 0;
    LocalFree(admin);
    return TRUE;
}

static WORD word(const BYTE *p)
{
    return (WORD)(p[0] | p[1] << 8);
}

static DWORD dword(const BYTE *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (DWORD)p[3] << 24;
}

/*
 * Authenticode hashes PE images without the checksum, certificate directory entry or certificates. other files are
 * hashed whole.
 */
static BOOL file_hash(const BYTE *bytes, DWORD size, BYTE *hash, DWORD hash_size)
{
    const MimeHashAlg *alg = hash_size == SHA256_SIZE ? &mime_sha256 : &mime_sha1;
    DWORD nt = size >= 64 && word(bytes) == 0x5A4D ? dword(bytes + 60) : 0;
    if (nt && nt <= size - 24 && dword(bytes + nt) == 0x4550) {
        DWORD optional = nt + 24, magic = word(bytes + optional);
        DWORD directories = magic == 0x10B ? 96 : magic == 0x20B ? 112 : 0;
        DWORD optional_size = word(bytes + nt + 20);
        if (directories && optional_size >= directories + 40 && optional + optional_size <= size) {
            DWORD checksum = optional + 64, directory = optional + directories + 32;
            DWORD certificate = dword(bytes + directory), certificate_size = dword(bytes + directory + 4);
            DWORD end = certificate && certificate <= size && certificate_size <= size - certificate ? certificate
                                                                                                   : size;
            if (end < directory + 8)
                end = size;
            BYTE *copy = LocalAlloc(LMEM_FIXED, end);
            if (!copy)
                return FALSE;
            memcpy(copy, bytes, checksum);
            memcpy(copy + checksum, bytes + checksum + 4, directory - checksum - 4);
            memcpy(copy + directory - 4, bytes + directory + 8, end - directory - 8);
            mime_hash(alg, copy, end - 12, hash);
            LocalFree(copy);
            return TRUE;
        }
    }
    mime_hash(alg, bytes, size, hash);
    return TRUE;
}

/*
 * measured on windows 11 26100: with no buffer the size comes back, the call succeeds and the last error says the
 * buffer is too small. a buffer that is too small fails with that error and the size. a context acquired for sha256
 * hashes with it, 32 bytes, and no context or the older call hashes with sha1.
 */
static BOOL calc_hash(HANDLE file, DWORD *hash_size, BYTE *hash, DWORD flags, DWORD needed)
{
    if (flags || !file || file == INVALID_HANDLE_VALUE || !hash_size) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!hash || *hash_size < needed) {
        *hash_size = needed;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return !hash;
    }
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size))
        return FALSE;
    if (size.QuadPart > 0x7FFFFFFF) {
        SetLastError(ERROR_FILE_TOO_LARGE);
        return FALSE;
    }
    HANDLE mapping = size.QuadPart ? CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr) : nullptr;
    const BYTE *bytes = mapping ? MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0) : nullptr;
    if (size.QuadPart && !bytes) {
        DWORD error = GetLastError();
        if (mapping)
            CloseHandle(mapping);
        SetLastError(error);
        return FALSE;
    }
    BOOL ok = file_hash(bytes ? bytes : (const BYTE *)"", (DWORD)size.QuadPart, hash, needed);
    if (bytes)
        UnmapViewOfFile(bytes);
    if (mapping)
        CloseHandle(mapping);
    if (ok) {
        *hash_size = needed;
        SetLastError(ERROR_SUCCESS);
    }
    return ok;
}

WINTRUSTAPI BOOL WINAPI CryptCATAdminCalcHashFromFileHandle(HANDLE file, DWORD *hash_size, BYTE *hash, DWORD flags)
{
    return calc_hash(file, hash_size, hash, flags, SHA1_SIZE);
}

WINTRUSTAPI BOOL WINAPI CryptCATAdminCalcHashFromFileHandle2(HCATADMIN admin, HANDLE file, DWORD *hash_size,
                                                            BYTE *hash, DWORD flags)
{
    if (!valid_admin(admin)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return calc_hash(file, hash_size, hash, flags, ((CAT_ADMIN *)admin)->HashSize);
}

WINTRUSTAPI HCATINFO WINAPI CryptCATAdminEnumCatalogFromHash(HCATADMIN admin, BYTE *hash, DWORD hash_size,
                                                            DWORD flags, HCATINFO *previous)
{
    if (!valid_admin(admin) || !hash || !hash_size || flags) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    /* a lookup that is given the catalog found before goes on after it and releases it, as windows does. */
    CAT_INFO *after = previous ? *previous : nullptr;
    HCATINFO result = nullptr;
    WCHAR path[MAX_PATH];
    BOOL found = find_catalog(hash, hash_size, after ? after->Path : nullptr, path);
    DWORD error = ERROR_NOT_FOUND;
    if (found) {
        CAT_INFO *info = LocalAlloc(LMEM_FIXED, sizeof(*info));
        if (info) {
            wcscpy(info->Path, path);
            result = info;
        } else {
            error = ERROR_NOT_ENOUGH_MEMORY;
        }
    }
    if (after) {
        LocalFree(after);
        *previous = nullptr;
    }
    SetLastError(result ? ERROR_SUCCESS : error);
    return result;
}

WINTRUSTAPI BOOL WINAPI CryptCATAdminReleaseCatalogContext(HCATADMIN admin, HCATINFO info, DWORD flags)
{
    if (!valid_admin(admin) || !info) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    LocalFree(info);
    return TRUE;
}

WINTRUSTAPI BOOL WINAPI CryptCATCatalogInfoFromContext(HCATINFO info, CATALOG_INFO *catalog, DWORD flags)
{
    if (!info || !catalog) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    catalog->cbStruct = sizeof(*catalog);
    wcscpy(catalog->wszCatalogFile, ((CAT_INFO *)info)->Path);
    return TRUE;
}
