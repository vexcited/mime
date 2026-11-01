/*
 * odbccp32.dll: the installer of ODBC drivers and data sources. what is installed lives in the registry as it does on
 * Windows: the drivers under ODBCINST.INI, the data sources under ODBC.INI of the user and of the machine. a driver
 * is configured by the setup library that it names, and the files of the driver are not copied here.
 */
#include <mime/win32/winbase.h>
#include <rtl.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

#define SQL_SUCCESS 0
#define SQL_ERROR (-1)
#define SQL_NO_DATA 100

#define ODBC_INSTALL_INQUIRY 1
#define ODBC_INSTALL_COMPLETE 2
#define ODBC_ADD_DSN 1
#define ODBC_CONFIG_DSN 2
#define ODBC_REMOVE_DSN 3
#define ODBC_ADD_SYS_DSN 4
#define ODBC_CONFIG_SYS_DSN 5
#define ODBC_REMOVE_SYS_DSN 6
#define ODBC_REMOVE_DEFAULT_DSN 7

#define ODBC_BOTH_DSN 0
#define ODBC_USER_DSN 1
#define ODBC_SYSTEM_DSN 2

/* the numbers of the errors a call leaves for SQLInstallerError. */
#define ODBC_ERROR_GENERAL_ERR 1
#define ODBC_ERROR_INVALID_BUFF_LEN 2
#define ODBC_ERROR_INVALID_REQUEST_TYPE 5
#define ODBC_ERROR_COMPONENT_NOT_FOUND 6
#define ODBC_ERROR_INVALID_NAME 7
#define ODBC_ERROR_INVALID_KEYWORD_VALUE 8
#define ODBC_ERROR_INVALID_DSN 9
#define ODBC_ERROR_INVALID_INF 10
#define ODBC_ERROR_REQUEST_FAILED 11
#define ODBC_ERROR_INVALID_PATH 12
#define ODBC_ERROR_LOAD_LIB_FAILED 15
#define ODBC_ERROR_INVALID_PARAM_SEQUENCE 16
#define ODBC_ERROR_INVALID_HWND 5

static const char *error_text(int code)
{
    switch (code) {
    case ODBC_ERROR_GENERAL_ERR:
        return "General installer error";
    case ODBC_ERROR_INVALID_BUFF_LEN:
        return "Invalid buffer length";
    case ODBC_ERROR_INVALID_REQUEST_TYPE:
        return "Invalid request type";
    case ODBC_ERROR_COMPONENT_NOT_FOUND:
        return "Component not found in registry";
    case ODBC_ERROR_INVALID_NAME:
        return "Invalid name";
    case ODBC_ERROR_INVALID_KEYWORD_VALUE:
        return "Invalid keyword-value pairs";
    case ODBC_ERROR_INVALID_DSN:
        return "Invalid DSN";
    case ODBC_ERROR_INVALID_INF:
        return "Invalid INF";
    case ODBC_ERROR_REQUEST_FAILED:
        return "Request failed";
    case ODBC_ERROR_INVALID_PATH:
        return "Invalid install path";
    case ODBC_ERROR_LOAD_LIB_FAILED:
        return "Could not load the driver or translator setup library";
    case ODBC_ERROR_INVALID_PARAM_SEQUENCE:
        return "Invalid parameter sequence";
    }
    return "Unknown error";
}

/* ------------------------------------------------------------------ the errors of the calls, kept for the thread */

#define MAX_ERRORS 8

static DWORD g_error_slot = TLS_OUT_OF_INDEXES;
static DWORD g_config_mode = ODBC_BOTH_DSN;

typedef struct {
    int Count, Read;
    int Codes[MAX_ERRORS];
    char Text[MAX_ERRORS][128];
} ERROR_LIST;

static ERROR_LIST *error_list(void)
{
    if (g_error_slot == TLS_OUT_OF_INDEXES) {
        DWORD slot = TlsAlloc();
        if (InterlockedCompareExchange((LONG *)&g_error_slot, (LONG)slot, (LONG)TLS_OUT_OF_INDEXES) != (LONG)TLS_OUT_OF_INDEXES)
            TlsFree(slot);
    }
    ERROR_LIST *list = TlsGetValue(g_error_slot);
    if (!list) {
        list = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*list));
        TlsSetValue(g_error_slot, list);
    }
    return list;
}

/* a new call forgets the errors of the last. */
static void clear_errors(void)
{
    ERROR_LIST *list = error_list();
    if (list)
        list->Count = list->Read = 0;
}

static BOOL fail(int code)
{
    ERROR_LIST *list = error_list();
    if (list && list->Count < MAX_ERRORS) {
        list->Text[list->Count][0] = 0;
        list->Codes[list->Count++] = code;
        list->Read = 0;
    }
    return FALSE;
}

short WINAPI SQLInstallerError(WORD index, DWORD *code, char *message, WORD size, WORD *written)
{
    ERROR_LIST *list = error_list();
    if (!list || index < 1 || index > list->Count)
        return SQL_NO_DATA;
    int number = list->Codes[index - 1];
    if (code)
        *code = number;
    const char *text = list->Text[index - 1][0] ? list->Text[index - 1] : error_text(number);
    WORD length = (WORD)lstrlenA(text);
    if (written)
        *written = length;
    if (message && size) {
        lstrcpynA(message, text, size);
        return length >= size ? 1 : SQL_SUCCESS;
    }
    return SQL_SUCCESS;
}

BOOL WINAPI SQLSetConfigMode(WORD mode)
{
    clear_errors();
    if (mode > ODBC_SYSTEM_DSN)
        return fail(ODBC_ERROR_INVALID_PARAM_SEQUENCE);
    g_config_mode = mode;
    return TRUE;
}

BOOL WINAPI SQLGetConfigMode(WORD *mode)
{
    clear_errors();
    if (!mode)
        return fail(ODBC_ERROR_INVALID_PARAM_SEQUENCE);
    *mode = (WORD)g_config_mode;
    return TRUE;
}

/* ------------------------------------------------------------------ registry */

#define INST_KEY "Software\\ODBC\\ODBCINST.INI"
#define DSN_KEY "Software\\ODBC\\ODBC.INI"

/* the machine holds drivers and the system data sources; the user holds data sources of their own. */
static HKEY root_for_dsn(void)
{
    return g_config_mode == ODBC_SYSTEM_DSN ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
}

static BOOL value_text(HKEY key, const char *name, char *out, DWORD size)
{
    DWORD type, length = size - 1;
    if (RegQueryValueExA(key, name, nullptr, &type, (BYTE *)out, &length) || type != REG_SZ)
        return FALSE;
    out[length < size ? length : size - 1] = 0;
    return TRUE;
}

static BOOL set_text(HKEY key, const char *name, const char *text)
{
    return RegSetValueExA(key, name, 0, REG_SZ, (const BYTE *)text, lstrlenA(text) + 1) == ERROR_SUCCESS;
}

/* the sections of the files of the installer are keys of the registry, and any other file is a plain ini file. */
static BOOL registry_file(const char *file, HKEY *root, const char **path)
{
    if (!file)
        return FALSE;
    if (!lstrcmpiA(file, "ODBCINST.INI")) {
        *root = HKEY_LOCAL_MACHINE;
        *path = INST_KEY;
        return TRUE;
    }
    if (!lstrcmpiA(file, "ODBC.INI")) {
        *root = root_for_dsn();
        *path = DSN_KEY;
        return TRUE;
    }
    return FALSE;
}

BOOL WINAPI SQLWritePrivateProfileString(const char *section, const char *key, const char *value, const char *file)
{
    clear_errors();
    if (!section || !file)
        return fail(ODBC_ERROR_INVALID_PARAM_SEQUENCE);
    HKEY root;
    const char *base;
    if (!registry_file(file, &root, &base))
        return WritePrivateProfileStringA(section, key, value, file) ? TRUE : fail(ODBC_ERROR_REQUEST_FAILED);
    char path[512];
    _snprintf(path, sizeof(path), "%s\\%s", base, section);
    if (!key) {
        LSTATUS status = RegDeleteTreeA(root, path);
        return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND ? TRUE : fail(ODBC_ERROR_REQUEST_FAILED);
    }
    HKEY handle;
    if (RegCreateKeyExA(root, path, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &handle, nullptr))
        return fail(ODBC_ERROR_REQUEST_FAILED);
    BOOL ok;
    if (value)
        ok = set_text(handle, key, value);
    else
        ok = RegDeleteValueA(handle, key) == ERROR_SUCCESS || GetLastError() == ERROR_FILE_NOT_FOUND;
    RegCloseKey(handle);
    return ok ? TRUE : fail(ODBC_ERROR_REQUEST_FAILED);
}

int WINAPI SQLGetPrivateProfileString(const char *section, const char *key, const char *default_value, char *buffer,
                                      int size, const char *file)
{
    clear_errors();
    if (!buffer || size <= 0)
        return -1;
    buffer[0] = 0;
    HKEY root;
    const char *base;
    if (!registry_file(file, &root, &base))
        return (int)GetPrivateProfileStringA(section, key, default_value, buffer, size, file);
    char path[512];
    _snprintf(path, sizeof(path), "%s\\%s", base, section ? section : "");
    HKEY handle;
    int length = 0;
    /* a user data source is looked for first in the user's keys and then in the machine's. */
    HKEY roots[2] = {root, g_config_mode == ODBC_BOTH_DSN && root == HKEY_CURRENT_USER ? HKEY_LOCAL_MACHINE : nullptr};
    for (int i = 0; i < 2 && !length; i++) {
        if (!roots[i] || RegOpenKeyExA(roots[i], path, 0, KEY_READ, &handle))
            continue;
        if (key) {
            if (value_text(handle, key, buffer, size))
                length = lstrlenA(buffer);
        } else {
            /* with no key, the names of the section follow one another, each ended and the list ended again. */
            int at = 0;
            for (DWORD n = 0;; n++) {
                char name[256];
                DWORD name_length = sizeof(name);
                if (RegEnumValueA(handle, n, name, &name_length, nullptr, nullptr, nullptr, nullptr))
                    break;
                if (at + (int)name_length + 2 > size)
                    break;
                memcpy(buffer + at, name, name_length + 1);
                at += name_length + 1;
            }
            if (at) {
                buffer[at] = 0;
                length = at;
            }
        }
        RegCloseKey(handle);
    }
    if (!length && default_value && key) {
        lstrcpynA(buffer, default_value, size);
        length = lstrlenA(buffer);
    }
    return length;
}

/* ------------------------------------------------------------------ drivers */

static void system_directory(char *out, DWORD size)
{
    if (!GetSystemDirectoryA(out, size))
        lstrcpynA(out, "C:\\Windows\\System32", size);
}

static BOOL has_path(const char *file)
{
    for (; *file; file++)
        if (*file == '\\' || *file == '/' || *file == ':')
            return TRUE;
    return FALSE;
}

static BOOL give_path(const char *path, char *out, WORD size, WORD *written)
{
    WORD length = (WORD)lstrlenA(path);
    if (written)
        *written = length;
    if (out && size > length) {
        memcpy(out, path, length + 1);
        return TRUE;
    }
    return FALSE;
}

BOOL WINAPI SQLInstallDriverEx(const char *driver, const char *path_in, char *path_out, WORD path_out_size,
                               WORD *path_out_length, WORD request, DWORD *usage)
{
    clear_errors();
    if (!driver || !*driver)
        return fail(ODBC_ERROR_INVALID_NAME);
    if (request != ODBC_INSTALL_INQUIRY && request != ODBC_INSTALL_COMPLETE)
        return fail(ODBC_ERROR_INVALID_REQUEST_TYPE);
    char directory[MAX_PATH];
    if (path_in && *path_in)
        lstrcpynA(directory, path_in, sizeof(directory));
    else
        system_directory(directory, sizeof(directory));
    if (!give_path(directory, path_out, path_out_size, path_out_length) && path_out)
        return fail(ODBC_ERROR_INVALID_BUFF_LEN);
    if (request == ODBC_INSTALL_INQUIRY)
        return TRUE;

    /* the entries after the name are keyword=value pairs, with the library paths made whole. */
    char path[512];
    _snprintf(path, sizeof(path), INST_KEY "\\%s", driver);
    HKEY key;
    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, path, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, nullptr))
        return fail(ODBC_ERROR_REQUEST_FAILED);
    for (const char *entry = driver + lstrlenA(driver) + 1; *entry; entry += lstrlenA(entry) + 1) {
        const char *equals = entry;
        while (*equals && *equals != '=')
            equals++;
        if (*equals != '=') {
            RegCloseKey(key);
            return fail(ODBC_ERROR_INVALID_KEYWORD_VALUE);
        }
        char name[128], value[512];
        lstrcpynA(name, entry, (int)(equals - entry + 1));
        const char *text = equals + 1;
        if ((!lstrcmpiA(name, "Driver") || !lstrcmpiA(name, "Setup")) && !has_path(text))
            _snprintf(value, sizeof(value), "%s\\%s", directory, text);
        else
            lstrcpynA(value, text, sizeof(value));
        set_text(key, name, value);
    }
    DWORD count = 0, size = sizeof(count), type;
    if (RegQueryValueExA(key, "UsageCount", nullptr, &type, (BYTE *)&count, &size) || type != REG_DWORD)
        count = 0;
    count++;
    RegSetValueExA(key, "UsageCount", 0, REG_DWORD, (const BYTE *)&count, sizeof(count));
    RegCloseKey(key);
    if (usage)
        *usage = count;

    HKEY drivers;
    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, INST_KEY "\\ODBC Drivers", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &drivers, nullptr))
        return fail(ODBC_ERROR_REQUEST_FAILED);
    set_text(drivers, driver, "Installed");
    RegCloseKey(drivers);
    return TRUE;
}

BOOL WINAPI SQLInstallDriverManager(char *path, WORD size, WORD *length)
{
    clear_errors();
    char directory[MAX_PATH];
    system_directory(directory, sizeof(directory));
    if (!give_path(directory, path, size, length) && path)
        return fail(ODBC_ERROR_INVALID_BUFF_LEN);
    return TRUE;
}

BOOL WINAPI SQLRemoveDriverManager(DWORD *usage)
{
    clear_errors();
    if (usage)
        *usage = 1;
    return TRUE;
}

BOOL WINAPI SQLRemoveDriver(const char *driver, BOOL remove_dsns, DWORD *usage)
{
    clear_errors();
    if (!driver || !*driver)
        return fail(ODBC_ERROR_INVALID_NAME);
    char path[512];
    _snprintf(path, sizeof(path), INST_KEY "\\%s", driver);
    HKEY key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_ALL_ACCESS, &key))
        return fail(ODBC_ERROR_COMPONENT_NOT_FOUND);
    DWORD count = 1, size = sizeof(count), type;
    if (RegQueryValueExA(key, "UsageCount", nullptr, &type, (BYTE *)&count, &size) || type != REG_DWORD)
        count = 1;
    count = count ? count - 1 : 0;
    if (usage)
        *usage = count;
    if (count) {
        RegSetValueExA(key, "UsageCount", 0, REG_DWORD, (const BYTE *)&count, sizeof(count));
        RegCloseKey(key);
        return TRUE;
    }
    RegCloseKey(key);
    RegDeleteTreeA(HKEY_LOCAL_MACHINE, path);
    HKEY drivers;
    if (!RegOpenKeyExA(HKEY_LOCAL_MACHINE, INST_KEY "\\ODBC Drivers", 0, KEY_SET_VALUE, &drivers)) {
        RegDeleteValueA(drivers, driver);
        RegCloseKey(drivers);
    }
    return TRUE;
}

/* the names of the installed drivers, each ended and the list ended again. */
BOOL WINAPI SQLGetInstalledDrivers(char *buffer, WORD size, WORD *length)
{
    clear_errors();
    if (!buffer || size < 2)
        return fail(ODBC_ERROR_INVALID_BUFF_LEN);
    int at = 0;
    HKEY key;
    if (!RegOpenKeyExA(HKEY_LOCAL_MACHINE, INST_KEY "\\ODBC Drivers", 0, KEY_READ, &key)) {
        for (DWORD n = 0;; n++) {
            char name[256];
            DWORD name_length = sizeof(name);
            if (RegEnumValueA(key, n, name, &name_length, nullptr, nullptr, nullptr, nullptr))
                break;
            if (at + (int)name_length + 2 > size)
                break;
            memcpy(buffer + at, name, name_length + 1);
            at += name_length + 1;
        }
        RegCloseKey(key);
    }
    if (!at)
        buffer[at++] = 0;
    buffer[at] = 0;
    if (length)
        *length = (WORD)(at + 1);
    return TRUE;
}

/* ------------------------------------------------------------------ data sources */

BOOL WINAPI SQLValidDSN(const char *dsn)
{
    clear_errors();
    if (!dsn || !*dsn || lstrlenA(dsn) > 32)
        return FALSE;
    for (const char *c = dsn; *c; c++)
        for (const char *bad = "[]{}(),;?*=!@\\"; *bad; bad++)
            if (*c == *bad)
                return FALSE;
    return TRUE;
}

BOOL WINAPI SQLWriteDSNToIni(const char *dsn, const char *driver)
{
    clear_errors();
    if (!SQLValidDSN(dsn))
        return fail(ODBC_ERROR_INVALID_DSN);
    if (!driver || !*driver)
        return fail(ODBC_ERROR_INVALID_NAME);
    char path[512], library[MAX_PATH];
    _snprintf(path, sizeof(path), INST_KEY "\\%s", driver);
    HKEY key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &key))
        return fail(ODBC_ERROR_COMPONENT_NOT_FOUND);
    BOOL found = value_text(key, "Driver", library, sizeof(library));
    RegCloseKey(key);
    if (!found)
        return fail(ODBC_ERROR_COMPONENT_NOT_FOUND);

    HKEY root = root_for_dsn();
    HKEY sources;
    if (RegCreateKeyExA(root, DSN_KEY "\\ODBC Data Sources", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &sources, nullptr))
        return fail(ODBC_ERROR_REQUEST_FAILED);
    set_text(sources, dsn, driver);
    RegCloseKey(sources);
    _snprintf(path, sizeof(path), DSN_KEY "\\%s", dsn);
    if (RegCreateKeyExA(root, path, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr))
        return fail(ODBC_ERROR_REQUEST_FAILED);
    set_text(key, "Driver", library);
    RegCloseKey(key);
    return TRUE;
}

BOOL WINAPI SQLRemoveDSNFromIni(const char *dsn)
{
    clear_errors();
    if (!dsn || !*dsn)
        return fail(ODBC_ERROR_INVALID_DSN);
    HKEY root = root_for_dsn();
    HKEY sources;
    if (!RegOpenKeyExA(root, DSN_KEY "\\ODBC Data Sources", 0, KEY_SET_VALUE, &sources)) {
        RegDeleteValueA(sources, dsn);
        RegCloseKey(sources);
    }
    char path[512];
    _snprintf(path, sizeof(path), DSN_KEY "\\%s", dsn);
    LSTATUS status = RegDeleteTreeA(root, path);
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND ? TRUE : fail(ODBC_ERROR_REQUEST_FAILED);
}

typedef BOOL(WINAPI *CONFIG_DSN)(HWND, WORD, const char *, const char *);

/* a data source is added, changed or removed by the setup library of its driver. */
BOOL WINAPI SQLConfigDataSource(HWND parent, WORD request, const char *driver, const char *attributes)
{
    clear_errors();
    if (request < ODBC_ADD_DSN || request > ODBC_REMOVE_DEFAULT_DSN)
        return fail(ODBC_ERROR_INVALID_REQUEST_TYPE);
    if (!driver || !*driver)
        return fail(ODBC_ERROR_INVALID_NAME);
    char path[512], setup[MAX_PATH];
    _snprintf(path, sizeof(path), INST_KEY "\\%s", driver);
    HKEY key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &key))
        return fail(ODBC_ERROR_COMPONENT_NOT_FOUND);
    BOOL found = value_text(key, "Setup", setup, sizeof(setup));
    RegCloseKey(key);
    if (!found)
        return fail(ODBC_ERROR_COMPONENT_NOT_FOUND);
    HMODULE module = LoadLibraryA(setup);
    CONFIG_DSN config = module ? (CONFIG_DSN)(void *)GetProcAddress(module, "ConfigDSN") : nullptr;
    if (!config) {
        if (module)
            FreeLibrary(module);
        return fail(ODBC_ERROR_LOAD_LIB_FAILED);
    }
    /* the requests for the system data sources are the same three, in the system's list. */
    DWORD saved = g_config_mode;
    WORD driver_request = request;
    if (request >= ODBC_ADD_SYS_DSN && request <= ODBC_REMOVE_SYS_DSN) {
        g_config_mode = ODBC_SYSTEM_DSN;
        driver_request = (WORD)(request - 3);
    } else {
        g_config_mode = ODBC_USER_DSN;
    }
    BOOL ok = config(parent, driver_request, driver, attributes);
    g_config_mode = saved;
    FreeLibrary(module);
    return ok ? TRUE : fail(ODBC_ERROR_REQUEST_FAILED);
}

/* a driver leaves an error of its own for the application to read with SQLInstallerError. */
short WINAPI SQLPostInstallerErrorA(DWORD code, const char *message)
{
    ERROR_LIST *list = error_list();
    if (!list || list->Count >= MAX_ERRORS)
        return SQL_ERROR;
    list->Codes[list->Count] = (int)code;
    lstrcpynA(list->Text[list->Count], message ? message : "", sizeof(list->Text[0]));
    list->Count++;
    return SQL_SUCCESS;
}

/* ------------------------------------------------------------------ wide versions */

/* the registry and the files of the installer are read in the code page of the system, as the ansi calls do. */
static char *narrow(const WCHAR *text, BOOL multi)
{
    if (!text)
        return nullptr;
    int length = 0;
    if (multi) {
        while (text[length] || text[length + 1])
            length++;
        length += 2;
    } else {
        length = lstrlenW(text) + 1;
    }
    int size = WideCharToMultiByte(CP_ACP, 0, text, length, nullptr, 0, nullptr, nullptr);
    char *out = HeapAlloc(GetProcessHeap(), 0, size + 1);
    if (!out)
        return nullptr;
    size = WideCharToMultiByte(CP_ACP, 0, text, length, out, size, nullptr, nullptr);
    out[size] = 0;
    return out;
}

static void drop(char *text)
{
    if (text)
        HeapFree(GetProcessHeap(), 0, text);
}

static int widen(const char *text, int length, WCHAR *out, int size)
{
    return MultiByteToWideChar(CP_ACP, 0, text, length, out, size);
}

short WINAPI SQLPostInstallerErrorW(DWORD code, const WCHAR *message)
{
    char *text = narrow(message, FALSE);
    short rc = SQLPostInstallerErrorA(code, text);
    drop(text);
    return rc;
}

short WINAPI SQLInstallerErrorW(WORD index, DWORD *code, WCHAR *message, WORD size, WORD *written)
{
    char text[256];
    WORD length = 0;
    short rc = SQLInstallerError(index, code, text, sizeof(text), &length);
    if (rc == SQL_NO_DATA)
        return rc;
    if (written)
        *written = length;
    if (message && size) {
        if (!widen(text, -1, message, size))
            message[size - 1] = 0;
        return length >= size ? 1 : SQL_SUCCESS;
    }
    return SQL_SUCCESS;
}

BOOL WINAPI SQLWritePrivateProfileStringW(const WCHAR *section, const WCHAR *key, const WCHAR *value, const WCHAR *file)
{
    char *a = narrow(section, FALSE), *b = narrow(key, FALSE), *c = narrow(value, FALSE), *d = narrow(file, FALSE);
    BOOL ok = SQLWritePrivateProfileString(a, b, c, d);
    drop(a), drop(b), drop(c), drop(d);
    return ok;
}

int WINAPI SQLGetPrivateProfileStringW(const WCHAR *section, const WCHAR *key, const WCHAR *default_value,
                                       WCHAR *buffer, int size, const WCHAR *file)
{
    if (!buffer || size <= 0)
        return -1;
    char *a = narrow(section, FALSE), *b = narrow(key, FALSE), *c = narrow(default_value, FALSE),
         *d = narrow(file, FALSE);
    char *text = HeapAlloc(GetProcessHeap(), 0, size + 2);
    int length = -1;
    if (text) {
        length = SQLGetPrivateProfileString(a, b, c, text, size, d);
        if (length >= 0) {
            length = widen(text, length, buffer, size);
            if (length < size)
                buffer[length] = 0;
            if (length + 1 < size)
                buffer[length + 1] = 0;
        }
    }
    drop(text);
    drop(a), drop(b), drop(c), drop(d);
    return length;
}

BOOL WINAPI SQLValidDSNW(const WCHAR *dsn)
{
    char *a = narrow(dsn, FALSE);
    BOOL ok = SQLValidDSN(a);
    drop(a);
    return ok;
}

BOOL WINAPI SQLWriteDSNToIniW(const WCHAR *dsn, const WCHAR *driver)
{
    char *a = narrow(dsn, FALSE), *b = narrow(driver, FALSE);
    BOOL ok = SQLWriteDSNToIni(a, b);
    drop(a), drop(b);
    return ok;
}

BOOL WINAPI SQLRemoveDSNFromIniW(const WCHAR *dsn)
{
    char *a = narrow(dsn, FALSE);
    BOOL ok = SQLRemoveDSNFromIni(a);
    drop(a);
    return ok;
}

BOOL WINAPI SQLRemoveDriverW(const WCHAR *driver, BOOL remove_dsns, DWORD *usage)
{
    char *a = narrow(driver, FALSE);
    BOOL ok = SQLRemoveDriver(a, remove_dsns, usage);
    drop(a);
    return ok;
}

BOOL WINAPI SQLConfigDataSourceW(HWND parent, WORD request, const WCHAR *driver, const WCHAR *attributes)
{
    char *a = narrow(driver, FALSE), *b = narrow(attributes, TRUE);
    BOOL ok = SQLConfigDataSource(parent, request, a, b);
    drop(a), drop(b);
    return ok;
}

BOOL WINAPI SQLInstallDriverExW(const WCHAR *driver, const WCHAR *path_in, WCHAR *path_out, WORD path_out_size,
                                WORD *path_out_length, WORD request, DWORD *usage)
{
    char *a = narrow(driver, TRUE), *b = narrow(path_in, FALSE);
    char out[MAX_PATH];
    WORD length = 0;
    BOOL ok = SQLInstallDriverEx(a, b, out, sizeof(out), &length, request, usage);
    if (ok && path_out && path_out_size)
        widen(out, -1, path_out, path_out_size);
    if (path_out_length)
        *path_out_length = length;
    drop(a), drop(b);
    return ok;
}

BOOL WINAPI SQLInstallDriverManagerW(WCHAR *path, WORD size, WORD *length)
{
    char out[MAX_PATH];
    WORD count = 0;
    BOOL ok = SQLInstallDriverManager(out, sizeof(out), &count);
    if (ok && path && size)
        widen(out, -1, path, size);
    if (length)
        *length = count;
    return ok;
}

BOOL WINAPI SQLGetInstalledDriversW(WCHAR *buffer, WORD size, WORD *length)
{
    if (!buffer || size < 2) {
        clear_errors();
        return fail(ODBC_ERROR_INVALID_BUFF_LEN);
    }
    char *text = HeapAlloc(GetProcessHeap(), 0, size);
    WORD count = 0;
    BOOL ok = text && SQLGetInstalledDrivers(text, size, &count);
    if (ok) {
        widen(text, count, buffer, size);
        if (length)
            *length = count;
    }
    drop(text);
    return ok;
}
