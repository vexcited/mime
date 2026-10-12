/*
 * special and known folders, and item id lists.
 *
 * like windows, a user folder's path comes from the User Shell Folders key
 * of the user's hive when it is set, else from the folder's default under
 * the profile. common folders read the same key of HKLM.
 *
 * mime's item id lists are its own: an absolute file system list is one
 * item holding the full path, the desktop is the empty list, and a virtual
 * folder is one item holding the folder's index. only shell32 reads them.
 */
#include "shell32_private.h"

#define PIDL_PATH_SIGNATURE 0x454D494Du    /* "MIME" */
#define PIDL_VIRTUAL_SIGNATURE 0x564D494Du /* "MIMV" */

typedef struct _SH_FOLDER {
    int Csidl;
    const GUID *Id;
    /* the User Shell Folders value overriding the default, or NULL. */
    LPCWSTR Value;
    BOOL Common;
    /* the default path, NULL for a virtual folder that has none. */
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
    {CSIDL_FONTS, &FOLDERID_Fonts, NULL, FALSE, L"%SystemRoot%\\Fonts"},
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
    {NO_CSIDL, &FOLDERID_LocalAppDataLow, NULL, FALSE, L"%USERPROFILE%\\AppData\\LocalLow"},
    {CSIDL_INTERNET_CACHE, &FOLDERID_InternetCache, L"Cache", FALSE,
     L"%USERPROFILE%\\AppData\\Local\\Microsoft\\Windows\\INetCache"},
    {CSIDL_COOKIES, &FOLDERID_Cookies, L"Cookies", FALSE,
     L"%USERPROFILE%\\AppData\\Local\\Microsoft\\Windows\\INetCookies"},
    {CSIDL_HISTORY, &FOLDERID_History, L"History", FALSE,
     L"%USERPROFILE%\\AppData\\Local\\Microsoft\\Windows\\History"},
    {CSIDL_COMMON_APPDATA, &FOLDERID_ProgramData, L"Common AppData", TRUE, L"%ProgramData%"},
    {CSIDL_WINDOWS, &FOLDERID_Windows, NULL, FALSE, L"%SystemRoot%"},
    {CSIDL_SYSTEM, &FOLDERID_System, NULL, FALSE, L"%SystemRoot%\\System32"},
    {CSIDL_SYSTEMX86, &FOLDERID_SystemX86, NULL, FALSE, L"%SystemRoot%\\SysWOW64"},
    {CSIDL_PROGRAM_FILES, &FOLDERID_ProgramFiles, NULL, FALSE, L"%ProgramFiles%"},
    {NO_CSIDL, &FOLDERID_ProgramFilesX64, NULL, FALSE, L"%ProgramW6432%"},
    {CSIDL_PROGRAM_FILESX86, &FOLDERID_ProgramFilesX86, NULL, FALSE, L"%ProgramFiles(x86)%"},
    {CSIDL_PROGRAM_FILES_COMMON, &FOLDERID_ProgramFilesCommon, NULL, FALSE, L"%CommonProgramFiles%"},
    {NO_CSIDL, &FOLDERID_ProgramFilesCommonX64, NULL, FALSE, L"%CommonProgramW6432%"},
    {CSIDL_PROGRAM_FILES_COMMONX86, &FOLDERID_ProgramFilesCommonX86, NULL, FALSE, L"%CommonProgramFiles(x86)%"},
    {CSIDL_MYPICTURES, &FOLDERID_Pictures, L"My Pictures", FALSE, L"%USERPROFILE%\\Pictures"},
    {CSIDL_PROFILE, &FOLDERID_Profile, NULL, FALSE, L"%USERPROFILE%"},
    {NO_CSIDL, &FOLDERID_UserProfiles, NULL, FALSE, L"%SystemDrive%\\Users"},
    {NO_CSIDL, &FOLDERID_Public, NULL, FALSE, L"%PUBLIC%"},
    {CSIDL_COMMON_TEMPLATES, &FOLDERID_CommonTemplates, L"Common Templates", TRUE,
     L"%ProgramData%\\Microsoft\\Windows\\Templates"},
    {CSIDL_COMMON_DOCUMENTS, &FOLDERID_PublicDocuments, L"Common Documents", TRUE, L"%PUBLIC%\\Documents"},
    {NO_CSIDL, &FOLDERID_PublicDownloads, NULL, FALSE, L"%PUBLIC%\\Downloads"},
    {CSIDL_COMMON_MUSIC, &FOLDERID_PublicMusic, L"CommonMusic", TRUE, L"%PUBLIC%\\Music"},
    {CSIDL_COMMON_PICTURES, &FOLDERID_PublicPictures, L"CommonPictures", TRUE, L"%PUBLIC%\\Pictures"},
    {CSIDL_COMMON_VIDEO, &FOLDERID_PublicVideos, L"CommonVideo", TRUE, L"%PUBLIC%\\Videos"},
    {CSIDL_RESOURCES, &FOLDERID_ResourceDir, NULL, FALSE, L"%SystemRoot%\\Resources"},
    {CSIDL_CDBURN_AREA, &FOLDERID_CDBurning, L"CD Burning", FALSE,
     L"%USERPROFILE%\\AppData\\Local\\Microsoft\\Windows\\Burn\\Burn"},
    {NO_CSIDL, &FOLDERID_Downloads, L"{374DE290-123F-4565-9164-39C4925E467B}", FALSE, L"%USERPROFILE%\\Downloads"},
    {NO_CSIDL, &FOLDERID_SavedGames, L"{4C5C32FF-BB9D-43B0-B5B4-2D72E54EAAA4}", FALSE,
     L"%USERPROFILE%\\Saved Games"},
    {NO_CSIDL, &FOLDERID_Links, L"{BFB9D5E0-C6A9-404C-B2B2-AE6DB6AF4968}", FALSE, L"%USERPROFILE%\\Links"},
    {CSIDL_DRIVES, &FOLDERID_ComputerFolder, NULL, FALSE, NULL},
    {CSIDL_NETWORK, &FOLDERID_NetworkFolder, NULL, FALSE, NULL},
    {CSIDL_CONTROLS, &FOLDERID_ControlPanelFolder, NULL, FALSE, NULL},
    {CSIDL_PRINTERS, &FOLDERID_PrintersFolder, NULL, FALSE, NULL},
    {CSIDL_BITBUCKET, &FOLDERID_RecycleBinFolder, NULL, FALSE, NULL},
};

#define SHELL_FOLDERS_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders"
/* the token value that names the default user profile. */
#define DEFAULT_USER_TOKEN ((HANDLE)(LONG_PTR)-1)

static const SH_FOLDER *folder_by_csidl(int csidl)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_folders); i++)
        if (g_folders[i].Csidl == csidl)
            return &g_folders[i];
    return NULL;
}

static const SH_FOLDER *folder_by_id(REFKNOWNFOLDERID id)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_folders); i++)
        if (IsEqualGUID(g_folders[i].Id, id))
            return &g_folders[i];
    return NULL;
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
        return NULL;
    int n = MultiByteToWideChar(CP_ACP, 0, text, -1, NULL, 0);
    LPWSTR wide = n > 0 ? ShAlloc(n * sizeof(WCHAR)) : NULL;
    if (wide)
        MultiByteToWideChar(CP_ACP, 0, text, -1, wide, n);
    return wide;
}

/* expands the variables of a folder template; the default user profile replaces %USERPROFILE%. */
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
    LSTATUS status = RegQueryValueExW(key, folder->Value, NULL, &type, (BYTE *)raw, &bytes);
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
        if (!CreateDirectoryW(path, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
            DWORD error = GetLastError();
            *p = L'\\';
            return HRESULT_FROM_WIN32(error);
        }
        *p = L'\\';
    }
    if (!CreateDirectoryW(path, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}

/* the path of a folder with KF_FLAG semantics, into a MAX_PATH buffer. */
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
    *path = NULL;
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
    if (SUCCEEDED(hr) && !WideCharToMultiByte(CP_ACP, 0, wide, -1, path, MAX_PATH, NULL, NULL)) {
        path[0] = 0;
        hr = E_FAIL;
    }
    return hr;
}

SHSTDAPI BOOL WINAPI SHGetSpecialFolderPathW(HWND hwnd, LPWSTR path, int csidl, BOOL create)
{
    return SUCCEEDED(SHGetFolderPathW(hwnd, csidl | (create ? CSIDL_FLAG_CREATE : 0), NULL, SHGFP_TYPE_CURRENT, path));
}

SHSTDAPI BOOL WINAPI SHGetSpecialFolderPathA(HWND hwnd, LPSTR path, int csidl, BOOL create)
{
    return SUCCEEDED(SHGetFolderPathA(hwnd, csidl | (create ? CSIDL_FLAG_CREATE : 0), NULL, SHGFP_TYPE_CURRENT, path));
}

/* ------------------------------------------------------------------ item id lists */

static PIDLIST_ABSOLUTE make_list(DWORD signature, const void *data, SIZE_T bytes)
{
    SIZE_T item = sizeof(USHORT) + sizeof(DWORD) + bytes;
    if (item > 0xFFFF)
        return NULL;
    BYTE *list = ShAllocZero(item + sizeof(USHORT));
    if (!list)
        return NULL;
    *(USHORT *)list = (USHORT)item;
    memcpy(list + sizeof(USHORT), &signature, sizeof(signature));
    memcpy(list + sizeof(USHORT) + sizeof(DWORD), data, bytes);
    return (PIDLIST_ABSOLUTE)list;
}

PIDLIST_ABSOLUTE ShIdListFromPath(LPCWSTR path)
{
    return make_list(PIDL_PATH_SIGNATURE, path, (wcslen(path) + 1) * sizeof(WCHAR));
}

static PIDLIST_ABSOLUTE virtual_list(const SH_FOLDER *folder)
{
    DWORD index = (DWORD)(folder - g_folders);
    return make_list(PIDL_VIRTUAL_SIGNATURE, &index, sizeof(index));
}

static PIDLIST_ABSOLUTE desktop_list(void)
{
    return ShAllocZero(sizeof(USHORT));
}

static DWORD list_signature(PCUIDLIST_ABSOLUTE pidl)
{
    DWORD signature = 0;
    if (pidl->mkid.cb >= sizeof(USHORT) + sizeof(DWORD))
        memcpy(&signature, pidl->mkid.abID, sizeof(signature));
    return signature;
}

BOOL ShPathFromIdList(PCUIDLIST_ABSOLUTE pidl, LPWSTR path, DWORD chars)
{
    if (!path || !chars)
        return FALSE;
    path[0] = 0;
    if (!pidl)
        return FALSE;
    WCHAR buffer[MAX_PATH];
    LPCWSTR source;
    if (!pidl->mkid.cb) {
        if (FAILED(resolve(folder_by_csidl(CSIDL_DESKTOPDIRECTORY), KF_FLAG_DONT_VERIFY, NULL, buffer)))
            return FALSE;
        source = buffer;
    } else if (list_signature(pidl) == PIDL_PATH_SIGNATURE) {
        source = (LPCWSTR)(pidl->mkid.abID + sizeof(DWORD));
    } else {
        return FALSE;
    }
    SIZE_T length = wcslen(source);
    if (length >= chars)
        return FALSE;
    memcpy(path, source, (length + 1) * sizeof(WCHAR));
    return TRUE;
}

SHSTDAPI UINT WINAPI ILGetSize(PCUIDLIST_RELATIVE pidl)
{
    if (!pidl)
        return 0;
    UINT size = sizeof(USHORT);
    for (const BYTE *p = (const BYTE *)pidl; *(const USHORT *)p; p += *(const USHORT *)p)
        size += *(const USHORT *)p;
    return size;
}

SHSTDAPI PIDLIST_RELATIVE WINAPI ILClone(PCUIDLIST_RELATIVE pidl)
{
    if (!pidl)
        return NULL;
    UINT size = ILGetSize(pidl);
    PIDLIST_RELATIVE copy = ShAlloc(size);
    if (copy)
        memcpy(copy, pidl, size);
    return copy;
}

SHSTDAPI void WINAPI ILFree(PIDLIST_RELATIVE pidl)
{
    ShFree(pidl);
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
    if (!WideCharToMultiByte(CP_ACP, 0, wide, -1, path, MAX_PATH, NULL, NULL)) {
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
    *pidl = NULL;
    if (out)
        *out = 0;
    if (!name)
        return E_INVALIDARG;
    WCHAR full[MAX_PATH];
    DWORD n = GetFullPathNameW(name, MAX_PATH, full, NULL);
    if (!n || n >= MAX_PATH)
        return HRESULT_FROM_WIN32(ERROR_INVALID_NAME);
    DWORD attributes = GetFileAttributesW(full);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    *pidl = ShIdListFromPath(full);
    if (!*pidl)
        return E_OUTOFMEMORY;
    if (out)
        *out = ShAttributesFromFile(attributes) & in;
    return S_OK;
}

SHSTDAPI PIDLIST_ABSOLUTE WINAPI ILCreateFromPathW(PCWSTR path)
{
    PIDLIST_ABSOLUTE pidl;
    return SUCCEEDED(SHParseDisplayName(path, NULL, &pidl, 0, NULL)) ? pidl : NULL;
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
    *pidl = NULL;
    if (reserved)
        return E_INVALIDARG;
    const SH_FOLDER *folder = folder_by_csidl(csidl & ~CSIDL_FLAG_MASK);
    if (!folder)
        return E_INVALIDARG;
    return folder_list(folder, csidl_flags(csidl), token, pidl);
}

SHSTDAPI HRESULT WINAPI SHGetSpecialFolderLocation(HWND hwnd, int csidl, PIDLIST_ABSOLUTE *pidl)
{
    return SHGetFolderLocation(hwnd, csidl, NULL, 0, pidl);
}

SHSTDAPI HRESULT WINAPI SHGetKnownFolderIDList(REFKNOWNFOLDERID id, DWORD flags, HANDLE token,
                                               PIDLIST_ABSOLUTE *pidl)
{
    if (!pidl)
        return E_INVALIDARG;
    *pidl = NULL;
    if (!id)
        return E_INVALIDARG;
    const SH_FOLDER *folder = folder_by_id(id);
    if (!folder)
        return E_INVALIDARG;
    return folder_list(folder, flags, token, pidl);
}
