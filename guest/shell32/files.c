/*
 * file information, dropped file lists and stock icons.
 *
 * there are no shell icons or system image list yet: requests for an
 * icon, an icon index or an icon location fail, so callers fall back to
 * their own icons, while names, types and attributes still come back.
 */
#include "shell32_private.h"

/* ------------------------------------------------------------------ dropped files */

/* the index-th name of a DROPFILES list, or the count when index is ~0. */
static UINT query_drop(HDROP drop, UINT index, LPWSTR wide, LPSTR ansi, UINT chars)
{
    DROPFILES *files = drop ? GlobalLock((HGLOBAL)drop) : nullptr;
    if (!files)
        return 0;
    SIZE_T size = GlobalSize((HGLOBAL)drop);
    if (files->pFiles >= size) {
        GlobalUnlock((HGLOBAL)drop);
        return 0;
    }
    const BYTE *start = (const BYTE *)files + files->pFiles, *end = (const BYTE *)files + size;
    UINT count = 0, result = 0;
    WCHAR name[MAX_PATH * 4];
    for (const BYTE *p = start; p < end;) {
        SIZE_T length = 0;
        if (files->fWide) {
            const WCHAR *s = (const WCHAR *)p;
            while ((const BYTE *)(s + length) < end && s[length])
                length++;
            if (!length)
                break;
            if (count == index) {
                SIZE_T n = length < RTL_NUMBER_OF(name) - 1 ? length : RTL_NUMBER_OF(name) - 1;
                memcpy(name, s, n * sizeof(WCHAR));
                name[n] = 0;
            }
            p += (length + 1) * sizeof(WCHAR);
        } else {
            const char *s = (const char *)p;
            while ((const BYTE *)(s + length) < end && s[length])
                length++;
            if (!length)
                break;
            if (count == index) {
                int n = MultiByteToWideChar(CP_ACP, 0, s, (int)length, name, RTL_NUMBER_OF(name) - 1);
                name[n > 0 ? n : 0] = 0;
            }
            p += length + 1;
        }
        if (count++ != index)
            continue;
        if (ansi) {
            int needed = WideCharToMultiByte(CP_ACP, 0, name, -1, nullptr, 0, nullptr, nullptr);
            if (needed <= 0) {
                result = 0;
            } else if (!chars) {
                result = (UINT)needed - 1;
            } else {
                int n = WideCharToMultiByte(CP_ACP, 0, name, -1, ansi, (int)chars, nullptr, nullptr);
                ansi[chars - 1] = 0;
                result = n > 0 ? (UINT)n - 1 : (UINT)strlen(ansi);
            }
        } else {
            UINT n = (UINT)wcslen(name);
            if (!wide || !chars) {
                result = n;
            } else {
                if (n >= chars)
                    n = chars - 1;
                memcpy(wide, name, n * sizeof(WCHAR));
                wide[n] = 0;
                result = n;
            }
        }
        break;
    }
    GlobalUnlock((HGLOBAL)drop);
    return index == 0xFFFFFFFF ? count : result;
}

SHSTDAPI UINT WINAPI DragQueryFileW(HDROP drop, UINT index, LPWSTR file, UINT chars)
{
    return query_drop(drop, index, file, nullptr, file ? chars : 0);
}

SHSTDAPI UINT WINAPI DragQueryFileA(HDROP drop, UINT index, LPSTR file, UINT chars)
{
    char none;
    return query_drop(drop, index, nullptr, file ? file : &none, file ? chars : 0);
}

SHSTDAPI BOOL WINAPI DragQueryPoint(HDROP drop, POINT *pt)
{
    DROPFILES *files = drop ? GlobalLock((HGLOBAL)drop) : nullptr;
    if (!files)
        return FALSE;
    if (pt)
        *pt = files->pt;
    BOOL client = !files->fNC;
    GlobalUnlock((HGLOBAL)drop);
    return client;
}

SHSTDAPI void WINAPI DragFinish(HDROP drop)
{
    if (drop)
        GlobalFree((HGLOBAL)drop);
}

SHSTDAPI void WINAPI DragAcceptFiles(HWND hwnd, BOOL accept)
{
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (accept)
        style |= WS_EX_ACCEPTFILES;
    else
        style &= ~(LONG_PTR)WS_EX_ACCEPTFILES;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, style);
}

/* ------------------------------------------------------------------ SHGetFileInfo */

static BOOL is_root(LPCWSTR path)
{
    return path[0] && path[1] == L':' && (!path[2] || (path[2] == L'\\' && !path[3]));
}

static BOOL class_string(LPCWSTR subkey, LPWSTR out, DWORD chars)
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, subkey, 0, KEY_READ, &key))
        return FALSE;
    DWORD type, bytes = (chars - 1) * sizeof(WCHAR);
    LSTATUS status = RegQueryValueExW(key, nullptr, nullptr, &type, (BYTE *)out, &bytes);
    RegCloseKey(key);
    if (status || type != REG_SZ || bytes < 2 * sizeof(WCHAR))
        return FALSE;
    out[bytes / sizeof(WCHAR)] = 0;
    return TRUE;
}

/* the friendly type of the file's class, else "EXT File" like windows, or "File". */
static void type_name(LPCWSTR path, DWORD attributes, LPWSTR out)
{
    if (is_root(path)) {
        wcscpy(out, L"Local Disk");
        return;
    }
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
        wcscpy(out, L"File folder");
        return;
    }
    LPCWSTR dot = wcsrchr(path, L'.');
    if (!dot || wcschr(dot, L'\\') || !dot[1]) {
        wcscpy(out, L"File");
        return;
    }
    WCHAR progid[MAX_PATH], name[80];
    if (class_string(dot, progid, RTL_NUMBER_OF(progid)) && class_string(progid, name, RTL_NUMBER_OF(name))) {
        wcscpy(out, name);
        return;
    }
    SIZE_T n = 0;
    for (LPCWSTR p = dot + 1; *p && n < 70; p++)
        out[n++] = towupper(*p);
    wcscpy(out + n, L" File");
}

/* SHGFI_EXETYPE: "PE" or "MZ" in the low word, the subsystem version for windows programs in the high word. */
static DWORD_PTR exe_type(LPCWSTR path)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return 0;
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS64 nt;
    DWORD read;
    DWORD_PTR result = 0;
    if (ReadFile(file, &dos, sizeof(dos), &read, nullptr) && read == sizeof(dos) && dos.e_magic == 0x5A4D) {
        result = 0x5A4D;
        LARGE_INTEGER offset = {.QuadPart = dos.e_lfanew};
        if (SetFilePointerEx(file, offset, nullptr, FILE_BEGIN) && ReadFile(file, &nt, sizeof(nt), &read, nullptr) &&
            read >= offsetof(IMAGE_NT_HEADERS64, OptionalHeader.Subsystem) + sizeof(USHORT) &&
            nt.Signature == IMAGE_NT_SIGNATURE) {
            result = 0x4550;
            if (nt.OptionalHeader.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_GUI)
                result |= (DWORD_PTR)(nt.OptionalHeader.MajorSubsystemVersion << 8 |
                                      (nt.OptionalHeader.MinorSubsystemVersion & 0xFF))
                          << 16;
        }
    }
    CloseHandle(file);
    return result;
}

SHSTDAPI DWORD_PTR WINAPI SHGetFileInfoW(LPCWSTR path, DWORD attributes, SHFILEINFOW *info, UINT size, UINT flags)
{
    WCHAR full[MAX_PATH];
    if (!path)
        return 0;
    if (flags & SHGFI_PIDL) {
        if (!ShPathFromIdList((PCIDLIST_ABSOLUTE)path, full, MAX_PATH))
            return 0;
    } else {
        DWORD n = GetFullPathNameW(path, MAX_PATH, full, nullptr);
        if (!n || n >= MAX_PATH)
            return 0;
    }
    if (!(flags & SHGFI_USEFILEATTRIBUTES)) {
        attributes = GetFileAttributesW(full);
        if (attributes == INVALID_FILE_ATTRIBUTES)
            return 0;
    }
    if (flags & SHGFI_EXETYPE)
        return (attributes & FILE_ATTRIBUTE_DIRECTORY) ? 0 : exe_type(full);
    if (!info || size < sizeof(*info))
        return 0;
    DWORD_PTR result = 1;
    if (flags & (SHGFI_ICON | SHGFI_SYSICONINDEX | SHGFI_ICONLOCATION)) {
        info->hIcon = nullptr;
        info->iIcon = 0;
        result = 0;
    }
    if (flags & SHGFI_DISPLAYNAME) {
        if (is_root(full)) {
            _snwprintf(info->szDisplayName, MAX_PATH, L"Local Disk (%lc:)", full[0]);
        } else {
            SIZE_T length = wcslen(full);
            if (length > 3 && full[length - 1] == L'\\')
                full[--length] = 0;
            LPCWSTR slash = wcsrchr(full, L'\\');
            wcscpy(info->szDisplayName, slash ? slash + 1 : full);
        }
    }
    if (flags & SHGFI_TYPENAME)
        type_name(full, attributes, info->szTypeName);
    if (flags & SHGFI_ATTRIBUTES) {
        SFGAOF value = ShAttributesFromFile(attributes);
        if (flags & SHGFI_ATTR_SPECIFIED)
            value &= info->dwAttributes;
        info->dwAttributes = value;
    }
    return result;
}

SHSTDAPI DWORD_PTR WINAPI SHGetFileInfoA(LPCSTR path, DWORD attributes, SHFILEINFOA *info, UINT size, UINT flags)
{
    SHFILEINFOW wide = {};
    LPWSTR wide_path = (flags & SHGFI_PIDL) ? (LPWSTR)path : ShWiden(path);
    if (!wide_path)
        return 0;
    if (info && size >= sizeof(*info))
        wide.dwAttributes = info->dwAttributes;
    DWORD_PTR result = SHGetFileInfoW(wide_path, attributes, info ? &wide : nullptr, sizeof(wide), flags);
    if (!(flags & SHGFI_PIDL))
        ShFree(wide_path);
    if (info && size >= sizeof(*info) && !(flags & SHGFI_EXETYPE)) {
        info->hIcon = wide.hIcon;
        info->iIcon = wide.iIcon;
        info->dwAttributes = wide.dwAttributes;
        WideCharToMultiByte(CP_ACP, 0, wide.szDisplayName, -1, info->szDisplayName, MAX_PATH, nullptr, nullptr);
        WideCharToMultiByte(CP_ACP, 0, wide.szTypeName, -1, info->szTypeName, 80, nullptr, nullptr);
    }
    return result;
}

/* ------------------------------------------------------------------ stock icons */

/* only the stock icons user32 has a system icon for can be given, as a copy the caller destroys. */
SHSTDAPI HRESULT WINAPI SHGetStockIconInfo(SHSTOCKICONID id, UINT flags, SHSTOCKICONINFO *info)
{
    if (!info || info->cbSize != sizeof(*info) || (UINT)id >= SIID_MAX_ICONS)
        return E_INVALIDARG;
    LPCWSTR system = nullptr;
    switch (id) {
    case SIID_APPLICATION:
        system = IDI_APPLICATION;
        break;
    case SIID_HELP:
        system = IDI_QUESTION;
        break;
    case SIID_SHIELD:
        system = IDI_SHIELD;
        break;
    case SIID_WARNING:
        system = IDI_WARNING;
        break;
    case SIID_INFO:
        system = IDI_INFORMATION;
        break;
    case SIID_ERROR:
        system = IDI_ERROR;
        break;
    default:
        break;
    }
    info->hIcon = nullptr;
    info->iSysImageIndex = -1;
    info->iIcon = 0;
    info->szPath[0] = 0;
    /* there is no icon file to name, so only requests for the icon itself succeed. */
    if (!system || !(flags & SHGSI_ICON) || (flags & SHGSI_SYSICONINDEX))
        return HRESULT_FROM_WIN32(ERROR_RESOURCE_NAME_NOT_FOUND);
    HICON shared = LoadIconW(nullptr, system);
    info->hIcon = shared ? CopyIcon(shared) : nullptr;
    if (!info->hIcon)
        return HRESULT_FROM_WIN32(ERROR_RESOURCE_NAME_NOT_FOUND);
    return S_OK;
}
