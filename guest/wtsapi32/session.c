/*
 * one interactive console session and session 0 for services. console client information describes the local
 * machine. returned buffers use LocalAlloc and are released by WTSFreeMemory.
 */
#include <mime/nt.h>
#include <mime/ntrtl.h>
#include <mime/win32/winbase.h>
#include <mime/win32/winnls.h>
#include <mime/win32/winuser.h>
#include "wts_private.h"

#define WTSAPI DECLSPEC_EXPORT

WINUSERAPI DWORD WINAPI GetWindowThreadProcessId(HWND window, LPDWORD process);

#define WTS_CURRENT_SERVER_HANDLE ((HANDLE)NULL)
#define WTS_CURRENT_SESSION ((DWORD)-1)
#define ERROR_INVALID_USER_BUFFER 1784L
#define ERROR_WINDOW_OF_OTHER_THREAD 1408L
#define ERROR_PRIVILEGE_NOT_HELD 1314L
#define ERROR_CTX_WINSTATION_NOT_FOUND 7022L

typedef enum _WTS_CONNECTSTATE_CLASS {
    WTSActive,
    WTSConnected,
    WTSConnectQuery,
    WTSShadow,
    WTSDisconnected,
    WTSIdle,
    WTSListen,
    WTSReset,
    WTSDown,
    WTSInit,
} WTS_CONNECTSTATE_CLASS;

typedef enum _WTS_INFO_CLASS {
    WTSInitialProgram,
    WTSApplicationName,
    WTSWorkingDirectory,
    WTSOEMId,
    WTSSessionId,
    WTSUserName,
    WTSWinStationName,
    WTSDomainName,
    WTSConnectState,
    WTSClientBuildNumber,
    WTSClientName,
    WTSClientDirectory,
    WTSClientProductId,
    WTSClientHardwareId,
    WTSClientAddress,
    WTSClientDisplay,
    WTSClientProtocolType,
    WTSIdleTime,
    WTSLogonTime,
    WTSIncomingBytes,
    WTSOutgoingBytes,
    WTSIncomingFrames,
    WTSOutgoingFrames,
    WTSClientInfo,
    WTSSessionInfo,
    WTSSessionInfoEx,
    WTSConfigInfo,
    WTSValidationInfo,
    WTSSessionAddressV4,
    WTSIsRemoteSession,
} WTS_INFO_CLASS;

typedef struct _WTSINFOW {
    WTS_CONNECTSTATE_CLASS State;
    DWORD SessionId;
    DWORD IncomingBytes;
    DWORD OutgoingBytes;
    DWORD IncomingFrames;
    DWORD OutgoingFrames;
    DWORD IncomingCompressedBytes;
    DWORD OutgoingCompressedBytes;
    WCHAR WinStationName[32];
    WCHAR Domain[17];
    WCHAR UserName[21];
    LARGE_INTEGER ConnectTime;
    LARGE_INTEGER DisconnectTime;
    LARGE_INTEGER LastInputTime;
    LARGE_INTEGER LogonTime;
    LARGE_INTEGER CurrentTime;
} WTSINFOW;

typedef struct _WTSINFOEX_LEVEL1_W {
    ULONG SessionId;
    WTS_CONNECTSTATE_CLASS SessionState;
    LONG SessionFlags;
    WCHAR WinStationName[33];
    WCHAR UserName[21];
    WCHAR DomainName[18];
    LARGE_INTEGER LogonTime;
    LARGE_INTEGER ConnectTime;
    LARGE_INTEGER DisconnectTime;
    LARGE_INTEGER LastInputTime;
    LARGE_INTEGER CurrentTime;
    DWORD IncomingBytes;
    DWORD OutgoingBytes;
    DWORD IncomingFrames;
    DWORD OutgoingFrames;
    DWORD IncomingCompressedBytes;
    DWORD OutgoingCompressedBytes;
} WTSINFOEX_LEVEL1_W;

typedef struct _WTSINFOEXW {
    DWORD Level;
    union {
        WTSINFOEX_LEVEL1_W WTSInfoExLevel1;
    } Data;
} WTSINFOEXW;

typedef struct _WTSCLIENTW {
    WCHAR ClientName[21];
    WCHAR Domain[18];
    WCHAR UserName[21];
    WCHAR WorkDirectory[261];
    WCHAR InitialProgram[261];
    BYTE EncryptionLevel;
    ULONG ClientAddressFamily;
    USHORT ClientAddress[31];
    USHORT HRes;
    USHORT VRes;
    USHORT ColorDepth;
    WCHAR ClientDirectory[261];
    ULONG ClientBuildNumber;
    ULONG ClientHardwareId;
    USHORT ClientProductId;
    USHORT OutBufCountHost;
    USHORT OutBufCountClient;
    USHORT OutBufLength;
    WCHAR DeviceId[261];
} WTSCLIENTW;

typedef struct _WTSCONFIGINFOW {
    ULONG version;
    ULONG fConnectClientDrivesAtLogon;
    ULONG fConnectPrinterAtLogon;
    ULONG fDisablePrinterRedirection;
    ULONG fDisableDefaultMainClientPrinter;
    ULONG ShadowSettings;
    WCHAR LogonUserName[21];
    WCHAR LogonDomain[18];
    WCHAR WorkDirectory[261];
    WCHAR InitialProgram[261];
    WCHAR ApplicationName[261];
} WTSCONFIGINFOW;

typedef struct _WTS_SESSION_INFOW {
    DWORD SessionId;
    LPWSTR pWinStationName;
    WTS_CONNECTSTATE_CLASS State;
} WTS_SESSION_INFOW;

typedef struct _WTS_CLIENT_DISPLAY {
    DWORD HorizontalResolution;
    DWORD VerticalResolution;
    DWORD ColorDepth;
} WTS_CLIENT_DISPLAY;

static_assert(sizeof(WTSINFOW) == 216, "WTSINFOW layout");
static_assert(offsetof(WTSCLIENTW, DeviceId) == 1780, "WTSCLIENTW layout");

FORCEINLINE void set_error(DWORD error)
{
    NtCurrentTeb()->LastErrorValue = error;
}

/* ---------------------------------------------------------------- the sessions */

typedef struct SESSION {
    DWORD Id;
    WTS_CONNECTSTATE_CLASS State;
    WCHAR Station[33];
    WCHAR User[21];
    WCHAR Domain[18];
    LARGE_INTEGER LogonTime;
    LARGE_INTEGER CurrentTime;
} SESSION;

static DWORD current_session(void)
{
    return NtCurrentPeb()->SessionId;
}

static void copy_text(PWSTR out, DWORD chars, PCWSTR text)
{
    DWORD i = 0;
    for (; text && text[i] && i + 1 < chars; i++)
        out[i] = text[i];
    out[i] = 0;
}

/* the user of the console is the user running this process, as advapi32 names it. */
static void session_user(SESSION *s)
{
    typedef BOOL(WINAPI * GET_USER_NAME)(LPWSTR, LPDWORD);
    HMODULE advapi = LoadLibraryW(L"advapi32.dll");
    GET_USER_NAME get = advapi ? (GET_USER_NAME)(void *)GetProcAddress(advapi, "GetUserNameW") : nullptr;
    WCHAR name[256];
    DWORD n = RTL_NUMBER_OF(name);
    if (get && get(name, &n))
        copy_text(s->User, RTL_NUMBER_OF(s->User), name);
    n = RTL_NUMBER_OF(name);
    if (GetComputerNameW(name, &n))
        copy_text(s->Domain, RTL_NUMBER_OF(s->Domain), name);
    if (advapi)
        FreeLibrary(advapi);
}

/* returns a Win32 error when the session description cannot be filled. */
static DWORD describe(HANDLE server, DWORD id, SESSION *s)
{
    if (!MimeWtsServerValid(server))
        return ERROR_INVALID_HANDLE;
    if (id == WTS_CURRENT_SESSION)
        id = current_session();
    memset(s, 0, sizeof(*s));
    s->Id = id;
    if (id == current_session() && id != 0) {
        s->State = WTSActive;
        copy_text(s->Station, RTL_NUMBER_OF(s->Station), L"Console");
        session_user(s);
    } else if (id == 0) {
        s->State = WTSDisconnected;
        copy_text(s->Station, RTL_NUMBER_OF(s->Station), L"Services");
    } else {
        return ERROR_CTX_WINSTATION_NOT_FOUND;
    }
    /* the user logs on as the machine starts. */
    struct {
        LARGE_INTEGER BootTime;
        LARGE_INTEGER CurrentTime;
        BYTE Rest[32];
    } time;
    if (NT_SUCCESS(NtQuerySystemInformation(SystemTimeOfDayInformation, &time, sizeof(time), nullptr))) {
        s->LogonTime = id ? time.BootTime : (LARGE_INTEGER){0};
        s->CurrentTime = time.CurrentTime;
    }
    return ERROR_SUCCESS;
}

/* ---------------------------------------------------------------- queries */

static BOOL give(const void *data, DWORD size, LPWSTR *buffer, DWORD *returned)
{
    void *out = LocalAlloc(LMEM_FIXED, size ? size : 1);
    if (!out)
        return FALSE;
    memcpy(out, data, size);
    *buffer = out;
    *returned = size;
    return TRUE;
}

static BOOL give_text(PCWSTR text, LPWSTR *buffer, DWORD *returned)
{
    return give(text, (DWORD)(wcslen(text) + 1) * sizeof(WCHAR), buffer, returned);
}

WTSAPI BOOL WINAPI WTSQuerySessionInformationW(HANDLE server, DWORD id, WTS_INFO_CLASS klass, LPWSTR *buffer,
                                               DWORD *returned)
{
    if (!buffer || !returned) {
        set_error(ERROR_INVALID_USER_BUFFER);
        return FALSE;
    }
    if ((klass >= WTSIdleTime && klass <= WTSOutgoingFrames) || klass == WTSValidationInfo ||
        klass == WTSSessionAddressV4 || klass == 30 || klass == 31) {
        set_error(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    if ((DWORD)klass > 31) {
        set_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    SESSION s;
    DWORD error = describe(server, id, &s);
    if (error) {
        set_error(error);
        return FALSE;
    }
    switch (klass) {
    case WTSInitialProgram:
    case WTSApplicationName:
        if (s.Id == 0) {
            set_error(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        return give_text(L"", buffer, returned);
    case WTSWorkingDirectory:
    case WTSClientName:
    case WTSClientDirectory:
        return give_text(L"", buffer, returned);
    case WTSOEMId:
        return give("", 1, buffer, returned); /* an ANSI string even here, like windows. */
    case WTSSessionId:
        return give(&s.Id, sizeof(DWORD), buffer, returned);
    case WTSUserName:
        return give_text(s.User, buffer, returned);
    case WTSWinStationName:
        return give_text(s.Station, buffer, returned);
    case WTSDomainName:
        return give_text(s.Domain, buffer, returned);
    case WTSConnectState:
        return give(&s.State, sizeof(DWORD), buffer, returned);
    case WTSClientBuildNumber:
    case WTSClientHardwareId:
        return give(&(DWORD){0}, sizeof(DWORD), buffer, returned);
    case WTSClientProductId:
    case WTSClientProtocolType:
        return give(&(USHORT){0}, sizeof(USHORT), buffer, returned); /* 0, the console. */
    case WTSClientAddress: {
        BYTE address[24] = {}; /* AF_UNSPEC: no remote client. */
        return give(address, sizeof(address), buffer, returned);
    }
    case WTSClientDisplay: {
        WTS_CLIENT_DISPLAY d = {(DWORD)GetSystemMetrics(SM_CXSCREEN), (DWORD)GetSystemMetrics(SM_CYSCREEN), 32};
        return give(&d, sizeof(d), buffer, returned);
    }
    case WTSClientInfo: {
        WTSCLIENTW *c = LocalAlloc(LPTR, sizeof(*c));
        if (!c)
            return FALSE;
        copy_text(c->Domain, RTL_NUMBER_OF(c->Domain), s.Domain);
        copy_text(c->UserName, RTL_NUMBER_OF(c->UserName), s.User);
        c->HRes = (USHORT)GetSystemMetrics(SM_CXSCREEN);
        c->VRes = (USHORT)GetSystemMetrics(SM_CYSCREEN);
        c->ColorDepth = 32;
        *buffer = (LPWSTR)c;
        *returned = sizeof(*c);
        return TRUE;
    }
    case WTSSessionInfo: {
        WTSINFOW *i = LocalAlloc(LPTR, sizeof(*i));
        if (!i)
            return FALSE;
        i->State = s.State;
        i->SessionId = s.Id;
        copy_text(i->WinStationName, RTL_NUMBER_OF(i->WinStationName), s.Station);
        copy_text(i->Domain, RTL_NUMBER_OF(i->Domain), s.Domain);
        copy_text(i->UserName, RTL_NUMBER_OF(i->UserName), s.User);
        i->ConnectTime = s.LogonTime;
        i->LogonTime = s.LogonTime;
        i->LastInputTime = s.CurrentTime;
        i->CurrentTime = s.CurrentTime;
        *buffer = (LPWSTR)i;
        *returned = sizeof(*i);
        return TRUE;
    }
    case WTSSessionInfoEx: {
        WTSINFOEXW *x = LocalAlloc(LPTR, sizeof(*x));
        if (!x)
            return FALSE;
        WTSINFOEX_LEVEL1_W *l = &x->Data.WTSInfoExLevel1;
        x->Level = 1;
        l->SessionId = s.Id;
        l->SessionState = s.State;
        l->SessionFlags = s.Id ? 1 : -1; /* WTS_SESSIONSTATE_UNLOCK, or unknown for session 0. */
        copy_text(l->WinStationName, RTL_NUMBER_OF(l->WinStationName), s.Station);
        copy_text(l->UserName, RTL_NUMBER_OF(l->UserName), s.User);
        copy_text(l->DomainName, RTL_NUMBER_OF(l->DomainName), s.Domain);
        l->LogonTime = s.LogonTime;
        l->ConnectTime = s.LogonTime;
        l->LastInputTime = s.CurrentTime;
        l->CurrentTime = s.CurrentTime;
        *buffer = (LPWSTR)x;
        *returned = sizeof(*x);
        return TRUE;
    }
    case WTSConfigInfo: {
        WTSCONFIGINFOW *c = LocalAlloc(LPTR, sizeof(*c));
        if (!c)
            return FALSE;
        c->version = 1;
        copy_text(c->LogonUserName, RTL_NUMBER_OF(c->LogonUserName), s.User);
        copy_text(c->LogonDomain, RTL_NUMBER_OF(c->LogonDomain), s.Domain);
        *buffer = (LPWSTR)c;
        *returned = sizeof(*c);
        return TRUE;
    }
    case WTSIsRemoteSession:
        return give(&(BOOLEAN){FALSE}, 1, buffer, returned);
    default:
        set_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
}

/* the string classes converted to ANSI; the structure classes have no ANSI form here yet. */
WTSAPI BOOL WINAPI WTSQuerySessionInformationA(HANDLE server, DWORD id, WTS_INFO_CLASS klass, LPSTR *buffer,
                                               DWORD *returned)
{
    if (klass == WTSClientInfo || klass == WTSSessionInfo || klass == WTSSessionInfoEx || klass == WTSConfigInfo) {
        set_error(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    LPWSTR wide;
    DWORD size;
    if (!WTSQuerySessionInformationW(server, id, klass, buffer ? &wide : nullptr, returned ? &size : nullptr))
        return FALSE;
    BOOL text = klass <= WTSDomainName && klass != WTSOEMId && klass != WTSSessionId;
    text |= klass == WTSClientName || klass == WTSClientDirectory;
    if (!text) {
        *buffer = (LPSTR)wide;
        *returned = size;
        return TRUE;
    }
    int n = WideCharToMultiByte(CP_ACP, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    LPSTR out = n > 0 ? LocalAlloc(LMEM_FIXED, n) : nullptr;
    if (out)
        WideCharToMultiByte(CP_ACP, 0, wide, -1, out, n, nullptr, nullptr);
    LocalFree(wide);
    if (!out)
        return FALSE;
    *buffer = out;
    *returned = n;
    return TRUE;
}

/*
 * the processes of the machine as the terminal services list them. there is one machine and no other session to ask,
 * so the list is the snapshot of the system with each process in its session. no process has an owner here.
 */
typedef struct _WTS_PROCESS_INFOW {
    DWORD SessionId;
    DWORD ProcessId;
    LPWSTR pProcessName;
    PSID pUserSid;
} WTS_PROCESS_INFOW;

typedef struct _PROCESSENTRY32W_ {
    DWORD dwSize;
    DWORD cntUsage;
    DWORD th32ProcessID;
    ULONG_PTR th32DefaultHeapID;
    DWORD th32ModuleID;
    DWORD cntThreads;
    DWORD th32ParentProcessID;
    LONG pcPriClassBase;
    DWORD dwFlags;
    WCHAR szExeFile[MAX_PATH];
} PROCESSENTRY32W_;

WINBASEAPI HANDLE WINAPI CreateToolhelp32Snapshot(DWORD flags, DWORD process);
WINBASEAPI BOOL WINAPI Process32FirstW(HANDLE snapshot, PROCESSENTRY32W_ *entry);
WINBASEAPI BOOL WINAPI Process32NextW(HANDLE snapshot, PROCESSENTRY32W_ *entry);

WTSAPI BOOL WINAPI WTSEnumerateProcessesW(HANDLE server, DWORD reserved, DWORD version, WTS_PROCESS_INFOW **processes,
                                          DWORD *count)
{
    if (reserved || version != 1 || !processes || !count) {
        set_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!MimeWtsServerValid(server)) {
        set_error(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    *processes = nullptr;
    *count = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(2, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return FALSE;
    DWORD capacity = 64, n = 0;
    SIZE_T names = 0;
    PROCESSENTRY32W_ *list = nullptr;
    PROCESSENTRY32W_ entry = {sizeof(entry)};
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
        if (n == capacity || !list) {
            PROCESSENTRY32W_ *grown = LocalAlloc(LMEM_FIXED, (capacity *= 2) * sizeof(*list));
            if (!grown) {
                LocalFree(list);
                CloseHandle(snapshot);
                set_error(ERROR_NOT_ENOUGH_MEMORY);
                return FALSE;
            }
            if (list)
                memcpy(grown, list, n * sizeof(*list));
            LocalFree(list);
            list = grown;
        }
        list[n++] = entry;
        names += (wcslen(entry.szExeFile) + 1) * sizeof(WCHAR);
    }
    CloseHandle(snapshot);
    WTS_PROCESS_INFOW *out = LocalAlloc(LPTR, n * sizeof(*out) + names + sizeof(WCHAR));
    if (!out) {
        LocalFree(list);
        set_error(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    PWSTR text = (PWSTR)(out + n);
    for (DWORD i = 0; i < n; i++) {
        DWORD session = 0;
        ProcessIdToSessionId(list[i].th32ProcessID, &session);
        out[i].SessionId = session;
        out[i].ProcessId = list[i].th32ProcessID;
        out[i].pProcessName = text;
        wcscpy(text, list[i].szExeFile);
        text += wcslen(text) + 1;
    }
    LocalFree(list);
    *processes = out;
    *count = n;
    return TRUE;
}

/* a session is ended by the session manager, which this machine does not have for a session to end. */
WTSAPI BOOL WINAPI WTSLogoffSession(HANDLE server, DWORD id, BOOL wait)
{
    if (!MimeWtsServerValid(server)) {
        set_error(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    set_error(ERROR_ACCESS_DENIED);
    return FALSE;
}

WTSAPI BOOL WINAPI WTSDisconnectSession(HANDLE server, DWORD id, BOOL wait)
{
    if (!MimeWtsServerValid(server)) {
        set_error(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    set_error(ERROR_ACCESS_DENIED);
    return FALSE;
}

WTSAPI void WINAPI WTSFreeMemory(PVOID memory)
{
    LocalFree(memory);
}

WTSAPI BOOL WINAPI WTSEnumerateSessionsW(HANDLE server, DWORD reserved, DWORD version, WTS_SESSION_INFOW **sessions,
                                         DWORD *count)
{
    if (reserved || version != 1 || !sessions || !count) {
        set_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!MimeWtsServerValid(server)) {
        set_error(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    DWORD ids[2] = {0, current_session()};
    DWORD n = ids[1] ? 2 : 1;
    SESSION s[2];
    SIZE_T size = n * sizeof(WTS_SESSION_INFOW);
    for (DWORD i = 0; i < n; i++) {
        describe(server, ids[i], &s[i]);
        size += (wcslen(s[i].Station) + 1) * sizeof(WCHAR);
    }
    WTS_SESSION_INFOW *out = LocalAlloc(LPTR, size);
    if (!out)
        return FALSE;
    PWSTR text = (PWSTR)(out + n);
    for (DWORD i = 0; i < n; i++) {
        out[i].SessionId = s[i].Id;
        out[i].State = s[i].State;
        out[i].pWinStationName = text;
        wcscpy(text, s[i].Station);
        text += wcslen(text) + 1;
    }
    *sessions = out;
    *count = n;
    return TRUE;
}

/* only the system account may take a session's token. */
WTSAPI BOOL WINAPI WTSQueryUserToken(ULONG id, PHANDLE token)
{
    set_error(token ? ERROR_PRIVILEGE_NOT_HELD : ERROR_INVALID_PARAMETER);
    return FALSE;
}

/* ---------------------------------------------------------------- notifications */

/*
 * the window must belong to this process. the console session never connects, disconnects or locks, so no
 * WM_WTSSESSION_CHANGE is generated.
 */
static BOOL check_window(HWND window, DWORD flags)
{
    DWORD process = 0;
    if (!IsWindow(window) || flags > 1) {
        set_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId()) {
        set_error(ERROR_WINDOW_OF_OTHER_THREAD);
        return FALSE;
    }
    return TRUE;
}

WTSAPI BOOL WINAPI WTSRegisterSessionNotification(HWND window, DWORD flags)
{
    return check_window(window, flags);
}

WTSAPI BOOL WINAPI WTSRegisterSessionNotificationEx(HANDLE server, HWND window, DWORD flags)
{
    if (!MimeWtsServerValid(server)) {
        set_error(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return check_window(window, flags);
}

WTSAPI BOOL WINAPI WTSUnRegisterSessionNotification(HWND window)
{
    return check_window(window, 0);
}

WTSAPI BOOL WINAPI WTSUnRegisterSessionNotificationEx(HANDLE server, HWND window)
{
    if (!MimeWtsServerValid(server)) {
        set_error(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return check_window(window, 0);
}
