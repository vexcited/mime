/*
 * event logging and hardware profiles. there is no event log service: sources accept every report and write it to
 * the debug output. the service control manager is in scm.c.
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

/* ---------------------------------------------------------------- reading event logs */

#define EVENT_LOG_MAGIC 0x474C5645 /* "EVLG" */
#define EVENTLOG_SEQUENTIAL_READ_ 0x0001
#define EVENTLOG_SEEK_READ_ 0x0002
#define EVENTLOG_FORWARDS_READ_ 0x0004
#define EVENTLOG_BACKWARDS_READ_ 0x0008

typedef struct EVENT_LOG {
    DWORD Magic;
    WCHAR Name[];
} EVENT_LOG;

/*
 * there is no event log: a log opens and closes and never holds a record. an unknown name opens the Application log,
 * as windows does for an unknown source, and the name is kept as given.
 */
ADVAPI HANDLE WINAPI OpenEventLogW(LPCWSTR server, LPCWSTR source)
{
    if (!source) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    if (!AdvIsLocalSystem(server)) {
        AdvSetError(RPC_S_SERVER_UNAVAILABLE);
        return nullptr;
    }
    SIZE_T n = wcslen(source);
    EVENT_LOG *log = AdvAlloc(sizeof(*log) + (n + 1) * sizeof(WCHAR));
    if (!log) {
        AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    log->Magic = EVENT_LOG_MAGIC;
    memcpy(log->Name, source, (n + 1) * sizeof(WCHAR));
    return log;
}

ADVAPI HANDLE WINAPI OpenEventLogA(LPCSTR server, LPCSTR source)
{
    if (!source) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    PWSTR s = AdvAnsiToWide(server), n = AdvAnsiToWide(source);
    HANDLE h = (!server || s) && n ? OpenEventLogW(s, n) : nullptr;
    AdvFree(s);
    AdvFree(n);
    return h;
}

static EVENT_LOG *event_log(HANDLE h)
{
    EVENT_LOG *log = h;
    if (!log || log->Magic != EVENT_LOG_MAGIC) {
        AdvSetError(ERROR_INVALID_HANDLE);
        return nullptr;
    }
    return log;
}

/* the information a log can give is whether it is full, and a log that never holds a record is not. */
ADVAPI BOOL WINAPI GetEventLogInformation(HANDLE h, DWORD level, LPVOID buffer, DWORD size, LPDWORD needed)
{
    if (!event_log(h))
        return FALSE;
    if (level != 0) {
        AdvSetError(ERROR_INVALID_LEVEL);
        return FALSE;
    }
    if (!needed) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *needed = sizeof(DWORD);
    if (!buffer || size < sizeof(DWORD)) {
        AdvSetError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    *(DWORD *)buffer = 0;
    return TRUE;
}

ADVAPI BOOL WINAPI CloseEventLog(HANDLE h)
{
    EVENT_LOG *log = event_log(h);
    if (!log)
        return FALSE;
    log->Magic = 0;
    AdvFree(log);
    return TRUE;
}

/* the flags must give exactly one read mode and one direction; with no records the read ends at once. */
ADVAPI BOOL WINAPI ReadEventLogW(HANDLE h, DWORD flags, DWORD offset, LPVOID buffer, DWORD bytes, DWORD *read, DWORD *needed)
{
    if (read)
        *read = 0;
    if (needed)
        *needed = 0;
    if (!event_log(h))
        return FALSE;
    DWORD how = flags & (EVENTLOG_SEQUENTIAL_READ_ | EVENTLOG_SEEK_READ_);
    DWORD way = flags & (EVENTLOG_FORWARDS_READ_ | EVENTLOG_BACKWARDS_READ_);
    if ((how != EVENTLOG_SEQUENTIAL_READ_ && how != EVENTLOG_SEEK_READ_) ||
        (way != EVENTLOG_FORWARDS_READ_ && way != EVENTLOG_BACKWARDS_READ_) || !buffer || !read || !needed) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    AdvSetError(ERROR_HANDLE_EOF);
    return FALSE;
}

ADVAPI BOOL WINAPI ReadEventLogA(HANDLE h, DWORD flags, DWORD offset, LPVOID buffer, DWORD bytes, DWORD *read, DWORD *needed)
{
    return ReadEventLogW(h, flags, offset, buffer, bytes, read, needed);
}

ADVAPI BOOL WINAPI GetNumberOfEventLogRecords(HANDLE h, PDWORD count)
{
    if (!event_log(h))
        return FALSE;
    if (!count) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *count = 0;
    return TRUE;
}

/* an empty log's oldest record is the number the first one will get. */
ADVAPI BOOL WINAPI GetOldestEventLogRecord(HANDLE h, PDWORD record)
{
    if (!event_log(h))
        return FALSE;
    if (!record) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *record = 1;
    return TRUE;
}

ADVAPI BOOL WINAPI ClearEventLogW(HANDLE h, LPCWSTR backup)
{
    if (!event_log(h))
        return FALSE;
    /* nothing to save, and nothing for a backup file to hold. */
    if (backup) {
        AdvSetError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    return TRUE;
}

ADVAPI BOOL WINAPI ClearEventLogA(HANDLE h, LPCSTR backup)
{
    PWSTR wide = AdvAnsiToWide(backup);
    BOOL ok;
    if (backup && !wide) {
        AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    ok = ClearEventLogW(h, wide);
    AdvFree(wide);
    return ok;
}

/* a log is never written, so a backup has nothing to hold and there is no file format to read one back from. */
ADVAPI BOOL WINAPI BackupEventLogW(HANDLE h, LPCWSTR file)
{
    if (!event_log(h))
        return FALSE;
    if (!file) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    AdvSetError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

/* the idle task work is done by a service that is not here. */
ADVAPI DWORD WINAPI ProcessIdleTasks(void)
{
    return RPC_S_SERVER_UNAVAILABLE;
}

ADVAPI BOOL WINAPI BackupEventLogA(HANDLE h, LPCSTR file)
{
    PWSTR wide = AdvAnsiToWide(file);
    if (file && !wide) {
        AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    BOOL ok = BackupEventLogW(h, wide);
    AdvFree(wide);
    return ok;
}

ADVAPI HANDLE WINAPI OpenBackupEventLogW(LPCWSTR server, LPCWSTR file)
{
    if (!file) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    if (!AdvIsLocalSystem(server)) {
        AdvSetError(RPC_S_SERVER_UNAVAILABLE);
        return nullptr;
    }
    DWORD attributes = GetFileAttributesW(file);
    AdvSetError(attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_NOT_SUPPORTED);
    return nullptr;
}

ADVAPI HANDLE WINAPI OpenBackupEventLogA(LPCSTR server, LPCSTR file)
{
    if (!file) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    PWSTR s = AdvAnsiToWide(server), f = AdvAnsiToWide(file);
    HANDLE h = (!server || s) && f ? OpenBackupEventLogW(s, f) : nullptr;
    AdvFree(s);
    AdvFree(f);
    return h;
}

/* the event never fires since no record is ever added. */
ADVAPI BOOL WINAPI NotifyChangeEventLog(HANDLE h, HANDLE event)
{
    if (!event_log(h))
        return FALSE;
    if (!event) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
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

/* writes one debug line: source, type, event id and the insertion strings. */
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

/* the HKEY_CURRENT_USER cache is never kept apart from the user, so there is nothing to drop. */
ADVAPI LSTATUS WINAPI RegDisablePredefinedCache(void)
{
    return ERROR_SUCCESS;
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

/* reads the current profile from IDConfigDB, else the dock state windows reports without one. */
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
 * runs the tests the caller asks for, all of them when result is NULL, and reports each that held. the text is
 * utf-16 when a utf-16 test holds and no test against it does.
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
    /* statistics: utf-16 western text mostly has zero high bytes. */
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

/* ---------------------------------------------------------------- remote registry */

/*
 * a key of this machine's registry, as its own handle for keys that have one. flags are only checked for a named
 * machine: bits above 7, or bits 1 and 4 together, give ERROR_INVALID_PARAMETER. no other machine is reachable since
 * the remote registry service has no peer: ERROR_BAD_NETPATH, what windows gives when no transport finds the machine.
 * "." and "localhost" are assumed to name this machine, like the computer name.
 */
ADVAPI LSTATUS WINAPI RegConnectRegistryExW(LPCWSTR machine, HKEY key, ULONG flags, HKEY *result)
{
    if (machine && *machine) {
        if ((flags & 0xFFFFFFF8) || (flags & 5) == 5)
            return ERROR_INVALID_PARAMETER;
        if (!AdvIsLocalSystem(machine))
            return ERROR_BAD_NETPATH;
    }
    if (!result)
        return ERROR_INVALID_PARAMETER;
    switch ((ULONG)(ULONG_PTR)key) {
    case 0x80000000: /* HKEY_CLASSES_ROOT */
    case 0x80000001: /* HKEY_CURRENT_USER */
    case 0x80000002: /* HKEY_LOCAL_MACHINE */
    case 0x80000003: /* HKEY_USERS */
        return RegDuplicateHKey(key, result);
    case 0x80000004: /* HKEY_PERFORMANCE_DATA */
    case 0x80000005: /* HKEY_CURRENT_CONFIG */
    case 0x80000006: /* HKEY_DYN_DATA */
    case 0x80000050: /* HKEY_PERFORMANCE_TEXT */
    case 0x80000060: /* HKEY_PERFORMANCE_NLSTEXT */
        *result = key;
        return ERROR_SUCCESS;
    }
    return ERROR_INVALID_HANDLE;
}

ADVAPI LSTATUS WINAPI RegConnectRegistryW(LPCWSTR machine, HKEY key, HKEY *result)
{
    return RegConnectRegistryExW(machine, key, 0, result);
}

ADVAPI LSTATUS WINAPI RegConnectRegistryExA(LPCSTR machine, HKEY key, ULONG flags, HKEY *result)
{
    PWSTR wide = AdvAnsiToWide(machine);
    if (machine && !wide)
        return ERROR_NOT_ENOUGH_MEMORY;
    LSTATUS status = RegConnectRegistryExW(wide, key, flags, result);
    AdvFree(wide);
    return status;
}

ADVAPI LSTATUS WINAPI RegConnectRegistryA(LPCSTR machine, HKEY key, HKEY *result)
{
    return RegConnectRegistryExA(machine, key, 0, result);
}

/*
 * registry reflection between the 32-bit and 64-bit views, which early 64-bit windows had and later dropped. the calls
 * remain for 32-bit programs written for it. no key is reflected, so disabling and enabling succeed and none is ever
 * found disabled; native 64-bit callers get what windows answers them.
 */
ADVAPI LSTATUS WINAPI RegDisableReflectionKey(HKEY key)
{
    (void)key;
#ifdef _WIN64
    return ERROR_CALL_NOT_IMPLEMENTED;
#else
    return ERROR_SUCCESS;
#endif
}

ADVAPI LSTATUS WINAPI RegEnableReflectionKey(HKEY key)
{
    (void)key;
#ifdef _WIN64
    return ERROR_CALL_NOT_IMPLEMENTED;
#else
    return ERROR_SUCCESS;
#endif
}

/*
 * renames a key, the key itself or a subkey of it. Windows asks the kernel to change the name in place. the kernel
 * of Mime has no such call, so the key is made again under the new name with a copy of its tree and the old one is
 * deleted: a handle to the old key stays open on a deleted key, which Windows would have renamed.
 */
ADVAPI LSTATUS WINAPI RegRenameKey(HKEY key, LPCWSTR subkey, LPCWSTR new_name)
{
    HKEY target = key;
    LSTATUS error = ERROR_SUCCESS;
    if (subkey) {
        error = RegOpenKeyExW(key, subkey, 0, KEY_READ | KEY_WRITE | DELETE, &target);
        if (error)
            return error;
    }
    HKEY parent = nullptr, created = nullptr;
    union {
        ULONG Length;
        BYTE Bytes[sizeof(ULONG) + 2 * 1024];
    } info;
    ULONG returned = 0;
    if (!new_name || !*new_name || wcschr(new_name, L'\\')) {
        error = ERROR_INVALID_PARAMETER;
        goto done;
    }
    if (!NT_SUCCESS(NtQueryKey(target, 3, &info, sizeof(info), &returned)) ||
        info.Length > sizeof(info) - sizeof(ULONG)) {
        error = ERROR_INVALID_HANDLE;
        goto done;
    }
    {
        WCHAR *path = (WCHAR *)(info.Bytes + sizeof(ULONG));
        ULONG chars = info.Length / sizeof(WCHAR);
        ULONG leaf = chars;
        while (leaf && path[leaf - 1] != L'\\')
            leaf--;
        /* the roots of the registry and of its hives have no parent to rename in. */
        if (leaf < 2 || leaf <= wcslen(L"\\REGISTRY\\X")) {
            error = ERROR_ACCESS_DENIED;
            goto done;
        }
        WCHAR old_name[256];
        if (chars - leaf >= RTL_NUMBER_OF(old_name)) {
            error = ERROR_INVALID_PARAMETER;
            goto done;
        }
        memcpy(old_name, path + leaf, (chars - leaf) * sizeof(WCHAR));
        old_name[chars - leaf] = 0;
        UNICODE_STRING name = {(USHORT)((leaf - 1) * sizeof(WCHAR)), (USHORT)((leaf - 1) * sizeof(WCHAR)), path};
        OBJECT_ATTRIBUTES attributes;
        InitializeObjectAttributes(&attributes, &name, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
        NTSTATUS status = NtOpenKey((PHANDLE)&parent, KEY_READ | KEY_WRITE | DELETE, &attributes);
        if (!NT_SUCCESS(status)) {
            error = RtlNtStatusToDosError(status);
            goto done;
        }
        HKEY existing;
        if (!RegOpenKeyExW(parent, new_name, 0, KEY_READ, &existing)) {
            RegCloseKey(existing);
            error = ERROR_ALREADY_EXISTS;
            goto done;
        }
        error = RegCreateKeyExW(parent, new_name, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &created, nullptr);
        if (error)
            goto done;
        error = RegCopyTreeW(target, nullptr, created);
        RegCloseKey(created);
        created = nullptr;
        if (error) {
            RegDeleteTreeW(parent, new_name);
            goto done;
        }
        if (target != key)
            RegCloseKey(target);
        target = key;
        error = RegDeleteTreeW(parent, old_name);
        if (error)
            RegDeleteTreeW(parent, new_name);
    }
done:
    if (created)
        RegCloseKey(created);
    if (parent)
        RegCloseKey(parent);
    if (target != key)
        RegCloseKey(target);
    return error;
}

ADVAPI LSTATUS WINAPI RegQueryReflectionKey(HKEY key, BOOL *disabled)
{
    (void)key;
#ifdef _WIN64
    (void)disabled;
    return ERROR_CALL_NOT_IMPLEMENTED;
#else
    if (!disabled)
        return ERROR_INVALID_PARAMETER;
    *disabled = FALSE;
    return ERROR_SUCCESS;
#endif
}

/* ---------------------------------------------------------------- transacted registry keys */

#define ERROR_INVALID_TRANSACTION_ 6700L

/*
 * keys cannot be opened or made inside a transaction, because no transaction can be made: the transaction manager
 * is not there. a handle that is not one fails the way a handle of another kind of object does.
 */
static LSTATUS transacted(HANDLE transaction)
{
    if (!transaction || transaction == INVALID_HANDLE_VALUE)
        return ERROR_INVALID_TRANSACTION_;
    return ERROR_INVALID_HANDLE;
}

ADVAPI LSTATUS WINAPI RegOpenKeyTransactedW(HKEY key, LPCWSTR subkey, DWORD options, REGSAM access, HKEY *result,
                                            HANDLE transaction, PVOID extended)
{
    if (extended)
        return ERROR_INVALID_PARAMETER;
    return transacted(transaction);
}

ADVAPI LSTATUS WINAPI RegOpenKeyTransactedA(HKEY key, LPCSTR subkey, DWORD options, REGSAM access, HKEY *result,
                                            HANDLE transaction, PVOID extended)
{
    if (extended)
        return ERROR_INVALID_PARAMETER;
    return transacted(transaction);
}

ADVAPI LSTATUS WINAPI RegCreateKeyTransactedW(HKEY key, LPCWSTR subkey, DWORD reserved, LPWSTR key_class, DWORD options,
                                              REGSAM access, PVOID security, HKEY *result, LPDWORD disposition,
                                              HANDLE transaction, PVOID extended)
{
    if (reserved || extended)
        return ERROR_INVALID_PARAMETER;
    return transacted(transaction);
}

ADVAPI LSTATUS WINAPI RegCreateKeyTransactedA(HKEY key, LPCSTR subkey, DWORD reserved, LPSTR key_class, DWORD options,
                                              REGSAM access, PVOID security, HKEY *result, LPDWORD disposition,
                                              HANDLE transaction, PVOID extended)
{
    if (reserved || extended)
        return ERROR_INVALID_PARAMETER;
    return transacted(transaction);
}
