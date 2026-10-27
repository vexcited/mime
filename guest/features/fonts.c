/*
 * The fonts of Microsoft as a feature: the files of the package go into the fonts folder of the drive, are registered
 * under the Fonts key of the registry and are made known to the window manager at once, then every window is told that
 * the fonts changed.
 *
 * Only the real files are installed. A file is accepted when its size and its hashes are those the catalog names for
 * its name, and when the families in its own name table are those the catalog says it has, for every face of it. A
 * file that fails any of this is not written, so no font is ever installed, or registered, under the name of another.
 */
#include "featurep.h"
#include "../lib/cabinet/cabinet.h"

#define FONTS_KEY L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts"
#define MAX_PACKAGE (512u << 20)
#define WM_FONTCHANGE_ 0x001D

WINBASEAPI int WINAPI AddFontResourceW(PCWSTR file);
WINBASEAPI BOOL WINAPI RemoveFontResourceW(PCWSTR file);

static void fonts_folder(RUN *run, WCHAR *path, SIZE_T chars)
{
    _snwprintf(path, chars, L"%lsWindows\\Fonts", run->Root);
    path[chars - 1] = 0;
}

static void font_path(RUN *run, const FEATURE_FONT *font, WCHAR *path, SIZE_T chars)
{
    WCHAR folder[MAX_PATH];
    fonts_folder(run, folder, RTL_NUMBER_OF(folder));
    _snwprintf(path, chars, L"%ls\\%hs", folder, font->File);
    path[chars - 1] = 0;
}

/* ------------------------------------------------------------------ the name table */

static unsigned be16(const BYTE *p)
{
    return p[0] << 8 | p[1];
}

static DWORD be32(const BYTE *p)
{
    return (DWORD)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

/* The family of one face, name id 1 of a Windows record, as ASCII. FALSE when there is none or it is not ASCII. */
static BOOL face_family(const BYTE *data, SIZE_T size, SIZE_T directory, char *family, SIZE_T chars)
{
    if (directory + 12 > size || be32(data + directory) != 0x00010000)
        return FALSE;
    unsigned tables = be16(data + directory + 4);
    if (directory + 12 + tables * 16 > size)
        return FALSE;
    for (unsigned i = 0; i < tables; i++) {
        const BYTE *t = data + directory + 12 + i * 16;
        if (be32(t) != 0x6E616D65)
            continue;
        DWORD offset = be32(t + 8), length = be32(t + 12);
        if (offset > size || length > size - offset || length < 6)
            return FALSE;
        const BYTE *name = data + offset;
        unsigned count = be16(name + 2), strings = be16(name + 4);
        const BYTE *best = nullptr;
        for (unsigned k = 0; k < count && 6 + (k + 1) * 12 <= length; k++) {
            const BYTE *r = name + 6 + k * 12;
            if (be16(r) != 3 || be16(r + 6) != 1 || (be16(r + 2) > 1 && be16(r + 2) != 10))
                continue;
            if (!best || be16(r + 4) == 0x0409)
                best = r;
        }
        if (!best)
            return FALSE;
        unsigned n = be16(best + 8), at = be16(best + 10);
        if ((SIZE_T)strings + at + n > length || n / 2 + 1 > chars || !n)
            return FALSE;
        for (SIZE_T c = 0; c < n / 2; c++) {
            unsigned ch = be16(name + strings + at + 2 * c);
            if (ch > 0x7E || ch < 0x20)
                return FALSE;
            family[c] = (char)ch;
        }
        family[n / 2] = 0;
        return TRUE;
    }
    return FALSE;
}

static BOOL family_listed(const FEATURE_FONT *font, const char *family)
{
    for (const char *const *name = font->Families; name && *name; name++)
        if (!_stricmp(*name, family))
            return TRUE;
    return FALSE;
}

/* Every face of the file has a family the catalog gives the file. */
static BOOL name_table_matches(const FEATURE_FONT *font, const BYTE *data, SIZE_T size)
{
    char family[96];
    if (size >= 12 && be32(data) == 0x74746366) {
        DWORD count = be32(data + 8);
        if (!count || count > 32 || 12 + count * 4 > size)
            return FALSE;
        for (DWORD i = 0; i < count; i++) {
            family[0] = 0;
            if (!face_family(data, size, be32(data + 12 + i * 4), family, sizeof(family)) || !family_listed(font, family))
                return FALSE;
        }
        return TRUE;
    }
    family[0] = 0;
    return face_family(data, size, 0, family, sizeof(family)) && family_listed(font, family);
}

/* ------------------------------------------------------------------ one file */

typedef struct INSTALL {
    RUN *Run;
    const FEATURE_DEF *Feature;
    DWORD Installed;
    DWORD Error;
    BOOL Seen[1024];
} INSTALL;

static void register_font(RUN *run, const FEATURE_FONT *font)
{
    WCHAR name[260], file[MAX_PATH];
    _snwprintf(name, RTL_NUMBER_OF(name), L"%hs", font->Registry);
    _snwprintf(file, RTL_NUMBER_OF(file), L"%hs", font->File);
    name[RTL_NUMBER_OF(name) - 1] = file[RTL_NUMBER_OF(file) - 1] = 0;
    RegSetKeyValueW(HKEY_LOCAL_MACHINE, FONTS_KEY, name, REG_SZ, file, (DWORD)((wcslen(file) + 1) * sizeof(WCHAR)));
}

/* Checks one file and puts it on the drive. */
static DWORD put_font(INSTALL *in, const FEATURE_FONT *font, const BYTE *data, SIZE_T size)
{
    RUN *run = in->Run;
    if (size != font->Size || !HashMatches(data, size, font->Sha256, font->Sha1)) {
        _snwprintf(run->Detail, RTL_NUMBER_OF(run->Detail), L"%hs is not the file that Microsoft's font of this name is.", font->File);
        run->Detail[RTL_NUMBER_OF(run->Detail) - 1] = 0;
        return (DWORD)0x80096010; /* TRUST_E_BAD_DIGEST */
    }
    if (!name_table_matches(font, data, size)) {
        _snwprintf(run->Detail, RTL_NUMBER_OF(run->Detail), L"%hs does not have the names that the font has.", font->File);
        run->Detail[RTL_NUMBER_OF(run->Detail) - 1] = 0;
        return ERROR_INVALID_DATA;
    }
    WCHAR path[MAX_PATH], part[MAX_PATH], folder[MAX_PATH];
    fonts_folder(run, folder, RTL_NUMBER_OF(folder));
    CreateDirectoryW(folder, nullptr);
    font_path(run, font, path, RTL_NUMBER_OF(path));
    _snwprintf(part, RTL_NUMBER_OF(part), L"%ls.part", path);
    part[RTL_NUMBER_OF(part) - 1] = 0;
    HANDLE file = CreateFileW(part, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return GetLastError();
    DWORD written = 0;
    BOOL ok = WriteFile(file, data, (DWORD)size, &written, nullptr) && written == size;
    DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (ok && !MoveFileExW(part, path, MOVEFILE_REPLACE_EXISTING)) {
        ok = FALSE;
        error = GetLastError();
    }
    if (!ok) {
        DeleteFileW(part);
        return error ? error : ERROR_DISK_FULL;
    }
    register_font(run, font);
    AddFontResourceW(path);
    in->Installed++;
    return ERROR_SUCCESS;
}

/* A file of the package, whose name must be one of the feature's. */
static BOOL entry_found(INSTALL *in, const char *name, const BYTE *data, SIZE_T size)
{
    const char *base = strrchr(name, '\\');
    const char *slash = strrchr(name, '/');
    if (slash && (!base || slash > base))
        base = slash;
    base = base ? base + 1 : name;
    const FEATURE_FONT *font = FeatureFindFont(in->Feature, base);
    if (!font)
        return TRUE;
    SIZE_T index = (SIZE_T)(font - in->Feature->Fonts);
    if (index < RTL_NUMBER_OF(in->Seen)) {
        if (in->Seen[index])
            return TRUE;
        in->Seen[index] = TRUE;
    }
    in->Error = put_font(in, font, data, size);
    return in->Error == ERROR_SUCCESS;
}

static BOOL pack_entry(void *context, const char *name, const BYTE *data, SIZE_T size)
{
    return entry_found(context, name, data, size);
}

/* ------------------------------------------------------------------ the cabinet */

typedef struct CAB_FILE {
    INSTALL *In;
    BYTE *Data;
    SIZE_T Size, Fill;
    char Name[260];
} CAB_FILE;

static void *cab_begin(void *context, const MIME_CAB_FILE *file)
{
    INSTALL *in = context;
    const char *base = strrchr(file->Name, '\\');
    const char *slash = strrchr(file->Name, '/');
    if (slash && (!base || slash > base))
        base = slash;
    base = base ? base + 1 : file->Name;
    const FEATURE_FONT *font = FeatureFindFont(in->Feature, base);
    if (!font || file->Size != font->Size)
        return nullptr;
    CAB_FILE *c = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*c));
    if (!c)
        return nullptr;
    c->In = in;
    c->Size = file->Size;
    c->Data = HeapAlloc(GetProcessHeap(), 0, file->Size ? file->Size : 1);
    if (!c->Data) {
        HeapFree(GetProcessHeap(), 0, c);
        return nullptr;
    }
    lstrcpynA(c->Name, base, sizeof(c->Name));
    return c;
}

static int cab_write(void *context, void *file_context, const uint8_t *data, size_t length)
{
    CAB_FILE *c = file_context;
    if (length > c->Size - c->Fill)
        return 1;
    memcpy(c->Data + c->Fill, data, length);
    c->Fill += length;
    return 0;
}

static int cab_end(void *context, void *file_context, const MIME_CAB_FILE *file)
{
    CAB_FILE *c = file_context;
    int result = 0;
    if (c->Fill != c->Size || !entry_found(c->In, c->Name, c->Data, c->Size))
        result = 1;
    HeapFree(GetProcessHeap(), 0, c->Data);
    HeapFree(GetProcessHeap(), 0, c);
    return result;
}

static void *cab_allocate(size_t size)
{
    return HeapAlloc(GetProcessHeap(), 0, size);
}

static void cab_release(void *memory)
{
    HeapFree(GetProcessHeap(), 0, memory);
}

/* ------------------------------------------------------------------ the feature */

static DWORD read_package(PCWSTR path, BYTE **data, SIZE_T *size)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return GetLastError();
    LARGE_INTEGER length;
    DWORD error = ERROR_SUCCESS;
    if (!GetFileSizeEx(file, &length) || length.QuadPart > MAX_PACKAGE) {
        error = ERROR_FILE_TOO_LARGE;
    } else {
        *size = (SIZE_T)length.QuadPart;
        *data = HeapAlloc(GetProcessHeap(), 0, *size ? *size : 1);
        DWORD got = 0;
        SIZE_T done = 0;
        if (!*data)
            error = ERROR_OUTOFMEMORY;
        while (!error && done < *size) {
            DWORD want = (DWORD)(*size - done < (64u << 20) ? *size - done : (64u << 20));
            if (!ReadFile(file, *data + done, want, &got, nullptr) || !got)
                error = GetLastError() ? GetLastError() : ERROR_HANDLE_EOF;
            done += got;
        }
        if (error && *data) {
            HeapFree(GetProcessHeap(), 0, *data);
            *data = nullptr;
        }
    }
    CloseHandle(file);
    return error;
}

static void broadcast(void)
{
    DWORD_PTR result;
    SendMessageTimeoutW(HWND_BROADCAST, WM_FONTCHANGE_, 0, 0, SMTO_ABORTIFHUNG, 2000, &result);
}

uint32_t FontsInstall(RUN *run, const FEATURE_DEF *feature, PCWSTR package)
{
    BYTE *data = nullptr;
    SIZE_T size = 0;
    DWORD error = read_package(package, &data, &size);
    if (error)
        return error;
    INSTALL *in = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*in));
    if (!in) {
        HeapFree(GetProcessHeap(), 0, data);
        return ERROR_OUTOFMEMORY;
    }
    in->Run = run;
    in->Feature = feature;
    if (size >= 4 && !memcmp(data, "MSCF", 4)) {
        MIME_CAB *cab;
        int status = MimeCabOpen(&cab, data, size, cab_allocate, cab_release);
        if (status == MIME_CAB_OK) {
            MIME_CAB_FILE_SINK sink = {cab_begin, cab_write, cab_end};
            status = MimeCabExtract(cab, &sink, in);
            MimeCabClose(cab);
        }
        error = in->Error ? in->Error : status == MIME_CAB_OK ? ERROR_SUCCESS : ERROR_INVALID_DATA;
    } else if (!PackWalk(data, size, pack_entry, in)) {
        error = in->Error ? in->Error : ERROR_INVALID_DATA;
    }
    /* The package must have held all the fonts of the feature. */
    if (!error && in->Installed != feature->FontCount) {
        error = ERROR_FILE_NOT_FOUND;
        _snwprintf(run->Detail, RTL_NUMBER_OF(run->Detail), L"The package has %lu of the %lu fonts of the feature.", in->Installed,
                   (DWORD)feature->FontCount);
        run->Detail[RTL_NUMBER_OF(run->Detail) - 1] = 0;
    }
    if (in->Installed)
        broadcast();
    HeapFree(GetProcessHeap(), 0, in);
    HeapFree(GetProcessHeap(), 0, data);
    return error;
}

BOOL FontsPresent(RUN *run, const FEATURE_DEF *feature)
{
    for (SIZE_T i = 0; i < feature->FontCount; i++) {
        WCHAR path[MAX_PATH];
        font_path(run, &feature->Fonts[i], path, RTL_NUMBER_OF(path));
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
            return FALSE;
    }
    return feature->FontCount != 0;
}

uint32_t FontsRemove(RUN *run, const FEATURE_DEF *feature)
{
    DWORD error = ERROR_SUCCESS;
    for (SIZE_T i = 0; i < feature->FontCount; i++) {
        const FEATURE_FONT *font = &feature->Fonts[i];
        WCHAR path[MAX_PATH], name[260];
        font_path(run, font, path, RTL_NUMBER_OF(path));
        RemoveFontResourceW(path);
        if (!DeleteFileW(path) && GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND)
            error = GetLastError();
        _snwprintf(name, RTL_NUMBER_OF(name), L"%hs", font->Registry);
        name[RTL_NUMBER_OF(name) - 1] = 0;
        HKEY key;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, FONTS_KEY, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
            RegDeleteValueW(key, name);
            RegCloseKey(key);
        }
    }
    broadcast();
    return error;
}
