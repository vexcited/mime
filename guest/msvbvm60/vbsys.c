/*
 * the system functions of the library: directories and files by name, Shell, Environ, Command, and the settings of a
 * program in the registry.
 */
#include "vb.h"

/* ------------------------------------------------------------------ Dir */

static struct {
    HANDLE find;
    DWORD attributes;
    BOOL volume;
} g_dir = {INVALID_HANDLE_VALUE, 0, FALSE};

static BSTR new_string(const WCHAR *s)
{
    BSTR b = SysAllocString(s);
    if (!b)
        VbRaise(VBERR_OUT_OF_STRING_SPACE);
    return b;
}

/* the next entry that the attributes allow: read only and archive always, the others when asked for. */
static BSTR next_entry(WIN32_FIND_DATAW *data, BOOL have)
{
    while (have) {
        if (!(~g_dir.attributes & data->dwFileAttributes & 0x16))
            return new_string(data->cFileName);
        have = FindNextFileW(g_dir.find, data);
    }
    DWORD error = GetLastError();
    if (g_dir.find != INVALID_HANDLE_VALUE)
        FindClose(g_dir.find);
    g_dir.find = INVALID_HANDLE_VALUE;
    if (error != ERROR_NO_MORE_FILES && error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND && error != ERROR_INVALID_NAME &&
        error != ERROR_BAD_NETPATH)
        VbRaise(error == ERROR_BAD_PATHNAME ? VBERR_BAD_FILE_NAME : VBERR_INVALID_CALL);
    return new_string(L"");
}

static BSTR dir_first(const WCHAR *pattern, int attributes)
{
    if (attributes & 0xC0)
        VbRaise(VBERR_INVALID_CALL);
    if (!attributes)
        attributes = 0x21;
    if (g_dir.find != INVALID_HANDLE_VALUE)
        FindClose(g_dir.find);
    g_dir.find = INVALID_HANDLE_VALUE;
    g_dir.attributes = (DWORD)attributes;
    if (attributes == 8) {
        WCHAR root[MAX_PATH], label[MAX_PATH];
        lstrcpynW(root, pattern, MAX_PATH);
        if (root[0] && root[1] == L':')
            root[2] = L'\\', root[3] = 0;
        label[0] = 0;
        if (!GetVolumeInformationW(root[0] ? root : nullptr, label, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0))
            return new_string(L"");
        return new_string(label);
    }
    WCHAR full[MAX_PATH + 8];
    UINT n = (UINT)lstrlenW(pattern);
    lstrcpynW(full, pattern, MAX_PATH);
    if (!n || pattern[n - 1] == L'\\' || pattern[n - 1] == L'/' || pattern[n - 1] == L':')
        lstrcatW(full, L"*.*");
    WIN32_FIND_DATAW data;
    g_dir.find = FindFirstFileW(full, &data);
    if (g_dir.find == INVALID_HANDLE_VALUE)
        return next_entry(&data, FALSE);
    return next_entry(&data, TRUE);
}

static BSTR dir_next(void)
{
    WIN32_FIND_DATAW data;
    if (g_dir.find == INVALID_HANDLE_VALUE)
        VbRaise(VBERR_INVALID_CALL);
    return next_entry(&data, FindNextFileW(g_dir.find, &data));
}

VB_STD BSTR vb_rtcDir(VARIANT *pattern, int attributes)
{
    if (VbIsMissing(pattern))
        return dir_next();
    VARIANT value;
    VbValue(&value, pattern);
    if (V_VT(&value) != VT_BSTR) {
        VARTYPE vt = V_VT(&value);
        VariantClear(&value);
        VbRaise(vt > VT_UI1 && vt != 36 && !(vt & VT_ARRAY) ? 458 : VBERR_TYPE_MISMATCH);
    }
    BSTR result = dir_first(V_BSTR(&value) ? V_BSTR(&value) : L"", attributes);
    VariantClear(&value);
    return result;
}

/* ------------------------------------------------------------------ the directory of a drive */

static BSTR current_dir(const WCHAR *drive)
{
    WCHAR spec[4] = {0, L':', 0, 0}, path[MAX_PATH];
    if (drive && drive[0]) {
        spec[0] = drive[0];
        if (!GetFullPathNameW(spec, MAX_PATH, path, nullptr))
            VbRaise(68);
        if (GetDriveTypeW((WCHAR[]){path[0], L':', L'\\', 0}) <= DRIVE_NO_ROOT_DIR)
            VbRaise(68);
    } else if (!GetCurrentDirectoryW(MAX_PATH, path)) {
        VbRaise(VBERR_IO);
    }
    return new_string(path);
}

VB_STD BSTR vb_rtcCurrentDirBstr(VARIANT *drive)
{
    if (VbIsMissing(drive))
        return current_dir(nullptr);
    BSTR text;
    if (!VbText(drive, &text))
        VbRaise(VBERR_INVALID_USE_OF_NULL);
    BSTR result = current_dir(text);
    SysFreeString(text);
    return result;
}

VB_STD VARIANT *vb_rtcCurrentDir(VARIANT *dest, VARIANT *drive)
{
    BSTR text = vb_rtcCurrentDirBstr(drive);
    VariantInit(dest);
    V_VT(dest) = VT_BSTR;
    V_BSTR(dest) = text;
    return dest;
}

VB_STD void vb_rtcChangeDir(BSTR path)
{
    if (!path || !SetCurrentDirectoryW(path))
        VbRaise(path ? VBERR_PATH_NOT_FOUND : VBERR_BAD_FILE_NAME);
}

/* a directory that is not there is a path that is not found, not a file. */
static LONG directory_error(DWORD error)
{
    return error == ERROR_FILE_NOT_FOUND ? VBERR_PATH_NOT_FOUND : VbErrorOfWin32(error);
}

VB_STD void vb_rtcMakeDir(BSTR path)
{
    if (!path || !CreateDirectoryW(path, nullptr)) {
        DWORD error = GetLastError();
        VbRaise(error == ERROR_ALREADY_EXISTS ? 75 : directory_error(error));
    }
}

VB_STD void vb_rtcRemoveDir(BSTR path)
{
    if (!path || !RemoveDirectoryW(path)) {
        DWORD error = GetLastError();
        VbRaise(error == ERROR_DIR_NOT_EMPTY || error == ERROR_ACCESS_DENIED ? 75 : directory_error(error));
    }
}

VB_STD void vb_rtcChangeDrive(BSTR drive)
{
    if (!drive || !drive[0])
        VbRaise(VBERR_INVALID_CALL);
    WCHAR spec[3] = {drive[0], L':', 0}, path[MAX_PATH];
    if (!GetFullPathNameW(spec, MAX_PATH, path, nullptr) || !SetCurrentDirectoryW(path))
        VbRaise(68);
}

/* ------------------------------------------------------------------ files by name */

static BSTR file_name(VARIANT *name)
{
    BSTR text;
    if (!VbText(name, &text))
        VbRaise(VBERR_INVALID_USE_OF_NULL);
    return text;
}

VB_STD void vb_rtcKillFiles(VARIANT *pattern)
{
    BSTR text = file_name(pattern);
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(text, &data);
    WCHAR directory[MAX_PATH], path[MAX_PATH];
    BOOL any = FALSE;
    lstrcpynW(directory, text, MAX_PATH);
    SysFreeString(text);
    WCHAR *slash = directory + lstrlenW(directory);
    while (slash > directory && slash[-1] != L'\\' && slash[-1] != L'/' && slash[-1] != L':')
        slash--;
    *slash = 0;
    if (find == INVALID_HANDLE_VALUE)
        VbRaise(VBERR_FILE_NOT_FOUND);
    do {
        if (data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | 0x2 | 0x4))
            continue;
        lstrcpyW(path, directory);
        lstrcatW(path, data.cFileName);
        any = TRUE;
        if (!DeleteFileW(path)) {
            DWORD error = GetLastError();
            FindClose(find);
            VbRaise(error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION ? (error == ERROR_SHARING_VIOLATION ? 55 : 75) : VbErrorOfWin32(error));
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    if (!any)
        VbRaise(VBERR_FILE_NOT_FOUND);
}

static void file_data(BSTR name, WIN32_FILE_ATTRIBUTE_DATA *data)
{
    if (!name || !name[0])
        VbRaise(VBERR_BAD_FILE_NAME);
    if (!GetFileAttributesExW(name, GetFileExInfoStandard, data)) {
        DWORD error = GetLastError();
        VbRaise(error == ERROR_ACCESS_DENIED ? 70 : VbErrorOfWin32(error));
    }
}

VB_STD LONG vb_rtcFileLen(BSTR name)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    file_data(name, &data);
    return (LONG)data.nFileSizeLow;
}

VB_STD VARIANT *vb_rtcFileDateTime(VARIANT *dest, BSTR name)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    FILETIME local;
    SYSTEMTIME st;
    DATE date = 0;
    file_data(name, &data);
    if (!FileTimeToLocalFileTime(&data.ftLastWriteTime, &local) || !FileTimeToSystemTime(&local, &st))
        VbRaise(VBERR_INVALID_CALL);
    SystemTimeToVariantTime(&st, &date);
    VariantInit(dest);
    V_VT(dest) = VT_DATE;
    V_DATE(dest) = date;
    return dest;
}

VB_STD LONG vb_rtcGetFileAttr(BSTR name)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    file_data(name, &data);
    return data.dwFileAttributes == FILE_ATTRIBUTE_NORMAL ? 0 : (LONG)(data.dwFileAttributes & 0xFF);
}

VB_STD void vb_rtcSetFileAttr(BSTR name, DWORD attributes)
{
    if (!name || !name[0])
        VbRaise(VBERR_BAD_FILE_NAME);
    if ((attributes | 0x27) != 0x27)
        VbRaise(VBERR_INVALID_CALL);
    if (!SetFileAttributesW(name, attributes ? attributes : FILE_ATTRIBUTE_NORMAL)) {
        DWORD error = GetLastError();
        VbRaise(error == ERROR_ACCESS_DENIED || error == 65 || error == ERROR_INVALID_PARAMETER || error == ERROR_INVALID_DRIVE
                    ? 75
                    : VBERR_FILE_NOT_FOUND);
    }
}

VB_STD void vb_rtcFileCopy(BSTR from, BSTR to)
{
    if (!from || !from[0] || !to || !to[0])
        VbRaise(VBERR_BAD_FILE_NAME);
    if (!CopyFileW(from, to, FALSE)) {
        DWORD error = GetLastError();
        VbRaise(error == ERROR_SHARING_VIOLATION ? 70 : error == ERROR_ACCESS_DENIED ? 75 : VbErrorOfWin32(error));
    }
}

VB_STD void vb___vbaNameFile(BSTR from, BSTR to)
{
    if (!from || !from[0] || !to || !to[0])
        VbRaise(VBERR_BAD_FILE_NAME);
    if (!MoveFileW(from, to)) {
        DWORD error = GetLastError();
        VbRaise(error == ERROR_NOT_SAME_DEVICE ? 74 : error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS ? 58 : VbErrorOfWin32(error));
    }
}

VB_STD void vb_rtcBeep(void) { MessageBeep(MB_OK); }

/* ------------------------------------------------------------------ Shell, Environ and Command */

VB_STD double vb_rtcShell(VARIANT *command, SHORT style)
{
    STARTUPINFOW startup = {};
    PROCESS_INFORMATION process;
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = (WORD)style;
    if ((USHORT)style > 9)
        VbRaise(VBERR_INVALID_CALL);
    BSTR text;
    if (!VbText(command, &text) || !text)
        VbRaise(VBERR_INVALID_CALL);
    UINT n = SysStringLen(text);
    WCHAR *line = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (n + 1) * sizeof(WCHAR));
    if (!line)
        VbRaise(VBERR_OUT_OF_MEMORY);
    memcpy(line, text, n * sizeof(WCHAR));
    line[n] = 0;
    SysFreeString(text);
    BOOL ok = CreateProcessW(nullptr, line, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process);
    DWORD error = GetLastError();
    HeapFree(GetProcessHeap(), 0, line);
    if (!ok)
        VbRaise(error == ERROR_FILE_NOT_FOUND ? VBERR_FILE_NOT_FOUND
                : error == ERROR_PATH_NOT_FOUND ? VBERR_PATH_NOT_FOUND
                : error == ERROR_NOT_ENOUGH_MEMORY || error == ERROR_OUTOFMEMORY ? VBERR_OUT_OF_MEMORY
                                                                                 : VBERR_INVALID_CALL);
    WaitForInputIdle(process.hProcess, 10000);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return (double)(LONG)process.dwProcessId;
}

BSTR VbEnvironment(const VARIANT *key)
{
    VARIANT value;
    VbValue(&value, key);
    if (V_VT(&value) == VT_NULL)
        VbRaise(VBERR_INVALID_USE_OF_NULL);
    BSTR result;
    if (V_VT(&value) == VT_BSTR) {
        WCHAR *name = V_BSTR(&value);
        UINT n = name ? SysStringLen(name) : 0;
        while (n && name[n - 1] == L' ')
            n--;
        while (n && *name == L' ')
            name++, n--;
        if (!n) {
            VariantClear(&value);
            VbRaise(VBERR_INVALID_CALL);
        }
        WCHAR *copy = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (n + 1) * sizeof(WCHAR));
        WCHAR buffer[1];
        memcpy(copy, name, n * sizeof(WCHAR));
        copy[n] = 0;
        DWORD need = GetEnvironmentVariableW(copy, buffer, 0);
        if (!need) {
            result = new_string(L"");
        } else {
            result = SysAllocStringLen(nullptr, need - 1);
            if (result)
                GetEnvironmentVariableW(copy, result, need);
        }
        HeapFree(GetProcessHeap(), 0, copy);
    } else {
        VARIANT number;
        VariantInit(&number);
        VbCheck(VariantChangeTypeEx(&number, &value, VbLocale(), 0, VT_I2));
        int index = V_I2(&number);
        if (index <= 0 || index > 255) {
            VariantClear(&value);
            VbRaise(VBERR_INVALID_CALL);
        }
        WCHAR *block = GetEnvironmentStringsW(), *at = block;
        result = nullptr;
        int seen = 0;
        while (at && *at) {
            int len = lstrlenW(at);
            if (*at != L'=' && ++seen == index) {
                result = SysAllocStringLen(at, (UINT)len);
                break;
            }
            at += len + 1;
        }
        if (!result)
            result = new_string(L"");
        if (block)
            FreeEnvironmentStringsW(block);
    }
    VariantClear(&value);
    if (!result)
        VbRaise(VBERR_OUT_OF_STRING_SPACE);
    return result;
}

VB_STD BSTR vb_rtcEnvironBstr(VARIANT *key) { return VbEnvironment(key); }

VB_STD VARIANT *vb_rtcEnvironVar(VARIANT *dest, VARIANT *key)
{
    BSTR text = VbEnvironment(key);
    VariantInit(dest);
    V_VT(dest) = VT_BSTR;
    V_BSTR(dest) = text;
    return dest;
}

VB_STD BSTR vb_rtcCommandBstr(void)
{
    const WCHAR *line = GetCommandLineW();
    if (*line == L'"') {
        line++;
        while (*line && *line != L'"')
            line++;
        if (*line)
            line++;
    } else {
        while (*line && *line != L' ' && *line != L'\t')
            line++;
    }
    while (*line == L' ' || *line == L'\t')
        line++;
    return new_string(line);
}

VB_STD VARIANT *vb_rtcCommandVar(VARIANT *dest)
{
    BSTR text = vb_rtcCommandBstr();
    VariantInit(dest);
    V_VT(dest) = VT_BSTR;
    V_BSTR(dest) = text;
    return dest;
}

/* ------------------------------------------------------------------ the settings of a program */

#define SETTINGS_ROOT L"Software\\VB and VBA Program Settings\\"

static void check_name(BSTR name)
{
    if (!name || !name[0])
        VbRaise(VBERR_INVALID_CALL);
}

static void settings_path(BSTR app, BSTR section, WCHAR *out, UINT size)
{
    UINT total = (UINT)lstrlenW(SETTINGS_ROOT) + (app ? SysStringLen(app) : 0) + 1 + (section ? SysStringLen(section) : 0) + 1;
    if (total >= size)
        VbRaise(VBERR_INVALID_CALL);
    lstrcpyW(out, SETTINGS_ROOT);
    lstrcatW(out, app);
    if (section) {
        lstrcatW(out, L"\\");
        lstrcatW(out, section);
    }
}

VB_STD BSTR vb_rtcGetSetting(BSTR app, BSTR section, BSTR key, VARIANT fallback)
{
    WCHAR path[260];
    HKEY hkey;
    DWORD type = 0, size = 0;
    check_name(app);
    check_name(section);
    check_name(key);
    settings_path(app, section, path, 260);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &hkey) != ERROR_SUCCESS || RegQueryValueExW(hkey, key, nullptr, &type, nullptr, &size) != ERROR_SUCCESS) {
        if (hkey)
            RegCloseKey(hkey);
        if (VbIsMissing(&fallback))
            return new_string(L"");
        BSTR text;
        if (!VbText(&fallback, &text))
            VbRaise(VBERR_INVALID_USE_OF_NULL);
        return text ? text : new_string(L"");
    }
    if (type != REG_SZ) {
        RegCloseKey(hkey);
        VbRaise(VBERR_INVALID_CALL);
    }
    BSTR result = SysAllocStringLen(nullptr, size / 2 ? size / 2 - 1 : 0);
    if (!result) {
        RegCloseKey(hkey);
        VbRaise(VBERR_OUT_OF_MEMORY);
    }
    if (RegQueryValueExW(hkey, key, nullptr, &type, (BYTE *)result, &size) != ERROR_SUCCESS) {
        SysFreeString(result);
        RegCloseKey(hkey);
        VbRaise(VBERR_INVALID_CALL);
    }
    RegCloseKey(hkey);
    return result;
}

VB_STD void vb_rtcSaveSetting(BSTR app, BSTR section, BSTR key, BSTR value)
{
    WCHAR path[260];
    HKEY hkey;
    check_name(app);
    check_name(section);
    check_name(key);
    settings_path(app, section, path, 260);
    UINT n = value ? SysStringLen(value) : 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, nullptr, 0, KEY_WRITE, nullptr, &hkey, nullptr) != ERROR_SUCCESS)
        VbRaise(VBERR_INVALID_CALL);
    LSTATUS status = RegSetValueExW(hkey, key, 0, REG_SZ, (const BYTE *)(value ? value : L""), (n + 1) * sizeof(WCHAR));
    RegCloseKey(hkey);
    if (status != ERROR_SUCCESS)
        VbRaise(VBERR_INVALID_CALL);
}

static void delete_tree(HKEY parent, const WCHAR *name)
{
    HKEY key;
    WCHAR child[260];
    if (RegOpenKeyExW(parent, name, 0, KEY_READ | KEY_WRITE, &key) != ERROR_SUCCESS)
        return;
    while (RegEnumKeyExW(key, 0, child, &(DWORD){260}, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS)
        delete_tree(key, child);
    RegCloseKey(key);
    RegDeleteKeyW(parent, name);
}

VB_STD void vb_rtcDeleteSetting(BSTR app, VARIANT section, VARIANT key)
{
    WCHAR path[260];
    check_name(app);
    if (VbIsMissing(&key)) {
        if (VbIsMissing(&section)) {
            settings_path(app, nullptr, path, 260);
            HKEY probe;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &probe) != ERROR_SUCCESS)
                VbRaise(VBERR_INVALID_CALL);
            RegCloseKey(probe);
            delete_tree(HKEY_CURRENT_USER, path);
            return;
        }
        BSTR name;
        VbText(&section, &name);
        check_name(name);
        settings_path(app, name, path, 260);
        SysFreeString(name);
        HKEY probe;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &probe) != ERROR_SUCCESS)
            VbRaise(VBERR_INVALID_CALL);
        RegCloseKey(probe);
        delete_tree(HKEY_CURRENT_USER, path);
        return;
    }
    if (VbIsMissing(&section))
        VbRaise(VBERR_INVALID_CALL);
    BSTR name, value;
    VbText(&section, &name);
    VbText(&key, &value);
    check_name(name);
    check_name(value);
    settings_path(app, name, path, 260);
    SysFreeString(name);
    HKEY hkey;
    LSTATUS status = RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_WRITE, &hkey);
    if (status == ERROR_SUCCESS) {
        status = RegDeleteValueW(hkey, value);
        RegCloseKey(hkey);
    }
    SysFreeString(value);
    if (status != ERROR_SUCCESS)
        VbRaise(VBERR_INVALID_CALL);
}

VB_STD VARIANT *vb_rtcGetAllSettings(VARIANT *dest, BSTR app, BSTR section)
{
    WCHAR path[260];
    HKEY hkey;
    check_name(app);
    check_name(section);
    settings_path(app, section, path, 260);
    VariantInit(dest);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &hkey) != ERROR_SUCCESS)
        return dest;
    DWORD count = 0, longest = 0;
    if (RegQueryInfoKeyW(hkey, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &count, &longest, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
        RegCloseKey(hkey);
        VbRaise(VBERR_INVALID_CALL);
    }
    if (!count) {
        RegCloseKey(hkey);
        return dest;
    }
    SAFEARRAYBOUND bounds[2] = {{count, 0}, {2, 0}};
    SAFEARRAY *array = SafeArrayCreate(VT_BSTR, 2, bounds);
    WCHAR *name = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (longest + 1) * sizeof(WCHAR));
    if (!array || !name) {
        RegCloseKey(hkey);
        VbRaise(VBERR_OUT_OF_MEMORY);
    }
    DWORD filled = 0;
    for (DWORD i = 0; i < count; i++) {
        DWORD name_size = longest + 1, type = 0, data_size = 0;
        if (RegEnumValueW(hkey, i, name, &name_size, nullptr, &type, nullptr, &data_size) != ERROR_SUCCESS || type != REG_SZ)
            break;
        BSTR text = SysAllocStringLen(nullptr, data_size / 2 ? data_size / 2 - 1 : 0);
        name_size = longest + 1;
        if (!text || RegEnumValueW(hkey, i, name, &name_size, nullptr, nullptr, (BYTE *)text, &data_size) != ERROR_SUCCESS) {
            SysFreeString(text);
            break;
        }
        LONG index[2] = {(LONG)i, 0};
        BSTR key = SysAllocString(name);
        SafeArrayPutElement(array, index, key);
        SysFreeString(key);
        index[1] = 1;
        SafeArrayPutElement(array, index, text);
        SysFreeString(text);
        filled++;
    }
    HeapFree(GetProcessHeap(), 0, name);
    RegCloseKey(hkey);
    V_VT(dest) = VT_ARRAY | VT_BSTR;
    V_ARRAY(dest) = array;
    (void)filled;
    return dest;
}
