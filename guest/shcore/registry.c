/* the SH registry helpers: one call over open, query or set and close. */
#include "shcore_private.h"

#define ERROR_KEY_HAS_CHILDREN 1020L
#define SRRF_RT_ANY 0x0000FFFF
#define SRRF_RM_ANY 0x00000000

/* REG_EXPAND_SZ comes back expanded as REG_SZ, sized for the expansion. */
LONG WINAPI SHQueryValueExW(HKEY key, LPCWSTR value, DWORD *reserved, DWORD *type, void *data, DWORD *size)
{
    DWORD kind = 0, bytes = size ? *size : 0;
    LONG error = RegQueryValueExW(key, value, nullptr, &kind, data, size ? &bytes : nullptr);
    if ((error && error != ERROR_MORE_DATA) || kind != REG_EXPAND_SZ || !size) {
        if (type)
            *type = kind;
        if (size)
            *size = bytes;
        return error;
    }
    DWORD raw_size = bytes;
    WCHAR *raw = LocalAlloc(LMEM_FIXED, raw_size + sizeof(WCHAR));
    if (!raw)
        return ERROR_NOT_ENOUGH_MEMORY;
    error = RegQueryValueExW(key, value, nullptr, &kind, (BYTE *)raw, &raw_size);
    if (error) {
        LocalFree(raw);
        return error;
    }
    raw[raw_size / sizeof(WCHAR)] = 0;
    DWORD needed = ExpandEnvironmentStringsW(raw, nullptr, 0) * sizeof(WCHAR);
    DWORD capacity = data ? *size : 0;
    if (data && needed <= capacity)
        ExpandEnvironmentStringsW(raw, data, capacity / sizeof(WCHAR));
    LocalFree(raw);
    if (type)
        *type = REG_SZ;
    *size = needed > raw_size ? needed : raw_size;
    return data && needed > capacity ? ERROR_MORE_DATA : ERROR_SUCCESS;
}

LONG WINAPI SHQueryValueExA(HKEY key, LPCSTR value, DWORD *reserved, DWORD *type, void *data, DWORD *size)
{
    DWORD kind = 0;
    LONG error = RegQueryValueExA(key, value, nullptr, &kind, data, size);
    if (!error && kind == REG_EXPAND_SZ && data && size) {
        char expanded[MAX_PATH * 2];
        DWORD n = ExpandEnvironmentStringsA(data, expanded, sizeof(expanded));
        if (n && n <= *size) {
            memcpy(data, expanded, n);
            *size = n;
        }
        kind = REG_SZ;
    }
    if (type)
        *type = kind;
    return error;
}

LONG WINAPI SHGetValueW(HKEY root, LPCWSTR subkey, LPCWSTR value, DWORD *type, void *data, DWORD *size)
{
    HKEY key;
    LONG error = RegOpenKeyExW(root, subkey, 0, KEY_QUERY_VALUE, &key);
    if (error)
        return error;
    error = SHQueryValueExW(key, value, nullptr, type, data, size);
    RegCloseKey(key);
    return error;
}

LONG WINAPI SHGetValueA(HKEY root, LPCSTR subkey, LPCSTR value, DWORD *type, void *data, DWORD *size)
{
    HKEY key;
    LONG error = RegOpenKeyExA(root, subkey, 0, KEY_QUERY_VALUE, &key);
    if (error)
        return error;
    error = SHQueryValueExA(key, value, nullptr, type, data, size);
    RegCloseKey(key);
    return error;
}

LONG WINAPI SHSetValueW(HKEY root, LPCWSTR subkey, LPCWSTR value, DWORD type, const void *data, DWORD size)
{
    HKEY key;
    LONG error = RegCreateKeyExW(root, subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (error)
        return error;
    error = RegSetValueExW(key, value, 0, type, data, size);
    RegCloseKey(key);
    return error;
}

LONG WINAPI SHSetValueA(HKEY root, LPCSTR subkey, LPCSTR value, DWORD type, const void *data, DWORD size)
{
    HKEY key;
    LONG error = RegCreateKeyExA(root, subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (error)
        return error;
    error = RegSetValueExA(key, value, 0, type, data, size);
    RegCloseKey(key);
    return error;
}

LONG WINAPI SHDeleteValueW(HKEY root, LPCWSTR subkey, LPCWSTR value)
{
    HKEY key;
    LONG error = RegOpenKeyExW(root, subkey, 0, KEY_SET_VALUE, &key);
    if (error)
        return error;
    error = RegDeleteValueW(key, value);
    RegCloseKey(key);
    return error;
}

LONG WINAPI SHDeleteValueA(HKEY root, LPCSTR subkey, LPCSTR value)
{
    HKEY key;
    LONG error = RegOpenKeyExA(root, subkey, 0, KEY_SET_VALUE, &key);
    if (error)
        return error;
    error = RegDeleteValueA(key, value);
    RegCloseKey(key);
    return error;
}

/* the key and everything below it. */
LONG WINAPI SHDeleteKeyW(HKEY root, LPCWSTR subkey)
{
    return RegDeleteTreeW(root, subkey);
}

LONG WINAPI SHDeleteKeyA(HKEY root, LPCSTR subkey)
{
    return RegDeleteTreeA(root, subkey);
}

LONG WINAPI SHDeleteEmptyKeyW(HKEY root, LPCWSTR subkey)
{
    HKEY key;
    LONG error = RegOpenKeyExW(root, subkey, 0, KEY_READ, &key);
    if (error)
        return error;
    DWORD keys = 0, values = 0;
    error = RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &keys, nullptr, nullptr, &values, nullptr, nullptr,
                             nullptr, nullptr);
    RegCloseKey(key);
    if (error)
        return error;
    if (keys || values)
        return ERROR_KEY_HAS_CHILDREN;
    return RegDeleteKeyW(root, subkey ? subkey : L"");
}

LONG WINAPI SHDeleteEmptyKeyA(HKEY root, LPCSTR subkey)
{
    WCHAR wide[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, subkey ? subkey : "", -1, wide, MAX_PATH);
    return SHDeleteEmptyKeyW(root, wide);
}

LONG WINAPI SHEnumKeyExW(HKEY key, DWORD index, LPWSTR name, DWORD *length)
{
    return RegEnumKeyExW(key, index, name, length, nullptr, nullptr, nullptr, nullptr);
}

LONG WINAPI SHEnumKeyExA(HKEY key, DWORD index, LPSTR name, DWORD *length)
{
    return RegEnumKeyExA(key, index, name, length, nullptr, nullptr, nullptr, nullptr);
}

LONG WINAPI SHEnumValueW(HKEY key, DWORD index, LPWSTR name, DWORD *length, DWORD *type, void *data, DWORD *size)
{
    return RegEnumValueW(key, index, name, length, nullptr, type, data, size);
}

LONG WINAPI SHEnumValueA(HKEY key, DWORD index, LPSTR name, DWORD *length, DWORD *type, void *data, DWORD *size)
{
    return RegEnumValueA(key, index, name, length, nullptr, type, data, size);
}

LONG WINAPI SHQueryInfoKeyW(HKEY key, DWORD *subkeys, DWORD *subkey_length, DWORD *values, DWORD *value_length)
{
    return RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, subkeys, subkey_length, nullptr, values, value_length,
                            nullptr, nullptr, nullptr);
}

LONG WINAPI SHQueryInfoKeyA(HKEY key, DWORD *subkeys, DWORD *subkey_length, DWORD *values, DWORD *value_length)
{
    return RegQueryInfoKeyA(key, nullptr, nullptr, nullptr, subkeys, subkey_length, nullptr, values, value_length,
                            nullptr, nullptr, nullptr);
}

LONG WINAPI SHRegGetValueW(HKEY root, LPCWSTR subkey, LPCWSTR value, DWORD flags, DWORD *type, void *data,
                           DWORD *size)
{
    return RegGetValueW(root, subkey, value, flags, type, data, size);
}

LONG WINAPI SHRegGetValueA(HKEY root, LPCSTR subkey, LPCSTR value, DWORD flags, DWORD *type, void *data, DWORD *size)
{
    return RegGetValueA(root, subkey, value, flags, type, data, size);
}

/* the user's value, or the machine's when the user has none. */
LONG WINAPI SHRegGetValueFromHKCUHKLM(LPCWSTR subkey, LPCWSTR value, DWORD flags, DWORD *type, void *data,
                                      DWORD *size)
{
    DWORD saved = size ? *size : 0;
    LONG error = RegGetValueW(HKEY_CURRENT_USER, subkey, value, flags, type, data, size);
    if (error == ERROR_FILE_NOT_FOUND) {
        if (size)
            *size = saved;
        error = RegGetValueW(HKEY_LOCAL_MACHINE, subkey, value, flags, type, data, size);
    }
    return error;
}

/* a number stored as a string or a dword, the fallback otherwise. */
int WINAPI SHRegGetIntW(HKEY key, LPCWSTR value, int fallback)
{
    WCHAR text[32];
    DWORD type, size = sizeof(text);
    if (SHQueryValueExW(key, value, nullptr, &type, text, &size))
        return fallback;
    if (type == REG_DWORD && size >= 4)
        return *(int *)text;
    if (type != REG_SZ)
        return fallback;
    text[31] = 0;
    const WCHAR *p = text;
    BOOL negative = *p == '-';
    if (negative)
        p++;
    if (*p < '0' || *p > '9')
        return fallback;
    int result = 0;
    while (*p >= '0' && *p <= '9')
        result = result * 10 + (*p++ - '0');
    return negative ? -result : result;
}

LONG WINAPI SHRegGetPathW(HKEY root, LPCWSTR subkey, LPCWSTR value, LPWSTR path, DWORD flags)
{
    DWORD size = MAX_PATH * sizeof(WCHAR);
    return SHGetValueW(root, subkey, value, nullptr, path, &size);
}

LONG WINAPI SHRegGetPathA(HKEY root, LPCSTR subkey, LPCSTR value, LPSTR path, DWORD flags)
{
    DWORD size = MAX_PATH;
    return SHGetValueA(root, subkey, value, nullptr, path, &size);
}

LONG WINAPI SHRegSetPathW(HKEY root, LPCWSTR subkey, LPCWSTR value, LPCWSTR path, DWORD flags)
{
    return SHSetValueW(root, subkey, value, REG_EXPAND_SZ, path, (DWORD)(wcslen(path) + 1) * sizeof(WCHAR));
}

LONG WINAPI SHRegSetPathA(HKEY root, LPCSTR subkey, LPCSTR value, LPCSTR path, DWORD flags)
{
    return SHSetValueA(root, subkey, value, REG_EXPAND_SZ, path, (DWORD)strlen(path) + 1);
}

HKEY WINAPI SHRegDuplicateHKey(HKEY key)
{
    HKEY copy = nullptr;
    RegOpenKeyExW(key, nullptr, 0, MAXIMUM_ALLOWED, &copy);
    return copy;
}

/* the values and subkeys of source under destination, recursively. */
static LONG copy_key(HKEY source, HKEY destination)
{
    WCHAR name[256];
    DWORD index = 0, length, type, size;
    LONG error;
    for (;;) {
        length = 256;
        size = 0;
        error = RegEnumValueW(source, index, name, &length, nullptr, &type, nullptr, &size);
        if (error == ERROR_NO_MORE_ITEMS)
            break;
        if (error)
            return error;
        BYTE *data = LocalAlloc(LMEM_FIXED, size ? size : 1);
        if (!data)
            return ERROR_NOT_ENOUGH_MEMORY;
        length = 256;
        error = RegEnumValueW(source, index, name, &length, nullptr, &type, data, &size);
        if (!error)
            error = RegSetValueExW(destination, name, 0, type, data, size);
        LocalFree(data);
        if (error)
            return error;
        index++;
    }
    for (index = 0;; index++) {
        length = 256;
        error = RegEnumKeyExW(source, index, name, &length, nullptr, nullptr, nullptr, nullptr);
        if (error == ERROR_NO_MORE_ITEMS)
            return ERROR_SUCCESS;
        if (error)
            return error;
        HKEY from, to;
        error = RegOpenKeyExW(source, name, 0, KEY_READ, &from);
        if (error)
            return error;
        error = RegCreateKeyExW(destination, name, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &to, nullptr);
        if (!error) {
            error = copy_key(from, to);
            RegCloseKey(to);
        }
        RegCloseKey(from);
        if (error)
            return error;
    }
}

LONG WINAPI SHCopyKeyW(HKEY root, LPCWSTR subkey, HKEY destination, DWORD reserved)
{
    HKEY source = root;
    if (!destination)
        return ERROR_INVALID_PARAMETER;
    if (subkey && *subkey) {
        LONG error = RegOpenKeyExW(root, subkey, 0, KEY_READ, &source);
        if (error)
            return error;
    }
    LONG error = copy_key(source, destination);
    if (source != root)
        RegCloseKey(source);
    return error;
}

LONG WINAPI SHCopyKeyA(HKEY root, LPCSTR subkey, HKEY destination, DWORD reserved)
{
    WCHAR wide[MAX_PATH];
    if (subkey)
        MultiByteToWideChar(CP_ACP, 0, subkey, -1, wide, MAX_PATH);
    return SHCopyKeyW(root, subkey ? wide : nullptr, destination, reserved);
}
