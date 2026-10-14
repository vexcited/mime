#include "shlwapi_private.h"

BOOL WINAPI PathUnExpandEnvStringsW(LPCWSTR path, LPWSTR out, UINT size)
{
    static const PCWSTR names[] = {L"ALLUSERSPROFILE", L"APPDATA", L"COMPUTERNAME", L"ProgramFiles",
                                  L"SystemRoot", L"SystemDrive", L"USERPROFILE"};
    if (!path || !out || !size)
        return FALSE;
    SIZE_T path_length = wcslen(path), longest = 0;
    PCWSTR match = NULL;
    WCHAR value[MAX_PATH];
    for (ULONG i = 0; i < RTL_NUMBER_OF(names); i++) {
        DWORD length = GetEnvironmentVariableW(names[i], value, RTL_NUMBER_OF(value));
        if (!length || length >= RTL_NUMBER_OF(value) || length <= longest || length > path_length)
            continue;
        if (_wcsnicmp(path, value, length) ||
            (path[length] && path[length] != L'\\' && path[length] != L'/'))
            continue;
        match = names[i];
        longest = length;
    }
    if (!match)
        return FALSE;
    SIZE_T name_length = wcslen(match);
    if (name_length + 2 + path_length - longest >= size)
        return FALSE;
    memmove(out + name_length + 2, path + longest, (path_length - longest + 1) * sizeof(WCHAR));
    out[0] = L'%';
    memcpy(out + 1, match, name_length * sizeof(WCHAR));
    out[name_length + 1] = L'%';
    return TRUE;
}

LPWSTR WINAPI StrFormatKBSizeW(LONGLONG bytes, LPWSTR out, UINT size)
{
    if (!out || !size || bytes < 0)
        return NULL;
    ULONGLONG kb = (ULONGLONG)bytes / 1000 + ((ULONGLONG)bytes % 1000 != 0);
    WCHAR number[32], reverse[32], decimal[8], separator[8], formatted[96];
    ULONG n = 0;
    do {
        reverse[n++] = L'0' + (WCHAR)(kb % 10);
        kb /= 10;
    } while (kb);
    for (ULONG i = 0; i < n; i++)
        number[i] = reverse[n - i - 1];
    number[n] = 0;
    if (!GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SDECIMAL, decimal, RTL_NUMBER_OF(decimal)) ||
        !GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_STHOUSAND, separator, RTL_NUMBER_OF(separator)))
        return NULL;
    NUMBERFMTW format = {0, 1, 3, decimal, separator, 1};
    int length = GetNumberFormatW(LOCALE_USER_DEFAULT, 0, number, &format, formatted, RTL_NUMBER_OF(formatted));
    if (!length || (UINT)length + 3 > size)
        return NULL;
    memcpy(out, formatted, (length - 1) * sizeof(WCHAR));
    memcpy(out + length - 1, L" KB", 4 * sizeof(WCHAR));
    return out;
}
