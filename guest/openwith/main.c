/*
 * openwith.exe: what the mime command asks of the machine.
 *
 *   open [--wait] [--admin] <target> [arguments]
 *                                        opens an app by its id or name, or anything the run box opens: programs,
 *                                        App Paths names, documents, shortcuts, urls; --admin opens it elevated
 *   apps                                 lists the apps, the start menu's shortcuts, as open takes them
 *   installed                            lists the programs installed, as the installed apps settings show them
 *   uninstall <id>                       runs the uninstaller of an installed program, or of the one an app belongs
 *                                        to, and waits for it
 *   features <arguments>                 runs OptionalFeatures.exe with the arguments and waits, its exit code becomes ours
 *
 * a console program opened here shares this console and its exit code becomes ours. a window program is left
 * running unless --wait is given. exit codes: 1 failure, 2 bad command line.
 */
#include <mime/win32/shellapi.h>
#include <mime/win32/shlobj.h>
#include <mime/win32/shobjidl.h>
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

/* runas with --admin, like the run as administrator command. */
static const WCHAR *g_verb = L"open";

/* opens like the run box, the error is left for the caller. */
static BOOL shell_execute(const WCHAR *file, const WCHAR *parameters, HANDLE *process)
{
    SHELLEXECUTEINFOW info = {sizeof(info)};
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC | SEE_MASK_NO_CONSOLE;
    info.lpVerb = g_verb;
    info.lpFile = file;
    info.lpParameters = parameters && parameters[0] ? parameters : nullptr;
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info))
        return FALSE;
    *process = info.hProcess;
    return TRUE;
}

/* waits for console programs, or for any with wait. */
static int finish(HANDLE process, BOOL wait)
{
    if (!process)
        return 0;
    DWORD code = 0;
    if (wait || is_console_process(process)) {
        WaitForSingleObject(process, INFINITE);
        GetExitCodeProcess(process, &code);
    }
    CloseHandle(process);
    return (int)code;
}

static int shell_open(const WCHAR *file, const WCHAR *parameters, BOOL wait)
{
    HANDLE process = nullptr;
    if (shell_execute(file, parameters, &process))
        return finish(process, wait);
    WCHAR text[256];
    err(L"openwith: cannot open '%ls': %ls\n", file, error_text(GetLastError(), text, RTL_NUMBER_OF(text)));
    return EXIT_FAILED;
}

/* ------------------------------------------------------------------ apps */

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

/* the apps are the start menu's shortcuts, by id. */
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

static ENTRY *find_by_id(MENU *menu, const WCHAR *id)
{
    for (UINT i = 0; i < menu->Count; i++)
        if (!_wcsicmp(menu->Entries[i].Id, id))
            return &menu->Entries[i];
    return nullptr;
}

static BOOL has_name(const ENTRY *entry, const WCHAR *name)
{
    WCHAR wanted[MAX_PATH], own[MAX_PATH];
    make_id(name, wanted, RTL_NUMBER_OF(wanted));
    make_id(entry->Name, own, RTL_NUMBER_OF(own));
    return wanted[0] && !_wcsicmp(own, wanted);
}

/* an app by its own name, matches counts the apps that have it. */
static ENTRY *find_by_name(MENU *menu, const WCHAR *name, UINT *matches)
{
    ENTRY *found = nullptr;
    *matches = 0;
    for (UINT i = 0; i < menu->Count; i++) {
        if (has_name(&menu->Entries[i], name)) {
            found = &menu->Entries[i];
            (*matches)++;
        }
    }
    return found;
}

static void list_ids_named(MENU *menu, const WCHAR *name)
{
    for (UINT i = 0; i < menu->Count; i++)
        if (has_name(&menu->Entries[i], name))
            err(L"  %ls\n", menu->Entries[i].Id);
}

static int command_apps(int argc, WCHAR **argv)
{
    if (argc) {
        err(L"usage: openwith apps\n");
        return EXIT_USAGE;
    }
    MENU menu;
    list_menu(&menu);
    if (!menu.Count) {
        out(L"No apps are in the start menu.\n");
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

/*
 * a path or a url goes to the run box. a bare name is an app id first, then what the run box finds under App Paths
 * and on the path, then an app's own name.
 */
static int command_open(int argc, WCHAR **argv)
{
    BOOL wait = FALSE;
    for (; argc && argv[0][0] == L'-' && argv[0][1] == L'-'; argc--, argv++) {
        if (!wcscmp(argv[0], L"--wait"))
            wait = TRUE;
        else if (!wcscmp(argv[0], L"--admin"))
            g_verb = L"runas";
        else
            break;
    }
    if (argc < 1) {
        err(L"usage: openwith open [--wait] [--admin] <target> [arguments]\n");
        return EXIT_USAGE;
    }
    const WCHAR *target = argv[0];
    static WCHAR parameters[8192];
    join_arguments(parameters, RTL_NUMBER_OF(parameters), argc - 1, argv + 1);
    if (wcschr(target, L'\\') || wcschr(target, L':'))
        return shell_open(target, parameters, wait);
    MENU menu;
    list_menu(&menu);
    int result = EXIT_FAILED;
    HANDLE process = nullptr;
    UINT matches = 0;
    ENTRY *entry = find_by_id(&menu, target);
    if (entry) {
        result = shell_open(entry->Path, parameters, wait);
    } else if (shell_execute(target, parameters, &process)) {
        result = finish(process, wait);
    } else if (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) {
        WCHAR text[256];
        err(L"openwith: cannot open '%ls': %ls\n", target, error_text(GetLastError(), text, RTL_NUMBER_OF(text)));
    } else if ((entry = find_by_name(&menu, target, &matches)) && matches == 1) {
        result = shell_open(entry->Path, parameters, wait);
    } else if (matches > 1) {
        err(L"openwith: '%ls' names several apps, use one of their ids:\n", target);
        list_ids_named(&menu, target);
    } else {
        err(L"openwith: no app, program or file is named '%ls', see `mime apps`\n", target);
    }
    HeapFree(GetProcessHeap(), 0, menu.Entries);
    return result;
}

/* ------------------------------------------------------------------ installed programs */

typedef struct PROGRAM {
    WCHAR Id[256];
    WCHAR Name[256];
    WCHAR Version[64];
    WCHAR Publisher[256];
    WCHAR Uninstall[1024];
    WCHAR Location[MAX_PATH];
    WCHAR Icon[MAX_PATH];
    BOOL Msi;
} PROGRAM;

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
static BOOL read_program(HKEY root, const WCHAR *id, PROGRAM *program)
{
    memset(program, 0, sizeof(*program));
    lstrcpynW(program->Id, id, RTL_NUMBER_OF(program->Id));
    if (!query_string(root, L"DisplayName", program->Name, RTL_NUMBER_OF(program->Name)))
        return FALSE;
    if (query_dword(root, L"SystemComponent") == 1)
        return FALSE;
    WCHAR text[256];
    if (query_string(root, L"ParentKeyName", text, RTL_NUMBER_OF(text)))
        return FALSE;
    if (query_string(root, L"ReleaseType", text, RTL_NUMBER_OF(text)) &&
        (!_wcsicmp(text, L"Update") || !_wcsicmp(text, L"Hotfix") || !_wcsicmp(text, L"Security Update")))
        return FALSE;
    query_string(root, L"DisplayVersion", program->Version, RTL_NUMBER_OF(program->Version));
    query_string(root, L"Publisher", program->Publisher, RTL_NUMBER_OF(program->Publisher));
    program->Msi = query_dword(root, L"WindowsInstaller") == 1;
    query_string(root, L"UninstallString", program->Uninstall, RTL_NUMBER_OF(program->Uninstall));
    query_string(root, L"InstallLocation", program->Location, RTL_NUMBER_OF(program->Location));
    query_string(root, L"DisplayIcon", program->Icon, RTL_NUMBER_OF(program->Icon));
    return program->Msi || program->Uninstall[0];
}

/* the machine's 64 and 32-bit registrations, then the user's; an id is listed once, first come. */
static PROGRAM *list_programs(UINT *count)
{
    static const struct {
        HKEY Root;
        REGSAM View;
    } places[] = {{HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY}, {HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY}, {HKEY_CURRENT_USER, 0}};
    PROGRAM *programs = nullptr;
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
                seen = !_wcsicmp(programs[k].Id, id);
            HKEY key;
            if (seen || RegOpenKeyExW(uninstall, id, 0, KEY_READ, &key))
                continue;
            PROGRAM *bigger = grow(programs, *count, &capacity, sizeof(PROGRAM));
            if (bigger) {
                programs = bigger;
                if (read_program(key, id, &programs[*count]))
                    (*count)++;
            }
            RegCloseKey(key);
        }
        RegCloseKey(uninstall);
    }
    /* by name, as the settings sort them. */
    for (UINT i = 1; i < *count; i++) {
        PROGRAM item = programs[i];
        UINT j = i;
        for (; j && _wcsicmp(programs[j - 1].Name, item.Name) > 0; j--)
            programs[j] = programs[j - 1];
        programs[j] = item;
    }
    return programs;
}

static int command_installed(int argc, WCHAR **argv)
{
    if (argc) {
        err(L"usage: openwith installed\n");
        return EXIT_USAGE;
    }
    UINT count;
    PROGRAM *programs = list_programs(&count);
    if (!count) {
        out(L"No programs are installed.\n");
        return 0;
    }
    SIZE_T id = 2, name = 4, version = 7;
    for (UINT i = 0; i < count; i++) {
        id = MAX(id, wcslen(programs[i].Id));
        name = MAX(name, wcslen(programs[i].Name));
        version = MAX(version, wcslen(programs[i].Version));
    }
    column(L"ID", id, FALSE);
    column(L"NAME", name, FALSE);
    column(L"VERSION", version, FALSE);
    column(L"PUBLISHER", 0, TRUE);
    for (UINT i = 0; i < count; i++) {
        column(programs[i].Id, id, FALSE);
        column(programs[i].Name, name, FALSE);
        column(programs[i].Version, version, FALSE);
        column(programs[i].Publisher, 0, TRUE);
    }
    HeapFree(GetProcessHeap(), 0, programs);
    return 0;
}

/* ------------------------------------------------------------------ uninstall */

/* whether path is folder or inside it. */
static BOOL is_within(const WCHAR *path, const WCHAR *folder)
{
    SIZE_T n = wcslen(folder);
    while (n && folder[n - 1] == L'\\')
        n--;
    return n && !_wcsnicmp(path, folder, n) && (!path[n] || path[n] == L'\\');
}

/* a folder one program owns: not a drive's root, not inside windows, not a common folder or one above it. */
static BOOL is_owned(const WCHAR *folder)
{
    static const int common[] = {CSIDL_PROGRAM_FILES, CSIDL_PROGRAM_FILESX86, CSIDL_LOCAL_APPDATA, CSIDL_APPDATA,
                                 CSIDL_COMMON_APPDATA};
    if (wcslen(folder) <= 3 || folder[1] != L':')
        return FALSE;
    WCHAR path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_WINDOWS, nullptr, SHGFP_TYPE_CURRENT, path)) &&
        is_within(folder, path))
        return FALSE;
    for (UINT i = 0; i < RTL_NUMBER_OF(common); i++)
        if (SUCCEEDED(SHGetFolderPathW(nullptr, common[i], nullptr, SHGFP_TYPE_CURRENT, path)) &&
            is_within(path, folder))
            return FALSE;
    return TRUE;
}

/* the folder of a command's program: "C:\App\uninst.exe" /S, C:\App\uninst.exe /S and C:\App\app.exe,0 give C:\App. */
static void program_folder(const WCHAR *command, WCHAR *folder, SIZE_T chars)
{
    const WCHAR *p = command;
    WCHAR end = L',';
    if (*p == L'"') {
        p++;
        end = L'"';
    }
    SIZE_T n = 0;
    for (; *p && *p != end && n + 1 < chars; p++) {
        folder[n++] = *p;
        if (n >= 4 && !_wcsnicmp(folder + n - 4, L".exe", 4))
            break;
    }
    folder[n] = 0;
    WCHAR *slash = wcsrchr(folder, L'\\');
    if (slash)
        *slash = 0;
    else
        folder[0] = 0;
}

/* the length of the most precise folder of a program that holds path, 0 when none does. */
static SIZE_T owner_length(const PROGRAM *program, const WCHAR *path)
{
    WCHAR folders[3][MAX_PATH];
    lstrcpynW(folders[0], program->Location, MAX_PATH);
    program_folder(program->Icon, folders[1], MAX_PATH);
    program_folder(program->Uninstall, folders[2], MAX_PATH);
    SIZE_T best = 0;
    for (UINT i = 0; i < RTL_NUMBER_OF(folders); i++)
        if (is_owned(folders[i]) && is_within(path, folders[i]))
            best = MAX(best, wcslen(folders[i]));
    return best;
}

/* the program a shortcut points into, the error is told here. */
static PROGRAM *find_owner(PROGRAM *programs, UINT count, const ENTRY *entry)
{
    WCHAR target[MAX_PATH] = {0};
    IShellLinkW *link = nullptr;
    IPersistFile *file = nullptr;
    BOOL com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    if (SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&link)) &&
        SUCCEEDED(link->lpVtbl->QueryInterface(link, &IID_IPersistFile, (void **)&file)) &&
        SUCCEEDED(file->lpVtbl->Load(file, entry->Path, STGM_READ)))
        link->lpVtbl->GetPath(link, target, MAX_PATH, nullptr, 0);
    if (file)
        file->lpVtbl->Release(file);
    if (link)
        link->lpVtbl->Release(link);
    if (com)
        CoUninitialize();
    if (!target[0]) {
        err(L"openwith: the app '%ls' does not point to a program, see `mime apps --installed`\n", entry->Id);
        return nullptr;
    }
    PROGRAM *owner = nullptr;
    SIZE_T best = 0;
    UINT ties = 0;
    for (UINT i = 0; i < count; i++) {
        SIZE_T length = owner_length(&programs[i], target);
        if (length && length == best)
            ties++;
        if (length > best) {
            best = length;
            owner = &programs[i];
            ties = 0;
        }
    }
    if (!owner) {
        err(L"openwith: no installed program owns the app '%ls', see `mime apps --installed`\n", entry->Id);
        return nullptr;
    }
    if (ties) {
        err(L"openwith: several installed programs own the app '%ls', use one of their ids:\n", entry->Id);
        for (UINT i = 0; i < count; i++)
            if (owner_length(&programs[i], target) == best)
                err(L"  %ls\n", programs[i].Id);
        return nullptr;
    }
    return owner;
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

static int run_uninstaller(const PROGRAM *program)
{
    /* windows installer products are removed by their product code. */
    static WCHAR command[2048];
    if (program->Msi)
        _snwprintf(command, RTL_NUMBER_OF(command), L"msiexec.exe /x %ls", program->Id);
    else
        lstrcpynW(command, program->Uninstall, RTL_NUMBER_OF(command));
    out(L"Uninstalling %ls...\n", program->Name);
    PROCESS_INFORMATION process;
    if (!start_command(command, &process)) {
        WCHAR text[256];
        err(L"openwith: cannot run the uninstaller '%ls': %ls\n", command,
            error_text(GetLastError(), text, RTL_NUMBER_OF(text)));
        return EXIT_FAILED;
    }
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    return (int)code;
}

/* an installed program by its id, else the program an app's shortcut points into. */
static int command_uninstall(int argc, WCHAR **argv)
{
    if (argc != 1) {
        err(L"usage: openwith uninstall <id>\n");
        return EXIT_USAGE;
    }
    UINT count;
    PROGRAM *programs = list_programs(&count);
    PROGRAM *program = nullptr;
    for (UINT i = 0; i < count && !program; i++)
        if (!_wcsicmp(programs[i].Id, argv[0]))
            program = &programs[i];
    if (!program) {
        MENU menu;
        list_menu(&menu);
        UINT matches = 1;
        ENTRY *entry = find_by_id(&menu, argv[0]);
        if (!entry)
            entry = find_by_name(&menu, argv[0], &matches);
        if (matches > 1) {
            err(L"openwith: '%ls' names several apps, use one of their ids:\n", argv[0]);
            list_ids_named(&menu, argv[0]);
        } else if (!entry) {
            err(L"openwith: no installed program or app has the id '%ls', see `mime apps --installed`\n", argv[0]);
        } else {
            program = find_owner(programs, count, entry);
        }
        HeapFree(GetProcessHeap(), 0, menu.Entries);
    }
    int result = program ? run_uninstaller(program) : EXIT_FAILED;
    HeapFree(GetProcessHeap(), 0, programs);
    return result;
}

/* ------------------------------------------------------------------ features */

#define FEATURES_PROGRAM L"C:\\Windows\\System32\\OptionalFeatures.exe"

/* OptionalFeatures.exe asks for the rights it needs itself, the console is shared, so its words reach the host. */
static int command_features(int argc, WCHAR **argv)
{
    static WCHAR parameters[2048];
    join_arguments(parameters, RTL_NUMBER_OF(parameters), argc, argv);
    return shell_open(FEATURES_PROGRAM, parameters, TRUE);
}

/* ------------------------------------------------------------------ entry */

void entry(void)
{
    int argc = 0;
    WCHAR **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int code = EXIT_USAGE;
    if (!argv || argc < 2)
        err(L"usage: openwith open|apps|installed|uninstall|features ...\n");
    else if (!wcscmp(argv[1], L"open"))
        code = command_open(argc - 2, argv + 2);
    else if (!wcscmp(argv[1], L"apps"))
        code = command_apps(argc - 2, argv + 2);
    else if (!wcscmp(argv[1], L"installed"))
        code = command_installed(argc - 2, argv + 2);
    else if (!wcscmp(argv[1], L"uninstall"))
        code = command_uninstall(argc - 2, argv + 2);
    else if (!wcscmp(argv[1], L"features"))
        code = command_features(argc - 2, argv + 2);
    else
        err(L"openwith: unknown command '%ls'\n", argv[1]);
    ExitProcess((UINT)code);
}
