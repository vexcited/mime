/*
 * services.exe: the database. each key below HKLM\SYSTEM\CurrentControlSet\Services is a service. names are read at
 * startup and a record is filled from its key on first use, so the hundreds of keys of an installation cost nothing
 * until something asks for them.
 */
#include "services.h"

static const WCHAR g_root[] = L"SYSTEM\\CurrentControlSet\\Services";
SERVICE *g_services;
BOOL g_database_ready;

/* ------------------------------------------------------------------ registry */

HKEY DbOpenKey(const WCHAR *name, REGSAM access, BOOL create)
{
    WCHAR path[sizeof(g_root) / sizeof(WCHAR) + SVC_NAME_MAX + 2];
    _snwprintf(path, RTL_NUMBER_OF(path) - 1, L"%ls\\%ls", g_root, name);
    path[RTL_NUMBER_OF(path) - 1] = 0;
    HKEY key = nullptr;
    LSTATUS status;
    if (create)
        status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, nullptr, 0, access, nullptr, &key, nullptr);
    else
        status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, access, &key);
    SetLastError((DWORD)status);
    return status == ERROR_SUCCESS ? key : nullptr;
}

/* expands REG_EXPAND_SZ; the caller frees the result. */
WCHAR *DbReadString(HKEY key, const WCHAR *value, DWORD *chars)
{
    DWORD type = 0, size = 0;
    if (RegQueryValueExW(key, value, nullptr, &type, nullptr, &size) != ERROR_SUCCESS)
        return nullptr;
    if (type != REG_SZ && type != REG_EXPAND_SZ && type != REG_MULTI_SZ)
        return nullptr;
    WCHAR *text = SvcAllocZero(size + 4 * sizeof(WCHAR));
    if (!text)
        return nullptr;
    if (RegQueryValueExW(key, value, nullptr, &type, (BYTE *)text, &size) != ERROR_SUCCESS) {
        SvcFree(text);
        return nullptr;
    }
    DWORD n = size / sizeof(WCHAR);
    if (type != REG_MULTI_SZ) {
        text[n] = 0;
        n = (DWORD)wcslen(text);
        if (type == REG_EXPAND_SZ && wcschr(text, L'%')) {
            DWORD need = ExpandEnvironmentStringsW(text, nullptr, 0);
            WCHAR *expanded = need ? SvcAlloc(need * sizeof(WCHAR)) : nullptr;
            if (expanded && ExpandEnvironmentStringsW(text, expanded, need)) {
                SvcFree(text);
                text = expanded;
                n = (DWORD)wcslen(text);
            } else {
                SvcFree(expanded);
            }
        }
    } else {
        text[n] = text[n + 1] = 0;
        n = SvcMultiSzChars(text);
    }
    if (chars)
        *chars = n;
    return text;
}

BOOL DbReadDword(HKEY key, const WCHAR *value, DWORD *out)
{
    DWORD type = 0, size = sizeof(*out), data = 0;
    if (RegQueryValueExW(key, value, nullptr, &type, (BYTE *)&data, &size) != ERROR_SUCCESS || type != REG_DWORD ||
        size != sizeof(data))
        return FALSE;
    *out = data;
    return TRUE;
}

DWORD DbWriteString(HKEY key, const WCHAR *value, const WCHAR *text, DWORD type)
{
    DWORD bytes = type == REG_MULTI_SZ ? SvcMultiSzChars(text) * sizeof(WCHAR) : (DWORD)(wcslen(text) + 1) * sizeof(WCHAR);
    return (DWORD)RegSetValueExW(key, value, 0, type, (const BYTE *)text, bytes);
}

DWORD DbWriteDword(HKEY key, const WCHAR *value, DWORD data)
{
    return (DWORD)RegSetValueExW(key, value, 0, REG_DWORD, (const BYTE *)&data, sizeof(data));
}

/* ------------------------------------------------------------------ service list */

static void free_record(SERVICE *s)
{
    SvcFree(s->DisplayName);
    SvcFree(s->Group);
    SvcFree(s->DepServices);
    SvcFree(s->DepGroups);
    if (s->Sd)
        LocalFree(s->Sd);
    SvcFree(s);
}

/* keeps the list sorted by name, the order enumerations answer in. */
static void insert(SERVICE *s)
{
    SERVICE **link = &g_services;
    while (*link && _wcsicmp((*link)->Name, s->Name) < 0)
        link = &(*link)->Next;
    s->Next = *link;
    *link = s;
}

const WCHAR *SvcDisplayName(const SERVICE *s)
{
    return s->DisplayName ? s->DisplayName : s->Name;
}

SERVICE *DbFind(const WCHAR *name)
{
    for (SERVICE *s = g_services; s; s = s->Next) {
        int c = _wcsicmp(s->Name, name);
        if (!c)
            return s;
        if (c > 0)
            break;
    }
    return nullptr;
}

SERVICE *DbFindByDisplay(const WCHAR *display)
{
    for (SERVICE *s = g_services; s; s = s->Next) {
        DbLoadRecord(s);
        if (!_wcsicmp(SvcDisplayName(s), display))
            return s;
    }
    return nullptr;
}

/* reads the key names only; records are filled when needed. */
void DbLoadNames(void)
{
    HKEY root;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, g_root, 0, KEY_READ, &root) != ERROR_SUCCESS)
        return;
    for (DWORD i = 0;; i++) {
        WCHAR name[SVC_NAME_MAX + 2];
        DWORD n = RTL_NUMBER_OF(name);
        LSTATUS status = RegEnumKeyExW(root, i, name, &n, nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS)
            break;
        if (status != ERROR_SUCCESS || n > SVC_NAME_MAX)
            continue;
        SERVICE *s = SvcAllocZero(sizeof(*s));
        if (!s)
            break;
        memcpy(s->Name, name, n * sizeof(WCHAR));
        s->Status.dwCurrentState = SERVICE_STOPPED;
        insert(s);
    }
    RegCloseKey(root);
}

BOOL DbDriverRunning(SERVICE *s)
{
    /* a driver is running when its object exists in the object manager's \Driver directory. */
    UNICODE_STRING dir = RTL_CONSTANT_STRING(L"\\Driver");
    UNICODE_STRING fs = RTL_CONSTANT_STRING(L"\\FileSystem");
    for (int k = 0; k < 2; k++) {
        OBJECT_ATTRIBUTES oa;
        InitializeObjectAttributes(&oa, k ? &fs : &dir, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
        HANDLE directory;
        if (!NT_SUCCESS(NtOpenDirectoryObject(&directory, DIRECTORY_QUERY, &oa)))
            continue;
        BYTE buffer[1024];
        ULONG context = 0, length;
        BOOLEAN restart = TRUE;
        for (;;) {
            NTSTATUS status = NtQueryDirectoryObject(directory, buffer, sizeof(buffer), TRUE, restart, &context, &length);
            restart = FALSE;
            if (!NT_SUCCESS(status))
                break;
            struct {
                UNICODE_STRING Name, TypeName;
            } *info = (void *)buffer;
            if (info->Name.Length / sizeof(WCHAR) == wcslen(s->Name) &&
                !_wcsnicmp(info->Name.Buffer, s->Name, info->Name.Length / sizeof(WCHAR))) {
                NtClose(directory);
                return TRUE;
            }
        }
        NtClose(directory);
    }
    return FALSE;
}

/* ------------------------------------------------------------------ loading records */

BOOL DbLoadRecord(SERVICE *s)
{
    if (s->Loaded)
        return s->Type != 0;
    s->Loaded = TRUE;
    HKEY key = DbOpenKey(s->Name, KEY_READ, FALSE);
    if (!key)
        return FALSE;
    DWORD type = 0, start = SERVICE_DEMAND_START, error_control = SERVICE_ERROR_NORMAL, tag = 0, delayed = 0, flag = 0;
    if (!DbReadDword(key, L"Type", &type) || !type) {
        RegCloseKey(key);
        return FALSE;
    }
    DbReadDword(key, L"Start", &start);
    DbReadDword(key, L"ErrorControl", &error_control);
    DbReadDword(key, L"Tag", &tag);
    s->Type = type;
    s->Start = start;
    s->ErrorControl = error_control;
    s->Tag = tag;
    if (DbReadDword(key, L"DelayedAutostart", &delayed) && delayed && start == SERVICE_AUTO_START)
        s->DelayedStart = TRUE;
    if (DbReadDword(key, L"FailureActionsOnNonCrashFailures", &flag) && flag)
        s->FailureOnNonCrash = TRUE;
    WCHAR *display = DbReadString(key, L"DisplayName", nullptr);
    /* an indirect string names a resource the manager would load; the key name stands in for it. */
    if (display && (!*display || display[0] == L'@')) {
        SvcFree(display);
        display = nullptr;
    }
    s->DisplayName = display;
    s->Group = DbReadString(key, L"Group", nullptr);
    if (s->Group && !*s->Group) {
        SvcFree(s->Group);
        s->Group = nullptr;
    }
    s->DepServices = DbReadString(key, L"DependOnService", nullptr);
    s->DepGroups = DbReadString(key, L"DependOnGroup", nullptr);
    s->Status.dwServiceType = type;
    s->Status.dwCurrentState = SERVICE_STOPPED;
    s->Status.dwWin32ExitCode = ERROR_SERVICE_NEVER_STARTED;
    RegCloseKey(key);
    return TRUE;
}

/* ------------------------------------------------------------------ dependencies */

/* whether s depends on other, even indirectly; a group stands for its members. */
BOOL DbDependsOn(SERVICE *s, SERVICE *other, DWORD depth)
{
    if (depth > 64)
        return TRUE;
    for (const WCHAR *d = s->DepServices; d && *d; d += wcslen(d) + 1) {
        SERVICE *dep = DbFind(d);
        if (!dep || !DbLoadRecord(dep))
            continue;
        if (dep == other || DbDependsOn(dep, other, depth + 1))
            return TRUE;
    }
    for (const WCHAR *g = s->DepGroups; g && *g; g += wcslen(g) + 1) {
        for (SERVICE *m = g_services; m; m = m->Next) {
            if (!DbLoadRecord(m) || !DbGroupMember(m, g))
                continue;
            if (m == other || (m != s && DbDependsOn(m, other, depth + 1)))
                return TRUE;
        }
    }
    return FALSE;
}

/* the direct dependents of s, or only the ones not stopped. */
void DbDependents(SERVICE *s, SERVICE ***list, DWORD *count, BOOL active_only)
{
    DWORD capacity = 16, n = 0;
    SERVICE **out = SvcAlloc(capacity * sizeof(*out));
    for (SERVICE *m = g_services; m && out; m = m->Next) {
        if (m == s || !DbLoadRecord(m))
            continue;
        BOOL depends = FALSE;
        for (const WCHAR *d = m->DepServices; d && *d && !depends; d += wcslen(d) + 1)
            depends = !_wcsicmp(d, s->Name);
        for (const WCHAR *g = m->DepGroups; g && *g && !depends; g += wcslen(g) + 1)
            depends = DbGroupMember(s, g);
        if (!depends || (active_only && m->Status.dwCurrentState == SERVICE_STOPPED))
            continue;
        if (n == capacity) {
            capacity *= 2;
            SERVICE **grown = SvcAlloc(capacity * sizeof(*out));
            if (!grown) {
                SvcFree(out);
                out = nullptr;
                break;
            }
            memcpy(grown, out, n * sizeof(*out));
            SvcFree(out);
            out = grown;
        }
        out[n++] = m;
    }
    *list = out;
    *count = out ? n : 0;
}

/* a group's position in ServiceGroupOrder, or all ones for a group that is not listed and for no group. */
DWORD DbGroupIndex(const WCHAR *group)
{
    static WCHAR *order;
    static BOOL read;
    if (!read) {
        read = TRUE;
        HKEY key;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\ServiceGroupOrder", 0, KEY_READ, &key) ==
            ERROR_SUCCESS) {
            order = DbReadString(key, L"List", nullptr);
            RegCloseKey(key);
        }
    }
    if (!group || !*group)
        return (DWORD)-1;
    DWORD i = 0;
    for (const WCHAR *g = order; g && *g; g += wcslen(g) + 1, i++)
        if (!_wcsicmp(g, group))
            return i;
    return (DWORD)-1;
}

/* whether the service is in the group. a request for TDI is also met by the members of PNP_TDI. */
BOOL DbGroupMember(const SERVICE *s, const WCHAR *group)
{
    if (!s->Group)
        return FALSE;
    return !_wcsicmp(s->Group, group) || (!_wcsicmp(group, L"TDI") && !_wcsicmp(s->Group, L"PNP_TDI"));
}

/* ------------------------------------------------------------------ failure actions */

/*
 * the value is a header of five numbers: the reset period, the offsets of the reboot message and the command, the action
 * count and the offset of the actions, followed by the data. other tools write it, so the offsets are taken as stored
 * and no layout is assumed.
 */
DWORD DbReadFailureActions(SERVICE *s, SERVICE_FAILURE_ACTIONSW *actions, SC_ACTION *buffer, WCHAR **reboot,
                           WCHAR **command)
{
    memset(actions, 0, sizeof(*actions));
    *reboot = *command = nullptr;
    HKEY key = DbOpenKey(s->Name, KEY_READ, FALSE);
    if (!key)
        return ERROR_FILE_NOT_FOUND;
    DWORD type = 0, size = 0;
    BYTE *blob = nullptr;
    DWORD error = ERROR_FILE_NOT_FOUND;
    if (RegQueryValueExW(key, L"FailureActions", nullptr, &type, nullptr, &size) == ERROR_SUCCESS && type == REG_BINARY &&
        size >= sizeof(FAILURE_BLOB) && (blob = SvcAlloc(size))) {
        if (RegQueryValueExW(key, L"FailureActions", nullptr, &type, blob, &size) == ERROR_SUCCESS) {
            FAILURE_BLOB h;
            memcpy(&h, blob, sizeof(h));
            actions->dwResetPeriod = h.ResetPeriod;
            DWORD count = h.ActionCount;
            if (count > SVC_MAX_FAILURE_ACTIONS)
                count = SVC_MAX_FAILURE_ACTIONS;
            if (h.Actions >= sizeof(h) && (UINT64)h.Actions + (UINT64)count * 8 <= size) {
                for (DWORD i = 0; i < count; i++) {
                    DWORD two[2];
                    memcpy(two, blob + h.Actions + 8 * i, 8);
                    buffer[i].Type = (SC_ACTION_TYPE)two[0];
                    buffer[i].Delay = two[1];
                }
                actions->cActions = count;
                actions->lpsaActions = buffer;
            }
            if (h.RebootMessage >= sizeof(h) && h.RebootMessage < size)
                *reboot = SvcDupN((WCHAR *)(blob + h.RebootMessage), wcsnlen((WCHAR *)(blob + h.RebootMessage), (size - h.RebootMessage) / 2));
            if (h.Command >= sizeof(h) && h.Command < size)
                *command = SvcDupN((WCHAR *)(blob + h.Command), wcsnlen((WCHAR *)(blob + h.Command), (size - h.Command) / 2));
            error = ERROR_SUCCESS;
        }
    }
    SvcFree(blob);
    RegCloseKey(key);
    return error;
}

/* ------------------------------------------------------------------ service parameters */

/* a program type, possibly interactive. the caller tells drivers apart. */
static BOOL is_program_type(DWORD type)
{
    switch (type & ~(DWORD)SERVICE_INTERACTIVE_PROCESS) {
    case SERVICE_WIN32_OWN_PROCESS:
    case SERVICE_WIN32_SHARE_PROCESS:
    case SERVICE_USER_OWN_PROCESS:
    case SERVICE_USER_SHARE_PROCESS:
    case 0xD0: /* user own process instance. */
    case 0xE0: /* user share process instance. */
    case 0x210: /* package own process. */
        return TRUE;
    default:
        return FALSE;
    }
}

/* windows' parameter rules: SERVICE_NO_CHANGE leaves a value alone, and a creation must give them all. */
DWORD DbCheckParameters(BOOL creating, const WCHAR *name, DWORD type, DWORD start, DWORD error_control,
                        const WCHAR *path, const WCHAR *group, const WCHAR *dependencies, DWORD dependencies_chars,
                        const WCHAR *account)
{
    (void)account;
    if (creating && (type == SERVICE_NO_CHANGE || start == SERVICE_NO_CHANGE || error_control == SERVICE_NO_CHANGE ||
                     !path || !*path))
        return ERROR_INVALID_PARAMETER;
    if (type != SERVICE_NO_CHANGE && !is_program_type(type) && type != SERVICE_KERNEL_DRIVER &&
        type != SERVICE_FILE_SYSTEM_DRIVER)
        return ERROR_INVALID_PARAMETER;
    if (start != SERVICE_NO_CHANGE) {
        if (start > SERVICE_DISABLED)
            return ERROR_INVALID_PARAMETER;
        /* boot and system start are for drivers. */
        if (start < SERVICE_AUTO_START && type != SERVICE_NO_CHANGE && type != SERVICE_KERNEL_DRIVER &&
            type != SERVICE_FILE_SYSTEM_DRIVER)
            return ERROR_INVALID_PARAMETER;
    }
    if (error_control != SERVICE_NO_CHANGE && error_control > SERVICE_ERROR_CRITICAL)
        return ERROR_INVALID_PARAMETER;
    if (path && !*path)
        return ERROR_INVALID_PARAMETER;
    if (dependencies && dependencies_chars) {
        if (!SvcValidMultiSz(dependencies, dependencies_chars))
            return ERROR_INVALID_PARAMETER;
        for (const WCHAR *d = dependencies; *d; d += wcslen(d) + 1) {
            if (*d == L'+') {
                if (group && !_wcsicmp(d + 1, group))
                    return ERROR_CIRCULAR_DEPENDENCY;
            } else if (!_wcsicmp(d, name)) {
                return ERROR_CIRCULAR_DEPENDENCY;
            }
        }
    }
    return ERROR_SUCCESS;
}

/* splits CreateService dependencies into services and groups; a plus sign marks a group. */
static void split_dependencies(const WCHAR *dependencies, DWORD chars, WCHAR **services, WCHAR **groups)
{
    *services = *groups = nullptr;
    if (!dependencies || !chars)
        return;
    WCHAR *s = SvcAllocZero((chars + 2) * sizeof(WCHAR));
    WCHAR *g = SvcAllocZero((chars + 2) * sizeof(WCHAR));
    DWORD sn = 0, gn = 0;
    for (const WCHAR *d = dependencies; s && g && *d; d += wcslen(d) + 1) {
        BOOL group = *d == L'+';
        size_t n = wcslen(d + group);
        memcpy(group ? g + gn : s + sn, d + group, (n + 1) * sizeof(WCHAR));
        if (group)
            gn += (DWORD)n + 1;
        else
            sn += (DWORD)n + 1;
    }
    if (sn) {
        *services = s;
    } else {
        SvcFree(s);
    }
    if (gn) {
        *groups = g;
    } else {
        SvcFree(g);
    }
}

static DWORD write_dependencies(HKEY key, const WCHAR *services, const WCHAR *groups)
{
    DWORD error = ERROR_SUCCESS;
    if (services)
        error = DbWriteString(key, L"DependOnService", services, REG_MULTI_SZ);
    else
        RegDeleteValueW(key, L"DependOnService");
    if (!error && groups)
        error = DbWriteString(key, L"DependOnGroup", groups, REG_MULTI_SZ);
    else if (!groups)
        RegDeleteValueW(key, L"DependOnGroup");
    return error;
}

/* LocalSystem or a built in service account; an empty account means LocalSystem. */
static BOOL system_account(const WCHAR *account)
{
    return !account || !*account || !_wcsicmp(account, L"LocalSystem") || !_wcsicmp(account, L"NT AUTHORITY\\SYSTEM") ||
           !_wcsicmp(account, L"NT AUTHORITY\\LocalService") || !_wcsicmp(account, L"NT AUTHORITY\\NetworkService") ||
           !_wcsicmp(account, L"NT AUTHORITY\\LocalSystem");
}

/* the virtual account of a service is named for the service, and has no password. */
static BOOL virtual_account(const WCHAR *account, const WCHAR *service)
{
    static const WCHAR prefix[] = L"NT SERVICE\\";
    return account && service && !_wcsnicmp(account, prefix, RTL_NUMBER_OF(prefix) - 1) &&
           !_wcsicmp(account + RTL_NUMBER_OF(prefix) - 1, service);
}

static DWORD validate_account(const WCHAR *account, const WCHAR *service)
{
    if (system_account(account) || virtual_account(account, service))
        return ERROR_SUCCESS;
    BYTE sid[SECURITY_MAX_SID_SIZE_];
    DWORD sid_size = sizeof(sid), domain_size = 256, use;
    WCHAR domain[256];
    /* local names only: ".\name", "machine\name" or "name". */
    const WCHAR *name = account;
    const WCHAR *slash = wcschr(account, L'\\');
    if (slash) {
        if (!(slash == account + 1 && account[0] == L'.')) {
            WCHAR computer[64];
            DWORD n = RTL_NUMBER_OF(computer);
            if (!GetComputerNameW(computer, &n) || (size_t)(slash - account) != n || _wcsnicmp(account, computer, n))
                return ERROR_INVALID_SERVICE_ACCOUNT;
        }
        name = slash + 1;
    }
    if (!LookupAccountNameW(nullptr, name, (PSID)sid, &sid_size, domain, &domain_size, &use))
        return ERROR_INVALID_SERVICE_ACCOUNT;
    return ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ create and change */

DWORD DbCreate(const WCHAR *name, const WCHAR *display, DWORD type, DWORD start, DWORD error_control, const WCHAR *path,
               const WCHAR *group, DWORD *tag, const WCHAR *dependencies, DWORD dependencies_chars,
               const WCHAR *account, const WCHAR *password, SERVICE **out)
{
    (void)password;
    DWORD error = DbCheckParameters(TRUE, name, type, start, error_control, path, group, dependencies,
                                    dependencies_chars, account);
    if (error)
        return error;
    if (tag && (!group || !*group))
        return ERROR_INVALID_PARAMETER;
    BOOL win32 = (type & (SERVICE_WIN32 | SERVICE_USER_SERVICE)) != 0;
    if (win32) {
        error = validate_account(account, name);
        if (error)
            return error;
        if ((type & SERVICE_INTERACTIVE_PROCESS) && !system_account(account))
            return ERROR_INVALID_PARAMETER;
    } else if (type & SERVICE_INTERACTIVE_PROCESS) {
        return ERROR_INVALID_PARAMETER;
    }
    SERVICE *existing = DbFind(name);
    if (existing && !DbLoadRecord(existing)) {
        /* an empty key left by an interrupted create or delete: OpenService does not find it, so it is no service. */
        DbRemove(existing);
        existing = nullptr;
    }
    if (existing)
        return existing->MarkedForDelete ? ERROR_SERVICE_MARKED_FOR_DELETE : ERROR_SERVICE_EXISTS;
    if (display && *display) {
        if (wcsnlen(display, SVC_NAME_MAX + 1) > SVC_NAME_MAX)
            return ERROR_INVALID_NAME;
        SERVICE *clash = DbFind(display);
        if (clash)
            return ERROR_DUPLICATE_SERVICE_NAME;
    }
    HKEY key = DbOpenKey(name, KEY_ALL_ACCESS, TRUE);
    if (!key)
        return GetLastError();
    WCHAR *dep_services, *dep_groups;
    split_dependencies(dependencies, dependencies_chars, &dep_services, &dep_groups);
    DWORD new_tag = 0;
    if (tag) {
        /* tags are unique within a group: take the next one after the highest. */
        for (SERVICE *m = g_services; m; m = m->Next)
            if (DbLoadRecord(m) && m->Group && !_wcsicmp(m->Group, group) && m->Tag >= new_tag)
                new_tag = m->Tag + 1;
        if (!new_tag)
            new_tag = 1;
    }
    error = DbWriteDword(key, L"Type", type);
    if (!error)
        error = DbWriteDword(key, L"Start", start);
    if (!error)
        error = DbWriteDword(key, L"ErrorControl", error_control);
    if (!error)
        error = DbWriteString(key, L"ImagePath", path, REG_EXPAND_SZ);
    if (!error && display && *display)
        error = DbWriteString(key, L"DisplayName", display, REG_SZ);
    if (!error && group && *group)
        error = DbWriteString(key, L"Group", group, REG_SZ);
    if (!error && tag)
        error = DbWriteDword(key, L"Tag", new_tag);
    if (!error)
        error = write_dependencies(key, dep_services, dep_groups);
    if (!error && win32)
        error = DbWriteString(key, L"ObjectName", account && *account ? account : L"LocalSystem", REG_SZ);
    else if (!error && account && *account)
        error = DbWriteString(key, L"ObjectName", account, REG_SZ);
    RegCloseKey(key);
    if (error) {
        SvcFree(dep_services);
        SvcFree(dep_groups);
        HKEY root;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, g_root, 0, KEY_ALL_ACCESS, &root) == ERROR_SUCCESS) {
            RegDeleteTreeW(root, name);
            RegCloseKey(root);
        }
        return error;
    }
    SERVICE *s = SvcAllocZero(sizeof(*s));
    if (!s) {
        SvcFree(dep_services);
        SvcFree(dep_groups);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    memcpy(s->Name, name, wcsnlen(name, SVC_NAME_MAX) * sizeof(WCHAR));
    s->Loaded = TRUE;
    s->Type = type;
    s->Start = start;
    s->ErrorControl = error_control;
    s->Tag = new_tag;
    s->DisplayName = display && *display && _wcsicmp(display, name) ? SvcDup(display) : nullptr;
    s->Group = group && *group ? SvcDup(group) : nullptr;
    s->DepServices = dep_services;
    s->DepGroups = dep_groups;
    s->Status.dwServiceType = type;
    s->Status.dwCurrentState = SERVICE_STOPPED;
    s->Status.dwWin32ExitCode = ERROR_SERVICE_NEVER_STARTED;
    s->Stamp = ++g_stamp;
    insert(s);
    /* refuse a cycle through the existing dependencies. */
    if (DbDependsOn(s, s, 0)) {
        DbRemove(s);
        return ERROR_CIRCULAR_DEPENDENCY;
    }
    if (tag)
        *tag = new_tag;
    *out = s;
    return ERROR_SUCCESS;
}

DWORD DbChange(SERVICE *s, DWORD type, DWORD start, DWORD error_control, const WCHAR *path, const WCHAR *group,
               DWORD *tag, const WCHAR *dependencies, DWORD dependencies_chars, const WCHAR *account,
               const WCHAR *password, const WCHAR *display)
{
    (void)password;
    DWORD new_type = type == SERVICE_NO_CHANGE ? s->Type : type;
    const WCHAR *own_group = group ? group : s->Group;
    DWORD error = DbCheckParameters(FALSE, s->Name, type, start, error_control, path, own_group, dependencies,
                                    dependencies_chars, account);
    if (error)
        return error;
    /* a driver cannot become a program or the reverse. */
    if (type != SERVICE_NO_CHANGE && ((type & (SERVICE_WIN32 | SERVICE_USER_SERVICE)) != 0) !=
                                         ((s->Type & (SERVICE_WIN32 | SERVICE_USER_SERVICE)) != 0))
        return ERROR_INVALID_PARAMETER;
    if (start != SERVICE_NO_CHANGE && start < SERVICE_AUTO_START && new_type != SERVICE_KERNEL_DRIVER &&
        new_type != SERVICE_FILE_SYSTEM_DRIVER)
        return ERROR_INVALID_PARAMETER;
    if (tag && (!own_group || !*own_group))
        return ERROR_INVALID_PARAMETER;
    BOOL win32 = (new_type & (SERVICE_WIN32 | SERVICE_USER_SERVICE)) != 0;
    if (account && win32) {
        error = validate_account(account, s->Name);
        if (error)
            return error;
        if ((new_type & SERVICE_INTERACTIVE_PROCESS) && !system_account(account))
            return ERROR_INVALID_PARAMETER;
    }
    if (display && *display) {
        if (wcsnlen(display, SVC_NAME_MAX + 1) > SVC_NAME_MAX)
            return ERROR_INVALID_NAME;
        SERVICE *clash = DbFind(display);
        if (clash && clash != s)
            return ERROR_DUPLICATE_SERVICE_NAME;
    }
    HKEY key = DbOpenKey(s->Name, KEY_ALL_ACCESS, FALSE);
    if (!key)
        return GetLastError();
    WCHAR *dep_services = nullptr, *dep_groups = nullptr;
    BOOL deps = dependencies != nullptr;
    if (deps)
        split_dependencies(dependencies, dependencies_chars, &dep_services, &dep_groups);
    error = ERROR_SUCCESS;
    if (type != SERVICE_NO_CHANGE)
        error = DbWriteDword(key, L"Type", type);
    if (!error && start != SERVICE_NO_CHANGE) {
        error = DbWriteDword(key, L"Start", start);
        /* only an automatic start can stay delayed. */
        if (!error && start != SERVICE_AUTO_START)
            RegDeleteValueW(key, L"DelayedAutostart");
    }
    if (!error && error_control != SERVICE_NO_CHANGE)
        error = DbWriteDword(key, L"ErrorControl", error_control);
    if (!error && path)
        error = DbWriteString(key, L"ImagePath", path, REG_EXPAND_SZ);
    if (!error && group) {
        if (*group)
            error = DbWriteString(key, L"Group", group, REG_SZ);
        else
            RegDeleteValueW(key, L"Group");
    }
    if (!error && tag) {
        DWORD new_tag = 0;
        for (SERVICE *m = g_services; m; m = m->Next)
            if (m != s && DbLoadRecord(m) && m->Group && own_group && !_wcsicmp(m->Group, own_group) && m->Tag >= new_tag)
                new_tag = m->Tag + 1;
        if (!new_tag)
            new_tag = 1;
        error = DbWriteDword(key, L"Tag", new_tag);
        *tag = new_tag;
        s->Tag = new_tag;
    }
    if (!error && deps)
        error = write_dependencies(key, dep_services, dep_groups);
    if (!error && account)
        error = DbWriteString(key, L"ObjectName", *account ? account : L"LocalSystem", REG_SZ);
    if (!error && display) {
        if (*display)
            error = DbWriteString(key, L"DisplayName", display, REG_SZ);
        else
            RegDeleteValueW(key, L"DisplayName");
    }
    RegCloseKey(key);
    if (error) {
        SvcFree(dep_services);
        SvcFree(dep_groups);
        return error;
    }
    if (type != SERVICE_NO_CHANGE) {
        s->Type = type;
        if (s->Status.dwCurrentState == SERVICE_STOPPED)
            s->Status.dwServiceType = type;
    }
    if (start != SERVICE_NO_CHANGE) {
        s->Start = start;
        if (start != SERVICE_AUTO_START)
            s->DelayedStart = FALSE;
    }
    if (error_control != SERVICE_NO_CHANGE)
        s->ErrorControl = error_control;
    if (group) {
        SvcFree(s->Group);
        s->Group = *group ? SvcDup(group) : nullptr;
    }
    if (deps) {
        SvcFree(s->DepServices);
        SvcFree(s->DepGroups);
        s->DepServices = dep_services;
        s->DepGroups = dep_groups;
        if (DbDependsOn(s, s, 0)) {
            /* the new list closes a cycle. like other failures after the write, the old list is not restored in the
               key; the dependencies are cleared instead. */
            HKEY k = DbOpenKey(s->Name, KEY_ALL_ACCESS, FALSE);
            if (k) {
                RegDeleteValueW(k, L"DependOnService");
                RegDeleteValueW(k, L"DependOnGroup");
                RegCloseKey(k);
            }
            SvcFree(s->DepServices);
            SvcFree(s->DepGroups);
            s->DepServices = s->DepGroups = nullptr;
            return ERROR_CIRCULAR_DEPENDENCY;
        }
    }
    if (display) {
        SvcFree(s->DisplayName);
        s->DisplayName = *display && _wcsicmp(display, s->Name) ? SvcDup(display) : nullptr;
    }
    s->Stamp = ++g_stamp;
    return ERROR_SUCCESS;
}

void DbRemove(SERVICE *s)
{
    for (SERVICE **link = &g_services; *link; link = &(*link)->Next) {
        if (*link == s) {
            *link = s->Next;
            break;
        }
    }
    HKEY root;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, g_root, 0, KEY_ALL_ACCESS, &root) == ERROR_SUCCESS) {
        RegDeleteTreeW(root, s->Name);
        RegCloseKey(root);
    }
    free_record(s);
}
