/* catalog file hashes and lookup. Mime installs no catalogs, so lookups find none. */
#include "trust_private.h"

#define CATADMIN_MAGIC 184
#define HASH_SIZE 20

typedef struct CAT_ADMIN {
    DWORD Magic;
    GUID Subsystem;
} CAT_ADMIN;

/* the catalog the hash was found in. Windows keeps one per lookup until it is released. */
typedef struct CAT_INFO {
    WCHAR Path[MAX_PATH];
} CAT_INFO;

/* the driver signing subsystem catalog store, where the system files' catalogs live. */
#define CATROOT L"C:\\Windows\\System32\\CatRoot\\{F750E6C3-38EE-11D1-85E5-00C04FC295EE}\\"

static BOOL catalog_has(HANDLE file, const BYTE *hash, DWORD hash_size)
{
    static BYTE bytes[0x100000];
    DWORD got = 0;
    if (!ReadFile(file, bytes, sizeof(bytes), &got, nullptr))
        return FALSE;
    for (DWORD i = 0; i + 2 + hash_size <= got; i++)
        if (bytes[i] == 4 && bytes[i + 1] == hash_size && !memcmp(bytes + i + 2, hash, hash_size))
            return TRUE;
    return FALSE;
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
static BOOL file_hash(const BYTE *bytes, DWORD size, BYTE *hash)
{
    DWORD length = HASH_SIZE;
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
            BOOL ok = CryptHashCertificate(0, CALG_SHA1, 0, copy, end - 12, hash, &length);
            LocalFree(copy);
            return ok;
        }
    }
    return CryptHashCertificate(0, CALG_SHA1, 0, bytes, size, hash, &length);
}

WINTRUSTAPI BOOL WINAPI CryptCATAdminCalcHashFromFileHandle(HANDLE file, DWORD *hash_size, BYTE *hash, DWORD flags)
{
    if (flags || !file || file == INVALID_HANDLE_VALUE || !hash_size) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!hash || *hash_size < HASH_SIZE) {
        *hash_size = HASH_SIZE;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
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
    BOOL ok = file_hash(bytes ? bytes : (const BYTE *)"", (DWORD)size.QuadPart, hash);
    if (bytes)
        UnmapViewOfFile(bytes);
    if (mapping)
        CloseHandle(mapping);
    if (ok) {
        *hash_size = HASH_SIZE;
        SetLastError(ERROR_SUCCESS);
    }
    return ok;
}

WINTRUSTAPI BOOL WINAPI CryptCATAdminCalcHashFromFileHandle2(HCATADMIN admin, HANDLE file, DWORD *hash_size,
                                                            BYTE *hash, DWORD flags)
{
    if (!valid_admin(admin)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return CryptCATAdminCalcHashFromFileHandle(file, hash_size, hash, flags);
}

WINTRUSTAPI HCATINFO WINAPI CryptCATAdminEnumCatalogFromHash(HCATADMIN admin, BYTE *hash, DWORD hash_size,
                                                            DWORD flags, HCATINFO *previous)
{
    if (!valid_admin(admin) || !hash || !hash_size || flags) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    WCHAR pattern[MAX_PATH];
    _snwprintf(pattern, MAX_PATH, CATROOT L"*.cat");
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) {
        SetLastError(ERROR_NOT_FOUND);
        return nullptr;
    }
    HCATINFO result = nullptr;
    do {
        WCHAR path[MAX_PATH];
        if (_snwprintf(path, MAX_PATH, CATROOT L"%s", found.cFileName) >= MAX_PATH)
            continue;
        HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                  OPEN_EXISTING, 0, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            continue;
        BOOL match = catalog_has(file, hash, hash_size);
        DWORD error = GetLastError();
        CloseHandle(file);
        if (!match) {
            SetLastError(error);
            continue;
        }
        CAT_INFO *info = LocalAlloc(LMEM_FIXED, sizeof(*info));
        if (!info)
            break;
        wcscpy(info->Path, path);
        result = info;
        break;
    } while (FindNextFileW(search, &found));
    FindClose(search);
    if (result) {
        SetLastError(ERROR_SUCCESS);
    } else if (GetLastError() != ERROR_NOT_ENOUGH_MEMORY) {
        SetLastError(ERROR_NOT_FOUND);
    }
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
