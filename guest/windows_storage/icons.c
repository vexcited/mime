#include "windows_storage_private.h"
#include "../shell32/stockicons.h"

UINT WINAPI PrivateExtractIconsW(LPCWSTR file, int index, int cx, int cy, HICON *icons, UINT *ids, UINT count, UINT flags);

/* a module of the system named as windows names it, C:\WINDOWS\system32\imageres.dll. */
static void system_path(LPCWSTR name, WCHAR *path, UINT chars)
{
    UINT n = GetSystemDirectoryW(path, chars);
    if (!n || n + 1 + lstrlenW(name) >= chars) {
        lstrcpynW(path, name, chars);
        return;
    }
    path[n] = L'\\';
    lstrcpynW(path + n + 1, name, chars - n - 1);
}

static void find_file(LPCWSTR file, WCHAR *path, UINT chars)
{
    if (!SearchPathW(nullptr, file, nullptr, chars, path, nullptr))
        lstrcpynW(path, file, chars);
}

static HICON extract_one(LPCWSTR path, int index, int cx, int cy)
{
    HICON icon = nullptr;
    UINT id;
    if (PrivateExtractIconsW(path, index, cx, cy, &icon, &id, 1, 0) != 1)
        return nullptr;
    return icon;
}

/*
 * genuine shell32 of windows 11 takes the stock icons and the icons of files from windows.storage. the pictures are
 * microsoft's, by the ids windows gives them, which the loader finds in the .mun files of SystemResources.
 */
HRESULT WINAPI StorageGetStockIconInfo(SHSTOCKICONID id, UINT flags, SHSTOCKICONINFO *info)
{
    if (!info || info->cbSize != sizeof(*info))
        return E_INVALIDARG;
    if ((UINT)id >= RTL_NUMBER_OF(g_stock_icons)) {
        info->iIcon = -1;
        return E_INVALIDARG;
    }
    system_path(g_stock_modules[g_stock_icons[id].Module], info->szPath, RTL_NUMBER_OF(info->szPath));
    info->iIcon = -(int)g_stock_icons[id].Resource;
    info->hIcon = nullptr;
    if (flags & SHGSI_ICON) {
        BOOL small = (flags & SHGSI_SMALLICON) != 0;
        int cx = GetSystemMetrics(small ? SM_CXSMICON : SM_CXICON), cy = GetSystemMetrics(small ? SM_CYSMICON : SM_CYICON);
        info->hIcon = extract_one(info->szPath, info->iIcon, cx, cy);
        if (!info->hIcon)
            return HRESULT_FROM_WIN32(GetLastError() ? GetLastError() : ERROR_RESOURCE_TYPE_NOT_FOUND);
    }
    return S_OK;
}

UINT WINAPI StorageExtractIconExW(LPCWSTR file, int index, HICON *large, HICON *small, UINT count)
{
    WCHAR path[MAX_PATH];
    if (!file)
        return 0;
    find_file(file, path, RTL_NUMBER_OF(path));
    if (index == -1 && !large && !small)
        return PrivateExtractIconsW(path, 0, 0, 0, nullptr, nullptr, 0, 0);
    UINT done = 0;
    if (large)
        for (UINT i = 0; i < count; i++)
            done += (large[i] = extract_one(path, index < 0 ? index : index + (int)i, GetSystemMetrics(SM_CXICON),
                                            GetSystemMetrics(SM_CYICON))) != nullptr;
    if (small)
        for (UINT i = 0; i < count; i++)
            done += (small[i] = extract_one(path, index < 0 ? index : index + (int)i, GetSystemMetrics(SM_CXSMICON),
                                            GetSystemMetrics(SM_CYSMICON))) != nullptr;
    return done;
}

static WCHAR *wide_of(LPCSTR text, WCHAR *out, UINT chars)
{
    if (!text || !MultiByteToWideChar(CP_ACP, 0, text, -1, out, (int)chars))
        return nullptr;
    return out;
}

UINT WINAPI StorageExtractIconExA(LPCSTR file, int index, HICON *large, HICON *small, UINT count)
{
    WCHAR wide[MAX_PATH];
    return StorageExtractIconExW(wide_of(file, wide, MAX_PATH), index, large, small, count);
}

/* an index of -1 asks how many icons the file has, given back as the handle. */
HICON WINAPI StorageExtractIconW(HINSTANCE instance, LPCWSTR file, UINT index)
{
    if ((int)index == -1)
        return (HICON)(ULONG_PTR)StorageExtractIconExW(file, -1, nullptr, nullptr, 0);
    HICON icon = nullptr;
    return StorageExtractIconExW(file, (int)index, &icon, nullptr, 1) ? icon : nullptr;
}

HICON WINAPI StorageExtractIconA(HINSTANCE instance, LPCSTR file, UINT index)
{
    WCHAR wide[MAX_PATH];
    return StorageExtractIconW(instance, wide_of(file, wide, MAX_PATH), index);
}
