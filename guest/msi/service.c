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
enum { RUN_CONFIGURE = 1 };

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

/* ------------------------------------------------------------------ the actions */

static UINT run_queued(MIME_MSI_PACKAGE *package, UINT_PTR which)
{
    if (which == RUN_CONFIGURE)
        return configure_services(package);
    return ERROR_INSTALL_FAILURE;
}

static BOOL has_rows(MIME_MSI_PACKAGE *package, PCWSTR name, UINT *error)
{
    MIME_MSI_TABLE *table;
    *error = optional_table(package, name, &table);
    return !*error && table && table->Rows;
}

UINT MimeMsiServiceAction(MIME_MSI_PACKAGE *package, PCWSTR action)
{
    if (wcscmp(action, L"MsiConfigureServices"))
        return UINT32_MAX;
    UINT error;
    BOOL rows = has_rows(package, L"MsiServiceConfig", &error);
    if (!error && !rows)
        rows = has_rows(package, L"MsiServiceConfigFailureActions", &error);
    if (error || !rows)
        return error;
    if (!package->TransactionActive || (package->Options & MSIOPENPACKAGEFLAGS_IGNOREMACHINESTATE))
        return ERROR_FUNCTION_NOT_CALLED;
    return MimeMsiQueueAction(package, run_queued, RUN_CONFIGURE);
}
