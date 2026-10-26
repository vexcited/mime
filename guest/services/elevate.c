/*
 * services.exe: the elevation broker, what the appinfo service does on windows for runas and requireAdministrator.
 * mime has no consent prompt: requests are approved as if the policy were never to notify, and the host log says so.
 *
 * the process gets the caller's linked token, in the caller's session, with the caller as parent. an already
 * elevated caller gets its own token.
 *
 * the same broker starts the servers a class says run as the interactive user, as dcom's launcher does: the process
 * gets the limited half of an elevated caller's token, or the caller's own when it is not elevated.
 */
#include "services.h"

#define SecurityImpersonation_ 2
#define TokenElevationType_ 18
#define TokenLinkedToken_ 19
#define TokenElevation_ 20
#define TokenSessionId_ 12
#define ELEVATION_DEFAULT 1
#define ELEVATION_FULL 2
#define ELEVATION_LIMITED 3
#define PROC_THREAD_ATTRIBUTE_PARENT_PROCESS_ 0x00020000
#define PROCESS_CREATE_PROCESS_ 0x0080
#define PROCESS_DUP_HANDLE_ 0x0040
#define PROCESS_QUERY_LIMITED_INFORMATION_ 0x1000
#define CREATE_DEFAULT_ERROR_MODE_ 0x04000000

typedef struct {
    HANDLE Token;
} LINKED_TOKEN;

typedef BOOL(WINAPI *CREATE_ENVIRONMENT)(LPVOID *environment, HANDLE token, BOOL inherit);
typedef BOOL(WINAPI *DESTROY_ENVIRONMENT)(LPVOID environment);

/* the client's effective token at the time of the call. */
static HANDLE client_token(HANDLE pipe, DWORD *error)
{
    if (!ImpersonateNamedPipeClient(pipe)) {
        *error = GetLastError();
        return nullptr;
    }
    HANDLE token = nullptr;
    BOOL ok = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY | TOKEN_DUPLICATE, TRUE, &token);
    *error = ok ? ERROR_SUCCESS : GetLastError();
    RevertToSelf();
    return token;
}

static DWORD token_dword(HANDLE token, DWORD klass, DWORD fallback)
{
    DWORD value = fallback, returned;
    if (!GetTokenInformation(token, klass, &value, sizeof(value), &returned))
        return fallback;
    return value;
}

/* the host's user always consents unless the policy declines raising programs. */
static BOOL consent_given(void)
{
    HKEY key;
    DWORD declined = 0, size = sizeof(declined), type = 0;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        if (RegQueryValueExW(key, L"DeclineElevationPrompts", nullptr, &type, (BYTE *)&declined, &size) != ERROR_SUCCESS || type != REG_DWORD)
            declined = 0;
        RegCloseKey(key);
    }
    return !declined;
}

/* a primary token for the new process, or NULL with an error. */
static HANDLE elevated_token(HANDLE caller, DWORD *error, DWORD *elevation)
{
    DWORD type = token_dword(caller, TokenElevationType_, ELEVATION_DEFAULT);
    HANDLE source = caller;
    LINKED_TOKEN linked = {};
    DWORD returned;
    if (type == ELEVATION_LIMITED) {
        /* the linked token is usable as a primary token only when queried by the system, as here. */
        if (!GetTokenInformation(caller, TokenLinkedToken_, &linked, sizeof(linked), &returned)) {
            *error = GetLastError();
            return nullptr;
        }
        source = linked.Token;
        *elevation = ELEVATION_FULL;
    } else if (type == ELEVATION_FULL || token_dword(caller, TokenElevation_, 0)) {
        /* already elevated: the process gets the caller's token. */
        *elevation = type == ELEVATION_FULL ? ELEVATION_FULL : ELEVATION_DEFAULT;
    } else {
        /* no administrator behind this token, and no prompt to ask for credentials. */
        *error = ERROR_CANCELLED;
        return nullptr;
    }
    HANDLE primary = nullptr;
    if (!DuplicateTokenEx(source, TOKEN_ALL_ACCESS, nullptr, SecurityImpersonation_, TokenPrimary, &primary)) {
        *error = GetLastError();
        primary = nullptr;
    }
    if (linked.Token)
        CloseHandle(linked.Token);
    return primary;
}

/* a primary token with the rights of the user without those of an administrator, or NULL with an error. */
static HANDLE limited_token(HANDLE caller, DWORD *error)
{
    DWORD type = token_dword(caller, TokenElevationType_, ELEVATION_DEFAULT);
    HANDLE source = caller;
    LINKED_TOKEN linked = {};
    DWORD returned;
    if (type == ELEVATION_FULL) {
        /* the filtered half of the pair, a primary token as the system asks for it. */
        if (!GetTokenInformation(caller, TokenLinkedToken_, &linked, sizeof(linked), &returned)) {
            *error = GetLastError();
            return nullptr;
        }
        source = linked.Token;
    }
    HANDLE primary = nullptr;
    if (!DuplicateTokenEx(source, TOKEN_ALL_ACCESS, nullptr, SecurityImpersonation_, TokenPrimary, &primary)) {
        *error = GetLastError();
        primary = nullptr;
    }
    if (linked.Token)
        CloseHandle(linked.Token);
    return primary;
}

/* the returned handles live in the client's process. */
DWORD ElevateLaunch(HANDLE pipe, DWORD pid, const ELEVATE_REQUEST *q, ELEVATE_RESULT *out)
{
    DWORD error;
    HANDLE caller = client_token(pipe, &error);
    if (!caller)
        return error;
    DWORD elevation = ELEVATION_DEFAULT;
    HANDLE token;
    if (q->Unelevated) {
        token = limited_token(caller, &error);
        CloseHandle(caller);
        if (!token) {
            SvcLog("interactive user: %ls for process %lu has no token to give: %lu", q->CommandLine, pid, error);
            return error;
        }
        SvcLog("interactive user: %ls is started for process %lu with the unelevated token", q->CommandLine, pid);
    } else {
        token = elevated_token(caller, &error, &elevation);
        CloseHandle(caller);
        if (!token) {
            SvcLog("elevation: %ls for process %lu is refused, the token has no administrator: %lu", q->CommandLine, pid, error);
            return error;
        }
        if (!consent_given()) {
            SvcLog("elevation: %ls for process %lu was declined by the policy of the machine", q->CommandLine, pid);
            CloseHandle(token);
            return ERROR_CANCELLED;
        }
        SvcLog("elevation: %ls for process %lu is approved without a prompt (no consent interface here)", q->CommandLine, pid);
    }

    HANDLE client = OpenProcess(PROCESS_DUP_HANDLE_ | PROCESS_CREATE_PROCESS_ | PROCESS_QUERY_LIMITED_INFORMATION_, FALSE, pid);
    if (!client) {
        error = GetLastError();
        CloseHandle(token);
        return error;
    }
    AccessEnablePrivilege(L"SeAssignPrimaryTokenPrivilege");
    AccessEnablePrivilege(L"SeIncreaseQuotaPrivilege");

    /* the token user's environment, not the caller's modified one. */
    LPVOID environment = nullptr;
    HMODULE userenv = LoadLibraryW(L"userenv.dll");
    CREATE_ENVIRONMENT create_environment = userenv ? (CREATE_ENVIRONMENT)GetProcAddress(userenv, "CreateEnvironmentBlock") : nullptr;
    DESTROY_ENVIRONMENT destroy_environment = userenv ? (DESTROY_ENVIRONMENT)GetProcAddress(userenv, "DestroyEnvironmentBlock") : nullptr;
    BOOL made_environment = create_environment && destroy_environment && create_environment(&environment, token, FALSE);
    STARTUPINFOEXW startup = {};
    startup.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    startup.StartupInfo.dwFlags = q->StartupFlags;
    startup.StartupInfo.wShowWindow = (WORD)q->Show;
    startup.StartupInfo.lpDesktop = q->Desktop ? q->Desktop : (WCHAR *)L"WinSta0\\Default";
    startup.StartupInfo.lpTitle = q->Title;
    SIZE_T list_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &list_size);
    LPPROC_THREAD_ATTRIBUTE_LIST list = list_size ? SvcAlloc(list_size) : nullptr;
    DWORD flags = CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_CONSOLE | (q->CreationFlags & (CREATE_NEW_PROCESS_GROUP | CREATE_SUSPENDED | CREATE_DEFAULT_ERROR_MODE_));
    if (list && InitializeProcThreadAttributeList(list, 1, 0, &list_size) &&
        UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_PARENT_PROCESS_, &client, sizeof(client), nullptr, nullptr)) {
        startup.lpAttributeList = list;
        flags |= EXTENDED_STARTUPINFO_PRESENT;
    }
    WCHAR *line = SvcDup(q->CommandLine);
    PROCESS_INFORMATION pi = {};
    BOOL ok = line && CreateProcessAsUserW(token, q->Application, line, nullptr, nullptr, FALSE, flags,
                                           made_environment ? environment : q->Environment, q->Directory,
                                           &startup.StartupInfo, &pi);
    error = ok ? ERROR_SUCCESS : GetLastError();
    if (list)
        DeleteProcThreadAttributeList(list);
    SvcFree(list);
    SvcFree(line);
    if (made_environment)
        destroy_environment(environment);
    CloseHandle(token);
    if (ok) {
        HANDLE process = nullptr, thread = nullptr;
        if (DuplicateHandle(GetCurrentProcess(), pi.hProcess, client, &process, 0, FALSE, DUPLICATE_SAME_ACCESS) &&
            DuplicateHandle(GetCurrentProcess(), pi.hThread, client, &thread, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
            out->ProcessId = pi.dwProcessId;
            out->ThreadId = pi.dwThreadId;
            out->Process = (UINT64)(ULONG_PTR)process;
            out->Thread = (UINT64)(ULONG_PTR)thread;
            out->Elevation = elevation;
        } else {
            error = GetLastError();
            TerminateProcess(pi.hProcess, error);
            if (process)
                DuplicateHandle(client, process, nullptr, nullptr, 0, FALSE, DUPLICATE_CLOSE_SOURCE);
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    CloseHandle(client);
    return error;
}
