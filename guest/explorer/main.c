/*
 * explorer.exe: the machine has no file manager and no desktop of its own, so a folder is shown by the file manager
 * of the host. it takes what programs give Windows': a path, /select with the item to show in its folder, /root, and
 * /n and /e which change nothing here. a file is opened as the shell opens it, like Windows' does. the machine has
 * no browser either: a web address is opened by the browser of the host.
 */
#include <mime/abi.h>
#include <mime/ntuser.h>
#include <mime/win32/winbase.h>
#include <mime/win32/shellapi.h>
#include <rtl.h>

#define PATH_CHARS 520
#define URL_CHARS 2084

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

/* a web address is longer than a path, so it is read apart: the whole argument, in quotes or not. */
static BOOL web_address(const WCHAR *line, WCHAR *url)
{
    while (*line == L' ' || *line == L'\t')
        line++;
    BOOL quoted = *line == L'"';
    if (quoted)
        line++;
    if (_wcsnicmp(line, L"http://", 7) && _wcsnicmp(line, L"https://", 8))
        return FALSE;
    SIZE_T n = 0;
    for (; *line && (quoted ? *line != L'"' : *line != L' ' && *line != L'\t'); line++) {
        if (n + 1 >= URL_CHARS)
            return FALSE;
        url[n++] = *line;
    }
    url[n] = 0;
    return TRUE;
}

static BOOL scheme_name(const WCHAR *name)
{
    SIZE_T n = 0;
    for (; name[n]; n++)
        if (!((name[n] | 0x20) >= L'a' && (name[n] | 0x20) <= L'z') && !(n && name[n] >= L'0' && name[n] <= L'9') &&
            !(n && (name[n] == L'+' || name[n] == L'-' || name[n] == L'.')))
            return FALSE;
    return n >= 2 && n < RTL_NUMBER_OF(((MimeShellProtocols *)0)->scheme[0]);
}

/* a class is a protocol when it has the URL Protocol value, and is answered when it has a command to open. */
static void add_protocols(MimeShellProtocols *list, HKEY root)
{
    HKEY classes;
    if (RegOpenKeyExW(root, L"Software\\Classes", 0, KEY_READ, &classes))
        return;
    for (DWORD i = 0;; i++) {
        WCHAR name[256], command[320];
        DWORD chars = RTL_NUMBER_OF(name);
        LSTATUS status = RegEnumKeyExW(classes, i, name, &chars, nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS)
            break;
        if (status || !scheme_name(name) || !_wcsicmp(name, L"http") || !_wcsicmp(name, L"https"))
            continue;
        HKEY key;
        if (RegOpenKeyExW(classes, name, 0, KEY_READ, &key))
            continue;
        BOOL protocol = !RegQueryValueExW(key, L"URL Protocol", nullptr, nullptr, nullptr, nullptr);
        RegCloseKey(key);
        _snwprintf(command, RTL_NUMBER_OF(command), L"%ls\\shell\\open\\command", name);
        if (!protocol || RegOpenKeyExW(classes, command, 0, KEY_READ, &key))
            continue;
        RegCloseKey(key);
        BOOL known = FALSE;
        for (DWORD k = 0; k < list->count && !known; k++)
            known = !_wcsicmp((const WCHAR *)list->scheme[k], name);
        if (!known && list->count < MIME_PROTOCOLS) {
            for (SIZE_T c = 0; name[c]; c++)
                list->scheme[list->count][c] = name[c] >= L'A' && name[c] <= L'Z' ? name[c] | 0x20 : name[c];
            list->count++;
        }
    }
    RegCloseKey(classes);
}

/*
 * the page that the browser shows may end by opening an address of a protocol that a program of the machine
 * registered, as a sign in does. the host is told of them first, so that it can bring such an address back.
 */
static void open_web_address(const WCHAR *address)
{
    static MimeShellProtocols protocols;
    static MimeShellUrl url;
    protocols.op = MIME_SHELL_PROTOCOLS;
    add_protocols(&protocols, HKEY_CURRENT_USER);
    add_protocols(&protocols, HKEY_LOCAL_MACHINE);
    NtUserMimeShell(MIME_SHELLOP_COMMAND, 0, (PUCHAR)&protocols, sizeof(protocols));
    url.op = MIME_SHELL_URL;
    memcpy(url.url, address, (wcslen(address) + 1) * sizeof(WCHAR));
    NtUserMimeShell(MIME_SHELLOP_COMMAND, 0, (PUCHAR)&url, sizeof(url));
}

void entry(void)
{
    WCHAR object[PATH_CHARS], full[PATH_CHARS];
    static WCHAR address[URL_CHARS];
    BOOL select;
    if (web_address(skip_program(GetCommandLineW()), address)) {
        open_web_address(address);
        ExitProcess(1);
    }
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
