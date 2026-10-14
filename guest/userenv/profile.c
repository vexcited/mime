/*
 * user profile directories.
 *
 * the kernel registers the profile of the logged on user at boot, under
 * ProfileList\<sid> of the software hive, next to the profiles, default,
 * public and program data directories windows setup writes there. these
 * functions read that list, like userenv does on windows.
 */
#include <mime/nt.h>
#include <mime/ntrtl.h>
#include <mime/win32/winbase.h>
#include <mime/win32/winnls.h>

#define USERENV DECLSPEC_EXPORT
#define PROFILE_LIST L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList"
#define TOKEN_QUERY 0x0008
#define TOKEN_USER_CLASS 1

FORCEINLINE void set_error(DWORD error)
{
    NtCurrentTeb()->LastErrorValue = error;
}

/* reads an expandable value of the profile list, or of one of its profiles; returns a win32 error. */
static DWORD profile_value(PCWSTR sid, PCWSTR value, PWSTR out, DWORD chars)
{
    WCHAR path[192], raw[MAX_PATH];
    if (sid)
        _snwprintf(path, RTL_NUMBER_OF(path), PROFILE_LIST L"\\%ls", sid);
    else
        wcscpy(path, PROFILE_LIST);
    HKEY key;
    LONG error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_QUERY_VALUE, &key);
    if (error)
        return error;
    DWORD type, size = sizeof(raw) - sizeof(WCHAR);
    error = RegQueryValueExW(key, value, NULL, &type, (LPBYTE)raw, &size);
    RegCloseKey(key);
    if (error)
        return error;
    if (type != REG_SZ && type != REG_EXPAND_SZ)
        return ERROR_FILE_NOT_FOUND;
    raw[size / sizeof(WCHAR)] = 0;
    DWORD n = ExpandEnvironmentStringsW(raw, out, chars);
    return n && n <= chars ? ERROR_SUCCESS : ERROR_INSUFFICIENT_BUFFER;
}

/* the string sid of a token's user, S-1-5-21-... */
static BOOL token_sid_string(HANDLE token, PWSTR out, DWORD chars)
{
    HANDLE opened = NULL;
    if (token == (HANDLE)(LONG_PTR)-4 || token == (HANDLE)(LONG_PTR)-6) {
        if (!NT_SUCCESS(NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY, &opened)))
            return FALSE;
        token = opened;
    }
    ULONG64 buffer[32];
    ULONG length;
    NTSTATUS status = NtQueryInformationToken(token, TOKEN_USER_CLASS, buffer, sizeof(buffer), &length);
    if (opened)
        NtClose(opened);
    if (!NT_SUCCESS(status))
        return FALSE;
    const BYTE *sid = (const BYTE *)buffer[0];
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

/*
 * copies a directory into the caller's buffer: the size counts the terminator
 * both on success and when the buffer is too small, as userenv does.
 */
static BOOL return_path(PCWSTR path, LPWSTR buffer, LPDWORD size)
{
    DWORD n = (DWORD)wcslen(path) + 1;
    if (!buffer || *size < n) {
        *size = n;
        set_error(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    memcpy(buffer, path, n * sizeof(WCHAR));
    *size = n;
    return TRUE;
}

static BOOL return_path_ansi(PCWSTR path, LPSTR buffer, LPDWORD size)
{
    DWORD n = (DWORD)WideCharToMultiByte(CP_ACP, 0, path, -1, NULL, 0, NULL, NULL);
    if (!buffer || *size < n) {
        *size = n;
        set_error(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    WideCharToMultiByte(CP_ACP, 0, path, -1, buffer, n, NULL, NULL);
    *size = n;
    return TRUE;
}

static BOOL user_profile(HANDLE token, PWSTR path)
{
    WCHAR sid[200];
    if (!token_sid_string(token, sid, RTL_NUMBER_OF(sid))) {
        set_error(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    DWORD error = profile_value(sid, L"ProfileImagePath", path, MAX_PATH);
    if (error) {
        set_error(error);
        return FALSE;
    }
    return TRUE;
}

USERENV BOOL WINAPI GetUserProfileDirectoryW(HANDLE token, LPWSTR buffer, LPDWORD size)
{
    WCHAR path[MAX_PATH];
    if (!token) {
        set_error(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (!size) {
        set_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return user_profile(token, path) && return_path(path, *size ? buffer : NULL, size);
}

USERENV BOOL WINAPI GetProfileType(DWORD *flags)
{
    if (!flags) {
        set_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    HANDLE token;
    NTSTATUS status = NtOpenThreadToken(NtCurrentThread(), TOKEN_QUERY, TRUE, &token);
    if (status == STATUS_NO_TOKEN)
        status = NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY, &token);
    if (!NT_SUCCESS(status)) {
        set_error(RtlNtStatusToDosError(status));
        return FALSE;
    }
    WCHAR path[MAX_PATH], sid[200];
    BOOL loaded = user_profile(token, path) && token_sid_string(token, sid, RTL_NUMBER_OF(sid));
    NtClose(token);
    if (!loaded)
        return FALSE;
    HKEY hive;
    LONG error = RegOpenKeyExW(HKEY_USERS, sid, 0, KEY_READ, &hive);
    if (error) {
        set_error(error);
        return FALSE;
    }
    RegCloseKey(hive);
    /* mime loads a persistent local profile for its current user. */
    *flags = 0;
    return TRUE;
}

USERENV BOOL WINAPI GetUserProfileDirectoryA(HANDLE token, LPSTR buffer, LPDWORD size)
{
    WCHAR path[MAX_PATH];
    if (!token) {
        set_error(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (!size) {
        set_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return user_profile(token, path) && return_path_ansi(path, *size ? buffer : NULL, size);
}

/* one of the directories the profile list itself names. */
static BOOL list_directory(PCWSTR value, PWSTR path)
{
    DWORD error = profile_value(NULL, value, path, MAX_PATH);
    if (error) {
        set_error(error);
        return FALSE;
    }
    return TRUE;
}

USERENV BOOL WINAPI GetProfilesDirectoryW(LPWSTR buffer, LPDWORD size)
{
    WCHAR path[MAX_PATH];
    if (!size) {
        set_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return list_directory(L"ProfilesDirectory", path) && return_path(path, buffer, size);
}

USERENV BOOL WINAPI GetProfilesDirectoryA(LPSTR buffer, LPDWORD size)
{
    WCHAR path[MAX_PATH];
    if (!size) {
        set_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return list_directory(L"ProfilesDirectory", path) && return_path_ansi(path, buffer, size);
}

USERENV BOOL WINAPI GetDefaultUserProfileDirectoryW(LPWSTR buffer, LPDWORD size)
{
    WCHAR path[MAX_PATH];
    if (!size) {
        set_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return list_directory(L"Default", path) && return_path(path, buffer, size);
}

USERENV BOOL WINAPI GetDefaultUserProfileDirectoryA(LPSTR buffer, LPDWORD size)
{
    WCHAR path[MAX_PATH];
    if (!size) {
        set_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return list_directory(L"Default", path) && return_path_ansi(path, buffer, size);
}

/* since windows vista the all users profile is the program data directory. */
USERENV BOOL WINAPI GetAllUsersProfileDirectoryW(LPWSTR buffer, LPDWORD size)
{
    WCHAR path[MAX_PATH];
    if (!size) {
        set_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return list_directory(L"ProgramData", path) && return_path(path, buffer, size);
}

USERENV BOOL WINAPI GetAllUsersProfileDirectoryA(LPSTR buffer, LPDWORD size)
{
    WCHAR path[MAX_PATH];
    if (!size) {
        set_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return list_directory(L"ProgramData", path) && return_path_ansi(path, buffer, size);
}
