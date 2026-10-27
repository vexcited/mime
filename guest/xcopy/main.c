/*
 * xcopy.exe: copies files and directory trees.
 *
 *   xcopy source [destination] [/A | /M] [/D[:m-d-y]] [/P] [/S [/E]] [/V] [/W] [/C] [/I] [/-I] [/Q] [/F] [/L] [/H] [/R]
 *         [/T] [/U] [/K] [/Y] [/-Y] [/EXCLUDE:file1[+file2]...]
 *
 * the switches that need the owner, the audit settings, short names, restartable or unbuffered copies are accepted
 * and have no effect. the exit code is 0 after a copy, 1 when no file was found, 2 after CTRL+C, 4 for an error of the
 * command line or the files and 5 when the disk is full.
 */
#include "tool.h"

#define EXIT_NOT_FOUND 1
#define EXIT_ERROR 4
#define EXIT_DISK 5

typedef struct OPTIONS {
    BOOL ArchiveOnly, ArchiveReset, Prompt, Subdirectories, Empty, Wait, Continue, AssumeDirectory, AssumeFile, Quiet,
        Full, ListOnly, Hidden, ReadOnly, TreeOnly, UpdateOnly, KeepAttributes, NoPrompt, ForcePrompt;
    BOOL HaveDate, DateFromDestination;
    FILETIME Date;
    WCHAR **Excludes;
    int ExcludeCount;
} OPTIONS;

typedef struct COPY_STATE {
    OPTIONS *Options;
    WCHAR *SourceRoot;
    /* what the source path is shown as: the way the command line gave it. */
    WCHAR *DisplayRoot;
    WCHAR *DestinationRoot;
    DWORD Copied;
    int Exit;
    BOOL Stop;
    /* the destination is the file name of the one file copied. */
    BOOL DestinationIsFile;
} COPY_STATE;

static const WCHAR kUsage[] =
    L"Copies files and directory trees.\n\n"
    L"XCOPY source [destination] [/A | /M] [/D[:date]] [/P] [/S [/E]] [/V] [/W]\n"
    L"                           [/C] [/I] [/-I] [/Q] [/F] [/L] [/G] [/H] [/R] [/T]\n"
    L"                           [/U] [/K] [/N] [/O] [/X] [/Y] [/-Y] [/Z] [/B] [/J]\n"
    L"                           [/EXCLUDE:file1[+file2][+file3]...] [/COMPRESS]\n"
    L"                           [/[-]SPARSE] [/NOCLONE]\n\n"
    L"  source       Specifies the file(s) to copy.\n"
    L"  destination  Specifies the location and/or name of new files.\n"
    L"  /A           Copies only files with the archive attribute set,\n"
    L"               doesn't change the attribute.\n"
    L"  /M           Copies only files with the archive attribute set,\n"
    L"               turns off the archive attribute.\n"
    L"  /D:m-d-y     Copies files changed on or after the specified date.\n"
    L"               If no date is given, copies only those files whose\n"
    L"               source time is newer than the destination time.\n"
    L"  /EXCLUDE:file1[+file2][+file3]...\n"
    L"               Specifies a list of files containing strings.  Each string\n"
    L"               should be in a separate line in the files.  When any of the\n"
    L"               strings match any part of the absolute path of the file to be\n"
    L"               copied, that file will be excluded from being copied.  For\n"
    L"               example, specifying a string like \\obj\\ or .obj will exclude\n"
    L"               all files underneath the directory obj or all files with the\n"
    L"               .obj extension respectively.\n"
    L"  /P           Prompts you before creating each destination file.\n"
    L"  /S           Copies directories and subdirectories except empty ones.\n"
    L"  /E           Copies directories and subdirectories, including empty ones.\n"
    L"               Same as /S /E. May be used to modify /T.\n"
    L"  /V           Verifies the size of each new file.\n"
    L"  /W           Prompts you to press a key before copying.\n"
    L"  /C           Continues copying even if errors occur.\n"
    L"  /I           If destination does not exist and copying more than one file,\n"
    L"               assumes that destination must be a directory.\n"
    L"  /-I          If destination does not exist and copying a single specified file,\n"
    L"               assumes that destination must be a file.\n"
    L"  /Q           Does not display file names while copying.\n"
    L"  /F           Displays full source and destination file names while copying.\n"
    L"  /L           Displays files that would be copied.\n"
    L"  /G           Allows the copying of encrypted files to destination that does\n"
    L"               not support encryption.\n"
    L"  /H           Copies hidden and system files also.\n"
    L"  /R           Overwrites read-only files.\n"
    L"  /T           Creates directory structure, but does not copy files. Does not\n"
    L"               include empty directories or subdirectories. /T /E includes\n"
    L"               empty directories and subdirectories.\n"
    L"  /U           Copies only files that already exist in destination.\n"
    L"  /K           Copies attributes. Normal Xcopy will reset read-only attributes.\n"
    L"  /N           Copies using the generated short names.\n"
    L"  /O           Copies file ownership and ACL information.\n"
    L"  /X           Copies file audit settings (implies /O).\n"
    L"  /Y           Suppresses prompting to confirm you want to overwrite an\n"
    L"               existing destination file.\n"
    L"  /-Y          Causes prompting to confirm you want to overwrite an\n"
    L"               existing destination file.\n"
    L"  /Z           Copies networked files in restartable mode.\n"
    L"  /B           Copies the Symbolic Link itself versus the target of the link.\n"
    L"  /J           Copies using unbuffered I/O. Recommended for very large files.\n"
    L"  /COMPRESS    Request network compression during file transfer where\n"
    L"               applicable.\n"
    L"  /[-]SPARSE   Enable or disable retaining the sparse state of files during \n"
    L"               copy. /-SPARSE overrides /SPARSE if both are provided.\n"
    L"  /NOCLONE     Do not attempt block cloning as an optimization.\n\n"
    L"The switch /Y may be preset in the COPYCMD environment variable.\n"
    L"This may be overridden with /-Y on the command line.\n";

static DECLSPEC_NORETURN void die(int code, const WCHAR *format, const WCHAR *argument)
{
    tu_print(format, argument);
    tu_exit(code);
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

static BOOL is_directory(const WCHAR *path)
{
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

/* a key from the input: one of the letters of allowed, in capitals. */
static WCHAR ask_key(const WCHAR *allowed)
{
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    BOOL console = input != INVALID_HANDLE_VALUE && GetConsoleMode(input, &mode);
    if (console)
        SetConsoleMode(input, (mode & ~(DWORD)ENABLE_LINE_INPUT & ~(DWORD)ENABLE_ECHO_INPUT) | ENABLE_PROCESSED_INPUT);
    tu_flush();
    WCHAR result = 0;
    for (;;) {
        WCHAR c = 0;
        if (console) {
            INPUT_RECORD record;
            DWORD read = 0;
            if (!ReadConsoleInputW(input, &record, 1, &read) || !read)
                break;
            if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown)
                continue;
            c = record.Event.KeyEvent.uChar.UnicodeChar;
        } else {
            BYTE byte;
            DWORD read = 0;
            if (!ReadFile(input, &byte, 1, &read, nullptr) || !read)
                break;
            c = byte;
        }
        c = tu_upper(c);
        if (c && wcschr(allowed, c)) {
            result = c;
            break;
        }
    }
    if (console)
        SetConsoleMode(input, mode);
    if (result)
        tu_print(L"%c\n", result);
    else
        tu_print(L"\n");
    return result;
}

/* ------------------------------------------------------------------ the settings */

/* m-d-y with two or four digit years. */
static BOOL parse_date(const WCHAR *text, FILETIME *out)
{
    DWORD parts[3] = {};
    int n = 0;
    BOOL digits = FALSE;
    for (const WCHAR *p = text;; p++) {
        if (tu_is_digit(*p)) {
            parts[n] = parts[n] * 10 + (*p - L'0');
            digits = TRUE;
        } else if ((*p == L'-' || *p == L'/' || *p == L'.' || !*p) && digits && n < 3) {
            n++;
            digits = FALSE;
            if (!*p)
                break;
        } else {
            return FALSE;
        }
    }
    if (n != 3)
        return FALSE;
    if (parts[2] < 100)
        parts[2] += parts[2] < 80 ? 2000 : 1900;
    SYSTEMTIME t = {};
    t.wMonth = (WORD)parts[0];
    t.wDay = (WORD)parts[1];
    t.wYear = (WORD)parts[2];
    FILETIME local;
    if (parts[0] < 1 || parts[0] > 12 || parts[1] < 1 || parts[1] > 31 || parts[2] < 1980 || parts[2] > 2099)
        return FALSE;
    if (!SystemTimeToFileTime(&t, &local))
        return FALSE;
    return LocalFileTimeToFileTime(&local, out);
}

static void load_excludes(OPTIONS *o, const WCHAR *list)
{
    WCHAR *copy = tu_dup(list);
    for (WCHAR *name = copy; name;) {
        WCHAR *plus = wcschr(name, L'+');
        if (plus)
            *plus = 0;
        HANDLE h = CreateFileW(name, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            die(EXIT_ERROR, L"File not found - %s\n", name);
        TU_READER reader;
        tu_reader_init(&reader, h);
        const BYTE *line;
        SIZE_T length;
        while (tu_reader_line(&reader, &line, &length)) {
            if (!length)
                continue;
            WCHAR *wide = tu_alloc((length + 1) * sizeof(WCHAR));
            MultiByteToWideChar(GetOEMCP(), 0, (LPCSTR)line, (int)length, wide, (int)length);
            o->Excludes = tu_realloc(o->Excludes, (o->ExcludeCount + 1) * sizeof(WCHAR *));
            o->Excludes[o->ExcludeCount++] = wide;
        }
        tu_reader_free(&reader);
        CloseHandle(h);
        name = plus ? plus + 1 : nullptr;
    }
    tu_free(copy);
}

static BOOL excluded(const OPTIONS *o, const WCHAR *path)
{
    for (int i = 0; i < o->ExcludeCount; i++) {
        SIZE_T n = wcslen(o->Excludes[i]);
        for (const WCHAR *p = path; *p; p++)
            if (tu_eq_n(p, o->Excludes[i], n) && wcsnlen(p, n) == n)
                return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ copying */

static BOOL make_directories(const WCHAR *path)
{
    if (is_directory(path))
        return TRUE;
    WCHAR *copy = tu_dup(path);
    SIZE_T n = wcslen(copy);
    while (n > 3 && (copy[n - 1] == L'\\' || copy[n - 1] == L'/'))
        copy[--n] = 0;
    BOOL ok = TRUE;
    /* each level from the top, the first level that exists stays. */
    for (SIZE_T i = 3; i <= n && ok; i++) {
        if (copy[i] == L'\\' || copy[i] == L'/' || copy[i] == 0) {
            WCHAR saved = copy[i];
            copy[i] = 0;
            if (!is_directory(copy) && !CreateDirectoryW(copy, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
                ok = FALSE;
            copy[i] = saved;
            if (!saved)
                break;
        }
    }
    tu_free(copy);
    return ok;
}

static void report_error(COPY_STATE *s, DWORD error, const WCHAR *destination)
{
    switch (error) {
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL:
        tu_print(L"Insufficient disk space\n");
        s->Exit = EXIT_DISK;
        break;
    case ERROR_SHARING_VIOLATION:
        tu_print(L"Sharing violation\n");
        s->Exit = EXIT_ERROR;
        break;
    case ERROR_ACCESS_DENIED:
        tu_print(L"Access denied\n");
        s->Exit = EXIT_ERROR;
        break;
    default: {
        TU_STR text = {};
        tu_error_text(error, &text);
        tu_print(L"File creation error - %s\n", tu_str_text(&text));
        tu_str_free(&text);
        (void)destination;
        s->Exit = EXIT_ERROR;
        break;
    }
    }
    if (!s->Options->Continue)
        s->Stop = TRUE;
}

static void copy_one(COPY_STATE *s, const WCHAR *source, const WCHAR *destination, const WIN32_FIND_DATAW *data)
{
    OPTIONS *o = s->Options;
    DWORD destination_attributes = GetFileAttributesW(destination);
    BOOL exists = destination_attributes != INVALID_FILE_ATTRIBUTES;
    if (exists && (destination_attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        tu_print(L"File creation error - Access is denied.\n");
        s->Exit = EXIT_ERROR;
        s->Stop = !o->Continue;
        return;
    }
    if (o->UpdateOnly && !exists)
        return;
    if (o->HaveDate) {
        if (o->DateFromDestination) {
            WIN32_FILE_ATTRIBUTE_DATA info;
            if (exists && GetFileAttributesExW(destination, GetFileExInfoStandard, &info) &&
                CompareFileTime(&data->ftLastWriteTime, &info.ftLastWriteTime) <= 0)
                return;
        } else if (CompareFileTime(&data->ftLastWriteTime, &o->Date) < 0) {
            return;
        }
    }
    if (excluded(o, source))
        return;
    if (o->TreeOnly) {
        return;
    }
    if (o->Prompt) {
        tu_print(L"%s (Y/N)? ", destination);
        if (ask_key(L"YN") != L'Y')
            return;
    }
    if (o->Wait) {
        tu_print(L"Press any key when ready to begin copying file(s)");
        o->Wait = FALSE;
        HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
        DWORD mode;
        if (GetConsoleMode(input, &mode)) {
            SetConsoleMode(input, (mode & ~(DWORD)ENABLE_LINE_INPUT & ~(DWORD)ENABLE_ECHO_INPUT));
            INPUT_RECORD record;
            DWORD read;
            do {
                ReadConsoleInputW(input, &record, 1, &read);
            } while (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown);
            SetConsoleMode(input, mode);
        }
        tu_print(L"\n");
    }
    if (!o->Quiet) {
        if (o->Full) {
            tu_print(L"%s -> %s\n", source, destination);
        } else {
            SIZE_T root = wcslen(s->SourceRoot);
            if (s->DisplayRoot && wcslen(source) >= root && tu_eq_n(source, s->SourceRoot, root))
                tu_print(L"%s%s\n", s->DisplayRoot, source + root);
            else
                tu_print(L"%s\n", source);
        }
    }
    if (o->ListOnly) {
        s->Copied++;
        return;
    }
    if (exists && !o->NoPrompt) {
        tu_print(L"Overwrite %s (Yes/No/All)? ", destination);
        WCHAR k = ask_key(L"YNA");
        if (k == L'A')
            o->NoPrompt = TRUE;
        else if (k != L'Y')
            return;
    }
    if (exists && (destination_attributes & FILE_ATTRIBUTE_READONLY)) {
        if (!o->ReadOnly) {
            report_error(s, ERROR_ACCESS_DENIED, destination);
            return;
        }
        SetFileAttributesW(destination, destination_attributes & ~(DWORD)FILE_ATTRIBUTE_READONLY);
    }
    if (!CopyFileW(source, destination, FALSE)) {
        report_error(s, GetLastError(), destination);
        return;
    }
    /* a copy is not read-only unless the attributes are kept, and its archive bit is set. */
    DWORD attributes = GetFileAttributesW(destination);
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        DWORD wanted = o->KeepAttributes ? data->dwFileAttributes : (data->dwFileAttributes & ~(DWORD)FILE_ATTRIBUTE_READONLY);
        wanted |= FILE_ATTRIBUTE_ARCHIVE;
        wanted &= ~(DWORD)(FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_COMPRESSED | FILE_ATTRIBUTE_ENCRYPTED |
                           FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_SPARSE_FILE);
        if (wanted != (attributes & ~(DWORD)FILE_ATTRIBUTE_COMPRESSED))
            SetFileAttributesW(destination, wanted);
    }
    if (o->ArchiveReset)
        SetFileAttributesW(source, data->dwFileAttributes & ~(DWORD)FILE_ATTRIBUTE_ARCHIVE);
    s->Copied++;
}

/* the files of source_dir that fit the pattern go to destination_dir, and with /S the directories below go on. */
static void copy_directory(COPY_STATE *s, const WCHAR *source_dir, const WCHAR *pattern, const WCHAR *destination_dir,
                           const WCHAR *destination_file, int depth)
{
    OPTIONS *o = s->Options;
    TU_STR spec = {};
    tu_str_format(&spec, L"%s%s", source_dir, pattern);
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(tu_str_text(&spec), &data);
    tu_str_free(&spec);
    BOOL created = FALSE;
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (s->Stop)
                break;
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            if (!o->Hidden && (data.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
                continue;
            if ((o->ArchiveOnly || o->ArchiveReset) && !(data.dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE))
                continue;
            TU_STR source = {}, destination = {};
            tu_str_format(&source, L"%s%s", source_dir, data.cFileName);
            if (destination_file)
                tu_str_puts(&destination, destination_file);
            else
                tu_str_format(&destination, L"%s%s", destination_dir, data.cFileName);
            if (!created && !o->ListOnly) {
                created = TRUE;
                if (!make_directories(destination_dir)) {
                    tu_print(L"Unable to create directory - %s\n", destination_dir);
                    s->Exit = EXIT_ERROR;
                    s->Stop = TRUE;
                }
            }
            if (!s->Stop) {
                if (o->TreeOnly)
                    ; /* only the directory is wanted. */
                else
                    copy_one(s, tu_str_text(&source), tu_str_text(&destination), &data);
            }
            tu_str_free(&source);
            tu_str_free(&destination);
        } while (!s->Stop && FindNextFileW(find, &data));
        FindClose(find);
    }
    if (!o->Subdirectories || s->Stop || depth > 64)
        return;
    TU_STR all = {};
    tu_str_format(&all, L"%s*", source_dir);
    find = FindFirstFileW(tu_str_text(&all), &data);
    tu_str_free(&all);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do {
        if (s->Stop)
            break;
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !wcscmp(data.cFileName, L".") ||
            !wcscmp(data.cFileName, L".."))
            continue;
        if (!o->Hidden && (data.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
            continue;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            continue;
        TU_STR source = {}, destination = {};
        tu_str_format(&source, L"%s%s\\", source_dir, data.cFileName);
        tu_str_format(&destination, L"%s%s\\", destination_dir, data.cFileName);
        /* the tree being copied into itself would never end. */
        if (!wcscmp(tu_str_text(&source), s->DestinationRoot) ||
            (wcslen(s->DestinationRoot) >= wcslen(tu_str_text(&source)) &&
             tu_eq_n(s->DestinationRoot, tu_str_text(&source), wcslen(tu_str_text(&source))))) {
            /* the destination lies below this directory: skip it, as the sources below would repeat. */
            tu_str_free(&source);
            tu_str_free(&destination);
            continue;
        }
        if (o->Empty && !o->ListOnly)
            make_directories(tu_str_text(&destination));
        copy_directory(s, tu_str_text(&source), pattern, tu_str_text(&destination), nullptr, depth + 1);
        tu_str_free(&source);
        tu_str_free(&destination);
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

int tool_main(void)
{
    OPTIONS o = {};
    const WCHAR *source_arg = nullptr, *destination_arg = nullptr;
    for (int i = 1; i < tu_argc; i++) {
        const WCHAR *a = tu_argv[i];
        if (a[0] == L'/' && a[1]) {
            const WCHAR *r = a + 1;
            const WCHAR *rest;
            if (tu_eq(r, L"?")) {
                tu_puts(kUsage);
                return 0;
            } else if (tu_eq(r, L"A")) o.ArchiveOnly = TRUE;
            else if (tu_eq(r, L"M")) o.ArchiveReset = TRUE;
            else if (tu_eq(r, L"P")) o.Prompt = TRUE;
            else if (tu_eq(r, L"S")) o.Subdirectories = TRUE;
            else if (tu_eq(r, L"E")) { o.Subdirectories = TRUE; o.Empty = TRUE; }
            else if (tu_eq(r, L"V")) ;
            else if (tu_eq(r, L"W")) o.Wait = TRUE;
            else if (tu_eq(r, L"C")) o.Continue = TRUE;
            else if (tu_eq(r, L"I")) o.AssumeDirectory = TRUE;
            else if (tu_eq(r, L"-I")) o.AssumeFile = TRUE;
            else if (tu_eq(r, L"Q")) o.Quiet = TRUE;
            else if (tu_eq(r, L"F")) o.Full = TRUE;
            else if (tu_eq(r, L"L")) o.ListOnly = TRUE;
            else if (tu_eq(r, L"H")) o.Hidden = TRUE;
            else if (tu_eq(r, L"R")) o.ReadOnly = TRUE;
            else if (tu_eq(r, L"T")) o.TreeOnly = TRUE;
            else if (tu_eq(r, L"U")) o.UpdateOnly = TRUE;
            else if (tu_eq(r, L"K")) o.KeepAttributes = TRUE;
            else if (tu_eq(r, L"Y")) { o.NoPrompt = TRUE; o.ForcePrompt = FALSE; }
            else if (tu_eq(r, L"-Y")) { o.ForcePrompt = TRUE; o.NoPrompt = FALSE; }
            else if (tu_eq(r, L"G") || tu_eq(r, L"N") || tu_eq(r, L"O") || tu_eq(r, L"X") || tu_eq(r, L"Z") ||
                     tu_eq(r, L"B") || tu_eq(r, L"J") || tu_eq(r, L"COMPRESS") || tu_eq(r, L"SPARSE") ||
                     tu_eq(r, L"-SPARSE") || tu_eq(r, L"NOCLONE")) ;
            else if (tu_prefix(r, L"D", &rest) && (!*rest || *rest == L':')) {
                o.HaveDate = TRUE;
                if (*rest == L':' && rest[1]) {
                    if (!parse_date(rest + 1, &o.Date))
                        die(EXIT_ERROR, L"Invalid date\n", nullptr);
                } else {
                    o.DateFromDestination = TRUE;
                }
            } else if (tu_prefix(r, L"EXCLUDE:", &rest)) {
                load_excludes(&o, rest);
            } else {
                die(EXIT_ERROR, L"Invalid switch - %s\n", a);
            }
            continue;
        }
        if (!source_arg)
            source_arg = a;
        else if (!destination_arg)
            destination_arg = a;
        else
            die(EXIT_ERROR, L"Invalid number of parameters\n", nullptr);
    }
    if (!source_arg)
        die(EXIT_ERROR, L"Invalid number of parameters\n", nullptr);
    if (!o.ForcePrompt && !o.NoPrompt) {
        WCHAR *copycmd = tu_getenv(L"COPYCMD");
        if (copycmd) {
            for (WCHAR *p = copycmd; *p; p++)
                if (p[0] == L'/' && tu_upper(p[1]) == L'Y')
                    o.NoPrompt = TRUE;
            tu_free(copycmd);
        }
    }

    /* the source as a directory and a pattern. */
    WCHAR *source_full = full_path(source_arg);
    SIZE_T n = wcslen(source_full);
    while (n > 3 && source_full[n - 1] == L'\\')
        source_full[--n] = 0;
    TU_STR source_dir = {};
    WCHAR *pattern;
    if (is_directory(source_full) && !has_wildcards(source_full)) {
        tu_str_format(&source_dir, L"%s%s", source_full, source_full[n - 1] == L'\\' ? L"" : L"\\");
        pattern = tu_dup(L"*");
    } else {
        WCHAR *slash = wcsrchr(source_full, L'\\');
        if (!slash)
            die(EXIT_ERROR, L"Invalid path\n", nullptr);
        tu_str_append(&source_dir, source_full, (SIZE_T)(slash - source_full) + 1);
        pattern = tu_dup(slash + 1);
    }
    if (!is_directory(tu_str_text(&source_dir))) {
        tu_print(L"File not found - %s\n0 File(s) copied\n", source_arg);
        return EXIT_NOT_FOUND;
    }

    /* the destination, and whether it names a directory. */
    WCHAR *destination_full = full_path(destination_arg ? destination_arg : L".");
    BOOL trailing = destination_arg && (destination_arg[wcslen(destination_arg) - 1] == L'\\' ||
                                        destination_arg[wcslen(destination_arg) - 1] == L'/');
    BOOL multiple = has_wildcards(pattern) || !wcscmp(pattern, L"*") || o.Subdirectories;
    BOOL destination_is_file = FALSE;
    DWORD destination_attributes = GetFileAttributesW(destination_full);
    if (destination_attributes != INVALID_FILE_ATTRIBUTES) {
        destination_is_file = !(destination_attributes & FILE_ATTRIBUTE_DIRECTORY);
        if (destination_is_file && multiple && !has_wildcards(pattern) && 0)
            ;
    } else if (trailing || o.AssumeDirectory) {
        destination_is_file = FALSE;
    } else if (o.AssumeFile) {
        destination_is_file = TRUE;
    } else {
        tu_print(L"Does %s specify a file name\nor directory name on the target\n(F = file, D = directory)? ", destination_arg);
        WCHAR k = ask_key(L"FD");
        if (!k)
            return EXIT_ERROR;
        destination_is_file = k == L'F';
    }
    TU_STR destination_dir = {};
    const WCHAR *destination_file = nullptr;
    if (destination_is_file && !multiple) {
        destination_file = destination_full;
        WCHAR *slash = wcsrchr(destination_full, L'\\');
        if (slash)
            tu_str_append(&destination_dir, destination_full, (SIZE_T)(slash - destination_full) + 1);
    } else {
        tu_str_puts(&destination_dir, destination_full);
        if (destination_dir.length && destination_dir.data[destination_dir.length - 1] != L'\\')
            tu_str_putc(&destination_dir, L'\\');
    }

    /* a file onto itself. */
    if (destination_file && tu_eq(destination_file, source_full)) {
        tu_print(L"File cannot be copied onto itself\n0 File(s) copied\n");
        return EXIT_ERROR;
    }
    if (!destination_file && !wcscmp(tu_str_text(&destination_dir), tu_str_text(&source_dir)) && !o.TreeOnly) {
        tu_print(L"File cannot be copied onto itself\n0 File(s) copied\n");
        return EXIT_ERROR;
    }
    if (o.Subdirectories) {
        const WCHAR *s = tu_str_text(&source_dir), *d = tu_str_text(&destination_dir);
        if (wcslen(d) > wcslen(s) && tu_eq_n(d, s, wcslen(s)) && !is_directory(d)) {
            /* a destination below the source that does not exist yet is created inside the tree being read. */
            tu_print(L"Cannot perform a cyclic copy\n");
            return EXIT_ERROR;
        }
    }

    COPY_STATE state = {};
    state.Options = &o;
    state.SourceRoot = tu_dup(tu_str_text(&source_dir));
    state.DestinationRoot = tu_dup(tu_str_text(&destination_dir));
    state.DestinationIsFile = destination_file != nullptr;
    {
        /* a relative source is shown relative, a directory as given with a separator after it. */
        BOOL relative = !(source_arg[0] == L'\\' || (source_arg[0] && source_arg[1] == L':'));
        if (relative) {
            TU_STR shown = {};
            SIZE_T last = 0;
            for (SIZE_T i = 0; source_arg[i]; i++)
                if (source_arg[i] == L'\\' || source_arg[i] == L'/')
                    last = i + 1;
            if (is_directory(source_full) && !has_wildcards(source_arg)) {
                tu_str_puts(&shown, source_arg);
                if (shown.length && shown.data[shown.length - 1] != L'\\' && shown.data[shown.length - 1] != L'/')
                    tu_str_putc(&shown, L'\\');
            } else {
                tu_str_append(&shown, source_arg, last);
            }
            state.DisplayRoot = tu_dup(tu_str_text(&shown));
            tu_str_free(&shown);
        }
    }
    if (o.TreeOnly && !o.ListOnly)
        make_directories(tu_str_text(&destination_dir)) ;
    copy_directory(&state, tu_str_text(&source_dir), pattern, tu_str_text(&destination_dir), destination_file, 0);

    if (!state.Copied && !state.Exit && !o.TreeOnly) {
        /* nothing matched at all. */
        tu_print(L"File not found - %s\n", source_arg);
        tu_print(o.ListOnly ? L"%lu File(s)\n" : L"%lu File(s) copied\n", state.Copied);
        return EXIT_NOT_FOUND;
    }
    tu_print(o.ListOnly ? L"%lu File(s)\n" : L"%lu File(s) copied\n", state.Copied);
    return state.Exit;
}
