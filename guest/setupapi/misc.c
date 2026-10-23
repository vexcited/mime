/*
 * setupapi outside inf parsing: copying or decompressing a file, copying an oem inf into the inf folder, and the
 * OsComponents records update packages leave, which keep a component's files from being restored.
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

/* ------------------------------------------------------------------ OS components */

#define OS_COMPONENTS L"SYSTEM\\Setup\\OsComponents"

static void guid_text(const GUID *g, WCHAR *out)
{
    _snwprintf(out, 40, L"{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", (unsigned long)g->Data1, g->Data2, g->Data3,
              g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
}

/* stored without the trailing zeros that make up most of the record, and read back with them. */
static LSTATUS set_record(HKEY key, PCWSTR name, const void *record, DWORD size)
{
    const BYTE *bytes = record;
    while (size > sizeof(DWORD) && !bytes[size - 1])
        size--;
    return RegSetValueExW(key, name, 0, REG_BINARY, bytes, size);
}

static BOOL get_record(HKEY key, PCWSTR name, void *record, DWORD size)
{
    BYTE *bytes = record;
    DWORD got = size;
    memset(bytes, 0, size);
    if (RegQueryValueExW(key, name, nullptr, nullptr, bytes, &got) != ERROR_SUCCESS || got < sizeof(DWORD))
        return FALSE;
    *(DWORD *)bytes = size;
    return TRUE;
}

SETUPAPI BOOL WINAPI SetupRegisterOsComponent(PSETUP_OS_COMPONENT_DATA component, PSETUP_OS_EXCEPTION_DATA exception)
{
    if (!component || component->Size < sizeof(*component)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR name[64], path[128];
    guid_text(&component->ComponentGuid, name);
    _snwprintf(path, RTL_NUMBER_OF(path), L"%ls\\%ls", OS_COMPONENTS, name);
    HKEY key;
    LSTATUS r = RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
    if (r != ERROR_SUCCESS) {
        SetLastError((DWORD)r);
        return FALSE;
    }
    set_record(key, L"Component", component, sizeof(*component));
    if (exception && exception->Size >= sizeof(*exception))
        set_record(key, L"Exception", exception, sizeof(*exception));
    else
        RegDeleteValueW(key, L"Exception");
    RegCloseKey(key);
    return TRUE;
}

SETUPAPI BOOL WINAPI SetupQueryRegisteredOsComponent(const GUID *guid, PSETUP_OS_COMPONENT_DATA component,
                                                     PSETUP_OS_EXCEPTION_DATA exception)
{
    if (!guid || !component || component->Size < sizeof(*component)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR name[64], path[128];
    guid_text(guid, name);
    _snwprintf(path, RTL_NUMBER_OF(path), L"%ls\\%ls", OS_COMPONENTS, name);
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        SetLastError(ERROR_NOT_FOUND);
        return FALSE;
    }
    BOOL ok = get_record(key, L"Component", component, sizeof(*component));
    if (ok && exception && exception->Size >= sizeof(*exception) && !get_record(key, L"Exception", exception, sizeof(*exception)))
        memset((BYTE *)exception + sizeof(DWORD), 0, sizeof(*exception) - sizeof(DWORD));
    RegCloseKey(key);
    if (!ok)
        SetLastError(ERROR_NOT_FOUND);
    return ok;
}

SETUPAPI BOOL WINAPI SetupUnRegisterOsComponent(const GUID *guid)
{
    if (!guid) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR name[64], path[128];
    guid_text(guid, name);
    _snwprintf(path, RTL_NUMBER_OF(path), L"%ls\\%ls", OS_COMPONENTS, name);
    LSTATUS r = RegDeleteKeyW(HKEY_LOCAL_MACHINE, path);
    if (r != ERROR_SUCCESS) {
        SetLastError(r == ERROR_FILE_NOT_FOUND ? ERROR_NOT_FOUND : (DWORD)r);
        return FALSE;
    }
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
