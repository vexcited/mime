/*
 * the settings of the host: what the registry keeps below HKLM and HKCU, the .wsh files that name a script with options,
 * and the file types, which //H points at one of the two hosts.
 */
#include "wsh.h"

#define SETTINGS_KEY L"Software\\Microsoft\\Windows Script Host\\Settings"

static HRESULT win32(LONG error)
{
    return error > 0 ? (HRESULT)((error & 0xFFFF) | 0x80070000) : (HRESULT)error;
}

static HRESULT read_dword(HKEY key, PCWSTR name, DWORD *value)
{
    DWORD type = 0, size = 4, data = 0;
    LONG error = RegQueryValueExW(key, name, nullptr, &type, (PBYTE)&data, &size);
    if (error)
        return win32(error);
    if (type != REG_DWORD || size != 4)
        return (HRESULT)0x80040153;
    *value = data;
    return S_OK;
}

HRESULT WshLoadRegistrySettings(HKEY root, SETTINGS *settings)
{
    HKEY key;
    LONG error = RegCreateKeyExW(root, SETTINGS_KEY, 0, nullptr, 0, KEY_READ, nullptr, &key, nullptr);
    if (error)
        return win32(error);
    DWORD value = 0;
    HRESULT hr = read_dword(key, L"Timeout", &value);
    if (SUCCEEDED(hr)) {
        settings->Timeout = value;
        settings->TimeoutSet = value != 0;
    } else if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
        settings->Timeout = 0;
        settings->TimeoutSet = FALSE;
        hr = S_OK;
    }
    if (SUCCEEDED(hr)) {
        hr = read_dword(key, L"DisplayLogo", &value);
        if (SUCCEEDED(hr)) {
            settings->Logo = value != 0;
            settings->LogoSet = TRUE;
        } else if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
            settings->LogoSet = FALSE;
            hr = S_OK;
        }
    }
    RegCloseKey(key);
    return hr;
}

/* //S: the timeout and the logo are kept for the user. */
HRESULT WshSaveRegistrySettings(const SETTINGS *settings)
{
    HKEY key;
    LONG error = RegCreateKeyExW(HKEY_CURRENT_USER, SETTINGS_KEY, 0, nullptr, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, nullptr, &key, nullptr);
    if (error)
        return win32(error);
    DWORD timeout = settings->TimeoutSet ? settings->Timeout : 0, logo = settings->Logo;
    error = RegSetValueExW(key, L"Timeout", 0, REG_DWORD, (PBYTE)&timeout, 4);
    if (!error)
        error = RegSetValueExW(key, L"DisplayLogo", 0, REG_DWORD, (PBYTE)&logo, 4);
    RegCloseKey(key);
    return win32(error);
}

/* whether the machine lets scripts run: Enabled in the settings of the machine and of the user, which wins unless the machine says to ignore it. */
BOOL WshSecurityEnabled(void)
{
    HKEY user = nullptr, machine = nullptr;
    RegOpenKeyExW(HKEY_CURRENT_USER, SETTINGS_KEY, 0, KEY_READ, &user);
    RegOpenKeyExW(HKEY_LOCAL_MACHINE, SETTINGS_KEY, 0, KEY_READ, &machine);
    DWORD ignore = !user, value = 0;
    BOOL enabled = TRUE;
    if (machine && user && SUCCEEDED(read_dword(machine, L"IgnoreUserSettings", &value)))
        ignore = value != 0;
    if (!ignore && user && SUCCEEDED(read_dword(user, L"Enabled", &value)))
        enabled = value != 0;
    else if (machine && SUCCEEDED(read_dword(machine, L"Enabled", &value)))
        enabled = value != 0;
    if (user)
        RegCloseKey(user);
    if (machine)
        RegCloseKey(machine);
    return enabled;
}

/* ------------------------------------------------------------------ .wsh files */

static HRESULT read_profile_string(PCWSTR file, PCWSTR section, PCWSTR key, PWSTR out, DWORD capacity)
{
    out[0] = 0;
    DWORD length = GetPrivateProfileStringW(section, key, L"", out, capacity, file);
    return length ? S_OK : HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
}

/* the script and the options of a settings file. The script file is the Path of [ScriptFile]. */
HRESULT WshLoadFileSettings(PCWSTR path, SETTINGS *settings, PWSTR script, DWORD capacity)
{
    WCHAR full[MAX_PATH], text[32];
    PWSTR part;
    if (!GetFullPathNameW(path, MAX_PATH, full, &part))
        return win32(GetLastError());
    DWORD attributes = GetFileAttributesW(full);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return win32(GetLastError());
    HRESULT hr = read_profile_string(full, L"ScriptFile", L"Path", script, capacity);
    if (FAILED(hr))
        script[0] = 0;
    if (!settings->TimeoutOption) {
        if (SUCCEEDED(read_profile_string(full, L"Options", L"Timeout", text, 32))) {
            int seconds = 0;
            for (PCWSTR p = text; *p >= L'0' && *p <= L'9'; p++)
                seconds = seconds * 10 + (*p - L'0');
            settings->Timeout = (DWORD)seconds;
            settings->TimeoutSet = TRUE;
        } else
            settings->TimeoutSet = FALSE;
    }
    if (!settings->LogoOption) {
        if (SUCCEEDED(read_profile_string(full, L"Options", L"DisplayLogo", text, 32))) {
            settings->Logo = text[0] != L'0' && text[0] != L'-';
            settings->LogoSet = TRUE;
        } else
            settings->LogoSet = FALSE;
    }
    return S_OK;
}

/* ------------------------------------------------------------------ the default host */

static HRESULT set_verb(HKEY root, PCWSTR path, PCWSTR verb)
{
    HKEY key;
    LONG error = RegOpenKeyExW(root, path, 0, KEY_SET_VALUE, &key);
    if (error)
        return win32(error);
    error = RegSetValueExW(key, nullptr, 0, REG_SZ, (const BYTE *)verb, (DWORD)((wcslen(verb) + 1) * sizeof(WCHAR)));
    RegCloseKey(key);
    return win32(error);
}

/* a file type opens with the verb: in the classes of the machine, and when that is denied in the classes of the user. */
static HRESULT change_type(PCWSTR progid, PCWSTR verb, BOOL only_engines)
{
    HKEY key;
    LONG error = RegOpenKeyExW(HKEY_CLASSES_ROOT, progid, 0, KEY_READ | KEY_WRITE, &key);
    if (error)
        return only_engines ? S_OK : win32(error);
    HRESULT hr = S_OK;
    if (only_engines) {
        HKEY engine;
        if (RegOpenKeyExW(key, L"ScriptEngine", 0, KEY_READ, &engine)) {
            RegCloseKey(key);
            return S_OK;
        }
        RegCloseKey(engine);
    }
    WCHAR path[300];
    _snwprintf(path, RTL_NUMBER_OF(path), L"%s\\Shell", progid);
    hr = set_verb(HKEY_CLASSES_ROOT, path, verb);
    if (FAILED(hr) && (hr & 0xFFFF) == ERROR_ACCESS_DENIED) {
        HKEY user;
        _snwprintf(path, RTL_NUMBER_OF(path), L"SOFTWARE\\Classes\\%s\\Shell", progid);
        if (!RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &user, nullptr)) {
            error = RegSetValueExW(user, nullptr, 0, REG_SZ, (const BYTE *)verb, (DWORD)((wcslen(verb) + 1) * sizeof(WCHAR)));
            RegCloseKey(user);
            hr = win32(error);
        }
    }
    RegCloseKey(key);
    return hr;
}

/* //H:cscript and //H:wscript: every file type with a script engine, and the two script file types, open with that host. */
HRESULT WshChangeDefaultHost(int host)
{
    if (host != 'W' && host != 'C')
        return E_INVALIDARG;
    PCWSTR verb = host == 'C' ? L"Open2" : L"Open";
    for (DWORD index = 0;; index++) {
        WCHAR name[260], progid[260];
        DWORD size = RTL_NUMBER_OF(name);
        LONG error = RegEnumKeyExW(HKEY_CLASSES_ROOT, index, name, &size, nullptr, nullptr, nullptr, nullptr);
        if (error == ERROR_NO_MORE_ITEMS)
            break;
        if (error)
            return win32(error);
        if (name[0] != L'.')
            continue;
        HKEY key;
        if (RegOpenKeyExW(HKEY_CLASSES_ROOT, name, 0, KEY_READ, &key))
            return win32(GetLastError());
        DWORD bytes = sizeof(progid), type;
        error = RegQueryValueExW(key, nullptr, nullptr, &type, (PBYTE)progid, &bytes);
        RegCloseKey(key);
        if (!error && bytes >= 2) {
            progid[RTL_NUMBER_OF(progid) - 1] = 0;
            HRESULT hr = change_type(progid, verb, TRUE);
            if (FAILED(hr))
                return hr;
        }
    }
    HRESULT hr = change_type(L"WSFFile", verb, FALSE);
    if (FAILED(hr))
        return hr;
    return change_type(L"WSHFile", verb, FALSE);
}
