/* svchost.exe: the command line, the group's registry settings and the service dispatcher table. */
#include "svchost.h"
#include <stdarg.h>

#define SVCHOST_KEY L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Svchost"

HOST_SERVICE *g_services;
DWORD g_service_count;
/* the process waits for the DLLs to unload before it ends, unless a service asked for the old COM behavior. */
BOOL g_wait_for_unloads = TRUE;

void HostLog(const char *format, ...)
{
    char text[256];
    va_list arguments;
    va_start(arguments, format);
    int n = _vsnprintf(text, sizeof(text) - 2, format, arguments);
    va_end(arguments);
    if (n < 0 || n > (int)sizeof(text) - 2)
        n = sizeof(text) - 2;
    text[n++] = '\n';
    text[n] = 0;
    OutputDebugStringA(text);
}

WCHAR *HostDup(const WCHAR *s)
{
    SIZE_T bytes = (wcslen(s) + 1) * sizeof(WCHAR);
    WCHAR *copy = HeapAlloc(GetProcessHeap(), 0, bytes);
    if (copy)
        memcpy(copy, s, bytes);
    return copy;
}

/* ------------------------------------------------------------------ command line */

static BOOL is_blank(WCHAR c)
{
    return c == L' ' || c == L'\t';
}

/*
 * `<program> [-k <group>] [-s <service>] [-p]`, options start with - or / and ignore case. the program name ends at
 * the first blank, the group and service may be quoted. the words are cut out of a copy of the line.
 */
static BOOL parse_command(const WCHAR *line, HOST_OPTIONS *o)
{
    o->Line = HostDup(line);
    if (!o->Line)
        return FALSE;
    WCHAR *p = o->Line;
    if (*p == L'"') {
        p++;
        while (*p && *p != L'"')
            p++;
        if (*p)
            p++;
    } else {
        while (*p && !is_blank(*p))
            p++;
    }
    if (*p)
        *p++ = 0;
    WCHAR **target = nullptr;
    while (*p) {
        while (is_blank(*p))
            p++;
        if (!*p)
            break;
        if ((*p == L'-' || *p == L'/') && p[1]) {
            WCHAR option = p[1] | 0x20;
            if (option == L'k')
                target = &o->Group;
            else if (option == L's')
                target = &o->Service;
            else if (option == L'p')
                o->Mitigation = TRUE;
            p += 2;
            continue;
        }
        WCHAR *word = p;
        if (*p == L'"') {
            word = ++p;
            while (*p && *p != L'"')
                p++;
        } else {
            while (*p && !is_blank(*p))
                p++;
        }
        if (*p)
            *p++ = 0;
        if (target) {
            *target = word;
            target = nullptr;
        }
    }
    return o->Group != nullptr;
}


/* ------------------------------------------------------------------ group */

#define GROUP_FLAG_ALLOW_LOWBOX 0x02
#define GROUP_FLAG_NO_GUI 0x08
#define GROUP_FLAG_ALLOW_INTERACTIVE 0x10
#define GROUP_FLAG_ALLOW_COM_CAPABILITY 0x20
#define GROUP_FLAG_ALLOW_CROSS_CONTAINER 0x40

#define POLICY_DYNAMIC_CODE 2
#define POLICY_EXTENSION_POINTS 6
#define POLICY_SIGNATURE 8
#define POLICY_REDIRECTION_TRUST 16

#define MAX_SERVICE_NAME 256

WINADVAPI BOOL WINAPI IsValidSecurityDescriptor(PSECURITY_DESCRIPTOR descriptor);

/* process setup read from the group's key. */
typedef struct GROUP_SETTINGS {
    BOOL init_com_security;
    DWORD authn_level;
    DWORD impersonation_level;
    DWORD capabilities;
    DWORD flags;
    void *access_descriptor; /* self relative, from the registry, or null. */
    DWORD dynamic_code;
    DWORD signature;
    DWORD extension_points;
    DWORD redirection_trust;
} GROUP_SETTINGS;

typedef struct NAME_LIST {
    WCHAR **items;
    DWORD count;
    DWORD capacity;
} NAME_LIST;

static BOOL add_name(NAME_LIST *list, WCHAR *name)
{
    if (list->count == list->capacity) {
        DWORD capacity = list->capacity ? list->capacity * 2 : 16;
        WCHAR **grown = list->items ? HeapReAlloc(GetProcessHeap(), 0, list->items, capacity * sizeof(WCHAR *))
                                    : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(WCHAR *));
        if (!grown)
            return FALSE;
        list->items = grown;
        list->capacity = capacity;
    }
    list->items[list->count++] = name;
    return TRUE;
}

/* fails unless the value is a DWORD. */
static BOOL get_dword(HKEY key, const WCHAR *name, DWORD *out)
{
    DWORD type = 0, data = 0, size = sizeof(data);
    if (RegQueryValueExW(key, name, nullptr, &type, (BYTE *)&data, &size) != ERROR_SUCCESS || type != REG_DWORD)
        return FALSE;
    *out = data;
    return TRUE;
}

/* the multi string value named after the group. */
static void names_from_value(HKEY svchost, const WCHAR *group, NAME_LIST *list)
{
    DWORD type = 0, size = 0;
    if (RegQueryValueExW(svchost, group, nullptr, &type, nullptr, &size) != ERROR_SUCCESS || type != REG_MULTI_SZ ||
        !size)
        return;
    WCHAR *data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size + 2 * sizeof(WCHAR));
    if (!data)
        return;
    if (RegQueryValueExW(svchost, group, nullptr, &type, (BYTE *)data, &size) != ERROR_SUCCESS || type != REG_MULTI_SZ) {
        HeapFree(GetProcessHeap(), 0, data);
        return;
    }
    for (WCHAR *s = data; *s; s += wcslen(s) + 1)
        add_name(list, s);
}

static void names_from_subkeys(HKEY group_key, NAME_LIST *list)
{
    for (DWORD index = 0;; index++) {
        WCHAR name[MAX_SERVICE_NAME + 1];
        DWORD chars = RTL_NUMBER_OF(name);
        if (RegEnumKeyExW(group_key, index, name, &chars, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            break;
        WCHAR *copy = HostDup(name);
        if (copy)
            add_name(list, copy);
    }
}

static BOOL mitigation_switch_on(void)
{
    HKEY key;
    DWORD on = 0;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"System\\CurrentControlSet\\Control\\SCMConfig", 0, KEY_READ, &key) !=
        ERROR_SUCCESS)
        return FALSE;
    get_dword(key, L"EnableSvchostMitigationPolicy", &on);
    RegCloseKey(key);
    return on != 0;
}

/* the group's value, else the process default. */
static DWORD policy_value(HKEY group, const WCHAR *name, DWORD fallback)
{
    DWORD value;
    return get_dword(group, name, &value) ? value : fallback;
}

static void read_settings(HKEY group, const HOST_OPTIONS *o, GROUP_SETTINGS *g)
{
    static const struct {
        const WCHAR *name;
        DWORD bit;
    } flag_values[] = {
        {L"CoInitializeSecurityAllowLowBox", GROUP_FLAG_ALLOW_LOWBOX},
        {L"NoGuiAccess", GROUP_FLAG_NO_GUI},
        {L"CoInitializeSecurityAllowInteractiveUsers", GROUP_FLAG_ALLOW_INTERACTIVE},
        {L"CoInitializeSecurityAllowComCapability", GROUP_FLAG_ALLOW_COM_CAPABILITY},
        {L"CoInitializeSecurityAllowCrossContainer", GROUP_FLAG_ALLOW_CROSS_CONTAINER},
    };
    DWORD value = 0;
    g->init_com_security = get_dword(group, L"CoInitializeSecurityParam", &value) && value;
    g->authn_level = policy_value(group, L"AuthenticationLevel", 4);
    g->impersonation_level = policy_value(group, L"ImpersonationLevel", 2);
    g->capabilities = policy_value(group, L"AuthenticationCapabilities", 0x3000);
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(flag_values); i++)
        if (get_dword(group, flag_values[i].name, &value) && value)
            g->flags |= flag_values[i].bit;

    DWORD type = 0, size = 0;
    if (RegQueryValueExW(group, L"COMAccessPermissionsSD", nullptr, &type, nullptr, &size) == ERROR_SUCCESS &&
        type == REG_BINARY && size) {
        void *sd = HeapAlloc(GetProcessHeap(), 0, size);
        if (sd && RegQueryValueExW(group, L"COMAccessPermissionsSD", nullptr, &type, sd, &size) == ERROR_SUCCESS &&
            IsValidSecurityDescriptor(sd))
            g->access_descriptor = sd;
        else if (sd)
            HeapFree(GetProcessHeap(), 0, sd);
    }

    DWORD asked = o->Mitigation && mitigation_switch_on() ? 1 : 0;
    g->dynamic_code = policy_value(group, L"DynamicCodePolicy", asked);
    g->signature = policy_value(group, L"BinarySignaturePolicy", asked);
    g->extension_points = policy_value(group, L"ExtensionPointsPolicy", asked);
    g->redirection_trust = policy_value(group, L"RedirectionTrustPolicy", 2);
}

/* services come from the group's value and subkeys, plus the `-s` one when given. */
static BOOL load_group(const HOST_OPTIONS *o, NAME_LIST *list, GROUP_SETTINGS *settings)
{
    HKEY svchost, group;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, SVCHOST_KEY, 0, KEY_READ, &svchost) != ERROR_SUCCESS)
        return FALSE;
    names_from_value(svchost, o->Group, list);
    if (RegOpenKeyExW(svchost, o->Group, 0, KEY_READ, &group) == ERROR_SUCCESS) {
        names_from_subkeys(group, list);
        read_settings(group, o, settings);
        RegCloseKey(group);
    }
    RegCloseKey(svchost);

    if (o->Service) {
        DWORD kept = 0;
        for (DWORD i = 0; i < list->count; i++)
            if (CompareStringOrdinal(list->items[i], -1, o->Service, -1, TRUE) == CSTR_EQUAL)
                list->items[kept++] = list->items[i];
        list->count = kept;
    }
    return list->count != 0;
}

/* ------------------------------------------------------------------ COM and mitigation policies */

typedef HRESULT(WINAPI *CO_INITIALIZE_EX)(void *reserved, DWORD model);
typedef void(WINAPI *CO_UNINITIALIZE)(void);
typedef HRESULT(WINAPI *CO_INITIALIZE_SECURITY)(PSECURITY_DESCRIPTOR sd, LONG count, void *services, void *reserved1,
                                                DWORD authn_level, DWORD impersonation, void *auth_list,
                                                DWORD capabilities, void *reserved3);
typedef BOOL(WINAPI *SET_MITIGATION_POLICY)(int policy, void *buffer, SIZE_T length);

static CO_UNINITIALIZE g_co_uninitialize; /* set once this process has initialized COM. */

/* used when the registry gives none: system, administrators and service accounts, plus what the flags allow. */
static PSECURITY_DESCRIPTOR default_access_descriptor(DWORD flags)
{
    WCHAR text[160] = L"O:SYG:SYD:(A;;0x3;;;SY)(A;;0x3;;;BA)(A;;0x3;;;LS)(A;;0x3;;;NS)";
    if (flags & GROUP_FLAG_ALLOW_INTERACTIVE)
        wcscat(text, L"(A;;0x3;;;IU)");
    if (flags & GROUP_FLAG_ALLOW_LOWBOX)
        wcscat(text, L"(A;;0x3;;;AC)");
    PSECURITY_DESCRIPTOR sd = nullptr;
    return ConvertStringSecurityDescriptorToSecurityDescriptorW(text, 1, &sd, nullptr) ? sd : nullptr;
}

static void start_com(const GROUP_SETTINGS *g)
{
    HMODULE ole = LoadLibraryW(L"ole32.dll");
    CO_INITIALIZE_EX initialize = ole ? (CO_INITIALIZE_EX)GetProcAddress(ole, "CoInitializeEx") : nullptr;
    CO_INITIALIZE_SECURITY secure = ole ? (CO_INITIALIZE_SECURITY)GetProcAddress(ole, "CoInitializeSecurity") : nullptr;
    CO_UNINITIALIZE uninitialize = ole ? (CO_UNINITIALIZE)GetProcAddress(ole, "CoUninitialize") : nullptr;
    if (!initialize || !secure || !uninitialize) {
        HostLog("svchost: ole32 cannot start COM");
        return;
    }
    if (initialize(nullptr, 0) >= 0)
        g_co_uninitialize = uninitialize;
    PSECURITY_DESCRIPTOR sd = g->access_descriptor ? g->access_descriptor : default_access_descriptor(g->flags);
    HRESULT hr = secure(sd, -1, nullptr, nullptr, g->authn_level, g->impersonation_level, nullptr, g->capabilities,
                        nullptr);
    if (hr < 0)
        HostLog("svchost: COM security failed, %08lx", (unsigned long)hr);
}

static void apply_mitigations(const GROUP_SETTINGS *g)
{
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    SET_MITIGATION_POLICY set = kernel ? (SET_MITIGATION_POLICY)GetProcAddress(kernel, "SetProcessMitigationPolicy")
                                       : nullptr;
    if (!set)
        return;
    const struct {
        int policy;
        DWORD flags;
    } wanted[] = {
        {POLICY_DYNAMIC_CODE, g->dynamic_code},
        {POLICY_SIGNATURE, g->signature},
        {POLICY_EXTENSION_POINTS, g->extension_points},
        {POLICY_REDIRECTION_TRUST, g->redirection_trust},
    };
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(wanted); i++)
        if (wanted[i].flags && !set(wanted[i].policy, (void *)&wanted[i].flags, sizeof(DWORD)))
            HostLog("svchost: policy %d refused, error %lu", wanted[i].policy, GetLastError());
}

/* ------------------------------------------------------------------ process */

void entry(void)
{
    HOST_OPTIONS options = {};
    NAME_LIST names = {};
    GROUP_SETTINGS settings = {};
    if (!parse_command(GetCommandLineW(), &options) || !load_group(&options, &names, &settings)) {
        HostLog("svchost: no services to host");
        ExitProcess(0);
    }

    g_services = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, names.count * sizeof(*g_services));
    SERVICE_TABLE_ENTRYW *table = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (names.count + 1) * sizeof(*table));
    if (!g_services || !table)
        ExitProcess(0);
    g_service_count = names.count;
    for (DWORD i = 0; i < names.count; i++) {
        g_services[i].name = names.items[i];
        table[i].lpServiceName = names.items[i];
        table[i].lpServiceProc = HostService;
    }

    apply_mitigations(&settings);
    if (settings.init_com_security)
        start_com(&settings);
    if (!StartServiceCtrlDispatcherW(table))
        HostLog("svchost: the dispatcher failed, error %lu", GetLastError());

    if (g_wait_for_unloads) {
        HostWaitForUnloads();
        if (g_co_uninitialize)
            g_co_uninitialize();
    }
    ExitProcess(0);
}
