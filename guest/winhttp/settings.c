/*
 * the proxy settings of a connection. Windows has the proxy discovery service read and write them for the caller it
 * impersonates; here the caller does the same work on the same registry values.
 */
#include "winhttp_private.h"
#include <mime/ntrtl.h>
#include <mime/ntrtlsec.h>

#ifndef ERROR_WINHTTP_INCORRECT_HANDLE_STATE
#define ERROR_WINHTTP_INCORRECT_HANDLE_STATE 12019L
#endif

#define WH_TOKEN_QUERY 0x0008
#define WH_TOKEN_INTEGRITY_LEVEL 25
#define WH_INTEGRITY_MEDIUM 0x2000

#define SETTINGS_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings"
#define CONNECTIONS_KEY SETTINGS_KEY L"\\Connections"
#define POLICY_KEY L"SOFTWARE\\Policies\\Microsoft\\Windows\\CurrentVersion\\Internet Settings"
#define DEFAULT_CONNECTION L"DefaultConnectionSettings"
#define SAVED_LEGACY L"SavedLegacySettings"

/* the size a blob states since it carries network keys, and the least one with the fields read here. */
#define BLOB_SIZE 0x46
#define BLOB_SIZE_MIN 0x3C

/* task memory, which the callers of these exports free themselves. */

typedef PVOID(WINAPI *WH_TASK_ALLOC)(SIZE_T);
typedef void(WINAPI *WH_TASK_FREE)(PVOID);

static INIT_ONCE TaskOnce = INIT_ONCE_STATIC_INIT;
static WH_TASK_ALLOC TaskAllocate;
static WH_TASK_FREE TaskRelease;

static BOOL CALLBACK InitTask(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    HMODULE ole32 = LoadLibraryW(L"ole32.dll");
    if (ole32) {
        TaskAllocate = (WH_TASK_ALLOC)GetProcAddress(ole32, "CoTaskMemAlloc");
        TaskRelease = (WH_TASK_FREE)GetProcAddress(ole32, "CoTaskMemFree");
    }
    return TRUE;
}

void *WhTaskAlloc(SIZE_T size)
{
    InitOnceExecuteOnce(&TaskOnce, InitTask, nullptr, nullptr);
    void *memory = TaskAllocate ? TaskAllocate(size ? size : 1) : nullptr;
    if (memory)
        memset(memory, 0, size);
    return memory;
}

void WhTaskFree(void *memory)
{
    if (!memory)
        return;
    InitOnceExecuteOnce(&TaskOnce, InitTask, nullptr, nullptr);
    if (TaskRelease)
        TaskRelease(memory);
}

DWORD WhTaskDupW(LPCWSTR text, PWSTR *copy)
{
    *copy = nullptr;
    if (!text)
        return ERROR_SUCCESS;
    SIZE_T size = (wcslen(text) + 1) * sizeof(WCHAR);
    PWSTR memory = WhTaskAlloc(size);
    if (!memory)
        return ERROR_NOT_ENOUGH_MEMORY;
    memcpy(memory, text, size);
    *copy = memory;
    return ERROR_SUCCESS;
}

static void TaskRelinquish(PVOID *pointer)
{
    PVOID memory = *pointer;
    *pointer = nullptr;
    WhTaskFree(memory);
}

void WhFreeProxySettings(WH_PROXY_SETTINGS *settings)
{
    TaskRelinquish((PVOID *)&settings->ConnectionName);
    TaskRelinquish((PVOID *)&settings->Proxy);
    TaskRelinquish((PVOID *)&settings->ProxyBypass);
    TaskRelinquish((PVOID *)&settings->LastKnownGoodAutoConfigUrl);
    TaskRelinquish((PVOID *)&settings->AutoconfigUrl);
    TaskRelinquish((PVOID *)&settings->AutoconfigSecondaryUrl);
    TaskRelinquish((PVOID *)&settings->DetectedInterfaceIp);
    TaskRelinquish(&settings->NetworkKeys);
}

DWORD WhCopyProxySettings(const WH_PROXY_SETTINGS *from, WH_PROXY_SETTINGS *to)
{
    *to = *from;
    to->ConnectionName = to->Proxy = to->ProxyBypass = to->AutoconfigUrl = to->AutoconfigSecondaryUrl = nullptr;
    to->LastKnownGoodAutoConfigUrl = nullptr;
    to->DetectedInterfaceIp = nullptr;
    to->NetworkKeys = nullptr;
    DWORD error = WhTaskDupW(from->ConnectionName, &to->ConnectionName);
    if (!error)
        error = WhTaskDupW(from->Proxy, &to->Proxy);
    if (!error)
        error = WhTaskDupW(from->ProxyBypass, &to->ProxyBypass);
    if (!error)
        error = WhTaskDupW(from->AutoconfigUrl, &to->AutoconfigUrl);
    if (!error)
        error = WhTaskDupW(from->AutoconfigSecondaryUrl, &to->AutoconfigSecondaryUrl);
    if (!error)
        error = WhTaskDupW(from->LastKnownGoodAutoConfigUrl, &to->LastKnownGoodAutoConfigUrl);
    if (!error && from->DetectedInterfaceIpCount && from->DetectedInterfaceIp) {
        SIZE_T size = (SIZE_T)from->DetectedInterfaceIpCount * sizeof(DWORD);
        if ((to->DetectedInterfaceIp = WhTaskAlloc(size)))
            memcpy(to->DetectedInterfaceIp, from->DetectedInterfaceIp, size);
        else
            error = ERROR_NOT_ENOUGH_MEMORY;
    }
    if (!error && from->NetworkKeyCount && from->NetworkKeys) {
        SIZE_T size = (SIZE_T)from->NetworkKeyCount * WH_NETWORK_KEY_SIZE;
        if ((to->NetworkKeys = WhTaskAlloc(size)))
            memcpy(to->NetworkKeys, from->NetworkKeys, size);
        else
            error = ERROR_NOT_ENOUGH_MEMORY;
    }
    if (error) {
        WhFreeProxySettings(to);
        memset(to, 0, sizeof(*to));
    }
    return error;
}

/*
 * the counters every process reads to learn that the settings changed. Windows has the service create the section and
 * the others open it to read, with a version of 0 while it does not exist.
 */

#define COUNTERS_NAME L"Global\\F932B6C7-3A20-46A0-B8A0-8894AA421973"
#define COUNTER_SETTINGS 0

static SRWLOCK CountersLock = SRWLOCK_INIT;
static volatile LONG *Counters;

/* the section of the service lets everyone read it. here every writer of settings raises the counters, so everyone writes. */
static HANDLE CreateCounters(void)
{
    static const SID world = {SID_REVISION, 1, {{0, 0, 0, 0, 0, 1}}, {0}};
    struct {
        ACL Acl;
        BYTE Room[32];
    } dacl;
    SECURITY_DESCRIPTOR descriptor;
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), &descriptor, FALSE};
    if (RtlCreateAcl(&dacl.Acl, sizeof(dacl), ACL_REVISION) < 0 ||
        RtlAddAccessAllowedAce(&dacl.Acl, ACL_REVISION, SECTION_QUERY | SECTION_MAP_READ | SECTION_MAP_WRITE | READ_CONTROL,
                               (PSID)&world) < 0 ||
        RtlCreateSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION) < 0 ||
        RtlSetDaclSecurityDescriptor(&descriptor, TRUE, &dacl.Acl, FALSE) < 0)
        return nullptr;
    return CreateFileMappingW(INVALID_HANDLE_VALUE, &attributes, PAGE_READWRITE, 0, 2 * sizeof(LONG), COUNTERS_NAME);
}

static volatile LONG *OpenCounters(BOOL create)
{
    AcquireSRWLockExclusive(&CountersLock);
    if (!Counters) {
        HANDLE mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, COUNTERS_NAME);
        if (!mapping && create)
            mapping = CreateCounters();
        if (mapping) {
            Counters = MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 2 * sizeof(LONG));
            CloseHandle(mapping);
        }
    }
    volatile LONG *counters = Counters;
    ReleaseSRWLockExclusive(&CountersLock);
    return counters;
}

static DWORD SettingsVersion(void)
{
    volatile LONG *counters = OpenCounters(FALSE);
    return counters ? (DWORD)counters[COUNTER_SETTINGS] : 0;
}

static void RaiseSettingsVersion(void)
{
    volatile LONG *counters = OpenCounters(TRUE);
    if (counters)
        InterlockedIncrement(&counters[COUNTER_SETTINGS]);
}

/* the registry values. */

static SRWLOCK RegistryLock = SRWLOCK_INIT;

/* the settings are those of the user unless a policy makes them the machine's. */
static HKEY BaseKey(void)
{
    DWORD per_user = 1, size = sizeof(per_user);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, POLICY_KEY, L"ProxySettingsPerUser", RRF_RT_REG_DWORD, nullptr, &per_user, &size))
        per_user = 1;
    return per_user ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
}

/* the service is a 64-bit process, so the settings of the machine are those of the 64-bit view for every caller. */
static REGSAM ViewOf(HKEY base)
{
    return base == HKEY_LOCAL_MACHINE ? KEY_WOW64_64KEY : 0;
}

typedef struct {
    const BYTE *Data;
    DWORD Size;
    DWORD Offset;
} WH_BLOB_READER;

/* a field the blob ends before reads as zeros. */
static void BlobBytes(WH_BLOB_READER *blob, void *out, DWORD size)
{
    memset(out, 0, size);
    DWORD left = blob->Size - blob->Offset;
    if (left > size)
        left = size;
    memcpy(out, blob->Data + blob->Offset, left);
    blob->Offset += left;
}

static DWORD BlobDword(WH_BLOB_READER *blob)
{
    DWORD value;
    BlobBytes(blob, &value, sizeof(value));
    return value;
}

/* a string is its length and its characters in the code page of the machine, and an empty one is no string. */
static DWORD BlobString(WH_BLOB_READER *blob, PWSTR *text)
{
    *text = nullptr;
    DWORD length = BlobDword(blob);
    if (length > blob->Size - blob->Offset)
        length = blob->Size - blob->Offset;
    const char *characters = (const char *)blob->Data + blob->Offset;
    blob->Offset += length;
    if (!length)
        return ERROR_SUCCESS;
    int count = MultiByteToWideChar(CP_ACP, 0, characters, (int)length, nullptr, 0);
    PWSTR wide = WhTaskAlloc(((SIZE_T)count + 1) * sizeof(WCHAR));
    if (!wide)
        return ERROR_NOT_ENOUGH_MEMORY;
    MultiByteToWideChar(CP_ACP, 0, characters, (int)length, wide, count);
    *text = wide;
    return ERROR_SUCCESS;
}

static BYTE *ReadValue(HKEY base, LPCWSTR name, DWORD *size)
{
    HKEY key;
    *size = 0;
    if (RegOpenKeyExW(base, CONNECTIONS_KEY, 0, KEY_READ | ViewOf(base), &key))
        return nullptr;
    DWORD type = 0, length = 0;
    BYTE *data = nullptr;
    if (!RegQueryValueExW(key, name, nullptr, &type, nullptr, &length) && length && (data = WhAlloc(length)) &&
        RegQueryValueExW(key, name, nullptr, &type, data, &length)) {
        WhFree(data);
        data = nullptr;
    }
    RegCloseKey(key);
    if (data)
        *size = length;
    return data;
}

/* FALSE for a value that is missing or older than the fields read here. */
static BOOL ParseBlob(const BYTE *data, DWORD size, WH_PROXY_SETTINGS *settings, DWORD *error)
{
    WH_BLOB_READER blob = {data, size, 0};
    *error = ERROR_SUCCESS;
    if (!data || size < sizeof(DWORD))
        return FALSE;
    DWORD stated = BlobDword(&blob);
    if (stated < BLOB_SIZE_MIN)
        return FALSE;
    settings->CurrentSettingsVersion = BlobDword(&blob);
    settings->Flags = BlobDword(&blob);
    if ((*error = BlobString(&blob, &settings->Proxy)) || (*error = BlobString(&blob, &settings->ProxyBypass)) ||
        (*error = BlobString(&blob, &settings->AutoconfigUrl)))
        return TRUE;
    settings->AutoDiscoveryFlags = BlobDword(&blob);
    if ((*error = BlobString(&blob, &settings->LastKnownGoodAutoConfigUrl)))
        return TRUE;
    BlobBytes(&blob, &settings->LastKnownDetectTime, sizeof(settings->LastKnownDetectTime));
    DWORD count = BlobDword(&blob);
    if (count > (blob.Size - blob.Offset) / sizeof(DWORD))
        count = (blob.Size - blob.Offset) / sizeof(DWORD);
    if (count) {
        if (!(settings->DetectedInterfaceIp = WhTaskAlloc((SIZE_T)count * sizeof(DWORD)))) {
            *error = ERROR_NOT_ENOUGH_MEMORY;
            return TRUE;
        }
        BlobBytes(&blob, settings->DetectedInterfaceIp, count * sizeof(DWORD));
        settings->DetectedInterfaceIpCount = count;
    }
    if ((*error = BlobString(&blob, &settings->AutoconfigSecondaryUrl)))
        return TRUE;
    settings->AutoconfigReloadDelayMins = BlobDword(&blob);
    if (stated < BLOB_SIZE)
        return TRUE;
    count = BlobDword(&blob);
    if (count > (blob.Size - blob.Offset) / WH_NETWORK_KEY_SIZE)
        count = (blob.Size - blob.Offset) / WH_NETWORK_KEY_SIZE;
    if (count) {
        if (!(settings->NetworkKeys = WhTaskAlloc((SIZE_T)count * WH_NETWORK_KEY_SIZE))) {
            *error = ERROR_NOT_ENOUGH_MEMORY;
            return TRUE;
        }
        BlobBytes(&blob, settings->NetworkKeys, count * WH_NETWORK_KEY_SIZE);
        settings->NetworkKeyCount = count;
    }
    return TRUE;
}

static DWORD WriteSettings(HKEY base, WH_PROXY_SETTINGS *settings, BOOL force);
static void MigrateLegacy(HKEY base, WH_PROXY_SETTINGS *settings, BOOL write);

/*
 * the settings of a connection, by the rules of the service: a connection without a value has direct access, with the
 * settings of the default connection when asked to fall back, the default connection without a value detects its proxy,
 * and a version is never 0.
 */
static DWORD ReadConnection(HKEY base, LPCWSTR name, BOOL fall_back, BOOL auto_discover, BOOL migrate, BOOL write,
                            BOOL *defaults, WH_PROXY_SETTINGS *settings)
{
    BOOL is_default = !name || !*name || !lstrcmpiW(name, DEFAULT_CONNECTION);
    memset(settings, 0, sizeof(*settings));
    settings->StructSize = WH_PROXY_SETTINGS_SIZE;
    settings->Flags = WH_SETTINGS_DIRECT;
    DWORD size, error;
    BYTE *data = ReadValue(base, is_default ? DEFAULT_CONNECTION : name, &size);
    BOOL found = ParseBlob(data, size, settings, &error);
    WhFree(data);
    if (found && error) {
        WhFreeProxySettings(settings);
        return error;
    }
    if (!found) {
        if (is_default) {
            settings->Flags |= WH_SETTINGS_AUTO_DETECT;
        } else if (fall_back) {
            if ((error = ReadConnection(base, nullptr, FALSE, auto_discover, migrate, FALSE, nullptr, settings)))
                return error;
        } else if (auto_discover) {
            settings->Flags |= WH_SETTINGS_AUTO_DETECT;
        }
        if (defaults)
            *defaults = TRUE;
    }
    if (!settings->CurrentSettingsVersion)
        settings->CurrentSettingsVersion = 1;
    if (is_default && migrate)
        MigrateLegacy(base, settings, write);
    return ERROR_SUCCESS;
}

/* the values that held the proxy before the blob, which programs still write on their own. */
typedef struct {
    DWORD Flags;
    BOOL HasAutoDetect;
    PWSTR Proxy;
    PWSTR Bypass;
    PWSTR Url;
} WH_LEGACY;

static DWORD LegacyString(HKEY key, LPCWSTR name, PWSTR *text)
{
    *text = nullptr;
    DWORD type = 0, size = 0;
    LSTATUS status = RegQueryValueExW(key, name, nullptr, &type, nullptr, &size);
    if (status == ERROR_FILE_NOT_FOUND)
        return ERROR_SUCCESS;
    if (status)
        return status;
    PWSTR value = WhTaskAlloc(size + 2 * sizeof(WCHAR));
    if (!value)
        return ERROR_NOT_ENOUGH_MEMORY;
    if ((status = RegQueryValueExW(key, name, nullptr, &type, (BYTE *)value, &size)) || !*value) {
        WhTaskFree(value);
        return status;
    }
    *text = value;
    return ERROR_SUCCESS;
}

static void FreeLegacy(WH_LEGACY *legacy)
{
    WhTaskFree(legacy->Proxy);
    WhTaskFree(legacy->Bypass);
    WhTaskFree(legacy->Url);
}

static BOOL ReadLegacy(HKEY key, WH_LEGACY *legacy)
{
    memset(legacy, 0, sizeof(*legacy));
    DWORD enable = 0, size = sizeof(enable), type = 0;
    if (RegQueryValueExW(key, L"ProxyEnable", nullptr, nullptr, (BYTE *)&enable, &size))
        return FALSE;
    legacy->Flags = enable ? WH_SETTINGS_DIRECT | WH_SETTINGS_PROXY : WH_SETTINGS_DIRECT;
    if (LegacyString(key, L"ProxyServer", &legacy->Proxy) || LegacyString(key, L"ProxyOverride", &legacy->Bypass) ||
        LegacyString(key, L"AutoConfigURL", &legacy->Url)) {
        FreeLegacy(legacy);
        return FALSE;
    }
    DWORD detect = 0;
    size = sizeof(detect);
    LSTATUS status = RegQueryValueExW(key, L"AutoDetect", nullptr, &type, (BYTE *)&detect, &size);
    if (status && status != ERROR_FILE_NOT_FOUND) {
        FreeLegacy(legacy);
        return FALSE;
    }
    if (!status && type == REG_DWORD) {
        legacy->HasAutoDetect = TRUE;
        if (detect)
            legacy->Flags |= WH_SETTINGS_AUTO_DETECT;
    }
    if (legacy->Url)
        legacy->Flags |= WH_SETTINGS_CONFIG_URL;
    return TRUE;
}

static BOOL SameText(LPCWSTR a, LPCWSTR b)
{
    if (!a || !b)
        return !a && !b;
    return !lstrcmpiW(a, b);
}

/*
 * legacy values that differ from the copy saved at the last write were changed by a program, so they replace the proxy,
 * its bypass list and the script of the settings, which are written back.
 */
static void MigrateLegacy(HKEY base, WH_PROXY_SETTINGS *settings, BOOL write)
{
    HKEY key, connections;
    if (RegOpenKeyExW(base, SETTINGS_KEY, 0, KEY_READ | KEY_WRITE | ViewOf(base), &key))
        return;
    if (RegOpenKeyExW(key, L"Connections", 0, KEY_READ | KEY_WRITE | ViewOf(base), &connections)) {
        RegCloseKey(key);
        return;
    }
    RegCloseKey(connections);
    DWORD migrate = 0, size = sizeof(migrate);
    RegQueryValueExW(key, L"MigrateProxy", nullptr, nullptr, (BYTE *)&migrate, &size);
    WH_LEGACY legacy;
    BOOL read = migrate && ReadLegacy(key, &legacy);
    RegCloseKey(key);
    if (!read)
        return;
    DWORD mask = WH_SETTINGS_PROXY | WH_SETTINGS_CONFIG_URL | (legacy.HasAutoDetect ? WH_SETTINGS_AUTO_DETECT : 0);
    WH_PROXY_SETTINGS saved;
    if (ReadConnection(base, SAVED_LEGACY, FALSE, FALSE, FALSE, FALSE, nullptr, &saved)) {
        FreeLegacy(&legacy);
        return;
    }
    BOOL same = (legacy.Flags & mask) == (saved.Flags & mask) &&
                (!(saved.Flags & WH_SETTINGS_CONFIG_URL) || SameText(legacy.Url, saved.AutoconfigUrl)) &&
                SameText(legacy.Proxy, saved.Proxy) && SameText(legacy.Bypass, saved.ProxyBypass);
    WhFreeProxySettings(&saved);
    if (same) {
        FreeLegacy(&legacy);
        return;
    }
    WH_PROXY_SETTINGS merged = {WH_PROXY_SETTINGS_SIZE};
    merged.CurrentSettingsVersion = settings->CurrentSettingsVersion;
    merged.Flags = legacy.Flags | (settings->Flags & ~mask);
    merged.Proxy = legacy.Proxy;
    merged.ProxyBypass = legacy.Bypass;
    merged.AutoconfigUrl = legacy.Url;
    if (!merged.AutoconfigUrl && WhTaskDupW(settings->AutoconfigUrl, &merged.AutoconfigUrl)) {
        WhFreeProxySettings(&merged);
        return;
    }
    if (write) {
        if (WriteSettings(base, &merged, TRUE)) {
            WhFreeProxySettings(&merged);
            return;
        }
        WhNotifyProxyConfigChange();
    }
    settings->CurrentSettingsVersion = merged.CurrentSettingsVersion;
    settings->Flags = (merged.Flags & mask) | (settings->Flags & ~mask);
    WhTaskFree(settings->Proxy);
    WhTaskFree(settings->ProxyBypass);
    WhTaskFree(settings->AutoconfigUrl);
    settings->Proxy = merged.Proxy;
    settings->ProxyBypass = merged.ProxyBypass;
    settings->AutoconfigUrl = merged.AutoconfigUrl;
}

DWORD WhReadProxySettings(LPCWSTR connection, BOOL fall_back, BOOL auto_discover, DWORD *version, BOOL *defaults,
                          WH_PROXY_SETTINGS *settings)
{
    HKEY base = BaseKey();
    BOOL unset = FALSE;
    AcquireSRWLockExclusive(&RegistryLock);
    DWORD error = ReadConnection(base, connection, fall_back, auto_discover, TRUE, TRUE, &unset, settings);
    ReleaseSRWLockExclusive(&RegistryLock);
    if (error)
        return error;
    /* the name comes back as it was given, and no name stays none. */
    if (connection && (error = WhTaskDupW(connection, &settings->ConnectionName))) {
        WhFreeProxySettings(settings);
        memset(settings, 0, sizeof(*settings));
        return error;
    }
    if (version)
        *version = SettingsVersion();
    if (defaults)
        *defaults = unset;
    return ERROR_SUCCESS;
}

/* writing. */

typedef struct {
    BYTE *Data;
    DWORD Size;
    DWORD Capacity;
    BOOL Failed;
} WH_BLOB_WRITER;

static void PutBytes(WH_BLOB_WRITER *blob, const void *data, DWORD size)
{
    if (blob->Failed)
        return;
    if (blob->Size + size > blob->Capacity) {
        DWORD capacity = (blob->Size + size) * 2 + 64;
        BYTE *grown = WhRealloc(blob->Data, capacity);
        if (!grown) {
            blob->Failed = TRUE;
            return;
        }
        blob->Data = grown;
        blob->Capacity = capacity;
    }
    memcpy(blob->Data + blob->Size, data, size);
    blob->Size += size;
}

static void PutDword(WH_BLOB_WRITER *blob, DWORD value)
{
    PutBytes(blob, &value, sizeof(value));
}

static void PutString(WH_BLOB_WRITER *blob, LPCWSTR text)
{
    int length = text ? WideCharToMultiByte(CP_ACP, 0, text, -1, nullptr, 0, nullptr, nullptr) : 0;
    if (length <= 1) {
        PutDword(blob, 0);
        return;
    }
    char *narrow = WhAlloc((SIZE_T)length);
    if (!narrow) {
        blob->Failed = TRUE;
        return;
    }
    WideCharToMultiByte(CP_ACP, 0, text, -1, narrow, length, nullptr, nullptr);
    PutDword(blob, (DWORD)length - 1);
    PutBytes(blob, narrow, (DWORD)length - 1);
    WhFree(narrow);
}

static DWORD SetOrDelete(HKEY key, LPCWSTR name, LPCWSTR text)
{
    if (text)
        return RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)text, (DWORD)((wcslen(text) + 1) * sizeof(WCHAR)));
    LSTATUS status = RegDeleteValueW(key, name);
    return status == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : status;
}

static DWORD WriteLegacy(HKEY base, REGSAM view, const WH_PROXY_SETTINGS *settings)
{
    HKEY key;
    DWORD error = RegCreateKeyExW(base, SETTINGS_KEY, 0, nullptr, 0, KEY_READ | KEY_SET_VALUE | view, nullptr, &key, nullptr);
    if (error)
        return error;
    DWORD enable = (settings->Flags & WH_SETTINGS_PROXY) != 0, one = 1;
    error = RegSetValueExW(key, L"ProxyEnable", 0, REG_DWORD, (const BYTE *)&enable, sizeof(enable));
    if (!error)
        error = SetOrDelete(key, L"ProxyServer", settings->Proxy);
    if (!error)
        error = SetOrDelete(key, L"ProxyOverride", settings->ProxyBypass);
    if (!error)
        error = SetOrDelete(key, L"AutoConfigURL", settings->Flags & WH_SETTINGS_CONFIG_URL ? settings->AutoconfigUrl : nullptr);
    if (!error)
        error = SetOrDelete(key, L"AutoDetect", nullptr);
    if (!error)
        RegSetValueExW(key, L"MigrateProxy", 0, REG_DWORD, (const BYTE *)&one, sizeof(one));
    RegCloseKey(key);
    return error;
}

/* one view of the registry: the legacy values, the blob and its copy, then the counter every process watches. */
static DWORD WriteView(HKEY base, REGSAM view, const WH_PROXY_SETTINGS *settings, const BYTE *blob, DWORD size)
{
    DWORD error = WriteLegacy(base, view, settings);
    HKEY key = nullptr;
    if (!error)
        error = RegCreateKeyExW(base, CONNECTIONS_KEY, 0, nullptr, 0, KEY_READ | KEY_SET_VALUE | view, nullptr, &key, nullptr);
    if (!error)
        error = RegSetValueExW(key, SAVED_LEGACY, 0, REG_BINARY, blob, size);
    if (!error)
        error = RegSetValueExW(key, DEFAULT_CONNECTION, 0, REG_BINARY, blob, size);
    if (key)
        RegCloseKey(key);
    if (!error)
        RaiseSettingsVersion();
    return error;
}

/*
 * the settings always go to the default connection, with a copy that tells a later read what the legacy values were.
 * a write based on an older version is refused unless forced, and each write raises the version of the settings. the
 * settings of the machine are written to the 64-bit view, then to the 32-bit one, each raising the counter.
 */
static DWORD WriteSettings(HKEY base, WH_PROXY_SETTINGS *settings, BOOL force)
{
    DWORD size;
    BYTE *data = ReadValue(base, DEFAULT_CONNECTION, &size);
    if (data) {
        WH_BLOB_READER current = {data, size, 0};
        DWORD stated = BlobDword(&current), version = BlobDword(&current);
        WhFree(data);
        if (!version)
            version = 1;
        if (version != settings->CurrentSettingsVersion) {
            if (!force && stated == BLOB_SIZE)
                return ERROR_INVALID_DATA;
            settings->CurrentSettingsVersion = version;
        }
    }
    if (++settings->CurrentSettingsVersion == 0)
        settings->CurrentSettingsVersion = 1;
    WH_BLOB_WRITER blob = {0};
    PutDword(&blob, BLOB_SIZE);
    PutDword(&blob, settings->CurrentSettingsVersion);
    PutDword(&blob, settings->Flags);
    PutString(&blob, settings->Proxy);
    PutString(&blob, settings->ProxyBypass);
    PutString(&blob, settings->AutoconfigUrl);
    PutDword(&blob, settings->AutoDiscoveryFlags);
    PutString(&blob, settings->LastKnownGoodAutoConfigUrl);
    PutBytes(&blob, &settings->LastKnownDetectTime, sizeof(settings->LastKnownDetectTime));
    DWORD addresses = settings->DetectedInterfaceIp ? settings->DetectedInterfaceIpCount : 0;
    PutDword(&blob, addresses);
    PutBytes(&blob, settings->DetectedInterfaceIp, addresses * sizeof(DWORD));
    PutString(&blob, settings->AutoconfigSecondaryUrl);
    PutDword(&blob, settings->AutoconfigReloadDelayMins);
    DWORD keys = settings->NetworkKeys ? settings->NetworkKeyCount : 0;
    PutDword(&blob, keys);
    PutBytes(&blob, settings->NetworkKeys, keys * WH_NETWORK_KEY_SIZE);
    if (blob.Failed) {
        WhFree(blob.Data);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    DWORD error;
    if (base == HKEY_LOCAL_MACHINE) {
        error = WriteView(base, KEY_WOW64_64KEY, settings, blob.Data, blob.Size);
        if (!error)
            error = WriteView(base, KEY_WOW64_32KEY, settings, blob.Data, blob.Size);
    } else {
        error = WriteView(base, 0, settings, blob.Data, blob.Size);
    }
    WhFree(blob.Data);
    return error;
}

/* the exports. */

void WINAPI WinHttpFreeProxySettings(WH_PROXY_SETTINGS *settings)
{
    if (settings)
        WhFreeProxySettings(settings);
}

DWORD WINAPI WinHttpGetProxySettingsVersion(HINTERNET handle, DWORD *version)
{
    if (!version)
        return ERROR_INVALID_PARAMETER;
    *version = 0;
    WH_OBJECT *session;
    DWORD error = WhReferenceOfType(handle, WH_SESSION, &session);
    if (error)
        return error;
    WhRelease(session);
    *version = SettingsVersion();
    return ERROR_SUCCESS;
}

DWORD WINAPI WinHttpReadProxySettings(HINTERNET handle, LPCWSTR connection, BOOL fall_back, BOOL auto_discover,
                                      DWORD *version, BOOL *defaults, WH_PROXY_SETTINGS *settings)
{
    if (defaults)
        *defaults = FALSE;
    if (!version || !settings || !defaults || settings->StructSize != WH_PROXY_SETTINGS_SIZE)
        return ERROR_INVALID_PARAMETER;
    memset(settings, 0, WH_PROXY_SETTINGS_SIZE);
    settings->StructSize = WH_PROXY_SETTINGS_SIZE;
    WH_OBJECT *session;
    DWORD error = WhReferenceOfType(handle, WH_SESSION, &session);
    if (error)
        return error;
    WhRelease(session);
    WH_PROXY_SETTINGS read;
    if ((error = WhReadProxySettings(connection, fall_back, auto_discover, version, defaults, &read))) {
        *defaults = FALSE;
        return error;
    }
    *settings = read;
    settings->StructSize = WH_PROXY_SETTINGS_SIZE;
    return ERROR_SUCCESS;
}

DWORD WINAPI WinHttpReadProxySettingsHvsi(HINTERNET handle, LPCWSTR connection, BOOL fall_back, BOOL auto_discover,
                                          int force_default, DWORD *version, BOOL *defaults,
                                          WH_PROXY_SETTINGS *settings)
{
    if (!version || !settings || !defaults)
        return ERROR_INVALID_PARAMETER;
    *version = 0;
    *defaults = FALSE;
    memset(settings, 0, WH_PROXY_SETTINGS_SIZE);
    settings->StructSize = WH_PROXY_SETTINGS_SIZE;
    return WinHttpReadProxySettings(handle, force_default ? DEFAULT_CONNECTION : connection, fall_back, auto_discover,
                                    version, defaults, settings);
}

/* WxGetThreadIntegrityLevel: the last subauthority of the label of the token. */
static DWORD IntegrityLevel(void)
{
    HANDLE token;
    struct {
        PSID Sid;
        DWORD Attributes;
        BYTE Room[SECURITY_MAX_SID_SIZE];
    } label;
    ULONG size = 0;
    DWORD level = 0;
    if (NtOpenProcessToken(NtCurrentProcess(), WH_TOKEN_QUERY, &token) < 0)
        return level;
    if (NtQueryInformationToken(token, WH_TOKEN_INTEGRITY_LEVEL, &label, sizeof(label), &size) >= 0 && label.Sid)
        level = *RtlSubAuthoritySid(label.Sid, *RtlSubAuthorityCountSid(label.Sid) - 1);
    NtClose(token);
    return level;
}

/*
 * the settings go to the service as they are and nothing comes back: the structure of the caller keeps its version,
 * and winhttp 26300 does not look at what the service answered, so a write the service refused answers success too.
 * measured on Windows 11: a stale write that is not forced and the write of a token that may not change the machine's
 * settings both answer 0 and change nothing. the service refuses a caller below the medium integrity level.
 */
DWORD WINAPI WinHttpWriteProxySettings(HINTERNET handle, BOOL force_update, WH_PROXY_SETTINGS *settings)
{
    if (!settings || settings->StructSize != WH_PROXY_SETTINGS_SIZE)
        return ERROR_INVALID_PARAMETER;
    WH_OBJECT *session;
    DWORD error = WhReferenceOfType(handle, WH_SESSION, &session);
    if (error)
        return error;
    WhRelease(session);
    if (IntegrityLevel() < WH_INTEGRITY_MEDIUM)
        return ERROR_SUCCESS;
    WH_PROXY_SETTINGS sent;
    if ((error = WhCopyProxySettings(settings, &sent)))
        return error;
    AcquireSRWLockExclusive(&RegistryLock);
    error = WriteSettings(BaseKey(), &sent, force_update);
    ReleaseSRWLockExclusive(&RegistryLock);
    WhFreeProxySettings(&sent);
    if (!error)
        WhNotifyProxyConfigChange();
    return ERROR_SUCCESS;
}

/*
 * SetProxySettingsPerUser of the service, which works as its caller: settings of the user delete the value of the
 * policy, settings of the machine set it to 0, and the counter rises when the registry changed.
 */
DWORD WINAPI WinHttpSetProxySettingsPerUser(DWORD per_user)
{
    HINTERNET session = WinHttpOpen(nullptr, WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);
    if (!session)
        return GetLastError();
    WinHttpCloseHandle(session);
    HKEY key;
    LSTATUS status;
    if (per_user) {
        status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, POLICY_KEY, 0, KEY_SET_VALUE, &key);
        if (!status) {
            status = RegDeleteValueW(key, L"ProxySettingsPerUser");
            RegCloseKey(key);
        }
        if (status)
            return status == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : (DWORD)status;
    } else {
        status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, POLICY_KEY, 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &key, nullptr);
        if (status)
            return status;
        DWORD value = 0, size = sizeof(value);
        if (!RegGetValueW(key, nullptr, L"ProxySettingsPerUser", RRF_RT_DWORD, nullptr, &value, &size) && !value) {
            RegCloseKey(key);
            return ERROR_SUCCESS;
        }
        value = 0;
        status = RegSetValueExW(key, L"ProxySettingsPerUser", 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
        RegCloseKey(key);
        if (status)
            return status;
    }
    RaiseSettingsVersion();
    return ERROR_SUCCESS;
}
