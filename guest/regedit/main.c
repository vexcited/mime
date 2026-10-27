/*
 * regedit.exe: the registry editor, and the program that .reg files are given to.
 *
 *   regedit [/s] file...            merges registry script files, asking first unless /s is given
 *   regedit /e file [key]           writes the registry, or a key of it, as a script of version 5.00
 *   regedit /a file [key]           the same in the ANSI format of Windows 9x and NT 4
 *   regedit                         the editor window
 *
 * the process ends with exit code 0 whatever happened, which is what the original does. see docs/regedit.md.
 */
#include "../lib/regfile/regsys.h"
#include "regedit.h"
#include <mime/win32/winuser.h>
#include <rtl.h>

#define MB_SETFOREGROUND_ 0x00010000

static MimeRegWriter g_writer;
BOOL g_allow_multiple;

/* ------------------------------------------------------------------ messages */

int RegeditBox(HWND window, PCWSTR text, UINT flags)
{
    return MessageBoxW(window, text, REGEDIT_TITLE, flags | MB_SETFOREGROUND_);
}

static int box_format(UINT flags, PCWSTR format, PCWSTR argument)
{
    WCHAR text[1400];
    _snwprintf(text, RTL_NUMBER_OF(text) - 1, format, argument);
    text[RTL_NUMBER_OF(text) - 1] = 0;
    return RegeditBox(nullptr, text, flags);
}

#define MESSAGE_DISABLED L"Registry editing has been disabled by your administrator."
#define MESSAGE_NEEDS_FILE L"This command line option needs a file name, and none was given."
#define MESSAGE_CONFIRM                                                                                              \
    L"Adding information can change or delete values and stop programs from working. Only add information from " \
    L"a source you trust.\n\n%s\n\nDo you want to add it to the registry?"
#define MESSAGE_DONE L"The keys and values in %s were added to the registry."
#define MESSAGE_OPEN L"Cannot import %s: the file could not be opened. There may be a disk or file system error."
#define MESSAGE_READ L"Cannot import %s: the file could not be read. There may be a disk error, or the file may be damaged."
#define MESSAGE_KEY L"Cannot import %s: a key could not be created in the registry."
#define MESSAGE_VALUE                                                                                                \
    L"Cannot import %s: not all of the data could be written to the registry. Some keys may be in use by the system "  \
    L"or by other programs, or you may not have the rights to change them."
#define MESSAGE_HEADER L"Cannot import %s: the file is not a registry file. Only registry files can be imported."
#define MESSAGE_SCRIPT L"Cannot import %s: the file is not a registry script."
#define MESSAGE_VERSION L"Cannot import %s: the file is not meant for this version of Windows."
#define MESSAGE_EXPORT_OPEN L"Cannot export %s: the file could not be created. There may be a disk or file system error."
#define MESSAGE_EXPORT_WRITE L"Cannot export %s: the file could not be written. There may be a disk or file system error."

/* ------------------------------------------------------------------ the policy */

/* DisableRegistryTools: 1 allows only silent work, 2 stops everything. */
static int launch_mode(void)
{
    HKEY key;
    int mode = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", 0,
                      KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        DWORD type, size = 4, value = 0;
        if (RegQueryValueExW(key, L"DisableRegistryTools", nullptr, &type, (BYTE *)&value, &size) == ERROR_SUCCESS &&
            type == REG_DWORD && size == 4 && (value == 1 || value == 2))
            mode = (int)value;
        RegCloseKey(key);
    }
    return mode;
}

/* ------------------------------------------------------------------ import */

enum { RESULT_OPEN = 100 };

static const MimeRegSysOptions g_import_options = {0, KEY_ALL_ACCESS, FALSE};

static void import_file(PCWSTR name, BOOL silent)
{
    if (!silent && box_format(MB_YESNO | MB_ICONEXCLAMATION, MESSAGE_CONFIRM, name) != IDYES)
        return;
    int result = RESULT_OPEN;
    WCHAR path[MAX_PATH];
    PWSTR part;
    DWORD length = SearchPathW(nullptr, name, nullptr, MAX_PATH, path, &part);
    if (length && length < MAX_PATH) {
        HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            result = (int)MimeRegSysImport(file, &g_import_options);
            CloseHandle(file);
        }
    }
    if (silent)
        return;
    switch (result) {
    case MIME_REG_OK:
        box_format(MB_ICONINFORMATION, MESSAGE_DONE, name);
        break;
    case RESULT_OPEN:
        box_format(MB_ICONERROR, MESSAGE_OPEN, name);
        break;
    case MIME_REG_READ_FAILED:
        box_format(MB_ICONERROR, MESSAGE_READ, name);
        break;
    case MIME_REG_BAD_HEADER:
        box_format(MB_ICONERROR, MESSAGE_HEADER, name);
        break;
    case MIME_REG_NOT_A_SCRIPT:
        box_format(MB_ICONERROR, MESSAGE_SCRIPT, name);
        break;
    case MIME_REG_WRONG_VERSION:
        box_format(MB_ICONERROR, MESSAGE_VERSION, name);
        break;
    case MIME_REG_KEY_FAILED:
        box_format(MB_ICONERROR, MESSAGE_KEY, name);
        break;
    default:
        box_format(MB_ICONERROR, MESSAGE_VALUE, name);
        break;
    }
}

/* ------------------------------------------------------------------ export */

/* opens the key a command line names, with the root in capitals. NULL when the name does not lead to a key. */
static HKEY open_named_key(PWSTR path)
{
    PWSTR rest = wcschr(path, L'\\');
    SIZE_T length = rest ? (SIZE_T)(rest - path) : wcslen(path);
    int root = MimeRegSysFindRoot(path, length);
    if (root < 0)
        return nullptr;
    for (SIZE_T i = 0; i < length; i++)
        path[i] = towupper(path[i]);
    HKEY key;
    if (RegOpenKeyExW(MimeRegSysRootKey(root), rest ? rest + 1 : nullptr, 0, KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS,
                      &key) != ERROR_SUCCESS)
        return nullptr;
    return key;
}

static void export_registry(PCWSTR file, PCWSTR key_name, BOOL ansi)
{
    if (!file) {
        RegeditBox(nullptr, MESSAGE_NEEDS_FILE, MB_ICONERROR);
        return;
    }
    HKEY key = nullptr;
    WCHAR path[2200];
    if (key_name) {
        if (wcslen(key_name) >= RTL_NUMBER_OF(path))
            return;
        wcscpy(path, key_name);
        key = open_named_key(path);
        if (!key)
            return;
    }
    HANDLE handle = CreateFileW(file, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        box_format(MB_ICONERROR, MESSAGE_EXPORT_OPEN, file);
        if (key)
            RegCloseKey(key);
        return;
    }
    MimeRegSysInitWriter(&g_writer, handle);
    MimeRegWriterBegin(&g_writer, ansi);
    BOOL ok;
    if (key) {
        ok = MimeRegSysExportKey(&g_writer, key, path, 0);
        RegCloseKey(key);
    } else {
        /* the whole registry is the machine and the users, the other roots show the same keys. */
        ok = MimeRegSysExportKey(&g_writer, HKEY_LOCAL_MACHINE, MimeRegSysRootName(MIME_REG_ROOT_MACHINE), 0) &&
             MimeRegSysExportKey(&g_writer, HKEY_USERS, MimeRegSysRootName(MIME_REG_ROOT_USERS), 0);
    }
    if (!MimeRegWriterFlush(&g_writer))
        ok = FALSE;
    CloseHandle(handle);
    if (!ok)
        box_format(MB_ICONERROR, MESSAGE_EXPORT_WRITE, file);
}

/* ------------------------------------------------------------------ the file type */

static void set_string(HKEY root, PCWSTR subkey, PCWSTR name, PCWSTR value)
{
    HKEY key;
    if (RegCreateKeyExW(root, subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;
    RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)value, (DWORD)((wcslen(value) + 1) * sizeof(WCHAR)));
    RegCloseKey(key);
}

/* what the INF of the original adds and removes: the file type of .reg files. */
static void register_server(BOOL install)
{
    if (!install) {
        MimeRegSysDeleteTree(HKEY_CLASSES_ROOT, L".reg", 0);
        MimeRegSysDeleteTree(HKEY_CLASSES_ROOT, L"regedit", 0);
        MimeRegSysDeleteTree(HKEY_CLASSES_ROOT, L"regfile", 0);
        return;
    }
    HKEY classes = HKEY_CLASSES_ROOT;
    set_string(classes, L".reg", nullptr, L"regfile");
    set_string(classes, L"regedit", nullptr, L"Registration Entries");
    set_string(classes, L"regedit\\shell\\open\\command", nullptr, L"regedit.exe %1");
    set_string(classes, L"regfile", nullptr, L"Registration Entries");
    set_string(classes, L"regfile", L"FriendlyTypeName", L"@regedit.exe,-309");
    set_string(classes, L"regfile\\DefaultIcon", nullptr, L"regedit.exe,1");
    set_string(classes, L"regfile\\shell\\open", nullptr, L"Mer&ge");
    set_string(classes, L"regfile\\shell\\open", L"MUIVerb", L"@regedit.exe,-310");
    set_string(classes, L"regfile\\shell\\open\\command", nullptr, L"regedit.exe \"%1\"");
}

/* ------------------------------------------------------------------ the command line */

/* the words of the arguments: spaces separate, a quote opens a word that runs to the next quote, with no escapes. */
static PWSTR next_word(PWSTR *cursor)
{
    PWSTR p = *cursor;
    while (*p == L' ')
        p++;
    if (!*p) {
        *cursor = p;
        return nullptr;
    }
    WCHAR end = L' ';
    if (*p == L'"') {
        end = L'"';
        p++;
    }
    PWSTR word = p;
    while (*p && *p != end)
        p++;
    if (*p)
        *p++ = 0;
    *cursor = p;
    return word;
}

static BOOL starts_with_nocase(PCWSTR text, PCWSTR word)
{
    return _wcsnicmp(text, word, wcslen(word)) == 0;
}

/*
 * 0 starts the window, 1 and 2 end the program: 1 when files were imported, so a window that is open updates itself.
 * mode is the policy: 1 allows only silent work.
 */
static int parse_command_line(int mode, PWSTR line)
{
    BOOL silent = FALSE, exporting = FALSE, ansi = FALSE;
    PWSTR p = line;
    if (*p == L'"') {
        p++;
        while (*p && *p != L'"')
            p++;
        if (*p)
            p++;
    } else {
        while (*p > L' ')
            p++;
    }
    while (*p && *p <= L' ')
        p++;
    for (;;) {
        while (*p == L' ')
            p++;
        if (*p != L'/' && *p != L'-')
            break;
        p++;
        while (*p && *p != L' ') {
            switch (*p | 0x20) {
            case L'a':
                ansi = TRUE;
                exporting = TRUE;
                break;
            case L'e':
                exporting = TRUE;
                break;
            case L'm':
                g_allow_multiple = TRUE;
                break;
            case L's':
                silent = TRUE;
                break;
            case L'l':
            case L'r':
                if (starts_with_nocase(p, L"regserver"))
                    register_server(TRUE);
                return 2;
            case L'u':
                if (starts_with_nocase(p, L"unregserver")) {
                    register_server(FALSE);
                    return 2;
                }
                break;
            }
            p++;
        }
    }
    if (mode == 1 && (!silent || !*p)) {
        RegeditBox(nullptr, MESSAGE_DISABLED, MB_ICONERROR);
        return 2;
    }
    if (!exporting) {
        if (!*p)
            return 0;
        PWSTR word = next_word(&p);
        if (mode == 1 && !word) {
            RegeditBox(nullptr, MESSAGE_DISABLED, MB_ICONERROR);
            return 2;
        }
        while (word) {
            WCHAR full[MAX_PATH];
            DWORD n = GetLongPathNameW(word, full, MAX_PATH);
            import_file(n && n < MAX_PATH ? full : word, silent);
            word = next_word(&p);
        }
        return 1;
    }
    PWSTR file = next_word(&p);
    PWSTR key = next_word(&p);
    if (mode == 1 && (!file || !key)) {
        RegeditBox(nullptr, MESSAGE_DISABLED, MB_ICONERROR);
        return 2;
    }
    if (next_word(&p))
        return 2;
    export_registry(file, key, ansi);
    return 2;
}

void entry(void)
{
    int mode = launch_mode();
    if (mode == 2) {
        RegeditBox(nullptr, MESSAGE_DISABLED, MB_ICONERROR);
        ExitProcess(0);
    }
    PCWSTR command = GetCommandLineW();
    SIZE_T size = (wcslen(command) + 1) * sizeof(WCHAR);
    PWSTR line = HeapAlloc(GetProcessHeap(), 0, size);
    if (!line)
        ExitProcess(0);
    memcpy(line, command, size);
    int result = parse_command_line(mode, line);
    if (result == 1)
        RegeditRefresh();
    else if (result == 0)
        RegeditRun();
    ExitProcess(0);
}
