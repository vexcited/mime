/*
 * shell helpers outside the namespace: directories, icons, recent documents
 * and the administrator check.
 */
#include "shell32_private.h"
#include <mime/stub.h>

typedef BOOL(WINAPI *CHECK_MEMBERSHIP)(HANDLE, PVOID, PBOOL);

/* whether the process token holds the enabled administrators group. */
SHSTDAPI BOOL WINAPI IsUserAnAdmin(void)
{
    static const BYTE admins[16] = {1, 2, 0, 0, 0, 0, 0, 5, 32, 0, 0, 0, 0x20, 2, 0, 0};
    HMODULE advapi32 = LoadLibraryW(L"advapi32.dll");
    CHECK_MEMBERSHIP check =
        advapi32 ? (CHECK_MEMBERSHIP)(void *)GetProcAddress(advapi32, "CheckTokenMembership") : nullptr;
    BOOL member = FALSE;
    if (check && !check(nullptr, (PVOID)admins, &member))
        member = FALSE;
    if (advapi32)
        FreeLibrary(advapi32);
    return member;
}

/* creates every missing directory of an absolute path. */
SHSTDAPI int WINAPI SHCreateDirectoryExW(HWND hwnd, LPCWSTR path, const SECURITY_ATTRIBUTES *attributes)
{
    if (!path || !path[0])
        return ERROR_BAD_PATHNAME;
    WCHAR full[MAX_PATH];
    if (path[0] != L'\\' && path[1] != L':')
        return ERROR_BAD_PATHNAME;
    DWORD n = GetFullPathNameW(path, MAX_PATH, full, nullptr);
    if (!n || n >= MAX_PATH)
        return ERROR_FILENAME_EXCED_RANGE;
    if (GetFileAttributesW(full) != INVALID_FILE_ATTRIBUTES)
        return ERROR_ALREADY_EXISTS;
    SIZE_T length = wcslen(full);
    if (length > 3 && full[length - 1] == L'\\')
        full[--length] = 0;
    /* the first component after the root is created first, so start past it. */
    LPWSTR p = full[0] == L'\\' && full[1] == L'\\' ? wcschr(full + 2, L'\\') : full + 2;
    if (p && full[0] == L'\\' && full[1] == L'\\')
        p = wcschr(p + 1, L'\\');
    for (; p && *p; p++) {
        if (*p != L'\\' || p == full + 2)
            continue;
        *p = 0;
        if (!CreateDirectoryW(full, (SECURITY_ATTRIBUTES *)attributes) && GetLastError() != ERROR_ALREADY_EXISTS) {
            DWORD error = GetLastError();
            *p = L'\\';
            return error;
        }
        *p = L'\\';
    }
    if (!CreateDirectoryW(full, (SECURITY_ATTRIBUTES *)attributes))
        return GetLastError();
    return ERROR_SUCCESS;
}

SHSTDAPI int WINAPI SHCreateDirectoryExA(HWND hwnd, LPCSTR path, const SECURITY_ATTRIBUTES *attributes)
{
    LPWSTR wide = ShWiden(path);
    if (path && !wide)
        return ERROR_NOT_ENOUGH_MEMORY;
    int result = SHCreateDirectoryExW(hwnd, wide, attributes);
    ShFree(wide);
    return result;
}

SHSTDAPI int WINAPI SHCreateDirectory(HWND hwnd, LPCWSTR path)
{
    return SHCreateDirectoryExW(hwnd, path, nullptr);
}

/* there is no recent documents list or jump list to add to. */
SHSTDAPI void WINAPI SHAddToRecentDocs(UINT flags, LPCVOID data)
{
}

SHSTDAPI HRESULT WINAPI SHCreateItemInKnownFolder(REFKNOWNFOLDERID folder, DWORD flags, PCWSTR name, REFIID riid,
                                                  void **object)
{
    if (!object)
        return E_INVALIDARG;
    *object = nullptr;
    PWSTR base;
    HRESULT hr = SHGetKnownFolderPath(folder, flags, nullptr, &base);
    if (FAILED(hr))
        return hr;
    WCHAR path[MAX_PATH];
    if (wcslen(base) + (name ? wcslen(name) + 1 : 0) >= MAX_PATH) {
        ShFree(base);
        return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
    }
    if (name && name[0])
        _snwprintf(path, MAX_PATH, L"%ls\\%ls", base, name);
    else
        wcscpy(path, base);
    path[MAX_PATH - 1] = 0;
    ShFree(base);
    return SHCreateItemFromParsingName(path, nullptr, riid, object);
}

/* the desktop folder object needs an IShellFolder namespace, which is not implemented. */
SHSTDAPI HRESULT WINAPI SHGetDesktopFolder(void **folder)
{
    MIME_STUB("shell32");
    if (!folder)
        return E_INVALIDARG;
    *folder = nullptr;
    return E_NOTIMPL;
}

typedef struct {
    UINT Wanted;
    UINT Seen;
    LPWSTR Name;
} ICON_SEARCH;

static BOOL CALLBACK find_icon_group(HMODULE module, LPCWSTR type, LPWSTR name, LONG_PTR param)
{
    ICON_SEARCH *search = (ICON_SEARCH *)param;
    if (search->Seen++ != search->Wanted)
        return TRUE;
    search->Name = IS_INTRESOURCE(name) ? name : ShDuplicate(name);
    return FALSE;
}

/*
 * an icon of an executable, a library or an icon file. a negative index
 * names a resource id, -1 asks for the number of icons. returns 1 for a
 * file without icon resources, as windows does.
 */
SHSTDAPI HICON WINAPI ExtractIconW(HINSTANCE instance, LPCWSTR file, UINT index)
{
    if (!file)
        return nullptr;
    WCHAR path[MAX_PATH];
    DWORD n = SearchPathW(nullptr, file, nullptr, MAX_PATH, path, nullptr);
    if (!n || n >= MAX_PATH)
        return nullptr;
    int cx = GetSystemMetrics(SM_CXICON), cy = GetSystemMetrics(SM_CYICON);
    LPCWSTR dot = wcsrchr(path, L'.');
    if (dot && !_wcsicmp(dot, L".ico")) {
        if (index == (UINT)-1)
            return (HICON)1;
        return index ? nullptr : (HICON)LoadImageW(nullptr, path, IMAGE_ICON, cx, cy, LR_LOADFROMFILE);
    }
    HMODULE module = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!module)
        return (HICON)1;
    HICON icon = nullptr;
    if (index == (UINT)-1) {
        ICON_SEARCH search = {(UINT)-1, 0, nullptr};
        EnumResourceNamesW(module, MAKEINTRESOURCEW(14), find_icon_group, (LONG_PTR)&search);
        icon = (HICON)(UINT_PTR)search.Seen;
    } else if ((int)index < 0) {
        icon = (HICON)LoadImageW(module, MAKEINTRESOURCEW(-(int)index), IMAGE_ICON, cx, cy, 0);
    } else {
        ICON_SEARCH search = {index, 0, nullptr};
        EnumResourceNamesW(module, MAKEINTRESOURCEW(14), find_icon_group, (LONG_PTR)&search);
        if (search.Name) {
            icon = (HICON)LoadImageW(module, search.Name, IMAGE_ICON, cx, cy, 0);
            if (!IS_INTRESOURCE(search.Name))
                ShFree(search.Name);
        } else if (!search.Seen) {
            icon = (HICON)1;
        }
    }
    FreeLibrary(module);
    return icon;
}

/*
 * icons of a file at the sizes of a large and a small icon. With no array it counts the icons, as an index of -1
 * does for ExtractIconW; a negative index names a resource id and takes one icon; else `count` icons from the
 * index on. The result counts the icons that were made, large and small each, which is a guess from how programs use it
 * (two for one icon of both sizes). An icon file has the one icon.
 */
SHSTDAPI UINT WINAPI ExtractIconExW(LPCWSTR file, int index, HICON *large, HICON *small, UINT count)
{
    if (large)
        for (UINT i = 0; i < count; i++)
            large[i] = nullptr;
    if (small)
        for (UINT i = 0; i < count; i++)
            small[i] = nullptr;
    if (!file)
        return 0;
    WCHAR path[MAX_PATH];
    DWORD n = SearchPathW(nullptr, file, nullptr, MAX_PATH, path, nullptr);
    if (!n || n >= MAX_PATH)
        return 0;
    int lx = GetSystemMetrics(SM_CXICON), ly = GetSystemMetrics(SM_CYICON);
    int sx = GetSystemMetrics(SM_CXSMICON), sy = GetSystemMetrics(SM_CYSMICON);
    LPCWSTR dot = wcsrchr(path, L'.');
    BOOL icon_file = dot && !_wcsicmp(dot, L".ico");
    HMODULE module = icon_file ? nullptr : LoadLibraryExW(path, nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!icon_file && !module)
        return 0;
    /* the groups of icons of the file, counted. */
    ICON_SEARCH total = {(UINT)-1, 0, nullptr};
    if (module)
        EnumResourceNamesW(module, MAKEINTRESOURCEW(14), find_icon_group, (LONG_PTR)&total);
    UINT groups = icon_file ? 1 : total.Seen;
    UINT made = 0;
    if (!large && !small) {
        if (module)
            FreeLibrary(module);
        return groups;
    }
    if (index < 0) {
        count = 1;
    } else if ((UINT)index >= groups) {
        count = 0;
    } else if (count > groups - (UINT)index) {
        count = groups - (UINT)index;
    }
    for (UINT i = 0; i < count; i++) {
        HANDLE big = nullptr, little = nullptr;
        if (icon_file) {
            if (large)
                big = LoadImageW(nullptr, path, IMAGE_ICON, lx, ly, LR_LOADFROMFILE);
            if (small)
                little = LoadImageW(nullptr, path, IMAGE_ICON, sx, sy, LR_LOADFROMFILE);
        } else {
            LPWSTR name = nullptr;
            ICON_SEARCH search = {(UINT)index + i, 0, nullptr};
            if (index < 0) {
                name = MAKEINTRESOURCEW(-index);
            } else {
                EnumResourceNamesW(module, MAKEINTRESOURCEW(14), find_icon_group, (LONG_PTR)&search);
                name = search.Name;
            }
            if (name) {
                if (large)
                    big = LoadImageW(module, name, IMAGE_ICON, lx, ly, 0);
                if (small)
                    little = LoadImageW(module, name, IMAGE_ICON, sx, sy, 0);
            }
            if (index >= 0 && search.Name && !IS_INTRESOURCE(search.Name))
                ShFree(search.Name);
        }
        if (large && big) {
            large[i] = (HICON)big;
            made++;
        }
        if (small && little) {
            small[i] = (HICON)little;
            made++;
        }
    }
    if (module)
        FreeLibrary(module);
    return made;
}

SHSTDAPI UINT WINAPI ExtractIconExA(LPCSTR file, int index, HICON *large, HICON *small, UINT count)
{
    LPWSTR wide = ShWiden(file);
    UINT made = wide ? ExtractIconExW(wide, index, large, small, count) : 0;
    ShFree(wide);
    return made;
}

SHSTDAPI HICON WINAPI ExtractIconA(HINSTANCE instance, LPCSTR file, UINT index)
{
    LPWSTR wide = ShWiden(file);
    HICON icon = wide ? ExtractIconW(instance, wide, index) : nullptr;
    ShFree(wide);
    return icon;
}
