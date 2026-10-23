/*
 * user specific registry keys: one handle over the same path under HKEY_CURRENT_USER and HKEY_LOCAL_MACHINE, read
 * from the user's side first and written where the flags say.
 */
#include "shlwapi_kb.h"

typedef struct {
    HKEY user, machine;
    REGSAM access;
    WCHAR path[MAX_PATH];
} US_KEY;

static US_KEY *us_key(HUSKEY key)
{
    return (US_KEY *)key;
}

static LONG join_path(LPWSTR out, LPCWSTR base, LPCWSTR path)
{
    out[0] = 0;
    if (base && *base && FAILED(KbShCopy(out, MAX_PATH, base)))
        return ERROR_INVALID_PARAMETER;
    if (path && *path) {
        SIZE_T length = wcslen(out);
        if (length) {
            if (length + 1 >= MAX_PATH)
                return ERROR_INVALID_PARAMETER;
            out[length++] = L'\\';
            out[length] = 0;
        }
        if (FAILED(KbShCopy(out + length, MAX_PATH - length, path)))
            return ERROR_INVALID_PARAMETER;
    }
    return ERROR_SUCCESS;
}

/* opens or creates one side of the key on demand. */
static HKEY side(US_KEY *k, BOOL user, BOOL create)
{
    HKEY *slot = user ? &k->user : &k->machine;
    if (*slot)
        return *slot;
    HKEY root = user ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
    LONG error = create ? RegCreateKeyExW(root, k->path, 0, nullptr, 0, k->access | KEY_SET_VALUE | KEY_QUERY_VALUE,
                                          nullptr, slot, nullptr)
                        : RegOpenKeyExW(root, k->path, 0, k->access, slot);
    if (error)
        *slot = nullptr;
    return *slot;
}

static LONG open_us(LPCWSTR path, REGSAM access, HUSKEY relative, PHUSKEY out, BOOL ignore_user, BOOL create,
                    DWORD flags)
{
    if (!out)
        return ERROR_INVALID_PARAMETER;
    *out = nullptr;
    US_KEY *k = KbAllocZero(sizeof(*k));
    if (!k)
        return ERROR_NOT_ENOUGH_MEMORY;
    k->access = access;
    LONG error = join_path(k->path, relative ? us_key(relative)->path : nullptr, path);
    if (error) {
        KbFree(k);
        return error;
    }
    if (create) {
        BOOL user = (flags & (SHREGSET_HKCU | SHREGSET_FORCE_HKCU)) != 0;
        BOOL machine = (flags & (SHREGSET_HKLM | SHREGSET_FORCE_HKLM)) != 0;
        if (!user && !machine)
            user = TRUE;
        error = ERROR_SUCCESS;
        if (user && !side(k, TRUE, TRUE))
            error = ERROR_ACCESS_DENIED;
        if (machine && !side(k, FALSE, TRUE) && !k->user)
            error = ERROR_ACCESS_DENIED;
    } else {
        if (!ignore_user)
            side(k, TRUE, FALSE);
        error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, k->path, 0, access, &k->machine);
        if (error)
            k->machine = nullptr;
        if (k->user)
            error = ERROR_SUCCESS;
    }
    if (error) {
        if (k->user)
            RegCloseKey(k->user);
        if (k->machine)
            RegCloseKey(k->machine);
        KbFree(k);
        return error;
    }
    *out = k;
    return ERROR_SUCCESS;
}

LWSTDAPI LONG WINAPI SHRegOpenUSKeyW(LPCWSTR path, REGSAM access, HUSKEY relative, PHUSKEY key, BOOL ignore_hkcu)
{
    return open_us(path, access, relative, key, ignore_hkcu, FALSE, 0);
}

LWSTDAPI LONG WINAPI SHRegCreateUSKeyW(LPCWSTR path, REGSAM access, HUSKEY relative, PHUSKEY key, DWORD flags)
{
    return open_us(path, access, relative, key, FALSE, TRUE, flags);
}

/* the ansi forms widen their strings and call the wide ones. */
static PWSTR widen(LPCSTR s, LONG *error)
{
    if (!s)
        return nullptr;
    PWSTR w = KbShAnsiToWide(s);
    if (!w)
        *error = ERROR_NOT_ENOUGH_MEMORY;
    return w;
}

LWSTDAPI LONG WINAPI SHRegOpenUSKeyA(LPCSTR path, REGSAM access, HUSKEY relative, PHUSKEY key, BOOL ignore_hkcu)
{
    LONG error = ERROR_SUCCESS;
    PWSTR w = widen(path, &error);
    if (!error)
        error = SHRegOpenUSKeyW(w, access, relative, key, ignore_hkcu);
    KbFree(w);
    return error;
}

LWSTDAPI LONG WINAPI SHRegCreateUSKeyA(LPCSTR path, REGSAM access, HUSKEY relative, PHUSKEY key, DWORD flags)
{
    LONG error = ERROR_SUCCESS;
    PWSTR w = widen(path, &error);
    if (!error)
        error = SHRegCreateUSKeyW(w, access, relative, key, flags);
    KbFree(w);
    return error;
}

LWSTDAPI LONG WINAPI SHRegCloseUSKey(HUSKEY key)
{
    US_KEY *k = us_key(key);
    if (!k)
        return ERROR_INVALID_PARAMETER;
    LONG error = ERROR_SUCCESS;
    if (k->user)
        error = RegCloseKey(k->user);
    if (k->machine) {
        LONG e = RegCloseKey(k->machine);
        if (!error)
            error = e;
    }
    KbFree(k);
    return error;
}

/* the user's value, else the machine's, else the fallback data with success. */
LWSTDAPI LONG WINAPI SHRegQueryUSValueW(HUSKEY key, LPCWSTR value, DWORD *type, void *data, DWORD *size,
                                        BOOL ignore_hkcu, void *fallback, DWORD fallback_size)
{
    US_KEY *k = us_key(key);
    LONG error = ERROR_INVALID_PARAMETER;
    if (k) {
        error = ERROR_FILE_NOT_FOUND;
        if (!ignore_hkcu && k->user)
            error = RegQueryValueExW(k->user, value, nullptr, type, data, size);
        if (error && error != ERROR_MORE_DATA && k->machine)
            error = RegQueryValueExW(k->machine, value, nullptr, type, data, size);
    }
    if (error && error != ERROR_MORE_DATA && fallback && fallback_size && size) {
        DWORD n = fallback_size < *size ? fallback_size : *size;
        if (data)
            memcpy(data, fallback, n);
        *size = n;
        error = ERROR_SUCCESS;
    }
    return error;
}

/* for the ansi forms: narrows string data back into the caller's buffer. */
static LONG narrow_value(LONG error, DWORD type, void *data, DWORD *size, const void *wide, DWORD wide_size)
{
    if (error)
        return error;
    if (type != REG_SZ && type != REG_EXPAND_SZ && type != REG_MULTI_SZ) {
        if (data) {
            if (wide_size > *size) {
                *size = wide_size;
                return ERROR_MORE_DATA;
            }
            memcpy(data, wide, wide_size);
        }
        *size = wide_size;
        return ERROR_SUCCESS;
    }
    int chars = (int)(wide_size / sizeof(WCHAR));
    int bytes = WideCharToMultiByte(CP_ACP, 0, wide, chars, nullptr, 0, nullptr, nullptr);
    if (data) {
        if ((DWORD)bytes > *size) {
            *size = (DWORD)bytes;
            return ERROR_MORE_DATA;
        }
        WideCharToMultiByte(CP_ACP, 0, wide, chars, data, bytes, nullptr, nullptr);
    }
    *size = (DWORD)bytes;
    return ERROR_SUCCESS;
}

static LONG query_a(HUSKEY key, LPCWSTR value, DWORD *type, void *data, DWORD *size, BOOL ignore_hkcu,
                    void *fallback, DWORD fallback_size)
{
    DWORD wide_size = 0, value_type = 0;
    LONG error = SHRegQueryUSValueW(key, value, &value_type, nullptr, &wide_size, ignore_hkcu, nullptr, 0);
    void *wide = nullptr;
    if (!error) {
        wide = KbAlloc(wide_size + sizeof(WCHAR));
        if (!wide)
            return ERROR_NOT_ENOUGH_MEMORY;
        error = SHRegQueryUSValueW(key, value, &value_type, wide, &wide_size, ignore_hkcu, nullptr, 0);
    }
    if (!error) {
        if (type)
            *type = value_type;
        if (size)
            error = narrow_value(error, value_type, data, size, wide, wide_size);
    } else if (fallback && fallback_size && size) {
        DWORD n = fallback_size < *size ? fallback_size : *size;
        if (data)
            memcpy(data, fallback, n);
        *size = n;
        error = ERROR_SUCCESS;
    }
    KbFree(wide);
    return error;
}

LWSTDAPI LONG WINAPI SHRegQueryUSValueA(HUSKEY key, LPCSTR value, DWORD *type, void *data, DWORD *size,
                                        BOOL ignore_hkcu, void *fallback, DWORD fallback_size)
{
    LONG error = ERROR_SUCCESS;
    PWSTR w = widen(value, &error);
    if (!error)
        error = query_a(key, w, type, data, size, ignore_hkcu, fallback, fallback_size);
    KbFree(w);
    return error;
}

/* SHREGSET_HKCU and SHREGSET_HKLM only write a value not there yet, their FORCE forms always. */
LWSTDAPI LONG WINAPI SHRegWriteUSValueW(HUSKEY key, LPCWSTR value, DWORD type, const void *data, DWORD size,
                                        DWORD flags)
{
    US_KEY *k = us_key(key);
    if (!k || !(flags & (SHREGSET_HKCU | SHREGSET_FORCE_HKCU | SHREGSET_HKLM | SHREGSET_FORCE_HKLM)))
        return ERROR_INVALID_PARAMETER;
    LONG error = ERROR_SUCCESS;
    for (int user = 1; user >= 0; user--) {
        DWORD force = user ? SHREGSET_FORCE_HKCU : SHREGSET_FORCE_HKLM;
        DWORD soft = user ? SHREGSET_HKCU : SHREGSET_HKLM;
        if (!(flags & (force | soft)))
            continue;
        HKEY h = side(k, user, TRUE);
        if (!h) {
            error = ERROR_ACCESS_DENIED;
            continue;
        }
        if (!(flags & force) && !RegQueryValueExW(h, value, nullptr, nullptr, nullptr, nullptr))
            continue;
        LONG e = RegSetValueExW(h, value, 0, type, data, size);
        if (e)
            error = e;
    }
    return error;
}

LWSTDAPI LONG WINAPI SHRegWriteUSValueA(HUSKEY key, LPCSTR value, DWORD type, const void *data, DWORD size,
                                        DWORD flags)
{
    LONG error = ERROR_SUCCESS;
    PWSTR w = widen(value, &error);
    if (error)
        return error;
    if (type == REG_SZ || type == REG_EXPAND_SZ || type == REG_MULTI_SZ) {
        int chars = MultiByteToWideChar(CP_ACP, 0, data, (int)size, nullptr, 0);
        PWSTR wide = KbAlloc(((SIZE_T)chars + 1) * sizeof(WCHAR));
        if (!wide) {
            error = ERROR_NOT_ENOUGH_MEMORY;
        } else {
            MultiByteToWideChar(CP_ACP, 0, data, (int)size, wide, chars);
            error = SHRegWriteUSValueW(key, w, type, wide, (DWORD)chars * sizeof(WCHAR), flags);
            KbFree(wide);
        }
    } else {
        error = SHRegWriteUSValueW(key, w, type, data, size, flags);
    }
    KbFree(w);
    return error;
}

/* the sides a delete or enumeration uses, by default the user's when it is open. */
static HKEY pick(US_KEY *k, DWORD flags, BOOL *both)
{
    *both = FALSE;
    switch (flags) {
    case SHREGDEL_HKCU:
        return k->user;
    case SHREGDEL_HKLM:
        return k->machine;
    case SHREGDEL_BOTH:
        *both = TRUE;
        return k->user;
    default:
        return k->user ? k->user : k->machine;
    }
}

LWSTDAPI LONG WINAPI SHRegDeleteUSValueW(HUSKEY key, LPCWSTR value, SHREGDEL_FLAGS flags)
{
    US_KEY *k = us_key(key);
    if (!k)
        return ERROR_INVALID_PARAMETER;
    BOOL both;
    HKEY h = pick(k, flags, &both);
    LONG error = h ? RegDeleteValueW(h, value) : ERROR_FILE_NOT_FOUND;
    if (both) {
        LONG e = k->machine ? RegDeleteValueW(k->machine, value) : ERROR_FILE_NOT_FOUND;
        if (error)
            error = e;
    }
    return error;
}

LWSTDAPI LONG WINAPI SHRegDeleteUSValueA(HUSKEY key, LPCSTR value, SHREGDEL_FLAGS flags)
{
    LONG error = ERROR_SUCCESS;
    PWSTR w = widen(value, &error);
    if (!error)
        error = SHRegDeleteUSValueW(key, w, flags);
    KbFree(w);
    return error;
}

/* only removes a subkey that has neither subkeys nor values. */
static LONG delete_empty(HKEY h, LPCWSTR subkey)
{
    if (!h)
        return ERROR_FILE_NOT_FOUND;
    HKEY child;
    LONG error = RegOpenKeyExW(h, subkey, 0, KEY_READ, &child);
    if (error)
        return error;
    DWORD subkeys = 0, values = 0;
    error = RegQueryInfoKeyW(child, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values, nullptr, nullptr,
                             nullptr, nullptr);
    RegCloseKey(child);
    if (error)
        return error;
    if (subkeys || values)
        return ERROR_KEY_HAS_CHILDREN;
    return RegDeleteKeyW(h, subkey);
}

LWSTDAPI LONG WINAPI SHRegDeleteEmptyUSKeyW(HUSKEY key, LPCWSTR subkey, SHREGDEL_FLAGS flags)
{
    US_KEY *k = us_key(key);
    if (!k)
        return ERROR_INVALID_PARAMETER;
    BOOL both;
    HKEY h = pick(k, flags, &both);
    LONG error = delete_empty(h, subkey);
    if (both) {
        LONG e = delete_empty(k->machine, subkey);
        if (error)
            error = e;
    }
    return error;
}

LWSTDAPI LONG WINAPI SHRegDeleteEmptyUSKeyA(HUSKEY key, LPCSTR subkey, SHREGDEL_FLAGS flags)
{
    LONG error = ERROR_SUCCESS;
    PWSTR w = widen(subkey, &error);
    if (!error)
        error = SHRegDeleteEmptyUSKeyW(key, w, flags);
    KbFree(w);
    return error;
}

/* enumerations use one side, windows never does both at once either. */
static HKEY enum_side(HUSKEY key, SHREGENUM_FLAGS flags, LONG *error)
{
    US_KEY *k = us_key(key);
    *error = ERROR_SUCCESS;
    if (!k) {
        *error = ERROR_INVALID_PARAMETER;
        return nullptr;
    }
    if (flags == SHREGENUM_BOTH) {
        *error = ERROR_INVALID_FUNCTION;
        return nullptr;
    }
    BOOL both;
    HKEY h = pick(k, flags, &both);
    if (!h)
        *error = ERROR_INVALID_FUNCTION;
    return h;
}

LWSTDAPI LONG WINAPI SHRegEnumUSKeyW(HUSKEY key, DWORD index, LPWSTR name, DWORD *size, SHREGENUM_FLAGS flags)
{
    LONG error;
    HKEY h = enum_side(key, flags, &error);
    return h ? RegEnumKeyExW(h, index, name, size, nullptr, nullptr, nullptr, nullptr) : error;
}

LWSTDAPI LONG WINAPI SHRegEnumUSKeyA(HUSKEY key, DWORD index, LPSTR name, DWORD *size, SHREGENUM_FLAGS flags)
{
    LONG error;
    HKEY h = enum_side(key, flags, &error);
    return h ? RegEnumKeyExA(h, index, name, size, nullptr, nullptr, nullptr, nullptr) : error;
}

LWSTDAPI LONG WINAPI SHRegEnumUSValueW(HUSKEY key, DWORD index, LPWSTR name, DWORD *name_size, DWORD *type, void *data,
                                       DWORD *size, SHREGENUM_FLAGS flags)
{
    LONG error;
    HKEY h = enum_side(key, flags, &error);
    return h ? RegEnumValueW(h, index, name, name_size, nullptr, type, data, size) : error;
}

LWSTDAPI LONG WINAPI SHRegEnumUSValueA(HUSKEY key, DWORD index, LPSTR name, DWORD *name_size, DWORD *type, void *data,
                                       DWORD *size, SHREGENUM_FLAGS flags)
{
    LONG error;
    HKEY h = enum_side(key, flags, &error);
    return h ? RegEnumValueA(h, index, name, name_size, nullptr, type, data, size) : error;
}

LWSTDAPI LONG WINAPI SHRegQueryInfoUSKeyW(HUSKEY key, DWORD *subkeys, DWORD *subkey_size, DWORD *values,
                                          DWORD *value_size, SHREGENUM_FLAGS flags)
{
    LONG error;
    HKEY h = enum_side(key, flags, &error);
    return h ? RegQueryInfoKeyW(h, nullptr, nullptr, nullptr, subkeys, subkey_size, nullptr, values, value_size,
                                nullptr, nullptr, nullptr)
             : error;
}

LWSTDAPI LONG WINAPI SHRegQueryInfoUSKeyA(HUSKEY key, DWORD *subkeys, DWORD *subkey_size, DWORD *values,
                                          DWORD *value_size, SHREGENUM_FLAGS flags)
{
    LONG error;
    HKEY h = enum_side(key, flags, &error);
    return h ? RegQueryInfoKeyA(h, nullptr, nullptr, nullptr, subkeys, subkey_size, nullptr, values, value_size,
                                nullptr, nullptr, nullptr)
             : error;
}

/* the fallback also covers a missing key. */
LWSTDAPI LONG WINAPI SHRegGetUSValueW(LPCWSTR subkey, LPCWSTR value, DWORD *type, void *data, DWORD *size,
                                      BOOL ignore_hkcu, void *fallback, DWORD fallback_size)
{
    if (!data || !size)
        return ERROR_INVALID_FUNCTION;
    HUSKEY key;
    LONG error = SHRegOpenUSKeyW(subkey, KEY_QUERY_VALUE, nullptr, &key, ignore_hkcu);
    if (!error) {
        error = SHRegQueryUSValueW(key, value, type, data, size, ignore_hkcu, fallback, fallback_size);
        SHRegCloseUSKey(key);
    } else if (fallback && fallback_size) {
        DWORD n = fallback_size < *size ? fallback_size : *size;
        memcpy(data, fallback, n);
        *size = n;
        error = ERROR_SUCCESS;
    }
    return error;
}

LWSTDAPI LONG WINAPI SHRegGetUSValueA(LPCSTR subkey, LPCSTR value, DWORD *type, void *data, DWORD *size,
                                      BOOL ignore_hkcu, void *fallback, DWORD fallback_size)
{
    if (!data || !size)
        return ERROR_INVALID_FUNCTION;
    LONG error = ERROR_SUCCESS;
    PWSTR k = widen(subkey, &error);
    PWSTR v = widen(value, &error);
    HUSKEY key = nullptr;
    if (!error)
        error = SHRegOpenUSKeyW(k, KEY_QUERY_VALUE, nullptr, &key, ignore_hkcu);
    if (!error) {
        error = query_a(key, v, type, data, size, ignore_hkcu, fallback, fallback_size);
        SHRegCloseUSKey(key);
    } else if (error != ERROR_NOT_ENOUGH_MEMORY && fallback && fallback_size) {
        DWORD n = fallback_size < *size ? fallback_size : *size;
        memcpy(data, fallback, n);
        *size = n;
        error = ERROR_SUCCESS;
    }
    KbFree(k);
    KbFree(v);
    return error;
}

LWSTDAPI LONG WINAPI SHRegSetUSValueW(LPCWSTR subkey, LPCWSTR value, DWORD type, const void *data, DWORD size,
                                      DWORD flags)
{
    HUSKEY key;
    LONG error = SHRegCreateUSKeyW(subkey, KEY_ALL_ACCESS, nullptr, &key, flags);
    if (!error) {
        error = SHRegWriteUSValueW(key, value, type, data, size, flags);
        SHRegCloseUSKey(key);
    }
    return error;
}

LWSTDAPI LONG WINAPI SHRegSetUSValueA(LPCSTR subkey, LPCSTR value, DWORD type, const void *data, DWORD size,
                                      DWORD flags)
{
    LONG error = ERROR_SUCCESS;
    PWSTR k = widen(subkey, &error);
    HUSKEY key = nullptr;
    if (!error)
        error = SHRegCreateUSKeyW(k, KEY_ALL_ACCESS, nullptr, &key, flags);
    if (!error) {
        error = SHRegWriteUSValueA(key, value, type, data, size, flags);
        SHRegCloseUSKey(key);
    }
    KbFree(k);
    return error;
}

/* "yes", "true", "1" or a nonzero number are TRUE, "no", "false" and "0" are FALSE, anything else
   gives the fallback. */
LWSTDAPI BOOL WINAPI SHRegGetBoolUSValueW(LPCWSTR subkey, LPCWSTR value, BOOL ignore_hkcu, BOOL fallback)
{
    BYTE data[64];
    DWORD type = 0, size = sizeof(data) - sizeof(WCHAR);
    memset(data, 0, sizeof(data));
    if (SHRegGetUSValueW(subkey, value, &type, data, &size, ignore_hkcu, nullptr, 0))
        return fallback;
    switch (type) {
    case REG_SZ:
    case REG_EXPAND_SZ: {
        LPCWSTR s = (LPCWSTR)data;
        if (!_wcsicmp(s, L"yes") || !_wcsicmp(s, L"true") || !wcscmp(s, L"1"))
            return TRUE;
        if (!_wcsicmp(s, L"no") || !_wcsicmp(s, L"false") || !wcscmp(s, L"0"))
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

LWSTDAPI BOOL WINAPI SHRegGetBoolUSValueA(LPCSTR subkey, LPCSTR value, BOOL ignore_hkcu, BOOL fallback)
{
    LONG error = ERROR_SUCCESS;
    PWSTR k = widen(subkey, &error);
    PWSTR v = widen(value, &error);
    BOOL result = error ? fallback : SHRegGetBoolUSValueW(k, v, ignore_hkcu, fallback);
    KbFree(k);
    KbFree(v);
    return result;
}
