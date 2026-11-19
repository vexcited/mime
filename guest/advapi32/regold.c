#include "advapi32_private.h"

/*
 * the registry functions windows keeps in advapi32 itself, which mime's kernelbase also exported. the kernelbase of a
 * 32-bit process is microsoft's and has only the extended forms, so the 32-bit advapi32 has them here. the behaviour
 * is that of guest/kernelbase/registry.c.
 */

ADVAPI LSTATUS WINAPI RegOpenKeyW(HKEY key, LPCWSTR subkey, HKEY *result)
{
    if (!result)
        return ERROR_INVALID_PARAMETER;
    if (!subkey || !*subkey) {
        *result = key;
        return ERROR_SUCCESS;
    }
    return RegOpenKeyExW(key, subkey, 0, MAXIMUM_ALLOWED, result);
}

ADVAPI LSTATUS WINAPI RegOpenKeyA(HKEY key, LPCSTR subkey, HKEY *result)
{
    if (!result)
        return ERROR_INVALID_PARAMETER;
    if (!subkey || !*subkey) {
        *result = key;
        return ERROR_SUCCESS;
    }
    return RegOpenKeyExA(key, subkey, 0, MAXIMUM_ALLOWED, result);
}

ADVAPI LSTATUS WINAPI RegCreateKeyW(HKEY key, LPCWSTR subkey, HKEY *result)
{
    if (!result)
        return ERROR_INVALID_PARAMETER;
    if (!subkey || !*subkey) {
        *result = key;
        return ERROR_SUCCESS;
    }
    return RegCreateKeyExW(key, subkey, 0, nullptr, 0, MAXIMUM_ALLOWED, nullptr, result, nullptr);
}

ADVAPI LSTATUS WINAPI RegCreateKeyA(HKEY key, LPCSTR subkey, HKEY *result)
{
    if (!result)
        return ERROR_INVALID_PARAMETER;
    if (!subkey || !*subkey) {
        *result = key;
        return ERROR_SUCCESS;
    }
    return RegCreateKeyExA(key, subkey, 0, nullptr, 0, MAXIMUM_ALLOWED, nullptr, result, nullptr);
}

ADVAPI LSTATUS WINAPI RegDeleteKeyW(HKEY key, LPCWSTR subkey)
{
    return RegDeleteKeyExW(key, subkey, 0, 0);
}

ADVAPI LSTATUS WINAPI RegDeleteKeyA(HKEY key, LPCSTR subkey)
{
    return RegDeleteKeyExA(key, subkey, 0, 0);
}

ADVAPI LSTATUS WINAPI RegEnumKeyW(HKEY key, DWORD index, LPWSTR name, DWORD size)
{
    DWORD n = size;
    return RegEnumKeyExW(key, index, name, &n, nullptr, nullptr, nullptr, nullptr);
}

ADVAPI LSTATUS WINAPI RegEnumKeyA(HKEY key, DWORD index, LPSTR name, DWORD size)
{
    DWORD n = size;
    return RegEnumKeyExA(key, index, name, &n, nullptr, nullptr, nullptr, nullptr);
}

ADVAPI LSTATUS WINAPI RegCopyTreeA(HKEY source, LPCSTR subkey, HKEY destination)
{
    PWSTR wide = subkey ? AdvAnsiToWide(subkey) : nullptr;
    if (subkey && !wide)
        return ERROR_NOT_ENOUGH_MEMORY;
    LSTATUS error = RegCopyTreeW(source, wide, destination);
    AdvFree(wide);
    return error;
}

ADVAPI LSTATUS WINAPI RegSaveKeyW(HKEY key, LPCWSTR file, LPSECURITY_ATTRIBUTES attributes)
{
    return RegSaveKeyExW(key, file, attributes, 1);
}

ADVAPI LSTATUS WINAPI RegSaveKeyA(HKEY key, LPCSTR file, LPSECURITY_ATTRIBUTES attributes)
{
    return RegSaveKeyExA(key, file, attributes, 1);
}

/* there are no hive files here: replacing one needs a privilege no token holds. */
ADVAPI LSTATUS WINAPI RegReplaceKeyW(HKEY key, LPCWSTR subkey, LPCWSTR new_file, LPCWSTR old_file)
{
    if (key == HKEY_PERFORMANCE_DATA)
        return ERROR_INVALID_HANDLE;
    if (!new_file || !old_file)
        return ERROR_INVALID_PARAMETER;
    return ERROR_PRIVILEGE_NOT_HELD;
}

ADVAPI LSTATUS WINAPI RegReplaceKeyA(HKEY key, LPCSTR subkey, LPCSTR new_file, LPCSTR old_file)
{
    PWSTR sub = subkey ? AdvAnsiToWide(subkey) : nullptr, new_wide = new_file ? AdvAnsiToWide(new_file) : nullptr,
          old_wide = old_file ? AdvAnsiToWide(old_file) : nullptr;
    LSTATUS error = ERROR_NOT_ENOUGH_MEMORY;
    if ((!subkey || sub) && (!new_file || new_wide) && (!old_file || old_wide))
        error = RegReplaceKeyW(key, sub, new_wide, old_wide);
    AdvFree(sub);
    AdvFree(new_wide);
    AdvFree(old_wide);
    return error;
}
