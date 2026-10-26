/*
 * openwith.exe: what the mime command asks of the machine.
 *
 *   open [--wait] <file> [arguments]   opens like the run box: programs, App Paths names, documents, shortcuts, urls
 *   apps                               lists the programs installed, as the installed apps settings show them
 *   uninstall <id>                     runs a program's uninstaller and waits for it
 *   startmenu                          lists the start menu shortcuts of the user
 *   start [--wait] <id> [arguments]    opens a start menu shortcut
 *
 * a console program opened here shares this console and its exit code becomes ours. a window program is left
 * running unless --wait is given. exit codes: 1 failure, 2 bad command line.
 */
#include <mime/win32/shellapi.h>
#include <mime/win32/shlobj.h>
#include <rtl.h>
#include <stdarg.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define EXIT_FAILED 1
#define EXIT_USAGE 2

#define UNINSTALL_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall"

/* ------------------------------------------------------------------ output */

static void write_text(DWORD which, const WCHAR *text, SIZE_T length)
{
    HANDLE handle = GetStdHandle(which);
    DWORD mode, written;
    if (GetConsoleMode(handle, &mode)) {
        WriteConsoleW(handle, text, (DWORD)length, &written, nullptr);
        return;
    }
    int bytes = WideCharToMultiByte(CP_UTF8, 0, text, (int)length, nullptr, 0, nullptr, nullptr);
    char *utf8 = bytes > 0 ? HeapAlloc(GetProcessHeap(), 0, (SIZE_T)bytes) : nullptr;
    if (!utf8)
        return;
    WideCharToMultiByte(CP_UTF8, 0, text, (int)length, utf8, bytes, nullptr, nullptr);
    WriteFile(handle, utf8, (DWORD)bytes, &written, nullptr);
    HeapFree(GetProcessHeap(), 0, utf8);
}

static void print_to(DWORD which, const WCHAR *format, va_list arguments)
{
    WCHAR text[2048];
    int n = _vsnwprintf(text, RTL_NUMBER_OF(text) - 1, format, arguments);
    if (n < 0 || n > (int)RTL_NUMBER_OF(text) - 1)
        n = RTL_NUMBER_OF(text) - 1;
    text[n] = 0;
    write_text(which, text, (SIZE_T)n);
}

static void out(const WCHAR *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    print_to(STD_OUTPUT_HANDLE, format, arguments);
    va_end(arguments);
}

static void err(const WCHAR *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    print_to(STD_ERROR_HANDLE, format, arguments);
    va_end(arguments);
}

/* the system's text for an error, without its line break. */
static const WCHAR *error_text(DWORD error, WCHAR *text, DWORD chars)
{
    DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, text,
                             chars, nullptr);
    while (n && (text[n - 1] == L'\n' || text[n - 1] == L'\r' || text[n - 1] == L' ' || text[n - 1] == L'.'))
        n--;
    if (!n)
        _snwprintf(text, chars, L"error %lu", error);
    else
        text[n] = 0;
    return text;
}

/* pads a column to width characters, the last column is not padded. */
static void column(const WCHAR *text, SIZE_T width, BOOL last)
{
    out(L"%ls", text);
    if (last) {
        out(L"\n");
        return;
    }
    for (SIZE_T n = wcslen(text); n < width + 2; n++)
        out(L" ");
}

/* ------------------------------------------------------------------ memory */

static void *grow(void *items, UINT count, UINT *capacity, SIZE_T size)
{
    if (count < *capacity)
        return items;
    UINT next = *capacity ? *capacity * 2 : 32;
    void *bigger = items ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, items, next * size)
                         : HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, next * size);
    if (bigger)
        *capacity = next;
    return bigger;
}

/* ------------------------------------------------------------------ open */

/* appends an argument quoted so CommandLineToArgvW gives it back unchanged. */
static void append_argument(WCHAR *line, SIZE_T chars, const WCHAR *argument)
{
    SIZE_T n = wcslen(line);
    BOOL quote = !argument[0] || wcschr(argument, L' ') || wcschr(argument, L'\t') || wcschr(argument, L'"');
    if (n && n + 1 < chars)
        line[n++] = L' ';
    if (quote && n + 1 < chars)
        line[n++] = L'"';
    for (const WCHAR *p = argument; *p && n + 3 < chars; p++) {
        SIZE_T slashes = 0;
        while (p[slashes] == L'\\')
            slashes++;
        if (slashes && (p[slashes] == L'"' || (!p[slashes] && quote))) {
            for (SIZE_T i = 0; i < slashes && n + 3 < chars; i++)
                line[n++] = L'\\';
        }
        if (*p == L'"')
            line[n++] = L'\\';
        line[n++] = *p;
    }
    if (quote && n + 1 < chars)
        line[n++] = L'"';
    line[n] = 0;
}

static void join_arguments(WCHAR *line, SIZE_T chars, int argc, WCHAR **argv)
{
    line[0] = 0;
    for (int i = 0; i < argc; i++)
        append_argument(line, chars, argv[i]);
}

/* whether a process runs a console program, read from its image's header. */
static BOOL is_console_process(HANDLE process)
{
    WCHAR path[MAX_PATH];
    DWORD chars = MAX_PATH;
    if (!QueryFullProcessImageNameW(process, 0, path, &chars))
        return FALSE;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    BYTE header[1024];
    DWORD got = 0;
    BOOL ok = ReadFile(file, header, sizeof(header), &got, nullptr);
    CloseHandle(file);
    if (!ok || got < 0x40)
        return FALSE;
    DWORD pe = *(DWORD *)(header + 0x3C);
    /* the subsystem sits at the same offset in pe32 and pe32+ optional headers. */
    if (pe > got - 0x5E || *(DWORD *)(header + pe) != 0x00004550)
        return FALSE;
    return *(WORD *)(header + pe + 0x5C) == IMAGE_SUBSYSTEM_WINDOWS_CUI;
}

/* opens like the run box, then waits for console programs, or for any with wait. */
static int shell_open(const WCHAR *file, const WCHAR *parameters, BOOL wait)
{
    SHELLEXECUTEINFOW info = {sizeof(info)};
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    info.lpVerb = L"open";
    info.lpFile = file;
    info.lpParameters = parameters && parameters[0] ? parameters : nullptr;
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info)) {
        WCHAR text[256];
        err(L"openwith: cannot open '%ls': %ls\n", file, error_text(GetLastError(), text, RTL_NUMBER_OF(text)));
        return EXIT_FAILED;
    }
    if (!info.hProcess)
        return 0;
    DWORD code = 0;
    if (wait || is_console_process(info.hProcess)) {
        WaitForSingleObject(info.hProcess, INFINITE);
        GetExitCodeProcess(info.hProcess, &code);
    }
    CloseHandle(info.hProcess);
    return (int)code;
}

static int command_open(int argc, WCHAR **argv)
{
    BOOL wait = FALSE;
    if (argc && !wcscmp(argv[0], L"--wait")) {
        wait = TRUE;
        argc--;
        argv++;
    }
    if (argc < 1) {
        err(L"usage: openwith open [--wait] <file> [arguments]\n");
        return EXIT_USAGE;
    }
    static WCHAR parameters[8192];
    join_arguments(parameters, RTL_NUMBER_OF(parameters), argc - 1, argv + 1);
    return shell_open(argv[0], parameters, wait);
}

/* ------------------------------------------------------------------ installed apps */

typedef struct APP {
    WCHAR Id[256];
    WCHAR Name[256];
    WCHAR Version[64];
    WCHAR Publisher[256];
    WCHAR Uninstall[1024];
    BOOL Msi;
} APP;

static BOOL query_string(HKEY key, const WCHAR *name, WCHAR *out, DWORD chars)
{
    WCHAR raw[1024];
    DWORD type, bytes = sizeof(raw) - sizeof(WCHAR);
    out[0] = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, (BYTE *)raw, &bytes) ||
        (type != REG_SZ && type != REG_EXPAND_SZ))
        return FALSE;
    raw[bytes / sizeof(WCHAR)] = 0;
    if (type == REG_EXPAND_SZ) {
        DWORD n = ExpandEnvironmentStringsW(raw, out, chars);
        if (!n || n > chars)
            out[0] = 0;
    } else {
        lstrcpynW(out, raw, (int)chars);
    }
    return out[0] != 0;
}

static DWORD query_dword(HKEY key, const WCHAR *name)
{
    DWORD value = 0, type, bytes = sizeof(value);
    if (RegQueryValueExW(key, name, nullptr, &type, (BYTE *)&value, &bytes) || type != REG_DWORD)
        return 0;
    return value;
}

/* what the installed apps settings leave out: components, updates and entries nobody can remove. */
static BOOL read_app(HKEY root, const WCHAR *id, APP *app)
{
    memset(app, 0, sizeof(*app));
    lstrcpynW(app->Id, id, RTL_NUMBER_OF(app->Id));
    if (!query_string(root, L"DisplayName", app->Name, RTL_NUMBER_OF(app->Name)))
        return FALSE;
    if (query_dword(root, L"SystemComponent") == 1)
        return FALSE;
    WCHAR text[256];
    if (query_string(root, L"ParentKeyName", text, RTL_NUMBER_OF(text)))
        return FALSE;
    if (query_string(root, L"ReleaseType", text, RTL_NUMBER_OF(text)) &&
        (!_wcsicmp(text, L"Update") || !_wcsicmp(text, L"Hotfix") || !_wcsicmp(text, L"Security Update")))
        return FALSE;
    query_string(root, L"DisplayVersion", app->Version, RTL_NUMBER_OF(app->Version));
    query_string(root, L"Publisher", app->Publisher, RTL_NUMBER_OF(app->Publisher));
    app->Msi = query_dword(root, L"WindowsInstaller") == 1;
    query_string(root, L"UninstallString", app->Uninstall, RTL_NUMBER_OF(app->Uninstall));
    return app->Msi || app->Uninstall[0];
}

/* the machine's 64 and 32-bit registrations, then the user's; an id is listed once, first come. */
static APP *list_apps(UINT *count)
{
    static const struct {
        HKEY Root;
        REGSAM View;
    } places[] = {{HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY}, {HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY}, {HKEY_CURRENT_USER, 0}};
    APP *apps = nullptr;
    UINT capacity = 0;
    *count = 0;
    for (UINT p = 0; p < RTL_NUMBER_OF(places); p++) {
        HKEY uninstall;
        if (RegOpenKeyExW(places[p].Root, UNINSTALL_KEY, 0, KEY_READ | places[p].View, &uninstall))
            continue;
        WCHAR id[256];
        for (DWORD i = 0;; i++) {
            DWORD chars = RTL_NUMBER_OF(id);
            LSTATUS status = RegEnumKeyExW(uninstall, i, id, &chars, nullptr, nullptr, nullptr, nullptr);
            if (status == ERROR_NO_MORE_ITEMS)
                break;
            if (status)
                continue;
            BOOL seen = FALSE;
            for (UINT k = 0; k < *count && !seen; k++)
                seen = !_wcsicmp(apps[k].Id, id);
            HKEY key;
            if (seen || RegOpenKeyExW(uninstall, id, 0, KEY_READ, &key))
                continue;
            APP *bigger = grow(apps, *count, &capacity, sizeof(APP));
            if (bigger) {
                apps = bigger;
                if (read_app(key, id, &apps[*count]))
                    (*count)++;
            }
            RegCloseKey(key);
        }
        RegCloseKey(uninstall);
    }
    /* by name, as the settings sort them. */
    for (UINT i = 1; i < *count; i++) {
        APP item = apps[i];
        UINT j = i;
        for (; j && _wcsicmp(apps[j - 1].Name, item.Name) > 0; j--)
            apps[j] = apps[j - 1];
        apps[j] = item;
    }
    return apps;
}

static int command_apps(int argc, WCHAR **argv)
{
    if (argc) {
        err(L"usage: openwith apps\n");
        return EXIT_USAGE;
    }
    UINT count;
    APP *apps = list_apps(&count);
    if (!count) {
        out(L"No apps are installed.\n");
        return 0;
    }
    SIZE_T id = 2, name = 4, version = 7;
    for (UINT i = 0; i < count; i++) {
        id = MAX(id, wcslen(apps[i].Id));
        name = MAX(name, wcslen(apps[i].Name));
        version = MAX(version, wcslen(apps[i].Version));
    }
    column(L"ID", id, FALSE);
    column(L"NAME", name, FALSE);
    column(L"VERSION", version, FALSE);
    column(L"PUBLISHER", 0, TRUE);
    for (UINT i = 0; i < count; i++) {
        column(apps[i].Id, id, FALSE);
        column(apps[i].Name, name, FALSE);
        column(apps[i].Version, version, FALSE);
        column(apps[i].Publisher, 0, TRUE);
    }
    HeapFree(GetProcessHeap(), 0, apps);
    return 0;
}

typedef DWORD(WINAPI *LAUNCH_ELEVATED)(LPCWSTR application, LPCWSTR command, LPCWSTR directory, DWORD flags,
                                       const STARTUPINFOW *startup, PROCESS_INFORMATION *info, DWORD *kind);

/* like the settings, an uninstaller that needs administrator rights gets them through the broker. */
static BOOL start_command(WCHAR *command, PROCESS_INFORMATION *process)
{
    STARTUPINFOW startup = {sizeof(startup)};
    if (CreateProcessW(nullptr, command, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, process))
        return TRUE;
    if (GetLastError() != ERROR_ELEVATION_REQUIRED)
        return FALSE;
    HMODULE advapi32 = LoadLibraryW(L"advapi32.dll");
    LAUNCH_ELEVATED launch = advapi32 ? (LAUNCH_ELEVATED)(void *)GetProcAddress(advapi32, "LaunchElevated") : nullptr;
    if (!launch)
        return FALSE;
    DWORD error = launch(nullptr, command, nullptr, 0, &startup, process, nullptr);
    SetLastError(error);
    return !error;
}

static int command_uninstall(int argc, WCHAR **argv)
{
    if (argc != 1) {
        err(L"usage: openwith uninstall <id>\n");
        return EXIT_USAGE;
    }
    UINT count;
    APP *apps = list_apps(&count);
    APP *app = nullptr;
    for (UINT i = 0; i < count && !app; i++)
        if (!_wcsicmp(apps[i].Id, argv[0]))
            app = &apps[i];
    if (!app) {
        err(L"openwith: no installed app has the id '%ls', see `mime apps`\n", argv[0]);
        HeapFree(GetProcessHeap(), 0, apps);
        return EXIT_FAILED;
    }
    /* windows installer products are removed by their product code. */
    static WCHAR command[2048];
    if (app->Msi)
        _snwprintf(command, RTL_NUMBER_OF(command), L"msiexec.exe /x %ls", app->Id);
    else
        lstrcpynW(command, app->Uninstall, RTL_NUMBER_OF(command));
    out(L"Uninstalling %ls...\n", app->Name);
    PROCESS_INFORMATION process;
    if (!start_command(command, &process)) {
        WCHAR text[256];
        err(L"openwith: cannot run the uninstaller '%ls': %ls\n", command,
            error_text(GetLastError(), text, RTL_NUMBER_OF(text)));
        HeapFree(GetProcessHeap(), 0, apps);
        return EXIT_FAILED;
    }
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    HeapFree(GetProcessHeap(), 0, apps);
    return (int)code;
}

/* ------------------------------------------------------------------ start menu */

typedef struct ENTRY {
    WCHAR Id[MAX_PATH];
    WCHAR Name[MAX_PATH];
    WCHAR Relative[MAX_PATH]; /* below Programs, without .lnk. */
    WCHAR Path[MAX_PATH];
} ENTRY;

typedef struct MENU {
    ENTRY *Entries;
    UINT Count, Capacity;
} MENU;

/* lowercase words joined by dashes, folders by slashes: "Discord Inc\Discord" is discord-inc/discord. */
static void make_id(const WCHAR *text, WCHAR *id, SIZE_T chars)
{
    SIZE_T n = 0;
    for (const WCHAR *p = text; *p && n + 1 < chars; p++) {
        WCHAR c = *p;
        if (c == L'\\') {
            while (n && id[n - 1] == L'-')
                n--;
            id[n++] = L'/';
        } else if ((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'.' || c == L'_') {
            id[n++] = c;
        } else if (c >= L'A' && c <= L'Z') {
            id[n++] = (WCHAR)(c - L'A' + L'a');
        } else if (c >= 0x80) {
            id[n++] = towlower(c);
        } else if (n && id[n - 1] != L'-' && id[n - 1] != L'/') {
            id[n++] = L'-';
        }
    }
    while (n && id[n - 1] == L'-')
        n--;
    id[n] = 0;
}

static void scan_folder(MENU *menu, const WCHAR *root, const WCHAR *relative)
{
    WCHAR pattern[MAX_PATH];
    if (_snwprintf(pattern, MAX_PATH, relative[0] ? L"%ls\\%ls\\*" : L"%ls%ls\\*", root, relative) < 0)
        return;
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do {
        if (!wcscmp(data.cFileName, L".") || !wcscmp(data.cFileName, L".."))
            continue;
        WCHAR child[MAX_PATH];
        if (_snwprintf(child, MAX_PATH, relative[0] ? L"%ls\\%ls" : L"%ls%ls", relative, data.cFileName) < 0)
            continue;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            scan_folder(menu, root, child);
            continue;
        }
        SIZE_T length = wcslen(child);
        if (length < 5 || _wcsicmp(child + length - 4, L".lnk"))
            continue;
        child[length - 4] = 0;
        /* the start menu merges the user's folder with the machine's, the user's shortcut wins. */
        BOOL seen = FALSE;
        for (UINT i = 0; i < menu->Count && !seen; i++)
            seen = !_wcsicmp(menu->Entries[i].Relative, child);
        if (seen)
            continue;
        ENTRY *bigger = grow(menu->Entries, menu->Count, &menu->Capacity, sizeof(ENTRY));
        if (!bigger)
            break;
        menu->Entries = bigger;
        ENTRY *entry = &menu->Entries[menu->Count++];
        wcscpy(entry->Relative, child);
        const WCHAR *slash = wcsrchr(child, L'\\');
        wcscpy(entry->Name, slash ? slash + 1 : child);
        make_id(child, entry->Id, RTL_NUMBER_OF(entry->Id));
        _snwprintf(entry->Path, MAX_PATH, L"%ls\\%ls.lnk", root, child);
        entry->Path[MAX_PATH - 1] = 0;
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

static void list_menu(MENU *menu)
{
    static const int folders[] = {CSIDL_PROGRAMS, CSIDL_COMMON_PROGRAMS};
    memset(menu, 0, sizeof(*menu));
    for (UINT i = 0; i < RTL_NUMBER_OF(folders); i++) {
        WCHAR root[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(nullptr, folders[i], nullptr, SHGFP_TYPE_CURRENT, root)))
            scan_folder(menu, root, L"");
    }
    for (UINT i = 1; i < menu->Count; i++) {
        ENTRY item = menu->Entries[i];
        UINT j = i;
        for (; j && _wcsicmp(menu->Entries[j - 1].Id, item.Id) > 0; j--)
            menu->Entries[j] = menu->Entries[j - 1];
        menu->Entries[j] = item;
    }
}

static int command_startmenu(int argc, WCHAR **argv)
{
    if (argc) {
        err(L"usage: openwith startmenu\n");
        return EXIT_USAGE;
    }
    MENU menu;
    list_menu(&menu);
    if (!menu.Count) {
        out(L"The start menu is empty.\n");
        return 0;
    }
    SIZE_T id = 2;
    for (UINT i = 0; i < menu.Count; i++)
        id = MAX(id, wcslen(menu.Entries[i].Id));
    column(L"ID", id, FALSE);
    column(L"NAME", 0, TRUE);
    for (UINT i = 0; i < menu.Count; i++) {
        column(menu.Entries[i].Id, id, FALSE);
        column(menu.Entries[i].Name, 0, TRUE);
    }
    HeapFree(GetProcessHeap(), 0, menu.Entries);
    return 0;
}

/* an entry by its id, else by its own name when only one has it. */
static int command_start(int argc, WCHAR **argv)
{
    BOOL wait = FALSE;
    if (argc && !wcscmp(argv[0], L"--wait")) {
        wait = TRUE;
        argc--;
        argv++;
    }
    if (argc < 1) {
        err(L"usage: openwith start [--wait] <id> [arguments]\n");
        return EXIT_USAGE;
    }
    MENU menu;
    list_menu(&menu);
    ENTRY *found = nullptr;
    for (UINT i = 0; i < menu.Count && !found; i++)
        if (!_wcsicmp(menu.Entries[i].Id, argv[0]))
            found = &menu.Entries[i];
    UINT matches = 0;
    if (!found) {
        WCHAR wanted[MAX_PATH];
        make_id(argv[0], wanted, RTL_NUMBER_OF(wanted));
        for (UINT i = 0; i < menu.Count; i++) {
            WCHAR name[MAX_PATH];
            make_id(menu.Entries[i].Name, name, RTL_NUMBER_OF(name));
            if (!_wcsicmp(name, wanted)) {
                found = &menu.Entries[i];
                matches++;
            }
        }
    }
    int result;
    if (matches > 1) {
        err(L"openwith: '%ls' names several start menu entries, use one of their ids:\n", argv[0]);
        WCHAR wanted[MAX_PATH];
        make_id(argv[0], wanted, RTL_NUMBER_OF(wanted));
        for (UINT i = 0; i < menu.Count; i++) {
            WCHAR name[MAX_PATH];
            make_id(menu.Entries[i].Name, name, RTL_NUMBER_OF(name));
            if (!_wcsicmp(name, wanted))
                err(L"  %ls\n", menu.Entries[i].Id);
        }
        result = EXIT_FAILED;
    } else if (!found) {
        err(L"openwith: no start menu entry has the id '%ls', see `mime start menu`\n", argv[0]);
        result = EXIT_FAILED;
    } else {
        static WCHAR parameters[8192];
        join_arguments(parameters, RTL_NUMBER_OF(parameters), argc - 1, argv + 1);
        result = shell_open(found->Path, parameters, wait);
    }
    HeapFree(GetProcessHeap(), 0, menu.Entries);
    return result;
}

/* ------------------------------------------------------------------ entry */

void entry(void)
{
    int argc = 0;
    WCHAR **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int code = EXIT_USAGE;
    if (!argv || argc < 2)
        err(L"usage: openwith open|apps|uninstall|startmenu|start ...\n");
    else if (!wcscmp(argv[1], L"open"))
        code = command_open(argc - 2, argv + 2);
    else if (!wcscmp(argv[1], L"apps"))
        code = command_apps(argc - 2, argv + 2);
    else if (!wcscmp(argv[1], L"uninstall"))
        code = command_uninstall(argc - 2, argv + 2);
    else if (!wcscmp(argv[1], L"startmenu"))
        code = command_startmenu(argc - 2, argv + 2);
    else if (!wcscmp(argv[1], L"start"))
        code = command_start(argc - 2, argv + 2);
    else
        err(L"openwith: unknown command '%ls'\n", argv[1]);
    ExitProcess((UINT)code);
}
