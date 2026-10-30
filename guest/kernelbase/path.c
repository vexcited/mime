/*
 * paths and volumes: full and long names, current, system and temporary directories, searching, drives and volume
 * information.
 */
#include "kernelbase_private.h"

#define FILE_DEVICE_CD_ROM 0x00000002
#define FILE_REMOVABLE_MEDIA 0x00000001
#define FILE_REMOTE_DEVICE 0x00000010

typedef struct KB_FILE_FS_VOLUME_INFORMATION {
    LARGE_INTEGER VolumeCreationTime;
    ULONG VolumeSerialNumber;
    ULONG VolumeLabelLength;
    BOOLEAN SupportsObjects;
    WCHAR VolumeLabel[1];
} KB_FILE_FS_VOLUME_INFORMATION;

typedef struct KB_FILE_FS_ATTRIBUTE_INFORMATION {
    ULONG FileSystemAttributes;
    LONG MaximumComponentNameLength;
    ULONG FileSystemNameLength;
    WCHAR FileSystemName[1];
} KB_FILE_FS_ATTRIBUTE_INFORMATION;

typedef struct KB_FILE_FS_FULL_SIZE_INFORMATION {
    LARGE_INTEGER TotalAllocationUnits;
    LARGE_INTEGER CallerAvailableAllocationUnits;
    LARGE_INTEGER ActualAvailableAllocationUnits;
    ULONG SectorsPerAllocationUnit;
    ULONG BytesPerSector;
} KB_FILE_FS_FULL_SIZE_INFORMATION;

/* returns the length on success, the size needed otherwise. */
static UINT copy_result(LPCWSTR value, LPWSTR buffer, UINT size)
{
    UINT n = (UINT)wcslen(value);
    if (n >= size || !buffer)
        return n + 1;
    memcpy(buffer, value, (n + 1) * sizeof(WCHAR));
    return n;
}

/* ------------------------------------------------------------------ full and long names */

DWORD WINAPI GetFullPathNameW(LPCWSTR name, DWORD size, LPWSTR buffer, LPWSTR *file_part)
{
    if (!name) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!*name) {
        KbSetError(ERROR_INVALID_NAME);
        return 0;
    }
    if (size > 0x7FFF)
        size = 0x7FFF;
    ULONG bytes = RtlGetFullPathName_U(name, size * sizeof(WCHAR), size ? buffer : nullptr, file_part);
    if (!bytes) {
        KbSetError(ERROR_INVALID_NAME);
        return 0;
    }
    return bytes / sizeof(WCHAR);
}

DWORD WINAPI GetFullPathNameA(LPCSTR name, DWORD size, LPSTR buffer, LPSTR *file_part)
{
    PWSTR w = KbAnsiToWide(name);
    if (!w) {
        if (!name)
            KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    WCHAR full[MAX_PATH * 4];
    PWSTR wpart = nullptr;
    DWORD n = GetFullPathNameW(w, RTL_NUMBER_OF(full), full, &wpart);
    KbFree(w);
    if (!n)
        return 0;
    if (n >= RTL_NUMBER_OF(full)) {
        KbSetError(ERROR_FILENAME_EXCED_RANGE);
        return 0;
    }
    DWORD r = KbWideResultToAnsi(full, buffer, size);
    if (file_part && r < size && buffer) {
        if (wpart) {
            UINT cp = KbFileApiCodePage();
            int prefix = WideCharToMultiByte(cp, 0, full, (int)(wpart - full), nullptr, 0, nullptr, nullptr);
            *file_part = buffer + prefix;
        } else {
            *file_part = nullptr;
        }
    }
    return r;
}

/* there are no short names: both directions return the existing long path as written. */
static DWORD same_path_name(LPCWSTR in, LPWSTR out, DWORD size)
{
    if (!in) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!*in) {
        KbSetError(ERROR_PATH_NOT_FOUND);
        return 0;
    }
    if (GetFileAttributesW(in) == INVALID_FILE_ATTRIBUTES)
        return 0;
    DWORD n = (DWORD)wcslen(in);
    if (n >= size || !out)
        return n + 1;
    memmove(out, in, (n + 1) * sizeof(WCHAR));
    return n;
}

DWORD WINAPI GetLongPathNameW(LPCWSTR short_path, LPWSTR long_path, DWORD size)
{
    return same_path_name(short_path, long_path, size);
}

DWORD WINAPI GetShortPathNameW(LPCWSTR long_path, LPWSTR short_path, DWORD size)
{
    return same_path_name(long_path, short_path, size);
}

static DWORD path_name_a(LPCSTR in, LPSTR out, DWORD size, DWORD(WINAPI *fn)(LPCWSTR, LPWSTR, DWORD))
{
    PWSTR w = KbAnsiToWide(in);
    if (!w) {
        if (!in)
            KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    WCHAR result[MAX_PATH * 4];
    DWORD n = fn(w, result, RTL_NUMBER_OF(result));
    KbFree(w);
    if (!n || n >= RTL_NUMBER_OF(result))
        return n;
    return KbWideResultToAnsi(result, out, size);
}

DWORD WINAPI GetLongPathNameA(LPCSTR short_path, LPSTR long_path, DWORD size)
{
    return path_name_a(short_path, long_path, size, GetLongPathNameW);
}

DWORD WINAPI GetShortPathNameA(LPCSTR long_path, LPSTR short_path, DWORD size)
{
    return path_name_a(long_path, short_path, size, GetShortPathNameW);
}

/* ------------------------------------------------------------------ current directory */

DWORD WINAPI GetCurrentDirectoryW(DWORD size, LPWSTR buffer)
{
    if (size > 0x7FFF)
        size = 0x7FFF;
    return RtlGetCurrentDirectory_U(size * sizeof(WCHAR), buffer) / sizeof(WCHAR);
}

DWORD WINAPI GetCurrentDirectoryA(DWORD size, LPSTR buffer)
{
    WCHAR w[MAX_PATH * 2];
    DWORD n = GetCurrentDirectoryW(RTL_NUMBER_OF(w), w);
    if (!n || n >= RTL_NUMBER_OF(w))
        return 0;
    return KbWideResultToAnsi(w, buffer, size);
}

BOOL WINAPI SetCurrentDirectoryW(LPCWSTR path)
{
    if (!path) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UNICODE_STRING us;
    RtlInitUnicodeString(&us, path);
    NTSTATUS status = RtlSetCurrentDirectory_U(&us);
    if (!NT_SUCCESS(status)) {
        if (status == STATUS_NOT_A_DIRECTORY)
            KbSetError(ERROR_DIRECTORY);
        else
            KbSetStatus(status);
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI SetCurrentDirectoryA(LPCSTR path)
{
    PWSTR w = KbAnsiToWide(path);
    if (!w) {
        if (!path)
            KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOL r = SetCurrentDirectoryW(w);
    KbFree(w);
    return r;
}

/* ------------------------------------------------------------------ system directories */

static void windows_directory(PWSTR out, SIZE_T chars)
{
    PCWSTR root = (PCWSTR)USER_SHARED_DATA->NtSystemRoot;
    SIZE_T n = wcsnlen(root, chars - 1);
    memcpy(out, root, n * sizeof(WCHAR));
    out[n] = 0;
}

UINT WINAPI GetWindowsDirectoryW(LPWSTR buffer, UINT size)
{
    WCHAR dir[MAX_PATH];
    windows_directory(dir, RTL_NUMBER_OF(dir));
    return copy_result(dir, buffer, size);
}

UINT WINAPI GetSystemWindowsDirectoryW(LPWSTR buffer, UINT size)
{
    return GetWindowsDirectoryW(buffer, size);
}

UINT WINAPI GetSystemDirectoryW(LPWSTR buffer, UINT size)
{
    WCHAR dir[MAX_PATH + 16];
    windows_directory(dir, MAX_PATH);
    wcscat(dir, L"\\system32");
    return copy_result(dir, buffer, size);
}

UINT WINAPI GetSystemWow64DirectoryW(LPWSTR buffer, UINT size)
{
    WCHAR dir[MAX_PATH + 16];
    windows_directory(dir, MAX_PATH);
    wcscat(dir, L"\\SysWOW64");
    return copy_result(dir, buffer, size);
}

static UINT directory_a(UINT(WINAPI *fn)(LPWSTR, UINT), LPSTR buffer, UINT size)
{
    WCHAR w[MAX_PATH + 16];
    UINT n = fn(w, RTL_NUMBER_OF(w));
    if (!n || n >= RTL_NUMBER_OF(w))
        return 0;
    return KbWideResultToAnsi(w, buffer, size);
}

UINT WINAPI GetWindowsDirectoryA(LPSTR buffer, UINT size)
{
    return directory_a(GetWindowsDirectoryW, buffer, size);
}

UINT WINAPI GetSystemWindowsDirectoryA(LPSTR buffer, UINT size)
{
    return directory_a(GetSystemWindowsDirectoryW, buffer, size);
}

UINT WINAPI GetSystemDirectoryA(LPSTR buffer, UINT size)
{
    return directory_a(GetSystemDirectoryW, buffer, size);
}

UINT WINAPI GetSystemWow64DirectoryA(LPSTR buffer, UINT size)
{
    return directory_a(GetSystemWow64DirectoryW, buffer, size);
}

/* ------------------------------------------------------------------ temporary files */

DWORD WINAPI GetTempPathW(DWORD size, LPWSTR buffer)
{
    WCHAR tmp[MAX_PATH], full[MAX_PATH + 2];
    static const PCWSTR vars[] = {L"TMP", L"TEMP", L"USERPROFILE"};
    DWORD n = 0;
    for (ULONG i = 0; i < RTL_NUMBER_OF(vars) && !n; i++) {
        n = GetEnvironmentVariableW(vars[i], tmp, RTL_NUMBER_OF(tmp));
        if (n >= RTL_NUMBER_OF(tmp))
            n = 0;
    }
    if (!n)
        n = GetWindowsDirectoryW(tmp, RTL_NUMBER_OF(tmp));
    if (!n || n >= RTL_NUMBER_OF(tmp))
        return 0;
    n = GetFullPathNameW(tmp, MAX_PATH, full, nullptr);
    if (!n || n >= MAX_PATH)
        return 0;
    if (full[n - 1] != '\\') {
        full[n++] = '\\';
        full[n] = 0;
    }
    if (n >= size || !buffer)
        return n + 1;
    memcpy(buffer, full, (n + 1) * sizeof(WCHAR));
    return n;
}

DWORD WINAPI GetTempPath2W(DWORD size, LPWSTR buffer)
{
    return GetTempPathW(size, buffer);
}

DWORD WINAPI GetTempPathA(DWORD size, LPSTR buffer)
{
    WCHAR w[MAX_PATH + 2];
    DWORD n = GetTempPathW(RTL_NUMBER_OF(w), w);
    if (!n || n >= RTL_NUMBER_OF(w))
        return 0;
    return KbWideResultToAnsi(w, buffer, size);
}

UINT WINAPI GetTempFileNameW(LPCWSTR path, LPCWSTR prefix, UINT unique, LPWSTR buffer)
{
    if (!path || !buffer) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    SIZE_T n = wcslen(path);
    if (n + 15 >= MAX_PATH) {
        KbSetError(ERROR_BUFFER_OVERFLOW);
        return 0;
    }
    WCHAR base[MAX_PATH];
    memcpy(base, path, n * sizeof(WCHAR));
    if (n && base[n - 1] != '\\' && base[n - 1] != '/')
        base[n++] = '\\';
    for (int i = 0; prefix && i < 3 && prefix[i]; i++)
        base[n++] = prefix[i];
    base[n] = 0;
    unique &= 0xFFFF;
    if (unique) {
        KbFormat(buffer, MAX_PATH, L"%s%X.TMP", base, unique);
        return unique;
    }
    UINT start = (GetTickCount() ^ GetCurrentThreadId()) & 0xFFFF;
    if (!start)
        start = 1;
    UINT num = start;
    do {
        KbFormat(buffer, MAX_PATH, L"%s%X.TMP", base, num);
        HANDLE h = CreateFileW(buffer, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, 0);
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            KbSetError(ERROR_SUCCESS);
            return num;
        }
        DWORD error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_SHARING_VIOLATION && error != ERROR_ACCESS_DENIED)
            return 0;
        num = (num + 1) & 0xFFFF;
        if (!num)
            num = 1;
    } while (num != start);
    KbSetError(ERROR_FILE_EXISTS);
    return 0;
}

UINT WINAPI GetTempFileNameA(LPCSTR path, LPCSTR prefix, UINT unique, LPSTR buffer)
{
    PWSTR wp = KbAnsiToWide(path), wx = KbAnsiToWide(prefix);
    WCHAR result[MAX_PATH];
    UINT r = 0;
    if (wp && (wx || !prefix)) {
        r = GetTempFileNameW(wp, wx, unique, result);
        if (r)
            WideCharToMultiByte(KbFileApiCodePage(), 0, result, -1, buffer, MAX_PATH, nullptr, nullptr);
    } else if (!path) {
        KbSetError(ERROR_INVALID_PARAMETER);
    }
    KbFree(wp);
    KbFree(wx);
    return r;
}

/* ------------------------------------------------------------------ searching */

static BOOL g_safe_search_mode = TRUE;

BOOL WINAPI SetSearchPathMode(DWORD flags)
{
    if (flags & BASE_SEARCH_PATH_ENABLE_SAFE_SEARCHMODE)
        g_safe_search_mode = TRUE;
    else if (flags & BASE_SEARCH_PATH_DISABLE_SAFE_SEARCHMODE)
        g_safe_search_mode = FALSE;
    else {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

/* the default search order: application directory, current directory, system, the Windows directory, then PATH. */
static PWSTR default_search_path(void)
{
    PRTL_USER_PROCESS_PARAMETERS pp = KbParams();
    WCHAR sys[MAX_PATH + 16], win[MAX_PATH];
    GetSystemDirectoryW(sys, RTL_NUMBER_OF(sys));
    GetWindowsDirectoryW(win, RTL_NUMBER_OF(win));
    SIZE_T app = pp->ImagePathName.Length / sizeof(WCHAR);
    while (app && pp->ImagePathName.Buffer[app - 1] != '\\')
        app--;
    DWORD path_chars = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    SIZE_T total = app + wcslen(sys) + wcslen(win) + path_chars + 16;
    PWSTR list = KbAlloc(total * sizeof(WCHAR));
    if (!list)
        return nullptr;
    SIZE_T n = 0;
    if (app) {
        memcpy(list, pp->ImagePathName.Buffer, app * sizeof(WCHAR));
        n = app;
        list[n++] = ';';
    }
    if (!g_safe_search_mode) {
        list[n++] = '.';
        list[n++] = ';';
    }
    n += (SIZE_T)KbFormat(list + n, total - n, L"%s;%s;", sys, win);
    if (g_safe_search_mode) {
        list[n++] = '.';
        list[n++] = ';';
    }
    if (path_chars)
        GetEnvironmentVariableW(L"PATH", list + n, (DWORD)(total - n));
    else
        list[n] = 0;
    return list;
}

DWORD WINAPI SearchPathW(LPCWSTR path, LPCWSTR name, LPCWSTR extension, DWORD size, LPWSTR buffer,
                         LPWSTR *file_part)
{
    if (!name || !*name) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (file_part)
        *file_part = nullptr;
    /* trailing blanks are not part of the name. */
    UNICODE_STRING file;
    RtlInitUnicodeString(&file, name);
    while (file.Length >= sizeof(WCHAR) && file.Buffer[file.Length / sizeof(WCHAR) - 1] == ' ')
        file.Length -= sizeof(WCHAR);
    if (!file.Length) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    PWSTR list = nullptr;
    ULONG flags = 6;
    if (!path) {
        list = default_search_path();
        if (!list) {
            KbSetError(ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }
        path = list;
        flags = 7;
    }
    UNICODE_STRING folders, ext, out = {0, (USHORT)(size > 0x7FFF ? 0xFFFE : size * sizeof(WCHAR)), buffer};
    RtlInitUnicodeString(&folders, path);
    RtlInitUnicodeString(&ext, extension);
    SIZE_T part = 0, needed = 0;
    NTSTATUS status = RtlDosSearchPath_Ustr(flags, &folders, &file, &ext, &out, nullptr, nullptr, &part, &needed);
    KbFree(list);
    if (status == STATUS_BUFFER_TOO_SMALL)
        return (DWORD)(needed / sizeof(WCHAR));
    if (!NT_SUCCESS(status)) {
        KbSetError(RtlNtStatusToDosError(status));
        return 0;
    }
    if (file_part)
        *file_part = buffer + part;
    return out.Length / sizeof(WCHAR);
}

DWORD WINAPI SearchPathA(LPCSTR path, LPCSTR name, LPCSTR extension, DWORD size, LPSTR buffer, LPSTR *file_part)
{
    PWSTR wp = KbAnsiToWide(path), wn = KbAnsiToWide(name), we = KbAnsiToWide(extension);
    DWORD r = 0;
    if (wn && (wp || !path) && (we || !extension)) {
        WCHAR result[MAX_PATH * 2];
        PWSTR part = nullptr;
        DWORD n = SearchPathW(wp, wn, we, RTL_NUMBER_OF(result), result, &part);
        if (n && n < RTL_NUMBER_OF(result)) {
            r = KbWideResultToAnsi(result, buffer, size);
            if (file_part && r < size && buffer) {
                if (part) {
                    int prefix = WideCharToMultiByte(KbFileApiCodePage(), 0, result, (int)(part - result), nullptr, 0,
                                                     nullptr, nullptr);
                    *file_part = buffer + prefix;
                } else {
                    *file_part = nullptr;
                }
            }
        }
    } else if (!name) {
        KbSetError(ERROR_INVALID_PARAMETER);
    }
    KbFree(wp);
    KbFree(wn);
    KbFree(we);
    return r;
}

BOOL WINAPI NeedCurrentDirectoryForExePathW(LPCWSTR name)
{
    if (name && wcschr(name, '\\'))
        return TRUE;
    return GetEnvironmentVariableW(L"NoDefaultCurrentDirectoryInExePath", nullptr, 0) == 0;
}

/* ------------------------------------------------------------------ drives */

static BOOL drive_exists(WCHAR letter)
{
    WCHAR path[] = L"\\??\\X:";
    path[4] = letter;
    UNICODE_STRING us;
    RtlInitUnicodeString(&us, path);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    HANDLE h;
    if (!NT_SUCCESS(NtOpenSymbolicLinkObject(&h, SYMBOLIC_LINK_QUERY, &oa)))
        return FALSE;
    NtClose(h);
    return TRUE;
}

DWORD WINAPI GetLogicalDrives(void)
{
    DWORD mask = 0;
    for (int i = 0; i < 26; i++)
        if (drive_exists((WCHAR)('A' + i)))
            mask |= 1u << i;
    return mask;
}

DWORD WINAPI GetLogicalDriveStringsW(DWORD size, LPWSTR buffer)
{
    DWORD mask = GetLogicalDrives();
    DWORD count = 0;
    for (int i = 0; i < 26; i++)
        if (mask & (1u << i))
            count++;
    DWORD need = count * 4;
    if (need + 1 > size || !buffer)
        return need + 1;
    LPWSTR p = buffer;
    for (int i = 0; i < 26; i++) {
        if (mask & (1u << i)) {
            *p++ = (WCHAR)('A' + i);
            *p++ = ':';
            *p++ = '\\';
            *p++ = 0;
        }
    }
    *p = 0;
    return need;
}

DWORD WINAPI GetLogicalDriveStringsA(DWORD size, LPSTR buffer)
{
    DWORD mask = GetLogicalDrives();
    DWORD count = 0;
    for (int i = 0; i < 26; i++)
        if (mask & (1u << i))
            count++;
    DWORD need = count * 4;
    if (need + 1 > size || !buffer)
        return need + 1;
    LPSTR p = buffer;
    for (int i = 0; i < 26; i++) {
        if (mask & (1u << i)) {
            *p++ = (CHAR)('A' + i);
            *p++ = ':';
            *p++ = '\\';
            *p++ = 0;
        }
    }
    *p = 0;
    return need;
}

/* the volume root of a path, such as C:\ or \\server\share\. */
static BOOL volume_root(LPCWSTR path, PWSTR root, SIZE_T chars)
{
    WCHAR full[MAX_PATH * 2];
    if (!path) {
        if (!GetCurrentDirectoryW(RTL_NUMBER_OF(full), full))
            return FALSE;
        path = full;
    }
    if (path[0] && path[1] == ':') {
        if (chars < 4)
            return FALSE;
        root[0] = (WCHAR)towupper(path[0]);
        root[1] = ':';
        root[2] = '\\';
        root[3] = 0;
        return TRUE;
    }
    if (path[0] == '\\' && path[1] == '\\') {
        SIZE_T i = 2, seps = 0;
        while (path[i] && seps < 2 && i + 2 < chars) {
            if (path[i] == '\\' || path[i] == '/')
                seps++;
            root[i] = path[i] == '/' ? '\\' : path[i];
            i++;
        }
        root[0] = root[1] = '\\';
        if (seps < 2)
            root[i++] = '\\';
        root[i] = 0;
        return TRUE;
    }
    if (!GetCurrentDirectoryW(RTL_NUMBER_OF(full), full))
        return FALSE;
    return volume_root(full, root, chars);
}

static NTSTATUS open_root(LPCWSTR root, PHANDLE h)
{
    UNICODE_STRING nt;
    if (!RtlDosPathNameToNtPathName_U(root, &nt, nullptr, nullptr))
        return STATUS_OBJECT_PATH_INVALID;
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &nt, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = NtOpenFile(h, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &oa, &iosb,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT);
    RtlFreeUnicodeString(&nt);
    return status;
}

UINT WINAPI GetDriveTypeW(LPCWSTR root)
{
    WCHAR r[MAX_PATH];
    if (!volume_root(root, r, RTL_NUMBER_OF(r)))
        return DRIVE_NO_ROOT_DIR;
    if (r[1] == ':' && !drive_exists(r[0]))
        return DRIVE_NO_ROOT_DIR;
    HANDLE h;
    if (!NT_SUCCESS(open_root(r, &h)))
        return r[0] == '\\' ? DRIVE_NO_ROOT_DIR : DRIVE_UNKNOWN;
    FILE_FS_DEVICE_INFORMATION info;
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = NtQueryVolumeInformationFile(h, &iosb, &info, sizeof(info), FileFsDeviceInformation);
    NtClose(h);
    if (!NT_SUCCESS(status))
        return DRIVE_FIXED;
    if (info.DeviceType == FILE_DEVICE_CD_ROM)
        return DRIVE_CDROM;
    if (info.Characteristics & FILE_REMOTE_DEVICE)
        return DRIVE_REMOTE;
    if (info.Characteristics & FILE_REMOVABLE_MEDIA)
        return DRIVE_REMOVABLE;
    return DRIVE_FIXED;
}

UINT WINAPI GetDriveTypeA(LPCSTR root)
{
    if (!root)
        return GetDriveTypeW(nullptr);
    PWSTR w = KbAnsiToWide(root);
    if (!w)
        return DRIVE_NO_ROOT_DIR;
    UINT r = GetDriveTypeW(w);
    KbFree(w);
    return r;
}

/* ------------------------------------------------------------------ volume information */

BOOL WINAPI GetVolumeInformationByHandleW(HANDLE file, LPWSTR name, DWORD name_size, LPDWORD serial,
                                          LPDWORD max_component, LPDWORD flags, LPWSTR fs_name, DWORD fs_name_size)
{
    IO_STATUS_BLOCK iosb;
    if (name || serial) {
        UCHAR buf[sizeof(KB_FILE_FS_VOLUME_INFORMATION) + MAX_PATH * sizeof(WCHAR)];
        NTSTATUS status = NtQueryVolumeInformationFile(file, &iosb, buf, sizeof(buf), FileFsVolumeInformation);
        if (!NT_SUCCESS(status))
            return KbSetStatus(status);
        KB_FILE_FS_VOLUME_INFORMATION *v = (KB_FILE_FS_VOLUME_INFORMATION *)buf;
        if (serial)
            *serial = v->VolumeSerialNumber;
        if (name) {
            DWORD n = v->VolumeLabelLength / sizeof(WCHAR);
            if (n >= name_size) {
                KbSetError(ERROR_BAD_LENGTH);
                return FALSE;
            }
            memcpy(name, v->VolumeLabel, n * sizeof(WCHAR));
            name[n] = 0;
        }
    }
    if (max_component || flags || fs_name) {
        UCHAR buf[sizeof(KB_FILE_FS_ATTRIBUTE_INFORMATION) + 64 * sizeof(WCHAR)];
        NTSTATUS status = NtQueryVolumeInformationFile(file, &iosb, buf, sizeof(buf), FileFsAttributeInformation);
        if (!NT_SUCCESS(status))
            return KbSetStatus(status);
        KB_FILE_FS_ATTRIBUTE_INFORMATION *a = (KB_FILE_FS_ATTRIBUTE_INFORMATION *)buf;
        if (max_component)
            *max_component = (DWORD)a->MaximumComponentNameLength;
        if (flags)
            *flags = a->FileSystemAttributes;
        if (fs_name) {
            DWORD n = a->FileSystemNameLength / sizeof(WCHAR);
            if (n >= fs_name_size) {
                KbSetError(ERROR_BAD_LENGTH);
                return FALSE;
            }
            memcpy(fs_name, a->FileSystemName, n * sizeof(WCHAR));
            fs_name[n] = 0;
        }
    }
    return TRUE;
}

BOOL WINAPI GetVolumeInformationW(LPCWSTR root, LPWSTR name, DWORD name_size, LPDWORD serial, LPDWORD max_component,
                                  LPDWORD flags, LPWSTR fs_name, DWORD fs_name_size)
{
    WCHAR r[MAX_PATH];
    if (root) {
        SIZE_T n = wcslen(root);
        if (!n || (root[n - 1] != '\\' && root[n - 1] != '/')) {
            KbSetError(ERROR_INVALID_NAME);
            return FALSE;
        }
    }
    if (!volume_root(root, r, RTL_NUMBER_OF(r))) {
        KbSetError(ERROR_INVALID_NAME);
        return FALSE;
    }
    HANDLE h;
    NTSTATUS status = open_root(r, &h);
    if (!NT_SUCCESS(status)) {
        KbSetError(status == STATUS_OBJECT_NAME_NOT_FOUND || status == STATUS_OBJECT_PATH_NOT_FOUND
                       ? ERROR_PATH_NOT_FOUND
                       : RtlNtStatusToDosError(status));
        return FALSE;
    }
    BOOL ok = GetVolumeInformationByHandleW(h, name, name_size, serial, max_component, flags, fs_name, fs_name_size);
    NtClose(h);
    return ok;
}

BOOL WINAPI GetVolumeInformationA(LPCSTR root, LPSTR name, DWORD name_size, LPDWORD serial, LPDWORD max_component,
                                  LPDWORD flags, LPSTR fs_name, DWORD fs_name_size)
{
    PWSTR wroot = KbAnsiToWide(root);
    if (root && !wroot)
        return FALSE;
    WCHAR wname[MAX_PATH + 1], wfs[MAX_PATH + 1];
    BOOL ok = GetVolumeInformationW(wroot, name ? wname : nullptr, RTL_NUMBER_OF(wname), serial, max_component, flags,
                                    fs_name ? wfs : nullptr, RTL_NUMBER_OF(wfs));
    KbFree(wroot);
    if (!ok)
        return FALSE;
    UINT cp = KbFileApiCodePage();
    if (name && !WideCharToMultiByte(cp, 0, wname, -1, name, (int)name_size, nullptr, nullptr)) {
        KbSetError(ERROR_BAD_LENGTH);
        return FALSE;
    }
    if (fs_name && !WideCharToMultiByte(cp, 0, wfs, -1, fs_name, (int)fs_name_size, nullptr, nullptr)) {
        KbSetError(ERROR_BAD_LENGTH);
        return FALSE;
    }
    return TRUE;
}

/* the label of the volume whose root the path names, or of the current drive. */
BOOL WINAPI SetVolumeLabelW(LPCWSTR root, LPCWSTR label)
{
    WCHAR r[MAX_PATH];
    if (root) {
        SIZE_T n = wcslen(root);
        if (!n || (root[n - 1] != '\\' && root[n - 1] != '/')) {
            KbSetError(ERROR_INVALID_NAME);
            return FALSE;
        }
    }
    if (!volume_root(root, r, RTL_NUMBER_OF(r))) {
        KbSetError(ERROR_PATH_NOT_FOUND);
        return FALSE;
    }
    HANDLE h;
    NTSTATUS status = open_root(r, &h);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    SIZE_T chars = label ? wcslen(label) : 0;
    ULONG size = (ULONG)(sizeof(ULONG) + (chars ? chars : 1) * sizeof(WCHAR));
    FILE_FS_LABEL_INFORMATION *info = KbAllocZero(size);
    if (!info) {
        NtClose(h);
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    info->VolumeLabelLength = (ULONG)(chars * sizeof(WCHAR));
    memcpy(info->VolumeLabel, label, chars * sizeof(WCHAR));
    IO_STATUS_BLOCK iosb;
    status = NtSetVolumeInformationFile(h, &iosb, info, size, FileFsLabelInformation);
    KbFree(info);
    NtClose(h);
    return NT_SUCCESS(status) ? TRUE : KbSetStatus(status);
}

BOOL WINAPI SetVolumeLabelA(LPCSTR root, LPCSTR label)
{
    PWSTR wide_root = root ? KbAnsiToWide(root) : nullptr, wide_label = label ? KbAnsiToWide(label) : nullptr;
    BOOL ok = FALSE;
    if ((wide_root || !root) && (wide_label || !label))
        ok = SetVolumeLabelW(wide_root, wide_label);
    KbFree(wide_root);
    KbFree(wide_label);
    return ok;
}

BOOL WINAPI GetVolumePathNameW(LPCWSTR path, LPWSTR volume, DWORD size)
{
    if (!path || !volume || !size) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR full[MAX_PATH * 2], root[MAX_PATH];
    DWORD n = GetFullPathNameW(path, RTL_NUMBER_OF(full), full, nullptr);
    if (!n || n >= RTL_NUMBER_OF(full) || !volume_root(full, root, RTL_NUMBER_OF(root)))
        return FALSE;
    if (wcslen(root) >= size) {
        KbSetError(ERROR_FILENAME_EXCED_RANGE);
        return FALSE;
    }
    wcscpy(volume, root);
    return TRUE;
}

BOOL WINAPI GetDiskFreeSpaceExW(LPCWSTR root, PULARGE_INTEGER available, PULARGE_INTEGER total, PULARGE_INTEGER free)
{
    WCHAR dir[MAX_PATH * 2];
    if (root) {
        SIZE_T n = wcslen(root);
        if (n >= RTL_NUMBER_OF(dir) - 2) {
            KbSetError(ERROR_INVALID_NAME);
            return FALSE;
        }
        wcscpy(dir, root);
        if (n && dir[n - 1] != '\\' && dir[n - 1] != '/')
            wcscat(dir, L"\\");
    } else if (!volume_root(nullptr, dir, RTL_NUMBER_OF(dir))) {
        return FALSE;
    }
    HANDLE h;
    NTSTATUS status = open_root(dir, &h);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    KB_FILE_FS_FULL_SIZE_INFORMATION info;
    IO_STATUS_BLOCK iosb;
    status = NtQueryVolumeInformationFile(h, &iosb, &info, sizeof(info), (FS_INFORMATION_CLASS)FileFsFullSizeInformation);
    NtClose(h);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    ULONGLONG unit = (ULONGLONG)info.SectorsPerAllocationUnit * info.BytesPerSector;
    if (available)
        available->QuadPart = (ULONGLONG)info.CallerAvailableAllocationUnits.QuadPart * unit;
    if (total)
        total->QuadPart = (ULONGLONG)info.TotalAllocationUnits.QuadPart * unit;
    if (free)
        free->QuadPart = (ULONGLONG)info.ActualAvailableAllocationUnits.QuadPart * unit;
    return TRUE;
}

BOOL WINAPI GetDiskFreeSpaceExA(LPCSTR root, PULARGE_INTEGER available, PULARGE_INTEGER total, PULARGE_INTEGER free)
{
    PWSTR w = KbAnsiToWide(root);
    if (root && !w)
        return FALSE;
    BOOL r = GetDiskFreeSpaceExW(w, available, total, free);
    KbFree(w);
    return r;
}

BOOL WINAPI GetDiskFreeSpaceW(LPCWSTR root, LPDWORD sectors_per_cluster, LPDWORD bytes_per_sector,
                              LPDWORD free_clusters, LPDWORD total_clusters)
{
    ULARGE_INTEGER available, total, free;
    if (!GetDiskFreeSpaceExW(root, &available, &total, &free))
        return FALSE;
    ULONGLONG cluster = 4096;
    ULONGLONG fc = free.QuadPart / cluster, tc = total.QuadPart / cluster;
    if (sectors_per_cluster)
        *sectors_per_cluster = 8;
    if (bytes_per_sector)
        *bytes_per_sector = 512;
    if (free_clusters)
        *free_clusters = fc > 0xFFFFFFFF ? 0xFFFFFFFF : (DWORD)fc;
    if (total_clusters)
        *total_clusters = tc > 0xFFFFFFFF ? 0xFFFFFFFF : (DWORD)tc;
    return TRUE;
}

BOOL WINAPI GetDiskFreeSpaceA(LPCSTR root, LPDWORD sectors_per_cluster, LPDWORD bytes_per_sector, LPDWORD free_clusters,
                              LPDWORD total_clusters)
{
    PWSTR w = KbAnsiToWide(root);
    if (root && !w)
        return FALSE;
    BOOL r = GetDiskFreeSpaceW(w, sectors_per_cluster, bytes_per_sector, free_clusters, total_clusters);
    KbFree(w);
    return r;
}

DWORD WINAPI QueryDosDeviceW(LPCWSTR device, LPWSTR target, DWORD size)
{
    if (!target || !size) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    if (!device) {
        /* without directory enumeration in the kernel, list the drive letters. */
        DWORD mask = GetLogicalDrives();
        DWORD n = 0;
        for (int i = 0; i < 26; i++) {
            if (!(mask & (1u << i)))
                continue;
            if (n + 4 > size) {
                KbSetError(ERROR_INSUFFICIENT_BUFFER);
                return 0;
            }
            target[n++] = (WCHAR)('A' + i);
            target[n++] = ':';
            target[n++] = 0;
        }
        if (n + 1 > size) {
            KbSetError(ERROR_INSUFFICIENT_BUFFER);
            return 0;
        }
        target[n++] = 0;
        return n;
    }
    WCHAR path[MAX_PATH];
    KbFormat(path, RTL_NUMBER_OF(path), L"\\??\\%s", device);
    UNICODE_STRING us;
    RtlInitUnicodeString(&us, path);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    HANDLE h;
    NTSTATUS status = NtOpenSymbolicLinkObject(&h, SYMBOLIC_LINK_QUERY, &oa);
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status);
        return 0;
    }
    WCHAR buf[MAX_PATH];
    UNICODE_STRING result = {0, sizeof(buf) - sizeof(WCHAR), buf};
    ULONG returned;
    status = NtQuerySymbolicLinkObject(h, &result, &returned);
    NtClose(h);
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status);
        return 0;
    }
    DWORD n = result.Length / sizeof(WCHAR);
    if (n + 2 > size) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    memcpy(target, buf, n * sizeof(WCHAR));
    target[n] = 0;
    target[n + 1] = 0;
    return n + 2;
}

DWORD WINAPI QueryDosDeviceA(LPCSTR device, LPSTR target, DWORD size)
{
    PWSTR w = KbAnsiToWide(device);
    if (device && !w)
        return 0;
    if (!target || !size) {
        KbFree(w);
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    PWSTR buf = KbAlloc(size * sizeof(WCHAR));
    if (!buf) {
        KbFree(w);
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    DWORD n = QueryDosDeviceW(w, buf, size);
    DWORD r = 0;
    if (n) {
        r = WideCharToMultiByte(CP_ACP, 0, buf, (int)n, target, (int)size, nullptr, nullptr);
        if (!r)
            KbSetError(ERROR_INSUFFICIENT_BUFFER);
    }
    KbFree(buf);
    KbFree(w);
    return r;
}

static BOOL dos_device_name(LPCWSTR device, PWSTR path, DWORD chars)
{
    if (!device || !*device || wcschr(device, '\\') || wcslen(device) + 5 > chars)
        return FALSE;
    KbFormat(path, chars, L"\\??\\%s", device);
    /* a trailing backslash after a drive letter names the same device. */
    size_t len = wcslen(path);
    if (len > 4 && path[len - 1] == '\\')
        path[len - 1] = 0;
    return TRUE;
}

static BOOL dos_device_target(DWORD flags, LPCWSTR target, PUNICODE_STRING nt)
{
    if (flags & DDD_RAW_TARGET_PATH)
        return RtlCreateUnicodeString(nt, target);
    return RtlDosPathNameToNtPathName_U(target, nt, nullptr, nullptr);
}

static BOOL remove_dos_device(DWORD flags, LPCWSTR path, LPCWSTR target)
{
    UNICODE_STRING us;
    RtlInitUnicodeString(&us, path);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    HANDLE h;
    NTSTATUS status = NtOpenSymbolicLinkObject(&h, SYMBOLIC_LINK_QUERY | DELETE, &oa);
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status);
        return FALSE;
    }
    if (target && *target && (flags & DDD_EXACT_MATCH_ON_REMOVE)) {
        WCHAR buf[MAX_PATH];
        UNICODE_STRING current = {0, sizeof(buf) - sizeof(WCHAR), buf}, wanted;
        ULONG returned;
        status = NtQuerySymbolicLinkObject(h, &current, &returned);
        if (NT_SUCCESS(status) && !dos_device_target(flags, target, &wanted))
            status = STATUS_OBJECT_PATH_INVALID;
        else if (NT_SUCCESS(status)) {
            if (!RtlEqualUnicodeString(&current, &wanted, TRUE))
                status = STATUS_OBJECT_NAME_NOT_FOUND;
            RtlFreeUnicodeString(&wanted);
        }
        if (!NT_SUCCESS(status)) {
            NtClose(h);
            KbSetStatus(status);
            return FALSE;
        }
    }
    status = NtMakeTemporaryObject(h);
    NtClose(h);
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status);
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI DefineDosDeviceW(DWORD flags, LPCWSTR device, LPCWSTR target)
{
    WCHAR path[MAX_PATH];
    if (!dos_device_name(device, path, RTL_NUMBER_OF(path))) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (flags & DDD_REMOVE_DEFINITION)
        return remove_dos_device(flags, path, target);
    if (!target || !*target) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UNICODE_STRING nt;
    if (!dos_device_target(flags, target, &nt)) {
        KbSetError(ERROR_PATH_NOT_FOUND);
        return FALSE;
    }
    /* Windows stacks definitions, here a new one replaces the current one. */
    UNICODE_STRING us;
    RtlInitUnicodeString(&us, path);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    HANDLE h;
    if (NT_SUCCESS(NtOpenSymbolicLinkObject(&h, DELETE, &oa))) {
        NtMakeTemporaryObject(h);
        NtClose(h);
    }
    oa.Attributes |= OBJ_PERMANENT;
    NTSTATUS status = NtCreateSymbolicLinkObject(&h, SYMBOLIC_LINK_ALL_ACCESS, &oa, &nt);
    RtlFreeUnicodeString(&nt);
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status);
        return FALSE;
    }
    NtClose(h);
    return TRUE;
}

BOOL WINAPI DefineDosDeviceA(DWORD flags, LPCSTR device, LPCSTR target)
{
    PWSTR wd = KbAnsiToWide(device), wt = KbAnsiToWide(target);
    BOOL r = FALSE;
    if ((!device || wd) && (!target || wt))
        r = DefineDosDeviceW(flags, wd, wt);
    KbFree(wd);
    KbFree(wt);
    return r;
}

/*
 * a 32-bit program turns System32 redirection off by setting a word in its teb, which the translator reads, see
 * wow32.c. Native programs are never redirected.
 */
#ifdef __i386__
#include <mime/wow32.h>
#define FILESYSREDIR (*(PVOID *)((PUCHAR)NtCurrentTeb() + MIME_TEB32_FSREDIR_OFFSET))

BOOL WINAPI Wow64DisableWow64FsRedirection(PVOID *old)
{
    if (!old) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *old = FILESYSREDIR;
    FILESYSREDIR = (PVOID)1;
    return TRUE;
}

BOOL WINAPI Wow64RevertWow64FsRedirection(PVOID old)
{
    FILESYSREDIR = old;
    return TRUE;
}

BOOLEAN WINAPI Wow64EnableWow64FsRedirection(BOOLEAN enable)
{
    FILESYSREDIR = enable ? nullptr : (PVOID)1;
    return TRUE;
}
#else
BOOL WINAPI Wow64DisableWow64FsRedirection(PVOID *old)
{
    if (old)
        *old = nullptr;
    return TRUE;
}

BOOL WINAPI Wow64RevertWow64FsRedirection(PVOID old)
{
    return TRUE;
}

BOOLEAN WINAPI Wow64EnableWow64FsRedirection(BOOLEAN enable)
{
    return TRUE;
}
#endif
