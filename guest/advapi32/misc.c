/*
 * event logging, the service control manager and hardware profiles.
 *
 * mime has no event log service and no service control manager: event
 * sources accept every report and write it to the debug output, and opening
 * the service control manager fails the way it does for a caller without
 * access, so programs take their documented fallback.
 */
#include "advapi32_private.h"
#include <mime/stub.h>

/* ---------------------------------------------------------------- event log */

#define EVENT_SOURCE_MAGIC 0x4E564545 /* "EEVN" */

typedef struct EVENT_SOURCE {
    DWORD Magic;
    WCHAR Name[];
} EVENT_SOURCE;

ADVAPI HANDLE WINAPI RegisterEventSourceW(LPCWSTR server, LPCWSTR source)
{
    if (!AdvIsLocalSystem(server)) {
        AdvSetError(RPC_S_SERVER_UNAVAILABLE);
        return nullptr;
    }
    if (!source)
        source = L"";
    SIZE_T n = wcslen(source);
    EVENT_SOURCE *s = AdvAlloc(sizeof(*s) + (n + 1) * sizeof(WCHAR));
    if (!s) {
        AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    s->Magic = EVENT_SOURCE_MAGIC;
    memcpy(s->Name, source, (n + 1) * sizeof(WCHAR));
    return s;
}

ADVAPI HANDLE WINAPI RegisterEventSourceA(LPCSTR server, LPCSTR source)
{
    PWSTR s = AdvAnsiToWide(server), n = AdvAnsiToWide(source);
    HANDLE h = (!server || s) && (!source || n) ? RegisterEventSourceW(s, n) : nullptr;
    AdvFree(s);
    AdvFree(n);
    return h;
}

static EVENT_SOURCE *event_source(HANDLE h)
{
    EVENT_SOURCE *s = h;
    if (!s || s->Magic != EVENT_SOURCE_MAGIC) {
        AdvSetError(ERROR_INVALID_HANDLE);
        return nullptr;
    }
    return s;
}

ADVAPI BOOL WINAPI DeregisterEventSource(HANDLE h)
{
    EVENT_SOURCE *s = event_source(h);
    if (!s)
        return FALSE;
    s->Magic = 0;
    AdvFree(s);
    return TRUE;
}

static PCWSTR event_type(WORD type)
{
    switch (type) {
    case 0x0001:
        return L"error";
    case 0x0002:
        return L"warning";
    case 0x0008:
        return L"audit success";
    case 0x0010:
        return L"audit failure";
    default:
        return L"information";
    }
}

/* one debug output line: source, type, event id and the insertion strings. */
ADVAPI BOOL WINAPI ReportEventW(HANDLE h, WORD type, WORD category, DWORD id, PSID user, WORD count, DWORD data_size,
                                LPCWSTR *strings, LPVOID data)
{
    EVENT_SOURCE *s = event_source(h);
    if (!s)
        return FALSE;
    if (count && !strings) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR line[1024];
    int n = _snwprintf(line, RTL_NUMBER_OF(line) - 2, L"%ls: %ls %lu:", s->Name, event_type(type), id & 0xFFFF);
    for (WORD i = 0; i < count && n >= 0 && n < (int)RTL_NUMBER_OF(line) - 2; i++) {
        int m = _snwprintf(line + n, RTL_NUMBER_OF(line) - 2 - n, L" %ls", strings[i] ? strings[i] : L"");
        n = m < 0 ? (int)RTL_NUMBER_OF(line) - 2 : n + m;
    }
    if (n < 0 || n > (int)RTL_NUMBER_OF(line) - 2)
        n = RTL_NUMBER_OF(line) - 2;
    line[n] = L'\n';
    line[n + 1] = 0;
    OutputDebugStringW(line);
    return TRUE;
}

ADVAPI BOOL WINAPI ReportEventA(HANDLE h, WORD type, WORD category, DWORD id, PSID user, WORD count, DWORD data_size,
                                LPCSTR *strings, LPVOID data)
{
    if (count && !strings) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PWSTR *wide = count ? AdvAllocZero(count * sizeof(PWSTR)) : nullptr;
    if (count && !wide) {
        AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    for (WORD i = 0; i < count; i++)
        wide[i] = AdvAnsiToWide(strings[i]);
    BOOL ok = ReportEventW(h, type, category, id, user, count, data_size, (LPCWSTR *)wide, data);
    for (WORD i = 0; i < count; i++)
        AdvFree(wide[i]);
    AdvFree(wide);
    return ok;
}

/* the cache of HKEY_CURRENT_USER is never kept apart from the user, so there is nothing to drop. */
ADVAPI LSTATUS WINAPI RegDisablePredefinedCache(void)
{
    return ERROR_SUCCESS;
}

/* ---------------------------------------------------------------- service control manager */

typedef HANDLE SC_HANDLE;

#define ERROR_DATABASE_DOES_NOT_EXIST 1065L

ADVAPI SC_HANDLE WINAPI OpenSCManagerW(LPCWSTR machine, LPCWSTR database, DWORD access)
{
    if (database && _wcsicmp(database, L"ServicesActive") && _wcsicmp(database, L"ServicesFailed")) {
        AdvSetError(ERROR_DATABASE_DOES_NOT_EXIST);
        return nullptr;
    }
    AdvSetError(AdvIsLocalSystem(machine) ? ERROR_ACCESS_DENIED : RPC_S_SERVER_UNAVAILABLE);
    return nullptr;
}

ADVAPI SC_HANDLE WINAPI OpenSCManagerA(LPCSTR machine, LPCSTR database, DWORD access)
{
    PWSTR m = AdvAnsiToWide(machine), d = AdvAnsiToWide(database);
    SC_HANDLE h = (!machine || m) && (!database || d) ? OpenSCManagerW(m, d, access) : nullptr;
    AdvFree(m);
    AdvFree(d);
    return h;
}

/* no manager handle can exist, so every handle given here is invalid. */
ADVAPI SC_HANDLE WINAPI OpenServiceW(SC_HANDLE manager, LPCWSTR name, DWORD access)
{
    AdvSetError(ERROR_INVALID_HANDLE);
    return nullptr;
}

ADVAPI SC_HANDLE WINAPI OpenServiceA(SC_HANDLE manager, LPCSTR name, DWORD access)
{
    AdvSetError(ERROR_INVALID_HANDLE);
    return nullptr;
}

ADVAPI BOOL WINAPI CloseServiceHandle(SC_HANDLE handle)
{
    AdvSetError(ERROR_INVALID_HANDLE);
    return FALSE;
}

#define ERROR_SERVICE_ALREADY_RUNNING 1056L
#define ERROR_FAILED_SERVICE_CONTROLLER_CONNECT 1063L
#define ERROR_SERVICE_NOT_IN_EXE 1083L

typedef struct SERVICE_TABLE_ENTRYW {
    LPWSTR lpServiceName;
    PVOID lpServiceProc;
} SERVICE_TABLE_ENTRYW;

typedef struct SERVICE_TABLE_ENTRYA {
    LPSTR lpServiceName;
    PVOID lpServiceProc;
} SERVICE_TABLE_ENTRYA;

static LONG g_dispatcher_started;

/*
 * a process the manager did not start cannot reach it: windows fails the
 * connection with ERROR_FAILED_SERVICE_CONTROLLER_CONNECT, and with no
 * manager here every process is one of those. a second call is refused
 * before the table is looked at.
 */
static BOOL start_dispatcher(BOOL empty)
{
    if (InterlockedExchange(&g_dispatcher_started, 1) == 1) {
        AdvSetError(ERROR_SERVICE_ALREADY_RUNNING);
        return FALSE;
    }
    AdvSetError(empty ? ERROR_INVALID_PARAMETER : ERROR_FAILED_SERVICE_CONTROLLER_CONNECT);
    return FALSE;
}

ADVAPI BOOL WINAPI StartServiceCtrlDispatcherW(const SERVICE_TABLE_ENTRYW *table)
{
    return start_dispatcher(!table->lpServiceName);
}

ADVAPI BOOL WINAPI StartServiceCtrlDispatcherA(const SERVICE_TABLE_ENTRYA *table)
{
    return start_dispatcher(!table->lpServiceName);
}

/* the service must be one of the dispatcher's table, which never ran. */
static HANDLE no_dispatch_entry(void)
{
    AdvSetError(ERROR_SERVICE_NOT_IN_EXE);
    return nullptr;
}

ADVAPI HANDLE WINAPI RegisterServiceCtrlHandlerW(LPCWSTR name, PVOID handler)
{
    return no_dispatch_entry();
}

ADVAPI HANDLE WINAPI RegisterServiceCtrlHandlerA(LPCSTR name, PVOID handler)
{
    return no_dispatch_entry();
}

ADVAPI HANDLE WINAPI RegisterServiceCtrlHandlerExW(LPCWSTR name, PVOID handler, LPVOID context)
{
    return no_dispatch_entry();
}

ADVAPI HANDLE WINAPI RegisterServiceCtrlHandlerExA(LPCSTR name, PVOID handler, LPVOID context)
{
    return no_dispatch_entry();
}

static BOOL bad_handle(void)
{
    AdvSetError(ERROR_INVALID_HANDLE);
    return FALSE;
}

ADVAPI BOOL WINAPI SetServiceStatus(HANDLE status, PVOID info)
{
    return bad_handle();
}

ADVAPI SC_HANDLE WINAPI CreateServiceW(SC_HANDLE manager, LPCWSTR name, LPCWSTR display, DWORD access, DWORD type,
                                       DWORD start, DWORD error_control, LPCWSTR binary, LPCWSTR group,
                                       LPDWORD tag, LPCWSTR dependencies, LPCWSTR user, LPCWSTR password)
{
    bad_handle();
    return nullptr;
}

ADVAPI SC_HANDLE WINAPI CreateServiceA(SC_HANDLE manager, LPCSTR name, LPCSTR display, DWORD access, DWORD type,
                                       DWORD start, DWORD error_control, LPCSTR binary, LPCSTR group, LPDWORD tag,
                                       LPCSTR dependencies, LPCSTR user, LPCSTR password)
{
    bad_handle();
    return nullptr;
}

ADVAPI BOOL WINAPI DeleteService(SC_HANDLE service)
{
    return bad_handle();
}

ADVAPI BOOL WINAPI ChangeServiceConfigW(SC_HANDLE service, DWORD type, DWORD start, DWORD error_control,
                                        LPCWSTR binary, LPCWSTR group, LPDWORD tag, LPCWSTR dependencies,
                                        LPCWSTR user, LPCWSTR password, LPCWSTR display)
{
    return bad_handle();
}

ADVAPI BOOL WINAPI ChangeServiceConfigA(SC_HANDLE service, DWORD type, DWORD start, DWORD error_control,
                                        LPCSTR binary, LPCSTR group, LPDWORD tag, LPCSTR dependencies, LPCSTR user,
                                        LPCSTR password, LPCSTR display)
{
    return bad_handle();
}

ADVAPI BOOL WINAPI ChangeServiceConfig2W(SC_HANDLE service, DWORD level, LPVOID info)
{
    return bad_handle();
}

ADVAPI BOOL WINAPI ChangeServiceConfig2A(SC_HANDLE service, DWORD level, LPVOID info)
{
    return bad_handle();
}

ADVAPI BOOL WINAPI QueryServiceConfigW(SC_HANDLE service, PVOID config, DWORD size, LPDWORD needed)
{
    return bad_handle();
}

ADVAPI BOOL WINAPI QueryServiceConfigA(SC_HANDLE service, PVOID config, DWORD size, LPDWORD needed)
{
    return bad_handle();
}

ADVAPI BOOL WINAPI QueryServiceConfig2W(SC_HANDLE service, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed)
{
    return bad_handle();
}

ADVAPI BOOL WINAPI QueryServiceConfig2A(SC_HANDLE service, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed)
{
    return bad_handle();
}

ADVAPI BOOL WINAPI QueryServiceStatusEx(SC_HANDLE service, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed)
{
    return bad_handle();
}

ADVAPI BOOL WINAPI ControlService(SC_HANDLE service, DWORD control, PVOID status)
{
    return bad_handle();
}

ADVAPI BOOL WINAPI EnumServicesStatusExW(SC_HANDLE manager, DWORD level, DWORD type, DWORD state, LPBYTE services,
                                         DWORD size, LPDWORD needed, LPDWORD returned, LPDWORD resume,
                                         LPCWSTR group)
{
    return bad_handle();
}

ADVAPI BOOL WINAPI EnumServicesStatusExA(SC_HANDLE manager, DWORD level, DWORD type, DWORD state, LPBYTE services,
                                         DWORD size, LPDWORD needed, LPDWORD returned, LPDWORD resume, LPCSTR group)
{
    return bad_handle();
}

/* ---------------------------------------------------------------- hardware profiles */

typedef struct HW_PROFILE_INFOW {
    DWORD dwDockInfo;
    WCHAR szHwProfileGuid[39];
    WCHAR szHwProfileName[80];
} HW_PROFILE_INFOW;

typedef struct HW_PROFILE_INFOA {
    DWORD dwDockInfo;
    CHAR szHwProfileGuid[39];
    CHAR szHwProfileName[80];
} HW_PROFILE_INFOA;

#define DOCKINFO_UNDOCKED 0x1
#define DOCKINFO_DOCKED 0x2
#define DOCKINFO_USER_SUPPLIED 0x4

/* reads the current profile of IDConfigDB, falling back to the dock state windows reports without one. */
ADVAPI BOOL WINAPI GetCurrentHwProfileW(HW_PROFILE_INFOW *info)
{
    if (!info) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    HKEY db = nullptr, profile = nullptr, dock = nullptr;
    DWORD current = 1, size = sizeof(current);
    WCHAR path[64];
    BOOL ok = !RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"System\\CurrentControlSet\\Control\\IDConfigDB", 0,
                             KEY_QUERY_VALUE, &db) &&
              !RegQueryValueExW(db, L"CurrentConfig", nullptr, nullptr, (LPBYTE)&current, &size) &&
              _snwprintf(path, RTL_NUMBER_OF(path), L"Hardware Profiles\\%04u", current) > 0 &&
              !RegOpenKeyExW(db, path, 0, KEY_QUERY_VALUE, &profile) &&
              !RegOpenKeyExW(db, L"CurrentDockInfo", 0, KEY_QUERY_VALUE, &dock);
    if (ok) {
        size = sizeof(DWORD);
        if (RegQueryValueExW(dock, L"DockingState", nullptr, nullptr, (LPBYTE)&info->dwDockInfo, &size) ||
            !info->dwDockInfo || (info->dwDockInfo & 3) == 3) {
            size = sizeof(DWORD);
            if (RegQueryValueExW(profile, L"DockState", nullptr, nullptr, (LPBYTE)&info->dwDockInfo, &size) ||
                !info->dwDockInfo)
                info->dwDockInfo = DOCKINFO_UNDOCKED | DOCKINFO_DOCKED | DOCKINFO_USER_SUPPLIED;
        }
        size = sizeof(info->szHwProfileGuid);
        if (RegQueryValueExW(profile, L"HwProfileGuid", nullptr, nullptr, (LPBYTE)info->szHwProfileGuid, &size))
            info->szHwProfileGuid[0] = 0;
        info->szHwProfileGuid[RTL_NUMBER_OF(info->szHwProfileGuid) - 1] = 0;
        size = sizeof(info->szHwProfileName);
        if (RegQueryValueExW(profile, L"FriendlyName", nullptr, nullptr, (LPBYTE)info->szHwProfileName, &size))
            info->szHwProfileName[0] = 0;
        info->szHwProfileName[RTL_NUMBER_OF(info->szHwProfileName) - 1] = 0;
    } else {
        AdvSetError(ERROR_REGISTRY_CORRUPT);
    }
    if (db)
        RegCloseKey(db);
    if (profile)
        RegCloseKey(profile);
    if (dock)
        RegCloseKey(dock);
    return ok;
}

ADVAPI BOOL WINAPI GetCurrentHwProfileA(HW_PROFILE_INFOA *info)
{
    if (!info) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    HW_PROFILE_INFOW w;
    memset(&w, 0, sizeof(w));
    if (!GetCurrentHwProfileW(&w))
        return FALSE;
    info->dwDockInfo = w.dwDockInfo;
    if (!WideCharToMultiByte(CP_ACP, 0, w.szHwProfileGuid, -1, info->szHwProfileGuid, sizeof(info->szHwProfileGuid),
                             nullptr, nullptr) ||
        !WideCharToMultiByte(CP_ACP, 0, w.szHwProfileName, -1, info->szHwProfileName, sizeof(info->szHwProfileName),
                             nullptr, nullptr)) {
        AdvSetError(ERROR_INTERNAL_ERROR);
        return FALSE;
    }
    return TRUE;
}

/* ---------------------------------------------------------------- services */

typedef struct _SERVICE_STATUS {
    DWORD dwServiceType;
    DWORD dwCurrentState;
    DWORD dwControlsAccepted;
    DWORD dwWin32ExitCode;
    DWORD dwServiceSpecificExitCode;
    DWORD dwCheckPoint;
    DWORD dwWaitHint;
} SERVICE_STATUS, *LPSERVICE_STATUS;

/* like OpenServiceW, these can never receive a valid service handle. */
ADVAPI BOOL WINAPI QueryServiceStatus(SC_HANDLE service, LPSERVICE_STATUS status)
{
    AdvSetError(status ? ERROR_INVALID_HANDLE : ERROR_INVALID_ADDRESS);
    return FALSE;
}

ADVAPI BOOL WINAPI StartServiceW(SC_HANDLE service, DWORD count, LPCWSTR *arguments)
{
    AdvSetError(ERROR_INVALID_HANDLE);
    return FALSE;
}

ADVAPI BOOL WINAPI StartServiceA(SC_HANDLE service, DWORD count, LPCSTR *arguments)
{
    AdvSetError(ERROR_INVALID_HANDLE);
    return FALSE;
}

/* ---------------------------------------------------------------- windows 3.1 registry values */

/* the default value of a subkey, always a string, as the 16-bit registry stored it. */
ADVAPI LSTATUS WINAPI RegSetValueW(HKEY key, LPCWSTR subkey, DWORD type, LPCWSTR data, DWORD size)
{
    if (type != REG_SZ || !data)
        return ERROR_INVALID_PARAMETER;
    HKEY target = key;
    if (subkey && *subkey) {
        LSTATUS error = RegCreateKeyExW(key, subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &target, nullptr);
        if (error)
            return error;
    }
    LSTATUS error = RegSetValueExW(target, nullptr, 0, REG_SZ, (const BYTE *)data,
                                   (DWORD)(wcslen(data) + 1) * sizeof(WCHAR));
    if (target != key)
        RegCloseKey(target);
    return error;
}

ADVAPI LSTATUS WINAPI RegSetValueA(HKEY key, LPCSTR subkey, DWORD type, LPCSTR data, DWORD size)
{
    if (type != REG_SZ || !data)
        return ERROR_INVALID_PARAMETER;
    PWSTR s = AdvAnsiToWide(subkey), d = AdvAnsiToWide(data);
    LSTATUS error = (!subkey || s) && d ? RegSetValueW(key, s, type, d, 0) : ERROR_NOT_ENOUGH_MEMORY;
    AdvFree(s);
    AdvFree(d);
    return error;
}

ADVAPI LSTATUS WINAPI RegQueryValueW(HKEY key, LPCWSTR subkey, LPWSTR data, PLONG size)
{
    if (data && !size)
        return ERROR_INVALID_PARAMETER;
    HKEY target = key;
    if (subkey && *subkey) {
        LSTATUS error = RegOpenKeyExW(key, subkey, 0, KEY_QUERY_VALUE, &target);
        if (error)
            return error;
    }
    DWORD type, bytes = size ? (DWORD)*size : 0;
    LSTATUS error = RegQueryValueExW(target, nullptr, nullptr, &type, (BYTE *)data, size ? &bytes : nullptr);
    if (error == ERROR_FILE_NOT_FOUND) {
        /* a key without a default value reads as an empty string. */
        bytes = sizeof(WCHAR);
        error = data && size && *size < (LONG)bytes ? ERROR_MORE_DATA : ERROR_SUCCESS;
        if (!error && data)
            data[0] = 0;
    } else if (!error && type != REG_SZ && type != REG_EXPAND_SZ) {
        error = ERROR_INVALID_DATA;
    }
    if (size && (!error || error == ERROR_MORE_DATA))
        *size = (LONG)bytes;
    if (target != key)
        RegCloseKey(target);
    return error;
}

ADVAPI LSTATUS WINAPI RegQueryValueA(HKEY key, LPCSTR subkey, LPSTR data, PLONG size)
{
    if (data && !size)
        return ERROR_INVALID_PARAMETER;
    PWSTR s = AdvAnsiToWide(subkey);
    if (subkey && !s)
        return ERROR_NOT_ENOUGH_MEMORY;
    LONG wide_size = 0;
    LSTATUS error = RegQueryValueW(key, s, nullptr, &wide_size);
    PWSTR w = !error ? AdvAlloc(wide_size + sizeof(WCHAR)) : nullptr;
    if (!error && !w)
        error = ERROR_NOT_ENOUGH_MEMORY;
    if (!error)
        error = RegQueryValueW(key, s, w, &wide_size);
    if (!error) {
        w[wide_size / sizeof(WCHAR)] = 0;
        int need = WideCharToMultiByte(CP_ACP, 0, w, -1, nullptr, 0, nullptr, nullptr);
        if (data && *size < need)
            error = ERROR_MORE_DATA;
        else if (data)
            WideCharToMultiByte(CP_ACP, 0, w, -1, data, *size, nullptr, nullptr);
        if (size)
            *size = need;
    }
    AdvFree(w);
    AdvFree(s);
    return error;
}

/* ---------------------------------------------------------------- text detection */

#define IS_TEXT_UNICODE_ASCII16 0x0001
#define IS_TEXT_UNICODE_STATISTICS 0x0002
#define IS_TEXT_UNICODE_CONTROLS 0x0004
#define IS_TEXT_UNICODE_SIGNATURE 0x0008
#define IS_TEXT_UNICODE_REVERSE_ASCII16 0x0010
#define IS_TEXT_UNICODE_REVERSE_STATISTICS 0x0020
#define IS_TEXT_UNICODE_REVERSE_CONTROLS 0x0040
#define IS_TEXT_UNICODE_REVERSE_SIGNATURE 0x0080
#define IS_TEXT_UNICODE_ILLEGAL_CHARS 0x0100
#define IS_TEXT_UNICODE_ODD_LENGTH 0x0200
#define IS_TEXT_UNICODE_NULL_BYTES 0x1000
#define IS_TEXT_UNICODE_UNICODE_MASK 0x000F
#define IS_TEXT_UNICODE_REVERSE_MASK 0x00F0
#define IS_TEXT_UNICODE_NOT_UNICODE_MASK 0x0F00
#define IS_TEXT_UNICODE_NOT_ASCII_MASK 0xF000

static BOOL is_control(WCHAR c)
{
    return c == L'\r' || c == L'\n' || c == L'\t' || c == L' ' || c == 0x3000;
}

/*
 * runs the tests the caller asks for, all when it gives no result
 * pointer, and reports each that held. the text counts as utf-16 when a
 * utf-16 test holds and no test against it does.
 */
ADVAPI BOOL WINAPI IsTextUnicode(const void *buffer, int size, LPINT result)
{
    const BYTE *bytes = buffer;
    const WCHAR *text = buffer;
    int wanted = result ? *result : 0xFFFF, found = 0, n = size / 2;
    if (!buffer || size < 2) {
        if (result)
            *result = 0;
        return FALSE;
    }
    if (size & 1)
        found |= IS_TEXT_UNICODE_ODD_LENGTH;
    if (text[0] == 0xFEFF)
        found |= IS_TEXT_UNICODE_SIGNATURE;
    if (text[0] == 0xFFFE)
        found |= IS_TEXT_UNICODE_REVERSE_SIGNATURE;
    BOOL ascii = TRUE, reverse_ascii = TRUE, controls = FALSE, reverse_controls = FALSE, illegal = FALSE;
    int high_low = 0, low_high = 0;
    for (int i = 0; i < n; i++) {
        WCHAR c = text[i], r = (WCHAR)(c >> 8 | c << 8);
        if (c >= 0x100)
            ascii = FALSE;
        if (r >= 0x100)
            reverse_ascii = FALSE;
        if (is_control(c))
            controls = TRUE;
        if (is_control(r))
            reverse_controls = TRUE;
        if (c == 0xFFFF || c == 0xFFFE)
            illegal = TRUE;
        if (!(c & 0xFF00) && (c & 0xFF))
            low_high++;
        if (!(c & 0xFF) && (c & 0xFF00))
            high_low++;
    }
    if (ascii)
        found |= IS_TEXT_UNICODE_ASCII16;
    if (reverse_ascii && !ascii)
        found |= IS_TEXT_UNICODE_REVERSE_ASCII16;
    if (controls)
        found |= IS_TEXT_UNICODE_CONTROLS;
    if (reverse_controls && !controls)
        found |= IS_TEXT_UNICODE_REVERSE_CONTROLS;
    if (illegal)
        found |= IS_TEXT_UNICODE_ILLEGAL_CHARS;
    /* statistics: most characters of utf-16 western text have a zero high byte. */
    if (low_high * 2 > n)
        found |= IS_TEXT_UNICODE_STATISTICS;
    else if (high_low * 2 > n)
        found |= IS_TEXT_UNICODE_REVERSE_STATISTICS;
    for (int i = 0; i < size; i++) {
        if (!bytes[i]) {
            found |= IS_TEXT_UNICODE_NULL_BYTES;
            break;
        }
    }
    found &= wanted;
    if (result)
        *result = found;
    if (found & (IS_TEXT_UNICODE_NOT_UNICODE_MASK | IS_TEXT_UNICODE_REVERSE_MASK))
        return FALSE;
    return (found & IS_TEXT_UNICODE_UNICODE_MASK) != 0;
}

/* ---------------------------------------------------------------- local security authority */

ADVAPI ULONG WINAPI LsaNtStatusToWinError(NTSTATUS status)
{
    return RtlNtStatusToDosError(status);
}
