/*
 * remote desktop session queries for the console and service sessions. there is no terminal server, so connection,
 * shadowing, licensing and remote management return ERROR_NOT_SUPPORTED. some tamper-protected programs require this
 * DLL to exist as a readable file.
 */
#include <mime/nt.h>
#include <mime/ntrtl.h>
#include <mime/win32/winbase.h>

#define WINSTA DECLSPEC_EXPORT

#define ERROR_CTX_WINSTATION_NOT_FOUND 7022L

#define WINSTATION_NAME_CHARS 32
#define WINSTATION_ACTIVE 0
#define WINSTATION_DISCONNECTED 4
#define SERVER_MAGIC 0x57534554 /* 'WSET'. */

typedef struct SERVER {
    DWORD Magic;
} SERVER;

typedef struct _SESSIONIDW {
    ULONG SessionId;
    WCHAR WinStationName[WINSTATION_NAME_CHARS + 1];
    ULONG State;
} SESSIONIDW;

static_assert(sizeof(SESSIONIDW) == 76, "SESSIONIDW layout");

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

static BOOL valid_server(HANDLE server)
{
    return server == nullptr || (server != INVALID_HANDLE_VALUE && ((SERVER *)server)->Magic == SERVER_MAGIC);
}

/* the console uses the process's session ID; services use session 0. */
static BOOL describe(ULONG id, PCWSTR *name, ULONG *state)
{
    if (id == 0) {
        *name = L"Services";
        *state = WINSTATION_DISCONNECTED;
        return TRUE;
    }
    if (id == NtCurrentPeb()->SessionId) {
        *name = L"Console";
        *state = WINSTATION_ACTIVE;
        return TRUE;
    }
    return FALSE;
}

WINSTA HANDLE WINAPI WinStationOpenServerW(PWSTR name)
{
    /* only this machine has sessions. */
    if (name && *name) {
        WCHAR local[MAX_COMPUTERNAME_LENGTH + 1];
        DWORD n = RTL_NUMBER_OF(local);
        if (!GetComputerNameW(local, &n) || _wcsicmp(local, name)) {
            SetLastError(ERROR_NOT_SUPPORTED);
            return nullptr;
        }
    }
    SERVER *server = LocalAlloc(LMEM_FIXED, sizeof(*server));
    if (!server)
        return nullptr;
    server->Magic = SERVER_MAGIC;
    return server;
}

WINSTA BOOLEAN WINAPI WinStationCloseServer(HANDLE server)
{
    if (!server || server == INVALID_HANDLE_VALUE || ((SERVER *)server)->Magic != SERVER_MAGIC) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    ((SERVER *)server)->Magic = 0;
    LocalFree(server);
    return TRUE;
}

WINSTA BOOLEAN WINAPI WinStationFreeMemory(PVOID memory)
{
    if (memory)
        LocalFree(memory);
    return TRUE;
}

WINSTA BOOLEAN WINAPI WinStationEnumerateW(HANDLE server, SESSIONIDW **sessions, ULONG *count)
{
    if (!valid_server(server) || !sessions || !count) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ULONG ids[2] = {0, NtCurrentPeb()->SessionId};
    ULONG n = ids[1] && ids[1] != ids[0] ? 2 : 1;
    SESSIONIDW *out = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, n * sizeof(*out));
    if (!out)
        return FALSE;
    for (ULONG i = 0; i < n; i++) {
        PCWSTR name;
        describe(ids[i], &name, &out[i].State);
        out[i].SessionId = ids[i];
        wcscpy(out[i].WinStationName, name);
    }
    *sessions = out;
    *count = n;
    return TRUE;
}

WINSTA BOOLEAN WINAPI WinStationNameFromLogonIdW(HANDLE server, ULONG id, PWSTR name)
{
    PCWSTR text;
    ULONG state;
    if (!valid_server(server) || !name) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!describe(id, &text, &state)) {
        SetLastError(ERROR_CTX_WINSTATION_NOT_FOUND);
        return FALSE;
    }
    wcscpy(name, text);
    return TRUE;
}

WINSTA BOOLEAN WINAPI WinStationNameFromLogonIdA(HANDLE server, ULONG id, PSTR name)
{
    WCHAR wide[WINSTATION_NAME_CHARS + 1];
    if (!name || !WinStationNameFromLogonIdW(server, id, wide))
        return FALSE;
    WideCharToMultiByte(CP_ACP, 0, wide, -1, name, WINSTATION_NAME_CHARS + 1, nullptr, nullptr);
    return TRUE;
}

WINSTA BOOLEAN WINAPI LogonIdFromWinStationNameW(HANDLE server, PCWSTR name, PULONG id)
{
    if (!valid_server(server) || !name || !id) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ULONG ids[2] = {0, NtCurrentPeb()->SessionId};
    for (int i = 0; i < 2; i++) {
        PCWSTR text;
        ULONG state;
        if (describe(ids[i], &text, &state) && !_wcsicmp(text, name)) {
            *id = ids[i];
            return TRUE;
        }
    }
    SetLastError(ERROR_CTX_WINSTATION_NOT_FOUND);
    return FALSE;
}

WINSTA BOOLEAN WINAPI LogonIdFromWinStationNameA(HANDLE server, PCSTR name, PULONG id)
{
    WCHAR wide[WINSTATION_NAME_CHARS + 1];
    if (!name || !MultiByteToWideChar(CP_ACP, 0, name, -1, wide, RTL_NUMBER_OF(wide))) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return LogonIdFromWinStationNameW(server, wide, id);
}

/* no session is a remote one. */
WINSTA BOOLEAN WINAPI WinStationIsCurrentSessionRemoteable(PBOOLEAN remoteable)
{
    if (!remoteable) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *remoteable = FALSE;
    return TRUE;
}

WINSTA BOOLEAN WINAPI WinStationIsSessionRemoteable(HANDLE server, ULONG id, PBOOLEAN remoteable)
{
    PCWSTR name;
    ULONG state;
    if (!valid_server(server) || !remoteable) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!describe(id, &name, &state)) {
        SetLastError(ERROR_CTX_WINSTATION_NOT_FOUND);
        return FALSE;
    }
    *remoteable = FALSE;
    return TRUE;
}

/* the user of the console is logged on; nobody is on the services' session. */
WINSTA BOOLEAN WINAPI WinStationGetLoggedOnCount(HANDLE server, PULONG count)
{
    if (!valid_server(server) || !count) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *count = NtCurrentPeb()->SessionId ? 1 : 0;
    return TRUE;
}

WINSTA BOOLEAN WINAPI WinStationVerify(HANDLE server, ULONG id)
{
    PCWSTR name;
    ULONG state;
    return valid_server(server) && describe(id, &name, &state);
}

WINSTA BOOLEAN WINAPI WinStationQueryInformationW(HANDLE server, ULONG id, ULONG klass, PVOID info, ULONG length,
                                                  PULONG returned)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

/*
 * measured on windows 11 26100, in both widths: only capability 1 can be asked, the redirection of the session's
 * devices to a remote client, which a local session answers with 0 and a last error of 0. any other gives
 * ERROR_INVALID_PARAMETER and a zero.
 */
WINSTA BOOLEAN WINAPI WinStationGetCurrentSessionCapabilities(ULONG capability, PULONG value)
{
    *value = 0;
    if (capability != 1) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

/* an event a program wants set when its session connects, disconnects or changes its user. */
typedef struct NOTIFICATION {
    struct NOTIFICATION *Next;
    HANDLE Event;
    ULONG Mask;
} NOTIFICATION;

static SRWLOCK g_notifications_lock = SRWLOCK_INIT;
static NOTIFICATION *g_notifications;

/*
 * the registration is kept and named by a cookie, any mask is taken, and a call without an event fails and leaves the
 * last error alone. the session of a mime machine stays connected to its console with one user, so nothing sets the
 * events.
 */
WINSTA BOOLEAN WINAPI WinStationRegisterCurrentSessionNotificationEvent(HANDLE event, ULONG mask, PULONG_PTR cookie)
{
    if (!event)
        return FALSE;
    NOTIFICATION *n = HeapAlloc(GetProcessHeap(), 0, sizeof(*n));
    if (!n) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    n->Event = event;
    n->Mask = mask;
    AcquireSRWLockExclusive(&g_notifications_lock);
    n->Next = g_notifications;
    g_notifications = n;
    ReleaseSRWLockExclusive(&g_notifications_lock);
    *cookie = (ULONG_PTR)n;
    return TRUE;
}

/* windows fails fast on a cookie it did not give, here that is an error. */
WINSTA BOOLEAN WINAPI WinStationUnRegisterNotificationEvent(ULONG_PTR cookie)
{
    NOTIFICATION *found = nullptr;
    AcquireSRWLockExclusive(&g_notifications_lock);
    for (NOTIFICATION **p = &g_notifications; *p; p = &(*p)->Next) {
        if ((ULONG_PTR)*p == cookie) {
            found = *p;
            *p = found->Next;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_notifications_lock);
    if (!found) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    HeapFree(GetProcessHeap(), 0, found);
    return TRUE;
}

/* everything else the library exports needs a terminal server. */
BOOLEAN WINAPI WinStationUnsupported(void)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}
