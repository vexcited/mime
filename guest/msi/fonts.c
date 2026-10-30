/*
 * fonts. a font of a component being installed is listed under the Fonts key with its title and loaded at once, and a
 * font of a component being removed is unloaded and unlisted. a title that the package leaves out is the full name
 * stored in the font file, followed by its kind.
 */
#include "msi_private.h"

#define FONTS_KEY L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts"
#define MAX_FONT_FILE (256u << 20)

static unsigned be16(const BYTE *p)
{
    return p[0] << 8 | p[1];
}

static unsigned be32(const BYTE *p)
{
    return (unsigned)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

/* the full name of the face at an offset, from its name table. */
static BOOL face_name(const BYTE *data, SIZE_T size, SIZE_T offset, WCHAR *name, SIZE_T chars)
{
    if (offset > size || size - offset < 12)
        return FALSE;
    unsigned tables = be16(data + offset + 4);
    if (size - offset - 12 < (SIZE_T)tables * 16)
        return FALSE;
    SIZE_T table = 0, length = 0;
    for (unsigned i = 0; i < tables; i++) {
        const BYTE *entry = data + offset + 12 + (SIZE_T)i * 16;
        if (!memcmp(entry, "name", 4)) {
            table = be32(entry + 8);
            length = be32(entry + 12);
        }
    }
    if (!table || table > size || size - table < length || length < 6)
        return FALSE;
    const BYTE *names = data + table;
    unsigned count = be16(names + 2);
    SIZE_T strings = be16(names + 4);
    if (6 + (SIZE_T)count * 12 > length)
        return FALSE;
    /* the English record of the Windows platform, then any of it, then the Macintosh one. */
    for (unsigned pass = 0; pass < 3; pass++)
        for (unsigned i = 0; i < count; i++) {
            const BYTE *record = names + 6 + (SIZE_T)i * 12;
            unsigned platform = be16(record), language = be16(record + 4), id = be16(record + 6);
            SIZE_T bytes = be16(record + 8), start = strings + be16(record + 10);
            if (id != 4 || start > length || length - start < bytes)
                continue;
            if (pass == 0 ? platform != 3 || language != 0x409 : pass == 1 ? platform != 3 : platform != 1)
                continue;
            SIZE_T units = platform == 3 ? bytes / 2 : bytes;
            if (!units || units >= chars)
                continue;
            for (SIZE_T j = 0; j < units; j++)
                name[j] = platform == 3 ? (WCHAR)be16(names + start + j * 2) : names[start + j];
            name[units] = 0;
            return TRUE;
        }
    return FALSE;
}

static PWSTR font_title(PCWSTR path)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        DbgPrint("msi: font %ws does not open, error %u\n", path, GetLastError());
        return nullptr;
    }
    LARGE_INTEGER length;
    BYTE *data = nullptr;
    DWORD read = 0;
    if (GetFileSizeEx(file, &length) && length.QuadPart > 12 && length.QuadPart <= MAX_FONT_FILE)
        data = MimeMsiAllocate((SIZE_T)length.QuadPart);
    if (data && (!ReadFile(file, data, (DWORD)length.QuadPart, &read, nullptr) || read != length.QuadPart)) {
        MimeMsiFree(data);
        data = nullptr;
    }
    CloseHandle(file);
    if (!data)
        return nullptr;
    SIZE_T size = (SIZE_T)length.QuadPart;
    BOOL collection = size >= 12 && !memcmp(data, "ttcf", 4);
    unsigned faces = collection ? be32(data + 8) : 1;
    WCHAR title[1024] = L"";
    BOOL found = faces && faces <= 64 && (!collection || size >= 12 + (SIZE_T)faces * 4);
    for (unsigned face = 0; face < faces && found; face++) {
        SIZE_T offset = collection ? be32(data + 12 + (SIZE_T)face * 4) : 0;
        WCHAR name[256];
        found = face_name(data, size, offset, name, RTL_NUMBER_OF(name)) &&
                wcslen(title) + wcslen(name) + 32 < RTL_NUMBER_OF(title);
        if (found) {
            if (*title)
                wcscat(title, L" & ");
            wcscat(title, name);
        }
    }
    MimeMsiFree(data);
    if (!found) {
        DbgPrint("msi: font %ws has no name, %u faces\n", path, faces);
        return nullptr;
    }
    wcscat(title, L" (TrueType)");
    return MimeMsiDuplicate(title);
}

static DWORD fonts_view(void)
{
    return KEY_WOW64_64KEY;
}

static BOOL in_fonts_folder(PCWSTR path)
{
    WCHAR folder[MAX_PATH];
    UINT length = GetWindowsDirectoryW(folder, RTL_NUMBER_OF(folder));
    if (!length || length + 8 >= RTL_NUMBER_OF(folder))
        return FALSE;
    wcscpy(folder + length, L"\\Fonts\\");
    length = (UINT)wcslen(folder);
    return !_wcsnicmp(path, folder, length) && !wcschr(path + length, L'\\');
}

/* the data of a value under the Fonts key, none when it is not there. */
static PWSTR listed_font(MIME_MSI_PACKAGE *package, PCWSTR title)
{
    HKEY key;
    PWSTR text = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, FONTS_KEY, 0, KEY_QUERY_VALUE | package->RegistryView, &key))
        return nullptr;
    MimeMsiRegistryText(key, title, &text);
    RegCloseKey(key);
    return text;
}

static UINT font_row(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *fonts, ULONG row, MIME_MSI_TABLE *files, BOOL remove)
{
    MIME_MSI_DATABASE *database = package->Database;
    ULONG file_column = MimeMsiColumn(fonts, L"File_", TRUE), title_column = MimeMsiColumn(fonts, L"FontTitle", TRUE);
    ULONG key_column = MimeMsiColumn(files, L"File", TRUE), component_column = MimeMsiColumn(files, L"Component_", TRUE);
    if (file_column == UINT32_MAX || title_column == UINT32_MAX || key_column == UINT32_MAX ||
        component_column == UINT32_MAX)
        return ERROR_INSTALL_PACKAGE_INVALID;
    PCWSTR file = MimeMsiText(database, fonts, row, file_column), title = MimeMsiText(database, fonts, row, title_column);
    PCWSTR component = nullptr;
    for (ULONG i = 0; file && i < files->Rows && !component; i++) {
        PCWSTR key = MimeMsiText(database, files, i, key_column);
        if (key && !wcscmp(key, file))
            component = MimeMsiText(database, files, i, component_column);
    }
    if (!component)
        return ERROR_INSTALL_PACKAGE_INVALID;
    INSTALLSTATE installed, action;
    UINT error = MimeMsiComponentState(package, component, &installed, &action);
    if (error || action != (remove ? INSTALLSTATE_ABSENT : INSTALLSTATE_LOCAL))
        return error;
    PWSTR path = nullptr, name = nullptr;
    error = MimeMsiFileTargetPath(package, file, &path);
    if (!error) {
        name = title && *title ? MimeMsiDuplicate(title) : font_title(path);
        if (!name)
            DbgPrint("msi: no title for the font %ws\n", path);
    }
    if (!error && name) {
        DWORD previous = package->RegistryView;
        package->RegistryView = fonts_view();
        PWSTR old = listed_font(package, name);
        if (old && (remove || _wcsicmp(old, in_fonts_folder(path) ? wcsrchr(path, L'\\') + 1 : path)))
            RemoveFontResourceW(old);
        if (remove)
            error = old ? MimeMsiDeleteRegistryValue(package, HKEY_LOCAL_MACHINE, FONTS_KEY, name) : ERROR_SUCCESS;
        else {
            /* a font in the fonts folder is listed by its file name, any other by its path. */
            PCWSTR data = in_fonts_folder(path) ? wcsrchr(path, L'\\') + 1 : path;
            error = MimeMsiWriteRegistry(package, HKEY_LOCAL_MACHINE, FONTS_KEY, name, REG_SZ, data,
                                         (DWORD)((wcslen(data) + 1) * sizeof(WCHAR)));
            if (!error)
                AddFontResourceExW(data, 0, nullptr);
        }
        package->RegistryView = previous;
        MimeMsiFree(old);
    }
    MimeMsiFree(name);
    MimeMsiFree(path);
    return error;
}

static UINT font_rows(MIME_MSI_PACKAGE *package, BOOL remove)
{
    MIME_MSI_TABLE *fonts, *files;
    UINT error = MimeMsiLoadTable(package->Database, L"Font", &fonts);
    if (error == ERROR_BAD_QUERY_SYNTAX)
        return ERROR_SUCCESS;
    if (!error && fonts->Rows)
        error = MimeMsiRequireTransaction(package);
    if (!error)
        error = MimeMsiLoadTable(package->Database, L"File", &files);
    /* a title is read from the font file, and a font is loaded from it, once it is there. */
    if (!error && !remove && fonts->Rows)
        error = MimeMsiInstallQueuedFiles(package);
    for (ULONG row = 0; !error && row < fonts->Rows; row++)
        error = font_row(package, fonts, row, files, remove);
    return error;
}

UINT MimeMsiFontsAction(MIME_MSI_PACKAGE *package, PCWSTR action)
{
    BOOL install = !wcscmp(action, L"RegisterFonts");
    if (!install && wcscmp(action, L"UnregisterFonts"))
        return UINT32_MAX;
    return font_rows(package, !install);
}
