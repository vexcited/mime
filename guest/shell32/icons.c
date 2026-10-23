/*
 * icons: where a file's icon comes from, loading an icon at a size, stock icons, and IExtractIcon over a file name.
 *
 * system art lives in imageres.dll, which Windows 11 keeps in SystemResources as imageres.dll.mun. a path naming a
 * library without icons is looked up there too. shell32.dll has no icons of its own here, so its indexes, which are
 * the stock icon numbers, come from the stock table.
 */
#include "shell32_private.h"

typedef struct {
    int Siid;
    /* the imageres.dll icon group, 0 for none. */
    int Resource;
    int Art;
} STOCK_ICON;

static const STOCK_ICON g_stock[] = {
    {SIID_DOCNOASSOC, 2, ART_DOC},
    {SIID_DOCASSOC, 2, ART_DOCASSOC},
    {SIID_APPLICATION, 15, ART_APP},
    {SIID_FOLDER, 3, ART_FOLDER},
    {SIID_FOLDEROPEN, 4, ART_FOLDEROPEN},
    {SIID_DRIVE525, 29, ART_DRIVE_REMOVABLE},
    {SIID_DRIVE35, 28, ART_DRIVE_REMOVABLE},
    {SIID_DRIVEREMOVE, 32, ART_DRIVE_REMOVABLE},
    {SIID_DRIVEFIXED, 35, ART_DRIVE},
    {SIID_DRIVENET, 33, ART_DRIVE_NETWORK},
    {SIID_DRIVENETDISABLED, 31, ART_DRIVE_NETWORK},
    {SIID_DRIVECD, 30, ART_DRIVE_OPTICAL},
    {SIID_DRIVERAM, 34, ART_DRIVE_MEMORY},
    {SIID_WORLD, 25, ART_COMPUTER},
    {SIID_SERVER, 109, ART_COMPUTER},
    {SIID_PRINTER, 51, ART_DOC},
    {SIID_MYNETWORK, 25, ART_COMPUTER},
    {SIID_FIND, 18, ART_FOLDER},
    {SIID_HELP, 99, ART_DOC},
    {SIID_SHARE, 143, ART_FOLDER},
    {SIID_LINK, 163, ART_LINK},
    {SIID_RECYCLER, 54, ART_DOC},
    {SIID_RECYCLERFULL, 55, ART_DOC},
    {SIID_MEDIACDAUDIO, 85, ART_DRIVE_OPTICAL},
    {SIID_LOCK, 59, ART_DOC},
    {SIID_PRINTERNET, 51, ART_DOC},
    {SIID_SERVERSHARE, 143, ART_FOLDER},
    {SIID_PRINTERFAX, 76, ART_DOC},
    {SIID_DRIVEUNKNOWN, 75, ART_DRIVE},
    {SIID_DRIVEDVD, 56, ART_DRIVE_OPTICAL},
    {SIID_MEDIADVD, 56, ART_DRIVE_OPTICAL},
    {SIID_AUDIOFILES, 131, ART_DOC},
    {SIID_IMAGEFILES, 132, ART_DOC},
    {SIID_VIDEOFILES, 133, ART_DOC},
    {SIID_SHIELD, 78, ART_DOC},
    {SIID_WARNING, 84, ART_DOC},
    {SIID_INFO, 81, ART_DOC},
    {SIID_ERROR, 98, ART_DOC},
    {SIID_KEY, 82, ART_DOC},
    {SIID_DESKTOPPC, 109, ART_COMPUTER},
    {SIID_MOBILEPC, 109, ART_COMPUTER},
    {SIID_USERS, 88, ART_DOC},
    {SIID_INTERNET, 25, ART_COMPUTER},
    {SIID_ZIPFILE, 174, ART_FOLDER},
    {SIID_SETTINGS, 114, ART_DOC},
    {SH_STOCK_COMPUTER, 109, ART_COMPUTER},
    {SH_STOCK_DESKTOP, 110, ART_COMPUTER},
    {SH_STOCK_SYSTEMDRIVE, 36, ART_DRIVE},
};

static const STOCK_ICON *find_stock(int siid)
{
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(g_stock); i++)
        if (g_stock[i].Siid == siid)
            return &g_stock[i];
    return nullptr;
}

/* ------------------------------------------------------------------ files and modules */

static LPCWSTR base_name(LPCWSTR path)
{
    LPCWSTR slash = wcsrchr(path, L'\\');
    return slash ? slash + 1 : path;
}

static BOOL has_extension(LPCWSTR path, LPCWSTR extension)
{
    LPCWSTR dot = wcsrchr(base_name(path), L'.');
    return dot && !_wcsicmp(dot, extension);
}

static BOOL is_drive_root(LPCWSTR path)
{
    return path[0] && path[1] == L':' && (!path[2] || (path[2] == L'\\' && !path[3]));
}

static BOOL windows_directory(WCHAR *path, UINT chars)
{
    UINT n = GetWindowsDirectoryW(path, chars);
    return n && n < chars;
}

/* system icon libraries stay mapped: opened for resources, they are never written. */
typedef struct {
    WCHAR Path[MAX_PATH];
    HMODULE Module;
} MODULE_SLOT;

static MODULE_SLOT g_modules[12];
static SRWLOCK g_modules_lock = SRWLOCK_INIT;

static BOOL in_windows_directory(LPCWSTR path)
{
    WCHAR windir[MAX_PATH];
    if (!windows_directory(windir, MAX_PATH))
        return FALSE;
    SIZE_T length = wcslen(windir);
    return !_wcsnicmp(path, windir, length) && path[length] == L'\\';
}

/* maps the file as data, cached when it is a system library. owned says the caller must free it. */
static HMODULE open_module(LPCWSTR path, BOOL *owned)
{
    BOOL cache = in_windows_directory(path);
    *owned = FALSE;
    HMODULE module = nullptr;
    if (cache) {
        AcquireSRWLockExclusive(&g_modules_lock);
        for (SIZE_T i = 0; i < RTL_NUMBER_OF(g_modules); i++)
            if (g_modules[i].Module && !_wcsicmp(g_modules[i].Path, path)) {
                module = g_modules[i].Module;
                break;
            }
        if (!module) {
            module = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
            if (module) {
                *owned = TRUE;
                for (SIZE_T i = 0; i < RTL_NUMBER_OF(g_modules); i++)
                    if (!g_modules[i].Module) {
                        wcscpy(g_modules[i].Path, path);
                        g_modules[i].Module = module;
                        *owned = FALSE;
                        break;
                    }
            }
        }
        ReleaseSRWLockExclusive(&g_modules_lock);
        return module;
    }
    module = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    *owned = module != nullptr;
    return module;
}

static void close_module(HMODULE module, BOOL owned)
{
    if (module && owned)
        FreeLibrary(module);
}

typedef struct {
    UINT Wanted, Seen;
    LPWSTR Name;
} GROUP_SEARCH;

static BOOL CALLBACK group_callback(HMODULE module, LPCWSTR type, LPWSTR name, LONG_PTR param)
{
    GROUP_SEARCH *search = (GROUP_SEARCH *)param;
    if (search->Seen++ != search->Wanted)
        return TRUE;
    search->Name = IS_INTRESOURCE(name) ? name : ShDuplicate(name);
    return FALSE;
}

static UINT count_groups(HMODULE module)
{
    GROUP_SEARCH search = {(UINT)-1, 0, nullptr};
    EnumResourceNamesW(module, MAKEINTRESOURCEW(14), group_callback, (LONG_PTR)&search);
    return search.Seen;
}

/* by position, or by the negated resource id. */
static HICON load_group(HMODULE module, int index, int cx, int cy)
{
    if (index < 0)
        return (HICON)LoadImageW(module, MAKEINTRESOURCEW(-index), IMAGE_ICON, cx, cy, 0);
    GROUP_SEARCH search = {(UINT)index, 0, nullptr};
    EnumResourceNamesW(module, MAKEINTRESOURCEW(14), group_callback, (LONG_PTR)&search);
    HICON icon = nullptr;
    if (search.Name) {
        icon = (HICON)LoadImageW(module, search.Name, IMAGE_ICON, cx, cy, 0);
        if (!IS_INTRESOURCE(search.Name))
            ShFree(search.Name);
    }
    return icon;
}

/* "<windows>\SystemResources\<name>.mun" for a library, where Windows 11 keeps its icons. */
static BOOL resource_file(LPCWSTR path, WCHAR *out)
{
    WCHAR windir[MAX_PATH];
    LPCWSTR name = base_name(path);
    if (!windows_directory(windir, MAX_PATH) || wcslen(windir) + wcslen(name) + 22 >= MAX_PATH)
        return FALSE;
    _snwprintf(out, MAX_PATH, L"%ls\\SystemResources\\%ls.mun", windir, name);
    out[MAX_PATH - 1] = 0;
    return GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES;
}

static HICON load_stock(int siid, int cx, int cy);

/* shell32.dll icon indexes are the stock icons of the same number. */
static BOOL is_shell32(LPCWSTR path)
{
    return !_wcsicmp(base_name(path), L"shell32.dll");
}

/* a name may stand for an icon library that only exists in SystemResources. */
BOOL ShIconFindFile(LPCWSTR file, WCHAR *path)
{
    DWORD n = SearchPathW(nullptr, file, nullptr, MAX_PATH, path, nullptr);
    if (n && n < MAX_PATH)
        return TRUE;
    WCHAR alternative[MAX_PATH];
    if (!resource_file(file, alternative))
        return FALSE;
    if (wcschr(file, L'\\')) {
        if (wcslen(file) >= MAX_PATH)
            return FALSE;
        wcscpy(path, file);
        return TRUE;
    }
    WCHAR windir[MAX_PATH];
    if (!windows_directory(windir, MAX_PATH) || wcslen(windir) + wcslen(file) + 10 >= MAX_PATH)
        return FALSE;
    _snwprintf(path, MAX_PATH, L"%ls\\System32\\%ls", windir, file);
    path[MAX_PATH - 1] = 0;
    return TRUE;
}

UINT ShIconCount(LPCWSTR path)
{
    if (has_extension(path, L".ico") || has_extension(path, L".cur"))
        return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES ? 1 : 0;
    BOOL owned;
    UINT count = 0;
    HMODULE module = open_module(path, &owned);
    if (module) {
        count = count_groups(module);
        close_module(module, owned);
    }
    WCHAR alternative[MAX_PATH];
    if (!count && resource_file(path, alternative)) {
        module = open_module(alternative, &owned);
        if (module) {
            count = count_groups(module);
            close_module(module, owned);
        }
    }
    if (!count && is_shell32(path))
        count = SIID_MAX_ICONS;
    return count;
}

HICON ShIconLoad(LPCWSTR path, int index, int cx, int cy)
{
    if (has_extension(path, L".ico") || has_extension(path, L".cur")) {
        if (index > 0)
            return nullptr;
        return (HICON)LoadImageW(nullptr, path, IMAGE_ICON, cx, cy, LR_LOADFROMFILE);
    }
    BOOL owned;
    HICON icon = nullptr;
    HMODULE module = open_module(path, &owned);
    if (module) {
        icon = load_group(module, index, cx, cy);
        close_module(module, owned);
    }
    WCHAR alternative[MAX_PATH];
    if (!icon && resource_file(path, alternative)) {
        module = open_module(alternative, &owned);
        if (module) {
            icon = load_group(module, index, cx, cy);
            close_module(module, owned);
        }
    }
    if (!icon && is_shell32(path) && index >= 0)
        icon = load_stock(index, cx, cy);
    return icon;
}

/* ------------------------------------------------------------------ pixels and icons */

HICON ShIconFromPixels(const DWORD *pixels, int cx, int cy)
{
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = cx;
    info.bmiHeader.biHeight = -cy;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void *bits;
    HBITMAP color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color)
        return nullptr;
    memcpy(bits, pixels, (SIZE_T)cx * (SIZE_T)cy * sizeof(DWORD));
    SIZE_T stride = (SIZE_T)((cx + 15) / 16) * 2;
    BYTE *zero = ShAllocZero(stride * (SIZE_T)cy);
    HBITMAP mask = zero ? CreateBitmap(cx, cy, 1, 1, zero) : nullptr;
    ShFree(zero);
    HICON icon = nullptr;
    if (mask) {
        ICONINFO icon_info = {TRUE, 0, 0, mask, color};
        icon = CreateIconIndirect(&icon_info);
    }
    if (mask)
        DeleteObject(mask);
    DeleteObject(color);
    return icon;
}

/* premultiplied, freed by the caller. */
DWORD *ShIconPixels(HICON icon, int *cx, int *cy)
{
    ICONINFO info;
    if (!GetIconInfo(icon, &info))
        return nullptr;
    DWORD *pixels = nullptr;
    BITMAP bitmap;
    if (info.hbmColor && GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap) && bitmap.bmWidth > 0 &&
        bitmap.bmHeight > 0) {
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = bitmap.bmWidth;
        bi.bmiHeader.biHeight = -bitmap.bmHeight;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        pixels = ShAlloc((SIZE_T)bitmap.bmWidth * (SIZE_T)bitmap.bmHeight * sizeof(DWORD));
        HDC dc = CreateCompatibleDC(nullptr);
        if (pixels && dc &&
            GetDIBits(dc, info.hbmColor, 0, (UINT)bitmap.bmHeight, pixels, &bi, DIB_RGB_COLORS) == bitmap.bmHeight) {
            *cx = bitmap.bmWidth;
            *cy = bitmap.bmHeight;
        } else {
            ShFree(pixels);
            pixels = nullptr;
        }
        if (dc)
            DeleteDC(dc);
    }
    if (info.hbmColor)
        DeleteObject(info.hbmColor);
    if (info.hbmMask)
        DeleteObject(info.hbmMask);
    return pixels;
}

/* premultiplies a color pixel by an opacity. */
static DWORD scale_pixel(DWORD p, DWORD opacity)
{
    DWORD out = 0;
    for (int shift = 0; shift < 32; shift += 8)
        out |= (((p >> shift) & 0xFF) * opacity / 255) << shift;
    return out;
}

/* draws the overlay over the base icon, and blends half with the selection color when selected. */
HICON ShDecorateIcon(HICON base, HICON overlay, BOOL selected)
{
    int cx, cy;
    DWORD *pixels = ShIconPixels(base, &cx, &cy);
    if (!pixels)
        return nullptr;
    int ox, oy;
    DWORD *top = overlay ? ShIconPixels(overlay, &ox, &oy) : nullptr;
    if (top && (ox != cx || oy != cy)) {
        ShFree(top);
        top = nullptr;
    }
    if (top)
        for (int i = 0; i < cx * cy; i++) {
            DWORD t = top[i], inverse = 255 - (t >> 24);
            pixels[i] = t + scale_pixel(pixels[i], inverse);
        }
    if (selected) {
        COLORREF c = GetSysColor(COLOR_HIGHLIGHT);
        DWORD red = c & 0xFF, green = (c >> 8) & 0xFF, blue = (c >> 16) & 0xFF;
        for (int i = 0; i < cx * cy; i++) {
            DWORD p = pixels[i], a = p >> 24;
            if (!a)
                continue;
            DWORD r = (((p >> 16) & 0xFF) + red * a / 255) / 2, g = (((p >> 8) & 0xFF) + green * a / 255) / 2,
                  b = ((p & 0xFF) + blue * a / 255) / 2;
            pixels[i] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    HICON icon = ShIconFromPixels(pixels, cx, cy);
    ShFree(top);
    ShFree(pixels);
    return icon;
}

/* ------------------------------------------------------------------ stock icons */

static HICON draw_stock(int siid, int cx, int cy)
{
    const STOCK_ICON *stock = find_stock(siid);
    int art = stock ? stock->Art : ART_DOC;
    DWORD *pixels = ShIconArt(art, cx, cy);
    HICON icon = pixels ? ShIconFromPixels(pixels, cx, cy) : nullptr;
    ShFree(pixels);
    return icon;
}

static HICON load_stock(int siid, int cx, int cy)
{
    if (siid < 0 || (siid >= SIID_MAX_ICONS && siid < SH_STOCK_COMPUTER) || siid > SH_STOCK_SYSTEMDRIVE)
        siid = SIID_DOCNOASSOC;
    const STOCK_ICON *stock = find_stock(siid);
    int resource = stock ? stock->Resource : g_stock[0].Resource;
    HICON icon = nullptr;
    WCHAR path[MAX_PATH];
    if (resource && windows_directory(path, MAX_PATH) && wcslen(path) + 24 < MAX_PATH) {
        wcscat(path, L"\\System32\\imageres.dll");
        BOOL owned;
        WCHAR alternative[MAX_PATH];
        HMODULE module = open_module(path, &owned);
        if (module) {
            icon = (HICON)LoadImageW(module, MAKEINTRESOURCEW(resource), IMAGE_ICON, cx, cy, 0);
            close_module(module, owned);
        }
        if (!icon && resource_file(path, alternative)) {
            module = open_module(alternative, &owned);
            if (module) {
                icon = (HICON)LoadImageW(module, MAKEINTRESOURCEW(resource), IMAGE_ICON, cx, cy, 0);
                close_module(module, owned);
            }
        }
    }
    return icon ? icon : draw_stock(siid, cx, cy);
}

HICON ShLoadStockIcon(int siid, int cx, int cy)
{
    return load_stock(siid, cx, cy);
}

/* the library path and index naming a stock icon, for the locations the shell hands out. */
static void stock_location(int siid, WCHAR *path, int *index)
{
    WCHAR windir[MAX_PATH];
    const STOCK_ICON *stock = find_stock(siid);
    path[0] = 0;
    *index = 0;
    if (!windows_directory(windir, MAX_PATH) || wcslen(windir) + 24 >= MAX_PATH)
        return;
    if (siid <= SIID_MYNETWORK) {
        _snwprintf(path, MAX_PATH, L"%ls\\System32\\shell32.dll", windir);
        *index = siid;
    } else if (stock && stock->Resource) {
        _snwprintf(path, MAX_PATH, L"%ls\\System32\\imageres.dll", windir);
        *index = -stock->Resource;
    }
    path[MAX_PATH - 1] = 0;
}

/* ------------------------------------------------------------------ the icon of a file */

typedef HRESULT(WINAPI *ASSOC_QUERY_STRING)(DWORD, int, LPCWSTR, LPCWSTR, LPWSTR, DWORD *);

static BOOL parse_number(LPCWSTR text, int *value)
{
    while (*text == L' ')
        text++;
    BOOL negative = *text == L'-';
    if (negative)
        text++;
    if (*text < L'0' || *text > L'9')
        return FALSE;
    int v = 0;
    for (; *text >= L'0' && *text <= L'9'; text++)
        v = v * 10 + (*text - L'0');
    while (*text == L' ')
        text++;
    if (*text)
        return FALSE;
    *value = negative ? -v : v;
    return TRUE;
}

/* "path,index" with quotes and environment variables. %1 is the file itself. */
static BOOL parse_location(LPCWSTR text, LPCWSTR self, SH_ICON_SOURCE *out)
{
    WCHAR work[MAX_PATH * 2];
    DWORD n = ExpandEnvironmentStringsW(text, work, RTL_NUMBER_OF(work));
    if (!n || n > RTL_NUMBER_OF(work))
        return FALSE;
    LPWSTR p = work, tail = nullptr;
    while (*p == L' ')
        p++;
    if (*p == L'"') {
        p++;
        LPWSTR close = wcschr(p, L'"');
        if (close) {
            *close = 0;
            tail = close + 1;
            while (*tail == L' ')
                tail++;
            if (*tail == L',')
                tail++;
            else
                tail = nullptr;
        }
    } else {
        LPWSTR comma = wcsrchr(p, L',');
        int ignored;
        if (comma && parse_number(comma + 1, &ignored)) {
            *comma = 0;
            tail = comma + 1;
        }
    }
    int index = 0;
    if (tail && !parse_number(tail, &index))
        return FALSE;
    SIZE_T length = wcslen(p);
    while (length && p[length - 1] == L' ')
        p[--length] = 0;
    if (!length)
        return FALSE;
    out->Stock = -1;
    out->Index = index;
    out->Self = FALSE;
    if (!wcscmp(p, L"%1")) {
        if (wcslen(self) >= MAX_PATH)
            return FALSE;
        wcscpy(out->Path, self);
        out->Index = 0;
        out->Self = TRUE;
        return TRUE;
    }
    if (length >= MAX_PATH)
        return FALSE;
    if (!wcschr(p, L'\\') && !wcschr(p, L':')) {
        WCHAR found[MAX_PATH];
        DWORD found_length = SearchPathW(nullptr, p, nullptr, MAX_PATH, found, nullptr);
        if (found_length && found_length < MAX_PATH) {
            wcscpy(out->Path, found);
            return TRUE;
        }
    }
    wcscpy(out->Path, p);
    return TRUE;
}

/* the extension's DefaultIcon, following the class chain the association code walks. */
static BOOL registered_icon(LPCWSTR extension, LPWSTR text, DWORD chars)
{
    HMODULE shlwapi = LoadLibraryW(L"shlwapi.dll");
    ASSOC_QUERY_STRING query =
        shlwapi ? (ASSOC_QUERY_STRING)(void *)GetProcAddress(shlwapi, "AssocQueryStringW") : nullptr;
    if (!query)
        return FALSE;
    DWORD size = chars;
    return SUCCEEDED(query(0, 15, extension, nullptr, text, &size)) && text[0];
}

/* a registered class turns the plain page into a document with a program. */
static BOOL extension_registered(LPCWSTR extension)
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, extension, 0, KEY_READ, &key))
        return FALSE;
    WCHAR progid[MAX_PATH];
    DWORD type, bytes = sizeof(progid) - sizeof(WCHAR);
    BOOL registered = !RegQueryValueExW(key, nullptr, nullptr, &type, (BYTE *)progid, &bytes) && bytes > sizeof(WCHAR);
    RegCloseKey(key);
    return registered;
}

void ShIconSourceStock(int siid, SH_ICON_SOURCE *source)
{
    memset(source, 0, sizeof(*source));
    source->Stock = siid;
    source->Fallback = siid;
}

/* a desktop.ini icon, which Windows reads for read only or system folders. */
static BOOL folder_ini_icon(LPCWSTR folder, SH_ICON_SOURCE *out)
{
    WCHAR ini[MAX_PATH], text[MAX_PATH * 2];
    if (wcslen(folder) + 13 >= MAX_PATH)
        return FALSE;
    _snwprintf(ini, MAX_PATH, L"%ls%ls", folder, folder[wcslen(folder) - 1] == L'\\' ? L"desktop.ini" : L"\\desktop.ini");
    ini[MAX_PATH - 1] = 0;
    if (GetFileAttributesW(ini) == INVALID_FILE_ATTRIBUTES)
        return FALSE;
    if (GetPrivateProfileStringW(L".ShellClassInfo", L"IconResource", L"", text, RTL_NUMBER_OF(text), ini) &&
        parse_location(text, L"", out))
        return TRUE;
    WCHAR file[MAX_PATH * 2];
    if (!GetPrivateProfileStringW(L".ShellClassInfo", L"IconFile", L"", file, RTL_NUMBER_OF(file), ini) || !file[0])
        return FALSE;
    WCHAR number[16] = L"";
    GetPrivateProfileStringW(L".ShellClassInfo", L"IconIndex", L"0", number, RTL_NUMBER_OF(number), ini);
    int index = 0;
    parse_number(number, &index);
    _snwprintf(text, RTL_NUMBER_OF(text), L"%ls,%d", file, index);
    text[RTL_NUMBER_OF(text) - 1] = 0;
    return parse_location(text, L"", out);
}

/*
 * attributes say whether it is a folder. disk allows asking the file system, which a request by attributes forbids.
 * open picks the open folder. executables and icon files show their own picture.
 */
void ShIconSourceForPath(LPCWSTR path, DWORD attributes, BOOL disk, BOOL open, SH_ICON_SOURCE *source)
{
    ShIconSourceStock(SIID_DOCNOASSOC, source);
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
        int stock = open ? SIID_FOLDEROPEN : SIID_FOLDER;
        if (is_drive_root(path)) {
            UINT type = disk ? GetDriveTypeW(path) : DRIVE_FIXED;
            WCHAR windir[MAX_PATH];
            switch (type) {
            case DRIVE_REMOVABLE:
                stock = (path[0] | 0x20) <= L'b' ? SIID_DRIVE35 : SIID_DRIVEREMOVE;
                break;
            case DRIVE_REMOTE:
                stock = SIID_DRIVENET;
                break;
            case DRIVE_CDROM:
                stock = SIID_DRIVECD;
                break;
            case DRIVE_RAMDISK:
                stock = SIID_DRIVERAM;
                break;
            default:
                stock = windows_directory(windir, MAX_PATH) && (windir[0] | 0x20) == (path[0] | 0x20)
                            ? SH_STOCK_SYSTEMDRIVE
                            : SIID_DRIVEFIXED;
                break;
            }
        } else if (disk && (attributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM)) &&
                   folder_ini_icon(path, source)) {
            source->Fallback = stock;
            return;
        }
        ShIconSourceStock(stock, source);
        return;
    }
    LPCWSTR dot = wcsrchr(base_name(path), L'.');
    if (!dot || !dot[1])
        return;
    WCHAR text[MAX_PATH * 2];
    BOOL program = has_extension(path, L".exe") || has_extension(path, L".scr") || has_extension(path, L".com");
    if (registered_icon(dot, text, RTL_NUMBER_OF(text)) && parse_location(text, path, source)) {
        source->Fallback = program ? SIID_APPLICATION : extension_registered(dot) ? SIID_DOCASSOC : SIID_DOCNOASSOC;
        /* shell32's first icons are the stock icons, told apart from the library's own. */
        if (is_shell32(source->Path) && source->Index >= 0 && source->Index <= SIID_MYNETWORK) {
            int stock = source->Index;
            ShIconSourceStock(stock, source);
        }
        return;
    }
    if (program || has_extension(path, L".ico") || has_extension(path, L".cur")) {
        if (wcslen(path) < MAX_PATH) {
            source->Stock = -1;
            source->Self = TRUE;
            wcscpy(source->Path, path);
            source->Fallback = program ? SIID_APPLICATION : SIID_DOCNOASSOC;
        }
        return;
    }
    source->Fallback = extension_registered(dot) ? SIID_DOCASSOC : SIID_DOCNOASSOC;
}

/* tries the file, then its fallback stock icon, then a drawing. */
HICON ShLoadIconSource(const SH_ICON_SOURCE *source, int cx, int cy)
{
    HICON icon = nullptr;
    if (source->Stock < 0 && source->Path[0])
        icon = ShIconLoad(source->Path, source->Index, cx, cy);
    if (icon)
        return icon;
    return load_stock(source->Stock >= 0 ? source->Stock : source->Fallback, cx, cy);
}

/* a key that tells sources apart, naming a stock icon by its number. */
void ShIconSourceKey(const SH_ICON_SOURCE *source, WCHAR *key, SIZE_T chars)
{
    if (source->Stock >= 0 || !source->Path[0]) {
        _snwprintf(key, chars, L"*%d", source->Stock >= 0 ? source->Stock : source->Fallback);
    } else {
        _snwprintf(key, chars, L"%ls,%d", source->Path, source->Index);
        for (WCHAR *p = key; *p; p++)
            *p = towlower(*p);
    }
    key[chars - 1] = 0;
}

/* as SHGFI_ICONLOCATION states them. returns whether it is a file at all. */
BOOL ShIconSourceLocation(const SH_ICON_SOURCE *source, WCHAR *path, int *index)
{
    if (source->Stock < 0 && source->Path[0]) {
        wcscpy(path, source->Path);
        *index = source->Index;
        return TRUE;
    }
    stock_location(source->Stock >= 0 ? source->Stock : source->Fallback, path, index);
    return path[0] != 0;
}

/* ------------------------------------------------------------------ stock icon information */

SHSTDAPI HRESULT WINAPI SHGetStockIconInfo(SHSTOCKICONID id, UINT flags, SHSTOCKICONINFO *info)
{
    if (!info || info->cbSize != sizeof(*info))
        return E_INVALIDARG;
    info->hIcon = nullptr;
    info->iSysImageIndex = -1;
    info->iIcon = -1;
    info->szPath[0] = 0;
    if ((flags & ~(SHGSI_ICON | SHGSI_SYSICONINDEX | SHGSI_LINKOVERLAY | SHGSI_SELECTED | SHGSI_SMALLICON |
                   SHGSI_SHELLICONSIZE)) ||
        (UINT)id >= SIID_MAX_ICONS)
        return E_INVALIDARG;
    stock_location((int)id, info->szPath, &info->iIcon);
    if (!(flags & (SHGSI_ICON | SHGSI_SYSICONINDEX)))
        return S_OK;
    SH_ICON_SOURCE source;
    ShIconSourceStock((int)id, &source);
    if (flags & SHGSI_SYSICONINDEX) {
        info->iSysImageIndex = ShSystemIconIndex(&source, flags & SHGSI_SMALLICON);
        if (info->iSysImageIndex < 0)
            return E_FAIL;
    }
    if (flags & SHGSI_ICON) {
        info->hIcon = ShIconForRequest(&source, flags);
        if (!info->hIcon)
            return E_FAIL;
    }
    return S_OK;
}

/*
 * for SHGetFileInfo and SHGetStockIconInfo: the system's large or small size or the shell's, with the link arrow and
 * the selection blend when asked.
 */
HICON ShIconForRequest(const SH_ICON_SOURCE *source, UINT flags)
{
    BOOL small = (flags & SHGFI_SMALLICON) != 0, shell = (flags & SHGFI_SHELLICONSIZE) != 0;
    int cx = shell ? (small ? 16 : 32) : GetSystemMetrics(small ? SM_CXSMICON : SM_CXICON);
    int cy = shell ? cx : GetSystemMetrics(small ? SM_CYSMICON : SM_CYICON);
    HICON icon = ShLoadIconSource(source, cx, cy);
    if (!icon || !(flags & (SHGFI_LINKOVERLAY | SHGFI_SELECTED)))
        return icon;
    HICON overlay = (flags & SHGFI_LINKOVERLAY) ? load_stock(SIID_LINK, cx, cy) : nullptr;
    HICON decorated = ShDecorateIcon(icon, overlay, (flags & SHGFI_SELECTED) != 0);
    if (overlay)
        DestroyIcon(overlay);
    if (decorated) {
        DestroyIcon(icon);
        return decorated;
    }
    return icon;
}

/* ------------------------------------------------------------------ IExtractIcon over a file name */

typedef struct {
    IExtractIconW Wide;
    IExtractIconA Ansi;
    LONG Refs;
    SH_ICON_SOURCE Source;
    SH_ICON_SOURCE OpenSource;
} FILE_EXTRACT;

static FILE_EXTRACT *extract_from_wide(IExtractIconW *This)
{
    return (FILE_EXTRACT *)This;
}

static FILE_EXTRACT *extract_from_ansi(IExtractIconA *This)
{
    return (FILE_EXTRACT *)((BYTE *)This - offsetof(FILE_EXTRACT, Ansi));
}

static HRESULT extract_query(FILE_EXTRACT *e, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IExtractIconW)) {
        *object = &e->Wide;
    } else if (IsEqualIID(riid, &IID_IExtractIconA)) {
        *object = &e->Ansi;
    } else {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&e->Refs);
    return S_OK;
}

static ULONG extract_release(FILE_EXTRACT *e)
{
    LONG refs = InterlockedDecrement(&e->Refs);
    if (!refs)
        ShFree(e);
    return (ULONG)refs;
}

/* a file uses its class's icon, a stock icon is named "*". */
static HRESULT extract_location(FILE_EXTRACT *e, UINT flags, WCHAR *file, UINT chars, int *index, UINT *out_flags)
{
    const SH_ICON_SOURCE *source = (flags & GIL_OPENICON) ? &e->OpenSource : &e->Source;
    WCHAR path[MAX_PATH];
    int position;
    UINT result = source->Self ? GIL_PERINSTANCE : GIL_PERCLASS;
    if (!ShIconSourceLocation(source, path, &position)) {
        if (chars > 1) {
            file[0] = L'*';
            file[1] = 0;
        }
        *index = ShSystemIconIndex(source, 0);
        *out_flags = result | GIL_NOTFILENAME;
        return S_OK;
    }
    if (wcslen(path) >= chars)
        return E_FAIL;
    wcscpy(file, path);
    *index = position;
    *out_flags = result;
    return S_OK;
}

static HRESULT extract_icons(PCWSTR file, UINT index, HICON *large, HICON *small, UINT size)
{
    if (large)
        *large = nullptr;
    if (small)
        *small = nullptr;
    if (!file || file[0] == L'*')
        return S_FALSE;
    int lx = (int)LOWORD(size), sx = (int)HIWORD(size);
    if (!lx)
        lx = GetSystemMetrics(SM_CXICON);
    if (!sx)
        sx = GetSystemMetrics(SM_CXSMICON);
    HICON big = large ? ShIconLoad(file, (int)index, lx, lx) : nullptr;
    HICON little = small ? ShIconLoad(file, (int)index, sx, sx) : nullptr;
    if (large)
        *large = big;
    if (small)
        *small = little;
    return (!large || big) && (!small || little) ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE ew_query(IExtractIconW *This, REFIID riid, void **object)
{
    return extract_query(extract_from_wide(This), riid, object);
}

static ULONG STDMETHODCALLTYPE ew_add_ref(IExtractIconW *This)
{
    return (ULONG)InterlockedIncrement(&extract_from_wide(This)->Refs);
}

static ULONG STDMETHODCALLTYPE ew_release(IExtractIconW *This)
{
    return extract_release(extract_from_wide(This));
}

static HRESULT STDMETHODCALLTYPE ew_location(IExtractIconW *This, UINT flags, PWSTR file, UINT chars, int *index,
                                             UINT *out_flags)
{
    if (!file || !index || !out_flags)
        return E_INVALIDARG;
    return extract_location(extract_from_wide(This), flags, file, chars, index, out_flags);
}

static HRESULT STDMETHODCALLTYPE ew_extract(IExtractIconW *This, PCWSTR file, UINT index, HICON *large, HICON *small,
                                            UINT size)
{
    return extract_icons(file, index, large, small, size);
}

static const IExtractIconWVtbl g_extract_wide_vtbl = {ew_query, ew_add_ref, ew_release, ew_location, ew_extract};

static HRESULT STDMETHODCALLTYPE ea_query(IExtractIconA *This, REFIID riid, void **object)
{
    return extract_query(extract_from_ansi(This), riid, object);
}

static ULONG STDMETHODCALLTYPE ea_add_ref(IExtractIconA *This)
{
    return (ULONG)InterlockedIncrement(&extract_from_ansi(This)->Refs);
}

static ULONG STDMETHODCALLTYPE ea_release(IExtractIconA *This)
{
    return extract_release(extract_from_ansi(This));
}

static HRESULT STDMETHODCALLTYPE ea_location(IExtractIconA *This, UINT flags, PSTR file, UINT chars, int *index,
                                             UINT *out_flags)
{
    if (!file || !index || !out_flags)
        return E_INVALIDARG;
    WCHAR wide[MAX_PATH];
    HRESULT hr = extract_location(extract_from_ansi(This), flags, wide, MAX_PATH, index, out_flags);
    if (FAILED(hr))
        return hr;
    if (!chars || !WideCharToMultiByte(CP_ACP, 0, wide, -1, file, (int)chars, nullptr, nullptr))
        return E_FAIL;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ea_extract(IExtractIconA *This, PCSTR file, UINT index, HICON *large, HICON *small,
                                            UINT size)
{
    LPWSTR wide = ShWiden(file);
    HRESULT hr = extract_icons(wide, index, large, small, size);
    ShFree(wide);
    return hr;
}

static const IExtractIconAVtbl g_extract_ansi_vtbl = {ea_query, ea_add_ref, ea_release, ea_location, ea_extract};

/* the file need not exist. */
SHSTDAPI HRESULT WINAPI SHCreateFileExtractIconW(LPCWSTR file, DWORD attributes, REFIID riid, void **object)
{
    if (!object)
        return E_INVALIDARG;
    *object = nullptr;
    if (!file || !riid)
        return E_INVALIDARG;
    FILE_EXTRACT *e = ShAllocZero(sizeof(*e));
    if (!e)
        return E_OUTOFMEMORY;
    e->Wide.lpVtbl = &g_extract_wide_vtbl;
    e->Ansi.lpVtbl = &g_extract_ansi_vtbl;
    e->Refs = 1;
    ShIconSourceForPath(file, attributes, FALSE, FALSE, &e->Source);
    ShIconSourceForPath(file, attributes, FALSE, TRUE, &e->OpenSource);
    HRESULT hr = extract_query(e, riid, object);
    extract_release(e);
    return hr;
}

SHSTDAPI HRESULT WINAPI SHCreateFileExtractIconA(LPCSTR file, DWORD attributes, REFIID riid, void **object)
{
    LPWSTR wide = ShWiden(file);
    HRESULT hr = file && !wide ? E_OUTOFMEMORY : SHCreateFileExtractIconW(wide, attributes, riid, object);
    ShFree(wide);
    return hr;
}

/* the two sizes are packed in the halves of size, as shell extractors take them. */
SHSTDAPI HRESULT WINAPI SHDefExtractIconW(LPCWSTR file, int index, UINT flags, HICON *large, HICON *small, UINT size)
{
    return extract_icons(file, (UINT)index, large, small, size);
}

SHSTDAPI HRESULT WINAPI SHDefExtractIconA(LPCSTR file, int index, UINT flags, HICON *large, HICON *small, UINT size)
{
    LPWSTR wide = ShWiden(file);
    HRESULT hr = file && !wide ? E_OUTOFMEMORY : extract_icons(wide, (UINT)index, large, small, size);
    ShFree(wide);
    return hr;
}
