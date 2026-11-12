/*
 * explorer.exe: the machine has no file manager and no desktop of its own, so a folder is shown by the file manager
 * of the host. it takes what programs give Windows': a path, /select with the item to show in its folder, /root, and
 * /n and /e which change nothing here. a file is opened as the shell opens it, like Windows' does.
 */
#include <mime/abi.h>
#include <mime/ntuser.h>
#include <mime/win32/winbase.h>
#include <mime/win32/shellapi.h>
#include <rtl.h>

#define PATH_CHARS 520

static const WCHAR *skip_program(const WCHAR *line)
{
    if (*line == L'"') {
        for (line++; *line && *line != L'"'; line++) {}
        if (*line)
            line++;
    } else {
        while (*line && *line != L' ' && *line != L'\t')
            line++;
    }
    return line;
}

/* the object of the command line, the last one named, and whether it is to be selected. */
static BOOL parse(const WCHAR *line, WCHAR *object, BOOL *select)
{
    object[0] = 0;
    *select = FALSE;
    for (;;) {
        while (*line == L' ' || *line == L'\t' || *line == L',')
            line++;
        /* a program that quotes the whole argument gives "/select,\"path\"". */
        if (*line == L'\\' && line[1] == L'"')
            line++;
        if (*line == L'"' && line[1] == L'/')
            line++;
        if (!*line)
            break;
        if (*line == L'/') {
            WCHAR name[16];
            SIZE_T n = 0;
            for (line++; *line && *line != L',' && *line != L' ' && *line != L'\t' && *line != L'"'; line++)
                if (n + 1 < RTL_NUMBER_OF(name))
                    name[n++] = *line;
            name[n] = 0;
            if (!_wcsicmp(name, L"select"))
                *select = TRUE;
            continue;
        }
        WCHAR value[PATH_CHARS];
        SIZE_T n = 0;
        if (*line == L'"') {
            for (line++; *line && *line != L'"'; line++)
                if (n + 1 < PATH_CHARS)
                    value[n++] = *line;
            if (*line)
                line++;
            if (n && value[n - 1] == L'\\' && *line == L'"')
                n--;
        } else {
            /* a path without quotes goes to the end of the line, spaces and all. */
            for (; *line; line++)
                if (n + 1 < PATH_CHARS)
                    value[n++] = *line;
            while (n && (value[n - 1] == L' ' || value[n - 1] == L'\t' || value[n - 1] == L'"'))
                n--;
        }
        if (n) {
            memcpy(object, value, n * sizeof(WCHAR));
            object[n] = 0;
        }
    }
    return object[0] != 0;
}

/* the path with the names as the drive has them, which is what the host can find. */
static BOOL real_path(const WCHAR *path, WCHAR *out)
{
    HANDLE h = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return FALSE;
    WCHAR full[PATH_CHARS + 8];
    DWORD n = GetFinalPathNameByHandleW(h, full, RTL_NUMBER_OF(full), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    CloseHandle(h);
    if (!n || n >= RTL_NUMBER_OF(full))
        return FALSE;
    const WCHAR *start = !wcsncmp(full, L"\\\\?\\", 4) ? full + 4 : full;
    if (wcslen(start) >= PATH_CHARS)
        return FALSE;
    wcscpy(out, start);
    return TRUE;
}

static void show(const WCHAR *path, BOOL select)
{
    MimeShellFolder folder = {.op = MIME_SHELL_FOLDER, .select = select ? 1u : 0u};
    WCHAR real[PATH_CHARS];
    if (!real_path(path, real))
        return;
    memcpy(folder.path, real, (wcslen(real) + 1) * sizeof(WCHAR));
    NtUserMimeShell(MIME_SHELLOP_COMMAND, 0, (PUCHAR)&folder, sizeof(folder));
}

void entry(void)
{
    WCHAR object[PATH_CHARS], full[PATH_CHARS];
    BOOL select;
    if (!parse(skip_program(GetCommandLineW()), object, &select)) {
        /* Windows' opens its home, the nearest here is the folder of the user. */
        DWORD n = GetEnvironmentVariableW(L"USERPROFILE", object, PATH_CHARS);
        if (!n || n >= PATH_CHARS)
            wcscpy(object, L"C:\\");
        select = FALSE;
    }
    DWORD n = GetFullPathNameW(object, PATH_CHARS, full, nullptr);
    DWORD attributes = n && n < PATH_CHARS ? GetFileAttributesW(full) : INVALID_FILE_ATTRIBUTES;
    if (attributes != INVALID_FILE_ATTRIBUTES && (select || (attributes & FILE_ATTRIBUTE_DIRECTORY))) {
        show(full, select);
    } else if (attributes != INVALID_FILE_ATTRIBUTES || wcschr(object, L':') > object + 1) {
        /* a file, or an address with a scheme. */
        SHELLEXECUTEINFOW info = {.cbSize = sizeof(info), .fMask = SEE_MASK_FLAG_NO_UI, .lpVerb = L"open",
                                  .lpFile = attributes != INVALID_FILE_ATTRIBUTES ? full : object, .nShow = SW_SHOWNORMAL};
        ShellExecuteExW(&info);
    }
    /* as Windows' does when it hands the folder to the explorer that is running. */
    ExitProcess(1);
}
