#include "msi_private.h"
#include <mime/win32/winsvc.h>

/*
 * services. the ServiceInstall, ServiceControl, MsiServiceConfig and MsiServiceConfigFailureActions tables work on the
 * service control manager. a service needs its files, so the actions that create, start or configure one wait for the
 * files and run in their order afterwards. stopping and deleting run as the sequence reaches them, before the files of
 * a removed product go. everything that changes a service leaves an undo behind, which a rollback runs.
 */

enum { MODE_NONE, MODE_INSTALL, MODE_REMOVE, MODE_REINSTALL };
enum { EVENT_INSTALL = 1, EVENT_UNINSTALL = 2, EVENT_REINSTALL = 4 };
enum { RUN_CONFIGURE = 1, RUN_INSTALL, RUN_START };
enum { CONTROL_START = 1, CONTROL_STOP = 2, CONTROL_DELETE = 8 };

static UINT optional_table(MIME_MSI_PACKAGE *package, PCWSTR name, MIME_MSI_TABLE **table)
{
    UINT error = MimeMsiLoadTable(package->Database, name, table);
    if (error == ERROR_BAD_QUERY_SYNTAX) {
        *table = nullptr;
        return ERROR_SUCCESS;
    }
    return error;
}

static PCWSTR text_of(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *table, ULONG row, PCWSTR column)
{
    ULONG index = MimeMsiColumn(table, column, TRUE);
    return index == UINT32_MAX ? nullptr : MimeMsiText(package->Database, table, row, index);
}

static int number_of(MIME_MSI_TABLE *table, ULONG row, PCWSTR column)
{
    ULONG index = MimeMsiColumn(table, column, FALSE);
    return index == UINT32_MAX ? MSI_NULL_INTEGER : MimeMsiInteger(table, row, index);
}

/* how the product changes a component, as a row of its own tables is to be read. */
static UINT component_mode(MIME_MSI_PACKAGE *package, PCWSTR name, ULONG *mode)
{
    BOOL local, registered;
    UINT error = MimeMsiComponentChange(package, name, &local, &registered);
    if (error)
        return error;
    if (local)
        *mode = registered ? (*MimeMsiGetProperty(package, L"REINSTALL") ? MODE_REINSTALL : MODE_NONE) : MODE_INSTALL;
    else
        *mode = registered ? MODE_REMOVE : MODE_NONE;
    return ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ text */

/* a list is written with [~] between its items, and a service takes it as a multi string. */
static UINT format_list(MIME_MSI_PACKAGE *package, PCWSTR raw, PWSTR *out, SIZE_T *chars)
{
    *out = nullptr;
    *chars = 0;
    PWSTR list = nullptr;
    SIZE_T at = 0, capacity = 0;
    UINT error = ERROR_SUCCESS;
    while (!error && *raw) {
        PCWSTR end = wcsstr(raw, L"[~]");
        SIZE_T length = end ? (SIZE_T)(end - raw) : wcslen(raw);
        PWSTR piece = MimeMsiAllocate((length + 1) * sizeof(WCHAR)), item = nullptr;
        if (!piece) {
            error = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }
        memcpy(piece, raw, length * sizeof(WCHAR));
        error = MimeMsiFormat(package, piece, &item);
        MimeMsiFree(piece);
        if (error)
            break;
        SIZE_T size = wcslen(item);
        if (at + size + 3 > capacity) {
            capacity = (at + size + 3) * 2;
            PWSTR grown = MimeMsiAllocate(capacity * sizeof(WCHAR));
            if (!grown)
                error = ERROR_NOT_ENOUGH_MEMORY;
            else {
                if (at)
                    memcpy(grown, list, at * sizeof(WCHAR));
                MimeMsiFree(list);
                list = grown;
            }
        }
        if (!error && size) {
            memcpy(list + at, item, (size + 1) * sizeof(WCHAR));
            at += size + 1;
        }
        MimeMsiFree(item);
        raw = end ? end + 3 : raw + length;
    }
    if (!error && !list) {
        list = MimeMsiAllocate(2 * sizeof(WCHAR));
        error = list ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
    }
    if (error) {
        MimeMsiFree(list);
        return error;
    }
    list[at++] = 0;
    if (at == 1)
        list[at++] = 0;
    *out = list;
    *chars = at;
    return ERROR_SUCCESS;
}

/* the items of a multi string as an argument array, which the caller frees. */
static PCWSTR *list_items(PCWSTR list, DWORD *count)
{
    DWORD n = 0;
    for (PCWSTR p = list; *p; p += wcslen(p) + 1)
        n++;
    PCWSTR *items = MimeMsiAllocate((n + 1) * sizeof(PCWSTR));
    if (!items)
        return nullptr;
    n = 0;
    for (PCWSTR p = list; *p; p += wcslen(p) + 1)
        items[n++] = p;
    *count = n;
    return items;
}

static BOOL parse_number(PCWSTR text, DWORD *value)
{
    if (!*text)
        return FALSE;
    ULONGLONG result = 0;
    for (; *text; text++) {
        if (*text < L'0' || *text > L'9' || (result = result * 10 + (ULONG)(*text - L'0')) > UINT32_MAX)
            return FALSE;
    }
    *value = (DWORD)result;
    return TRUE;
}

/* ------------------------------------------------------------------ the manager */

static SC_HANDLE open_service(PCWSTR name, DWORD access)
{
    SC_HANDLE manager = OpenSCManagerW(nullptr, L"ServicesActive", SC_MANAGER_ALL_ACCESS);
    if (!manager)
        return nullptr;
    SC_HANDLE service = OpenServiceW(manager, name, access);
    DWORD error = GetLastError();
    CloseServiceHandle(manager);
    SetLastError(error);
    return service;
}

/* ------------------------------------------------------------------ MsiServiceConfig */

/* the old value of a setting that an install changed, put back by a rollback. */
typedef struct CONFIG_UNDO {
    PWSTR Name;
    DWORD Level, Value;
    PWSTR List;
    SERVICE_FAILURE_ACTIONSW Actions;
    PWSTR Reboot, Command;
} CONFIG_UNDO;

static void config_undo_free(CONFIG_UNDO *undo)
{
    MimeMsiFree(undo->Name);
    MimeMsiFree(undo->List);
    MimeMsiFree(undo->Reboot);
    MimeMsiFree(undo->Command);
    MimeMsiFree(undo->Actions.lpsaActions);
    MimeMsiFree(undo);
}

static UINT config_undo_run(void *context, BOOL rollback)
{
    CONFIG_UNDO *undo = context;
    UINT error = ERROR_SUCCESS;
    if (rollback) {
        SC_HANDLE service = open_service(undo->Name, SERVICE_CHANGE_CONFIG);
        if (!service)
            error = GetLastError();
        else {
            BOOL done;
            if (undo->Level == SERVICE_CONFIG_FAILURE_ACTIONS)
                done = ChangeServiceConfig2W(service, undo->Level, &undo->Actions);
            else if (undo->Level == SERVICE_CONFIG_REQUIRED_PRIVILEGES_INFO)
                done = ChangeServiceConfig2W(service, undo->Level, &undo->List);
            else
                done = ChangeServiceConfig2W(service, undo->Level, &undo->Value);
            if (!done)
                error = GetLastError();
            CloseServiceHandle(service);
        }
        if (error)
            DbgPrint("msi: could not restore level %u of service %ws, error %u\n", undo->Level, undo->Name, error);
    }
    config_undo_free(undo);
    return error;
}

static CONFIG_UNDO *config_undo_new(PCWSTR name, DWORD level)
{
    CONFIG_UNDO *undo = MimeMsiAllocate(sizeof(*undo));
    if (!undo)
        return nullptr;
    undo->Level = level;
    undo->Name = MimeMsiDuplicate(name);
    if (!undo->Name) {
        MimeMsiFree(undo);
        return nullptr;
    }
    return undo;
}

/* the service's current setting of one level, in a buffer the caller frees. */
static BYTE *query_level(SC_HANDLE service, DWORD level, UINT *error)
{
    DWORD needed = 0;
    QueryServiceConfig2W(service, level, nullptr, 0, &needed);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !needed) {
        *error = GetLastError();
        return nullptr;
    }
    BYTE *buffer = MimeMsiAllocate(needed + sizeof(WCHAR) * 2);
    if (!buffer) {
        *error = ERROR_NOT_ENOUGH_MEMORY;
        return nullptr;
    }
    if (!QueryServiceConfig2W(service, level, buffer, needed, &needed)) {
        *error = GetLastError();
        MimeMsiFree(buffer);
        return nullptr;
    }
    return buffer;
}

static UINT note_config_undo(MIME_MSI_PACKAGE *package, CONFIG_UNDO *undo)
{
    UINT error = MimeMsiNoteUndo(package, config_undo_run, undo);
    if (error)
        config_undo_free(undo);
    return error;
}

/* one of the settings that take a number or a list, with the previous one kept for a rollback. */
static UINT configure_level(MIME_MSI_PACKAGE *package, SC_HANDLE service, PCWSTR name, DWORD type, PCWSTR raw)
{
    DWORD level = type, value = 0;
    UINT error = ERROR_SUCCESS;
    PWSTR list = nullptr, argument = nullptr;
    SIZE_T chars = 0;
    if (type != SERVICE_CONFIG_REQUIRED_PRIVILEGES_INFO) {
        error = MimeMsiFormat(package, raw, &argument);
        if (error)
            return error;
    }
    UINT invalid = ERROR_INVALID_PARAMETER;
    switch (type) {
    case SERVICE_CONFIG_DELAYED_AUTO_START_INFO:
    case SERVICE_CONFIG_FAILURE_ACTIONS_FLAG:
        if (!parse_number(argument, &value) || value > 1)
            error = invalid;
        break;
    case SERVICE_CONFIG_SERVICE_SID_INFO:
        if (!parse_number(argument, &value) || value > INT32_MAX)
            error = invalid;
        break;
    case SERVICE_CONFIG_PRESHUTDOWN_INFO:
        /* a bad timeout becomes three minutes. */
        if (!parse_number(argument, &value) || value > INT32_MAX)
            value = 180000;
        break;
    case SERVICE_CONFIG_REQUIRED_PRIVILEGES_INFO:
        error = format_list(package, raw, &list, &chars);
        if (!error && chars <= 2)
            error = invalid;
        break;
    default:
        error = invalid;
        break;
    }
    MimeMsiFree(argument);
    if (error) {
        MimeMsiFree(list);
        return error;
    }
    CONFIG_UNDO *undo = config_undo_new(name, level);
    BYTE *old = undo ? query_level(service, level, &error) : nullptr;
    if (!undo)
        error = ERROR_NOT_ENOUGH_MEMORY;
    if (!error) {
        if (level == SERVICE_CONFIG_REQUIRED_PRIVILEGES_INFO) {
            PCWSTR privileges = *(PCWSTR *)old;
            SIZE_T length = 2;
            for (PCWSTR p = privileges; p && *p; p += wcslen(p) + 1)
                length += wcslen(p) + 1;
            undo->List = MimeMsiAllocate(length * sizeof(WCHAR));
            if (!undo->List)
                error = ERROR_NOT_ENOUGH_MEMORY;
            else if (privileges && *privileges)
                memcpy(undo->List, privileges, (length - 1) * sizeof(WCHAR));
        } else
            undo->Value = *(DWORD *)old;
    }
    MimeMsiFree(old);
    if (!error) {
        BOOL done;
        if (level == SERVICE_CONFIG_REQUIRED_PRIVILEGES_INFO)
            done = ChangeServiceConfig2W(service, level, &list);
        else
            done = ChangeServiceConfig2W(service, level, &value);
        if (!done)
            error = GetLastError();
    }
    MimeMsiFree(list);
    if (error) {
        if (undo)
            config_undo_free(undo);
        return error;
    }
    return note_config_undo(package, undo);
}

/* the actions of a failure, with their delays, from the two lists of the row. */
static UINT failure_actions(MIME_MSI_PACKAGE *package, PCWSTR name, SC_HANDLE service, DWORD period, PCWSTR reboot,
                            PCWSTR command, PCWSTR types, PCWSTR delays)
{
    PWSTR type_list = nullptr, delay_list = nullptr;
    SIZE_T chars;
    UINT error = format_list(package, types, &type_list, &chars);
    if (!error)
        error = format_list(package, delays, &delay_list, &chars);
    DWORD count = 0, delay_count = 0;
    PCWSTR *type_items = type_list ? list_items(type_list, &count) : nullptr;
    PCWSTR *delay_items = delay_list ? list_items(delay_list, &delay_count) : nullptr;
    SC_ACTION *actions = nullptr;
    if (!error && (!type_items || !delay_items))
        error = ERROR_NOT_ENOUGH_MEMORY;
    if (!error && count != delay_count)
        error = ERROR_INVALID_PARAMETER;
    if (!error && count)
        actions = MimeMsiAllocate(count * sizeof(*actions));
    if (!error && count && !actions)
        error = ERROR_NOT_ENOUGH_MEMORY;
    for (DWORD i = 0; !error && i < count; i++) {
        DWORD type, delay;
        if (!parse_number(type_items[i], &type) || !parse_number(delay_items[i], &delay) || type > SC_ACTION_RUN_COMMAND ||
            delay > INT32_MAX)
            error = ERROR_INVALID_PARAMETER;
        else {
            actions[i].Type = (SC_ACTION_TYPE)type;
            actions[i].Delay = delay;
        }
    }
    CONFIG_UNDO *undo = nullptr;
    BYTE *old = nullptr;
    if (!error) {
        undo = config_undo_new(name, SERVICE_CONFIG_FAILURE_ACTIONS);
        old = undo ? query_level(service, SERVICE_CONFIG_FAILURE_ACTIONS, &error) : nullptr;
        if (!undo)
            error = ERROR_NOT_ENOUGH_MEMORY;
    }
    if (!error) {
        const SERVICE_FAILURE_ACTIONSW *was = (const void *)old;
        undo->Actions.dwResetPeriod = was->dwResetPeriod;
        undo->Reboot = MimeMsiDuplicate(was->lpRebootMsg ? was->lpRebootMsg : L"");
        undo->Command = MimeMsiDuplicate(was->lpCommand ? was->lpCommand : L"");
        undo->Actions.lpRebootMsg = undo->Reboot;
        undo->Actions.lpCommand = undo->Command;
        undo->Actions.cActions = was->cActions;
        if (was->cActions) {
            undo->Actions.lpsaActions = MimeMsiAllocate(was->cActions * sizeof(SC_ACTION));
            if (undo->Actions.lpsaActions)
                memcpy(undo->Actions.lpsaActions, was->lpsaActions, was->cActions * sizeof(SC_ACTION));
        }
        if (!undo->Reboot || !undo->Command || (was->cActions && !undo->Actions.lpsaActions))
            error = ERROR_NOT_ENOUGH_MEMORY;
    }
    MimeMsiFree(old);
    if (!error) {
        SERVICE_FAILURE_ACTIONSW now = {period, (PWSTR)reboot, (PWSTR)command, count, actions};
        if (!ChangeServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS, &now))
            error = GetLastError();
    }
    if (error && undo)
        config_undo_free(undo);
    else if (undo)
        error = note_config_undo(package, undo);
    MimeMsiFree(actions);
    MimeMsiFree(type_items);
    MimeMsiFree(delay_items);
    MimeMsiFree(type_list);
    MimeMsiFree(delay_list);
    return error;
}

/* the event bit a component's change calls for, which a row has to carry. */
static BOOL event_applies(ULONG mode, int event)
{
    if (event == MSI_NULL_INTEGER)
        return FALSE;
    return (mode == MODE_INSTALL && (event & EVENT_INSTALL)) || (mode == MODE_REMOVE && (event & EVENT_UNINSTALL)) ||
           (mode == MODE_REINSTALL && (event & EVENT_REINSTALL));
}

static UINT configure_services(MIME_MSI_PACKAGE *package)
{
    MIME_MSI_TABLE *table, *failures;
    UINT error = optional_table(package, L"MsiServiceConfig", &table);
    if (!error)
        error = optional_table(package, L"MsiServiceConfigFailureActions", &failures);
    for (ULONG row = 0; table && !error && row < table->Rows; row++) {
        PCWSTR component = text_of(package, table, row, L"Component_");
        PCWSTR raw_name = text_of(package, table, row, L"Name");
        PCWSTR raw_argument = text_of(package, table, row, L"Argument");
        int event = number_of(table, row, L"Event"), type = number_of(table, row, L"ConfigType");
        ULONG mode;
        if (!component || !raw_name || type == MSI_NULL_INTEGER)
            return ERROR_INSTALL_PACKAGE_INVALID;
        error = component_mode(package, component, &mode);
        if (error || !event_applies(mode, event))
            continue;
        PWSTR name = nullptr;
        error = MimeMsiFormat(package, raw_name, &name);
        SC_HANDLE service = error ? nullptr : open_service(name, SERVICE_QUERY_CONFIG | SERVICE_CHANGE_CONFIG);
        if (!error && !service)
            error = GetLastError();
        if (!error) {
            error = configure_level(package, service, name, (DWORD)type, raw_argument ? raw_argument : L"");
            CloseServiceHandle(service);
        }
        if (error) {
            DbgPrint("msi: could not configure service %ws with type %d, error %u\n", name ? name : raw_name, type, error);
            error = ERROR_INSTALL_FAILURE;
        }
        MimeMsiFree(name);
    }
    for (ULONG row = 0; failures && !error && row < failures->Rows; row++) {
        PCWSTR component = text_of(package, failures, row, L"Component_");
        PCWSTR raw_name = text_of(package, failures, row, L"Name");
        int event = number_of(failures, row, L"Event"), period = number_of(failures, row, L"ResetPeriod");
        ULONG mode;
        if (!component || !raw_name)
            return ERROR_INSTALL_PACKAGE_INVALID;
        error = component_mode(package, component, &mode);
        if (error || !event_applies(mode, event))
            continue;
        PWSTR name = nullptr, reboot = nullptr, command = nullptr;
        PCWSTR types = text_of(package, failures, row, L"Actions"), delays = text_of(package, failures, row, L"DelayActions");
        error = MimeMsiFormat(package, raw_name, &name);
        if (!error)
            error = MimeMsiFormat(package, text_of(package, failures, row, L"RebootMessage"), &reboot);
        if (!error)
            error = MimeMsiFormat(package, text_of(package, failures, row, L"Command"), &command);
        SC_HANDLE service = error ? nullptr : open_service(name, SERVICE_QUERY_CONFIG | SERVICE_CHANGE_CONFIG);
        if (!error && !service)
            error = GetLastError();
        if (!error) {
            /* a blank reset period never resets the count. */
            error = failure_actions(package, name, service, period == MSI_NULL_INTEGER ? INFINITE : (DWORD)period, reboot,
                                    command, types ? types : L"", delays ? delays : L"");
            CloseServiceHandle(service);
        }
        if (error) {
            DbgPrint("msi: could not set the failure actions of service %ws, error %u\n", name ? name : raw_name, error);
            error = ERROR_INSTALL_FAILURE;
        }
        MimeMsiFree(name);
        MimeMsiFree(reboot);
        MimeMsiFree(command);
    }
    return error;
}

/* ------------------------------------------------------------------ starting, stopping, deleting */

static PWSTR multi_copy(PCWSTR list)
{
    SIZE_T chars = 1;
    for (PCWSTR p = list; p && *p; p += wcslen(p) + 1)
        chars += wcslen(p) + 1;
    PWSTR copy = MimeMsiAllocate((chars + 1) * sizeof(WCHAR));
    if (copy && list && *list)
        memcpy(copy, list, chars * sizeof(WCHAR));
    return copy;
}

/* a service to start or stop again, kept by a rollback. */
typedef struct SERVICE_OP {
    PWSTR Name, Arguments;
    BOOL Wait;
} SERVICE_OP;

static void op_free(SERVICE_OP *op)
{
    MimeMsiFree(op->Name);
    MimeMsiFree(op->Arguments);
    MimeMsiFree(op);
}

static SERVICE_OP *op_new(PCWSTR name, PCWSTR arguments, BOOL wait)
{
    SERVICE_OP *op = MimeMsiAllocate(sizeof(*op));
    if (!op)
        return nullptr;
    op->Name = MimeMsiDuplicate(name);
    op->Arguments = multi_copy(arguments);
    op->Wait = wait;
    if (!op->Name || !op->Arguments) {
        op_free(op);
        return nullptr;
    }
    return op;
}

/* the service is polled for half a minute at most, as windows polls it. */
static BOOL wait_for(SC_HANDLE service, DWORD state)
{
    SERVICE_STATUS status;
    for (ULONG tries = 0; tries <= 60; tries++) {
        ControlService(service, SERVICE_CONTROL_INTERROGATE, &status);
        if (!QueryServiceStatus(service, &status) || !status.dwCurrentState)
            return FALSE;
        if (status.dwCurrentState == state)
            return TRUE;
        Sleep(500);
    }
    return FALSE;
}

static DWORD start_service(PCWSTR name, PCWSTR arguments, BOOL wait, BOOL *started)
{
    *started = FALSE;
    SC_HANDLE service = open_service(name, SERVICE_START | SERVICE_QUERY_STATUS | SERVICE_INTERROGATE);
    if (!service)
        return GetLastError();
    DWORD count = 0, error = ERROR_SUCCESS;
    PCWSTR *items = arguments && *arguments ? list_items(arguments, &count) : nullptr;
    if (arguments && *arguments && !items)
        error = ERROR_NOT_ENOUGH_MEMORY;
    else if (StartServiceW(service, count, items))
        *started = TRUE;
    else if ((error = GetLastError()) == ERROR_SERVICE_ALREADY_RUNNING)
        error = ERROR_SUCCESS;
    if (!error && wait && !wait_for(service, SERVICE_RUNNING))
        error = ERROR_SERVICE_REQUEST_TIMEOUT;
    MimeMsiFree(items);
    CloseServiceHandle(service);
    return error;
}

static UINT start_undo(void *context, BOOL rollback)
{
    SERVICE_OP *op = context;
    BOOL started;
    DWORD error = rollback ? start_service(op->Name, op->Arguments, op->Wait, &started) : ERROR_SUCCESS;
    if (error)
        DbgPrint("msi: could not start service %ws again, error %u\n", op->Name, error);
    op_free(op);
    return error;
}

static DWORD stop_service(MIME_MSI_PACKAGE *package, PCWSTR name, PCWSTR arguments, BOOL wait);

/* stops the active services that depend on one, each waited for. */
static DWORD stop_dependents(MIME_MSI_PACKAGE *package, SC_HANDLE service)
{
    DWORD size = 0, count = 0;
    EnumDependentServicesW(service, SERVICE_ACTIVE, nullptr, 0, &size, &count);
    if (GetLastError() != ERROR_MORE_DATA || !size)
        return ERROR_SUCCESS;
    ENUM_SERVICE_STATUSW *list = MimeMsiAllocate(size);
    if (!list)
        return ERROR_NOT_ENOUGH_MEMORY;
    DWORD error = ERROR_SUCCESS;
    if (!EnumDependentServicesW(service, SERVICE_ACTIVE, list, size, &size, &count))
        error = GetLastError();
    for (DWORD i = 0; !error && i < count; i++)
        error = stop_service(package, list[i].lpServiceName, nullptr, TRUE);
    MimeMsiFree(list);
    return error;
}

/* a service that is missing, never started or already stopped counts as stopped. with a package the stop is journaled. */
static DWORD stop_service(MIME_MSI_PACKAGE *package, PCWSTR name, PCWSTR arguments, BOOL wait)
{
    SC_HANDLE service = open_service(name, SERVICE_STOP | SERVICE_QUERY_STATUS | SERVICE_ENUMERATE_DEPENDENTS);
    if (!service) {
        DWORD error = GetLastError();
        return error == ERROR_SERVICE_DOES_NOT_EXIST ? ERROR_SUCCESS : error;
    }
    DWORD result = ERROR_SUCCESS;
    SERVICE_STATUS status;
    if (ControlService(service, SERVICE_CONTROL_STOP, &status)) {
        SERVICE_OP *op = package ? op_new(name, arguments, wait) : nullptr;
        if (package && !op)
            result = ERROR_NOT_ENOUGH_MEMORY;
        else if (op && MimeMsiNoteUndo(package, start_undo, op)) {
            op_free(op);
            result = ERROR_NOT_ENOUGH_MEMORY;
        }
    } else {
        DWORD error = GetLastError();
        if (error == ERROR_SERVICE_DOES_NOT_EXIST || error == ERROR_SERVICE_NOT_ACTIVE || error == ERROR_SERVICE_NEVER_STARTED) {
            CloseServiceHandle(service);
            return ERROR_SUCCESS;
        }
        if (error == ERROR_DEPENDENT_SERVICES_RUNNING) {
            result = stop_dependents(package, service);
            if (!result)
                result = stop_service(package, name, arguments, wait);
        }
    }
    if (wait && !wait_for(service, SERVICE_STOPPED))
        result = ERROR_SERVICE_REQUEST_TIMEOUT;
    CloseServiceHandle(service);
    return result;
}

static UINT stop_undo(void *context, BOOL rollback)
{
    SERVICE_OP *op = context;
    DWORD error = rollback ? stop_service(nullptr, op->Name, nullptr, op->Wait) : ERROR_SUCCESS;
    if (error)
        DbgPrint("msi: could not stop service %ws again, error %u\n", op->Name, error);
    op_free(op);
    return error;
}

/* the configuration of a service that goes away or changes, put back by a rollback. */
typedef struct SAVED_SERVICE {
    PWSTR Name, Display, Path, Group, Dependencies, Account, Description;
    DWORD Type, Start, Error;
    BOOL Recreate;
} SAVED_SERVICE;

static void saved_free(SAVED_SERVICE *saved)
{
    MimeMsiFree(saved->Name);
    MimeMsiFree(saved->Display);
    MimeMsiFree(saved->Path);
    MimeMsiFree(saved->Group);
    MimeMsiFree(saved->Dependencies);
    MimeMsiFree(saved->Account);
    MimeMsiFree(saved->Description);
    MimeMsiFree(saved);
}

static SAVED_SERVICE *saved_new(SC_HANDLE service, PCWSTR name, BOOL recreate)
{
    DWORD needed = 0;
    QueryServiceConfigW(service, nullptr, 0, &needed);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !needed)
        return nullptr;
    QUERY_SERVICE_CONFIGW *config = MimeMsiAllocate(needed);
    SAVED_SERVICE *saved = config ? MimeMsiAllocate(sizeof(*saved)) : nullptr;
    if (!saved || !QueryServiceConfigW(service, config, needed, &needed)) {
        MimeMsiFree(config);
        MimeMsiFree(saved);
        return nullptr;
    }
    saved->Recreate = recreate;
    saved->Type = config->dwServiceType;
    saved->Start = config->dwStartType;
    saved->Error = config->dwErrorControl;
    saved->Name = MimeMsiDuplicate(name);
    saved->Display = MimeMsiDuplicate(config->lpDisplayName ? config->lpDisplayName : name);
    saved->Path = MimeMsiDuplicate(config->lpBinaryPathName ? config->lpBinaryPathName : L"");
    saved->Group = MimeMsiDuplicate(config->lpLoadOrderGroup ? config->lpLoadOrderGroup : L"");
    saved->Dependencies = multi_copy(config->lpDependencies);
    saved->Account = config->lpServiceStartName ? MimeMsiDuplicate(config->lpServiceStartName) : nullptr;
    UINT error = ERROR_SUCCESS;
    BYTE *text = query_level(service, SERVICE_CONFIG_DESCRIPTION, &error);
    PCWSTR description = text ? *(PCWSTR *)text : nullptr;
    saved->Description = MimeMsiDuplicate(description ? description : L"");
    BOOL account = !config->lpServiceStartName || saved->Account;
    MimeMsiFree(text);
    MimeMsiFree(config);
    if (!saved->Name || !saved->Display || !saved->Path || !saved->Group || !saved->Dependencies || !saved->Description || !account) {
        saved_free(saved);
        return nullptr;
    }
    return saved;
}

static UINT saved_undo(void *context, BOOL rollback)
{
    SAVED_SERVICE *saved = context;
    DWORD error = ERROR_SUCCESS;
    if (rollback) {
        SC_HANDLE manager = OpenSCManagerW(nullptr, L"ServicesActive", SC_MANAGER_ALL_ACCESS);
        SC_HANDLE service = nullptr;
        if (!manager)
            error = GetLastError();
        else if (saved->Recreate)
            service = CreateServiceW(manager, saved->Name, saved->Display, SERVICE_ALL_ACCESS, saved->Type, saved->Start,
                                     saved->Error, saved->Path, *saved->Group ? saved->Group : nullptr, nullptr,
                                     *saved->Dependencies ? saved->Dependencies : nullptr, saved->Account, nullptr);
        else
            service = OpenServiceW(manager, saved->Name, SERVICE_CHANGE_CONFIG);
        if (manager && !service)
            error = GetLastError();
        if (service && !saved->Recreate &&
            !ChangeServiceConfigW(service, saved->Type, saved->Start, saved->Error, saved->Path, saved->Group, nullptr,
                                  saved->Dependencies, saved->Account, nullptr, saved->Display))
            error = GetLastError();
        if (service && !error) {
            PWSTR description = saved->Description;
            if (!ChangeServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, &description))
                error = GetLastError();
        }
        if (service)
            CloseServiceHandle(service);
        if (manager)
            CloseServiceHandle(manager);
        if (error)
            DbgPrint("msi: could not restore service %ws, error %u\n", saved->Name, error);
    }
    saved_free(saved);
    return error;
}

static DWORD note_saved(MIME_MSI_PACKAGE *package, SAVED_SERVICE *saved)
{
    if (!MimeMsiNoteUndo(package, saved_undo, saved))
        return ERROR_SUCCESS;
    saved_free(saved);
    return ERROR_NOT_ENOUGH_MEMORY;
}

/* stops a service and removes it. a rollback creates it again. */
static DWORD delete_service(MIME_MSI_PACKAGE *package, PCWSTR name)
{
    DWORD error = stop_service(package, name, nullptr, TRUE);
    if (error)
        return error;
    SC_HANDLE service = open_service(name, DELETE | READ_CONTROL | SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS);
    if (!service) {
        error = GetLastError();
        return error == ERROR_SERVICE_DOES_NOT_EXIST ? ERROR_SUCCESS : error;
    }
    SAVED_SERVICE *saved = saved_new(service, name, TRUE);
    if (!DeleteService(service)) {
        error = GetLastError();
        if (error == ERROR_SERVICE_MARKED_FOR_DELETE)
            error = ERROR_SUCCESS;
        if (saved)
            saved_free(saved);
    } else if (saved)
        error = note_saved(package, saved);
    CloseServiceHandle(service);
    return error;
}

static UINT created_undo(void *context, BOOL rollback)
{
    SERVICE_OP *op = context;
    DWORD error = ERROR_SUCCESS;
    if (rollback) {
        error = stop_service(nullptr, op->Name, nullptr, TRUE);
        SC_HANDLE service = error ? nullptr : open_service(op->Name, DELETE);
        if (service) {
            if (!DeleteService(service) && GetLastError() != ERROR_SERVICE_MARKED_FOR_DELETE)
                error = GetLastError();
            CloseServiceHandle(service);
        } else if (!error && GetLastError() != ERROR_SERVICE_DOES_NOT_EXIST)
            error = GetLastError();
        if (error)
            DbgPrint("msi: could not remove service %ws, error %u\n", op->Name, error);
    }
    op_free(op);
    return error;
}

/* ------------------------------------------------------------------ ServiceControl */

static BOOL service_key_exists(PCWSTR name)
{
    WCHAR path[300];
    HKEY key;
    _snwprintf(path, RTL_NUMBER_OF(path), L"System\\CurrentControlSet\\Services\\%s", name);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_QUERY_VALUE, &key))
        return FALSE;
    RegCloseKey(key);
    return TRUE;
}

/* a failure to control a service stops the install when the row waits or the service is severe about its own errors. */
static BOOL control_is_fatal(PCWSTR name, BOOL wait)
{
    if (wait)
        return TRUE;
    WCHAR path[300];
    HKEY key;
    _snwprintf(path, RTL_NUMBER_OF(path), L"System\\CurrentControlSet\\Services\\%s", name);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_QUERY_VALUE, &key))
        return FALSE;
    DWORD value = 0, size = sizeof(value), type;
    BOOL fatal = !RegQueryValueExW(key, L"ErrorControl", nullptr, &type, (BYTE *)&value, &size) && type == REG_DWORD && value > 1;
    RegCloseKey(key);
    return fatal;
}

static BOOL transient(DWORD error)
{
    return error == ERROR_SERVICE_REQUEST_TIMEOUT || error == ERROR_SERVICE_CANNOT_ACCEPT_CTRL ||
           error == ERROR_SERVICE_MARKED_FOR_DELETE || error == ERROR_SERVICE_DATABASE_LOCKED;
}

static DWORD control_once(MIME_MSI_PACKAGE *package, ULONG kind, PCWSTR name, PCWSTR arguments, BOOL wait)
{
    if (kind == CONTROL_START) {
        BOOL started;
        DWORD error = start_service(name, arguments, wait, &started);
        if (!error && started) {
            SERVICE_OP *op = op_new(name, nullptr, wait);
            if (!op)
                return ERROR_NOT_ENOUGH_MEMORY;
            if (MimeMsiNoteUndo(package, stop_undo, op)) {
                op_free(op);
                return ERROR_NOT_ENOUGH_MEMORY;
            }
        }
        return error;
    }
    if (kind == CONTROL_STOP)
        return stop_service(package, name, arguments, wait);
    return delete_service(package, name);
}

static UINT control_services(MIME_MSI_PACKAGE *package, ULONG kind)
{
    MIME_MSI_TABLE *table;
    UINT error = optional_table(package, L"ServiceControl", &table);
    for (ULONG row = 0; table && !error && row < table->Rows; row++) {
        PCWSTR component = text_of(package, table, row, L"Component_");
        PCWSTR raw_name = text_of(package, table, row, L"Name");
        int event = number_of(table, row, L"Event"), wait = number_of(table, row, L"Wait");
        ULONG mode;
        if (!component || !raw_name || event == MSI_NULL_INTEGER)
            return ERROR_INSTALL_PACKAGE_INVALID;
        error = component_mode(package, component, &mode);
        if (error)
            return error;
        if (!((mode == MODE_INSTALL && (event & kind)) || (mode == MODE_REMOVE && (event & (kind << 4)))))
            continue;
        PWSTR name = nullptr, arguments = nullptr;
        SIZE_T chars;
        PCWSTR raw_arguments = text_of(package, table, row, L"Arguments");
        error = MimeMsiFormat(package, raw_name, &name);
        if (!error)
            error = format_list(package, raw_arguments ? raw_arguments : L"", &arguments, &chars);
        if (error) {
            MimeMsiFree(name);
            MimeMsiFree(arguments);
            return error;
        }
        BOOL waits = wait != MSI_NULL_INTEGER && wait;
        DWORD failure = ERROR_SUCCESS;
        /* stopping or deleting what the machine does not have needs nothing. */
        if (kind == CONTROL_START || service_key_exists(name)) {
            for (ULONG attempt = 0; attempt < 8; attempt++) {
                if (attempt)
                    Sleep(5000);
                failure = control_once(package, kind, name, arguments, waits);
                if (!failure || !transient(failure))
                    break;
            }
        }
        if (failure) {
            BOOL fatal = control_is_fatal(name, waits);
            DbgPrint("msi: service %ws could not be %s, error %u%s\n", name,
                     kind == CONTROL_START ? "started" : kind == CONTROL_STOP ? "stopped" : "deleted", failure,
                     fatal ? "" : ", which is ignored");
            if (fatal)
                error = ERROR_INSTALL_FAILURE;
        }
        MimeMsiFree(name);
        MimeMsiFree(arguments);
    }
    return error;
}

/* ------------------------------------------------------------------ ServiceInstall */

typedef struct INSTALL_ROW {
    PWSTR Name, Display, Command, Group, Dependencies, Account, Password, Description;
    DWORD Type, Start, Error;
    BOOL Vital;
} INSTALL_ROW;

static void install_row_free(INSTALL_ROW *row)
{
    MimeMsiFree(row->Name);
    MimeMsiFree(row->Display);
    MimeMsiFree(row->Command);
    MimeMsiFree(row->Group);
    MimeMsiFree(row->Dependencies);
    MimeMsiFree(row->Account);
    MimeMsiFree(row->Password);
    MimeMsiFree(row->Description);
}

static PCWSTR non_empty(PCWSTR text)
{
    return text && *text ? text : nullptr;
}

static DWORD note_created(MIME_MSI_PACKAGE *package, PCWSTR name)
{
    SERVICE_OP *op = op_new(name, nullptr, TRUE);
    if (!op)
        return ERROR_NOT_ENOUGH_MEMORY;
    if (MimeMsiNoteUndo(package, created_undo, op)) {
        op_free(op);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    return ERROR_SUCCESS;
}

/* creates the service, or sets an existing one to the row. */
static DWORD install_one(MIME_MSI_PACKAGE *package, const INSTALL_ROW *row)
{
    SC_HANDLE manager = OpenSCManagerW(nullptr, L"ServicesActive", SC_MANAGER_ALL_ACCESS);
    if (!manager)
        return GetLastError();
    DWORD error = ERROR_SUCCESS;
    SC_HANDLE service = OpenServiceW(manager, row->Name, READ_CONTROL | WRITE_DAC | WRITE_OWNER | SERVICE_QUERY_CONFIG |
                                                              SERVICE_CHANGE_CONFIG | SERVICE_QUERY_STATUS);
    if (!service) {
        service = CreateServiceW(manager, row->Name, row->Display, SERVICE_ALL_ACCESS, row->Type, row->Start, row->Error,
                                 row->Command, non_empty(row->Group), nullptr, non_empty(row->Dependencies),
                                 non_empty(row->Account), non_empty(row->Password));
        if (!service)
            error = GetLastError();
        else
            error = note_created(package, row->Name);
    } else {
        SAVED_SERVICE *saved = saved_new(service, row->Name, FALSE);
        if (!ChangeServiceConfigW(service, row->Type, row->Start, row->Error, row->Command, row->Group, nullptr,
                                  row->Dependencies, non_empty(row->Account), non_empty(row->Password), row->Display)) {
            error = GetLastError();
            if (saved)
                saved_free(saved);
        } else if (saved)
            error = note_saved(package, saved);
    }
    if (service && !error) {
        PWSTR description = row->Description;
        /* a service that cannot take a description is not worth failing for. */
        if (!ChangeServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, &description) && GetLastError() != ERROR_INVALID_FUNCTION)
            DbgPrint("msi: could not describe service %ws, error %u\n", row->Name, GetLastError());
    }
    if (service)
        CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return error;
}

/* the path of the key file of a component, left null when its key path is not a file of the package. */
static UINT key_file_path(MIME_MSI_PACKAGE *package, PCWSTR component, PWSTR *path)
{
    *path = nullptr;
    MIME_MSI_TABLE *components, *files;
    UINT error = MimeMsiLoadTable(package->Database, L"Component", &components);
    if (!error)
        error = MimeMsiLoadTable(package->Database, L"File", &files);
    if (error)
        return error;
    ULONG id = MimeMsiColumn(components, L"Component", TRUE);
    if (id == UINT32_MAX)
        return ERROR_INSTALL_PACKAGE_INVALID;
    for (ULONG row = 0; row < components->Rows; row++) {
        PCWSTR candidate = MimeMsiText(package->Database, components, row, id);
        if (!candidate || wcscmp(candidate, component))
            continue;
        PCWSTR key = text_of(package, components, row, L"KeyPath");
        int attributes = number_of(components, row, L"Attributes");
        if (!key || !*key || (attributes != MSI_NULL_INTEGER && (attributes & 4)))
            return ERROR_SUCCESS;
        ULONG file_id = MimeMsiColumn(files, L"File", TRUE);
        for (ULONG file = 0; file_id != UINT32_MAX && file < files->Rows; file++) {
            PCWSTR name = MimeMsiText(package->Database, files, file, file_id);
            if (name && !wcscmp(name, key))
                return MimeMsiFileTargetPath(package, key, path);
        }
        return ERROR_SUCCESS;
    }
    return ERROR_INSTALL_PACKAGE_INVALID;
}

/* permissions on a service are not applied. */
static UINT check_permissions(MIME_MSI_PACKAGE *package)
{
    static const PCWSTR tables[] = {L"LockPermissions", L"MsiLockPermissionsEx"};
    for (ULONG i = 0; i < RTL_NUMBER_OF(tables); i++) {
        MIME_MSI_TABLE *table;
        UINT error = optional_table(package, tables[i], &table);
        if (error)
            return error;
        for (ULONG row = 0; table && row < table->Rows; row++) {
            PCWSTR owner = text_of(package, table, row, L"Table");
            if (owner && !wcscmp(owner, L"ServiceInstall"))
                return ERROR_CALL_NOT_IMPLEMENTED;
        }
    }
    return ERROR_SUCCESS;
}

static UINT install_services(MIME_MSI_PACKAGE *package)
{
    MIME_MSI_TABLE *table;
    UINT error = optional_table(package, L"ServiceInstall", &table);
    if (!error && table && table->Rows)
        error = check_permissions(package);
    for (ULONG row = 0; table && !error && row < table->Rows; row++) {
        PCWSTR component = text_of(package, table, row, L"Component_");
        PCWSTR raw_name = text_of(package, table, row, L"Name");
        PCWSTR raw_dependencies = text_of(package, table, row, L"Dependencies");
        int type = number_of(table, row, L"ServiceType"), start = number_of(table, row, L"StartType");
        int control = number_of(table, row, L"ErrorControl");
        ULONG mode;
        if (!component || !raw_name || type == MSI_NULL_INTEGER || start == MSI_NULL_INTEGER)
            return ERROR_INSTALL_PACKAGE_INVALID;
        error = component_mode(package, component, &mode);
        if (error)
            return error;
        if (mode != MODE_INSTALL)
            continue;
        PWSTR path = nullptr, arguments = nullptr;
        error = key_file_path(package, component, &path);
        if (error || !path)
            continue;
        INSTALL_ROW service = {};
        SIZE_T chars;
        error = MimeMsiFormat(package, raw_name, &service.Name);
        if (!error)
            error = MimeMsiFormat(package, text_of(package, table, row, L"DisplayName"), &service.Display);
        if (!error && !*service.Display) {
            MimeMsiFree(service.Display);
            service.Display = MimeMsiDuplicate(service.Name);
            error = service.Display ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
        }
        if (!error)
            error = MimeMsiFormat(package, text_of(package, table, row, L"Arguments"), &arguments);
        /* a path with a space is quoted for a program, the kernel's own services are not. */
        if (!error) {
            BOOL quote = wcschr(path, L' ') && !(type & (SERVICE_KERNEL_DRIVER | SERVICE_FILE_SYSTEM_DRIVER));
            SIZE_T length = wcslen(path) + wcslen(arguments) + 4;
            service.Command = MimeMsiAllocate(length * sizeof(WCHAR));
            if (!service.Command)
                error = ERROR_NOT_ENOUGH_MEMORY;
            else
                _snwprintf(service.Command, length, L"%s%s%s%s%s", quote ? L"\"" : L"", path, quote ? L"\"" : L"",
                           *arguments ? L" " : L"", arguments);
        }
        if (!error)
            error = MimeMsiFormat(package, text_of(package, table, row, L"LoadOrderGroup"), &service.Group);
        if (!error)
            error = format_list(package, raw_dependencies ? raw_dependencies : L"", &service.Dependencies, &chars);
        if (!error)
            error = MimeMsiFormat(package, text_of(package, table, row, L"StartName"), &service.Account);
        if (!error)
            error = MimeMsiFormat(package, text_of(package, table, row, L"Password"), &service.Password);
        if (!error)
            error = MimeMsiFormat(package, text_of(package, table, row, L"Description"), &service.Description);
        MimeMsiFree(arguments);
        MimeMsiFree(path);
        if (!error) {
            service.Type = (DWORD)type;
            service.Start = (DWORD)start;
            service.Vital = control != MSI_NULL_INTEGER && (control & 0x8000);
            service.Error = control == MSI_NULL_INTEGER ? 1 : (DWORD)control & ~0x8000u;
            DWORD failure = install_one(package, &service);
            if (failure) {
                /* only a vital service, or one that is severe about errors, takes the install down with it. */
                BOOL fatal = service.Vital || service.Error > 1;
                DbgPrint("msi: service %ws could not be installed, error %u%s\n", service.Name, failure,
                         fatal ? "" : ", which is ignored");
                if (fatal)
                    error = ERROR_INSTALL_FAILURE;
            }
        }
        install_row_free(&service);
    }
    return error;
}

/* ------------------------------------------------------------------ the actions */

static UINT run_queued(MIME_MSI_PACKAGE *package, UINT_PTR which)
{
    switch (which) {
    case RUN_CONFIGURE:
        return configure_services(package);
    case RUN_INSTALL:
        return install_services(package);
    case RUN_START:
        return control_services(package, CONTROL_START);
    }
    return ERROR_INSTALL_FAILURE;
}

static BOOL has_rows(MIME_MSI_PACKAGE *package, PCWSTR name, UINT *error)
{
    MIME_MSI_TABLE *table;
    *error = optional_table(package, name, &table);
    return !*error && table && table->Rows;
}

/* the actions that create, configure and start services wait for the files, those that stop and delete do not. */
UINT MimeMsiServiceAction(MIME_MSI_PACKAGE *package, PCWSTR action)
{
    static const struct {
        PCWSTR Name, Table, Second;
        UINT_PTR Run;
        ULONG Kind;
    } actions[] = {
        {L"MsiConfigureServices", L"MsiServiceConfig", L"MsiServiceConfigFailureActions", RUN_CONFIGURE, 0},
        {L"InstallServices", L"ServiceInstall", nullptr, RUN_INSTALL, 0},
        {L"StartServices", L"ServiceControl", nullptr, RUN_START, 0},
        {L"StopServices", L"ServiceControl", nullptr, 0, CONTROL_STOP},
        {L"DeleteServices", L"ServiceControl", nullptr, 0, CONTROL_DELETE},
    };
    for (ULONG i = 0; i < RTL_NUMBER_OF(actions); i++) {
        if (wcscmp(action, actions[i].Name))
            continue;
        UINT error;
        BOOL rows = has_rows(package, actions[i].Table, &error);
        if (!error && !rows && actions[i].Second)
            rows = has_rows(package, actions[i].Second, &error);
        if (error || !rows)
            return error;
        if (!package->TransactionActive || (package->Options & MSIOPENPACKAGEFLAGS_IGNOREMACHINESTATE))
            return ERROR_FUNCTION_NOT_CALLED;
        if (actions[i].Run)
            return MimeMsiQueueAction(package, run_queued, actions[i].Run);
        return control_services(package, actions[i].Kind);
    }
    return UINT32_MAX;
}
