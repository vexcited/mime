/*
 * setx.exe: writes an environment variable into the registry, for the user or, with /M, for the machine.
 *
 *   setx [/S system [/U user [/P [password]]]] var value [/M]
 *   setx [...] var /K regpath [/M]
 *   setx [...] var /F file {/A x,y | /R x,y string} [/D delimiters] [/M]
 *
 * the value is a string, or an expandable string when it holds a %name% or is written as ~name~. a value over 1024
 * characters is cut. once the variable is stored every window is told that the environment changed. the exit code is
 * 0 or 1.
 */
#include "tool.h"
#include <mime/win32/winuser.h>

#define MAX_VALUE 1024
#define USER_ENVIRONMENT L"Environment"
#define MACHINE_ENVIRONMENT L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment"
#define RPC_S_SERVER_UNAVAILABLE_ 1722

static const WCHAR kUsage[] =
    L"\nSetX has three ways of working: \n\nSyntax 1:\n"
    L"    SETX [/S system [/U [domain\\]user [/P [password]]]] var value [/M]\n\n"
    L"Syntax 2:\n"
    L"    SETX [/S system [/U [domain\\]user [/P [password]]]] var /K regpath [/M]\n\n"
    L"Syntax 3:\n"
    L"    SETX [/S system [/U [domain\\]user [/P [password]]]]\n"
    L"         /F file {var {/A x,y | /R x,y string}[/M] | /X} [/D delimiters]\n\n"
    L"Description:\n"
    L"    Creates or modifies environment variables in the user or system\n"
    L"    environment. Can set variables based on arguments, regkeys or\n"
    L"    file input.\n\n"
    L"Parameter List:\n"
    L"    /S     system          Specifies the remote system to connect to.\n\n"
    L"    /U     [domain\\]user   Specifies the user context under which\n"
    L"                           the command should execute.\n\n"
    L"    /P     [password]      Specifies the password for the given\n"
    L"                           user context. Prompts for input if omitted.\n\n"
    L"    var                    Specifies the environment variable to set.\n\n"
    L"    value                  Specifies a value to be assigned to the \n"
    L"                           environment variable.\n\n"
    L"    /K     regpath         Specifies that the variable is set based\n"
    L"                           on information from a registry key.\n"
    L"                           Path should be specified in the format of\n"
    L"                           hive\\key\\...\\value. For example,\n"
    L"                           HKEY_LOCAL_MACHINE\\System\\CurrentControlSet\\\n"
    L"                           Control\\TimeZoneInformation\\StandardName.\n\n"
    L"    /F     file            Specifies the filename of the text file\n"
    L"                           to use.\n\n"
    L"    /A     x,y             Specifies absolute file coordinates\n"
    L"                           (line X, item Y) as parameters to search \n"
    L"                           within the file.\n\n"
    L"    /R     x,y string      Specifies relative file coordinates with\n"
    L"                           respect to \"string\" as the search parameters.\n\n"
    L"    /M                     Specifies that the variable should be set in\n"
    L"                           the system wide (HKEY_LOCAL_MACHINE)\n"
    L"                           environment. The default is to set the\n"
    L"                           variable under the HKEY_CURRENT_USER \n"
    L"                           environment.\n\n"
    L"    /D     delimiters      Specifies additional delimiters such as \",\"\n"
    L"                           or \"\\\". The built-in delimiters are space,\n"
    L"                           tab, carriage return, and linefeed. Any \n"
    L"                           ASCII character can be used as an additional\n"
    L"                           delimiter. The maximum number of delimiters,\n"
    L"                           including the built-in delimiters, is 15.\n\n"
    L"    /?                     Displays this help message.\n\n"
    L"NOTE: 1) SETX writes variables to the master environment in the registry.\n\n"
    L"      2) On a local system, variables created or modified by this tool\n"
    L"         will be available in future command windows but not in the\n"
    L"         current CMD.exe command window.\n\n"
    L"      3) On a remote system, variables created or modified by this tool\n"
    L"         will be available at the next logon session.\n\n"
    L"      4) The valid Registry Key data types are REG_DWORD, REG_EXPAND_SZ,\n"
    L"         REG_SZ, REG_MULTI_SZ.\n\n"
    L"      5) Supported hives:  HKEY_LOCAL_MACHINE (HKLM),\n"
    L"         HKEY_CURRENT_USER (HKCU).\n\n"
    L"      6) Delimiters are case sensitive.\n\n"
    L"      7) REG_DWORD values are extracted from the registry in decimal \n"
    L"         format.\n\n"
    L"Examples:\n"
    L"    SETX MACHINE COMPAQ \n"
    L"    SETX MACHINE \"COMPAQ COMPUTER\" /M\n"
    L"    SETX MYPATH \"%PATH%\"\n"
    L"    SETX MYPATH ~PATH~\n"
    L"    SETX TZONE /K HKEY_LOCAL_MACHINE\\System\\CurrentControlSet\\\n"
    L"         Control\\TimeZoneInformation\\StandardName\n"
    L"    SETX IPADDR /F ipconfig.out /A 5,11 \n"
    L"    SETX IPGATEWAY /F ipconfig.out /R 0,7 Gateway\n";

static int fail(const WCHAR *text)
{
    tu_eprint(L"%s", text);
    return 1;
}

static int fail_syntax(void)
{
    return fail(L"ERROR: Invalid syntax.\nType \"SETX /?\" for usage.\n");
}

static BOOL is_switch(const WCHAR *a, const WCHAR *name)
{
    return (a[0] == L'/' || a[0] == L'-') && tu_eq(a + 1, name);
}

static int registry_error(LONG error)
{
    switch (error) {
    case ERROR_FILE_NOT_FOUND:
        return fail(L"ERROR: The system cannot find registry path specified.\n");
    case ERROR_PATH_NOT_FOUND:
        return fail(L"ERROR: The system cannot find the path specified.\n");
    case ERROR_ACCESS_DENIED:
        return fail(L"ERROR: Access to the registry path is denied.\n");
    case ERROR_INVALID_HANDLE:
        return fail(L"ERROR: The handle is invalid.\n");
    case ERROR_NOT_ENOUGH_MEMORY:
        return fail(L"ERROR: Not enough storage is available to process this command.\n");
    default: {
        TU_STR text = {};
        tu_error_text((DWORD)error, &text);
        tu_eprint(L"ERROR: %s\n", tu_str_text(&text));
        tu_str_free(&text);
        return 1;
    }
    }
}

/* the kind of string a value is stored as: two percent signs make it expandable, and ~name~ stands for %name%. */
static DWORD value_type(WCHAR *value)
{
    SIZE_T n = wcslen(value);
    if (n > 2 && value[0] == L'~' && value[n - 1] == L'~') {
        value[0] = value[n - 1] = L'%';
        return REG_EXPAND_SZ;
    }
    int percents = 0;
    for (const WCHAR *p = value; *p; p++)
        if (*p == L'%')
            percents++;
    return percents >= 2 ? REG_EXPAND_SZ : REG_SZ;
}

static int store(const WCHAR *name, const WCHAR *value, DWORD type, BOOL machine)
{
    HKEY key;
    LONG error;
    if (machine) {
        error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, MACHINE_ENVIRONMENT, 0, KEY_WRITE, &key);
    } else {
        error = RegOpenKeyExW(HKEY_CURRENT_USER, USER_ENVIRONMENT, 0, KEY_WRITE, &key);
        if (error == ERROR_FILE_NOT_FOUND)
            error = RegCreateKeyExW(HKEY_CURRENT_USER, USER_ENVIRONMENT, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
    }
    if (error)
        return registry_error(error);
    error = RegSetValueExW(key, name, 0, type, (const BYTE *)value, (DWORD)((wcslen(value) + 1) * sizeof(WCHAR)));
    RegCloseKey(key);
    if (error)
        return registry_error(error);
    return 0;
}

static void announce(void)
{
    DWORD_PTR result = 0;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment", SMTO_ABORTIFHUNG, 5000, &result);
    tu_print(L"\nSUCCESS: Specified value was saved.\n");
}

static BOOL is_local_system(const WCHAR *name)
{
    WCHAR host[MAX_COMPUTERNAME_LENGTH + 2];
    DWORD size = TU_ARRAY(host);
    if (tu_eq(name, L".") || tu_eq(name, L"localhost") || tu_eq(name, L"127.0.0.1") || tu_eq(name, L"::1"))
        return TRUE;
    while (*name == L'\\')
        name++;
    return GetComputerNameW(host, &size) && tu_eq(name, host);
}

/* ------------------------------------------------------------------ the value of a registry path */

static int from_registry(const WCHAR *name, const WCHAR *path, BOOL machine)
{
    WCHAR *copy = tu_dup(path);
    WCHAR *slash = wcschr(copy, L'\\');
    if (!slash)
        return tu_free(copy), fail(L"ERROR: Bad format for supplied key.\n");
    *slash = 0;
    HKEY hive;
    if (tu_eq(copy, L"HKEY_LOCAL_MACHINE") || tu_eq(copy, L"HKLM"))
        hive = HKEY_LOCAL_MACHINE;
    else if (tu_eq(copy, L"HKEY_CURRENT_USER") || tu_eq(copy, L"HKCU"))
        hive = HKEY_CURRENT_USER;
    else {
        tu_free(copy);
        return fail(L"ERROR: Invalid Hive specified. Valid values are HKEY_LOCAL_MACHINE(or HKLM) or HKEY_CURRENT_USER(or HKCU).\n");
    }
    WCHAR *key_path = slash + 1;
    WCHAR *last = wcsrchr(key_path, L'\\');
    WCHAR *value_name;
    if (!last) {
        value_name = key_path;
        key_path = (WCHAR *)L"";
    } else {
        *last = 0;
        value_name = last + 1;
    }
    HKEY key;
    LONG error = RegOpenKeyExW(hive, key_path, 0, KEY_READ, &key);
    if (error) {
        tu_free(copy);
        return fail(L"ERROR: Could not find the key in the registry.\n");
    }
    DWORD type = 0, size = 0;
    error = RegQueryValueExW(key, value_name, nullptr, &type, nullptr, &size);
    if (error) {
        RegCloseKey(key);
        tu_free(copy);
        return fail(L"ERROR: Could not read the key from registry.\n");
    }
    BYTE *data = tu_alloc(size + 4);
    error = RegQueryValueExW(key, value_name, nullptr, &type, data, &size);
    RegCloseKey(key);
    if (error) {
        tu_free(data);
        tu_free(copy);
        return fail(L"ERROR: Could not read the key from registry.\n");
    }
    TU_STR text = {};
    DWORD stored = REG_SZ;
    if (type == REG_SZ || type == REG_EXPAND_SZ) {
        tu_str_puts(&text, (const WCHAR *)data);
        stored = type;
    } else if (type == REG_DWORD && size >= 4) {
        tu_str_format(&text, L"%lu", *(DWORD *)data);
    } else if (type == REG_MULTI_SZ) {
        for (const WCHAR *p = (const WCHAR *)data; *p; p += wcslen(p) + 1) {
            if (text.length)
                tu_str_putc(&text, L';');
            tu_str_puts(&text, p);
        }
    } else {
        tu_free(data);
        tu_free(copy);
        return fail(L"ERROR: Only REG_DWORD, REG_EXPAND_SZ, REG_SZ, REG_MULTI_SZ are supported.\n");
    }
    if (text.length > MAX_VALUE) {
        text.length = MAX_VALUE;
        tu_eprint(L"\nWARNING: The length of the extracted value is truncated to 1024 characters.\n");
    }
    tu_print(L"\nExtracted value: \"%s\".", tu_str_text(&text));
    int status = store(name, tu_str_text(&text), stored, machine);
    tu_str_free(&text);
    tu_free(data);
    tu_free(copy);
    if (!status)
        announce();
    return status;
}

/* ------------------------------------------------------------------ the value of a place in a file */

static BOOL is_delimiter(WCHAR c, const WCHAR *extra)
{
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || (c && wcschr(extra, c));
}

/* the words of a line, cut at the delimiters. */
static int words_of(WCHAR *line, const WCHAR *extra, WCHAR **words, int max)
{
    int n = 0;
    for (WCHAR *p = line; *p && n < max;) {
        while (*p && is_delimiter(*p, extra))
            *p++ = 0;
        if (!*p)
            break;
        words[n++] = p;
        while (*p && !is_delimiter(*p, extra))
            p++;
    }
    return n;
}

static BOOL parse_pair(const WCHAR *text, LONG *x, LONG *y)
{
    const WCHAR *comma = wcschr(text, L',');
    if (!comma || comma == text || !comma[1])
        return FALSE;
    WCHAR *first = tu_dup_n(text, (SIZE_T)(comma - text));
    BOOL ok = tu_parse_int(first, x) && tu_parse_int(comma + 1, y);
    tu_free(first);
    return ok;
}

static int from_file(const WCHAR *name, const WCHAR *file, const WCHAR *absolute, const WCHAR *relative,
                     const WCHAR *search, const WCHAR *delimiters, BOOL machine)
{
    LONG x, y;
    if (!parse_pair(absolute ? absolute : relative, &x, &y))
        return fail(L"ERROR: Must provide valid row,column.\n");
    if (relative && (!search || !*search))
        return fail(L"ERROR: Must provide a valid search string for relative searches.\n");
    HANDLE h = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        tu_eprint(L"ERROR: Cannot open text file \"%s\".\n", file);
        return 1;
    }
    TU_READER reader;
    tu_reader_init(&reader, h);
    WCHAR **lines = nullptr;
    int count = 0, capacity = 0;
    const BYTE *raw;
    SIZE_T length;
    while (tu_reader_line(&reader, &raw, &length)) {
        WCHAR *wide = tu_alloc((length + 1) * sizeof(WCHAR));
        if (length)
            MultiByteToWideChar(GetOEMCP(), 0, (LPCSTR)raw, (int)length, wide, (int)length);
        if (count == capacity) {
            capacity = capacity ? capacity * 2 : 32;
            lines = tu_realloc(lines, capacity * sizeof(WCHAR *));
        }
        lines[count++] = wide;
    }
    tu_reader_free(&reader);
    CloseHandle(h);

    LONG line = 0, item = 0;
    if (absolute) {
        line = x < 0 ? count + x : x;
        item = y;
    } else {
        BOOL found = FALSE;
        for (int i = 0; i < count && !found; i++) {
            WCHAR *copy = tu_dup(lines[i]);
            WCHAR *words[512];
            int n = words_of(copy, delimiters, words, 512);
            for (int k = 0; k < n; k++) {
                if (wcsstr(words[k], search)) {
                    line = i + x;
                    item = k + y;
                    found = TRUE;
                    break;
                }
            }
            tu_free(copy);
        }
        if (!found) {
            tu_eprint(L"ERROR: Coordinates (%ld,%ld) do not exist or specified text not found.\n", x, y);
            return 1;
        }
    }
    WCHAR *result = nullptr;
    if (line >= 0 && line < count) {
        WCHAR *copy = tu_dup(lines[line]);
        WCHAR *words[512];
        int n = words_of(copy, delimiters, words, 512);
        if (item < 0)
            item += n;
        if (item >= 0 && item < n)
            result = tu_dup(words[item]);
        tu_free(copy);
    }
    for (int i = 0; i < count; i++)
        tu_free(lines[i]);
    tu_free(lines);
    if (!result) {
        tu_eprint(L"ERROR: Coordinates (%ld,%ld) do not exist.\n", x, y);
        return 1;
    }
    if (wcslen(result) > MAX_VALUE) {
        result[MAX_VALUE] = 0;
        tu_eprint(L"\nWARNING: The length of the extracted value is truncated to 1024 characters.\n");
    }
    tu_print(L"\nExtracted value: \"%s\".\n", result);
    int status = store(name, result, REG_SZ, machine);
    tu_free(result);
    if (!status)
        announce();
    return status;
}

int tool_main(void)
{
    const WCHAR *system = nullptr, *user = nullptr, *registry = nullptr, *file = nullptr, *absolute = nullptr,
                *relative = nullptr, *search = nullptr, *delimiters = L"";
    BOOL machine = FALSE, have_password = FALSE, dump = FALSE;
    const WCHAR *positional[2] = {};
    int positional_count = 0;
    for (int i = 1; i < tu_argc; i++) {
        const WCHAR *a = tu_argv[i];
        BOOL has_value = i + 1 < tu_argc;
        if (is_switch(a, L"?")) {
            tu_puts(kUsage);
            return 0;
        } else if (is_switch(a, L"M")) {
            machine = TRUE;
        } else if (is_switch(a, L"X")) {
            dump = TRUE;
        } else if (is_switch(a, L"S")) {
            if (!has_value)
                return fail_syntax();
            system = tu_argv[++i];
            if (!*system)
                return fail(L"ERROR: /S cannot be empty.\n");
        } else if (is_switch(a, L"U")) {
            if (!has_value)
                return fail_syntax();
            user = tu_argv[++i];
            if (!*user)
                return fail(L"ERROR: /U cannot be empty.\n");
        } else if (is_switch(a, L"P")) {
            have_password = TRUE;
            if (has_value && tu_argv[i + 1][0] != L'/')
                i++;
        } else if (is_switch(a, L"K")) {
            if (!has_value)
                return fail(L"ERROR: Invalid syntax. Value expected for '/k'.\nType \"SETX /?\" for usage.\n");
            registry = tu_argv[++i];
            if (!*registry)
                return fail(L"/K cannot be empty.\n");
        } else if (is_switch(a, L"F")) {
            if (!has_value)
                return fail_syntax();
            file = tu_argv[++i];
            if (!*file)
                return fail(L"ERROR: /F cannot be empty.\n");
        } else if (is_switch(a, L"A")) {
            if (!has_value)
                return fail_syntax();
            absolute = tu_argv[++i];
        } else if (is_switch(a, L"R")) {
            if (i + 2 >= tu_argc)
                return fail_syntax();
            relative = tu_argv[++i];
            search = tu_argv[++i];
        } else if (is_switch(a, L"D")) {
            if (!has_value)
                return fail_syntax();
            delimiters = tu_argv[++i];
        } else if (positional_count < 2) {
            positional[positional_count++] = a;
        } else {
            return fail_syntax();
        }
    }
    if (user && !system)
        return fail(L"ERROR: Invalid syntax. Username specified without specifying system name.\n");
    if (have_password && !user)
        return fail(L"ERROR: Invalid syntax. Password specified without specifying user name.\n");
    if (system && !is_local_system(system)) {
        TU_STR text = {};
        tu_error_text(RPC_S_SERVER_UNAVAILABLE_, &text);
        tu_eprint(L"ERROR: %s\n", tu_str_text(&text));
        return 1;
    }
    if (dump)
        return fail(L"ERROR: Invalid syntax.\nType \"SETX /?\" for usage.\n");
    if (!positional_count)
        return fail_syntax();
    const WCHAR *name = positional[0];
    if (!*name)
        return fail(L"ERROR: The environmental variable name cannot be empty.\n");
    if (wcschr(name, L'='))
        return fail(L"ERROR: The name of an environment variable cannot include an equal sign (=).\n");
    if (registry) {
        if (positional_count != 1 || file)
            return fail_syntax();
        return from_registry(name, registry, machine);
    }
    if (file) {
        if (positional_count != 1 || (!!absolute == !!relative))
            return fail_syntax();
        return from_file(name, file, absolute, relative, search, delimiters, machine);
    }
    if (positional_count != 2 || absolute || relative)
        return fail_syntax();

    WCHAR *value = tu_alloc((MAX_VALUE + 2) * sizeof(WCHAR));
    SIZE_T n = wcslen(positional[1]);
    if (n > MAX_VALUE) {
        memcpy(value, positional[1], MAX_VALUE * sizeof(WCHAR));
        tu_eprint(L"\nWARNING: The data being saved is truncated to 1024 characters.\n");
    } else {
        memcpy(value, positional[1], n * sizeof(WCHAR));
    }
    DWORD type = value_type(value);
    int status = store(name, value, type, machine);
    tu_free(value);
    if (!status)
        announce();
    return status;
}
