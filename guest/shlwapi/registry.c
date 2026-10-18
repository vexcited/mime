/*
 * registry helpers shlwapi keeps for itself: the wrappers of old shell code, values read from
 * the user's or the machine's side, ini strings and policies.
 */
#include "shlwapi_private.h"

/* ------------------------------------------------------------------ wrappers */

LONG WINAPI RegCreateKeyExWrapW(HKEY key, LPCWSTR subkey, DWORD reserved, LPWSTR class_name, DWORD options,
                                REGSAM access, SECURITY_ATTRIBUTES *security, HKEY *out, DWORD *disposition);
LONG WINAPI RegCreateKeyExWrapW(HKEY key, LPCWSTR subkey, DWORD reserved, LPWSTR class_name, DWORD options,
                                REGSAM access, SECURITY_ATTRIBUTES *security, HKEY *out, DWORD *disposition)
{
    return RegCreateKeyExW(key, subkey, reserved, class_name, options, access, security, out, disposition);
}

LONG WINAPI RegDeleteKeyWrapW(HKEY key, LPCWSTR subkey);
LONG WINAPI RegDeleteKeyWrapW(HKEY key, LPCWSTR subkey)
{
    return RegDeleteKeyExW(key, subkey, 0, 0);
}

LONG WINAPI RegDeleteValueWrapW(HKEY key, LPCWSTR value);
LONG WINAPI RegDeleteValueWrapW(HKEY key, LPCWSTR value)
{
    return RegDeleteValueW(key, value);
}

LONG WINAPI RegEnumKeyExWrapW(HKEY key, DWORD index, LPWSTR name, DWORD *size, DWORD *reserved, LPWSTR class_name,
                              DWORD *class_size, FILETIME *time);
LONG WINAPI RegEnumKeyExWrapW(HKEY key, DWORD index, LPWSTR name, DWORD *size, DWORD *reserved, LPWSTR class_name,
                              DWORD *class_size, FILETIME *time)
{
    return RegEnumKeyExW(key, index, name, size, reserved, class_name, class_size, time);
}

LONG WINAPI RegEnumValueWrapW(HKEY key, DWORD index, LPWSTR name, DWORD *name_size, DWORD *reserved, DWORD *type,
                              BYTE *data, DWORD *size);
LONG WINAPI RegEnumValueWrapW(HKEY key, DWORD index, LPWSTR name, DWORD *name_size, DWORD *reserved, DWORD *type,
                              BYTE *data, DWORD *size)
{
    return RegEnumValueW(key, index, name, name_size, reserved, type, data, size);
}

LONG WINAPI RegOpenKeyWrapW(HKEY key, LPCWSTR subkey, HKEY *out);
LONG WINAPI RegOpenKeyWrapW(HKEY key, LPCWSTR subkey, HKEY *out)
{
    return RegOpenKeyExW(key, subkey, 0, MAXIMUM_ALLOWED, out);
}

LONG WINAPI RegOpenKeyExWrapW(HKEY key, LPCWSTR subkey, DWORD options, REGSAM access, HKEY *out);
LONG WINAPI RegOpenKeyExWrapW(HKEY key, LPCWSTR subkey, DWORD options, REGSAM access, HKEY *out)
{
    return RegOpenKeyExW(key, subkey, options, access, out);
}

LONG WINAPI RegQueryInfoKeyWrapW(HKEY key, LPWSTR class_name, DWORD *class_size, DWORD *reserved, DWORD *subkeys,
                                 DWORD *subkey_size, DWORD *class_max, DWORD *values, DWORD *value_name_size,
                                 DWORD *value_size, DWORD *security, FILETIME *time);
LONG WINAPI RegQueryInfoKeyWrapW(HKEY key, LPWSTR class_name, DWORD *class_size, DWORD *reserved, DWORD *subkeys,
                                 DWORD *subkey_size, DWORD *class_max, DWORD *values, DWORD *value_name_size,
                                 DWORD *value_size, DWORD *security, FILETIME *time)
{
    return RegQueryInfoKeyW(key, class_name, class_size, reserved, subkeys, subkey_size, class_max, values,
                            value_name_size, value_size, security, time);
}

/* the default value of a subkey, as RegQueryValue reads it. */
LONG WINAPI RegQueryValueWrapW(HKEY key, LPCWSTR subkey, LPWSTR data, LONG *size);
LONG WINAPI RegQueryValueWrapW(HKEY key, LPCWSTR subkey, LPWSTR data, LONG *size)
{
    HKEY sub = key;
    if (subkey && *subkey) {
        LONG error = RegOpenKeyExW(key, subkey, 0, KEY_QUERY_VALUE, &sub);
        if (error)
            return error;
    }
    DWORD bytes = size ? (DWORD)*size : 0;
    LONG error = RegQueryValueExW(sub, nullptr, nullptr, nullptr, (BYTE *)data, size ? &bytes : nullptr);
    if (size)
        *size = (LONG)bytes;
    if (sub != key)
        RegCloseKey(sub);
    return error;
}

LONG WINAPI RegQueryValueExWrapW(HKEY key, LPCWSTR value, DWORD *reserved, DWORD *type, BYTE *data, DWORD *size);
LONG WINAPI RegQueryValueExWrapW(HKEY key, LPCWSTR value, DWORD *reserved, DWORD *type, BYTE *data, DWORD *size)
{
    return RegQueryValueExW(key, value, reserved, type, data, size);
}

LONG WINAPI RegSetValueExWrapW(HKEY key, LPCWSTR value, DWORD reserved, DWORD type, const BYTE *data, DWORD size);
LONG WINAPI RegSetValueExWrapW(HKEY key, LPCWSTR value, DWORD reserved, DWORD type, const BYTE *data, DWORD size)
{
    return RegSetValueExW(key, value, reserved, type, data, size);
}

/* version.dll's functions, loaded when first used. */
DWORD WINAPI GetFileVersionInfoSizeWrapW(LPCWSTR file, DWORD *handle);
DWORD WINAPI GetFileVersionInfoSizeWrapW(LPCWSTR file, DWORD *handle)
{
    DWORD(WINAPI * size)(LPCWSTR, DWORD *) =
        (DWORD(WINAPI *)(LPCWSTR, DWORD *))ShlProc(L"version.dll", "GetFileVersionInfoSizeW");
    return size ? size(file, handle) : 0;
}

BOOL WINAPI GetFileVersionInfoWrapW(LPCWSTR file, DWORD handle, DWORD size, void *data);
BOOL WINAPI GetFileVersionInfoWrapW(LPCWSTR file, DWORD handle, DWORD size, void *data)
{
    BOOL(WINAPI * info)(LPCWSTR, DWORD, DWORD, void *) =
        (BOOL(WINAPI *)(LPCWSTR, DWORD, DWORD, void *))ShlProc(L"version.dll", "GetFileVersionInfoW");
    return info && info(file, handle, size, data);
}

BOOL WINAPI VerQueryValueWrapW(const void *block, LPCWSTR path, void **out, UINT *size);
BOOL WINAPI VerQueryValueWrapW(const void *block, LPCWSTR path, void **out, UINT *size)
{
    BOOL(WINAPI * query)(const void *, LPCWSTR, void **, UINT *) =
        (BOOL(WINAPI *)(const void *, LPCWSTR, void **, UINT *))ShlProc(L"version.dll", "VerQueryValueW");
    return query && query(block, path, out, size);
}

/* ------------------------------------------------------------------ values */

typedef LONG(WINAPI *GET_VALUE_W)(HKEY, LPCWSTR, LPCWSTR, DWORD *, void *, DWORD *);
typedef LONG(WINAPI *GET_VALUE_A)(HKEY, LPCSTR, LPCSTR, DWORD *, void *, DWORD *);

/* SHGetValue, except in a safe mode boot where nothing is read. */
DWORD WINAPI SHGetValueGoodBootW(HKEY key, LPCWSTR subkey, LPCWSTR value, DWORD *type, void *data, DWORD *size);
DWORD WINAPI SHGetValueGoodBootW(HKEY key, LPCWSTR subkey, LPCWSTR value, DWORD *type, void *data, DWORD *size)
{
    if (GetSystemMetrics(SM_CLEANBOOT))
        return ERROR_GEN_FAILURE;
    GET_VALUE_W get = (GET_VALUE_W)ShlProc(L"shcore.dll", "SHGetValueW");
    return get ? (DWORD)get(key, subkey, value, type, data, size) : ERROR_PROC_NOT_FOUND;
}

DWORD WINAPI SHGetValueGoodBootA(HKEY key, LPCSTR subkey, LPCSTR value, DWORD *type, void *data, DWORD *size);
DWORD WINAPI SHGetValueGoodBootA(HKEY key, LPCSTR subkey, LPCSTR value, DWORD *type, void *data, DWORD *size)
{
    if (GetSystemMetrics(SM_CLEANBOOT))
        return ERROR_GEN_FAILURE;
    GET_VALUE_A get = (GET_VALUE_A)ShlProc(L"shcore.dll", "SHGetValueA");
    return get ? (DWORD)get(key, subkey, value, type, data, size) : ERROR_PROC_NOT_FOUND;
}

/* the subkey created when missing, then the value set. */
LONG WINAPI SHRegSetValue(HKEY key, LPCWSTR subkey, LPCWSTR value, DWORD flags, DWORD type, const void *data,
                          DWORD size);
LONG WINAPI SHRegSetValue(HKEY key, LPCWSTR subkey, LPCWSTR value, DWORD flags, DWORD type, const void *data,
                          DWORD size)
{
    HKEY sub = key;
    if (subkey && *subkey) {
        LONG error = RegCreateKeyExW(key, subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &sub, nullptr);
        if (error)
            return error;
    }
    LONG error = RegSetValueExW(sub, value, 0, type, data, size);
    if (sub != key)
        RegCloseKey(sub);
    return error;
}

/* the user's value, else the machine's. */
LONG WINAPI SHRegGetValueFromHKCUHKLM(LPCWSTR subkey, LPCWSTR value, DWORD flags, DWORD *type, void *data,
                                      DWORD *size)
{
    DWORD saved = size ? *size : 0;
    LONG error = RegGetValueW(HKEY_CURRENT_USER, subkey, value, flags, type, data, size);
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
        if (size)
            *size = saved;
        error = RegGetValueW(HKEY_LOCAL_MACHINE, subkey, value, flags, type, data, size);
    }
    return error;
}

/* "yes", "true" and "1" or a nonzero number are TRUE, "no", "false" and "0" FALSE. */
BOOL WINAPI SHRegGetBoolValueFromHKCUHKLM(LPCWSTR subkey, LPCWSTR value, BOOL fallback)
{
    BYTE data[64];
    DWORD type = 0, size = sizeof(data) - sizeof(WCHAR);
    memset(data, 0, sizeof(data));
    if (SHRegGetValueFromHKCUHKLM(subkey, value, SRRF_RT_ANY, &type, data, &size))
        return fallback;
    switch (type) {
    case REG_SZ:
    case REG_EXPAND_SZ: {
        LPCWSTR s = (LPCWSTR)data;
        if (!lstrcmpiW(s, L"yes") || !lstrcmpiW(s, L"true") || !lstrcmpW(s, L"1"))
            return TRUE;
        if (!lstrcmpiW(s, L"no") || !lstrcmpiW(s, L"false") || !lstrcmpW(s, L"0"))
            return FALSE;
        return fallback;
    }
    case REG_DWORD:
    case REG_BINARY:
        for (DWORD i = 0; i < size; i++)
            if (data[i])
                return TRUE;
        return FALSE;
    default:
        return fallback;
    }
}

/* ------------------------------------------------------------------ ini files and policies */

DWORD WINAPI SHGetIniStringW(LPCWSTR section, LPCWSTR key, LPWSTR out, DWORD size, LPCWSTR file);
DWORD WINAPI SHGetIniStringW(LPCWSTR section, LPCWSTR key, LPWSTR out, DWORD size, LPCWSTR file)
{
    return GetPrivateProfileStringW(section, key, L"", out, size, file);
}

BOOL WINAPI SHSetIniStringW(LPCWSTR section, LPCWSTR key, LPCWSTR value, LPCWSTR file);
BOOL WINAPI SHSetIniStringW(LPCWSTR section, LPCWSTR key, LPCWSTR value, LPCWSTR file)
{
    return WritePrivateProfileStringW(section, key, value, file);
}

/* a DWORD of the policies under Software\Microsoft\Windows\CurrentVersion\Policies, the
   machine's winning over the user's; 0 when neither sets it. */
DWORD WINAPI SHGetRestriction(LPCWSTR unused, LPCWSTR subkey, LPCWSTR value);
DWORD WINAPI SHGetRestriction(LPCWSTR unused, LPCWSTR subkey, LPCWSTR value)
{
    WCHAR path[MAX_PATH + 64];
    if (!subkey || !value || wcslen(subkey) >= MAX_PATH)
        return 0;
    wsprintfW(path, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\%s", subkey);
    HKEY roots[2] = {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER};
    for (int i = 0; i < 2; i++) {
        DWORD data = 0, size = sizeof(data);
        if (!RegGetValueW(roots[i], path, value, RRF_RT_REG_DWORD | RRF_RT_REG_BINARY, nullptr, &data, &size))
            return data;
    }
    return 0;
}
