/*
 * special and known folders, and item id lists.
 *
 * like windows, a user folder's path comes from the user's User Shell Folders key when set, else from its default
 * under the profile. common folders read the same key in HKLM.
 *
 * item id lists live in pidl.c: the desktop is the empty list, a file system object is the items of its path below
 * "This PC", and a virtual folder is one item naming its class.
 */
#include "shell32_private.h"

typedef struct _SH_FOLDER {
    int Csidl;
    const GUID *Id;
    /* the User Shell Folders value that overrides the default, or NULL. */
    LPCWSTR Value;
    BOOL Common;
    /* NULL for a virtual folder without a path. */
    LPCWSTR Default;
} SH_FOLDER;

#define NO_CSIDL -1

static const SH_FOLDER g_folders[] = {
    {CSIDL_DESKTOP, &FOLDERID_Desktop, L"Desktop", FALSE, L"%USERPROFILE%\\Desktop"},
    {CSIDL_DESKTOPDIRECTORY, &FOLDERID_Desktop, L"Desktop", FALSE, L"%USERPROFILE%\\Desktop"},
    {CSIDL_PROGRAMS, &FOLDERID_Programs, L"Programs", FALSE,
     L"%USERPROFILE%\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs"},
    {CSIDL_PERSONAL, &FOLDERID_Documents, L"Personal", FALSE, L"%USERPROFILE%\\Documents"},
    {CSIDL_FAVORITES, &FOLDERID_Favorites, L"Favorites", FALSE, L"%USERPROFILE%\\Favorites"},
    {CSIDL_STARTUP, &FOLDERID_Startup, L"Startup", FALSE,
     L"%USERPROFILE%\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs\\Startup"},
    {CSIDL_RECENT, &FOLDERID_Recent, L"Recent", FALSE, L"%USERPROFILE%\\AppData\\Roaming\\Microsoft\\Windows\\Recent"},
    {CSIDL_SENDTO, &FOLDERID_SendTo, L"SendTo", FALSE, L"%USERPROFILE%\\AppData\\Roaming\\Microsoft\\Windows\\SendTo"},
    {CSIDL_STARTMENU, &FOLDERID_StartMenu, L"Start Menu", FALSE,
     L"%USERPROFILE%\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu"},
    {CSIDL_MYMUSIC, &FOLDERID_Music, L"My Music", FALSE, L"%USERPROFILE%\\Music"},
    {CSIDL_MYVIDEO, &FOLDERID_Videos, L"My Video", FALSE, L"%USERPROFILE%\\Videos"},
    {CSIDL_FONTS, &FOLDERID_Fonts, nullptr, FALSE, L"%SystemRoot%\\Fonts"},
    {CSIDL_TEMPLATES, &FOLDERID_Templates, L"Templates", FALSE,
     L"%USERPROFILE%\\AppData\\Roaming\\Microsoft\\Windows\\Templates"},
    {CSIDL_COMMON_STARTMENU, &FOLDERID_CommonStartMenu, L"Common Start Menu", TRUE,
     L"%ProgramData%\\Microsoft\\Windows\\Start Menu"},
    {CSIDL_COMMON_PROGRAMS, &FOLDERID_CommonPrograms, L"Common Programs", TRUE,
     L"%ProgramData%\\Microsoft\\Windows\\Start Menu\\Programs"},
    {CSIDL_COMMON_STARTUP, &FOLDERID_CommonStartup, L"Common Startup", TRUE,
     L"%ProgramData%\\Microsoft\\Windows\\Start Menu\\Programs\\Startup"},
    {CSIDL_COMMON_DESKTOPDIRECTORY, &FOLDERID_PublicDesktop, L"Common Desktop", TRUE, L"%PUBLIC%\\Desktop"},
    {CSIDL_APPDATA, &FOLDERID_RoamingAppData, L"AppData", FALSE, L"%USERPROFILE%\\AppData\\Roaming"},
    {CSIDL_LOCAL_APPDATA, &FOLDERID_LocalAppData, L"Local AppData", FALSE, L"%USERPROFILE%\\AppData\\Local"},
    {NO_CSIDL, &FOLDERID_LocalAppDataLow, nullptr, FALSE, L"%USERPROFILE%\\AppData\\LocalLow"},
    {NO_CSIDL, &FOLDERID_QuickLaunch, nullptr, FALSE,
     L"%USERPROFILE%\\AppData\\Roaming\\Microsoft\\Internet Explorer\\Quick Launch"},
    {NO_CSIDL, &FOLDERID_ImplicitAppShortcuts, nullptr, FALSE,
     L"%USERPROFILE%\\AppData\\Roaming\\Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\ImplicitAppShortcuts"},
    {CSIDL_INTERNET_CACHE, &FOLDERID_InternetCache, L"Cache", FALSE,
     L"%USERPROFILE%\\AppData\\Local\\Microsoft\\Windows\\INetCache"},
    {CSIDL_COOKIES, &FOLDERID_Cookies, L"Cookies", FALSE,
     L"%USERPROFILE%\\AppData\\Local\\Microsoft\\Windows\\INetCookies"},
    {CSIDL_HISTORY, &FOLDERID_History, L"History", FALSE,
     L"%USERPROFILE%\\AppData\\Local\\Microsoft\\Windows\\History"},
    {CSIDL_COMMON_APPDATA, &FOLDERID_ProgramData, L"Common AppData", TRUE, L"%ProgramData%"},
    {CSIDL_WINDOWS, &FOLDERID_Windows, nullptr, FALSE, L"%SystemRoot%"},
    {CSIDL_SYSTEM, &FOLDERID_System, nullptr, FALSE, L"%SystemRoot%\\System32"},
    {CSIDL_SYSTEMX86, &FOLDERID_SystemX86, nullptr, FALSE, L"%SystemRoot%\\SysWOW64"},
    {CSIDL_PROGRAM_FILES, &FOLDERID_ProgramFiles, nullptr, FALSE, L"%ProgramFiles%"},
    {NO_CSIDL, &FOLDERID_ProgramFilesX64, nullptr, FALSE, L"%ProgramW6432%"},
    {CSIDL_PROGRAM_FILESX86, &FOLDERID_ProgramFilesX86, nullptr, FALSE, L"%ProgramFiles(x86)%"},
    {CSIDL_PROGRAM_FILES_COMMON, &FOLDERID_ProgramFilesCommon, nullptr, FALSE, L"%CommonProgramFiles%"},
    {NO_CSIDL, &FOLDERID_ProgramFilesCommonX64, nullptr, FALSE, L"%CommonProgramW6432%"},
    {CSIDL_PROGRAM_FILES_COMMONX86, &FOLDERID_ProgramFilesCommonX86, nullptr, FALSE, L"%CommonProgramFiles(x86)%"},
    {CSIDL_MYPICTURES, &FOLDERID_Pictures, L"My Pictures", FALSE, L"%USERPROFILE%\\Pictures"},
    {CSIDL_PROFILE, &FOLDERID_Profile, nullptr, FALSE, L"%USERPROFILE%"},
    {NO_CSIDL, &FOLDERID_UserProfiles, nullptr, FALSE, L"%SystemDrive%\\Users"},
    {NO_CSIDL, &FOLDERID_Public, nullptr, FALSE, L"%PUBLIC%"},
    {CSIDL_COMMON_TEMPLATES, &FOLDERID_CommonTemplates, L"Common Templates", TRUE,
     L"%ProgramData%\\Microsoft\\Windows\\Templates"},
    {CSIDL_COMMON_DOCUMENTS, &FOLDERID_PublicDocuments, L"Common Documents", TRUE, L"%PUBLIC%\\Documents"},
    {NO_CSIDL, &FOLDERID_PublicDownloads, nullptr, FALSE, L"%PUBLIC%\\Downloads"},
    {CSIDL_COMMON_MUSIC, &FOLDERID_PublicMusic, L"CommonMusic", TRUE, L"%PUBLIC%\\Music"},
    {CSIDL_COMMON_PICTURES, &FOLDERID_PublicPictures, L"CommonPictures", TRUE, L"%PUBLIC%\\Pictures"},
    {CSIDL_COMMON_VIDEO, &FOLDERID_PublicVideos, L"CommonVideo", TRUE, L"%PUBLIC%\\Videos"},
    {CSIDL_RESOURCES, &FOLDERID_ResourceDir, nullptr, FALSE, L"%SystemRoot%\\Resources"},
    {CSIDL_CDBURN_AREA, &FOLDERID_CDBurning, L"CD Burning", FALSE,
     L"%USERPROFILE%\\AppData\\Local\\Microsoft\\Windows\\Burn\\Burn"},
    {NO_CSIDL, &FOLDERID_Downloads, L"{374DE290-123F-4565-9164-39C4925E467B}", FALSE, L"%USERPROFILE%\\Downloads"},
    {NO_CSIDL, &FOLDERID_SavedGames, L"{4C5C32FF-BB9D-43B0-B5B4-2D72E54EAAA4}", FALSE,
     L"%USERPROFILE%\\Saved Games"},
    {NO_CSIDL, &FOLDERID_Links, L"{BFB9D5E0-C6A9-404C-B2B2-AE6DB6AF4968}", FALSE, L"%USERPROFILE%\\Links"},
    {CSIDL_DRIVES, &FOLDERID_ComputerFolder, nullptr, FALSE, nullptr},
    {CSIDL_NETWORK, &FOLDERID_NetworkFolder, nullptr, FALSE, nullptr},
    {CSIDL_CONTROLS, &FOLDERID_ControlPanelFolder, nullptr, FALSE, nullptr},
    {CSIDL_PRINTERS, &FOLDERID_PrintersFolder, nullptr, FALSE, nullptr},
    {CSIDL_BITBUCKET, &FOLDERID_RecycleBinFolder, nullptr, FALSE, nullptr},
};

#define SHELL_FOLDERS_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders"
/* the token value meaning the default user profile. */
#define DEFAULT_USER_TOKEN ((HANDLE)(LONG_PTR)-1)

static const SH_FOLDER *folder_by_csidl(int csidl)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_folders); i++)
        if (g_folders[i].Csidl == csidl)
            return &g_folders[i];
    return nullptr;
}

static const SH_FOLDER *folder_by_id(REFKNOWNFOLDERID id)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_folders); i++)
        if (IsEqualGUID(g_folders[i].Id, id))
            return &g_folders[i];
    return nullptr;
}

LPWSTR ShDuplicate(LPCWSTR text)
{
    SIZE_T bytes = (wcslen(text) + 1) * sizeof(WCHAR);
    LPWSTR copy = ShAlloc(bytes);
    if (copy)
        memcpy(copy, text, bytes);
    return copy;
}

LPWSTR ShWiden(LPCSTR text)
{
    if (!text)
        return nullptr;
    int n = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    LPWSTR wide = n > 0 ? ShAlloc(n * sizeof(WCHAR)) : nullptr;
    if (wide)
        MultiByteToWideChar(CP_ACP, 0, text, -1, wide, n);
    return wide;
}

/* for the default user, the default profile replaces %USERPROFILE%. */
static BOOL expand(LPCWSTR template, BOOL default_user, LPWSTR out)
{
    WCHAR source[MAX_PATH * 2];
    static const WCHAR profile[] = L"%USERPROFILE%";
    if (default_user && !_wcsnicmp(template, profile, RTL_NUMBER_OF(profile) - 1)) {
        if (wcslen(template) >= MAX_PATH)
            return FALSE;
        _snwprintf(source, RTL_NUMBER_OF(source), L"%%SystemDrive%%\\Users\\Default%ls",
                   template + RTL_NUMBER_OF(profile) - 1);
        source[RTL_NUMBER_OF(source) - 1] = 0;
        template = source;
    }
    DWORD n = ExpandEnvironmentStringsW(template, out, MAX_PATH);
    return n && n <= MAX_PATH && !wcschr(out, L'%');
}

static BOOL read_override(const SH_FOLDER *folder, LPWSTR out)
{
    HKEY key;
    if (!folder->Value || RegOpenKeyExW(folder->Common ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, SHELL_FOLDERS_KEY, 0,
                                        KEY_READ, &key))
        return FALSE;
    WCHAR raw[MAX_PATH];
    DWORD type, bytes = sizeof(raw) - sizeof(WCHAR);
    LSTATUS status = RegQueryValueExW(key, folder->Value, nullptr, &type, (BYTE *)raw, &bytes);
    RegCloseKey(key);
    if (status || (type != REG_SZ && type != REG_EXPAND_SZ) || bytes < 2 * sizeof(WCHAR))
        return FALSE;
    raw[bytes / sizeof(WCHAR)] = 0;
    return expand(raw, FALSE, out);
}

static HRESULT create_tree(LPWSTR path)
{
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
        return S_OK;
    for (LPWSTR p = wcslen(path) > 3 ? path + 3 : path; *p; p++) {
        if (*p != L'\\')
            continue;
        *p = 0;
        if (!CreateDirectoryW(path, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
            DWORD error = GetLastError();
            *p = L'\\';
            return HRESULT_FROM_WIN32(error);
        }
        *p = L'\\';
    }
    if (!CreateDirectoryW(path, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}

/* with KF_FLAG semantics, into a MAX_PATH buffer. */
static HRESULT resolve(const SH_FOLDER *folder, DWORD flags, HANDLE token, LPWSTR out)
{
    out[0] = 0;
    if (!folder->Default)
        return E_FAIL;
    BOOL default_user = token == DEFAULT_USER_TOKEN;
    if ((flags & KF_FLAG_DEFAULT_PATH) || default_user || !read_override(folder, out)) {
        if (!expand(folder->Default, default_user, out)) {
            out[0] = 0;
            return E_FAIL;
        }
    }
    if (flags & KF_FLAG_CREATE)
        return create_tree(out);
    if (!(flags & KF_FLAG_DONT_VERIFY) && GetFileAttributesW(out) == INVALID_FILE_ATTRIBUTES)
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    return S_OK;
}

/* ------------------------------------------------------------------ known folders */

SHSTDAPI HRESULT WINAPI SHGetKnownFolderPath(REFKNOWNFOLDERID id, DWORD flags, HANDLE token, PWSTR *path)
{
    if (!path)
        return E_INVALIDARG;
    *path = nullptr;
    if (!id)
        return E_INVALIDARG;
    const SH_FOLDER *folder = folder_by_id(id);
    if (!folder)
        return E_INVALIDARG;
    WCHAR buffer[MAX_PATH];
    HRESULT hr = resolve(folder, flags, token, buffer);
    if (FAILED(hr))
        return hr;
    *path = ShDuplicate(buffer);
    return *path ? S_OK : E_OUTOFMEMORY;
}

/* ------------------------------------------------------------------ special folders */

static DWORD csidl_flags(int csidl)
{
    DWORD flags = 0;
    if (csidl & CSIDL_FLAG_CREATE)
        flags |= KF_FLAG_CREATE;
    if (csidl & CSIDL_FLAG_DONT_VERIFY)
        flags |= KF_FLAG_DONT_VERIFY;
    return flags;
}

SHSTDAPI HRESULT WINAPI SHGetFolderPathW(HWND hwnd, int csidl, HANDLE token, DWORD flags, LPWSTR path)
{
    if (!path)
        return E_INVALIDARG;
    path[0] = 0;
    const SH_FOLDER *folder = folder_by_csidl(csidl & ~CSIDL_FLAG_MASK);
    if (!folder)
        return E_INVALIDARG;
    if (!folder->Default)
        return E_FAIL;
    DWORD kf = csidl_flags(csidl);
    if (flags == SHGFP_TYPE_DEFAULT)
        kf |= KF_FLAG_DEFAULT_PATH;
    HRESULT hr = resolve(folder, kf, token, path);
    if (FAILED(hr))
        path[0] = 0;
    return hr;
}

SHSTDAPI HRESULT WINAPI SHGetFolderPathA(HWND hwnd, int csidl, HANDLE token, DWORD flags, LPSTR path)
{
    if (!path)
        return E_INVALIDARG;
    WCHAR wide[MAX_PATH];
    HRESULT hr = SHGetFolderPathW(hwnd, csidl, token, flags, wide);
    path[0] = 0;
    if (SUCCEEDED(hr) && !WideCharToMultiByte(CP_ACP, 0, wide, -1, path, MAX_PATH, nullptr, nullptr)) {
        path[0] = 0;
        hr = E_FAIL;
    }
    return hr;
}

WINBASEAPI HRESULT WINAPI PathCchAppend(LPWSTR path, SIZE_T size, LPCWSTR more);

/*
 * the folder must resolve with S_OK. CSIDL_FLAG_CREATE creates the whole path, CSIDL_FLAG_DONT_VERIFY skips the
 * check, otherwise it must be an existing directory: ERROR_PATH_NOT_FOUND when absent, ERROR_FILE_EXISTS when it is
 * a file. any failure leaves an empty path. an empty or absent subdirectory means the folder alone.
 */
SHSTDAPI HRESULT WINAPI SHGetFolderPathAndSubDirW(HWND hwnd, int csidl, HANDLE token, DWORD flags, LPCWSTR subdir,
                                                  LPWSTR path)
{
    HRESULT hr = SHGetFolderPathW(nullptr, csidl, token, flags, path);
    if (hr != S_OK || !subdir || !subdir[0])
        return hr;
    if (FAILED(PathCchAppend(path, MAX_PATH, subdir))) {
        hr = HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
    } else if (csidl & CSIDL_FLAG_CREATE) {
        int error = SHCreateDirectoryExW(nullptr, path, nullptr);
        hr = error == ERROR_ALREADY_EXISTS || !error ? S_OK : HRESULT_FROM_WIN32(error);
    } else if (csidl & CSIDL_FLAG_DONT_VERIFY) {
        return hr;
    } else {
        DWORD attributes = GetFileAttributesW(path);
        if (attributes == INVALID_FILE_ATTRIBUTES)
            hr = HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
        else if (attributes & FILE_ATTRIBUTE_DIRECTORY)
            return hr;
        else
            hr = HRESULT_FROM_WIN32(ERROR_FILE_EXISTS);
    }
    if (hr != S_OK)
        path[0] = 0;
    return hr;
}

SHSTDAPI HRESULT WINAPI SHGetFolderPathAndSubDirA(HWND hwnd, int csidl, HANDLE token, DWORD flags, LPCSTR subdir,
                                                  LPSTR path)
{
    if (!path)
        return E_INVALIDARG;
    WCHAR wide_subdir[MAX_PATH] = L"", wide_path[MAX_PATH];
    if (subdir)
        MultiByteToWideChar(CP_ACP, 0, subdir, -1, wide_subdir, MAX_PATH);
    HRESULT hr = SHGetFolderPathAndSubDirW(hwnd, csidl, token, flags, wide_subdir, wide_path);
    if (!WideCharToMultiByte(CP_ACP, 0, wide_path, -1, path, MAX_PATH, nullptr, nullptr))
        path[0] = 0;
    return hr;
}

SHSTDAPI BOOL WINAPI SHGetSpecialFolderPathW(HWND hwnd, LPWSTR path, int csidl, BOOL create)
{
    return SUCCEEDED(SHGetFolderPathW(hwnd, csidl | (create ? CSIDL_FLAG_CREATE : 0), nullptr, SHGFP_TYPE_CURRENT, path));
}

SHSTDAPI BOOL WINAPI SHGetSpecialFolderPathA(HWND hwnd, LPSTR path, int csidl, BOOL create)
{
    return SUCCEEDED(SHGetFolderPathA(hwnd, csidl | (create ? CSIDL_FLAG_CREATE : 0), nullptr, SHGFP_TYPE_CURRENT, path));
}

/* ------------------------------------------------------------------ item id lists */

/* the class ids of the folders without a path, as windows' id lists name them. */
static const struct {
    const GUID *Id;
    BYTE Sort;
    GUID Class;
} g_virtual[] = {
    {&FOLDERID_ComputerFolder, 0x50, {0x20D04FE0, 0x3AEA, 0x1069, {0xA2, 0xD8, 0x08, 0x00, 0x2B, 0x30, 0x30, 0x9D}}},
    {&FOLDERID_NetworkFolder, 0x58, {0xF02C1A0D, 0xBE21, 0x4350, {0x88, 0xB0, 0x73, 0x67, 0xFC, 0x96, 0xEF, 0x3C}}},
    {&FOLDERID_ControlPanelFolder, 0x70, {0x5399E694, 0x6CE5, 0x4D6C, {0x8F, 0xCE, 0x1D, 0x88, 0x70, 0xFD, 0xCB, 0xA0}}},
    {&FOLDERID_PrintersFolder, 0x70, {0x2227A280, 0x3AEA, 0x1069, {0xA2, 0xDE, 0x08, 0x00, 0x2B, 0x30, 0x30, 0x9D}}},
    {&FOLDERID_RecycleBinFolder, 0x78, {0x645FF040, 0x5081, 0x101B, {0x9F, 0x08, 0x00, 0xAA, 0x00, 0x2F, 0x95, 0x4E}}},
};

static PIDLIST_ABSOLUTE virtual_list(const SH_FOLDER *folder)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_virtual); i++)
        if (IsEqualGUID(g_virtual[i].Id, folder->Id))
            return ShClassList(g_virtual[i].Sort, &g_virtual[i].Class);
    return nullptr;
}

static PIDLIST_ABSOLUTE desktop_list(void)
{
    return ShAllocZero(sizeof(USHORT));
}

SHSTDAPI BOOL WINAPI SHGetPathFromIDListEx(PCIDLIST_ABSOLUTE pidl, PWSTR path, DWORD chars, int options)
{
    return ShPathFromIdList(pidl, path, chars);
}

SHSTDAPI BOOL WINAPI SHGetPathFromIDListW(PCIDLIST_ABSOLUTE pidl, LPWSTR path)
{
    return ShPathFromIdList(pidl, path, MAX_PATH);
}

SHSTDAPI BOOL WINAPI SHGetPathFromIDListA(PCIDLIST_ABSOLUTE pidl, LPSTR path)
{
    WCHAR wide[MAX_PATH];
    if (!path)
        return FALSE;
    path[0] = 0;
    if (!ShPathFromIdList(pidl, wide, MAX_PATH))
        return FALSE;
    if (!WideCharToMultiByte(CP_ACP, 0, wide, -1, path, MAX_PATH, nullptr, nullptr)) {
        path[0] = 0;
        return FALSE;
    }
    return TRUE;
}

SHSTDAPI HRESULT WINAPI SHParseDisplayName(PCWSTR name, IBindCtx *context, PIDLIST_ABSOLUTE *pidl, SFGAOF in,
                                           SFGAOF *out)
{
    if (!pidl)
        return E_INVALIDARG;
    *pidl = nullptr;
    if (out)
        *out = 0;
    DWORD attributes;
    HRESULT hr = ShParseName(name, context, pidl, &attributes);
    if (SUCCEEDED(hr) && out)
        *out = ShAttributesFromFile(attributes) & in;
    return hr;
}

SHSTDAPI PIDLIST_ABSOLUTE WINAPI ILCreateFromPathW(PCWSTR path)
{
    PIDLIST_ABSOLUTE pidl;
    return SUCCEEDED(SHParseDisplayName(path, nullptr, &pidl, 0, nullptr)) ? pidl : nullptr;
}

static HRESULT folder_list(const SH_FOLDER *folder, DWORD flags, HANDLE token, PIDLIST_ABSOLUTE *pidl)
{
    if (folder->Csidl == CSIDL_DESKTOP || IsEqualGUID(folder->Id, &FOLDERID_Desktop)) {
        *pidl = desktop_list();
    } else if (!folder->Default) {
        *pidl = virtual_list(folder);
    } else {
        WCHAR path[MAX_PATH];
        HRESULT hr = resolve(folder, flags, token, path);
        if (FAILED(hr))
            return hr;
        *pidl = ShIdListFromPath(path);
    }
    return *pidl ? S_OK : E_OUTOFMEMORY;
}

SHSTDAPI HRESULT WINAPI SHGetFolderLocation(HWND hwnd, int csidl, HANDLE token, DWORD reserved,
                                            PIDLIST_ABSOLUTE *pidl)
{
    if (!pidl)
        return E_INVALIDARG;
    *pidl = nullptr;
    if (reserved)
        return E_INVALIDARG;
    const SH_FOLDER *folder = folder_by_csidl(csidl & ~CSIDL_FLAG_MASK);
    if (!folder)
        return E_INVALIDARG;
    return folder_list(folder, csidl_flags(csidl), token, pidl);
}

SHSTDAPI HRESULT WINAPI SHGetSpecialFolderLocation(HWND hwnd, int csidl, PIDLIST_ABSOLUTE *pidl)
{
    return SHGetFolderLocation(hwnd, csidl, nullptr, 0, pidl);
}

SHSTDAPI HRESULT WINAPI SHGetKnownFolderIDList(REFKNOWNFOLDERID id, DWORD flags, HANDLE token,
                                               PIDLIST_ABSOLUTE *pidl)
{
    if (!pidl)
        return E_INVALIDARG;
    *pidl = nullptr;
    if (!id)
        return E_INVALIDARG;
    const SH_FOLDER *folder = folder_by_id(id);
    if (!folder)
        return E_INVALIDARG;
    return folder_list(folder, flags, token, pidl);
}
