/*
 * fsutil.exe: the file system utility. only the hardlink commands are here.
 *
 *   fsutil hardlink create <new file> <existing file>
 *   fsutil hardlink list <file>
 *
 * a list prints every name of the file as a path from the root of its volume, one to a line. the exit code is 0, or 1
 * when the command is wrong or the system refused it.
 */
#include "tool.h"

static const WCHAR kCommands[] =
    L"---- Commands Supported ----\n\n"
    L"hardlink      Hardlink management\n";

static const WCHAR kHardlink[] =
    L"---- HARDLINK Commands Supported ----\n\n"
    L"create        Create a hardlink\n"
    L"list          Enumerate hardlinks on a file\n";

static int error_exit(DWORD error)
{
    TU_STR text = {};
    tu_error_text(error, &text);
    tu_eprint(L"Error:  %s\n", tu_str_text(&text));
    tu_str_free(&text);
    return 1;
}

static int create(void)
{
    if (tu_argc != 5) {
        tu_print(L"Usage : fsutil hardlink create <new filename> <existing filename>\n"
                 L"        Eg : fsutil hardlink create c:\\foo.txt c:\\bar.txt\n");
        return 1;
    }
    if (!CreateHardLinkW(tu_argv[3], tu_argv[4], nullptr))
        return error_exit(GetLastError());
    tu_print(L"Hardlink created for %s <<===>> %s\n", tu_argv[3], tu_argv[4]);
    return 0;
}

static int list(void)
{
    if (tu_argc != 4) {
        tu_print(L"Usage : fsutil hardlink list <filename>\n"
                 L"        Eg : fsutil hardlink list c:\\foo.txt\n");
        return 1;
    }
    DWORD length = MAX_PATH;
    WCHAR *name = tu_alloc(length * sizeof(WCHAR));
    HANDLE find = FindFirstFileNameW(tu_argv[3], 0, &length, name);
    if (find == INVALID_HANDLE_VALUE && GetLastError() == ERROR_MORE_DATA) {
        tu_free(name);
        name = tu_alloc(length * sizeof(WCHAR));
        find = FindFirstFileNameW(tu_argv[3], 0, &length, name);
    }
    if (find == INVALID_HANDLE_VALUE) {
        tu_free(name);
        return error_exit(GetLastError());
    }
    for (;;) {
        tu_print(L"%s\n", name);
        DWORD size = length;
        if (!FindNextFileNameW(find, &size, name)) {
            if (GetLastError() != ERROR_MORE_DATA)
                break;
            tu_free(name);
            length = size;
            name = tu_alloc(length * sizeof(WCHAR));
            if (!FindNextFileNameW(find, &size, name))
                break;
        }
    }
    DWORD error = GetLastError();
    FindClose(find);
    tu_free(name);
    return error == ERROR_HANDLE_EOF ? 0 : error_exit(error);
}

int tool_main(void)
{
    if (tu_argc == 1) {
        tu_puts(kCommands);
        return 0;
    }
    if (!tu_eq(tu_argv[1], L"hardlink")) {
        tu_puts(kCommands);
        return 1;
    }
    if (tu_argc == 2) {
        tu_puts(kHardlink);
        return 0;
    }
    if (tu_eq(tu_argv[2], L"create"))
        return create();
    if (tu_eq(tu_argv[2], L"list"))
        return list();
    tu_puts(kHardlink);
    return 1;
}
