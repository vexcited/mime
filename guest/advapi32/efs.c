/*
 * encrypting file system calls. mime's volumes have no encryption, so decrypting succeeds without doing anything,
 * the status reports an unsupported volume, and encrypting or opening a file raw is refused.
 */
#include "advapi32_private.h"

/* FileEncryptionStatus results. */
#define FILE_SYSTEM_NOT_SUPPORT 6

/* the service looks the file up first, so it must exist. */
static BOOL file_exists(LPCWSTR name)
{
    if (!name) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    DWORD attributes = GetFileAttributesW(name);
    return attributes != INVALID_FILE_ATTRIBUTES;
}

ADVAPI BOOL WINAPI DecryptFileW(LPCWSTR name, DWORD reserved)
{
    if (reserved) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return file_exists(name);
}

ADVAPI BOOL WINAPI DecryptFileA(LPCSTR name, DWORD reserved)
{
    PWSTR wide = name ? AdvAnsiToWide(name) : nullptr;
    if (name && !wide)
        return FALSE;
    BOOL ok = DecryptFileW(wide, reserved);
    DWORD error = GetLastError();
    AdvFree(wide);
    SetLastError(error);
    return ok;
}

ADVAPI BOOL WINAPI EncryptFileW(LPCWSTR name)
{
    if (!file_exists(name))
        return FALSE;
    AdvSetError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

ADVAPI BOOL WINAPI EncryptFileA(LPCSTR name)
{
    PWSTR wide = name ? AdvAnsiToWide(name) : nullptr;
    if (name && !wide)
        return FALSE;
    BOOL ok = EncryptFileW(wide);
    DWORD error = GetLastError();
    AdvFree(wide);
    SetLastError(error);
    return ok;
}

ADVAPI BOOL WINAPI FileEncryptionStatusW(LPCWSTR name, LPDWORD status)
{
    if (!status) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!file_exists(name))
        return FALSE;
    *status = FILE_SYSTEM_NOT_SUPPORT;
    return TRUE;
}

ADVAPI BOOL WINAPI FileEncryptionStatusA(LPCSTR name, LPDWORD status)
{
    PWSTR wide = name ? AdvAnsiToWide(name) : nullptr;
    if (name && !wide)
        return FALSE;
    BOOL ok = FileEncryptionStatusW(wide, status);
    DWORD error = GetLastError();
    AdvFree(wide);
    SetLastError(error);
    return ok;
}

ADVAPI BOOL WINAPI EncryptionDisable(LPCWSTR directory, BOOL disable)
{
    if (!file_exists(directory))
        return FALSE;
    AdvSetError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

ADVAPI DWORD WINAPI OpenEncryptedFileRawW(LPCWSTR name, ULONG flags, PVOID *context)
{
    if (context)
        *context = nullptr;
    return ERROR_NOT_SUPPORTED;
}

ADVAPI DWORD WINAPI OpenEncryptedFileRawA(LPCSTR name, ULONG flags, PVOID *context)
{
    if (context)
        *context = nullptr;
    return ERROR_NOT_SUPPORTED;
}
