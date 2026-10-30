/*
 * setupapi outside inf parsing: copying or decompressing a file and copying an oem inf into the inf folder.
 */
#include "setup_private.h"

/* kernel32's lz calls, for files made by compress.exe. */
WINBASEAPI int WINAPI LZOpenFileW(LPWSTR name, LPVOID info, WORD style);
WINBASEAPI LONG WINAPI LZCopy(int source, int destination);
WINBASEAPI void WINAPI LZClose(int file);

SETUPAPI DWORD WINAPI SetupDecompressOrCopyFileW(PCWSTR source, PCWSTR target, PUINT compression)
{
    if (!source || !target)
        return ERROR_INVALID_PARAMETER;
    if (compression)
        *compression = 0; /* FILE_COMPRESSION_NONE */
    HANDLE in = CreateFileW(source, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (in == INVALID_HANDLE_VALUE)
        return GetLastError();
    BYTE head[8] = {};
    DWORD done = 0;
    ReadFile(in, head, sizeof(head), &done, nullptr);
    CloseHandle(in);
    /* compress.exe output starts with SZDD, and its extension's last letter is an underscore or missing. */
    if (done == 8 && !memcmp(head, "SZDD\x88\xF0\x27\x33", 8)) {
        if (compression)
            *compression = 2; /* FILE_COMPRESSION_WINLZA */
        BYTE of1[136] = {}, of2[136] = {};
        int from = LZOpenFileW((LPWSTR)source, (LPVOID)of1, 0);
        if (from < 0)
            return ERROR_INVALID_DATA;
        int out = LZOpenFileW((LPWSTR)target, (LPVOID)of2, 0x1000); /* OF_CREATE */
        if (out < 0) {
            LZClose(from);
            return ERROR_ACCESS_DENIED;
        }
        LONG result = LZCopy(from, out);
        LZClose(out);
        LZClose(from);
        return result >= 0 ? ERROR_SUCCESS : ERROR_INVALID_DATA;
    }
    return CopyFileW(source, target, FALSE) ? ERROR_SUCCESS : GetLastError();
}

SETUPAPI DWORD WINAPI SetupDecompressOrCopyFileA(PCSTR source, PCSTR target, PUINT compression)
{
    PWSTR a = SetupToWide(source), b = SetupToWide(target);
    DWORD error = a && b ? SetupDecompressOrCopyFileW(a, b, compression) : ERROR_NOT_ENOUGH_MEMORY;
    SetupFree(a);
    SetupFree(b);
    return error;
}

/* copies the inf into the windows inf folder as oemN.inf and returns that name. */
SETUPAPI BOOL WINAPI SetupCopyOEMInfW(PCWSTR source, PCWSTR location, DWORD media_type, DWORD style, PWSTR dest, DWORD size,
                                      PDWORD required, PWSTR *component)
{
    if (!source) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR full[MAX_PATH];
    if (!GetFullPathNameW(source, MAX_PATH, full, nullptr)) {
        return FALSE;
    }
    if (GetFileAttributesW(full) == INVALID_FILE_ATTRIBUTES)
        return FALSE;
    WCHAR directory[MAX_PATH], target[MAX_PATH];
    UINT n = GetWindowsDirectoryW(directory, MAX_PATH);
    if (!n || n + 20 >= MAX_PATH) {
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return FALSE;
    }
    wcscat(directory, L"\\INF");
    CreateDirectoryW(directory, nullptr);
    /* the same inf is not copied twice: an oem file with the same bytes is returned instead. */
    BOOL found = FALSE;
    for (int i = 0; i < 1000 && !found; i++) {
        _snwprintf(target, RTL_NUMBER_OF(target), L"%ls\\oem%d.inf", directory, i);
        if (GetFileAttributesW(target) == INVALID_FILE_ATTRIBUTES) {
            if (CopyFileW(full, target, TRUE))
                found = TRUE;
            break;
        }
    }
    if (!found && GetLastError() != ERROR_SUCCESS && target[0] == 0)
        return FALSE;
    if (style & 1) /* SP_COPY_DELETESOURCE */
        DeleteFileW(full);
    DWORD need = (DWORD)wcslen(target) + 1;
    if (required)
        *required = need;
    if (dest) {
        if (size < need) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        wcscpy(dest, target);
        if (component) {
            PWSTR slash = wcsrchr(dest, '\\');
            *component = slash ? slash + 1 : dest;
        }
    }
    return TRUE;
}

SETUPAPI BOOL WINAPI SetupCopyOEMInfA(PCSTR source, PCSTR location, DWORD media_type, DWORD style, PSTR dest, DWORD size,
                                      PDWORD required, PSTR *component)
{
    PWSTR wide = SetupToWide(source);
    WCHAR out[MAX_PATH];
    PWSTR part = nullptr;
    BOOL ok = wide && SetupCopyOEMInfW(wide, nullptr, media_type, style, out, MAX_PATH, nullptr, &part);
    SetupFree(wide);
    if (!ok)
        return FALSE;
    PSTR ansi = SetupToAnsi(out);
    DWORD need = ansi ? (DWORD)strlen(ansi) + 1 : 0;
    if (required)
        *required = need;
    if (dest && ansi) {
        if (size < need) {
            SetupFree(ansi);
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        memcpy(dest, ansi, need);
        if (component) {
            PSTR slash = strrchr(dest, '\\');
            *component = slash ? slash + 1 : dest;
        }
    }
    SetupFree(ansi);
    return TRUE;
}

/* ------------------------------------------------------------------ a device property from ansi text */

SETUPAPI BOOL WINAPI SetupDiSetDeviceRegistryPropertyA(HDEVINFO set, PSP_DEVINFO_DATA device, DWORD property,
                                                       const BYTE *buffer, DWORD size)
{
    if (!buffer || property == SPDRP_CONFIGFLAGS)
        return SetupDiSetDeviceRegistryPropertyW(set, device, property, buffer, size);
    /* ansi text, a string or for some properties a multi string, which the registry keeps wide. */
    int chars = MultiByteToWideChar(CP_ACP, 0, (PCSTR)buffer, (int)size, nullptr, 0);
    PWSTR wide = chars ? SetupAlloc(((SIZE_T)chars + 1) * sizeof(WCHAR)) : nullptr;
    if (!wide) {
        SetLastError(chars ? ERROR_NOT_ENOUGH_MEMORY : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    MultiByteToWideChar(CP_ACP, 0, (PCSTR)buffer, (int)size, wide, chars);
    BOOL ok = SetupDiSetDeviceRegistryPropertyW(set, device, property, (const BYTE *)wide, (DWORD)(chars * sizeof(WCHAR)));
    SetupFree(wide);
    return ok;
}
