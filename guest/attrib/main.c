/*
 * attrib.exe: shows or changes the attributes of files.
 *
 *   attrib [+R | -R] [+A | -A] [+S | -S] [+H | -H] [+O | -O] [+I | -I] [+X | -X] [+P | -P] [+U | -U] [+V | -V]
 *          [drive:][path][filename] [/S [/D]] [/L]
 *
 * with no attribute given the files are listed with their attributes before their full names, otherwise the attributes
 * are changed. a hidden file is left alone unless the command names H, and a system file unless it names S.
 * the exit code is 0, or 1 when a file was not found or could not be changed.
 */
#include "tool.h"

#define FILE_ATTRIBUTE_OFFLINE_ 0x00001000
#define FILE_ATTRIBUTE_NOT_CONTENT_INDEXED_ 0x00002000
#define FILE_ATTRIBUTE_INTEGRITY_STREAM_ 0x00008000
#define FILE_ATTRIBUTE_NO_SCRUB_DATA_ 0x00020000
#define FILE_ATTRIBUTE_PINNED_ 0x00080000
#define FILE_ATTRIBUTE_UNPINNED_ 0x00100000
#define FILE_ATTRIBUTE_STRICTLY_SEQUENTIAL_ 0x20000000

typedef struct STATE {
    DWORD Set;
    DWORD Clear;
    BOOL Recurse;
    BOOL Directories;
    BOOL Failed;
    BOOL Display;
    BOOL MentionsHidden;
    BOOL MentionsSystem;
} STATE;

static const WCHAR kUsage[] =
    L"Displays or changes file attributes.\n\n"
    L"ATTRIB [+R | -R] [+A | -A] [+S | -S] [+H | -H] [+O | -O] [+I | -I] [+X | -X] [+P | -P] [+U | -U]\n"
    L"       [drive:][path][filename] [/S [/D]] [/L]\n\n"
    L"  +   Sets an attribute.\n"
    L"  -   Clears an attribute.\n"
    L"  R   Read-only file attribute.\n"
    L"  A   Archive file attribute.\n"
    L"  S   System file attribute.\n"
    L"  H   Hidden file attribute.\n"
    L"  O   Offline attribute.\n"
    L"  I   Not content indexed file attribute.\n"
    L"  X   No scrub file attribute.\n"
    L"  V   Integrity attribute.\n"
    L"  P   Pinned attribute.\n"
    L"  U   Unpinned attribute.\n"
    L"  B   SMR Blob attribute.\n"
    L"  [drive:][path][filename]\n"
    L"      Specifies a file or files for attrib to process.\n"
    L"  /S  Processes matching files in the current folder\n"
    L"      and all subfolders.\n"
    L"  /D  Processes folders as well.\n"
    L"  /L  Work on the attributes of the Symbolic Link versus\n"
    L"      the target of the Symbolic Link\n\n";

static DWORD bit_of(WCHAR letter)
{
    switch (tu_upper(letter)) {
    case L'R': return FILE_ATTRIBUTE_READONLY;
    case L'A': return FILE_ATTRIBUTE_ARCHIVE;
    case L'S': return FILE_ATTRIBUTE_SYSTEM;
    case L'H': return FILE_ATTRIBUTE_HIDDEN;
    case L'O': return FILE_ATTRIBUTE_OFFLINE_;
    case L'I': return FILE_ATTRIBUTE_NOT_CONTENT_INDEXED_;
    case L'X': return FILE_ATTRIBUTE_NO_SCRUB_DATA_;
    case L'V': return FILE_ATTRIBUTE_INTEGRITY_STREAM_;
    case L'P': return FILE_ATTRIBUTE_PINNED_;
    case L'U': return FILE_ATTRIBUTE_UNPINNED_;
    case L'B': return FILE_ATTRIBUTE_STRICTLY_SEQUENTIAL_;
    default: return 0;
    }
}

static void show(DWORD a, const WCHAR *path)
{
#define MARK(bit, letter) (((a) & (bit)) ? (letter) : L' ')
    tu_print(L"%c  %c%c%c %c%c %c%c %c%c %c    %s\n", MARK(FILE_ATTRIBUTE_ARCHIVE, L'A'), MARK(FILE_ATTRIBUTE_SYSTEM, L'S'),
             MARK(FILE_ATTRIBUTE_HIDDEN, L'H'), MARK(FILE_ATTRIBUTE_READONLY, L'R'), MARK(FILE_ATTRIBUTE_OFFLINE_, L'O'),
             MARK(FILE_ATTRIBUTE_NOT_CONTENT_INDEXED_, L'I'), MARK(FILE_ATTRIBUTE_INTEGRITY_STREAM_, L'V'),
             MARK(FILE_ATTRIBUTE_NO_SCRUB_DATA_, L'X'), MARK(FILE_ATTRIBUTE_PINNED_, L'P'),
             MARK(FILE_ATTRIBUTE_UNPINNED_, L'U'), MARK(FILE_ATTRIBUTE_STRICTLY_SEQUENTIAL_, L'B'), path);
#undef MARK
}

static void handle(STATE *s, const WCHAR *path, DWORD attributes)
{
    if (s->Display) {
        show(attributes, path);
        return;
    }
    if ((attributes & FILE_ATTRIBUTE_HIDDEN) && !s->MentionsHidden) {
        tu_print(L"Not resetting hidden file - %s\n", path);
        s->Failed = TRUE;
        return;
    }
    if ((attributes & FILE_ATTRIBUTE_SYSTEM) && !s->MentionsSystem) {
        tu_print(L"Not resetting system file - %s\n", path);
        s->Failed = TRUE;
        return;
    }
    DWORD changed = (attributes & ~s->Clear) | s->Set;
    /* the directory flag and the other flags the system owns are not set through this call. */
    changed &= ~(DWORD)(FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_COMPRESSED | FILE_ATTRIBUTE_REPARSE_POINT |
                        FILE_ATTRIBUTE_ENCRYPTED | FILE_ATTRIBUTE_SPARSE_FILE | FILE_ATTRIBUTE_TEMPORARY);
    changed |= attributes & FILE_ATTRIBUTE_TEMPORARY;
    if (!changed)
        changed = FILE_ATTRIBUTE_NORMAL;
    if (!SetFileAttributesW(path, changed)) {
        tu_print(GetLastError() == ERROR_ACCESS_DENIED ? L"Access denied - %s\n" : L"Unable to change attribute - %s\n",
                  path);
        s->Failed = TRUE;
    }
}

static WCHAR *full_path(const WCHAR *path)
{
    DWORD size = GetFullPathNameW(path, 0, nullptr, nullptr);
    if (!size)
        return tu_dup(path);
    WCHAR *full = tu_alloc(size * sizeof(WCHAR));
    GetFullPathNameW(path, size, full, nullptr);
    return full;
}

static BOOL has_wildcards(const WCHAR *text)
{
    return wcschr(text, L'*') || wcschr(text, L'?');
}

/* the entries of a directory that fit a name pattern; directory ends with a separator or is empty. */
static BOOL process(STATE *s, const WCHAR *directory, const WCHAR *pattern, BOOL explicit_name, int depth)
{
    BOOL found = FALSE;
    TU_STR spec = {};
    tu_str_format(&spec, L"%s%s", directory, pattern);
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(tu_str_text(&spec), &data);
    tu_str_free(&spec);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            BOOL is_directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (!wcscmp(data.cFileName, L".") || !wcscmp(data.cFileName, L".."))
                continue;
            if (is_directory && !s->Directories && !explicit_name)
                continue;
            found = TRUE;
            TU_STR path = {};
            tu_str_format(&path, L"%s%s", directory, data.cFileName);
            WCHAR *full = full_path(tu_str_text(&path));
            handle(s, full, data.dwFileAttributes);
            tu_free(full);
            tu_str_free(&path);
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    if (!s->Recurse || depth > 64)
        return found;
    TU_STR all = {};
    tu_str_format(&all, L"%s*", directory);
    find = FindFirstFileW(tu_str_text(&all), &data);
    tu_str_free(&all);
    if (find == INVALID_HANDLE_VALUE)
        return found;
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !wcscmp(data.cFileName, L".") ||
            !wcscmp(data.cFileName, L"..") || (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            continue;
        TU_STR next = {};
        tu_str_format(&next, L"%s%s\\", directory, data.cFileName);
        if (process(s, tu_str_text(&next), pattern, explicit_name, depth + 1))
            found = TRUE;
        tu_str_free(&next);
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return found;
}

int tool_main(void)
{
    STATE s = {};
    const WCHAR **specs = tu_alloc((tu_argc + 1) * sizeof(WCHAR *));
    int spec_count = 0;
    for (int i = 1; i < tu_argc; i++) {
        const WCHAR *a = tu_argv[i];
        if ((a[0] == L'+' || a[0] == L'-') && a[1] && !a[2]) {
            DWORD bit = bit_of(a[1]);
            if (!bit) {
                tu_print(L"Invalid switch - %s\n", a);
                return 1;
            }
            if (a[0] == L'+')
                s.Set |= bit;
            else
                s.Clear |= bit;
            if (bit == FILE_ATTRIBUTE_HIDDEN)
                s.MentionsHidden = TRUE;
            if (bit == FILE_ATTRIBUTE_SYSTEM)
                s.MentionsSystem = TRUE;
        } else if (a[0] == L'/') {
            if (tu_eq(a + 1, L"S"))
                s.Recurse = TRUE;
            else if (tu_eq(a + 1, L"D"))
                s.Directories = TRUE;
            else if (tu_eq(a + 1, L"L"))
                ;
            else if (tu_eq(a + 1, L"?")) {
                tu_puts(kUsage);
                return 0;
            } else {
                tu_print(L"Invalid switch - %s\n", a);
                return 1;
            }
        } else {
            specs[spec_count++] = a;
        }
    }
    s.Display = !s.Set && !s.Clear;
    if (!spec_count)
        specs[spec_count++] = L"*";
    for (int i = 0; i < spec_count; i++) {
        const WCHAR *spec = specs[i];
        SIZE_T dir_length = 0;
        for (SIZE_T k = 0; spec[k]; k++)
            if (spec[k] == L'\\' || spec[k] == L'/' || spec[k] == L':')
                dir_length = k + 1;
        WCHAR *directory = tu_dup_n(spec, dir_length);
        const WCHAR *pattern = spec + dir_length;
        if (!*pattern)
            pattern = L"*";
        if (!process(&s, directory, pattern, !has_wildcards(pattern), 0)) {
            DWORD attributes = GetFileAttributesW(directory[0] ? directory : L".");
            if (dir_length && attributes == INVALID_FILE_ATTRIBUTES)
                tu_print(L"Path not found - %s\n", spec);
            else
                tu_print(L"File not found - %s\n", spec);
            s.Failed = TRUE;
        }
        tu_free(directory);
    }
    return s.Failed ? 1 : 0;
}
