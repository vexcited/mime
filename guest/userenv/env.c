/*
 * environment blocks of a user, as profapi builds them for userenv: the
 * machine's variables first, then the user's, each later source replacing
 * what came before except Path, which the user's value extends.
 */
#include <mime/nt.h>
#include <mime/ntrtl.h>
#include <mime/win32/winbase.h>
#include <mime/win32/winnls.h>

#define USERENV DECLSPEC_EXPORT
#define TOKEN_QUERY 0x0008
#define TOKEN_USER_CLASS 1

typedef BOOL(WINAPI *LOOKUP_ACCOUNT_SID)(LPCWSTR, PSID, LPWSTR, LPDWORD, LPWSTR, LPDWORD, PDWORD);

static void set_var(PWSTR *env, PCWSTR name, PCWSTR value)
{
    UNICODE_STRING n, v;
    RtlInitUnicodeString(&n, name);
    if (value && *value) {
        RtlInitUnicodeString(&v, value);
        RtlSetEnvironmentVariable(env, &n, &v);
    } else {
        RtlSetEnvironmentVariable(env, &n, nullptr);
    }
}

static BOOL get_var(PWSTR env, PCWSTR name, PWSTR out, DWORD chars)
{
    UNICODE_STRING n, v = {0, (USHORT)((chars - 1) * sizeof(WCHAR)), out};
    RtlInitUnicodeString(&n, name);
    if (!NT_SUCCESS(RtlQueryEnvironmentVariable_U(env, &n, &v)))
        return FALSE;
    out[v.Length / sizeof(WCHAR)] = 0;
    return TRUE;
}

/* REG_EXPAND_SZ values expand against the block built so far. */
static void expand_in(PWSTR env, PCWSTR raw, PWSTR out, DWORD chars)
{
    UNICODE_STRING source, dest = {0, (USHORT)((chars - 1) * sizeof(WCHAR)), out};
    RtlInitUnicodeString(&source, raw);
    if (!NT_SUCCESS(RtlExpandEnvironmentStrings_U(env, &source, &dest, nullptr))) {
        lstrcpynW(out, raw, (int)chars);
        return;
    }
    out[dest.Length / sizeof(WCHAR)] = 0;
}

/* every string value of a key; the user's Path is appended to the machine's with a semicolon. */
static void set_from_key(PWSTR *env, HKEY root, PCWSTR path, BOOL user)
{
    HKEY key;
    if (RegOpenKeyExW(root, path, 0, KEY_READ, &key))
        return;
    for (DWORD i = 0;; i++) {
        WCHAR name[256], raw[2048], value[4096];
        DWORD name_len = RTL_NUMBER_OF(name), size = sizeof(raw) - sizeof(WCHAR), type;
        LONG error = RegEnumValueW(key, i, name, &name_len, nullptr, &type, (LPBYTE)raw, &size);
        if (error == ERROR_NO_MORE_ITEMS)
            break;
        if (error || (type != REG_SZ && type != REG_EXPAND_SZ))
            continue;
        raw[size / sizeof(WCHAR)] = 0;
        if (type == REG_EXPAND_SZ)
            expand_in(*env, raw, value, RTL_NUMBER_OF(value));
        else
            lstrcpynW(value, raw, RTL_NUMBER_OF(value));
        if (user && !_wcsicmp(name, L"Path")) {
            WCHAR joined[8192];
            if (get_var(*env, L"Path", joined, RTL_NUMBER_OF(joined)) && joined[0]) {
                size_t n = wcslen(joined);
                if (n + 1 + wcslen(value) < RTL_NUMBER_OF(joined)) {
                    if (joined[n - 1] != L';')
                        joined[n++] = L';';
                    wcscpy(joined + n, value);
                    set_var(env, L"Path", joined);
                    continue;
                }
            }
        }
        set_var(env, name, value);
    }
    RegCloseKey(key);
}

static void set_from_value(PWSTR *env, PCWSTR name, PCWSTR key, PCWSTR value)
{
    WCHAR data[MAX_PATH];
    DWORD size = sizeof(data);
    if (!RegGetValueW(HKEY_LOCAL_MACHINE, key, value, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr,
                      data, &size)) {
        WCHAR expanded[MAX_PATH];
        expand_in(*env, data, expanded, RTL_NUMBER_OF(expanded));
        set_var(env, name, expanded);
    }
}

static BOOL profile_list_value(PCWSTR sid, PCWSTR value, PWSTR out, DWORD chars)
{
    WCHAR path[256], raw[MAX_PATH];
    _snwprintf(path, RTL_NUMBER_OF(path), L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList%ls%ls",
               sid ? L"\\" : L"", sid ? sid : L"");
    DWORD size = sizeof(raw);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, path, value, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr,
                     raw, &size))
        return FALSE;
    return ExpandEnvironmentStringsW(raw, out, chars) - 1 < chars;
}

static void set_machine(PWSTR *env)
{
    WCHAR value[MAX_PATH];
    set_var(env, L"SystemRoot", get_var(nullptr, L"SystemRoot", value, MAX_PATH) ? value : nullptr);
    set_var(env, L"SystemDrive", get_var(nullptr, L"SystemDrive", value, MAX_PATH) ? value : nullptr);
    if (profile_list_value(nullptr, L"ProgramData", value, MAX_PATH)) {
        set_var(env, L"ALLUSERSPROFILE", value);
        set_var(env, L"ProgramData", value);
    }
    if (profile_list_value(nullptr, L"Public", value, MAX_PATH))
        set_var(env, L"PUBLIC", value);
    set_from_key(env, HKEY_LOCAL_MACHINE, L"System\\CurrentControlSet\\Control\\Session Manager\\Environment", FALSE);
    DWORD n = MAX_PATH;
    if (GetComputerNameExW(ComputerNameNetBIOS, value, &n))
        set_var(env, L"COMPUTERNAME", value);
    WCHAR profiles[MAX_PATH], name[MAX_PATH];
    if (profile_list_value(nullptr, L"ProfilesDirectory", profiles, MAX_PATH) &&
        profile_list_value(nullptr, L"Default", name, MAX_PATH))
        set_var(env, L"USERPROFILE", name);
    static const PCWSTR folders[][2] = {
        {L"ProgramFiles", L"ProgramFilesDir"},
        {L"CommonProgramFiles", L"CommonFilesDir"},
#ifdef _WIN64
        {L"ProgramFiles(x86)", L"ProgramFilesDir (x86)"},
        {L"CommonProgramFiles(x86)", L"CommonFilesDir (x86)"},
#endif
        {L"ProgramW6432", L"ProgramW6432Dir"},
        {L"CommonProgramW6432", L"CommonW6432Dir"},
    };
    for (size_t i = 0; i < RTL_NUMBER_OF(folders); i++)
        set_from_value(env, folders[i][0], L"Software\\Microsoft\\Windows\\CurrentVersion", folders[i][1]);
}

static BOOL token_user(HANDLE token, ULONG64 *buffer, ULONG size)
{
    HANDLE opened = nullptr;
    if (token == (HANDLE)(LONG_PTR)-4 || token == (HANDLE)(LONG_PTR)-6) {
        if (!NT_SUCCESS(NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY, &opened)))
            return FALSE;
        token = opened;
    }
    ULONG length;
    NTSTATUS status = NtQueryInformationToken(token, TOKEN_USER_CLASS, buffer, size, &length);
    if (opened)
        NtClose(opened);
    return NT_SUCCESS(status);
}

static BOOL sid_string(const BYTE *sid, PWSTR out, DWORD chars)
{
    if (sid[0] != 1 || sid[1] > 15)
        return FALSE;
    ULONG64 authority = 0;
    for (int i = 0; i < 6; i++)
        authority = authority << 8 | sid[2 + i];
    int n = _snwprintf(out, chars, authority >> 32 ? L"S-1-0x%012llX" : L"S-1-%llu", authority);
    for (BYTE i = 0; i < sid[1] && n > 0 && (DWORD)n < chars; i++)
        n += _snwprintf(out + n, chars - n, L"-%lu", ((const DWORD *)(sid + 8))[i]);
    return n > 0 && (DWORD)n < chars;
}

static DWORD set_user(PWSTR *env, HANDLE token)
{
    ULONG64 buffer[32];
    WCHAR sid[200];
    if (!token_user(token, buffer, sizeof(buffer)) || !sid_string((const BYTE *)buffer[0], sid, RTL_NUMBER_OF(sid)))
        return ERROR_INVALID_HANDLE;
    HKEY hive;
    LONG error = RegOpenKeyExW(HKEY_USERS, sid, 0, KEY_READ, &hive);
    if (error)
        return error;
    HMODULE advapi = LoadLibraryW(L"advapi32.dll");
    LOOKUP_ACCOUNT_SID lookup = advapi ? (LOOKUP_ACCOUNT_SID)GetProcAddress(advapi, "LookupAccountSidW") : nullptr;
    WCHAR name[256], domain[256], value[MAX_PATH];
    DWORD name_len = RTL_NUMBER_OF(name), domain_len = RTL_NUMBER_OF(domain), use;
    if (lookup && lookup(nullptr, (PSID)buffer[0], name, &name_len, domain, &domain_len, &use)) {
        set_var(env, L"USERNAME", name);
        set_var(env, L"USERDOMAIN", domain);
    }
    if (advapi)
        FreeLibrary(advapi);
    if (profile_list_value(sid, L"ProfileImagePath", value, MAX_PATH)) {
        WCHAR folder[MAX_PATH];
        set_var(env, L"USERPROFILE", value);
        _snwprintf(folder, RTL_NUMBER_OF(folder), L"%ls\\AppData\\Roaming", value);
        set_var(env, L"APPDATA", folder);
        _snwprintf(folder, RTL_NUMBER_OF(folder), L"%ls\\AppData\\Local", value);
        set_var(env, L"LOCALAPPDATA", folder);
    }
    /* the misspelled key is the one windows reads. */
    set_from_key(env, HKEY_LOCAL_MACHINE, L"SOFTWARE\\DefaultUserEnviroment", TRUE);
    set_from_key(env, hive, L"Environment", TRUE);
    set_from_key(env, hive, L"Volatile Environment", TRUE);
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    WCHAR path[64];
    _snwprintf(path, RTL_NUMBER_OF(path), L"Volatile Environment\\%lu", session);
    set_from_key(env, hive, path, TRUE);
    RegCloseKey(hive);
    return ERROR_SUCCESS;
}

USERENV BOOL WINAPI CreateEnvironmentBlock(LPVOID *environment, HANDLE token, BOOL inherit)
{
    PWSTR env;
    NTSTATUS status = RtlCreateEnvironment((BOOLEAN)inherit, &env);
    if (!NT_SUCCESS(status)) {
        SetLastError(RtlNtStatusToDosError(status));
        return FALSE;
    }
    set_machine(&env);
    if (token && token != INVALID_HANDLE_VALUE) {
        DWORD error = set_user(&env, token);
        if (error) {
            RtlDestroyEnvironment(env);
            *environment = nullptr;
            SetLastError(error);
            return FALSE;
        }
    }
    *environment = env;
    return TRUE;
}

USERENV BOOL WINAPI DestroyEnvironmentBlock(LPVOID environment)
{
    if (!environment) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    RtlDestroyEnvironment(environment);
    return TRUE;
}

/* ------------------------------------------------------------------ group policy */

/*
 * a machine without a domain applies no group policy: the sections lock
 * nothing and no notification ever fires, so registering only checks the
 * event.
 */
USERENV HANDLE WINAPI EnterCriticalPolicySection(BOOL machine)
{
    static LONG g_section;
    return (HANDLE)&g_section;
}

USERENV BOOL WINAPI LeaveCriticalPolicySection(HANDLE section)
{
    if (!section) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

USERENV BOOL WINAPI RegisterGPNotification(HANDLE event, BOOL machine)
{
    if (!event) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

USERENV BOOL WINAPI UnregisterGPNotification(HANDLE event)
{
    if (!event) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}
