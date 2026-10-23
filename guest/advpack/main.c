/*
 * advpack.dll, the helpers setup programs use: admin checks, file versions, deleting trees, cabinet extraction and
 * running setup commands. anything that needs an INF engine fails, as advpack does when it cannot read the INF.
 */
#include "../lib/cabinet/cabinet.h"
#include <mime/win32/winbase.h>
#include <mime/win32/winnls.h>
#include <mime/win32/winver.h>
#include <rtl.h>

#define ADVAPI DECLSPEC_EXPORT

#define E_FAIL_ ((HRESULT)0x80004005L)
#define S_ASYNCHRONOUS_ ((HRESULT)0x000401E8L)

#define ADN_DEL_IF_EMPTY 0x00000001
#define ADN_DONT_DEL_SUBDIRS 0x00000002
#define ADN_DONT_DEL_DIR 0x00000004
#define ADN_DEL_UNC_PATHS 0x00000008

#define SECURITY_NT_AUTHORITY_VALUE {0, 0, 0, 0, 0, 5}
#define SECURITY_BUILTIN_DOMAIN_RID_ 0x20
#define DOMAIN_ALIAS_RID_ADMINS_ 0x220
#define TOKEN_QUERY_ 0x0008
#define TOKEN_GROUPS_ 2

typedef LONG (WINAPI *CHECK_TOKEN_MEMBERSHIP)(HANDLE, PSID, PBOOL);

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

/* ------------------------------------------------------------------ text */

static PWSTR to_wide(PCSTR text)
{
    if (!text)
        return nullptr;
    int count = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    PWSTR wide = count ? HeapAlloc(GetProcessHeap(), 0, (SIZE_T)count * sizeof(WCHAR)) : nullptr;
    if (wide)
        MultiByteToWideChar(CP_ACP, 0, text, -1, wide, count);
    return wide;
}

static PSTR to_ansi(PCWSTR text)
{
    if (!text)
        return nullptr;
    int count = WideCharToMultiByte(CP_ACP, 0, text, -1, nullptr, 0, nullptr, nullptr);
    PSTR ansi = count ? HeapAlloc(GetProcessHeap(), 0, (SIZE_T)count) : nullptr;
    if (ansi)
        WideCharToMultiByte(CP_ACP, 0, text, -1, ansi, count, nullptr, nullptr);
    return ansi;
}

static void release(PVOID memory)
{
    if (memory)
        HeapFree(GetProcessHeap(), 0, memory);
}

/* ------------------------------------------------------------------ the user */

/*
 * the administrators group counts whether enabled or not, so a filtered admin token still passes.
 */
ADVAPI BOOL WINAPI IsNTAdmin(DWORD reserved, DWORD *reserved2)
{
    HMODULE advapi = LoadLibraryW(L"advapi32.dll");
    if (!advapi)
        return FALSE;
    typedef BOOL (WINAPI *OPEN_TOKEN)(HANDLE, DWORD, PHANDLE);
    typedef BOOL (WINAPI *GET_INFORMATION)(HANDLE, DWORD, PVOID, DWORD, PDWORD);
    typedef BOOL (WINAPI *ALLOCATE_SID)(PVOID, BYTE, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, PSID *);
    typedef BOOL (WINAPI *EQUAL_SID)(PSID, PSID);
    typedef PVOID (WINAPI *FREE_SID)(PSID);
    OPEN_TOKEN open_token = (OPEN_TOKEN)GetProcAddress(advapi, "OpenProcessToken");
    GET_INFORMATION get_information = (GET_INFORMATION)GetProcAddress(advapi, "GetTokenInformation");
    ALLOCATE_SID allocate_sid = (ALLOCATE_SID)GetProcAddress(advapi, "AllocateAndInitializeSid");
    EQUAL_SID equal_sid = (EQUAL_SID)GetProcAddress(advapi, "EqualSid");
    FREE_SID free_sid = (FREE_SID)GetProcAddress(advapi, "FreeSid");
    BOOL member = FALSE;
    BYTE authority[6] = SECURITY_NT_AUTHORITY_VALUE;
    PSID administrators = nullptr;
    if (open_token && get_information && allocate_sid && equal_sid && free_sid &&
        allocate_sid(authority, 2, SECURITY_BUILTIN_DOMAIN_RID_, DOMAIN_ALIAS_RID_ADMINS_, 0, 0, 0, 0, 0, 0, &administrators)) {
        HANDLE token;
        if (open_token(GetCurrentProcess(), TOKEN_QUERY_, &token)) {
            DWORD size = 0;
            get_information(token, TOKEN_GROUPS_, nullptr, 0, &size);
            struct GROUPS {
                DWORD Count;
                struct { PSID Sid; DWORD Attributes; } Groups[1];
            } *groups = size ? HeapAlloc(GetProcessHeap(), 0, size) : nullptr;
            if (groups && get_information(token, TOKEN_GROUPS_, groups, size, &size)) {
                for (DWORD i = 0; i < groups->Count && !member; i++)
                    member = equal_sid(groups->Groups[i].Sid, administrators);
            }
            release(groups);
            CloseHandle(token);
        }
        free_sid(administrators);
    }
    FreeLibrary(advapi);
    return member;
}

/* ------------------------------------------------------------------ versions */

/*
 * with bVersion false this returns the language and code page of the first translation instead. a file with no
 * version information is not an error: both numbers are zero.
 */
ADVAPI HRESULT WINAPI GetVersionFromFileExW(LPCWSTR file, LPDWORD high, LPDWORD low, BOOL version)
{
    if (!high || !low)
        return E_FAIL_;
    *high = *low = 0;
    DWORD handle, size = file ? GetFileVersionInfoSizeW(file, &handle) : 0;
    void *block = size ? HeapAlloc(GetProcessHeap(), 0, size) : nullptr;
    if (block && GetFileVersionInfoW(file, 0, size, block)) {
        void *value = nullptr;
        UINT length = 0;
        if (version) {
            if (VerQueryValueW(block, L"\\", &value, &length) && length) {
                const DWORD *words = value;
                *high = words[2];
                *low = words[3];
            }
        } else if (VerQueryValueW(block, L"\\VarFileInfo\\Translation", &value, &length) && length) {
            const WORD *words = value;
            *high = words[0];
            *low = words[1];
        }
    }
    release(block);
    return S_OK;
}

ADVAPI HRESULT WINAPI GetVersionFromFileW(LPCWSTR file, LPDWORD high, LPDWORD low, BOOL version)
{
    return GetVersionFromFileExW(file, high, low, version);
}

ADVAPI HRESULT WINAPI GetVersionFromFileExA(LPCSTR file, LPDWORD high, LPDWORD low, BOOL version)
{
    PWSTR wide = to_wide(file);
    HRESULT hr = wide ? GetVersionFromFileExW(wide, high, low, version) : E_FAIL_;
    release(wide);
    return hr;
}

ADVAPI HRESULT WINAPI GetVersionFromFileA(LPCSTR file, LPDWORD high, LPDWORD low, BOOL version)
{
    return GetVersionFromFileExA(file, high, low, version);
}

/* ------------------------------------------------------------------ deleting */

static BOOL full_path(PCWSTR path)
{
    return (path[0] && path[1] == L':' && path[2] == L'\\') || (path[0] == L'\\' && path[1] == L'\\');
}

/*
 * the flags keep the folder, keep its subfolders, delete only when empty, or allow network paths, which are
 * otherwise refused. relative paths are refused.
 */
ADVAPI HRESULT WINAPI DelNodeW(LPCWSTR path, DWORD flags)
{
    if (!path || !full_path(path) || (path[0] == L'\\' && path[1] == L'\\' && !(flags & ADN_DEL_UNC_PATHS)))
        return E_FAIL_;
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return E_FAIL_;
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
        return DeleteFileW(path) ? S_OK : E_FAIL_;
    }
    if (flags & ADN_DEL_IF_EMPTY) {
        SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
        return RemoveDirectoryW(path) ? S_OK : E_FAIL_;
    }
    SIZE_T length = wcslen(path);
    if (length > MAX_PATH - 8)
        return E_FAIL_;
    WCHAR pattern[MAX_PATH], child[MAX_PATH];
    wcscpy(pattern, path);
    if (pattern[length - 1] != L'\\')
        pattern[length++] = L'\\';
    wcscpy(pattern + length, L"*");
    WIN32_FIND_DATAW found;
    HANDLE find = FindFirstFileW(pattern, &found);
    if (find == INVALID_HANDLE_VALUE)
        return E_FAIL_;
    HRESULT hr = S_OK;
    do {
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            (!wcscmp(found.cFileName, L".") || !wcscmp(found.cFileName, L"..") || (flags & ADN_DONT_DEL_SUBDIRS)))
            continue;
        if (length + wcslen(found.cFileName) >= MAX_PATH)
            continue;
        wcscpy(child, pattern);
        wcscpy(child + length, found.cFileName);
        hr = DelNodeW(child, flags & ADN_DEL_UNC_PATHS);
    } while (hr >= 0 && FindNextFileW(find, &found));
    FindClose(find);
    if (hr >= 0 && !(flags & ADN_DONT_DEL_DIR)) {
        if (DelNodeW(path, ADN_DEL_IF_EMPTY | (flags & ADN_DEL_UNC_PATHS)) < 0 && !(flags & ADN_DONT_DEL_SUBDIRS))
            return E_FAIL_;
    }
    return hr;
}

ADVAPI HRESULT WINAPI DelNodeA(LPCSTR path, DWORD flags)
{
    PWSTR wide = to_wide(path);
    HRESULT hr = wide ? DelNodeW(wide, flags) : E_FAIL_;
    release(wide);
    return hr;
}

/* the parameters are the path, a comma, then the flags as a number. */
ADVAPI HRESULT WINAPI DelNodeRunDLL32W(HWND window, HINSTANCE instance, LPWSTR parameters, INT show)
{
    WCHAR path[MAX_PATH];
    DWORD flags = 0;
    SIZE_T n = 0;
    PCWSTR p = parameters;
    if (!p)
        return E_FAIL_;
    if (*p == L'"') {
        p++;
        while (*p && *p != L'"' && n + 1 < MAX_PATH)
            path[n++] = *p++;
        if (*p == L'"')
            p++;
    } else
        while (*p && *p != L',' && n + 1 < MAX_PATH)
            path[n++] = *p++;
    path[n] = 0;
    if (*p == L',')
        p++;
    while (*p == L' ')
        p++;
    while (*p >= L'0' && *p <= L'9')
        flags = flags * 10 + (DWORD)(*p++ - L'0');
    return DelNodeW(path, flags);
}

ADVAPI HRESULT WINAPI DelNodeRunDLL32A(HWND window, HINSTANCE instance, LPSTR parameters, INT show)
{
    PWSTR wide = to_wide(parameters);
    HRESULT hr = wide ? DelNodeRunDLL32W(window, instance, wide, show) : E_FAIL_;
    release(wide);
    return hr;
}

/* ------------------------------------------------------------------ files and cabinets */

typedef struct EXTRACT {
    PCWSTR Directory;
    PCSTR Names;
    BOOL All;
} EXTRACT;

typedef struct EXTRACT_FILE {
    HANDLE File;
} EXTRACT_FILE;

static BOOL name_listed(const EXTRACT *extract, PCSTR name)
{
    if (extract->All)
        return TRUE;
    SIZE_T length = strlen(name);
    for (PCSTR p = extract->Names; *p;) {
        PCSTR end = strchr(p, ':');
        SIZE_T count = end ? (SIZE_T)(end - p) : strlen(p);
        if (count == length && !_strnicmp(p, name, count))
            return TRUE;
        if (!end)
            break;
        p = end + 1;
    }
    return FALSE;
}

static void *extract_begin(void *context, const MIME_CAB_FILE *file)
{
    EXTRACT *extract = context;
    if (!name_listed(extract, file->Name))
        return nullptr;
    /* drop any folder part of the name. */
    PCSTR base = file->Name;
    for (PCSTR p = file->Name; *p; p++)
        if (*p == '\\' || *p == '/')
            base = p + 1;
    WCHAR path[MAX_PATH];
    SIZE_T length = wcslen(extract->Directory);
    if (length + strlen(base) + 2 >= MAX_PATH)
        return nullptr;
    wcscpy(path, extract->Directory);
    if (length && path[length - 1] != L'\\')
        path[length++] = L'\\';
    for (SIZE_T i = 0; base[i]; i++)
        path[length + i] = (UCHAR)base[i];
    path[length + strlen(base)] = 0;
    EXTRACT_FILE *entry = HeapAlloc(GetProcessHeap(), 0, sizeof(*entry));
    if (!entry)
        return nullptr;
    entry->File = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (entry->File == INVALID_HANDLE_VALUE) {
        release(entry);
        return nullptr;
    }
    return entry;
}

static int extract_write(void *context, void *file_context, const uint8_t *data, size_t length)
{
    EXTRACT_FILE *entry = file_context;
    while (length) {
        DWORD written, wanted = length < 0x100000 ? (DWORD)length : 0x100000;
        if (!WriteFile(entry->File, data, wanted, &written, nullptr) || !written)
            return 1;
        data += written;
        length -= written;
    }
    return 0;
}

static int extract_end(void *context, void *file_context, const MIME_CAB_FILE *file)
{
    EXTRACT_FILE *entry = file_context;
    FILETIME local, utc;
    if (DosDateTimeToFileTime(file->Date, file->Time, &local) && LocalFileTimeToFileTime(&local, &utc))
        SetFileTime(entry->File, &utc, &utc, &utc);
    CloseHandle(entry->File);
    release(entry);
    return 0;
}

static void *cab_allocate(size_t size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}

static void cab_release(void *memory)
{
    release(memory);
}

/* list is colon separated; empty or a star means every file. */
ADVAPI HRESULT WINAPI ExtractFilesA(LPCSTR cabinet, LPCSTR directory, DWORD flags, LPCSTR list, LPVOID reserved,
                                    DWORD reserved2)
{
    if (!cabinet || !directory)
        return E_FAIL_;
    PWSTR wide_cabinet = to_wide(cabinet), wide_directory = to_wide(directory);
    HRESULT hr = E_FAIL_;
    HANDLE file = wide_cabinet ? CreateFileW(wide_cabinet, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr)
                               : INVALID_HANDLE_VALUE;
    LARGE_INTEGER size = {};
    HANDLE mapping = file != INVALID_HANDLE_VALUE && GetFileSizeEx(file, &size) && size.QuadPart > 0 ?
                     CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr) : nullptr;
    void *view = mapping ? MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0) : nullptr;
    if (view && wide_directory) {
        CreateDirectoryW(wide_directory, nullptr);
        MIME_CAB *cab;
        if (!MimeCabOpen(&cab, view, (SIZE_T)size.QuadPart, cab_allocate, cab_release)) {
            EXTRACT extract = {wide_directory, list ? list : "*", !list || !*list || !strcmp(list, "*")};
            static const MIME_CAB_FILE_SINK sink = {extract_begin, extract_write, extract_end};
            hr = MimeCabExtract(cab, &sink, &extract) ? E_FAIL_ : S_OK;
            MimeCabClose(cab);
        }
    }
    if (view)
        UnmapViewOfFile(view);
    if (mapping)
        CloseHandle(mapping);
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
    release(wide_cabinet);
    release(wide_directory);
    return hr;
}

ADVAPI HRESULT WINAPI ExtractFilesW(LPCWSTR cabinet, LPCWSTR directory, DWORD flags, LPCWSTR list, LPVOID reserved,
                                    DWORD reserved2)
{
    PSTR a = to_ansi(cabinet), b = to_ansi(directory), c = to_ansi(list);
    HRESULT hr = a && b ? ExtractFilesA(a, b, flags, c, reserved, reserved2) : E_FAIL_;
    release(a);
    release(b);
    release(c);
    return hr;
}

#define AIF_NOSKIP 0x00000001
#define AIF_NOVERSIONCHECK 0x00000002
#define AIF_NOOVERWRITE 0x00000010
#define AIF_QUIET 0x00000020
#define AIF_NOLANGUAGECHECK 0x00000004
#define AIF_REPLACEONLY 0x00000400

static HRESULT join(PWSTR out, PCWSTR directory, PCWSTR name)
{
    SIZE_T length = wcslen(directory), more = wcslen(name);
    if (length + more + 2 >= MAX_PATH)
        return E_FAIL_;
    wcscpy(out, directory);
    if (length && out[length - 1] != L'\\')
        out[length++] = L'\\';
    wcscpy(out + length, name);
    return S_OK;
}

/*
 * keeps the newer version by default. AIF_NOOVERWRITE keeps an existing file, AIF_REPLACEONLY never creates one.
 */
ADVAPI HRESULT WINAPI AdvInstallFileW(HWND window, LPCWSTR source_dir, LPCWSTR source_file, LPCWSTR dest_dir,
                                      LPCWSTR dest_file, DWORD flags, DWORD reserved)
{
    if (!source_dir || !source_file || !dest_dir)
        return E_FAIL_;
    WCHAR source[MAX_PATH], destination[MAX_PATH];
    if (join(source, source_dir, source_file) < 0 || join(destination, dest_dir, dest_file ? dest_file : source_file) < 0)
        return E_FAIL_;
    if (GetFileAttributesW(source) == INVALID_FILE_ATTRIBUTES)
        return E_FAIL_;
    BOOL present = GetFileAttributesW(destination) != INVALID_FILE_ATTRIBUTES;
    if (present && (flags & AIF_NOOVERWRITE))
        return S_OK;
    if (!present && (flags & AIF_REPLACEONLY))
        return S_OK;
    if (present && !(flags & AIF_NOVERSIONCHECK)) {
        DWORD source_high, source_low, current_high, current_low;
        GetVersionFromFileExW(source, &source_high, &source_low, TRUE);
        GetVersionFromFileExW(destination, &current_high, &current_low, TRUE);
        if ((current_high > source_high || (current_high == source_high && current_low > source_low)) &&
            !(flags & AIF_NOSKIP))
            return S_OK;
    }
    CreateDirectoryW(dest_dir, nullptr);
    SetFileAttributesW(destination, FILE_ATTRIBUTE_NORMAL);
    return CopyFileW(source, destination, FALSE) ? S_OK : E_FAIL_;
}

ADVAPI HRESULT WINAPI AdvInstallFileA(HWND window, LPCSTR source_dir, LPCSTR source_file, LPCSTR dest_dir,
                                      LPCSTR dest_file, DWORD flags, DWORD reserved)
{
    PWSTR a = to_wide(source_dir), b = to_wide(source_file), c = to_wide(dest_dir), d = to_wide(dest_file);
    HRESULT hr = a && b && c ? AdvInstallFileW(window, a, b, c, d, flags, reserved) : E_FAIL_;
    release(a);
    release(b);
    release(c);
    release(d);
    return hr;
}

/* ------------------------------------------------------------------ running programs */

#define RSC_FLAG_INF 0x00000001

/*
 * INF commands need the INF engine and fail. when the caller wants the process handle, the program is not waited
 * for and the result is the asynchronous success code.
 */
ADVAPI HRESULT WINAPI RunSetupCommandW(HWND window, LPCWSTR command, LPCWSTR section, LPCWSTR directory,
                                       LPCWSTR title, HANDLE *process_out, DWORD flags, LPVOID reserved)
{
    if (!command || (flags & RSC_FLAG_INF))
        return E_FAIL_;
    SIZE_T length = wcslen(command);
    PWSTR line = HeapAlloc(GetProcessHeap(), 0, (length + 1) * sizeof(WCHAR));
    if (!line)
        return E_FAIL_;
    wcscpy(line, command);
    STARTUPINFOW startup = {.cb = sizeof(startup)};
    PROCESS_INFORMATION process;
    BOOL made = CreateProcessW(nullptr, line, nullptr, nullptr, FALSE, 0, nullptr, directory, &startup, &process);
    release(line);
    if (!made)
        return HRESULT_FROM_WIN32(GetLastError());
    CloseHandle(process.hThread);
    if (process_out) {
        *process_out = process.hProcess;
        return S_ASYNCHRONOUS_;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    CloseHandle(process.hProcess);
    return S_OK;
}

ADVAPI HRESULT WINAPI RunSetupCommandA(HWND window, LPCSTR command, LPCSTR section, LPCSTR directory, LPCSTR title,
                                       HANDLE *process, DWORD flags, LPVOID reserved)
{
    PWSTR a = to_wide(command), b = to_wide(directory);
    HRESULT hr = a ? RunSetupCommandW(window, a, nullptr, b, nullptr, process, flags, reserved) : E_FAIL_;
    release(a);
    release(b);
    return hr;
}

/* ------------------------------------------------------------------ restarts */

/* nothing here ever needs a restart. */
ADVAPI DWORD WINAPI NeedRebootInit(void)
{
    return 0;
}

ADVAPI BOOL WINAPI NeedReboot(DWORD check)
{
    return FALSE;
}

ADVAPI HRESULT WINAPI RebootCheckOnInstallW(HWND window, LPCWSTR inf, LPCWSTR section, DWORD reserved)
{
    return S_OK;
}

ADVAPI HRESULT WINAPI RebootCheckOnInstallA(HWND window, LPCSTR inf, LPCSTR section, DWORD reserved)
{
    return S_OK;
}

/* ------------------------------------------------------------------ INF engine */

#define NO_INF(name, args) ADVAPI HRESULT WINAPI name args { return E_FAIL_; }

NO_INF(DoInfInstallA, (void *arguments))
NO_INF(DoInfInstallW, (void *arguments))
NO_INF(FileSaveRestoreA, (HWND w, LPSTR list, LPCSTR directory, LPCSTR base, DWORD flags))
NO_INF(FileSaveRestoreW, (HWND w, LPWSTR list, LPCWSTR directory, LPCWSTR base, DWORD flags))
NO_INF(LaunchINFSectionExA, (HWND w, HINSTANCE i, LPSTR parameters, INT show))
NO_INF(LaunchINFSectionExW, (HWND w, HINSTANCE i, LPWSTR parameters, INT show))
NO_INF(RegisterOCXA, (HWND w, HINSTANCE i, LPCSTR parameters, int show))
NO_INF(RegisterOCXW, (HWND w, HINSTANCE i, LPWSTR parameters, int show))
NO_INF(AddDelBackupEntryA, (LPCSTR list, LPCSTR directory, LPCSTR base, DWORD flags))
NO_INF(AddDelBackupEntryW, (LPCWSTR list, LPCWSTR directory, LPCWSTR base, DWORD flags))
NO_INF(CloseINFEngine, (void *inf))
NO_INF(ExecuteCabA, (HWND w, void *cab, LPVOID reserved))
NO_INF(ExecuteCabW, (HWND w, void *cab, LPVOID reserved))
NO_INF(FileSaveMarkNotExistA, (LPCSTR list, LPCSTR directory, LPCSTR base))
NO_INF(FileSaveMarkNotExistW, (LPCWSTR list, LPCWSTR directory, LPCWSTR base))
NO_INF(FileSaveRestoreOnINFA, (HWND w, LPCSTR title, LPCSTR inf, LPCSTR section, LPCSTR directory, LPCSTR base, DWORD flags))
NO_INF(FileSaveRestoreOnINFW, (HWND w, LPCWSTR title, LPCWSTR inf, LPCWSTR section, LPCWSTR directory, LPCWSTR base, DWORD flags))
NO_INF(OpenINFEngineA, (LPCSTR inf, LPCSTR section, DWORD flags, void **handle, PVOID reserved))
NO_INF(OpenINFEngineW, (LPCWSTR inf, LPCWSTR section, DWORD flags, void **handle, PVOID reserved))
NO_INF(RegInstallA, (HMODULE module, LPCSTR section, const void *table))
NO_INF(RegInstallW, (HMODULE module, LPCWSTR section, const void *table))
NO_INF(RegRestoreAllA, (HWND w, LPCSTR title, HKEY key))
NO_INF(RegRestoreAllW, (HWND w, LPCWSTR title, HKEY key))
NO_INF(RegSaveRestoreA, (HWND w, LPCSTR title, HKEY key, LPCSTR root, LPCSTR sub, LPCSTR value, DWORD flags))
NO_INF(RegSaveRestoreW, (HWND w, LPCWSTR title, HKEY key, LPCWSTR root, LPCWSTR sub, LPCWSTR value, DWORD flags))
NO_INF(RegSaveRestoreOnINFA, (HWND w, LPCSTR title, LPCSTR inf, LPCSTR section, HKEY machine, HKEY user, DWORD flags))
NO_INF(RegSaveRestoreOnINFW, (HWND w, LPCWSTR title, LPCWSTR inf, LPCWSTR section, HKEY machine, HKEY user, DWORD flags))
NO_INF(SetPerUserSecValuesA, (void *section))
NO_INF(SetPerUserSecValuesW, (void *section))
NO_INF(TranslateInfStringA, (LPCSTR inf, LPCSTR install, LPCSTR section, LPCSTR key, LPSTR buffer, DWORD size, PDWORD required, PVOID reserved))
NO_INF(TranslateInfStringW, (LPCWSTR inf, LPCWSTR install, LPCWSTR section, LPCWSTR key, LPWSTR buffer, DWORD size, PDWORD required, PVOID reserved))
NO_INF(TranslateInfStringExA, (void *inf, LPCSTR file, LPCSTR section, LPCSTR key, LPSTR buffer, DWORD size, PDWORD required, PVOID reserved))
NO_INF(TranslateInfStringExW, (void *inf, LPCWSTR file, LPCWSTR section, LPCWSTR key, LPWSTR buffer, DWORD size, PDWORD required, PVOID reserved))
NO_INF(UserInstStubWrapperA, (HWND w, HINSTANCE i, LPCSTR parameters, INT show))
NO_INF(UserInstStubWrapperW, (HWND w, HINSTANCE i, LPCWSTR parameters, INT show))
NO_INF(UserUnInstStubWrapperA, (HWND w, HINSTANCE i, LPCSTR parameters, INT show))
NO_INF(UserUnInstStubWrapperW, (HWND w, HINSTANCE i, LPCWSTR parameters, INT show))

/* the rundll32 entries return a count, not an HRESULT: zero when the INF cannot run. */
ADVAPI INT WINAPI LaunchINFSectionA(HWND window, HINSTANCE instance, LPSTR parameters, INT show)
{
    return 0;
}

ADVAPI INT WINAPI LaunchINFSectionW(HWND window, HINSTANCE instance, LPWSTR parameters, INT show)
{
    return 0;
}

ADVAPI INT WINAPI LaunchINFSection(HWND window, HINSTANCE instance, LPSTR parameters, INT show)
{
    return 0;
}
