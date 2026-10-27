/*
 * where.exe: finds files by name in the current directory and the directories of PATH, or under a directory.
 *
 *   where [/R dir] [/Q] [/F] [/T] pattern...
 *
 * a pattern holds * and ?, and is tried as it is and with every extension of PATHEXT after it. `$env:pattern` searches the
 * directories of an environment variable and `dirs:pattern` the directories listed in the text. the exit code is 0 when
 * everything was found, 1 when something was not and 2 for a command line that cannot be used.
 */
#include "tool.h"

static const WCHAR kUsage[] =
    L"\nWHERE [/R dir] [/Q] [/F] [/T] pattern...\n\n"
    L"Description:\n"
    L"    Displays the location of files that match the search pattern.\n"
    L"    By default, the search is done along the current directory and\n"
    L"    in the paths specified by the PATH environment variable.\n\n"
    L"Parameter List:\n"
    L"    /R       Recursively searches and displays the files that match the\n"
    L"             given pattern starting from the specified directory.\n\n"
    L"    /Q       Returns only the exit code, without displaying the list\n"
    L"             of matched files. (Quiet mode)\n\n"
    L"    /F       Displays the matched filename in double quotes.\n\n"
    L"    /T       Displays the file size, last modified date and time for all\n"
    L"             matched files.\n\n"
    L"    pattern  Specifies the search pattern for the files to match.\n"
    L"             Wildcards * and ? can be used in the pattern. The\n"
    L"             \"$env:pattern\" and \"path:pattern\" formats can also be\n"
    L"             specified, where \"env\" is an environment variable and\n"
    L"             the search is done in the specified paths of the \"env\"\n"
    L"             environment variable. These formats should not be used\n"
    L"             with /R. The search is also done by appending the\n"
    L"             extensions of the PATHEXT variable to the pattern.\n\n"
    L"     /?      Displays this help message.\n\n"
    L"  NOTE: The tool returns an error level of 0 if the search is\n"
    L"        successful, of 1 if the search is unsuccessful and\n"
    L"        of 2 for failures or errors.\n\n"
    L"Examples:\n"
    L"    WHERE /?\n"
    L"    WHERE myfilename1 myfile????.*\n"
    L"    WHERE $windir:*.* \n"
    L"    WHERE /R c:\\windows *.exe *.dll *.bat  \n"
    L"    WHERE /Q ??.??? \n"
    L"    WHERE \"c:\\windows;c:\\windows\\system32:*.dll\"\n"
    L"    WHERE /F /T *.dll \n";

typedef struct SEARCH {
    BOOL Quiet;
    BOOL Quote;
    BOOL Times;
    WCHAR **Extensions;
    int ExtensionCount;
    DWORD Found;
} SEARCH;

static const WCHAR *any_of(const WCHAR *text, const WCHAR *set)
{
    for (; *text; text++)
        if (wcschr(set, *text))
            return text;
    return nullptr;
}

static int error_exit(const WCHAR *text)
{
    tu_eprint(L"%s", text);
    return 2;
}

static BOOL is_switch(const WCHAR *a, const WCHAR *name)
{
    return (a[0] == L'/' || a[0] == L'-') && tu_eq(a + 1, name);
}

static void print_match(SEARCH *s, const WCHAR *directory, const WIN32_FIND_DATAW *data)
{
    s->Found++;
    if (s->Quiet)
        return;
    TU_STR line = {};
    if (s->Times) {
        FILETIME local;
        SYSTEMTIME t;
        ULONGLONG size = ((ULONGLONG)data->nFileSizeHigh << 32) | data->nFileSizeLow;
        FileTimeToLocalFileTime(&data->ftLastWriteTime, &local);
        FileTimeToSystemTime(&local, &t);
        DWORD hour = t.wHour % 12 ? t.wHour % 12 : 12;
        tu_str_format(&line, L"%9llu  %u/%u/%u  %2u:%02u:%02u %s  ", size, t.wMonth, t.wDay, t.wYear, hour, t.wMinute,
                      t.wSecond, t.wHour < 12 ? L"AM" : L"PM");
    }
    if (s->Quote)
        tu_str_putc(&line, L'"');
    tu_str_puts(&line, directory);
    SIZE_T n = wcslen(directory);
    if (n && directory[n - 1] != L'\\' && directory[n - 1] != L'/')
        tu_str_putc(&line, L'\\');
    tu_str_puts(&line, data->cFileName);
    if (s->Quote)
        tu_str_putc(&line, L'"');
    tu_str_putc(&line, L'\n');
    tu_write(TU_STDOUT, line.data, line.length);
    tu_str_free(&line);
}

/* the files of a directory matching one pattern. */
static void match_in(SEARCH *s, const WCHAR *directory, const WCHAR *pattern)
{
    TU_STR path = {};
    tu_str_puts(&path, directory);
    SIZE_T n = path.length;
    if (n && path.data[n - 1] != L'\\' && path.data[n - 1] != L'/')
        tu_str_putc(&path, L'\\');
    tu_str_puts(&path, pattern);
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(tu_str_text(&path), &data);
    tu_str_free(&path);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        print_match(s, directory, &data);
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

/* the pattern as it is, then with each extension after it. */
static void search_dir(SEARCH *s, const WCHAR *directory, const WCHAR *pattern)
{
    match_in(s, directory, pattern);
    for (int i = 0; i < s->ExtensionCount; i++) {
        TU_STR with = {};
        tu_str_format(&with, L"%s%s", pattern, s->Extensions[i]);
        match_in(s, directory, tu_str_text(&with));
        tu_str_free(&with);
    }
}

static void search_tree(SEARCH *s, const WCHAR *directory, const WCHAR *pattern, int depth)
{
    search_dir(s, directory, pattern);
    if (depth > 64)
        return;
    TU_STR all = {};
    tu_str_format(&all, L"%s%s*", directory, (directory[0] && directory[wcslen(directory) - 1] != L'\\') ? L"\\" : L"");
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(tu_str_text(&all), &data);
    tu_str_free(&all);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !wcscmp(data.cFileName, L".") ||
            !wcscmp(data.cFileName, L"..") || (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            continue;
        TU_STR sub = {};
        tu_str_format(&sub, L"%s%s%s", directory, (directory[0] && directory[wcslen(directory) - 1] != L'\\') ? L"\\" : L"",
                      data.cFileName);
        search_tree(s, tu_str_text(&sub), pattern, depth + 1);
        tu_str_free(&sub);
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

/* the directories of a list separated by semicolons; quotes around a directory are removed. */
static void search_list(SEARCH *s, const WCHAR *list, const WCHAR *pattern)
{
    WCHAR *copy = tu_dup(list);
    for (WCHAR *dir = copy; dir;) {
        WCHAR *semi = wcschr(dir, L';');
        if (semi)
            *semi = 0;
        SIZE_T n = wcslen(dir);
        if (n >= 2 && dir[0] == L'"' && dir[n - 1] == L'"') {
            dir[n - 1] = 0;
            dir++;
        }
        if (*dir)
            search_dir(s, dir, pattern);
        dir = semi ? semi + 1 : nullptr;
    }
    tu_free(copy);
}

int tool_main(void)
{
    SEARCH s = {};
    const WCHAR *root = nullptr;
    WCHAR **patterns = tu_alloc((tu_argc + 1) * sizeof(WCHAR *));
    int pattern_count = 0;
    for (int i = 1; i < tu_argc; i++) {
        const WCHAR *a = tu_argv[i];
        if (is_switch(a, L"?")) {
            tu_puts(kUsage);
            return 0;
        } else if (is_switch(a, L"Q")) {
            s.Quiet = TRUE;
        } else if (is_switch(a, L"F")) {
            s.Quote = TRUE;
        } else if (is_switch(a, L"T")) {
            s.Times = TRUE;
        } else if (is_switch(a, L"R")) {
            if (i + 1 >= tu_argc)
                return error_exit(L"ERROR: No directory specified for recursive option.\n");
            root = tu_argv[++i];
        } else if ((a[0] == L'/' || a[0] == L'-') && a[1]) {
            tu_eprint(L"ERROR: Invalid argument or option - '%s'.\nType \"WHERE /?\" for usage help.\n", a);
            return 2;
        } else {
            patterns[pattern_count++] = (WCHAR *)a;
        }
    }
    if (!pattern_count) {
        tu_eprint(L"ERROR: Invalid argument or option - ''.\nThe syntax of this command is:\n");
        tu_puts(kUsage);
        return 2;
    }
    if (root) {
        DWORD attributes = GetFileAttributesW(root);
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
            return error_exit(L"ERROR: Invalid directory specified.\n");
    }

    WCHAR *pathext = tu_getenv(L"PATHEXT");
    const WCHAR *ext_list = pathext ? pathext : L".COM;.EXE;.BAT;.CMD;.VBS;.VBE;.JS;.JSE;.WSF;.WSH;.MSC";
    WCHAR *ext_copy = tu_dup(ext_list);
    s.Extensions = tu_alloc((wcslen(ext_copy) + 2) * sizeof(WCHAR *));
    for (WCHAR *e = ext_copy; e;) {
        WCHAR *semi = wcschr(e, L';');
        if (semi)
            *semi = 0;
        if (*e)
            s.Extensions[s.ExtensionCount++] = e;
        e = semi ? semi + 1 : nullptr;
    }

    WCHAR *path = tu_getenv(L"PATH");
    WCHAR cwd[MAX_PATH * 2];
    DWORD cwd_length = GetCurrentDirectoryW(TU_ARRAY(cwd), cwd);
    if (!cwd_length || cwd_length >= TU_ARRAY(cwd))
        cwd[0] = 0;

    int missing = 0;
    for (int i = 0; i < pattern_count; i++) {
        const WCHAR *pattern = patterns[i];
        DWORD before = s.Found;
        if (pattern[0] == L'$') {
            if (root)
                return error_exit(L"ERROR: \"$env:pattern\" cannot be used with /R.\n");
            const WCHAR *colon = wcschr(pattern, L':');
            if (!colon || !colon[1])
                return error_exit(L"ERROR: Missing pattern in \"$env:pattern\".\n");
            if (colon == pattern + 1)
                return error_exit(L"ERROR: Invalid pattern is specified in \"$env:pattern\".\n");
            WCHAR *name = tu_dup_n(pattern + 1, (SIZE_T)(colon - pattern - 1));
            WCHAR *value = tu_getenv(name);
            if (!value) {
                tu_eprint(L"ERROR: Environment variable \"%s\" is not found.\n", name);
                tu_free(name);
                return 2;
            }
            search_list(&s, value, colon + 1);
            tu_free(value);
            tu_free(name);
        } else if (wcsrchr(pattern, L':') && wcsrchr(pattern, L':') > pattern + 1 &&
                   !any_of(wcsrchr(pattern, L':'), L"\\/")) {
            if (root)
                return error_exit(L"ERROR: \"path:pattern\" format cannot be used with /R.\n");
            const WCHAR *colon = wcsrchr(pattern, L':');
            if (!colon[1])
                return error_exit(L"ERROR: Missing pattern in \"path:pattern\".\n");
            WCHAR *dirs = tu_dup_n(pattern, (SIZE_T)(colon - pattern));
            search_list(&s, dirs, colon + 1);
            tu_free(dirs);
        } else if (root) {
            search_tree(&s, root, pattern, 0);
        } else {
            if (cwd[0])
                search_dir(&s, cwd, pattern);
            if (path)
                search_list(&s, path, pattern);
        }
        if (s.Found == before) {
            missing++;
            if (!s.Quiet) {
                if (any_of(pattern, L"*?"))
                    tu_eprint(L"INFO: Could not find files for the given pattern(s).\n");
                else
                    tu_eprint(L"INFO: Could not find \"%s\".\n", pattern);
            }
        }
    }
    return missing ? 1 : 0;
}
