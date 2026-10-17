/*
 * the mci core of winmm: the devices of the process, their drivers found
 * through [MCI32] and the file extensions of [MCI Extensions], the command
 * tables that name each command, and what the core does itself: open,
 * close, sysinfo, break and sound. devices a string command opens on the
 * fly live on a window thread of their own, which closes them at their
 * first notification, as windows' does.
 */
#define MIME_BUILD_WINMM
#include "winmm_private.h"
#include "mci_private.h"

#define NT_KEY L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\"
#define CORE_TABLE 200

RTL_CRITICAL_SECTION MciLock = {nullptr, -1, 0, nullptr, nullptr, 0};
MCI_TABLE MciTables[MCI_MAX_TABLES];

static MCI_DEVICE **g_devices;
static UINT g_capacity;
static HWND g_window;
static DWORD g_window_thread;

/* ------------------------------------------------------------------ devices */

BOOL MciInit(void)
{
    if (g_devices)
        return TRUE;
    RtlEnterCriticalSection(&MciLock);
    if (!g_devices) {
        g_devices = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 4 * sizeof(*g_devices));
        if (g_devices)
            g_capacity = 4;
    }
    RtlLeaveCriticalSection(&MciLock);
    return g_devices != nullptr;
}

MCI_DEVICE *MciDevice(MCIDEVICEID id)
{
    return id && id < g_capacity ? g_devices[id] : nullptr;
}

static BOOL visible(const MCI_DEVICE *d, DWORD thread)
{
    return d && d->name && !(d->open_flags & MCI_OPEN_ELEMENT_ID) && !d->closing &&
           (d->owner == thread || d->requester == thread);
}

MCIDEVICEID MciDeviceByName(const WCHAR *name, DWORD thread)
{
    if (!name)
        return 0;
    if (!lstrcmpiW(name, L"all"))
        return MCI_ALL_DEVICE_ID;
    MCIDEVICEID id = 0;
    RtlEnterCriticalSection(&MciLock);
    for (UINT i = 1; i < g_capacity; i++)
        if (visible(g_devices[i], thread) && !lstrcmpiW(g_devices[i]->name, name)) {
            id = i;
            break;
        }
    RtlLeaveCriticalSection(&MciLock);
    return id;
}

/* the lowest free slot from 1, the list growing and shrinking by 4. */
static MCI_DEVICE *new_device(void)
{
    MCI_DEVICE *d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*d));
    if (!d)
        return nullptr;
    UINT id = 1;
    while (id < g_capacity && g_devices[id])
        id++;
    if (id >= g_capacity) {
        MCI_DEVICE **grown = HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, g_devices,
                                         (g_capacity + 4) * sizeof(*g_devices));
        if (!grown) {
            HeapFree(GetProcessHeap(), 0, d);
            return nullptr;
        }
        g_devices = grown;
        g_capacity += 4;
    }
    d->id = id;
    d->type_table = d->custom_table = MCI_NO_COMMAND_TABLE;
    g_devices[id] = d;
    return d;
}

static void free_device(MCI_DEVICE *d)
{
    g_devices[d->id] = nullptr;
    UINT top = g_capacity;
    while (top > 1 && !g_devices[top - 1])
        top--;
    if (g_capacity - top > 4 && g_capacity > 4) {
        MCI_DEVICE **shrunk = HeapReAlloc(GetProcessHeap(), 0, g_devices, (g_capacity - 4) * sizeof(*g_devices));
        if (shrunk) {
            g_devices = shrunk;
            g_capacity -= 4;
        }
    }
    HeapFree(GetProcessHeap(), 0, d->name);
    HeapFree(GetProcessHeap(), 0, d);
}

static WCHAR *duplicate(const WCHAR *s)
{
    if (!s)
        return nullptr;
    int n = lstrlenW(s) + 1;
    WCHAR *copy = HeapAlloc(GetProcessHeap(), 0, n * sizeof(WCHAR));
    if (copy)
        memcpy(copy, s, n * sizeof(WCHAR));
    return copy;
}

/* ------------------------------------------------------------------ command tables */

/* an entry: its keyword, then a dword and a word, packed. */
const BYTE *MciEntry(const BYTE *p, const WCHAR **word, DWORD *value, WORD *type)
{
    *word = (const WCHAR *)p;
    while (p[0] || p[1])
        p += 2;
    p += 2;
    memcpy(value, p, 4);
    memcpy(type, p + 4, 2);
    return p + 6;
}

static UINT add_table(const BYTE *data, DWORD size, HMODULE module, UINT type)
{
    UINT index = 0;
    while (index < MCI_MAX_TABLES && MciTables[index].used)
        index++;
    if (index == MCI_MAX_TABLES)
        return MCI_NO_COMMAND_TABLE;
    /* every command head is indexed, even one inside a constant group. */
    UINT count = 0;
    const BYTE *end = data + size;
    for (const BYTE *p = data; p + 8 <= end;) {
        const WCHAR *word;
        DWORD value;
        WORD kind;
        p = MciEntry(p, &word, &value, &kind);
        if (kind == MCI_COMMAND_HEAD)
            count++;
        if (kind == MCI_END_COMMAND_LIST)
            break;
    }
    if (!count)
        return MCI_NO_COMMAND_TABLE;
    MCI_TABLE *t = &MciTables[index];
    t->data = HeapAlloc(GetProcessHeap(), 0, size + 8);
    t->commands = HeapAlloc(GetProcessHeap(), 0, count * sizeof(*t->commands));
    if (!t->data || !t->commands) {
        HeapFree(GetProcessHeap(), 0, t->data);
        HeapFree(GetProcessHeap(), 0, t->commands);
        t->data = nullptr;
        t->commands = nullptr;
        return MCI_NO_COMMAND_TABLE;
    }
    memcpy(t->data, data, size);
    memset(t->data + size, 0, 8);
    t->size = size;
    t->count = 0;
    end = t->data + size;
    for (const BYTE *p = t->data; p + 8 <= end;) {
        const BYTE *at = p;
        const WCHAR *word;
        DWORD value;
        WORD kind;
        p = MciEntry(p, &word, &value, &kind);
        if (kind == MCI_COMMAND_HEAD)
            t->commands[t->count++] = at;
        if (kind == MCI_END_COMMAND_LIST)
            break;
    }
    t->module = module;
    t->type = type;
    t->used = TRUE;
    return index;
}

static UINT load_rcdata(HMODULE module, const WCHAR *name, UINT type)
{
    HRSRC r = FindResourceW(module, name, (LPCWSTR)RT_RCDATA);
    HGLOBAL g = r ? LoadResource(module, r) : nullptr;
    const BYTE *data = g ? LockResource(g) : nullptr;
    return data ? add_table(data, SizeofResource(module, r), module, type) : MCI_NO_COMMAND_TABLE;
}

static BOOL load_core(void)
{
    if (MciTables[0].used)
        return TRUE;
    return load_rcdata(WinmmModule, MAKEINTRESOURCEW(CORE_TABLE), 0) == 0;
}

/* a "#n" name is the number n, as resource names go. */
static const WCHAR *resource_name(const WCHAR *name)
{
    if (!IS_INTRESOURCE(name) && name[0] == '#') {
        UINT n = 0;
        for (const WCHAR *p = name + 1; *p >= '0' && *p <= '9'; p++)
            n = n * 10 + (*p - '0');
        return MAKEINTRESOURCEW(n);
    }
    return name;
}

static UINT load_table(HMODULE module, const WCHAR *name, UINT type)
{
    if (!load_core())
        return MCI_NO_COMMAND_TABLE;
    if (!IS_INTRESOURCE(name) && name[0] != '#') {
        /* a file <name>.mci on the search path comes first, its first 8 characters naming it. */
        WCHAR file[16], path[MAX_PATH];
        lstrcpynW(file, name, 9);
        lstrcatW(file, L".mci");
        if (SearchPathW(nullptr, file, nullptr, MAX_PATH, path, nullptr)) {
            HMODULE data = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_AS_DATAFILE);
            if (data) {
                UINT index = load_rcdata(data, name, type);
                if (index != MCI_NO_COMMAND_TABLE)
                    return index;
                FreeLibrary(data);
            }
        }
    }
    return load_rcdata(module, resource_name(name), type);
}

static BOOL table_in_use(UINT index)
{
    for (UINT i = 1; i < g_capacity; i++)
        if (g_devices[i] && (g_devices[i]->type_table == index || g_devices[i]->custom_table == index))
            return TRUE;
    return FALSE;
}

static void free_table(UINT index)
{
    MCI_TABLE *t = &MciTables[index];
    HeapFree(GetProcessHeap(), 0, t->data);
    HeapFree(GetProcessHeap(), 0, t->commands);
    memset(t, 0, sizeof(*t));
}

static const BYTE *find_in(UINT index, const WCHAR *verb, UINT message)
{
    if (index >= MCI_MAX_TABLES || !MciTables[index].used)
        return nullptr;
    const MCI_TABLE *t = &MciTables[index];
    for (UINT i = 0; i < t->count; i++) {
        const WCHAR *word;
        DWORD value;
        WORD kind;
        MciEntry(t->commands[i], &word, &value, &kind);
        if (verb ? !lstrcmpiW(word, verb) : value == message)
            return t->commands[i];
    }
    return nullptr;
}

/* the device's own table, then its type's, then the core's; the first holding the verb wins entirely. */
static const BYTE *find_command(MCIDEVICEID id, const WCHAR *verb, UINT message, UINT *table)
{
    RtlEnterCriticalSection(&MciLock);
    load_core();
    MCI_DEVICE *d = id != MCI_ALL_DEVICE_ID ? MciDevice(id) : nullptr;
    UINT order[3] = {d ? d->custom_table : MCI_NO_COMMAND_TABLE, d ? d->type_table : MCI_NO_COMMAND_TABLE, 0};
    const BYTE *found = nullptr;
    for (int i = 0; i < 3 && !found; i++) {
        found = find_in(order[i], verb, message);
        if (found && table)
            *table = order[i];
    }
    RtlLeaveCriticalSection(&MciLock);
    return found;
}

const BYTE *MciFindCommand(MCIDEVICEID id, const WCHAR *verb, UINT *table)
{
    return find_command(id, verb, 0, table);
}

const BYTE *MciFindMessage(MCIDEVICEID id, UINT message, UINT *table)
{
    return find_command(id, nullptr, message, table);
}

WINMMAPI UINT WINAPI mciLoadCommandResource(HANDLE instance, LPCWSTR name, UINT type)
{
    RtlEnterCriticalSection(&MciLock);
    UINT index = name ? load_table((HMODULE)instance, name, type) : MCI_NO_COMMAND_TABLE;
    RtlLeaveCriticalSection(&MciLock);
    return index;
}

WINMMAPI BOOL WINAPI mciFreeCommandResource(UINT table)
{
    RtlEnterCriticalSection(&MciLock);
    BOOL ok = table < MCI_MAX_TABLES && MciTables[table].used && !table_in_use(table);
    if (ok)
        free_table(table);
    RtlLeaveCriticalSection(&MciLock);
    return ok;
}

/* ------------------------------------------------------------------ types and drivers */

/* a type name, which may end in digits, against winmm's names of 513 to 523. */
UINT MciTypeOf(const WCHAR *name)
{
    if (!name)
        return 0;
    int n = lstrlenW(name);
    while (n && name[n - 1] >= '0' && name[n - 1] <= '9')
        n--;
    for (UINT type = MCI_DEVTYPE_FIRST; type <= MCI_DEVTYPE_LAST; type++) {
        WCHAR text[32];
        int m = LoadStringW(WinmmModule, type, text, RTL_NUMBER_OF(text));
        if (m == n && CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, text, m, name, n) == CSTR_EQUAL)
            return type;
    }
    return 0;
}

static BOOL registry_value(const WCHAR *key, const WCHAR *name, WCHAR *out, DWORD chars)
{
    WCHAR path[128];
    lstrcpyW(path, NT_KEY);
    lstrcatW(path, key);
    DWORD size = chars * sizeof(WCHAR);
    return !RegGetValueW(HKEY_LOCAL_MACHINE, path, name, RRF_RT_REG_SZ, nullptr, out, &size);
}

/* the type a file's extension names in [MCI Extensions]; a "type!element" form has none. */
static BOOL type_of_file(const WCHAR *file, WCHAR *type)
{
    const WCHAR *bang = nullptr;
    for (const WCHAR *p = file + 1; *file && *p; p++)
        if (*p == '!') {
            bang = p;
            break;
        }
    if (bang) {
        WCHAR prefix[80], value[256];
        int n = (int)(bang - file);
        if (n < 80) {
            lstrcpynW(prefix, file, n + 1);
            if (registry_value(L"MCI32", prefix, value, RTL_NUMBER_OF(value)))
                return FALSE;
        }
    }
    int n = lstrlenW(file);
    const WCHAR *dot = nullptr;
    for (int i = n - 1; i >= 0 && n - i <= 32; i--) {
        if (file[i] == '/' || file[i] == '\\')
            break;
        if (file[i] == '.') {
            dot = file + i;
            break;
        }
    }
    if (!dot || !dot[1])
        return FALSE;
    return registry_value(L"MCI Extensions", dot + 1, type, 80) && type[0];
}

/*
 * the driver of a type from [MCI32]: the key itself, then the key with a
 * digit, then the key whose driver file starts with the type.
 */
static MCIERROR find_driver(const WCHAR *type, WCHAR *install, WCHAR *file, WCHAR *params)
{
    if (lstrlenW(type) > 79)
        return MCIERR_DEVICE_LENGTH;
    WCHAR value[256];
    BOOL found = FALSE;
    lstrcpyW(install, type);
    if (registry_value(L"MCI32", type, value, RTL_NUMBER_OF(value))) {
        found = TRUE;
    } else {
        for (WCHAR digit = '1'; digit <= '9' && !found; digit++) {
            wsprintfW(install, L"%s%c", type, digit);
            found = registry_value(L"MCI32", install, value, RTL_NUMBER_OF(value));
        }
    }
    if (!found) {
        HKEY key;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, NT_KEY L"MCI32", 0, KEY_QUERY_VALUE, &key))
            return MCIERR_DEVICE_NOT_INSTALLED;
        int n = lstrlenW(type);
        for (DWORD i = 0;; i++) {
            WCHAR name[256];
            DWORD name_size = RTL_NUMBER_OF(name), size = sizeof(value), kind;
            if (RegEnumValueW(key, i, name, &name_size, nullptr, &kind, (BYTE *)value, &size))
                break;
            const WCHAR *base = value;
            for (const WCHAR *p = value; *p && *p != ' '; p++)
                if (*p == '\\' || *p == '/' || *p == ':')
                    base = p + 1;
            if (!_wcsnicmp(base, type, n) && (base[n] == '.' || base[n] == ' ')) {
                lstrcpyW(install, name);
                found = TRUE;
                break;
            }
            if (!i && !name_size)
                break;
        }
        RegCloseKey(key);
        if (!found)
            return MCIERR_INVALID_DEVICE_NAME;
    }
    if (!value[0])
        return MCIERR_CANNOT_LOAD_DRIVER;
    WCHAR *space = value;
    while (*space && *space != ' ')
        space++;
    WCHAR defaults[256] = L"";
    if (*space) {
        *space = 0;
        lstrcpynW(defaults, space + 1, RTL_NUMBER_OF(defaults));
    }
    lstrcpynW(file, value, MAX_PATH);
    GetProfileStringW(file, install, defaults, params, 256);
    return MMSYSERR_NOERROR;
}

/* ------------------------------------------------------------------ yield */

static UINT CALLBACK break_key(MCIDEVICEID id, DWORD key)
{
    if (!(GetKeyState((int)key) & 1))
        return 0;
    MSG msg;
    while (PeekMessageW(&msg, nullptr, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE))
        ;
    return (UINT)-1;
}

WINMMAPI BOOL WINAPI mciSetYieldProc(MCIDEVICEID id, YIELDPROC proc, DWORD data)
{
    RtlEnterCriticalSection(&MciLock);
    MCI_DEVICE *d = MciDevice(id);
    if (d) {
        d->yield = proc;
        d->yield_data = data;
    }
    RtlLeaveCriticalSection(&MciLock);
    return d != nullptr;
}

WINMMAPI YIELDPROC WINAPI mciGetYieldProc(MCIDEVICEID id, LPDWORD data)
{
    RtlEnterCriticalSection(&MciLock);
    MCI_DEVICE *d = MciDevice(id);
    YIELDPROC proc = d ? d->yield : nullptr;
    if (d && data)
        *data = d->yield_data;
    RtlLeaveCriticalSection(&MciLock);
    return proc;
}

WINMMAPI UINT WINAPI mciDriverYield(MCIDEVICEID id)
{
    DWORD data;
    YIELDPROC proc = mciGetYieldProc(id, &data);
    return proc ? proc(id, data) : 0;
}

/* ------------------------------------------------------------------ driver services */

WINMMAPI DWORD_PTR WINAPI mciGetDriverData(MCIDEVICEID id)
{
    RtlEnterCriticalSection(&MciLock);
    MCI_DEVICE *d = MciDevice(id);
    DWORD_PTR data = d ? d->data : 0;
    RtlLeaveCriticalSection(&MciLock);
    return data;
}

WINMMAPI BOOL WINAPI mciSetDriverData(MCIDEVICEID id, DWORD_PTR data)
{
    RtlEnterCriticalSection(&MciLock);
    MCI_DEVICE *d = MciDevice(id);
    if (d)
        d->data = data;
    RtlLeaveCriticalSection(&MciLock);
    return d != nullptr;
}

WINMMAPI BOOL WINAPI mciDriverNotify(HANDLE callback, MCIDEVICEID id, UINT status)
{
    return PostMessageW((HWND)callback, MM_MCINOTIFY, status, id);
}

WINMMAPI HTASK WINAPI mciGetCreatorTask(MCIDEVICEID id)
{
    RtlEnterCriticalSection(&MciLock);
    MCI_DEVICE *d = MciDevice(id);
    HTASK task = d ? (HTASK)(DWORD_PTR)d->owner : nullptr;
    RtlLeaveCriticalSection(&MciLock);
    return task;
}

WINMMAPI MCIDEVICEID WINAPI mciGetDeviceIDW(LPCWSTR device)
{
    if (!MciInit())
        return 0;
    return MciDeviceByName(device, GetCurrentThreadId());
}

WINMMAPI MCIDEVICEID WINAPI mciGetDeviceIDA(LPCSTR device)
{
    WCHAR name[MAX_PATH];
    if (!device || !MultiByteToWideChar(CP_ACP, 0, device, -1, name, MAX_PATH))
        return 0;
    return mciGetDeviceIDW(name);
}

WINMMAPI MCIDEVICEID WINAPI mciGetDeviceIDFromElementIDW(DWORD element, LPCWSTR type)
{
    if (!type || !MciInit())
        return 0;
    MCIDEVICEID id = 0;
    RtlEnterCriticalSection(&MciLock);
    for (UINT i = 1; i < g_capacity && !id; i++) {
        MCI_DEVICE *d = g_devices[i];
        WCHAR name[32];
        if (d && (d->open_flags & MCI_OPEN_ELEMENT_ID) && d->element_id == element &&
            LoadStringW(WinmmModule, d->type, name, RTL_NUMBER_OF(name)) && !lstrcmpiW(name, type))
            id = i;
    }
    RtlLeaveCriticalSection(&MciLock);
    return id;
}

WINMMAPI MCIDEVICEID WINAPI mciGetDeviceIDFromElementIDA(DWORD element, LPCSTR type)
{
    WCHAR name[64];
    if (!type || !MultiByteToWideChar(CP_ACP, 0, type, -1, name, RTL_NUMBER_OF(name)))
        return 0;
    return mciGetDeviceIDFromElementIDW(element, name);
}

/* the module whose strings a resource return of the device names. */
HMODULE MciStringModule(MCIDEVICEID id, BOOL driver)
{
    RtlEnterCriticalSection(&MciLock);
    MCI_DEVICE *d = MciDevice(id);
    HMODULE module = WinmmModule;
    if (d) {
        UINT table = driver ? d->custom_table : d->type_table;
        if (table < MCI_MAX_TABLES && MciTables[table].used)
            module = MciTables[table].module;
        else if (d->module)
            module = d->module;
    }
    RtlLeaveCriticalSection(&MciLock);
    return module;
}

/* ------------------------------------------------------------------ open and close */

static MCIERROR close_device(MCI_DEVICE *d, DWORD flags, DWORD_PTR parms)
{
    if (d->closing && !d->closing_by_notify)
        return MMSYSERR_NOERROR;
    d->closing = TRUE;
    MCI_GENERIC_PARMS dummy = {};
    MCIERROR r = (MCIERROR)SendDriverMessage(d->driver, MCI_CLOSE_DRIVER, flags, parms ? parms : (DWORD_PTR)&dummy);
    /* notifications still queued for a device opened on another thread's behalf are dropped. */
    if (d->owner != d->requester && g_window && GetCurrentThreadId() == g_window_thread) {
        MSG msg;
        MSG kept[32];
        int n = 0;
        while (n < 32 && PeekMessageW(&msg, g_window, MM_MCINOTIFY, MM_MCINOTIFY, PM_REMOVE))
            if (msg.lParam != (LPARAM)d->id)
                kept[n++] = msg;
        for (int i = 0; i < n; i++)
            PostMessageW(kept[i].hwnd, kept[i].message, kept[i].wParam, kept[i].lParam);
    }
    /* the driver frees its own table as it closes, which must no longer count as in use. */
    RtlEnterCriticalSection(&MciLock);
    UINT table = d->type_table;
    d->custom_table = d->type_table = MCI_NO_COMMAND_TABLE;
    RtlLeaveCriticalSection(&MciLock);
    CloseDriver(d->driver, 0, 0);
    RtlEnterCriticalSection(&MciLock);
    free_device(d);
    if (table != MCI_NO_COMMAND_TABLE && table && !table_in_use(table))
        free_table(table);
    RtlLeaveCriticalSection(&MciLock);
    return LOWORD(r);
}

/* the name a device answers to, unless it is open for this thread already. */
static MCIERROR check_name(const WCHAR *name, BOOL alias, DWORD thread)
{
    if (name && MciDeviceByName(name, thread) && MciDeviceByName(name, thread) != MCI_ALL_DEVICE_ID)
        return alias ? MCIERR_DUPLICATE_ALIAS : MCIERR_DEVICE_OPEN;
    return MMSYSERR_NOERROR;
}

static MCIERROR load_device(DWORD flags, MCI_OPEN_PARMSW *p, const WCHAR *type, const WCHAR *element,
                            const WCHAR *name, DWORD requester, MCI_REPARSE *reparse)
{
    WCHAR install[80], file[MAX_PATH], params[256];
    MCIERROR r = find_driver(type, install, file, params);
    if (r)
        return r;
    if ((flags & MCI_OPEN_ALIAS) && check_name(p->lpstrAlias, TRUE, requester))
        return MCIERR_DUPLICATE_ALIAS;
    RtlEnterCriticalSection(&MciLock);
    MCI_DEVICE *d = new_device();
    if (d) {
        d->name = duplicate(name);
        lstrcpynW(d->install, install, RTL_NUMBER_OF(d->install));
        d->open_flags = flags;
        d->element_id = (flags & MCI_OPEN_ELEMENT_ID) ? (DWORD)(DWORD_PTR)p->lpstrElementName : 0;
        d->owner = GetCurrentThreadId();
        d->requester = requester;
        d->closing = TRUE; /* hidden until its driver accepted the open */
    }
    RtlLeaveCriticalSection(&MciLock);
    if (!d)
        return MCIERR_CANNOT_LOAD_DRIVER;
    MCI_OPEN_DRIVER_PARMS open = {d->id, params, MCI_NO_COMMAND_TABLE, 0};
    d->driver = OpenDriver(file, L"MCI32", (LPARAM)&open);
    if (!d->driver) {
        RtlEnterCriticalSection(&MciLock);
        free_device(d);
        RtlLeaveCriticalSection(&MciLock);
        return MCIERR_CANNOT_LOAD_DRIVER;
    }
    d->module = GetDriverModuleHandle(d->driver);
    d->type = open.wType;
    d->custom_table = open.wCustomCommandTable;
    /* a type table is shared by every device of the type. */
    RtlEnterCriticalSection(&MciLock);
    UINT table = MCI_NO_COMMAND_TABLE;
    for (UINT i = 1; i < MCI_MAX_TABLES && table == MCI_NO_COMMAND_TABLE; i++)
        if (MciTables[i].used && MciTables[i].type && MciTables[i].type == d->type)
            table = i;
    if (table == MCI_NO_COMMAND_TABLE && d->type >= MCI_DEVTYPE_FIRST && d->type <= MCI_DEVTYPE_LAST)
        table = load_table(WinmmModule, MAKEINTRESOURCEW(d->type), d->type);
    if (table == MCI_NO_COMMAND_TABLE)
        table = load_table(WinmmModule, type, d->type);
    d->type_table = table;
    RtlLeaveCriticalSection(&MciLock);
    DWORD open_flags = flags;
    MCI_OPEN_PARMSW *target = p;
    if (reparse) {
        UINT custom;
        const BYTE *command = MciFindCommand(d->id, L"open", &custom);
        if (command && custom != 0) {
            MCIERROR e = MciParseArgs(command, reparse->args, FALSE, reparse);
            if (e) {
                d->closing = FALSE;
                close_device(d, 0, 0);
                return e;
            }
            open_flags = reparse->flags;
            target = (MCI_OPEN_PARMSW *)reparse->block;
            target->dwCallback = p->dwCallback;
        } else if (reparse->deferred) {
            d->closing = FALSE;
            close_device(d, 0, 0);
            return reparse->deferred;
        }
    }
    /* the driver sees the element and type the core resolved, in the caller's own block. */
    LPCWSTR saved_type = target->lpstrDeviceType, saved_element = target->lpstrElementName;
    target->lpstrDeviceType = type;
    if (!(open_flags & MCI_OPEN_ELEMENT_ID)) {
        target->lpstrElementName = element;
        if (element)
            open_flags |= MCI_OPEN_ELEMENT;
    }
    target->wDeviceID = d->id;
    d->closing = FALSE;
    r = (MCIERROR)SendDriverMessage(d->driver, MCI_OPEN_DRIVER, open_flags, (DWORD_PTR)target);
    target->lpstrDeviceType = saved_type;
    target->lpstrElementName = saved_element;
    if (LOWORD(r)) {
        MCIDEVICEID id = d->id;
        close_device(d, 0, 0);
        return LOWORD(r) >= MCIERR_CUSTOM_DRIVER_BASE ? MAKELONG(LOWORD(r), id) : LOWORD(r);
    }
    d->yield = break_key;
    d->yield_data = VK_CANCEL;
    p->wDeviceID = d->id;
    return MMSYSERR_NOERROR;
}

static MCIERROR open_device(DWORD flags, MCI_OPEN_PARMSW *p, DWORD requester, MCI_REPARSE *reparse)
{
    if (!p)
        return MCIERR_NULL_PARAMETER_BLOCK;
    WCHAR type_name[96], mapped[80];
    const WCHAR *type = nullptr, *element = nullptr;
    if (flags & MCI_OPEN_TYPE_ID) {
        UINT id = LOWORD((DWORD_PTR)p->lpstrDeviceType);
        WCHAR base[80];
        if (!LoadStringW(WinmmModule, id, base, RTL_NUMBER_OF(base)) || id < MCI_DEVTYPE_FIRST || id > MCI_DEVTYPE_LAST)
            return MCIERR_EXTENSION_NOT_FOUND;
        UINT ordinal = HIWORD((DWORD_PTR)p->lpstrDeviceType);
        if (ordinal && lstrlenW(base) > 69)
            return MCIERR_DEVICE_ORD_LENGTH;
        if (ordinal)
            wsprintfW(type_name, L"%s%u", base, ordinal);
        else
            lstrcpyW(type_name, base);
        type = type_name;
    } else if (flags & MCI_OPEN_TYPE) {
        type = p->lpstrDeviceType;
    }
    BOOL has_element = (flags & (MCI_OPEN_ELEMENT | MCI_OPEN_ELEMENT_ID)) != 0;
    if (!has_element && !(flags & (MCI_OPEN_TYPE | MCI_OPEN_TYPE_ID)))
        return MCIERR_MISSING_PARAMETER;
    if (flags & MCI_OPEN_ELEMENT)
        element = p->lpstrElementName;
    if (has_element ? (!(flags & MCI_OPEN_ELEMENT_ID) && !element) : !type)
        return MCIERR_INVALID_DEVICE_NAME;
    const WCHAR *alias = (flags & MCI_OPEN_ALIAS) ? p->lpstrAlias : nullptr;
    if (flags & MCI_OPEN_ELEMENT_ID) {
        if (type && mciGetDeviceIDFromElementIDW((DWORD)(DWORD_PTR)p->lpstrElementName, type))
            return MCIERR_DEVICE_OPEN;
    } else {
        MCIERROR r = check_name(alias ? alias : element ? element : type, alias != nullptr, requester);
        if (r)
            return r;
    }
    WCHAR split[MAX_PATH];
    if (element && !type) {
        if (!type_of_file(element, mapped))
            return MCIERR_EXTENSION_NOT_FOUND;
        type = mapped;
    } else if (!has_element) {
        const WCHAR *bang = nullptr;
        for (const WCHAR *c = type + 1; type[0] && *c; c++)
            if (*c == '!') {
                bang = c;
                break;
            }
        if (type_of_file(type, mapped)) {
            element = type;
            type = mapped;
        } else if (bang) {
            int n = (int)(bang - type);
            if (n >= MAX_PATH)
                return MCIERR_DEVICE_LENGTH;
            lstrcpynW(split, type, n + 1);
            if (bang[1]) {
                element = bang + 1;
                if (!alias && MciDeviceByName(element, requester))
                    return MCIERR_DEVICE_OPEN;
            }
            type = split;
        }
    }
    const WCHAR *name = alias ? alias : (flags & MCI_OPEN_ELEMENT_ID) ? nullptr : element ? element : type;
    return load_device(flags, p, type, element, name, requester, reparse);
}

/* ------------------------------------------------------------------ sysinfo, break, sound */

static MCIERROR sysinfo(MCIDEVICEID id, DWORD flags, MCI_SYSINFO_PARMSW *p, DWORD requester)
{
    if (!p)
        return MCIERR_NULL_PARAMETER_BLOCK;
    if ((flags & MCI_SYSINFO_NAME) && !p->dwNumber)
        return MCIERR_OUTOFRANGE;
    if (!p->lpstrReturn || !p->dwRetSize)
        return MCIERR_PARAM_OVERFLOW;
    if ((flags & MCI_SYSINFO_QUANTITY) && (flags & MCI_SYSINFO_NAME))
        return MCIERR_FLAGS_NOT_COMPATIBLE;
    if (flags & MCI_SYSINFO_INSTALLNAME) {
        if (id == MCI_ALL_DEVICE_ID)
            return MCIERR_CANNOT_USE_ALL;
        RtlEnterCriticalSection(&MciLock);
        MCI_DEVICE *d = MciDevice(id);
        MCIERROR r = MCIERR_INVALID_DEVICE_NAME;
        if (d) {
            r = (DWORD)lstrlenW(d->install) >= p->dwRetSize ? MCIERR_PARAM_OVERFLOW : MMSYSERR_NOERROR;
            if (!r)
                lstrcpyW(p->lpstrReturn, d->install);
        }
        RtlLeaveCriticalSection(&MciLock);
        return r;
    }
    BOOL all = id == MCI_ALL_DEVICE_ID;
    if (!all && !p->wDeviceType)
        return MCIERR_DEVICE_TYPE_REQUIRED;
    if (!(flags & (MCI_SYSINFO_QUANTITY | MCI_SYSINFO_NAME)))
        return MCIERR_MISSING_PARAMETER;
    DWORD count = 0;
    if (flags & MCI_SYSINFO_OPEN) {
        MCIERROR r = MCIERR_OUTOFRANGE;
        if (flags & MCI_SYSINFO_NAME)
            p->lpstrReturn[0] = 0;
        RtlEnterCriticalSection(&MciLock);
        for (UINT i = 1; i < g_capacity; i++) {
            MCI_DEVICE *d = g_devices[i];
            if (!d || d->requester != requester || (!all && d->type != p->wDeviceType))
                continue;
            if (++count == p->dwNumber && (flags & MCI_SYSINFO_NAME)) {
                const WCHAR *name = d->name ? d->name : L"";
                r = (DWORD)lstrlenW(name) >= p->dwRetSize ? MCIERR_PARAM_OVERFLOW : MMSYSERR_NOERROR;
                if (!r)
                    lstrcpyW(p->lpstrReturn, name);
                break;
            }
        }
        RtlLeaveCriticalSection(&MciLock);
        if (flags & MCI_SYSINFO_NAME)
            return r;
    } else {
        HKEY key;
        WCHAR found[256] = L"";
        if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, NT_KEY L"MCI32", 0, KEY_QUERY_VALUE, &key)) {
            for (DWORD i = 0;; i++) {
                WCHAR name[256];
                DWORD size = RTL_NUMBER_OF(name);
                if (RegEnumValueW(key, i, name, &size, nullptr, nullptr, nullptr, nullptr))
                    break;
                if (!all && MciTypeOf(name) != p->wDeviceType)
                    continue;
                if (++count == p->dwNumber)
                    lstrcpyW(found, name);
            }
            RegCloseKey(key);
        }
        if (flags & MCI_SYSINFO_NAME) {
            if (!found[0])
                return MCIERR_OUTOFRANGE;
            if ((DWORD)lstrlenW(found) >= p->dwRetSize)
                return MCIERR_PARAM_OVERFLOW;
            lstrcpyW(p->lpstrReturn, found);
            return MMSYSERR_NOERROR;
        }
    }
    /* a quantity is a number for the string interface to print. */
    if (p->dwRetSize * sizeof(WCHAR) < sizeof(DWORD))
        return MCIERR_PARAM_OVERFLOW;
    memcpy(p->lpstrReturn, &count, sizeof(count));
    return MAKELONG(0, MCI_INTEGER_RETURNED >> 16);
}

static MCIERROR do_break(MCIDEVICEID id, DWORD flags, MCI_BREAK_PARMS *p)
{
    BOOL on = flags & MCI_BREAK_KEY, off = flags & MCI_BREAK_OFF;
    if (on && off)
        return MCIERR_FLAGS_NOT_COMPATIBLE;
    if (!on && !off)
        return MCIERR_MISSING_PARAMETER;
    if (on && !p)
        return MCIERR_NULL_PARAMETER_BLOCK;
    if (!mciSetYieldProc(id, on ? break_key : nullptr, on ? (DWORD)p->nVirtKey : 0))
        return MMSYSERR_INVALPARAM;
    return MMSYSERR_NOERROR;
}

static MCIERROR sound(DWORD flags, MCI_SOUND_PARMSW *p)
{
    const WCHAR *name = (flags & MCI_SOUND_NAME) && p ? p->lpstrSoundName : L".Default";
    return PlaySoundW(name, nullptr, SND_ALIAS | ((flags & MCI_WAIT) ? 0 : SND_ASYNC)) ? MMSYSERR_NOERROR
                                                                                        : MCIERR_HARDWARE;
}

/* ------------------------------------------------------------------ commands */

static MCIERROR to_driver(MCI_DEVICE *d, UINT message, DWORD flags, DWORD_PTR parms)
{
    if ((flags & MCI_WAIT) && d->yield == break_key)
        GetKeyState((int)d->yield_data);
    return (MCIERROR)SendDriverMessage(d->driver, message, flags, parms);
}

static MCIERROR core_notify(MCIERROR r, DWORD flags, DWORD_PTR parms, MCIDEVICEID id)
{
    if (!r && (flags & MCI_NOTIFY) && parms)
        mciDriverNotify((HANDLE)((MCI_GENERIC_PARMS *)parms)->dwCallback, id, MCI_NOTIFY_SUCCESSFUL);
    return r;
}

/* one device's command; the raw driver answer, its high word holding the return flags. */
static MCIERROR device_command(MCIDEVICEID id, UINT message, DWORD flags, DWORD_PTR parms)
{
    RtlEnterCriticalSection(&MciLock);
    MCI_DEVICE *d = MciDevice(id);
    MCIERROR r = MMSYSERR_NOERROR;
    if (!d)
        r = MCIERR_INVALID_DEVICE_ID;
    else if (d->closing && message != MCI_CLOSE_DRIVER)
        r = MCIERR_DEVICE_LOCKED;
    else if (d->owner != GetCurrentThreadId())
        r = MCIERR_ILLEGAL_FOR_AUTO_OPEN;
    RtlLeaveCriticalSection(&MciLock);
    if (r)
        return r;
    switch (message) {
    case MCI_CLOSE:
        return close_device(d, flags, parms);
    case MCI_BREAK:
        return core_notify(do_break(id, flags, (MCI_BREAK_PARMS *)parms), flags, parms, id);
    default:
        return to_driver(d, message, flags, parms);
    }
}

MCIERROR MciCommand(MCIDEVICEID id, UINT message, DWORD flags, DWORD_PTR parms, DWORD requester, MCI_REPARSE *reparse)
{
    if (!MciInit())
        return MCIERR_OUT_OF_MEMORY;
    switch (message) {
    case MCI_OPEN:
        if (id == MCI_ALL_DEVICE_ID)
            return MCIERR_CANNOT_USE_ALL;
        return open_device(flags, (MCI_OPEN_PARMSW *)parms, requester, reparse);
    case MCI_SYSINFO: {
        MCIERROR r = sysinfo(id, flags, (MCI_SYSINFO_PARMSW *)parms, requester);
        return HIWORD(r) ? r : core_notify(r, flags, parms, id);
    }
    case MCI_SOUND:
        return core_notify(sound(flags, (MCI_SOUND_PARMSW *)parms), flags, parms, id);
    }
    if (id != MCI_ALL_DEVICE_ID)
        return device_command(id, message, flags, parms);
    /* every device of the calling thread in turn; a second failure makes MCIERR_MULTIPLE. */
    MCIERROR result = 0;
    int failures = 0;
    for (UINT i = 1; i < g_capacity; i++) {
        RtlEnterCriticalSection(&MciLock);
        MCI_DEVICE *d = MciDevice(i);
        BOOL mine = d && d->requester == requester;
        BOOL relay = mine && d->owner != GetCurrentThreadId() && d->name;
        WCHAR name[MAX_PATH] = L"";
        if (relay)
            lstrcpynW(name, d->name, MAX_PATH);
        RtlLeaveCriticalSection(&MciLock);
        if (!mine)
            continue;
        MCIERROR r;
        if (relay && message == MCI_CLOSE) {
            WCHAR command[MAX_PATH + 8];
            wsprintfW(command, L"close %s", name);
            r = MciSendString(command, nullptr, 0, nullptr, requester, FALSE, FALSE);
        } else {
            r = LOWORD(device_command(i, message, flags, parms));
        }
        if (r == MCIERR_DEVICE_LOCKED || r == MCIERR_ILLEGAL_FOR_AUTO_OPEN)
            return r;
        if (r)
            failures++;
        result = r;
    }
    return failures > 1 ? MCIERR_MULTIPLE : result;
}

/* a resource return loses its string id, and the driver's flags their word. */
static MCIERROR finish(MCIERROR r, MCIDEVICEID id, DWORD_PTR parms)
{
    if (!LOWORD(r) && (r & MCI_RESOURCE_RETURNED) && parms) {
        DWORD value;
        memcpy(&value, (BYTE *)parms + sizeof(DWORD_PTR), 4);
        value &= 0xFFFF;
        memcpy((BYTE *)parms + sizeof(DWORD_PTR), &value, 4);
    }
    r = LOWORD(r);
    if (r >= MCIERR_CUSTOM_DRIVER_BASE)
        r = MAKELONG(r, id == MCI_ALL_DEVICE_ID ? 0xFFFF : id);
    return r;
}

WINMMAPI MCIERROR WINAPI mciSendCommandW(MCIDEVICEID id, UINT message, DWORD_PTR flags, DWORD_PTR parms)
{
    MCIERROR r = MciCommand(id, message, (DWORD)flags, parms, GetCurrentThreadId(), nullptr);
    if (message == MCI_OPEN && LOWORD(r) >= MCIERR_CUSTOM_DRIVER_BASE)
        return r;
    return finish(r, id, parms);
}

/* ------------------------------------------------------------------ the window thread */

typedef struct RELAY {
    const WCHAR *command;
    WCHAR *ret;
    UINT size;
    DWORD requester;
    BOOL force_notify;
    WCHAR directory[MAX_PATH];
} RELAY;

static LRESULT CALLBACK notify_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_MCI_RELAY) {
        RELAY *r = (RELAY *)lparam;
        WCHAR saved[MAX_PATH];
        if (!GetCurrentDirectoryW(MAX_PATH, saved))
            return MCIERR_GET_CD;
        if (!SetCurrentDirectoryW(r->directory))
            return MCIERR_SET_CD;
        MCIERROR e = MciSendString(r->command, r->ret, r->size, hwnd, r->requester, TRUE, r->force_notify);
        SetCurrentDirectoryW(saved);
        return e;
    }
    if (message == MM_MCINOTIFY) {
        /* an auto opened device closes at its first notification, whatever it says. */
        RtlEnterCriticalSection(&MciLock);
        MCI_DEVICE *d = MciDevice((MCIDEVICEID)lparam);
        if (d && !d->closing) {
            d->closing = TRUE;
            d->closing_by_notify = TRUE;
        } else {
            d = nullptr;
        }
        RtlLeaveCriticalSection(&MciLock);
        if (d)
            close_device(d, 0, 0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static DWORD WINAPI notify_thread(void *ready)
{
    WNDCLASSW wc = {0, notify_proc, 0, 0, WinmmModule, nullptr, nullptr, nullptr, nullptr, L"MCI notify"};
    RegisterClassW(&wc);
    g_window = CreateWindowExW(0, L"MCI notify", nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, WinmmModule, nullptr);
    SetEvent(ready);
    if (!g_window)
        return 0;
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

HWND MciNotifyWindow(void)
{
    if (g_window)
        return g_window;
    RtlEnterCriticalSection(&MciLock);
    if (!g_window) {
        HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        HANDLE thread = ready ? CreateThread(nullptr, 0, notify_thread, ready, 0, &g_window_thread) : nullptr;
        if (thread) {
            SetThreadPriority(thread, THREAD_PRIORITY_HIGHEST);
            WaitForSingleObject(ready, INFINITE);
            CloseHandle(thread);
        }
        if (ready)
            CloseHandle(ready);
    }
    RtlLeaveCriticalSection(&MciLock);
    return g_window;
}

/* a string command carried out on the window thread for the calling one, which waits for it. */
MCIERROR MciRelay(const WCHAR *command, WCHAR *ret, UINT size, DWORD requester, BOOL force_notify)
{
    HWND window = MciNotifyWindow();
    if (!window)
        return MCIERR_INTERNAL;
    if (GetCurrentThreadId() == g_window_thread)
        return MCI_RELAY_SENTINEL;
    RELAY r = {command, ret, size, requester, force_notify};
    if (!GetCurrentDirectoryW(MAX_PATH, r.directory))
        return MCIERR_GET_CD;
    return (MCIERROR)SendMessageW(window, WM_MCI_RELAY, 0, (LPARAM)&r);
}

BOOL MciOnWindowThread(void)
{
    return g_window_thread && GetCurrentThreadId() == g_window_thread;
}

/* ------------------------------------------------------------------ errors and mciExecute */

WINMMAPI BOOL WINAPI mciGetErrorStringW(MCIERROR error, LPWSTR text, UINT size)
{
    if (!text)
        return FALSE;
    int n = 0;
    if (HIWORD(error)) {
        RtlEnterCriticalSection(&MciLock);
        MCI_DEVICE *d = MciInit() ? MciDevice(HIWORD(error)) : nullptr;
        HMODULE module = d ? d->module : nullptr;
        RtlLeaveCriticalSection(&MciLock);
        n = module ? LoadStringW(module, LOWORD(error), text, size)
                   : LoadStringW(WinmmModule, MCIERR_DRIVER, text, size);
    } else if (error < MCIERR_BASE) {
        if (size && !MmErrorTextW(error, text, size))
            return TRUE;
    } else {
        n = LoadStringW(WinmmModule, error, text, size);
    }
    if (n <= 0) {
        if (size)
            text[0] = 0;
        return FALSE;
    }
    return TRUE;
}

WINMMAPI BOOL WINAPI mciGetErrorStringA(MCIERROR error, LPSTR text, UINT size)
{
    if (!text)
        return FALSE;
    WCHAR wide[MAXERRORLENGTH];
    BOOL ok = mciGetErrorStringW(error, wide, RTL_NUMBER_OF(wide));
    if (size)
        text[0] = 0;
    if (ok && size && !WideCharToMultiByte(CP_ACP, 0, wide, -1, text, size, nullptr, nullptr)) {
        text[size - 1] = 0;
        WideCharToMultiByte(CP_ACP, 0, wide, -1, text, size - 1, nullptr, nullptr);
    }
    return ok;
}

/* the device word of a command, unquoted, for a message box's caption. */
static void second_word(const WCHAR *command, WCHAR *out, int size)
{
    const WCHAR *p = command;
    while (*p == ' ')
        p++;
    while (*p && *p != ' ')
        p++;
    while (*p == ' ')
        p++;
    int n = 0;
    BOOL quoted = *p == '"';
    if (quoted)
        p++;
    while (*p && n < size - 1 && (quoted ? *p != '"' : *p != ' '))
        out[n++] = *p++;
    out[n] = 0;
}

WINMMAPI BOOL WINAPI mciExecute(LPCSTR command)
{
    WCHAR wide[512];
    if (!command || !MultiByteToWideChar(CP_ACP, 0, command, -1, wide, RTL_NUMBER_OF(wide)))
        return FALSE;
    MCIERROR r = mciSendStringW(wide, nullptr, 0, nullptr);
    if (!LOWORD(r))
        return TRUE;
    WCHAR text[MAXERRORLENGTH], caption[MAX_PATH];
    if (!mciGetErrorStringW(r, text, RTL_NUMBER_OF(text)))
        LoadStringW(WinmmModule, 2001, text, RTL_NUMBER_OF(text));
    second_word(wide, caption, MAX_PATH);
    MessageBoxW(nullptr, text, caption[0] ? caption : nullptr, MB_ICONHAND);
    return FALSE;
}
