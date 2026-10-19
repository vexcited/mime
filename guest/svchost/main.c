/*
 * svchost.exe: the command line, the group in the registry and the table the service dispatcher is given.
 */
#include "svchost.h"
#include <stdarg.h>

#define SVCHOST_KEY L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Svchost"

HOSTED *g_hosted;
DWORD g_hosted_count;
/* COM is shut down at the end unless a service asked for the old behavior. */
BOOL g_com_uninit = TRUE;
static BOOL g_com_started;

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

/* ------------------------------------------------------------------ the command line */

static BOOL is_blank(WCHAR c)
{
    return c == L' ' || c == L'\t';
}

/*
 * `<program> [-k <group>] [-s <service>] [-p]`, with - or / before each option and no case (read: BuildCommandOptions).
 * the program name ends at the first blank; the group and the service may be quoted. the options are taken
 * from the copy of the line, which is cut into the words.
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

/* ------------------------------------------------------------------ the group */

static BOOL read_dword(HKEY key, const WCHAR *name, DWORD *out)
{
    DWORD type = 0, size = sizeof(DWORD), value = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, (BYTE *)&value, &size) != ERROR_SUCCESS || type != REG_DWORD)
        return FALSE;
    *out = value;
    return TRUE;
}

/* the multi sz value of a group, in a block of its own. */
static WCHAR *read_multi(HKEY key, const WCHAR *name, DWORD *chars)
{
    DWORD type = 0, bytes = 0;
    *chars = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS || type != REG_MULTI_SZ ||
        bytes < sizeof(WCHAR))
        return nullptr;
    WCHAR *text = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes + 2 * sizeof(WCHAR));
    if (!text)
        return nullptr;
    if (RegQueryValueExW(key, name, nullptr, &type, (BYTE *)text, &bytes) != ERROR_SUCCESS) {
        HeapFree(GetProcessHeap(), 0, text);
        return nullptr;
    }
    *chars = bytes / sizeof(WCHAR);
    return text;
}

/* adds a name to a multi sz that is being built; false when it is there already. */
static BOOL add_name(WCHAR **names, DWORD *chars, const WCHAR *name)
{
    for (const WCHAR *n = *names; n && *n; n += wcslen(n) + 1)
        if (CompareStringOrdinal(n, -1, name, -1, TRUE) == 2)
            return TRUE;
    DWORD add = (DWORD)wcslen(name) + 1;
    WCHAR *grown = *names ? HeapReAlloc(GetProcessHeap(), 0, *names, (*chars + add + 1) * sizeof(WCHAR))
                          : HeapAlloc(GetProcessHeap(), 0, (add + 1) * sizeof(WCHAR));
    if (!grown)
        return FALSE;
    memcpy(grown + *chars, name, add * sizeof(WCHAR));
    *chars += add;
    grown[*chars] = 0;
    *names = grown;
    return TRUE;
}

/*
 * the services of a group are the strings of the value of its name in the Svchost key and the names of the
 * keys below the key of that name; the key also holds how the process is to be set up (read: ReadPerInstanceRegistryParameters).
 */
static BOOL read_group(HOST_OPTIONS *o)
{
    HKEY root, key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, SVCHOST_KEY, 0, KEY_READ, &root) != ERROR_SUCCESS)
        return FALSE;
    DWORD listed;
    WCHAR *names = read_multi(root, o->Group, &listed);
    DWORD chars = names ? listed - 1 : 0;
    /* a list is as long as its text, without the last terminator. */
    if (names && chars && names[chars - 1] == 0)
        chars--;
    if (names)
        names[chars] = 0;
    if (names && chars)
        chars++;
    if (RegOpenKeyExW(root, o->Group, 0, KEY_READ, &key) == ERROR_SUCCESS) {
        for (DWORD i = 0;; i++) {
            WCHAR name[257];
            DWORD n = RTL_NUMBER_OF(name);
            if (RegEnumKeyExW(key, i, name, &n, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                break;
            if (!add_name(&names, &chars, name))
                break;
        }
    }
    RegCloseKey(root);
    if (!names || !names[0]) {
        if (key)
            RegCloseKey(key);
        HeapFree(GetProcessHeap(), 0, names);
        return FALSE;
    }
    o->Names = names;
    o->AuthenticationLevel = 4;
    o->ImpersonationLevel = 2;
    o->AuthenticationCapabilities = 0x3000;
    DWORD value;
    if (key) {
        o->SecurityParam = read_dword(key, L"CoInitializeSecurityParam", &value) && value;
        if (read_dword(key, L"CoInitializeSecurityAllowLowBox", &value) && value)
            o->Flags |= HOST_ALLOW_LOWBOX;
        if (read_dword(key, L"CoInitializeSecurityAllowInteractiveUsers", &value) && value)
            o->Flags |= HOST_ALLOW_INTERACTIVE;
        if (read_dword(key, L"CoInitializeSecurityAllowComCapability", &value) && value)
            o->Flags |= HOST_ALLOW_COM_CAPABILITY;
        if (read_dword(key, L"CoInitializeSecurityAllowCrossContainer", &value) && value)
            o->Flags |= HOST_ALLOW_CROSS_CONTAINER;
        if (read_dword(key, L"AuthenticationLevel", &value))
            o->AuthenticationLevel = value;
        if (read_dword(key, L"ImpersonationLevel", &value))
            o->ImpersonationLevel = value;
        if (read_dword(key, L"AuthenticationCapabilities", &value))
            o->AuthenticationCapabilities = value;
        /* the mitigations: a value of the group wins, else the defaults of -p when the machine asks for them. */
        BOOL by_default = FALSE;
        HKEY config;
        if (o->Mitigation && RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"System\\CurrentControlSet\\Control\\SCMConfig", 0,
                                           KEY_READ, &config) == ERROR_SUCCESS) {
            by_default = read_dword(config, L"EnableSvchostMitigationPolicy", &value) && value;
            RegCloseKey(config);
        }
        o->DynamicCode = read_dword(key, L"DynamicCodePolicy", &value) ? value : by_default;
        o->BinarySignature = read_dword(key, L"BinarySignaturePolicy", &value) ? value : by_default;
        o->RedirectionTrust = read_dword(key, L"RedirectionTrustPolicy", &value) ? value : 2;
        o->ExtensionPoints = read_dword(key, L"ExtensionPointsPolicy", &value) ? value : by_default;
        DWORD type = 0, bytes = 0;
        if (RegQueryValueExW(key, L"COMAccessPermissionsSD", nullptr, &type, nullptr, &bytes) == ERROR_SUCCESS &&
            type == REG_BINARY && bytes) {
            void *sd = HeapAlloc(GetProcessHeap(), 0, bytes);
            if (sd && RegQueryValueExW(key, L"COMAccessPermissionsSD", nullptr, &type, sd, &bytes) == ERROR_SUCCESS)
                o->AccessSd = sd;
            else
                HeapFree(GetProcessHeap(), 0, sd);
        }
        RegCloseKey(key);
    }
    return TRUE;
}

/* ------------------------------------------------------------------ the process */

typedef BOOL(WINAPI *SET_MITIGATION)(int policy, void *buffer, SIZE_T length);

static void apply_mitigations(const HOST_OPTIONS *o)
{
    DWORD flags;
    if (o->DynamicCode) {
        flags = o->DynamicCode;
        SetProcessMitigationPolicy(ProcessDynamicCodePolicy, &flags, sizeof(flags));
    }
    if (o->BinarySignature) {
        flags = o->BinarySignature;
        SetProcessMitigationPolicy(ProcessSignaturePolicy, &flags, sizeof(flags));
    }
    if (o->RedirectionTrust) {
        flags = o->RedirectionTrust;
        SetProcessMitigationPolicy(ProcessRedirectionTrustPolicy, &flags, sizeof(flags));
    }
    if (o->ExtensionPoints) {
        flags = 1;
        SetProcessMitigationPolicy(ProcessExtensionPointDisablePolicy, &flags, sizeof(flags));
    }
}

/* the security of the process' com, for the groups whose services serve com objects: a guess at the access list. */
static void initialize_security(const HOST_OPTIONS *o)
{
    typedef HRESULT(WINAPI * CO_INITIALIZE_EX)(void *reserved, DWORD model);
    typedef HRESULT(WINAPI * CO_INITIALIZE_SECURITY)(PSECURITY_DESCRIPTOR sd, LONG count, void *names, void *reserved1,
                                                     DWORD level, DWORD impersonation, void *auth, DWORD capabilities,
                                                     void *reserved3);
    HMODULE ole = LoadLibraryW(L"ole32.dll");
    CO_INITIALIZE_EX initialize = ole ? (CO_INITIALIZE_EX)GetProcAddress(ole, "CoInitializeEx") : nullptr;
    CO_INITIALIZE_SECURITY secure = ole ? (CO_INITIALIZE_SECURITY)GetProcAddress(ole, "CoInitializeSecurity") : nullptr;
    if (!initialize || !secure)
        return;
    PSECURITY_DESCRIPTOR sd = o->AccessSd;
    PSECURITY_DESCRIPTOR built = nullptr;
    if (!sd) {
        WCHAR text[160] = L"O:SYG:SYD:(A;;0xb;;;SY)(A;;0xb;;;BA)";
        if (o->Flags & HOST_ALLOW_LOWBOX)
            wcscat(text, L"(A;;0x3;;;AC)");
        if (o->Flags & HOST_ALLOW_INTERACTIVE)
            wcscat(text, L"(A;;0x3;;;IU)");
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(text, 1, &built, nullptr))
            return;
        sd = built;
    }
    HRESULT result = initialize(nullptr, 0 /* COINIT_MULTITHREADED */);
    if (result >= 0) {
        g_com_started = TRUE;
        result = secure(sd, -1, nullptr, nullptr, o->AuthenticationLevel, o->ImpersonationLevel, nullptr,
                        o->AuthenticationCapabilities, nullptr);
    }
    if (result < 0)
        HostLog("svchost: the security of com is not set: %08lx", (unsigned long)result);
    if (built)
        LocalFree(built);
}

static SERVICE_TABLE_ENTRYW *build_table(const HOST_OPTIONS *o)
{
    DWORD count = 0;
    for (const WCHAR *n = o->Names; *n; n += wcslen(n) + 1)
        count++;
    SERVICE_TABLE_ENTRYW *table = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (count + 1) * sizeof(*table));
    g_hosted = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (count ? count : 1) * sizeof(*g_hosted));
    if (!table || !g_hosted)
        return nullptr;
    for (const WCHAR *n = o->Names; *n; n += wcslen(n) + 1) {
        /* a process of one service of its group takes that one alone. */
        if (o->Service && CompareStringOrdinal(n, -1, o->Service, -1, TRUE) != 2)
            continue;
        HOSTED *s = &g_hosted[g_hosted_count];
        s->Name = (WCHAR *)n;
        InitializeSRWLock(&s->Lock);
        InitializeConditionVariable(&s->Unloaded);
        table[g_hosted_count].lpServiceName = s->Name;
        table[g_hosted_count].lpServiceProc = HostService;
        g_hosted_count++;
    }
    return g_hosted_count ? table : nullptr;
}

void entry(void)
{
    HOST_OPTIONS o = {};
    SetErrorMode(SEM_FAILCRITICALERRORS);
    RegDisablePredefinedCacheEx();
    if (!parse_command(GetCommandLineW(), &o)) {
        HostLog("svchost: no group on the command line");
        ExitProcess(0);
    }
    if (!read_group(&o)) {
        HostLog("svchost: no services in the group %ls", o.Group);
        ExitProcess(0);
    }
    SERVICE_TABLE_ENTRYW *table = build_table(&o);
    if (!table) {
        HostLog("svchost: the group %ls has no service to host", o.Group);
        ExitProcess(0);
    }
    HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);
    if (o.SecurityParam)
        initialize_security(&o);
    apply_mitigations(&o);
    StartServiceCtrlDispatcherW(table);
    if (g_com_uninit) {
        HostWaitForUnloads();
        if (g_com_started) {
            HMODULE ole = GetModuleHandleW(L"ole32.dll");
            void(WINAPI * uninitialize)(void) = ole ? (void(WINAPI *)(void))GetProcAddress(ole, "CoUninitialize") : nullptr;
            if (uninitialize)
                uninitialize();
        }
    }
    ExitProcess(0);
}
