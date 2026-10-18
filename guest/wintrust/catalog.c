/*
 * the catalog administrator: hashes of files as catalogs record them and
 * the lookup of the catalogs holding a hash. mime installs no catalog, so
 * a lookup finds none, as on a machine whose file is signed by no
 * catalog.
 */
#include "trust_private.h"

#define CATADMIN_MAGIC 184
#define HASH_SIZE 20

typedef struct CAT_ADMIN {
    DWORD Magic;
    GUID Subsystem;
} CAT_ADMIN;

/* the subsystem windows tries first without one, the driver signing catalogs. */
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
 * the portable executable hash of authenticode: the image without its
 * checksum, its certificate directory entry and its certificates; any
 * other file is hashed whole.
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
    SetLastError(ERROR_NOT_FOUND);
    return nullptr;
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
    catalog->wszCatalogFile[0] = 0;
    return TRUE;
}
