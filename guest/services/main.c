/*
 * services.exe: the client pipe and its operations. each operation is one request and one answer, run by the
 * connection's thread. handles belong to the client process, not the connection, so any connection can use them.
 */
#include "services.h"

static CLIENT *g_clients;
static WCHAR g_lock_owner[256];
static ULONGLONG g_lock_since;
static CLIENT *g_lock_holder;

typedef struct CONN {
    HANDLE Pipe;
    HANDLE Event;
    DWORD Pid;
} CONN;

/* ------------------------------------------------------------------ clients and their handles */

static void handle_close_locked(CLIENT *cl, SCHANDLE *h);

static void client_rundown(CLIENT *cl)
{
    while (cl->Handles)
        handle_close_locked(cl, cl->Handles);
    NotifyClientGone(cl);
    if (g_lock_holder == cl)
        g_lock_holder = nullptr;
    if (cl->NotifyPipe) {
        CloseHandle(cl->NotifyPipe);
        cl->NotifyPipe = nullptr;
    }
    for (CLIENT **link = &g_clients; *link; link = &(*link)->Next) {
        if (*link == cl) {
            *link = cl->Next;
            break;
        }
    }
    if (cl->Process)
        CloseHandle(cl->Process);
    SvcFree(cl);
}

static void NTAPI client_exited(PVOID context, BOOLEAN timed_out)
{
    (void)timed_out;
    CLIENT *cl = context;
    EnterCriticalSection(&g_lock);
    if (cl->Refs)
        cl->Dead = TRUE;
    else
        client_rundown(cl);
    LeaveCriticalSection(&g_lock);
}

static CLIENT *client_get(DWORD pid)
{
    for (CLIENT *cl = g_clients; cl; cl = cl->Next)
        if (cl->Pid == pid)
            return cl;
    CLIENT *cl = SvcAllocZero(sizeof(*cl));
    if (!cl)
        return nullptr;
    cl->Pid = pid;
    cl->Next = g_clients;
    g_clients = cl;
    cl->Process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (cl->Process)
        RegisterWaitForSingleObject(&cl->Wait, cl->Process, client_exited, cl, INFINITE, WT_EXECUTEONLYONCE);
    return cl;
}

static SCHANDLE *handle_find(CLIENT *cl, UINT64 id, DWORD kind)
{
    for (SCHANDLE *h = cl->Handles; h; h = h->Next)
        if (h->Id == id && h->Kind == kind)
            return h;
    return nullptr;
}

static SCHANDLE *handle_any(CLIENT *cl, UINT64 id)
{
    for (SCHANDLE *h = cl->Handles; h; h = h->Next)
        if (h->Id == id)
            return h;
    return nullptr;
}

static SCHANDLE *handle_new(CLIENT *cl, DWORD kind, DWORD access, SERVICE *s)
{
    SCHANDLE *h = SvcAllocZero(sizeof(*h));
    if (!h)
        return nullptr;
    h->Id = SvcNewId();
    h->Kind = kind;
    h->Access = access;
    h->Service = s;
    h->Next = cl->Handles;
    cl->Handles = h;
    if (s)
        s->OpenHandles++;
    return h;
}

/* a deleted service is freed once nothing holds it and it has stopped. */
static void remove_if_done(SERVICE *s)
{
    if (!s->MarkedForDelete || s->OpenHandles || s->StartPending || s->Status.dwCurrentState != SERVICE_STOPPED)
        return;
    WCHAR name[SVC_NAME_MAX + 1];
    SvcCopyName(name, s->Name, SVC_NAME_MAX);
    name[SVC_NAME_MAX] = 0;
    NotifyServiceDeleting(s, TRUE);
    DbRemove(s);
    NotifyCreatedDeleted(name, FALSE);
    WakeAllConditionVariable(&g_changed);
}

void MainCollect(void)
{
    for (SERVICE *s = g_services, *next; s; s = next) {
        next = s->Next;
        remove_if_done(s);
    }
}

static void handle_close_locked(CLIENT *cl, SCHANDLE *h)
{
    for (SCHANDLE **link = &cl->Handles; *link; link = &(*link)->Next) {
        if (*link == h) {
            *link = h->Next;
            break;
        }
    }
    SERVICE *s = h->Service;
    SvcFree(h);
    if (s) {
        if (s->OpenHandles)
            s->OpenHandles--;
        remove_if_done(s);
    }
}

/* ------------------------------------------------------------------ the connection */

static DWORD io_wait(CONN *c, OVERLAPPED *ov, BOOL started, DWORD *transferred)
{
    if (!started && GetLastError() != ERROR_IO_PENDING)
        return GetLastError();
    return GetOverlappedResult(c->Pipe, ov, transferred, TRUE) ? ERROR_SUCCESS : GetLastError();
}

static DWORD conn_read(CONN *c, void *data, DWORD size)
{
    BYTE *p = data;
    while (size) {
        OVERLAPPED ov = {};
        ov.hEvent = c->Event;
        ResetEvent(c->Event);
        DWORD done = 0;
        BOOL started = ReadFile(c->Pipe, p, size, &done, &ov);
        DWORD error = io_wait(c, &ov, started, &done);
        if (error)
            return error;
        if (!done)
            return ERROR_BROKEN_PIPE;
        p += done;
        size -= done;
    }
    return ERROR_SUCCESS;
}

static DWORD conn_write(CONN *c, const void *data, DWORD size)
{
    const BYTE *p = data;
    while (size) {
        OVERLAPPED ov = {};
        ov.hEvent = c->Event;
        ResetEvent(c->Event);
        DWORD done = 0;
        BOOL started = WriteFile(c->Pipe, p, size, &done, &ov);
        DWORD error = io_wait(c, &ov, started, &done);
        if (error)
            return error;
        p += done;
        size -= done;
    }
    return ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ operation helpers */

static BOOL valid_service_name(const WCHAR *name)
{
    if (!name || !*name)
        return FALSE;
    size_t n = wcslen(name);
    return n <= SVC_NAME_MAX && !wcschr(name, L'\\') && !wcschr(name, L'/');
}

/* a length-prefixed string, NULL stays NULL. the caller frees the copy. */
static WCHAR *get_string(SCM_READER *r, BOOL *present)
{
    UINT32 chars;
    const WCHAR *s = ScmGetChars(r, &chars);
    if (present)
        *present = s != nullptr;
    if (!s)
        return nullptr;
    return SvcDupN(s, chars);
}

/* a SERVICE_STATUS_PROCESS as nine DWORDs. */
static void put_status(SCM_WRITER *w, SERVICE *s)
{
    SERVICE_STATUS_PROCESS p;
    RunStatusSnapshot(s, &p);
    const DWORD *f = (const DWORD *)&p;
    for (int i = 0; i < 9; i++)
        ScmPutU32(w, f[i]);
}

static BOOL need(SCHANDLE *h, DWORD access)
{
    return (h->Access & access) == access;
}

static BOOL list_active(SERVICE *s, DWORD state_mask)
{
    SERVICE_STATUS_PROCESS p;
    RunStatusSnapshot(s, &p);
    BOOL active = p.dwCurrentState != SERVICE_STOPPED;
    return (active && (state_mask & SERVICE_ACTIVE)) || (!active && (state_mask & SERVICE_INACTIVE));
}

/* ------------------------------------------------------------------ operations */

static DWORD op_open_manager(CONN *c, CLIENT *cl, SCM_READER *r, SCM_WRITER *w)
{
    BOOL has_database;
    WCHAR *database = get_string(r, &has_database);
    DWORD desired = ScmGetU32(r);
    if (r->Failed) {
        SvcFree(database);
        return ERROR_INVALID_PARAMETER;
    }
    if (g_shutdown) {
        SvcFree(database);
        return ERROR_SHUTDOWN_IN_PROGRESS;
    }
    /* only ServicesActive opens. ServicesFailed fails as a missing database. */
    if (database && _wcsicmp(database, L"ServicesActive") && _wcsicmp(database, L"ServicesFailed")) {
        SvcFree(database);
        return ERROR_INVALID_NAME;
    }
    if (database && !_wcsicmp(database, L"ServicesFailed")) {
        SvcFree(database);
        return ERROR_DATABASE_DOES_NOT_EXIST;
    }
    SvcFree(database);
    DWORD granted = 0;
    LeaveCriticalSection(&g_lock);
    DWORD error = AccessCheckClient(c->Pipe, g_manager_sd, desired | SC_MANAGER_CONNECT, &g_manager_mapping, &granted);
    EnterCriticalSection(&g_lock);
    if (error)
        return error;
    SCHANDLE *h = handle_new(cl, SCH_MANAGER, granted, nullptr);
    if (!h)
        return ERROR_NOT_ENOUGH_MEMORY;
    ScmPutU64(w, h->Id);
    return ERROR_SUCCESS;
}

static DWORD op_open_service(CONN *c, CLIENT *cl, SCM_READER *r, SCM_WRITER *w)
{
    UINT64 manager = ScmGetU64(r);
    WCHAR *name = get_string(r, nullptr);
    DWORD desired = ScmGetU32(r);
    DWORD error = ERROR_INVALID_PARAMETER;
    if (r->Failed)
        goto done;
    if (g_shutdown) {
        error = ERROR_SHUTDOWN_IN_PROGRESS;
        goto done;
    }
    SCHANDLE *m = handle_find(cl, manager, SCH_MANAGER);
    if (!m) {
        error = ERROR_INVALID_HANDLE;
        goto done;
    }
    if (!valid_service_name(name)) {
        error = ERROR_INVALID_NAME;
        goto done;
    }
    SERVICE *s = DbFind(name);
    if (!s || !DbLoadRecord(s)) {
        error = ERROR_SERVICE_DOES_NOT_EXIST;
        goto done;
    }
    error = AccessReadSd(s);
    if (error)
        goto done;
    s->OpenHandles++; /* keeps it alive while the lock is dropped for the check. */
    PSECURITY_DESCRIPTOR sd = s->Sd;
    DWORD granted = 0;
    LeaveCriticalSection(&g_lock);
    error = AccessCheckClient(c->Pipe, sd, desired, &g_service_mapping, &granted);
    EnterCriticalSection(&g_lock);
    s->OpenHandles--;
    if (error)
        goto done;
    SCHANDLE *h = handle_new(cl, SCH_SERVICE, granted, s);
    if (!h) {
        error = ERROR_NOT_ENOUGH_MEMORY;
        goto done;
    }
    ScmPutU64(w, h->Id);
done:
    SvcFree(name);
    return error;
}

static DWORD op_close(CLIENT *cl, SCM_READER *r)
{
    UINT64 id = ScmGetU64(r);
    SCHANDLE *h = handle_any(cl, id);
    if (!h)
        return ERROR_INVALID_HANDLE;
    handle_close_locked(cl, h);
    return ERROR_SUCCESS;
}

static DWORD op_create(CONN *c, CLIENT *cl, SCM_READER *r, SCM_WRITER *w)
{
    (void)c;
    UINT64 manager = ScmGetU64(r);
    WCHAR *name = get_string(r, nullptr);
    BOOL has_display, has_path, has_group, has_account, has_password;
    WCHAR *display = get_string(r, &has_display);
    DWORD access = ScmGetU32(r);
    DWORD type = ScmGetU32(r), start = ScmGetU32(r), error_control = ScmGetU32(r);
    WCHAR *path = get_string(r, &has_path);
    WCHAR *group = get_string(r, &has_group);
    DWORD want_tag = ScmGetU32(r);
    UINT32 dep_chars;
    const WCHAR *deps = ScmGetChars(r, &dep_chars);
    WCHAR *dependencies = deps ? SvcDupN(deps, dep_chars) : nullptr;
    WCHAR *account = get_string(r, &has_account);
    WCHAR *password = get_string(r, &has_password);
    DWORD error = ERROR_INVALID_PARAMETER;
    if (r->Failed)
        goto done;
    if (g_shutdown) {
        error = ERROR_SHUTDOWN_IN_PROGRESS;
        goto done;
    }
    SCHANDLE *m = handle_find(cl, manager, SCH_MANAGER);
    if (!m) {
        error = ERROR_INVALID_HANDLE;
        goto done;
    }
    if (!need(m, SC_MANAGER_CREATE_SERVICE)) {
        error = ERROR_ACCESS_DENIED;
        goto done;
    }
    if (!valid_service_name(name)) {
        error = ERROR_INVALID_NAME;
        goto done;
    }
    SERVICE *s;
    DWORD tag = 0;
    error = DbCreate(name, display, type, start, error_control, path, group, want_tag ? &tag : nullptr, dependencies,
                     dep_chars, account, password, &s);
    if (error)
        goto done;
    s->Sd = nullptr;
    /* the creator gets the access it asked for, the descriptor is the default. */
    AccessReadSd(s);
    SCHANDLE *h = handle_new(cl, SCH_SERVICE, access, s);
    if (!h) {
        error = ERROR_NOT_ENOUGH_MEMORY;
        goto done;
    }
    ScmPutU64(w, h->Id);
    ScmPutU32(w, tag);
    NotifyCreatedDeleted(s->Name, TRUE);
    WakeAllConditionVariable(&g_changed);
done:
    SvcFree(name);
    SvcFree(display);
    SvcFree(path);
    SvcFree(group);
    SvcFree(dependencies);
    SvcFree(account);
    SvcFree(password);
    return error;
}

static DWORD op_delete(CLIENT *cl, SCM_READER *r)
{
    UINT64 id = ScmGetU64(r);
    if (g_shutdown)
        return ERROR_SHUTDOWN_IN_PROGRESS;
    SCHANDLE *h = handle_find(cl, id, SCH_SERVICE);
    if (!h)
        return ERROR_INVALID_HANDLE;
    if (!need(h, DELETE))
        return ERROR_ACCESS_DENIED;
    SERVICE *s = h->Service;
    if (s->MarkedForDelete)
        return ERROR_SERVICE_MARKED_FOR_DELETE;
    s->MarkedForDelete = TRUE;
    NotifyServiceDeleting(s, FALSE);
    return ERROR_SUCCESS;
}

static DWORD op_change_config(CLIENT *cl, SCM_READER *r, SCM_WRITER *w)
{
    UINT64 id = ScmGetU64(r);
    DWORD type = ScmGetU32(r), start = ScmGetU32(r), error_control = ScmGetU32(r);
    BOOL has_path, has_group, has_account, has_password, has_display;
    WCHAR *path = get_string(r, &has_path);
    WCHAR *group = get_string(r, &has_group);
    DWORD want_tag = ScmGetU32(r);
    UINT32 dep_chars;
    const WCHAR *deps = ScmGetChars(r, &dep_chars);
    WCHAR *dependencies = deps ? SvcDupN(deps, dep_chars) : nullptr;
    WCHAR *account = get_string(r, &has_account);
    WCHAR *password = get_string(r, &has_password);
    WCHAR *display = get_string(r, &has_display);
    DWORD error = ERROR_INVALID_PARAMETER;
    if (r->Failed)
        goto done;
    if (g_shutdown) {
        error = ERROR_SHUTDOWN_IN_PROGRESS;
        goto done;
    }
    SCHANDLE *h = handle_find(cl, id, SCH_SERVICE);
    if (!h) {
        error = ERROR_INVALID_HANDLE;
        goto done;
    }
    if (!need(h, SERVICE_CHANGE_CONFIG)) {
        error = ERROR_ACCESS_DENIED;
        goto done;
    }
    SERVICE *s = h->Service;
    if (s->MarkedForDelete) {
        error = ERROR_SERVICE_MARKED_FOR_DELETE;
        goto done;
    }
    DWORD tag = 0;
    error = DbChange(s, type, start, error_control, path, group, want_tag ? &tag : nullptr, dependencies, dep_chars,
                     account, password, display);
    if (!error)
        ScmPutU32(w, tag);
done:
    SvcFree(path);
    SvcFree(group);
    SvcFree(dependencies);
    SvcFree(account);
    SvcFree(password);
    SvcFree(display);
    return error;
}

/* the FailureActions value: the header, then the strings, then the action list. */
static DWORD write_failure_actions(SERVICE *s, DWORD reset, const WCHAR *reboot, const WCHAR *command, DWORD count,
                                   const SC_ACTION *actions)
{
    HKEY key = DbOpenKey(s->Name, KEY_ALL_ACCESS, FALSE);
    if (!key)
        return GetLastError();
    DWORD reboot_bytes = reboot ? (DWORD)(wcslen(reboot) + 1) * 2 : 0;
    DWORD command_bytes = command ? (DWORD)(wcslen(command) + 1) * 2 : 0;
    DWORD total = sizeof(FAILURE_BLOB) + reboot_bytes + command_bytes + count * 8;
    BYTE *blob = SvcAllocZero(total + 8);
    if (!blob) {
        RegCloseKey(key);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    FAILURE_BLOB h = {reset, 0, 0, count, 0};
    DWORD at = sizeof(h);
    if (reboot) {
        h.RebootMessage = at;
        memcpy(blob + at, reboot, reboot_bytes);
        at += reboot_bytes;
    }
    if (command) {
        h.Command = at;
        memcpy(blob + at, command, command_bytes);
        at += command_bytes;
    }
    h.Actions = at;
    for (DWORD i = 0; i < count; i++) {
        DWORD two[2] = {(DWORD)actions[i].Type, actions[i].Delay};
        memcpy(blob + at + 8 * i, two, 8);
    }
    memcpy(blob, &h, sizeof(h));
    DWORD error = (DWORD)RegSetValueExW(key, L"FailureActions", 0, REG_BINARY, blob, total);
    SvcFree(blob);
    RegCloseKey(key);
    return error;
}

static DWORD op_change_config2(CLIENT *cl, SCM_READER *r)
{
    UINT64 id = ScmGetU64(r);
    DWORD level = ScmGetU32(r);
    if (r->Failed)
        return ERROR_INVALID_PARAMETER;
    if (g_shutdown)
        return ERROR_SHUTDOWN_IN_PROGRESS;
    SCHANDLE *h = handle_find(cl, id, SCH_SERVICE);
    if (!h)
        return ERROR_INVALID_HANDLE;
    if (!need(h, SERVICE_CHANGE_CONFIG))
        return ERROR_ACCESS_DENIED;
    SERVICE *s = h->Service;
    if (s->MarkedForDelete)
        return ERROR_SERVICE_MARKED_FOR_DELETE;
    HKEY key = DbOpenKey(s->Name, KEY_ALL_ACCESS, FALSE);
    if (!key)
        return GetLastError();
    DWORD error = ERROR_SUCCESS;
    switch (level) {
    case SERVICE_CONFIG_DESCRIPTION: {
        BOOL present;
        WCHAR *text = get_string(r, &present);
        /* a NULL pointer keeps the description, an empty string removes it. */
        if (present) {
            if (*text)
                error = DbWriteString(key, L"Description", text, REG_SZ);
            else
                RegDeleteValueW(key, L"Description");
        }
        SvcFree(text);
        break;
    }
    case SERVICE_CONFIG_FAILURE_ACTIONS: {
        DWORD reset = ScmGetU32(r);
        BOOL has_reset = ScmGetU32(r);
        (void)has_reset;
        BOOL has_reboot, has_command;
        WCHAR *reboot = get_string(r, &has_reboot);
        WCHAR *command = get_string(r, &has_command);
        DWORD count = ScmGetU32(r);
        if (count > SVC_MAX_FAILURE_ACTIONS || r->Failed) {
            error = ERROR_INVALID_PARAMETER;
        } else {
            SC_ACTION actions[SVC_MAX_FAILURE_ACTIONS];
            for (DWORD i = 0; i < count; i++) {
                actions[i].Type = (SC_ACTION_TYPE)ScmGetU32(r);
                actions[i].Delay = ScmGetU32(r);
                if (actions[i].Type > SC_ACTION_OWN_RESTART)
                    error = ERROR_INVALID_PARAMETER;
            }
            if (!error && r->Failed)
                error = ERROR_INVALID_PARAMETER;
            if (!error) {
                /* no list keeps the existing actions, an empty one with a zero count removes them. */
                if (count == 0 && !has_reboot && !has_command && !reset)
                    RegDeleteValueW(key, L"FailureActions");
                else
                    error = write_failure_actions(s, reset, reboot, command, count, actions);
            }
        }
        SvcFree(reboot);
        SvcFree(command);
        break;
    }
    case SERVICE_CONFIG_DELAYED_AUTO_START_INFO: {
        DWORD delayed = ScmGetU32(r);
        if (s->Start != SERVICE_AUTO_START) {
            error = ERROR_INVALID_PARAMETER;
        } else {
            error = DbWriteDword(key, L"DelayedAutostart", delayed != 0);
            if (!error) {
                if (delayed)
                    s->DelayedStart = TRUE;
                else
                    s->DelayedStart = FALSE;
            }
        }
        break;
    }
    case SERVICE_CONFIG_FAILURE_ACTIONS_FLAG: {
        DWORD flag = ScmGetU32(r);
        error = DbWriteDword(key, L"FailureActionsOnNonCrashFailures", flag != 0);
        if (!error) {
            if (flag)
                s->FailureOnNonCrash = TRUE;
            else
                s->FailureOnNonCrash = FALSE;
        }
        break;
    }
    case SERVICE_CONFIG_SERVICE_SID_INFO: {
        DWORD type = ScmGetU32(r);
        if (type != SERVICE_SID_TYPE_NONE && type != SERVICE_SID_TYPE_UNRESTRICTED && type != SERVICE_SID_TYPE_RESTRICTED)
            error = ERROR_INVALID_PARAMETER;
        else if (!(s->Type & SERVICE_WIN32))
            error = ERROR_INVALID_PARAMETER;
        else
            error = DbWriteDword(key, L"ServiceSidType", type);
        break;
    }
    case SERVICE_CONFIG_REQUIRED_PRIVILEGES_INFO: {
        UINT32 chars;
        const WCHAR *p = ScmGetChars(r, &chars);
        if (r->Failed || (p && !SvcValidMultiSz(p, chars)) || !(s->Type & SERVICE_WIN32)) {
            error = ERROR_INVALID_PARAMETER;
        } else if (!p || !chars || !*p) {
            RegDeleteValueW(key, L"RequiredPrivileges");
        } else {
            WCHAR *copy = SvcDupN(p, chars);
            error = copy ? DbWriteString(key, L"RequiredPrivileges", copy, REG_MULTI_SZ) : ERROR_NOT_ENOUGH_MEMORY;
            SvcFree(copy);
        }
        break;
    }
    case SERVICE_CONFIG_PRESHUTDOWN_INFO: {
        DWORD timeout = ScmGetU32(r);
        error = (s->Type & SERVICE_WIN32) ? DbWriteDword(key, L"PreshutdownTimeout", timeout) : ERROR_INVALID_PARAMETER;
        break;
    }
    case SERVICE_CONFIG_LAUNCH_PROTECTED: {
        DWORD protection = ScmGetU32(r);
        error = protection <= 3 ? DbWriteDword(key, L"LaunchProtected", protection) : ERROR_INVALID_PARAMETER;
        break;
    }
    case SERVICE_CONFIG_TRIGGER_INFO:
    case SERVICE_CONFIG_PREFERRED_NODE:
        error = ERROR_NOT_SUPPORTED;
        break;
    default:
        error = ERROR_INVALID_LEVEL;
        break;
    }
    RegCloseKey(key);
    if (!error)
        s->Stamp = ++g_stamp;
    return error;
}

static DWORD op_query_config(CLIENT *cl, SCM_READER *r, SCM_WRITER *w)
{
    UINT64 id = ScmGetU64(r);
    if (g_shutdown)
        return ERROR_SHUTDOWN_IN_PROGRESS;
    SCHANDLE *h = handle_find(cl, id, SCH_SERVICE);
    if (!h)
        return ERROR_INVALID_HANDLE;
    if (!need(h, SERVICE_QUERY_CONFIG))
        return ERROR_ACCESS_DENIED;
    SERVICE *s = h->Service;
    /* read from the key every time, like windows. */
    HKEY key = DbOpenKey(s->Name, KEY_READ, FALSE);
    if (!key)
        return ERROR_FILE_NOT_FOUND;
    DWORD type = s->Type, start = s->Start, error_control = s->ErrorControl, tag = 0;
    DbReadDword(key, L"Type", &type);
    DbReadDword(key, L"Start", &start);
    DbReadDword(key, L"ErrorControl", &error_control);
    DbReadDword(key, L"Tag", &tag);
    WCHAR *path = DbReadString(key, L"ImagePath", nullptr);
    WCHAR *group = DbReadString(key, L"Group", nullptr);
    WCHAR *account = DbReadString(key, L"ObjectName", nullptr);
    WCHAR *display = DbReadString(key, L"DisplayName", nullptr);
    WCHAR *services = DbReadString(key, L"DependOnService", nullptr);
    WCHAR *groups = DbReadString(key, L"DependOnGroup", nullptr);
    RegCloseKey(key);
    ScmPutU32(w, type);
    ScmPutU32(w, start);
    ScmPutU32(w, error_control);
    ScmPutString(w, path ? path : L"");
    ScmPutString(w, group ? group : L"");
    ScmPutU32(w, tag);
    /* services first, then groups, each prefixed with a plus sign. */
    DWORD service_chars = services ? SvcMultiSzChars(services) - 1 : 0;
    DWORD group_count_chars = 0;
    for (const WCHAR *g = groups; g && *g; g += wcslen(g) + 1)
        group_count_chars += (DWORD)wcslen(g) + 2;
    WCHAR *list = SvcAllocZero((service_chars + group_count_chars + 2) * sizeof(WCHAR));
    DWORD at = 0;
    if (list) {
        if (services && service_chars) {
            memcpy(list, services, service_chars * sizeof(WCHAR));
            at = service_chars;
        }
        for (const WCHAR *g = groups; g && *g; g += wcslen(g) + 1) {
            list[at++] = L'+';
            size_t n = wcslen(g);
            memcpy(list + at, g, (n + 1) * sizeof(WCHAR));
            at += (DWORD)n + 1;
        }
    }
    ScmPutChars(w, list, at + 1);
    BOOL driver = (type & (SERVICE_KERNEL_DRIVER | SERVICE_FILE_SYSTEM_DRIVER | SERVICE_RECOGNIZER_DRIVER)) != 0;
    ScmPutString(w, account ? account : (driver ? nullptr : L"LocalSystem"));
    ScmPutString(w, display ? display : SvcDisplayName(s));
    SvcFree(list);
    SvcFree(path);
    SvcFree(group);
    SvcFree(account);
    SvcFree(display);
    SvcFree(services);
    SvcFree(groups);
    return ERROR_SUCCESS;
}

static DWORD op_query_config2(CLIENT *cl, SCM_READER *r, SCM_WRITER *w)
{
    UINT64 id = ScmGetU64(r);
    DWORD level = ScmGetU32(r);
    if (g_shutdown)
        return ERROR_SHUTDOWN_IN_PROGRESS;
    SCHANDLE *h = handle_find(cl, id, SCH_SERVICE);
    if (!h)
        return ERROR_INVALID_HANDLE;
    if (!need(h, SERVICE_QUERY_CONFIG))
        return ERROR_ACCESS_DENIED;
    SERVICE *s = h->Service;
    HKEY key = DbOpenKey(s->Name, KEY_READ, FALSE);
    if (!key)
        return ERROR_FILE_NOT_FOUND;
    DWORD error = ERROR_SUCCESS, value = 0;
    switch (level) {
    case SERVICE_CONFIG_DESCRIPTION: {
        WCHAR *text = DbReadString(key, L"Description", nullptr);
        /* an indirect string would be loaded from its file, one that cannot be is returned as is. */
        ScmPutString(w, text);
        SvcFree(text);
        break;
    }
    case SERVICE_CONFIG_FAILURE_ACTIONS: {
        SERVICE_FAILURE_ACTIONSW actions;
        SC_ACTION buffer[SVC_MAX_FAILURE_ACTIONS];
        WCHAR *reboot, *command;
        DbReadFailureActions(s, &actions, buffer, &reboot, &command);
        ScmPutU32(w, actions.dwResetPeriod);
        ScmPutString(w, reboot);
        ScmPutString(w, command);
        ScmPutU32(w, actions.cActions);
        for (DWORD i = 0; i < actions.cActions; i++) {
            ScmPutU32(w, (DWORD)actions.lpsaActions[i].Type);
            ScmPutU32(w, actions.lpsaActions[i].Delay);
        }
        SvcFree(reboot);
        SvcFree(command);
        break;
    }
    case SERVICE_CONFIG_DELAYED_AUTO_START_INFO:
        DbReadDword(key, L"DelayedAutostart", &value);
        ScmPutU32(w, value != 0 && s->Start == SERVICE_AUTO_START);
        break;
    case SERVICE_CONFIG_FAILURE_ACTIONS_FLAG:
        DbReadDword(key, L"FailureActionsOnNonCrashFailures", &value);
        ScmPutU32(w, value != 0);
        break;
    case SERVICE_CONFIG_SERVICE_SID_INFO:
        DbReadDword(key, L"ServiceSidType", &value);
        ScmPutU32(w, value);
        break;
    case SERVICE_CONFIG_REQUIRED_PRIVILEGES_INFO: {
        WCHAR *p = DbReadString(key, L"RequiredPrivileges", nullptr);
        ScmPutChars(w, p, p ? SvcMultiSzChars(p) : 0);
        SvcFree(p);
        break;
    }
    case SERVICE_CONFIG_PRESHUTDOWN_INFO:
        value = 10000;
        DbReadDword(key, L"PreshutdownTimeout", &value);
        ScmPutU32(w, value);
        break;
    case SERVICE_CONFIG_TRIGGER_INFO:
        ScmPutU32(w, 0);
        break;
    case SERVICE_CONFIG_LAUNCH_PROTECTED:
        DbReadDword(key, L"LaunchProtected", &value);
        ScmPutU32(w, value);
        break;
    default:
        error = ERROR_INVALID_LEVEL;
        break;
    }
    RegCloseKey(key);
    return error;
}

static DWORD op_query_status(CLIENT *cl, SCM_READER *r, SCM_WRITER *w)
{
    UINT64 id = ScmGetU64(r);
    if (g_shutdown)
        return ERROR_SHUTDOWN_IN_PROGRESS;
    SCHANDLE *h = handle_find(cl, id, SCH_SERVICE);
    if (!h)
        return ERROR_INVALID_HANDLE;
    if (!need(h, SERVICE_QUERY_STATUS))
        return ERROR_ACCESS_DENIED;
    put_status(w, h->Service);
    return ERROR_SUCCESS;
}

typedef struct ENUM_ENTRY {
    SERVICE *Service;
} ENUM_ENTRY;

static DWORD op_enum(CLIENT *cl, SCM_READER *r, SCM_WRITER *w)
{
    UINT64 id = ScmGetU64(r);
    DWORD type_mask = ScmGetU32(r), state_mask = ScmGetU32(r), resume = ScmGetU32(r);
    BOOL has_group;
    WCHAR *group = get_string(r, &has_group);
    DWORD error = ERROR_SUCCESS;
    if (r->Failed) {
        SvcFree(group);
        return ERROR_INVALID_PARAMETER;
    }
    if (g_shutdown) {
        SvcFree(group);
        return ERROR_SHUTDOWN_IN_PROGRESS;
    }
    SCHANDLE *h = handle_find(cl, id, SCH_MANAGER);
    if (!h) {
        SvcFree(group);
        return ERROR_INVALID_HANDLE;
    }
    if (!need(h, SC_MANAGER_ENUMERATE_SERVICE)) {
        SvcFree(group);
        return ERROR_ACCESS_DENIED;
    }
    /* unknown type or state bits are refused. the error is a guess from the documented ones. */
    if (!type_mask || (type_mask & ~(DWORD)SERVICE_TYPE_ALL) || !state_mask || (state_mask & ~(DWORD)SERVICE_STATE_ALL)) {
        SvcFree(group);
        return ERROR_INVALID_PARAMETER;
    }
    /* the matching services from the resume index on. the caller packs as many as its buffer holds. */
    DWORD index = 0, count = 0;
    UINT32 count_offset = w->Size;
    ScmPutU32(w, 0);
    for (SERVICE *s = g_services; s; s = s->Next) {
        if (!DbLoadRecord(s) || !(s->Type & type_mask))
            continue;
        if (has_group) {
            const WCHAR *g = s->Group ? s->Group : L"";
            if (_wcsicmp(g, group))
                continue;
        }
        if (!list_active(s, state_mask))
            continue;
        if (index++ < resume)
            continue;
        ScmPutString(w, s->Name);
        ScmPutString(w, SvcDisplayName(s));
        put_status(w, s);
        count++;
    }
    if (!w->Failed)
        memcpy(w->Data + count_offset, &count, 4);
    SvcFree(group);
    return error;
}

static void dependents_order(SERVICE *s, DWORD state_mask, SERVICE **out, DWORD *n, DWORD capacity, DWORD depth)
{
    if (depth > 32)
        return;
    SERVICE **list;
    DWORD count;
    DbDependents(s, &list, &count, FALSE);
    for (DWORD i = 0; i < count; i++) {
        BOOL seen = FALSE;
        for (DWORD k = 0; k < *n; k++)
            seen |= out[k] == list[i];
        if (seen)
            continue;
        dependents_order(list[i], state_mask, out, n, capacity, depth + 1);
        for (DWORD k = 0; k < *n; k++)
            seen |= out[k] == list[i];
        if (!seen && *n < capacity && list_active(list[i], state_mask))
            out[(*n)++] = list[i];
    }
    SvcFree(list);
}

static DWORD op_enum_dependents(CLIENT *cl, SCM_READER *r, SCM_WRITER *w)
{
    UINT64 id = ScmGetU64(r);
    DWORD state_mask = ScmGetU32(r);
    if (g_shutdown)
        return ERROR_SHUTDOWN_IN_PROGRESS;
    SCHANDLE *h = handle_find(cl, id, SCH_SERVICE);
    if (!h)
        return ERROR_INVALID_HANDLE;
    if (!need(h, SERVICE_ENUMERATE_DEPENDENTS))
        return ERROR_ACCESS_DENIED;
    SERVICE *out[256];
    DWORD n = 0;
    dependents_order(h->Service, state_mask, out, &n, RTL_NUMBER_OF(out), 0);
    ScmPutU32(w, n);
    for (DWORD i = 0; i < n; i++) {
        ScmPutString(w, out[i]->Name);
        ScmPutString(w, SvcDisplayName(out[i]));
        put_status(w, out[i]);
    }
    return ERROR_SUCCESS;
}

static DWORD op_start(CLIENT *cl, SCM_READER *r)
{
    UINT64 id = ScmGetU64(r);
    DWORD count = ScmGetU32(r);
    DWORD error = ERROR_SUCCESS;
    WCHAR **args = nullptr;
    if (count > 4096 || r->Failed)
        return ERROR_INVALID_PARAMETER;
    if (count) {
        args = SvcAllocZero(count * sizeof(*args));
        if (!args)
            return ERROR_NOT_ENOUGH_MEMORY;
        for (DWORD i = 0; i < count; i++) {
            BOOL present;
            args[i] = get_string(r, &present);
            if (!present || r->Failed)
                error = ERROR_INVALID_PARAMETER;
        }
    }
    if (!error) {
        SCHANDLE *h = handle_find(cl, id, SCH_SERVICE);
        if (g_shutdown) {
            error = ERROR_SHUTDOWN_IN_PROGRESS;
        } else if (!h) {
            error = ERROR_INVALID_HANDLE;
        } else if (!need(h, SERVICE_START)) {
            error = ERROR_ACCESS_DENIED;
        } else if (g_lock_holder && g_lock_holder != cl) {
            error = ERROR_SERVICE_DATABASE_LOCKED;
        } else {
            error = RunStart(h->Service, count, (const WCHAR *const *)args, START_CLIENT);
        }
    }
    for (DWORD i = 0; i < count; i++)
        SvcFree(args ? args[i] : nullptr);
    SvcFree(args);
    return error;
}

static DWORD op_control(CLIENT *cl, SCM_READER *r, SCM_WRITER *w)
{
    UINT64 id = ScmGetU64(r);
    DWORD control = ScmGetU32(r);
    DWORD level = ScmGetU32(r);
    DWORD reason = level ? ScmGetU32(r) : 0;
    (void)reason;
    if (r->Failed)
        return ERROR_INVALID_PARAMETER;
    if (g_shutdown)
        return ERROR_SHUTDOWN_IN_PROGRESS;
    SCHANDLE *h = handle_find(cl, id, SCH_SERVICE);
    if (!h)
        return ERROR_INVALID_HANDLE;
    DWORD access;
    switch (control) {
    case SERVICE_CONTROL_STOP:
        access = SERVICE_STOP;
        break;
    case SERVICE_CONTROL_PAUSE:
    case SERVICE_CONTROL_CONTINUE:
    case SERVICE_CONTROL_PARAMCHANGE:
    case SERVICE_CONTROL_NETBINDADD:
    case SERVICE_CONTROL_NETBINDREMOVE:
    case SERVICE_CONTROL_NETBINDENABLE:
    case SERVICE_CONTROL_NETBINDDISABLE:
        access = SERVICE_PAUSE_CONTINUE;
        break;
    case SERVICE_CONTROL_INTERROGATE:
        access = SERVICE_INTERROGATE;
        break;
    case SERVICE_CONTROL_TRIGGEREVENT:
        access = SERVICE_START;
        break;
    default:
        if (control < 128 || control > 255)
            return ERROR_INVALID_PARAMETER;
        access = SERVICE_USER_DEFINED_CONTROL;
        break;
    }
    if (!need(h, access))
        return ERROR_ACCESS_DENIED;
    SERVICE *s = h->Service;
    s->OpenHandles++;
    DWORD result = 0;
    DWORD error = RunControl(s, control, 0, nullptr, 0, &result);
    if (s->OpenHandles)
        s->OpenHandles--;
    /* a user-defined control returns the handler's result, the others the call's error. */
    if (!error && control >= 128 && result)
        error = result;
    put_status(w, s);
    return error;
}

/* SCM_OP_LAUNCH_ELEVATED and SCM_OP_LAUNCH_UNELEVATED: answers with the ids and with handles in the client's process. */
static DWORD op_launch_elevated(CONN *c, CLIENT *cl, SCM_READER *r, SCM_WRITER *w, BOOL unelevated)
{
    (void)cl;
    BOOL present;
    WCHAR *application = get_string(r, &present);
    WCHAR *command_line = get_string(r, &present);
    WCHAR *directory = get_string(r, &present);
    WCHAR *desktop = get_string(r, &present);
    WCHAR *title = get_string(r, &present);
    ELEVATE_REQUEST q = {application, command_line, directory, desktop, title};
    q.CreationFlags = ScmGetU32(r);
    q.StartupFlags = ScmGetU32(r);
    q.Show = ScmGetU32(r);
    q.Unelevated = unelevated;
    DWORD error = ERROR_INVALID_PARAMETER;
    if (!r->Failed && command_line && *command_line) {
        ELEVATE_RESULT out = {};
        /* the launch is slow and needs nothing from the database. */
        LeaveCriticalSection(&g_lock);
        error = g_shutdown ? ERROR_SHUTDOWN_IN_PROGRESS : ElevateLaunch(c->Pipe, c->Pid, &q, &out);
        EnterCriticalSection(&g_lock);
        if (!error) {
            ScmPutU32(w, out.ProcessId);
            ScmPutU32(w, out.ThreadId);
            ScmPutU32(w, out.Elevation);
            ScmPutU64(w, out.Process);
            ScmPutU64(w, out.Thread);
        }
    }
    SvcFree(application);
    SvcFree(command_line);
    SvcFree(directory);
    SvcFree(desktop);
    SvcFree(title);
    return error;
}

static DWORD op_set_status(CONN *c, SCM_READER *r)
{
    UINT64 id = ScmGetU64(r);
    SERVICE_STATUS st;
    DWORD *f = (DWORD *)&st;
    for (int i = 0; i < 7; i++)
        f[i] = ScmGetU32(r);
    if (r->Failed)
        return ERROR_INVALID_PARAMETER;
    return RunSetStatus(id, c->Pid, &st);
}

static DWORD op_notify(CLIENT *cl, SCM_READER *r)
{
    UINT64 id = ScmGetU64(r);
    UINT64 block = ScmGetU64(r);
    DWORD mask = ScmGetU32(r), version = ScmGetU32(r);
    if (r->Failed)
        return ERROR_INVALID_PARAMETER;
    if (g_shutdown)
        return ERROR_SHUTDOWN_IN_PROGRESS;
    SCHANDLE *h = handle_any(cl, id);
    if (!h)
        return ERROR_INVALID_HANDLE;
    if (h->Kind == SCH_SERVICE) {
        if (!need(h, SERVICE_QUERY_STATUS))
            return ERROR_ACCESS_DENIED;
    } else if (!need(h, SC_MANAGER_ENUMERATE_SERVICE)) {
        return ERROR_ACCESS_DENIED;
    }
    return NotifyRegister(cl, h, block, mask, version, nullptr);
}

static DWORD op_query_security(CONN *c, CLIENT *cl, SCM_READER *r, SCM_WRITER *w)
{
    (void)c;
    UINT64 id = ScmGetU64(r);
    DWORD information = ScmGetU32(r), size = ScmGetU32(r);
    if (r->Failed)
        return ERROR_INVALID_PARAMETER;
    if (g_shutdown)
        return ERROR_SHUTDOWN_IN_PROGRESS;
    SCHANDLE *h = handle_any(cl, id);
    if (!h)
        return ERROR_INVALID_HANDLE;
    DWORD access = 0;
    if (information & (OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION))
        access |= READ_CONTROL;
    if (information & SACL_SECURITY_INFORMATION)
        access |= ACCESS_SYSTEM_SECURITY;
    if (!need(h, access))
        return ERROR_ACCESS_DENIED;
    PSECURITY_DESCRIPTOR sd = g_manager_sd;
    if (h->Kind == SCH_SERVICE) {
        DWORD error = AccessReadSd(h->Service);
        if (error)
            return error;
        sd = h->Service->Sd;
    }
    if (size > SCM_MAX_MESSAGE / 2)
        size = SCM_MAX_MESSAGE / 2;
    BYTE *buffer = SvcAllocZero(size + 16);
    if (!buffer)
        return ERROR_NOT_ENOUGH_MEMORY;
    DWORD needed = 0;
    DWORD error = AccessQuerySd(sd, information, buffer, size, &needed);
    if (!error) {
        ScmPutU32(w, needed);
        ScmPutBytes(w, buffer, needed);
    } else if (error == ERROR_INSUFFICIENT_BUFFER) {
        ScmPutU32(w, needed);
    }
    SvcFree(buffer);
    return error;
}

static DWORD op_set_security(CLIENT *cl, SCM_READER *r)
{
    UINT64 id = ScmGetU64(r);
    DWORD information = ScmGetU32(r);
    UINT32 length;
    const void *bytes = ScmGetBytes(r, &length);
    if (r->Failed || !bytes)
        return ERROR_INVALID_PARAMETER;
    if (g_shutdown)
        return ERROR_SHUTDOWN_IN_PROGRESS;
    SCHANDLE *h = handle_any(cl, id);
    if (!h)
        return ERROR_INVALID_HANDLE;
    DWORD access = 0;
    if (information & DACL_SECURITY_INFORMATION)
        access |= WRITE_DAC;
    if (information & (OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION))
        access |= WRITE_OWNER;
    if (information & SACL_SECURITY_INFORMATION)
        access |= ACCESS_SYSTEM_SECURITY;
    if (!access || (information & ~(OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION |
                                    SACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION)))
        return ERROR_INVALID_PARAMETER;
    if (!need(h, access))
        return ERROR_ACCESS_DENIED;
    PSECURITY_DESCRIPTOR modification = LocalAlloc(0, length);
    if (!modification)
        return ERROR_NOT_ENOUGH_MEMORY;
    memcpy(modification, bytes, length);
    if (!IsValidSecurityDescriptor(modification)) {
        LocalFree(modification);
        return ERROR_INVALID_SECURITY_DESCR_;
    }
    DWORD error;
    if (h->Kind == SCH_SERVICE) {
        error = AccessReadSd(h->Service);
        if (!error)
            error = AccessSetSd(&h->Service->Sd, information, modification);
        if (!error)
            error = AccessWriteSd(h->Service);
    } else {
        error = AccessSetSd(&g_manager_sd, information, modification);
        if (!error) {
            HKEY key = nullptr;
            if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\ServiceGroupOrder\\Security", 0,
                                nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, nullptr) == ERROR_SUCCESS) {
                RegSetValueExW(key, L"Security", 0, REG_BINARY, (const BYTE *)g_manager_sd,
                               GetSecurityDescriptorLength(g_manager_sd));
                RegCloseKey(key);
            }
        }
    }
    LocalFree(modification);
    return error;
}

static DWORD op_lock(CONN *c, CLIENT *cl, SCM_READER *r)
{
    UINT64 id = ScmGetU64(r);
    if (g_shutdown)
        return ERROR_SHUTDOWN_IN_PROGRESS;
    SCHANDLE *h = handle_find(cl, id, SCH_MANAGER);
    if (!h)
        return ERROR_INVALID_HANDLE;
    if (!need(h, SC_MANAGER_LOCK))
        return ERROR_ACCESS_DENIED;
    if (g_lock_holder && g_lock_holder != cl)
        return ERROR_SERVICE_DATABASE_LOCKED;
    if (g_lock_holder == cl)
        return ERROR_SERVICE_DATABASE_LOCKED;
    g_lock_holder = cl;
    g_lock_since = GetTickCount64();
    LeaveCriticalSection(&g_lock);
    WCHAR name[256] = L"";
    AccessClientName(c->Pipe, name, RTL_NUMBER_OF(name));
    EnterCriticalSection(&g_lock);
    SvcCopyName(g_lock_owner, name, RTL_NUMBER_OF(g_lock_owner) - 1);
    return ERROR_SUCCESS;
}

static DWORD op_unlock(CLIENT *cl)
{
    if (g_lock_holder != cl)
        return ERROR_INVALID_SERVICE_LOCK;
    g_lock_holder = nullptr;
    return ERROR_SUCCESS;
}

static DWORD op_query_lock(CLIENT *cl, SCM_READER *r, SCM_WRITER *w)
{
    UINT64 id = ScmGetU64(r);
    SCHANDLE *h = handle_find(cl, id, SCH_MANAGER);
    if (!h)
        return ERROR_INVALID_HANDLE;
    if (!need(h, SC_MANAGER_QUERY_LOCK_STATUS))
        return ERROR_ACCESS_DENIED;
    ScmPutU32(w, g_lock_holder != nullptr);
    ScmPutString(w, g_lock_holder ? g_lock_owner : L"");
    ScmPutU32(w, g_lock_holder ? (DWORD)((GetTickCount64() - g_lock_since) / 1000) : 0);
    return ERROR_SUCCESS;
}

static DWORD op_names(CLIENT *cl, SCM_READER *r, SCM_WRITER *w, BOOL display)
{
    UINT64 id = ScmGetU64(r);
    WCHAR *name = get_string(r, nullptr);
    DWORD error = ERROR_INVALID_PARAMETER;
    if (r->Failed || !name)
        goto done;
    SCHANDLE *h = handle_find(cl, id, SCH_MANAGER);
    if (!h) {
        error = ERROR_INVALID_HANDLE;
        goto done;
    }
    if (!need(h, SC_MANAGER_CONNECT)) {
        error = ERROR_ACCESS_DENIED;
        goto done;
    }
    if (display) {
        SERVICE *s = DbFind(name);
        if (!s || !DbLoadRecord(s)) {
            error = ERROR_SERVICE_DOES_NOT_EXIST;
            goto done;
        }
        ScmPutString(w, SvcDisplayName(s));
        error = ERROR_SUCCESS;
    } else {
        SERVICE *s = DbFindByDisplay(name);
        if (!s) {
            error = ERROR_SERVICE_DOES_NOT_EXIST;
            goto done;
        }
        ScmPutString(w, s->Name);
        error = ERROR_SUCCESS;
    }
done:
    SvcFree(name);
    return error;
}

/* ------------------------------------------------------------------ one request */

typedef enum { KEEP_NONE, KEEP_CHANNEL, KEEP_NOTIFY } KEEP;

static DWORD handle_request(CONN *c, DWORD op, SCM_READER *r, SCM_WRITER *w, KEEP *keep)
{
    EnterCriticalSection(&g_lock);
    CLIENT *cl = client_get(c->Pid);
    DWORD error = ERROR_NOT_ENOUGH_MEMORY;
    if (cl) {
        cl->Refs++;
        switch (op) {
        case SCM_OP_OPEN_MANAGER:
            error = op_open_manager(c, cl, r, w);
            break;
        case SCM_OP_OPEN_SERVICE:
            error = op_open_service(c, cl, r, w);
            break;
        case SCM_OP_CLOSE_HANDLE:
            error = op_close(cl, r);
            break;
        case SCM_OP_CREATE_SERVICE:
            error = op_create(c, cl, r, w);
            break;
        case SCM_OP_DELETE_SERVICE:
            error = op_delete(cl, r);
            break;
        case SCM_OP_CHANGE_CONFIG:
            error = op_change_config(cl, r, w);
            break;
        case SCM_OP_CHANGE_CONFIG2:
            error = op_change_config2(cl, r);
            break;
        case SCM_OP_QUERY_CONFIG:
            error = op_query_config(cl, r, w);
            break;
        case SCM_OP_QUERY_CONFIG2:
            error = op_query_config2(cl, r, w);
            break;
        case SCM_OP_QUERY_STATUS:
            error = op_query_status(cl, r, w);
            break;
        case SCM_OP_ENUM_SERVICES:
            error = op_enum(cl, r, w);
            break;
        case SCM_OP_ENUM_DEPENDENTS:
            error = op_enum_dependents(cl, r, w);
            break;
        case SCM_OP_START_SERVICE:
            error = op_start(cl, r);
            break;
        case SCM_OP_CONTROL_SERVICE:
            error = op_control(cl, r, w);
            break;
        case SCM_OP_NOTIFY_STATUS_CHANGE:
            error = op_notify(cl, r);
            break;
        case SCM_OP_QUERY_SECURITY:
            error = op_query_security(c, cl, r, w);
            break;
        case SCM_OP_SET_SECURITY:
            error = op_set_security(cl, r);
            break;
        case SCM_OP_LOCK_DATABASE:
            error = op_lock(c, cl, r);
            break;
        case SCM_OP_UNLOCK_DATABASE:
            error = op_unlock(cl);
            break;
        case SCM_OP_QUERY_LOCK_STATUS:
            error = op_query_lock(cl, r, w);
            break;
        case SCM_OP_GET_DISPLAY_NAME:
            error = op_names(cl, r, w, TRUE);
            break;
        case SCM_OP_GET_KEY_NAME:
            error = op_names(cl, r, w, FALSE);
            break;
        case SCM_OP_BOOT_CONFIG_STATUS:
            error = ERROR_SUCCESS;
            break;
        case SCM_OP_OPEN_CHANNEL:
            error = RunOpenChannel(c->Pipe, c->Pid);
            if (!error)
                *keep = KEEP_CHANNEL;
            break;
        case SCM_OP_SET_STATUS:
            error = op_set_status(c, r);
            break;
        case SCM_OP_SET_BITS:
            error = ERROR_SUCCESS;
            break;
        case SCM_OP_LAUNCH_ELEVATED:
            error = op_launch_elevated(c, cl, r, w, FALSE);
            break;
        case SCM_OP_LAUNCH_UNELEVATED:
            error = op_launch_elevated(c, cl, r, w, TRUE);
            break;
        case SCM_OP_NOTIFY_ATTACH:
            error = ERROR_SUCCESS;
            *keep = KEEP_NOTIFY;
            break;
        default:
            error = ERROR_INVALID_FUNCTION;
            break;
        }
        if (!--cl->Refs && cl->Dead)
            client_rundown(cl);
    }
    MainCollect();
    LeaveCriticalSection(&g_lock);
    return error;
}

static DWORD WINAPI connection(LPVOID context)
{
    CONN *c = context;
    ULONG pid = 0;
    if (!GetNamedPipeClientProcessId(c->Pipe, &pid))
        pid = 0;
    c->Pid = pid;
    c->Event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    BOOL keep_pipe = FALSE;
    for (;;) {
        SCM_HEADER header;
        if (conn_read(c, &header, sizeof(header)))
            break;
        if (header.Magic != SCM_MAGIC || header.Size < sizeof(header) || header.Size > SCM_MAX_MESSAGE)
            break;
        BYTE *message = SvcAlloc(header.Size);
        if (!message)
            break;
        memcpy(message, &header, sizeof(header));
        if (header.Size > sizeof(header) && conn_read(c, message + sizeof(header), header.Size - (DWORD)sizeof(header))) {
            SvcFree(message);
            break;
        }
        SCM_READER r;
        ScmReaderInit(&r, message, header.Size);
        SCM_WRITER w;
        ScmWriterInit(&w, 0);
        KEEP keep = KEEP_NONE;
        DWORD error = handle_request(c, header.Code, &r, &w, &keep);
        SvcFree(message);
        UINT32 size;
        UCHAR *out = ScmWriterFinish(&w, &size);
        BOOL failed = FALSE;
        if (out) {
            ((SCM_HEADER *)out)->Code = error;
            failed = conn_write(c, out, size) != 0;
        }
        SvcFree(w.Data);
        if (keep == KEEP_CHANNEL) {
            EnterCriticalSection(&g_lock);
            RunChannelOpened(c->Pipe, c->Pid, !failed && out != nullptr);
            LeaveCriticalSection(&g_lock);
            keep_pipe = TRUE;
            break;
        }
        if (keep == KEEP_NOTIFY) {
            EnterCriticalSection(&g_lock);
            CLIENT *cl = client_get(c->Pid);
            DWORD attach = cl && !failed ? NotifyAttach(cl, c->Pipe) : ERROR_NOT_ENOUGH_MEMORY;
            LeaveCriticalSection(&g_lock);
            keep_pipe = attach == ERROR_SUCCESS;
            break;
        }
        if (failed)
            break;
    }
    if (!keep_pipe) {
        DisconnectNamedPipe(c->Pipe);
        CloseHandle(c->Pipe);
    }
    CloseHandle(c->Event);
    SvcFree(c);
    return 0;
}

/* ------------------------------------------------------------------ the server */

static HANDLE make_instance(BOOL first)
{
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, FALSE};
    PSECURITY_DESCRIPTOR sd = nullptr;
    /* everyone may connect, system and administrators get full access. */
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GRGW;;;WD)(A;;GA;;;SY)(A;;GA;;;BA)", SDDL_REVISION_1, &sd,
                                                             nullptr))
        sa.lpSecurityDescriptor = sd;
    HANDLE pipe = CreateNamedPipeW(SCM_PIPE_NAME,
                                   PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
                                   PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                   PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0, &sa);
    if (sd)
        LocalFree(sd);
    return pipe;
}

static DWORD WINAPI listener(LPVOID context)
{
    HANDLE pipe = context;
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    for (;;) {
        OVERLAPPED ov = {};
        ov.hEvent = event;
        ResetEvent(event);
        BOOL connected = ConnectNamedPipe(pipe, &ov);
        DWORD error = connected ? 0 : GetLastError();
        if (error == ERROR_IO_PENDING) {
            DWORD ignored;
            error = GetOverlappedResult(pipe, &ov, &ignored, TRUE) ? 0 : GetLastError();
        }
        if (error && error != ERROR_PIPE_CONNECTED) {
            DisconnectNamedPipe(pipe);
            continue;
        }
        CONN *c = SvcAllocZero(sizeof(*c));
        HANDLE next = make_instance(FALSE);
        if (!c || !next) {
            SvcFree(c);
            if (next)
                CloseHandle(next);
            DisconnectNamedPipe(pipe);
            Sleep(10);
            continue;
        }
        c->Pipe = pipe;
        HANDLE thread = CreateThread(nullptr, 0, connection, c, 0, nullptr);
        if (thread) {
            CloseHandle(thread);
        } else {
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
            SvcFree(c);
        }
        pipe = next;
    }
}

static void read_timeouts(void)
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        /* the control pipe timeout, 30 seconds unless the key overrides it. */
        DWORD value;
        if (DbReadDword(key, L"ServicesPipeTimeout", &value))
            g_control_timeout = value;
        g_handler_timeout = g_control_timeout;
        RegCloseKey(key);
    }
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\SCMConfig", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        DWORD value;
        if (DbReadDword(key, L"AutostartServicesPipeTimeout", &value))
            g_autostart_timeout = value;
        RegCloseKey(key);
    }
    if (g_autostart_timeout < g_control_timeout)
        g_autostart_timeout = g_control_timeout;
}

void MainWake(void)
{
    WakeAllConditionVariable(&g_changed);
}

void entry(void)
{
    /* a second instance has nothing to do. */
    HANDLE done = OpenEventW(SYNCHRONIZE, FALSE, SCM_AUTOSTART_EVENT);
    if (done) {
        CloseHandle(done);
        ExitProcess(0);
    }
    InitializeCriticalSection(&g_lock);
    InitializeConditionVariable(&g_changed);
    TimerInit();
    AccessInit();
    read_timeouts();
    DbLoadNames();

    HANDLE first = make_instance(TRUE);
    if (first == INVALID_HANDLE_VALUE)
        ExitProcess(1);
    HANDLE thread = CreateThread(nullptr, 0, listener, first, 0, nullptr);
    if (thread)
        CloseHandle(thread);
    ShutdownInit();

    /* clients wait on this event. it is created here or already was by the first client. */
    HANDLE ready = SvcCreateWorldEvent(SCM_START_EVENT);
    if (ready)
        SetEvent(ready);
    BootStart();
    Sleep(INFINITE);
    ExitProcess(0);
}
