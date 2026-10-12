/*
 * event logging, the service control manager and hardware profiles.
 *
 * mime has no event log service and no service control manager: event
 * sources accept every report and write it to the debug output, and opening
 * the service control manager fails the way it does for a caller without
 * access, so programs take their documented fallback.
 */
#include "advapi32_private.h"

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
        return NULL;
    }
    if (!source)
        source = L"";
    SIZE_T n = wcslen(source);
    EVENT_SOURCE *s = AdvAlloc(sizeof(*s) + (n + 1) * sizeof(WCHAR));
    if (!s) {
        AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    s->Magic = EVENT_SOURCE_MAGIC;
    memcpy(s->Name, source, (n + 1) * sizeof(WCHAR));
    return s;
}

ADVAPI HANDLE WINAPI RegisterEventSourceA(LPCSTR server, LPCSTR source)
{
    PWSTR s = AdvAnsiToWide(server), n = AdvAnsiToWide(source);
    HANDLE h = (!server || s) && (!source || n) ? RegisterEventSourceW(s, n) : NULL;
    AdvFree(s);
    AdvFree(n);
    return h;
}

static EVENT_SOURCE *event_source(HANDLE h)
{
    EVENT_SOURCE *s = h;
    if (!s || s->Magic != EVENT_SOURCE_MAGIC) {
        AdvSetError(ERROR_INVALID_HANDLE);
        return NULL;
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
    PWSTR *wide = count ? AdvAllocZero(count * sizeof(PWSTR)) : NULL;
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

/* ---------------------------------------------------------------- service control manager */

typedef HANDLE SC_HANDLE;

#define ERROR_DATABASE_DOES_NOT_EXIST 1065L

ADVAPI SC_HANDLE WINAPI OpenSCManagerW(LPCWSTR machine, LPCWSTR database, DWORD access)
{
    if (database && _wcsicmp(database, L"ServicesActive") && _wcsicmp(database, L"ServicesFailed")) {
        AdvSetError(ERROR_DATABASE_DOES_NOT_EXIST);
        return NULL;
    }
    AdvSetError(AdvIsLocalSystem(machine) ? ERROR_ACCESS_DENIED : RPC_S_SERVER_UNAVAILABLE);
    return NULL;
}

ADVAPI SC_HANDLE WINAPI OpenSCManagerA(LPCSTR machine, LPCSTR database, DWORD access)
{
    PWSTR m = AdvAnsiToWide(machine), d = AdvAnsiToWide(database);
    SC_HANDLE h = (!machine || m) && (!database || d) ? OpenSCManagerW(m, d, access) : NULL;
    AdvFree(m);
    AdvFree(d);
    return h;
}

/* no manager handle can exist, so every handle given here is invalid. */
ADVAPI SC_HANDLE WINAPI OpenServiceW(SC_HANDLE manager, LPCWSTR name, DWORD access)
{
    AdvSetError(ERROR_INVALID_HANDLE);
    return NULL;
}

ADVAPI SC_HANDLE WINAPI OpenServiceA(SC_HANDLE manager, LPCSTR name, DWORD access)
{
    AdvSetError(ERROR_INVALID_HANDLE);
    return NULL;
}

ADVAPI BOOL WINAPI CloseServiceHandle(SC_HANDLE handle)
{
    AdvSetError(ERROR_INVALID_HANDLE);
    return FALSE;
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
    HKEY db = NULL, profile = NULL, dock = NULL;
    DWORD current = 1, size = sizeof(current);
    WCHAR path[64];
    BOOL ok = !RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"System\\CurrentControlSet\\Control\\IDConfigDB", 0,
                             KEY_QUERY_VALUE, &db) &&
              !RegQueryValueExW(db, L"CurrentConfig", NULL, NULL, (LPBYTE)&current, &size) &&
              _snwprintf(path, RTL_NUMBER_OF(path), L"Hardware Profiles\\%04u", current) > 0 &&
              !RegOpenKeyExW(db, path, 0, KEY_QUERY_VALUE, &profile) &&
              !RegOpenKeyExW(db, L"CurrentDockInfo", 0, KEY_QUERY_VALUE, &dock);
    if (ok) {
        size = sizeof(DWORD);
        if (RegQueryValueExW(dock, L"DockingState", NULL, NULL, (LPBYTE)&info->dwDockInfo, &size) ||
            !info->dwDockInfo || (info->dwDockInfo & 3) == 3) {
            size = sizeof(DWORD);
            if (RegQueryValueExW(profile, L"DockState", NULL, NULL, (LPBYTE)&info->dwDockInfo, &size) ||
                !info->dwDockInfo)
                info->dwDockInfo = DOCKINFO_UNDOCKED | DOCKINFO_DOCKED | DOCKINFO_USER_SUPPLIED;
        }
        size = sizeof(info->szHwProfileGuid);
        if (RegQueryValueExW(profile, L"HwProfileGuid", NULL, NULL, (LPBYTE)info->szHwProfileGuid, &size))
            info->szHwProfileGuid[0] = 0;
        info->szHwProfileGuid[RTL_NUMBER_OF(info->szHwProfileGuid) - 1] = 0;
        size = sizeof(info->szHwProfileName);
        if (RegQueryValueExW(profile, L"FriendlyName", NULL, NULL, (LPBYTE)info->szHwProfileName, &size))
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
                             NULL, NULL) ||
        !WideCharToMultiByte(CP_ACP, 0, w.szHwProfileName, -1, info->szHwProfileName, sizeof(info->szHwProfileName),
                             NULL, NULL)) {
        AdvSetError(ERROR_INTERNAL_ERROR);
        return FALSE;
    }
    return TRUE;
}
