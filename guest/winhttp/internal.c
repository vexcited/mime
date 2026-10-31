/*
 * the connection, protocol and proxy settings exports that Windows keeps for its own components. the proxy discovery
 * service does not exist here, so what Windows asks of it fails with the error a service that cannot start gives.
 */
#include "winhttp_private.h"
#include <mime/ntrtl.h>
#include <mime/ntrtlsec.h>

#ifndef ERROR_WINHTTP_AUTO_PROXY_SERVICE_ERROR
#define ERROR_WINHTTP_AUTO_PROXY_SERVICE_ERROR 12178L
#endif
#ifndef ERROR_WINHTTP_INCORRECT_HANDLE_STATE
#define ERROR_WINHTTP_INCORRECT_HANDLE_STATE 12019L
#endif

#define CRED_PERSIST_ENTERPRISE_VALUE 3

void WINAPI WinHttpSetSecureLegacyServersAppCompat(void)
{
    static volatile LONG enabled;
    InterlockedExchange(&enabled, 1);
}

/* the handle checks that come before every call to the service. */
static DWORD ReferenceOfType(HINTERNET handle, DWORD type, WH_OBJECT **object)
{
    *object = nullptr;
    if (!handle)
        return ERROR_INVALID_HANDLE;
    WH_OBJECT *found = WhReference(handle);
    if (!found)
        return ERROR_INVALID_HANDLE;
    if (found->Type != type) {
        WhRelease(found);
        return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
    }
    *object = found;
    return ERROR_SUCCESS;
}

static DWORD SessionService(HINTERNET handle)
{
    WH_OBJECT *session;
    DWORD error = ReferenceOfType(handle, WH_SESSION, &session);
    if (error)
        return error;
    WhRelease(session);
    return ERROR_WINHTTP_AUTO_PROXY_SERVICE_ERROR;
}

/* connection policy entries. */

typedef struct {
    GUID Id;
    DWORD SidLength;
    PSID Sid;
    BYTE Rest[24];
} WH_POLICY_ENTRY;

typedef struct {
    WH_POLICY_ENTRY *Entries;
    DWORD Count;
} WH_POLICY_LIST;

static BOOL SidBufferValid(DWORD length, PSID sid)
{
    return sid && length && RtlValidSid(sid) && RtlLengthSid(sid) == length;
}

DWORD WINAPI WinHttpConnectionDeletePolicyEntries(HINTERNET handle, DWORD tag)
{
    return SessionService(handle);
}

DWORD WINAPI WinHttpConnectionDeletePolicyEntriesByAppSid(HINTERNET handle, DWORD length, PSID sid)
{
    if (!SidBufferValid(length, sid))
        return ERROR_INVALID_PARAMETER;
    return SessionService(handle);
}

DWORD WINAPI WinHttpConnectionSetPolicyEntries(HINTERNET handle, DWORD tag, WH_POLICY_LIST *list)
{
    if (!list)
        return ERROR_INVALID_PARAMETER;
    if (!list->Count)
        return ERROR_SUCCESS;
    if (!list->Entries)
        return ERROR_INVALID_PARAMETER;
    for (DWORD i = 0; i < list->Count; i++) {
        if (list->Entries[i].SidLength && !SidBufferValid(list->Entries[i].SidLength, list->Entries[i].Sid))
            return ERROR_INVALID_PARAMETER;
    }
    return SessionService(handle);
}

DWORD WINAPI WinHttpConnectionUpdateIfIndexTable(HINTERNET handle, DWORD_PTR table)
{
    return SessionService(handle);
}

/* proxies of a connection, kept in the registry as the DNS client keeps them. */

#define CONNECTION_PROXY_KEY L"SYSTEM\\CurrentControlSet\\Services\\Dnscache\\Parameters\\DnsConnectionsProxies"

enum { PROXY_SWITCH_CONFIG, PROXY_SWITCH_SCRIPT, PROXY_SWITCH_WPAD };

typedef struct {
    DWORD Version;
    PWSTR FriendlyName;
    DWORD Flags;
    DWORD Switch;
    PWSTR Server;
    PWSTR Username;
    PWSTR Password;
    PWSTR Exception;
    PWSTR ExtraInfo;
    DWORD Port;
} WH_CONNECTION_PROXY_INFO;

typedef struct {
    DWORD Type;
    WH_CONNECTION_PROXY_INFO Info;
} WH_CONNECTION_PROXY_ELEMENT;

typedef struct {
    DWORD Count;
    WH_CONNECTION_PROXY_ELEMENT *Proxies;
} WH_CONNECTION_PROXY_LIST;

typedef struct {
    WCHAR Name[65];
} WH_CONNECTION_NAME;

typedef struct {
    DWORD Count;
    WH_CONNECTION_NAME *Names;
} WH_CONNECTION_NAME_LIST;

/* a connection name cannot hold a backslash as a registry key name. */
static BOOL Normalize(LPCWSTR name, PWSTR out, DWORD capacity)
{
    DWORD length = (DWORD)wcslen(name);
    if (length >= capacity)
        return FALSE;
    for (DWORD i = 0; i <= length; i++)
        out[i] = name[i] == L'\\' ? L'/' : name[i];
    return TRUE;
}

static void PutNumber(PWSTR out, DWORD value)
{
    WCHAR digits[12];
    DWORD count = 0;
    do {
        digits[count++] = (WCHAR)(L'0' + value % 10);
        value /= 10;
    } while (value);
    while (count)
        *out++ = digits[--count];
    *out = 0;
}

static DWORD OpenProxies(REGSAM access, BOOL create, HKEY *key)
{
    if (create)
        return RegCreateKeyExW(HKEY_LOCAL_MACHINE, CONNECTION_PROXY_KEY, 0, nullptr, 0, access, nullptr, key, nullptr);
    return RegOpenKeyExW(HKEY_LOCAL_MACHINE, CONNECTION_PROXY_KEY, 0, access, key);
}

static DWORD GetString(HKEY key, LPCWSTR name, PWSTR *text)
{
    DWORD type = 0, size = 0;
    *text = nullptr;
    LSTATUS status = RegQueryValueExW(key, name, nullptr, &type, nullptr, &size);
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND)
        return status;
    PWSTR value = WhAllocZero(size + 2 * sizeof(WCHAR));
    if (!value)
        return ERROR_NOT_ENOUGH_MEMORY;
    if (status == ERROR_SUCCESS && type == REG_SZ && size && (status = RegQueryValueExW(key, name, nullptr, &type, (BYTE *)value, &size))) {
        WhFree(value);
        return status;
    }
    *text = value;
    return ERROR_SUCCESS;
}

static DWORD GetDword(HKEY key, LPCWSTR name, DWORD *number)
{
    DWORD type = 0, size = sizeof(*number);
    *number = 0;
    LSTATUS status = RegQueryValueExW(key, name, nullptr, &type, (BYTE *)number, &size);
    if (status == ERROR_FILE_NOT_FOUND || (!status && type != REG_DWORD)) {
        *number = 0;
        return ERROR_SUCCESS;
    }
    return status;
}

void WINAPI WinHttpConnectionFreeProxyInfo(WH_CONNECTION_PROXY_INFO *info)
{
    if (!info)
        return;
    WhFree(info->FriendlyName);
    WhFree(info->Server);
    WhFree(info->Username);
    WhFree(info->Password);
    WhFree(info->Exception);
    WhFree(info->ExtraInfo);
    memset(info, 0, sizeof(*info));
}

static DWORD ReadProxyInfo(HKEY key, WH_CONNECTION_PROXY_INFO *info)
{
    DWORD error = GetDword(key, L"Flags", &info->Flags);
    if (!error)
        error = GetDword(key, L"Switch", &info->Switch);
    if (!error)
        error = GetString(key, L"FriendlyName", &info->FriendlyName);
    if (error) {
        /* nothing else is read. */
    } else if (info->Switch == PROXY_SWITCH_CONFIG) {
        error = GetString(key, L"Server", &info->Server);
        if (!error)
            error = GetString(key, L"Username", &info->Username);
        if (!error)
            error = GetString(key, L"Exception", &info->Exception);
        if (!error)
            error = GetString(key, L"ExtraInfo", &info->ExtraInfo);
        if (!error)
            error = GetDword(key, L"Port", &info->Port);
    } else if (info->Switch == PROXY_SWITCH_SCRIPT) {
        error = GetString(key, L"Script", &info->Server);
        if (!error)
            error = GetString(key, L"Username", &info->Username);
    } else if (info->Switch == PROXY_SWITCH_WPAD) {
        error = GetString(key, L"Username", &info->Username);
    } else {
        error = ERROR_INVALID_DATA;
    }
    if (error)
        WinHttpConnectionFreeProxyInfo(info);
    else
        info->Version = 1;
    return error;
}

DWORD WINAPI WinHttpConnectionGetProxyInfo(LPCWSTR name, DWORD type, WH_CONNECTION_PROXY_INFO *info)
{
    if (info)
        memset(info, 0, sizeof(*info));
    if (!name || !info)
        return ERROR_INVALID_PARAMETER;
    WCHAR path[300];
    HKEY root, key;
    if (!Normalize(name, path, 260))
        return ERROR_INVALID_PARAMETER;
    DWORD length = (DWORD)wcslen(path);
    path[length] = L'\\';
    PutNumber(path + length + 1, type);
    DWORD error = OpenProxies(KEY_READ, FALSE, &root);
    if (error)
        return error;
    error = RegOpenKeyExW(root, path, 0, KEY_READ, &key);
    RegCloseKey(root);
    if (error)
        return error;
    error = ReadProxyInfo(key, info);
    RegCloseKey(key);
    return error;
}

DWORD WINAPI WinHttpConnectionGetProxyList(LPCWSTR name, WH_CONNECTION_PROXY_LIST *list)
{
    if (list)
        memset(list, 0, sizeof(*list));
    if (!name)
        return ERROR_INVALID_PARAMETER;
    WCHAR path[300];
    HKEY root, connection;
    if (!Normalize(name, path, 260))
        return ERROR_INVALID_PARAMETER;
    DWORD error = OpenProxies(KEY_READ, FALSE, &root);
    if (error)
        return error;
    error = RegOpenKeyExW(root, path, 0, KEY_READ, &connection);
    RegCloseKey(root);
    if (error == ERROR_FILE_NOT_FOUND)
        return ERROR_SUCCESS;
    if (error)
        return error;
    DWORD count = 0;
    error = RegQueryInfoKeyW(connection, nullptr, nullptr, nullptr, &count, nullptr, nullptr, nullptr, nullptr, nullptr,
                             nullptr, nullptr);
    WH_CONNECTION_PROXY_ELEMENT *proxies = nullptr;
    DWORD filled = 0;
    if (!error && count && !(proxies = WhAllocZero(count * sizeof(*proxies))))
        error = ERROR_NOT_ENOUGH_MEMORY;
    for (DWORD i = 0; !error && i < count; i++) {
        WCHAR child[11];
        DWORD size = 10;
        HKEY key;
        LSTATUS status = RegEnumKeyExW(connection, i, child, &size, nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS)
            break;
        if ((error = status) || (error = RegOpenKeyExW(connection, child, 0, KEY_READ, &key)))
            break;
        error = ReadProxyInfo(key, &proxies[filled].Info);
        RegCloseKey(key);
        if (error)
            break;
        DWORD number = 0;
        for (const WCHAR *digit = child; *digit >= L'0' && *digit <= L'9'; digit++)
            number = number * 10 + (*digit - L'0');
        proxies[filled++].Type = number;
    }
    RegCloseKey(connection);
    if (error) {
        for (DWORD i = 0; i < filled; i++)
            WinHttpConnectionFreeProxyInfo(&proxies[i].Info);
        WhFree(proxies);
        return error;
    }
    if (list) {
        list->Count = filled;
        list->Proxies = proxies;
    } else {
        for (DWORD i = 0; i < filled; i++)
            WinHttpConnectionFreeProxyInfo(&proxies[i].Info);
        WhFree(proxies);
    }
    return ERROR_SUCCESS;
}

void WINAPI WinHttpConnectionFreeProxyList(WH_CONNECTION_PROXY_LIST *list)
{
    if (!list)
        return;
    for (DWORD i = 0; list->Proxies && i < list->Count; i++)
        WinHttpConnectionFreeProxyInfo(&list->Proxies[i].Info);
    WhFree(list->Proxies);
    memset(list, 0, sizeof(*list));
}

DWORD WINAPI WinHttpConnectionGetNameList(WH_CONNECTION_NAME_LIST *list)
{
    if (list)
        memset(list, 0, sizeof(*list));
    HKEY root;
    DWORD count = 0, error = OpenProxies(KEY_READ, FALSE, &root);
    if (error)
        return error;
    error = RegQueryInfoKeyW(root, nullptr, nullptr, nullptr, &count, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                             nullptr);
    WH_CONNECTION_NAME *names = nullptr;
    if (!error && count && !(names = WhAllocZero(count * sizeof(*names))))
        error = ERROR_NOT_ENOUGH_MEMORY;
    DWORD filled = 0;
    while (!error && filled < count) {
        DWORD size = 65;
        LSTATUS status = RegEnumKeyExW(root, filled, names[filled].Name, &size, nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS)
            break;
        if ((error = status))
            break;
        filled++;
    }
    RegCloseKey(root);
    if (error) {
        WhFree(names);
        return error;
    }
    if (list) {
        list->Names = names;
        list->Count = filled;
    } else {
        WhFree(names);
    }
    return ERROR_SUCCESS;
}

void WINAPI WinHttpConnectionFreeNameList(WH_CONNECTION_NAME_LIST *list)
{
    if (!list)
        return;
    WhFree(list->Names);
    memset(list, 0, sizeof(*list));
}

/* a change of the proxy settings reaches every process through two WNF state names. */

static const WNF_STATE_NAME ProxyConfigChange = {{0xA3BC0875, 0x1192063A}};
static const WNF_STATE_NAME ProxyChange2 = {{0xA3BC1875, 0x1192063A}};

static void NotifyProxyConfigChange(void)
{
    RtlPublishWnfStateData(ProxyConfigChange, nullptr, nullptr, 0, nullptr);
}

typedef void(CALLBACK *WH_PROXY_CHANGE_CALLBACK)(ULONGLONG flags, PVOID context);

typedef struct WH_CHANGE_ENTRY {
    struct WH_CHANGE_ENTRY *Next;
    ULONGLONG Flags;
    WH_PROXY_CHANGE_CALLBACK Callback;
    PVOID Context;
    HMODULE Module;
    LONG Running;
    BOOL Removed;
} WH_CHANGE_ENTRY;

static CRITICAL_SECTION ChangeLock;
static INIT_ONCE ChangeOnce = INIT_ONCE_STATIC_INIT;
static WH_CHANGE_ENTRY *ChangeEntries;
static PVOID ChangeSubscriptions[2];
static BOOL ChangeSubscribed;

static BOOL CALLBACK InitChange(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    InitializeCriticalSection(&ChangeLock);
    return TRUE;
}

static DWORD WINAPI RunChangeCallbacks(LPVOID parameter)
{
    EnterCriticalSection(&ChangeLock);
    WH_CHANGE_ENTRY *entry = ChangeEntries;
    while (entry) {
        if (!entry->Removed) {
            entry->Running++;
            LeaveCriticalSection(&ChangeLock);
            entry->Callback(entry->Flags, entry->Context);
            EnterCriticalSection(&ChangeLock);
            entry->Running--;
        }
        entry = entry->Next;
    }
    LeaveCriticalSection(&ChangeLock);
    return 0;
}

static NTSTATUS NTAPI ChangeCallback(WNF_STATE_NAME name, WNF_CHANGE_STAMP stamp, PWNF_TYPE_ID type, PVOID context,
                                     const VOID *buffer, ULONG size)
{
    QueueUserWorkItem(RunChangeCallbacks, nullptr, 0);
    return STATUS_SUCCESS;
}

DWORD WINAPI WinHttpRegisterProxyChangeNotification(ULONGLONG flags, WH_PROXY_CHANGE_CALLBACK callback, PVOID context,
                                                    PVOID *registration)
{
    if (registration)
        *registration = nullptr;
    if (flags != 1 || !registration || !callback)
        return ERROR_INVALID_PARAMETER;
    InitOnceExecuteOnce(&ChangeOnce, InitChange, nullptr, nullptr);
    WH_CHANGE_ENTRY *entry = WhAllocZero(sizeof(*entry));
    if (!entry)
        return ERROR_NOT_ENOUGH_MEMORY;
    /* the library of the callback stays loaded while it is registered. */
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)callback, &entry->Module)) {
        DWORD error = GetLastError();
        WhFree(entry);
        return error;
    }
    entry->Flags = flags;
    entry->Callback = callback;
    entry->Context = context;
    EnterCriticalSection(&ChangeLock);
    if (!ChangeSubscribed) {
        NTSTATUS status = RtlSubscribeWnfStateChangeNotification(&ChangeSubscriptions[0], ProxyChange2, 0xFFFFFFFF,
                                                                 ChangeCallback, nullptr, nullptr, 0, 1);
        if (status >= 0)
            status = RtlSubscribeWnfStateChangeNotification(&ChangeSubscriptions[1], ProxyConfigChange, 0xFFFFFFFF,
                                                            ChangeCallback, nullptr, nullptr, 0, 1);
        if (status < 0) {
            LeaveCriticalSection(&ChangeLock);
            FreeLibrary(entry->Module);
            WhFree(entry);
            return RtlNtStatusToDosError(status);
        }
        ChangeSubscribed = TRUE;
    }
    entry->Next = ChangeEntries;
    ChangeEntries = entry;
    LeaveCriticalSection(&ChangeLock);
    *registration = entry;
    return ERROR_SUCCESS;
}

DWORD WINAPI WinHttpUnregisterProxyChangeNotification(PVOID registration)
{
    WH_CHANGE_ENTRY *entry = registration;
    if (!entry)
        return ERROR_INVALID_PARAMETER;
    InitOnceExecuteOnce(&ChangeOnce, InitChange, nullptr, nullptr);
    EnterCriticalSection(&ChangeLock);
    WH_CHANGE_ENTRY **link = &ChangeEntries;
    while (*link && *link != entry)
        link = &(*link)->Next;
    if (!*link) {
        LeaveCriticalSection(&ChangeLock);
        return ERROR_INVALID_PARAMETER;
    }
    entry->Removed = TRUE;
    /* the entry keeps its link so a running pass can go on to the next one. */
    *link = entry->Next;
    while (entry->Running > 0) {
        LeaveCriticalSection(&ChangeLock);
        Sleep(1);
        EnterCriticalSection(&ChangeLock);
    }
    LeaveCriticalSection(&ChangeLock);
    FreeLibrary(entry->Module);
    WhFree(entry);
    return ERROR_SUCCESS;
}

DWORD WINAPI WinHttpConnectionSetProxyInfo(LPCWSTR name, DWORD type, const WH_CONNECTION_PROXY_INFO *info)
{
    if (!name || !info)
        return ERROR_INVALID_PARAMETER;
    if (info->FriendlyName && wcslen(info->FriendlyName) > 0x40)
        return ERROR_INVALID_PARAMETER;
    size_t user = info->Username ? wcslen(info->Username) : 0;
    if (info->Switch == PROXY_SWITCH_CONFIG) {
        if ((info->Server && wcslen(info->Server) > 0x100) || user > 0x80 ||
            (info->Password && wcslen(info->Password) > 0x80) || (info->Exception && wcslen(info->Exception) > 0x400) ||
            (info->ExtraInfo && wcslen(info->ExtraInfo) > 0x400))
            return ERROR_INVALID_PARAMETER;
    } else if (info->Switch == PROXY_SWITCH_SCRIPT) {
        if (!info->Server || !*info->Server || wcslen(info->Server) > 0x824 || user > 0x80)
            return ERROR_INVALID_PARAMETER;
    } else if (info->Switch != PROXY_SWITCH_WPAD || user > 0x80) {
        return ERROR_INVALID_PARAMETER;
    }
    if (info->Version != 1)
        return ERROR_INVALID_PARAMETER;
    WCHAR path[300], number[12], friendly[400];
    if (!Normalize(name, path, 260))
        return ERROR_INVALID_PARAMETER;
    HKEY root, connection, key;
    DWORD error = OpenProxies(KEY_ALL_ACCESS, TRUE, &root);
    if (error)
        return error;
    error = RegCreateKeyExW(root, path, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &connection, nullptr);
    RegCloseKey(root);
    if (error)
        return error;
    PutNumber(number, type);
    error = RegDeleteKeyExW(connection, number, 0, 0);
    if (error == ERROR_FILE_NOT_FOUND)
        error = ERROR_SUCCESS;
    if (!error)
        error = RegCreateKeyExW(connection, number, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, nullptr);
    RegCloseKey(connection);
    if (error)
        return error;
    LPCWSTR display = info->FriendlyName;
    if (!display || !*display) {
        wsprintfW(friendly, L"PXY(%s:%u)", name, type);
        display = friendly;
    }
#define SET_DWORD(value_name, number_value)                                                                            \
    do {                                                                                                               \
        DWORD data = (number_value);                                                                                   \
        if (!error)                                                                                                    \
            error = RegSetValueExW(key, value_name, 0, REG_DWORD, (const BYTE *)&data, sizeof(data));                  \
    } while (0)
#define SET_STRING(value_name, text)                                                                                   \
    do {                                                                                                               \
        if (!error)                                                                                                    \
            error = RegSetValueExW(key, value_name, 0, REG_SZ, (const BYTE *)(text),                                   \
                                   (DWORD)((wcslen(text) + 1) * sizeof(WCHAR)));                                       \
    } while (0)
    SET_DWORD(L"Flags", info->Flags);
    SET_DWORD(L"Switch", info->Switch);
    SET_STRING(L"FriendlyName", display);
    if (info->Switch == PROXY_SWITCH_CONFIG) {
        if (info->Server)
            SET_STRING(L"Server", info->Server);
        if (info->Username && *info->Username)
            SET_STRING(L"Username", info->Username);
        if (info->Exception && *info->Exception)
            SET_STRING(L"Exception", info->Exception);
        if (info->ExtraInfo && *info->ExtraInfo)
            SET_STRING(L"ExtraInfo", info->ExtraInfo);
        SET_DWORD(L"Port", info->Port);
    } else {
        if (info->Switch == PROXY_SWITCH_SCRIPT)
            SET_STRING(L"Script", info->Server);
        if (info->Username && *info->Username)
            SET_STRING(L"Username", info->Username);
    }
    RegCloseKey(key);
    if (error)
        return error;
    NotifyProxyConfigChange();
    return ERROR_SUCCESS;
}

DWORD WINAPI WinHttpConnectionDeleteProxyInfo(LPCWSTR name, DWORD type)
{
    if (!name)
        return ERROR_INVALID_PARAMETER;
    WCHAR path[300], number[12];
    if (!Normalize(name, path, 260))
        return ERROR_INVALID_PARAMETER;
    HKEY root, connection;
    DWORD error = OpenProxies(KEY_ALL_ACCESS, FALSE, &root);
    if (error)
        return error;
    error = RegOpenKeyExW(root, path, 0, KEY_ALL_ACCESS, &connection);
    if (!error) {
        PutNumber(number, type);
        error = RegDeleteKeyExW(connection, number, 0, 0);
        DWORD remaining = 1;
        if (!error && !RegQueryInfoKeyW(connection, nullptr, nullptr, nullptr, &remaining, nullptr, nullptr, nullptr,
                                        nullptr, nullptr, nullptr, nullptr) &&
            !remaining) {
            RegCloseKey(connection);
            connection = nullptr;
            RegDeleteKeyExW(root, path, 0, 0);
        }
        if (connection)
            RegCloseKey(connection);
    }
    RegCloseKey(root);
    if (!error)
        NotifyProxyConfigChange();
    return error;
}

/* proxy settings. */

typedef struct {
    DWORD StructSize;
    DWORD Flags;
    DWORD CurrentSettingsVersion;
    PWSTR ConnectionName;
    PWSTR Proxy;
    PWSTR ProxyBypass;
    PWSTR AutoconfigUrl;
    PWSTR AutoconfigSecondaryUrl;
    DWORD AutoDiscoveryFlags;
    PWSTR LastKnownGoodAutoConfigUrl;
    DWORD AutoconfigReloadDelayMins;
    FILETIME LastKnownDetectTime;
    DWORD DetectedInterfaceIpCount;
    PDWORD DetectedInterfaceIp;
    DWORD NetworkKeyCount;
    PVOID NetworkKeys;
} WH_PROXY_SETTINGS;

#define PROXY_SETTINGS_SIZE ((DWORD)sizeof(WH_PROXY_SETTINGS))

typedef void(WINAPI *WH_COTASKMEMFREE)(PVOID);

static void TaskFree(PVOID *pointer)
{
    static WH_COTASKMEMFREE free_memory;
    if (!*pointer)
        return;
    if (!free_memory) {
        HMODULE ole32 = LoadLibraryW(L"ole32.dll");
        free_memory = ole32 ? (WH_COTASKMEMFREE)GetProcAddress(ole32, "CoTaskMemFree") : nullptr;
    }
    PVOID memory = *pointer;
    *pointer = nullptr;
    if (free_memory)
        free_memory(memory);
}

void WINAPI WinHttpFreeProxySettings(WH_PROXY_SETTINGS *settings)
{
    if (!settings)
        return;
    TaskFree((PVOID *)&settings->ConnectionName);
    TaskFree((PVOID *)&settings->Proxy);
    TaskFree((PVOID *)&settings->ProxyBypass);
    TaskFree((PVOID *)&settings->LastKnownGoodAutoConfigUrl);
    TaskFree((PVOID *)&settings->AutoconfigUrl);
    TaskFree((PVOID *)&settings->AutoconfigSecondaryUrl);
    TaskFree((PVOID *)&settings->DetectedInterfaceIp);
    TaskFree(&settings->NetworkKeys);
}

typedef struct {
    DWORD Flags;
    PVOID Reserved;
    PWSTR Proxy;
    PWSTR ProxyBypass;
    PWSTR AutoconfigUrl;
    DWORD ProxyCount;
    PWSTR *Proxies;
    PVOID More;
} WH_PROXY_SETTINGS_EX;

DWORD WINAPI WinHttpFreeProxySettingsEx(DWORD type, WH_PROXY_SETTINGS_EX *settings)
{
    if (type < 1 || type > 3)
        return ERROR_INVALID_PARAMETER;
    if (!settings)
        return ERROR_SUCCESS;
    for (DWORD i = 0; settings->Proxies && i < settings->ProxyCount; i++)
        TaskFree((PVOID *)&settings->Proxies[i]);
    TaskFree((PVOID *)&settings->Reserved);
    TaskFree((PVOID *)&settings->Proxy);
    TaskFree((PVOID *)&settings->ProxyBypass);
    TaskFree((PVOID *)&settings->Proxies);
    TaskFree(&settings->More);
    return ERROR_SUCCESS;
}

DWORD WINAPI WinHttpGetProxySettingsVersion(HINTERNET handle, DWORD *version)
{
    if (!version)
        return ERROR_INVALID_PARAMETER;
    *version = 0;
    WH_OBJECT *session;
    DWORD error = ReferenceOfType(handle, WH_SESSION, &session);
    if (!error)
        WhRelease(session);
    return error;
}

DWORD WINAPI WinHttpReadProxySettings(HINTERNET handle, LPCWSTR connection, BOOL fall_back, BOOL auto_discover,
                                      DWORD *version, BOOL *defaults, WH_PROXY_SETTINGS *settings)
{
    if (defaults)
        *defaults = FALSE;
    if (!version || !settings || !defaults || settings->StructSize != PROXY_SETTINGS_SIZE)
        return ERROR_INVALID_PARAMETER;
    memset(settings, 0, PROXY_SETTINGS_SIZE);
    settings->StructSize = PROXY_SETTINGS_SIZE;
    return SessionService(handle);
}

DWORD WINAPI WinHttpReadProxySettingsHvsi(HINTERNET handle, LPCWSTR connection, BOOL fall_back, BOOL auto_discover,
                                          int force_default, DWORD *version, BOOL *defaults,
                                          WH_PROXY_SETTINGS *settings)
{
    if (!version || !settings || !defaults)
        return ERROR_INVALID_PARAMETER;
    *version = 0;
    *defaults = FALSE;
    memset(settings, 0, PROXY_SETTINGS_SIZE);
    settings->StructSize = PROXY_SETTINGS_SIZE;
    return WinHttpReadProxySettings(handle, force_default ? L"DefaultConnectionSettings" : connection, fall_back,
                                    auto_discover, version, defaults, settings);
}

DWORD WINAPI WinHttpWriteProxySettings(HINTERNET handle, BOOL force_update, WH_PROXY_SETTINGS *settings)
{
    if (!settings || settings->StructSize != PROXY_SETTINGS_SIZE)
        return ERROR_INVALID_PARAMETER;
    return SessionService(handle);
}

DWORD WINAPI WinHttpSetProxySettingsPerUser(DWORD flags)
{
    HINTERNET session = WinHttpOpen(nullptr, WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);
    if (!session)
        return GetLastError();
    WinHttpCloseHandle(session);
    return ERROR_WINHTTP_AUTO_PROXY_SERVICE_ERROR;
}

DWORD WINAPI WinHttpGetProxySettingsEx(HINTERNET resolver, DWORD type, PVOID options, DWORD_PTR context)
{
    if (!resolver)
        return ERROR_INVALID_HANDLE;
    if (type < 1 || type > 3)
        return ERROR_INVALID_PARAMETER;
    WH_OBJECT *object;
    DWORD error = ReferenceOfType(resolver, WH_RESOLVER, &object);
    if (error)
        return error;
    WhRelease(object);
    return ERROR_WINHTTP_AUTO_PROXY_SERVICE_ERROR;
}

DWORD WINAPI WinHttpGetProxySettingsResultEx(HINTERNET resolver, WH_PROXY_SETTINGS_EX *settings)
{
    if (!settings)
        return ERROR_INVALID_PARAMETER;
    if (!resolver)
        return ERROR_INVALID_HANDLE;
    WH_OBJECT *object;
    DWORD error = ReferenceOfType(resolver, WH_RESOLVER, &object);
    if (error)
        return error;
    WhRelease(object);
    return ERROR_WINHTTP_INCORRECT_HANDLE_STATE;
}

DWORD WINAPI WinHttpGetProxyForUrlHvsi(HINTERNET handle, LPCWSTR url, PVOID options, PVOID session_options, DWORD size,
                                       BYTE *buffer, int *scheme, PVOID result, PVOID *script)
{
    if (scheme)
        *scheme = 0;
    if (script)
        *script = nullptr;
    if (!url || !options || !session_options || (size == 0) != (buffer == nullptr) || !scheme || !result || !script)
        return ERROR_INVALID_PARAMETER;
    return SessionService(handle);
}

typedef struct {
    DWORD Flags;
    DWORD Type;
    LPWSTR TargetName;
    LPWSTR Comment;
    FILETIME LastWritten;
    DWORD BlobSize;
    LPBYTE Blob;
    DWORD Persist;
    DWORD AttributeCount;
    PVOID Attributes;
    LPWSTR TargetAlias;
    LPWSTR UserName;
} WH_CREDENTIAL;

DWORD WINAPI WinHttpSaveProxyCredentials(int scheme, LPWSTR target, LPWSTR user, LPBYTE password)
{
    if (!scheme || !target || !user || !password)
        return ERROR_INVALID_PARAMETER;
    typedef BOOL(WINAPI * WRITE)(WH_CREDENTIAL *, DWORD);
    HMODULE advapi32 = LoadLibraryW(L"advapi32.dll");
    WRITE write = advapi32 ? (WRITE)GetProcAddress(advapi32, "CredWriteW") : nullptr;
    if (!write)
        return ERROR_PROC_NOT_FOUND;
    WH_CREDENTIAL credential = {0};
    credential.TargetName = target;
    credential.BlobSize = (DWORD)(wcslen((LPCWSTR)password) * sizeof(WCHAR));
    credential.UserName = user;
    credential.Blob = password;
    credential.Persist = CRED_PERSIST_ENTERPRISE_VALUE;
    credential.Type = scheme == 1 || scheme == 8 ? 1 : 2;
    return write(&credential, 0) ? ERROR_SUCCESS : GetLastError();
}

/* the objects of the proxy list interfaces are internal to the Windows components that exchange them. */

DWORD WINAPI WinHttpCreateProxyList(ULONGLONG a, ULONGLONG b, PVOID *list)
{
    return list ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER;
}

DWORD WINAPI WinHttpCreateProxyManager(DWORD flags, DWORD options, PVOID list, PVOID *resolver)
{
    return resolver ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER;
}

DWORD WINAPI WinHttpCreateProxyResult(DWORD a, DWORD b, DWORD c, LPCWSTR proxy, int direct, PVOID *result)
{
    return result ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER;
}

DWORD WINAPI WinHttpCreateUiCompatibleProxyString(LPCWSTR proxy, LPWSTR *text)
{
    return proxy && text ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER;
}

DWORD WINAPI WinHttpRefreshProxySettings(PVOID resolver, int flags, PVOID callback, PVOID context)
{
    return resolver ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER;
}

DWORD WINAPI WinHttpResolverGetProxyForUrl(DWORD flags, PVOID resolver, PVOID url, PVOID context, PVOID callback,
                                           PVOID callback_context, PVOID *result)
{
    return result && resolver && url ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER;
}

DWORD WINAPI WinHttpProbeConnectivity(HINTERNET handle, LPCWSTR user, LPCWSTR password, LPWSTR buffer, int *a, int *b)
{
    if (b)
        *b = 0;
    if (!handle)
        return ERROR_INVALID_HANDLE;
    if (!buffer || !b)
        return ERROR_INVALID_PARAMETER;
    *buffer = 0;
    return ERROR_NOT_SUPPORTED;
}

/* connection groups, which a connection joins only by a guid that no option here can give it. */

typedef struct {
    ULONG Hosts;
    PVOID HostGroups;
} WH_QUERY_CONNECTION_GROUP_RESULT;

DWORD WINAPI WinHttpQueryConnectionGroup(HINTERNET handle, GUID *id, ULONGLONG flags,
                                         WH_QUERY_CONNECTION_GROUP_RESULT **result)
{
    WH_OBJECT *object = WhReference(handle);
    if (!object)
        return ERROR_INVALID_HANDLE;
    DWORD error = ERROR_SUCCESS;
    if (object->Type != WH_REQUEST && object->Type != WH_CONNECT) {
        error = ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
    } else if ((flags & ~1ULL) || !result) {
        error = ERROR_INVALID_PARAMETER;
    } else {
        *result = nullptr;
        if (object->Type == WH_REQUEST && (flags & 1))
            error = ERROR_INVALID_PARAMETER;
        else if (object->Type == WH_REQUEST && !object->Conn)
            error = ERROR_WINHTTP_INCORRECT_HANDLE_STATE;
        else if (!(*result = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(**result))))
            error = ERROR_NOT_ENOUGH_MEMORY;
    }
    WhRelease(object);
    return error;
}

void WINAPI WinHttpFreeQueryConnectionGroupResult(PVOID result)
{
    HeapFree(GetProcessHeap(), 0, result);
}

/* tunnel only requests do not exist, so a request is never one. */

DWORD WINAPI WinHttpGetTunnelSocket(HINTERNET handle, ULONGLONG *socket, BYTE **data, DWORD *length)
{
    if (!handle)
        return ERROR_INVALID_HANDLE;
    if (!socket || !data || !length)
        return ERROR_INVALID_PARAMETER;
    WH_OBJECT *request;
    DWORD error = ReferenceOfType(handle, WH_REQUEST, &request);
    if (error)
        return error;
    WhRelease(request);
    return ERROR_WINHTTP_INCORRECT_HANDLE_STATE;
}

HINTERNET WINAPI WinHttpConnectionOnlyConvert(HINTERNET handle, DWORD_PTR context)
{
    WH_OBJECT *request;
    DWORD error = ReferenceOfType(handle, WH_REQUEST, &request);
    if (!error) {
        WhRelease(request);
        error = ERROR_WINHTTP_INCORRECT_HANDLE_STATE;
    }
    SetLastError(error);
    return nullptr;
}

/* no handle here is connection only, so a handle that exists is of the wrong type. */
static DWORD NoConnectionOnlyHandle(HINTERNET handle)
{
    WH_OBJECT *object = WhReference(handle);
    if (!object)
        return ERROR_INVALID_HANDLE;
    WhRelease(object);
    return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
}

DWORD WINAPI WinHttpConnectionOnlySend(HINTERNET handle, BYTE *data, DWORD length, ULONGLONG context)
{
    if (!data || !length || context)
        return ERROR_INVALID_PARAMETER;
    return NoConnectionOnlyHandle(handle);
}

DWORD WINAPI WinHttpConnectionOnlyReceive(HINTERNET handle, BYTE *data, DWORD length, DWORD *read, ULONGLONG context)
{
    if (!read)
        return ERROR_INVALID_PARAMETER;
    *read = 0;
    if (!data || !length || context)
        return ERROR_INVALID_PARAMETER;
    return NoConnectionOnlyHandle(handle);
}

/* an upgraded request becomes a handle that carries the raw bytes of the connection. */

HINTERNET WINAPI WinHttpProtocolCompleteUpgrade(HINTERNET handle, DWORD_PTR context)
{
    WH_OBJECT *request;
    DWORD error = ReferenceOfType(handle, WH_REQUEST, &request);
    if (error) {
        SetLastError(error);
        return nullptr;
    }
    if (!request->UpgradeRequested || request->State != 2 || request->StatusCode != 101 || !request->Conn) {
        WhRelease(request);
        SetLastError(ERROR_WINHTTP_INCORRECT_HANDLE_STATE);
        return nullptr;
    }
    WH_OBJECT *protocol = WhCreate(WH_PROTOCOL, request);
    if (!protocol) {
        WhRelease(request);
        return nullptr;
    }
    EnterCriticalSection(&request->Lock);
    protocol->Conn = request->Conn;
    request->Conn = nullptr;
    LeaveCriticalSection(&request->Lock);
    protocol->Context = context;
    HINTERNET result = protocol->Handle;
    WhNotify(protocol, WINHTTP_CALLBACK_STATUS_HANDLE_CREATED, &result, sizeof(result));
    WhRelease(protocol);
    WhRelease(request);
    SetLastError(ERROR_SUCCESS);
    return result;
}

typedef struct {
    WH_OBJECT *Object;
    BOOL Send;
    BYTE *Buffer;
    DWORD Length;
} WH_PROTOCOL_JOB;

static DWORD RunProtocol(WH_OBJECT *protocol, BOOL send, BYTE *buffer, DWORD length, DWORD *done)
{
    *done = 0;
    if (!protocol->Conn)
        return ERROR_WINHTTP_INCORRECT_HANDLE_STATE;
    if (send) {
        DWORD error = WhConnSend(protocol->Conn, buffer, length, protocol->Timeouts.Send);
        if (!error)
            *done = length;
        return error;
    }
    return WhReadSome(protocol, buffer, length, done);
}

static DWORD WINAPI ProtocolWorker(LPVOID parameter)
{
    WH_PROTOCOL_JOB *job = parameter;
    WH_OBJECT *protocol = job->Object;
    DWORD done;
    DWORD error = RunProtocol(protocol, job->Send, job->Buffer, job->Length, &done);
    EnterCriticalSection(&protocol->Lock);
    protocol->Busy = FALSE;
    LeaveCriticalSection(&protocol->Lock);
    if (error) {
        WINHTTP_ASYNC_RESULT result = {job->Send ? API_WRITE_DATA : API_READ_DATA, error};
        WhNotify(protocol, WINHTTP_CALLBACK_STATUS_REQUEST_ERROR, &result, sizeof(result));
    } else if (job->Send) {
        WhNotify(protocol, WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE, &done, sizeof(done));
    } else {
        WhNotify(protocol, WINHTTP_CALLBACK_STATUS_READ_COMPLETE, job->Buffer, done);
    }
    WhFree(job);
    WhRelease(protocol);
    return 0;
}

static DWORD ProtocolTransfer(HINTERNET handle, BOOL send, BYTE *buffer, DWORD length, DWORD *done)
{
    WH_OBJECT *protocol;
    DWORD error = ReferenceOfType(handle, WH_PROTOCOL, &protocol);
    if (error)
        return error;
    if (!WhIsAsync(protocol)) {
        DWORD ignored;
        error = RunProtocol(protocol, send, buffer, length, done ? done : &ignored);
        WhRelease(protocol);
        return error;
    }
    WH_PROTOCOL_JOB *job = WhAlloc(sizeof(*job));
    if (!job) {
        WhRelease(protocol);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    *job = (WH_PROTOCOL_JOB){protocol, send, buffer, length};
    EnterCriticalSection(&protocol->Lock);
    BOOL busy = protocol->Busy;
    protocol->Busy = TRUE;
    LeaveCriticalSection(&protocol->Lock);
    if (busy) {
        WhFree(job);
        WhRelease(protocol);
        return ERROR_INVALID_OPERATION;
    }
    if (!QueueUserWorkItem(ProtocolWorker, job, 0)) {
        protocol->Busy = FALSE;
        WhFree(job);
        WhRelease(protocol);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    return ERROR_IO_PENDING;
}

DWORD WINAPI WinHttpProtocolSend(HINTERNET handle, ULONGLONG context, BYTE *data, DWORD length)
{
    if (!data || !length || context)
        return ERROR_INVALID_PARAMETER;
    return ProtocolTransfer(handle, TRUE, data, length, nullptr);
}

DWORD WINAPI WinHttpProtocolReceive(HINTERNET handle, ULONGLONG context, BYTE *buffer, DWORD length, DWORD *read)
{
    if (!read)
        return ERROR_INVALID_PARAMETER;
    *read = 0;
    if (!buffer || !length || context)
        return ERROR_INVALID_PARAMETER;
    return ProtocolTransfer(handle, FALSE, buffer, length, read);
}
