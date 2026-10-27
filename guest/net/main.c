/*
 * net.exe: the commands of the network tool that batch files use without a network.
 *
 *   net start [service]       net stop service     net pause service     net continue service
 *   net session               net helpmsg number    net use
 *
 * `net session` succeeds when the caller runs with the full administrator token and is refused with system error 5
 * otherwise, which is how scripts find out whether they were elevated. the exit code is 0, 1 for a wrong command line
 * and 2 for an error.
 */
#include "tool.h"
#include <mime/win32/winsvc.h>

#define SC_MANAGER_CONNECT_ 0x1
#define SC_MANAGER_ENUMERATE_SERVICE_ 0x4
#define SERVICE_QUERY_STATUS_ 0x4
#define SERVICE_START_ 0x10
#define SERVICE_STOP_ 0x20
#define SERVICE_PAUSE_CONTINUE_ 0x40
#define SERVICE_ENUMERATE_DEPENDENTS_ 0x8
#define TOKEN_QUERY_ 0x8
#define TokenElevation_ 20

WINBASEAPI BOOL WINAPI OpenProcessToken(HANDLE process, DWORD access, PHANDLE token);
WINBASEAPI BOOL WINAPI GetTokenInformation(HANDLE token, int klass, LPVOID info, DWORD length, PDWORD returned);
WINBASEAPI SC_HANDLE WINAPI OpenSCManagerW(LPCWSTR machine, LPCWSTR database, DWORD access);

static void say_text(const WCHAR *text)
{
    tu_puts(text);
}

static int syntax(const WCHAR *command)
{
    tu_print(L"The syntax of this command is:\n\n%s\n", command);
    return 1;
}

static int completed(void)
{
    say_text(L"The command completed successfully.\n\n");
    return 0;
}

/* a failure the way net tells it: the system error, its text, and the pointer to the help. */
static int system_error(DWORD error)
{
    TU_STR text = {};
    tu_error_text(error, &text);
    tu_eprint(L"System error %lu has occurred.\n\n%s\n\n", error, tu_str_text(&text));
    tu_str_free(&text);
    return 2;
}

/* a message of the network tools, and the line that points to net helpmsg. */
static int network_error(DWORD number, const WCHAR *text)
{
    tu_eprint(L"%s\n\nMore help is available by typing NET HELPMSG %lu.\n\n", text, number);
    return 2;
}

static BOOL is_elevated(void)
{
    HANDLE token;
    BOOL elevated = FALSE;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY_, &token)) {
        DWORD value = 0, size = 0;
        if (GetTokenInformation(token, TokenElevation_, &value, sizeof(value), &size))
            elevated = value != 0;
        CloseHandle(token);
    }
    return elevated;
}

/* ------------------------------------------------------------------ services */

static const WCHAR *state_failure(DWORD error, DWORD *number)
{
    switch (error) {
    case ERROR_SERVICE_ALREADY_RUNNING:
        *number = 2182;
        return L"The requested service has already been started.";
    case ERROR_SERVICE_NOT_ACTIVE:
        *number = 3521;
        return nullptr;
    case ERROR_SERVICE_CANNOT_ACCEPT_CTRL:
        *number = 2189;
        return L"The service could not be controlled in its present state.";
    case ERROR_SERVICE_DOES_NOT_EXIST:
        *number = 2185;
        return L"The service name is invalid.";
    default:
        *number = 0;
        return nullptr;
    }
}

/* the key name of a service from the name the user typed, which may be the display name. */
static WCHAR *resolve_service(SC_HANDLE manager, const WCHAR *name)
{
    SC_HANDLE service = OpenServiceW(manager, name, SERVICE_QUERY_STATUS_);
    if (service) {
        CloseServiceHandle(service);
        return tu_dup(name);
    }
    WCHAR key[256];
    DWORD size = TU_ARRAY(key);
    if (GetServiceKeyNameW(manager, name, key, &size))
        return tu_dup(key);
    return nullptr;
}

static WCHAR *display_name_of(SC_HANDLE manager, const WCHAR *key)
{
    WCHAR display[256];
    DWORD size = TU_ARRAY(display);
    if (GetServiceDisplayNameW(manager, key, display, &size))
        return tu_dup(display);
    return tu_dup(key);
}

static int failure_of(DWORD error, const WCHAR *display, const WCHAR *what_failed)
{
    DWORD number;
    const WCHAR *text = state_failure(error, &number);
    if (number == 3521) {
        tu_eprint(L"The %s service is not started.\n\nMore help is available by typing NET HELPMSG 3521.\n\n", display);
        return 2;
    }
    if (text)
        return network_error(number, text);
    if (what_failed)
        tu_eprint(L"%s\n\n", what_failed);
    return system_error(error);
}

static int list_started(void)
{
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE_);
    if (!manager)
        return system_error(GetLastError());
    DWORD size = 1 << 16, resume = 0;
    BYTE *buffer = tu_alloc(size);
    TU_TABLE names;
    static const TU_COLUMN column[] = {{L"", 0, FALSE}};
    tu_table_init(&names, column, 1);
    for (;;) {
        DWORD needed = 0, returned = 0;
        BOOL ok = EnumServicesStatusExW(manager, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_ACTIVE, buffer, size,
                                        &needed, &returned, &resume, nullptr);
        DWORD error = ok ? 0 : GetLastError();
        if (error && error != ERROR_MORE_DATA) {
            tu_free(buffer);
            CloseServiceHandle(manager);
            return system_error(error);
        }
        ENUM_SERVICE_STATUS_PROCESSW *list = (ENUM_SERVICE_STATUS_PROCESSW *)buffer;
        for (DWORD i = 0; i < returned; i++) {
            const WCHAR *cells[1] = {list[i].lpDisplayName ? list[i].lpDisplayName : list[i].lpServiceName};
            tu_table_row(&names, cells);
        }
        if (error != ERROR_MORE_DATA)
            break;
        if (!returned) {
            size = needed + 1024;
            buffer = tu_realloc(buffer, size);
        }
    }
    /* the names come sorted. */
    for (SIZE_T i = 1; i < names.rows_count; i++) {
        WCHAR **row = names.rows[i];
        SIZE_T j = i;
        while (j > 0 && CompareStringOrdinal(names.rows[j - 1][0], -1, row[0], -1, TRUE) == CSTR_GREATER_THAN) {
            names.rows[j] = names.rows[j - 1];
            j--;
        }
        names.rows[j] = row;
    }
    tu_puts(L"These Windows services are started:\n\n");
    for (SIZE_T i = 0; i < names.rows_count; i++)
        tu_print(L"   %s\n", names.rows[i][0]);
    tu_puts(L"\n");
    tu_table_free(&names);
    tu_free(buffer);
    CloseServiceHandle(manager);
    return completed();
}

/* waits while the service is in a pending state, printing a dot at each look; false when the query fails. */
static BOOL wait_for_state(SC_HANDLE service, DWORD pending, SERVICE_STATUS_PROCESS *status)
{
    DWORD needed;
    DWORD last_checkpoint = (DWORD)-1;
    ULONGLONG last_progress = GetTickCount64();
    for (;;) {
        if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, (LPBYTE)status, sizeof(*status), &needed))
            return FALSE;
        if (status->dwCurrentState != pending)
            return TRUE;
        if (status->dwCheckPoint != last_checkpoint) {
            last_checkpoint = status->dwCheckPoint;
            last_progress = GetTickCount64();
        }
        DWORD hint = status->dwWaitHint < 10000 ? 10000 : status->dwWaitHint;
        if (GetTickCount64() - last_progress > hint)
            return TRUE;
        DWORD wait = status->dwWaitHint / 10;
        if (wait < 250)
            wait = 250;
        if (wait > 2000)
            wait = 2000;
        tu_puts(L".");
        tu_flush();
        Sleep(wait);
    }
}

static int service_command(int kind, const WCHAR *name)
{
    /* kind: 0 start, 1 stop, 2 pause, 3 continue. */
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT_);
    if (!manager)
        return system_error(GetLastError());
    WCHAR *key = resolve_service(manager, name);
    if (!key) {
        CloseServiceHandle(manager);
        return network_error(2185, L"The service name is invalid.");
    }
    WCHAR *display = display_name_of(manager, key);
    DWORD access = SERVICE_QUERY_STATUS_ | (kind == 0 ? SERVICE_START_ : kind == 1 ? SERVICE_STOP_ | SERVICE_ENUMERATE_DEPENDENTS_
                                                                                   : SERVICE_PAUSE_CONTINUE_);
    SC_HANDLE service = OpenServiceW(manager, key, access);
    int result = 0;
    if (!service) {
        result = failure_of(GetLastError(), display, nullptr);
        goto done;
    }
    SERVICE_STATUS_PROCESS status;
    DWORD needed;
    if (kind == 0) {
        tu_print(L"The %s service is starting.", display);
        tu_flush();
        if (!StartServiceW(service, 0, nullptr)) {
            DWORD error = GetLastError();
            tu_puts(L"\n\n");
            if (error == ERROR_SERVICE_ALREADY_RUNNING) {
                result = network_error(2182, L"The requested service has already been started.");
            } else {
                tu_eprint(L"The %s service could not be started.\n\n", display);
                result = failure_of(error, display, nullptr);
            }
            goto done;
        }
        BOOL ok = wait_for_state(service, SERVICE_START_PENDING, &status);
        tu_puts(L"\n");
        if (ok && status.dwCurrentState == SERVICE_RUNNING) {
            tu_print(L"\nThe %s service was started successfully.\n\n", display);
        } else {
            tu_eprint(L"\nThe %s service could not be started.\n\n", display);
            if (status.dwWin32ExitCode == ERROR_SERVICE_SPECIFIC_ERROR)
                tu_eprint(L"A service specific error occurred: %lu.\n\n", status.dwServiceSpecificExitCode);
            else if (status.dwWin32ExitCode)
                result = failure_of(status.dwWin32ExitCode, display, nullptr);
            else
                tu_eprint(L"The service did not report an error.\n\n");
            if (!result)
                result = 2;
        }
    } else if (kind == 1) {
        tu_print(L"The %s service is stopping.", display);
        tu_flush();
        SERVICE_STATUS plain;
        if (!ControlService(service, SERVICE_CONTROL_STOP, &plain)) {
            DWORD error = GetLastError();
            tu_puts(L"\n\n");
            if (error == ERROR_SERVICE_NOT_ACTIVE) {
                tu_eprint(L"The %s service is not started.\n\nMore help is available by typing NET HELPMSG 3521.\n\n", display);
                result = 2;
            } else {
                tu_eprint(L"The %s service could not be stopped.\n\n", display);
                result = failure_of(error, display, nullptr);
            }
            goto done;
        }
        BOOL ok = wait_for_state(service, SERVICE_STOP_PENDING, &status);
        tu_puts(L"\n");
        if (ok && status.dwCurrentState == SERVICE_STOPPED)
            tu_print(L"\nThe %s service was stopped successfully.\n\n", display);
        else {
            tu_eprint(L"\nThe %s service could not be stopped.\n\n", display);
            result = 2;
        }
    } else {
        SERVICE_STATUS plain;
        DWORD control = kind == 2 ? SERVICE_CONTROL_PAUSE : SERVICE_CONTROL_CONTINUE;
        if (!ControlService(service, control, &plain)) {
            DWORD error = GetLastError();
            if (error == ERROR_SERVICE_NOT_ACTIVE) {
                tu_eprint(L"The %s service is not started.\n\nMore help is available by typing NET HELPMSG 3521.\n\n", display);
                result = 2;
            } else {
                tu_eprint(kind == 2 ? L"The %s service failed to pause.\n\n" : L"The %s service failed to resume.\n\n", display);
                result = failure_of(error, display, nullptr);
            }
            goto done;
        }
        tu_print(kind == 2 ? L"The %s service is pausing." : L"The %s service is continuing.", display);
        tu_flush();
        BOOL ok = wait_for_state(service, kind == 2 ? SERVICE_PAUSE_PENDING : SERVICE_CONTINUE_PENDING, &status);
        tu_puts(L"\n");
        (void)needed;
        if (ok)
            tu_print(kind == 2 ? L"\nThe %s service was paused successfully.\n\n" : L"\nThe %s service was continued successfully.\n\n",
                     display);
    }
done:
    if (service)
        CloseServiceHandle(service);
    CloseServiceHandle(manager);
    tu_free(key);
    tu_free(display);
    return result;
}

/* ------------------------------------------------------------------ the command */

int tool_main(void)
{
    if (tu_argc < 2)
        return syntax(L"NET\n    [ ACCOUNTS | COMPUTER | CONFIG | CONTINUE | FILE | GROUP | HELP |\n      HELPMSG | LOCALGROUP | PAUSE | SESSION | SHARE | START |\n      STATISTICS | STOP | TIME | USE | USER | VIEW ]");
    const WCHAR *command = tu_argv[1];
    if (tu_eq(command, L"session")) {
        if (!is_elevated())
            return system_error(ERROR_ACCESS_DENIED);
        tu_puts(L"There are no entries in the list.\n\n");
        return 0;
    }
    if (tu_eq(command, L"start") || tu_eq(command, L"stop") || tu_eq(command, L"pause") || tu_eq(command, L"continue")) {
        if (tu_argc == 2) {
            if (tu_eq(command, L"start"))
                return list_started();
            return syntax(tu_eq(command, L"stop") ? L"NET STOP service" : tu_eq(command, L"pause") ? L"NET PAUSE service"
                                                                                                    : L"NET CONTINUE service");
        }
        if (tu_argc > 3) {
            tu_eprint(L"The command contains an invalid number of arguments.\n\nMore help is available by typing NET HELPMSG 3503.\n\n");
            return 1;
        }
        int kind = tu_eq(command, L"start") ? 0 : tu_eq(command, L"stop") ? 1 : tu_eq(command, L"pause") ? 2 : 3;
        return service_command(kind, tu_argv[2]);
    }
    if (tu_eq(command, L"helpmsg")) {
        if (tu_argc != 3)
            return syntax(L"NET HELPMSG message#");
        DWORD number;
        if (!tu_parse_u32(tu_argv[2], &number)) {
            tu_eprint(L"The command contains an invalid number of arguments.\n\n");
            return 1;
        }
        WCHAR text[1024];
        DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, number, 0, text,
                                 TU_ARRAY(text), nullptr);
        if (!n) {
            tu_eprint(L"The specified message number could not be found.\n\n");
            return 2;
        }
        tu_print(L"\n%s\n", text);
        return 0;
    }
    if (tu_eq(command, L"user") || tu_eq(command, L"localgroup") || tu_eq(command, L"group") || tu_eq(command, L"accounts") ||
        tu_eq(command, L"share") || tu_eq(command, L"view") || tu_eq(command, L"config") || tu_eq(command, L"time") ||
        tu_eq(command, L"statistics") || tu_eq(command, L"file") || tu_eq(command, L"computer"))
        return system_error(ERROR_NOT_SUPPORTED);
    if (tu_eq(command, L"use") && tu_argc == 2) {
        tu_puts(L"New connections will be remembered.\n\nThere are no entries in the list.\n\n");
        return 0;
    }
    if (tu_eq(command, L"help")) {
        return syntax(L"NET\n    [ ACCOUNTS | COMPUTER | CONFIG | CONTINUE | FILE | GROUP | HELP |\n      HELPMSG | LOCALGROUP | PAUSE | SESSION | SHARE | START |\n      STATISTICS | STOP | TIME | USE | USER | VIEW ]");
    }
    tu_eprint(L"The syntax of this command is:\n\nNET\n    [ ACCOUNTS | COMPUTER | CONFIG | CONTINUE | FILE | GROUP | HELP |\n      HELPMSG | LOCALGROUP | PAUSE | SESSION | SHARE | START |\n      STATISTICS | STOP | TIME | USE | USER | VIEW ]\n");
    return 1;
}
