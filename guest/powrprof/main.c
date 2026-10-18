/*
 * powrprof: the power information windows' kernel answers, as a desktop
 * machine on mains power without a battery, sleep states or hibernation
 * file reports it, and the balanced scheme the power service would hold.
 */
#define POWRPROFAPI
#include <mime/win32/powrprof.h>
#include <mime/win32/objbase.h>

#define RPC_S_SERVER_UNAVAILABLE 1722L
#define BATTERY_UNKNOWN_RATE 0x80000000u
#define BATTERY_UNKNOWN_TIME 0xFFFFFFFFu
#define PO_TZ_INVALID_MODE 3
#define SETTING_MAGIC 0x554D4F4F
#define SUSPEND_MAGIC 0x6552534D

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

/* ------------------------------------------------------------------ power information */

static ULONG processor_mhz(void)
{
    HKEY key;
    DWORD mhz = 0, size = sizeof(mhz);
    if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ,
                       &key)) {
        RegQueryValueExW(key, L"~MHz", nullptr, nullptr, (BYTE *)&mhz, &size);
        RegCloseKey(key);
    }
    return mhz;
}

static void capabilities(SYSTEM_POWER_CAPABILITIES *caps)
{
    memset(caps, 0, sizeof(*caps));
    caps->PowerButtonPresent = TRUE;
    caps->SystemS5 = TRUE;
    caps->FullWake = TRUE;
    caps->ProcessorMinThrottle = 100;
    caps->ProcessorMaxThrottle = 100;
    caps->AcOnLineWake = PowerSystemUnspecified;
    caps->SoftLidWake = PowerSystemUnspecified;
    caps->RtcWake = PowerSystemUnspecified;
    caps->MinDeviceWakeState = PowerSystemUnspecified;
    caps->DefaultLowLatencyWake = PowerSystemUnspecified;
}

/* what NtPowerInformation answers for the levels programs read. */
static NTSTATUS power_information(POWER_INFORMATION_LEVEL level, PVOID input, ULONG input_size, PVOID output,
                                  ULONG output_size)
{
    switch (level) {
    case SystemPowerCapabilities:
        if (output_size < sizeof(SYSTEM_POWER_CAPABILITIES))
            return STATUS_BUFFER_TOO_SMALL;
        capabilities(output);
        return STATUS_SUCCESS;
    case SystemBatteryState: {
        if (output_size < sizeof(SYSTEM_BATTERY_STATE))
            return STATUS_BUFFER_TOO_SMALL;
        SYSTEM_BATTERY_STATE *state = output;
        memset(state, 0, sizeof(*state));
        state->AcOnLine = TRUE;
        state->Rate = BATTERY_UNKNOWN_RATE;
        state->EstimatedTime = BATTERY_UNKNOWN_TIME;
        return STATUS_SUCCESS;
    }
    case ProcessorInformation: {
        SYSTEM_INFO info;
        GetSystemInfo(&info);
        ULONG count = info.dwNumberOfProcessors;
        if (output_size < count * sizeof(PROCESSOR_POWER_INFORMATION))
            return STATUS_BUFFER_TOO_SMALL;
        ULONG mhz = processor_mhz();
        PROCESSOR_POWER_INFORMATION *cpu = output;
        for (ULONG i = 0; i < count; i++) {
            cpu[i].Number = i;
            cpu[i].MaxMhz = mhz;
            cpu[i].CurrentMhz = mhz;
            cpu[i].MhzLimit = mhz;
            cpu[i].MaxIdleState = 0;
            cpu[i].CurrentIdleState = 0;
        }
        return STATUS_SUCCESS;
    }
    case SystemPowerInformation: {
        if (output_size < sizeof(SYSTEM_POWER_INFORMATION))
            return STATUS_BUFFER_TOO_SMALL;
        SYSTEM_POWER_INFORMATION *power = output;
        memset(power, 0, sizeof(*power));
        power->CoolingMode = PO_TZ_INVALID_MODE;
        return STATUS_SUCCESS;
    }
    case LastWakeTime:
    case LastSleepTime:
        if (output_size < sizeof(ULONGLONG))
            return STATUS_BUFFER_TOO_SMALL;
        *(ULONGLONG *)output = 0;
        return STATUS_SUCCESS;
    case SystemExecutionState:
        if (output_size < sizeof(ULONG))
            return STATUS_BUFFER_TOO_SMALL;
        *(ULONG *)output = 0;
        return STATUS_SUCCESS;
    case PlatformRole:
        if (output_size < sizeof(ULONG))
            return STATUS_BUFFER_TOO_SMALL;
        *(ULONG *)output = PlatformRoleDesktop;
        return STATUS_SUCCESS;
    case PlatformInformation:
        if (output_size < sizeof(BOOLEAN))
            return STATUS_BUFFER_TOO_SMALL;
        *(BOOLEAN *)output = FALSE;
        return STATUS_SUCCESS;
    case SystemReserveHiberFile:
        return STATUS_PRIVILEGE_NOT_HELD;
    default:
        return STATUS_INVALID_PARAMETER;
    }
}

LONG WINAPI CallNtPowerInformation(POWER_INFORMATION_LEVEL level, PVOID input, ULONG input_size, PVOID output,
                                   ULONG output_size)
{
    if (!output && output_size)
        return STATUS_INVALID_PARAMETER;
    return power_information(level, input, input_size, output, output_size);
}

LONG WINAPI PowerInformationWithPrivileges(POWER_INFORMATION_LEVEL level, PVOID input, ULONG input_size,
                                           PVOID output, ULONG output_size)
{
    return CallNtPowerInformation(level, input, input_size, output, output_size);
}

BOOLEAN WINAPI GetPwrCapabilities(PSYSTEM_POWER_CAPABILITIES caps)
{
    if (!caps) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    capabilities(caps);
    return TRUE;
}

BOOLEAN WINAPI IsPwrHibernateAllowed(void)
{
    SYSTEM_POWER_CAPABILITIES caps;
    return GetPwrCapabilities(&caps) && caps.SystemS4 && caps.HiberFilePresent;
}

BOOLEAN WINAPI IsPwrSuspendAllowed(void)
{
    SYSTEM_POWER_CAPABILITIES caps;
    return GetPwrCapabilities(&caps) && (caps.SystemS1 || caps.SystemS2 || caps.SystemS3);
}

BOOLEAN WINAPI IsPwrShutdownAllowed(void)
{
    SYSTEM_POWER_CAPABILITIES caps;
    return GetPwrCapabilities(&caps) && caps.SystemS5;
}

/* no sleep state to enter. */
BOOLEAN WINAPI SetSuspendState(BOOLEAN hibernate, BOOLEAN force, BOOLEAN wake_events_disabled)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

/* a slate counts as mobile to callers of the first version. */
POWER_PLATFORM_ROLE WINAPI PowerDeterminePlatformRoleEx(ULONG version)
{
    ULONG role = PlatformRoleUnspecified;
    if (power_information(PlatformRole, nullptr, 0, &role, sizeof(role)) < 0)
        role = PlatformRoleDesktop;
    if (version < 2 && role == PlatformRoleSlate)
        return PlatformRoleMobile;
    return (POWER_PLATFORM_ROLE)role;
}

POWER_PLATFORM_ROLE WINAPI PowerDeterminePlatformRole(void)
{
    return PowerDeterminePlatformRoleEx(1);
}

/* ------------------------------------------------------------------ schemes and settings */

static const GUID g_balanced = {0x381b4222, 0xf694, 0x41f0, {0x96, 0x85, 0xff, 0x5b, 0xb2, 0x60, 0xdf, 0x2e}};
static const GUID g_high = {0x8c5e7fda, 0xe8bf, 0x4a96, {0x9a, 0x85, 0xa6, 0xe2, 0x3a, 0x8c, 0x63, 0x5c}};
static const GUID g_saver = {0xa1841308, 0x3541, 0x4fab, {0xbc, 0x81, 0xf7, 0x15, 0x56, 0xf2, 0x0b, 0x4a}};

static const GUID g_sub_disk = {0x0012ee47, 0x9041, 0x4b5d, {0x9b, 0x77, 0x53, 0x5f, 0xba, 0x8b, 0x14, 0x42}};
static const GUID g_sub_sleep = {0x238c9fa8, 0x0aad, 0x41ed, {0x83, 0xf4, 0x97, 0xbe, 0x24, 0x2c, 0x8f, 0x20}};
static const GUID g_sub_video = {0x7516b95f, 0xf776, 0x4464, {0x8c, 0x53, 0x06, 0x16, 0x7f, 0x40, 0xcc, 0x99}};
static const GUID g_sub_processor = {0x54533251, 0x82be, 0x4824, {0x96, 0xc1, 0x47, 0xb6, 0x0b, 0x74, 0x0d, 0x00}};
static const GUID g_sub_buttons = {0x4f971e89, 0xeebd, 0x4455, {0xa8, 0xde, 0x9e, 0x59, 0x04, 0x0e, 0x73, 0x47}};

static const struct {
    const GUID *subgroup;
    GUID setting;
    DWORD ac, dc;
} g_settings[] = {
    /* DISKIDLE */
    {&g_sub_disk, {0x6738e2c4, 0xe8a5, 0x4a42, {0xb1, 0x6a, 0xe0, 0x40, 0xe7, 0x69, 0x75, 0x6e}}, 1200, 600},
    /* STANDBYIDLE */
    {&g_sub_sleep, {0x29f6c1db, 0x86da, 0x48c5, {0x9f, 0xdb, 0xf2, 0xb6, 0x7b, 0x1f, 0x44, 0xda}}, 1800, 900},
    /* HIBERNATEIDLE */
    {&g_sub_sleep, {0x9d7815a6, 0x7ee4, 0x497e, {0x88, 0x88, 0x51, 0x5a, 0x05, 0xf0, 0x23, 0x64}}, 0, 0},
    /* VIDEOIDLE */
    {&g_sub_video, {0x3c0bc021, 0xc8a8, 0x4e07, {0xa9, 0x73, 0x6b, 0x14, 0xcb, 0xcb, 0x2b, 0x7e}}, 600, 300},
    /* PROCTHROTTLEMIN */
    {&g_sub_processor, {0x893dee8e, 0x2bef, 0x41e0, {0x89, 0xc6, 0xb5, 0x5d, 0x09, 0x29, 0x96, 0x4c}}, 5, 5},
    /* PROCTHROTTLEMAX */
    {&g_sub_processor, {0xbc5038f7, 0x23e0, 0x4960, {0x96, 0xda, 0x33, 0xab, 0xaf, 0x59, 0x35, 0xec}}, 100, 100},
    /* SYSCOOLPOL */
    {&g_sub_processor, {0x94d3a615, 0xa899, 0x4ac5, {0xae, 0x2b, 0xe4, 0xd8, 0xf6, 0x34, 0x36, 0x7f}}, 1, 0},
    /* LIDACTION */
    {&g_sub_buttons, {0x5ca83367, 0x6e45, 0x459f, {0xa2, 0x7b, 0x47, 0x6b, 0x1d, 0x01, 0xc9, 0x36}}, 1, 1},
    /* PBUTTONACTION */
    {&g_sub_buttons, {0x7648efa3, 0xdd9c, 0x4e3e, {0xb5, 0x66, 0x50, 0xf9, 0x29, 0x38, 0x62, 0x80}}, 1, 1},
};

static BOOL known_scheme(const GUID *scheme)
{
    return !scheme || IsEqualGUID(scheme, &g_balanced) || IsEqualGUID(scheme, &g_high) ||
           IsEqualGUID(scheme, &g_saver);
}

DWORD WINAPI PowerGetActiveScheme(HKEY root, GUID **scheme)
{
    *scheme = nullptr;
    GUID *copy = LocalAlloc(LMEM_ZEROINIT, sizeof(GUID));
    if (!copy)
        return ERROR_NOT_ENOUGH_MEMORY;
    *copy = g_balanced;
    *scheme = copy;
    return ERROR_SUCCESS;
}

static DWORD find_setting(const GUID *scheme, const GUID *subgroup, const GUID *setting, BOOL ac, DWORD *value)
{
    if (!known_scheme(scheme) || !setting)
        return ERROR_FILE_NOT_FOUND;
    for (UINT i = 0; i < sizeof(g_settings) / sizeof(g_settings[0]); i++) {
        if (IsEqualGUID(&g_settings[i].setting, setting) &&
            (!subgroup || IsEqualGUID(g_settings[i].subgroup, subgroup))) {
            *value = ac ? g_settings[i].ac : g_settings[i].dc;
            return ERROR_SUCCESS;
        }
    }
    return ERROR_FILE_NOT_FOUND;
}

static DWORD read_value(const GUID *scheme, const GUID *subgroup, const GUID *setting, BOOL ac, PULONG type,
                        BYTE *buffer, DWORD *size)
{
    if (!size)
        return ERROR_INVALID_PARAMETER;
    DWORD value;
    DWORD error = find_setting(scheme, subgroup, setting, ac, &value);
    if (error)
        return error;
    if (type)
        *type = REG_DWORD;
    if (buffer && *size < sizeof(DWORD)) {
        *size = sizeof(DWORD);
        return ERROR_MORE_DATA;
    }
    *size = sizeof(DWORD);
    if (buffer)
        memcpy(buffer, &value, sizeof(DWORD));
    return ERROR_SUCCESS;
}

DWORD WINAPI PowerReadACValue(HKEY root, const GUID *scheme, const GUID *subgroup, const GUID *setting, PULONG type,
                              LPBYTE buffer, LPDWORD size)
{
    return read_value(scheme, subgroup, setting, TRUE, type, buffer, size);
}

DWORD WINAPI PowerReadDCValue(HKEY root, const GUID *scheme, const GUID *subgroup, const GUID *setting, PULONG type,
                              PUCHAR buffer, LPDWORD size)
{
    return read_value(scheme, subgroup, setting, FALSE, type, buffer, size);
}

static DWORD read_index(const GUID *scheme, const GUID *subgroup, const GUID *setting, BOOL ac, DWORD *index)
{
    if (!index)
        return ERROR_INVALID_PARAMETER;
    return find_setting(scheme, subgroup, setting, ac, index);
}

DWORD WINAPI PowerReadACValueIndex(HKEY root, const GUID *scheme, const GUID *subgroup, const GUID *setting,
                                   LPDWORD index)
{
    return read_index(scheme, subgroup, setting, TRUE, index);
}

DWORD WINAPI PowerReadDCValueIndex(HKEY root, const GUID *scheme, const GUID *subgroup, const GUID *setting,
                                   LPDWORD index)
{
    return read_index(scheme, subgroup, setting, FALSE, index);
}

DWORD WINAPI PowerReadACDefaultIndex(HKEY root, const GUID *scheme, const GUID *subgroup, const GUID *setting,
                                     LPDWORD index)
{
    return read_index(scheme, subgroup, setting, TRUE, index);
}

DWORD WINAPI PowerReadDCDefaultIndex(HKEY root, const GUID *scheme, const GUID *subgroup, const GUID *setting,
                                     LPDWORD index)
{
    return read_index(scheme, subgroup, setting, FALSE, index);
}

/* ------------------------------------------------------------------ notifications */

/* nothing ever changes, so a registration only has to be a handle the unregistering accepts. */
typedef struct {
    DWORD magic;
    DWORD flags;
    GUID setting;
    DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS subscriber;
} REGISTRATION;

DWORD WINAPI PowerSettingRegisterNotificationEx(const GUID *setting, DWORD reserved, DWORD flags, HANDLE recipient,
                                                PHPOWERNOTIFY handle)
{
    if (!handle)
        return ERROR_INVALID_PARAMETER;
    if (flags == DEVICE_NOTIFY_SERVICE_HANDLE)
        return ERROR_INVALID_HANDLE;
    if (flags != DEVICE_NOTIFY_CALLBACK || !recipient || !setting)
        return ERROR_INVALID_PARAMETER;
    REGISTRATION *registration = LocalAlloc(LMEM_ZEROINIT, sizeof(*registration));
    if (!registration)
        return ERROR_NOT_ENOUGH_MEMORY;
    registration->magic = SETTING_MAGIC;
    registration->flags = flags;
    registration->setting = *setting;
    registration->subscriber = *(DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS *)recipient;
    *handle = registration;
    return ERROR_SUCCESS;
}

DWORD WINAPI PowerSettingRegisterNotification(const GUID *setting, DWORD flags, HANDLE recipient,
                                              PHPOWERNOTIFY handle)
{
    return PowerSettingRegisterNotificationEx(setting, ~0u, flags, recipient, handle);
}

DWORD WINAPI PowerSettingUnregisterNotification(HPOWERNOTIFY handle)
{
    REGISTRATION *registration = handle;
    if (!registration || registration == INVALID_HANDLE_VALUE || registration->magic != SETTING_MAGIC)
        return ERROR_INVALID_PARAMETER;
    registration->magic = 0;
    LocalFree(registration);
    return ERROR_SUCCESS;
}

DWORD WINAPI PowerRegisterSuspendResumeNotification(DWORD flags, HANDLE recipient, PHPOWERNOTIFY handle)
{
    if (flags != DEVICE_NOTIFY_CALLBACK)
        return ERROR_INVALID_PARAMETER;
    REGISTRATION *registration = LocalAlloc(LMEM_ZEROINIT, sizeof(*registration));
    if (!registration)
        return ERROR_NOT_ENOUGH_MEMORY;
    registration->magic = SUSPEND_MAGIC;
    registration->flags = flags;
    if (recipient)
        registration->subscriber = *(DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS *)recipient;
    *handle = registration;
    return ERROR_SUCCESS;
}

DWORD WINAPI PowerUnregisterSuspendResumeNotification(HPOWERNOTIFY handle)
{
    REGISTRATION *registration = handle;
    if (!registration || registration == INVALID_HANDLE_VALUE || registration->magic != SUSPEND_MAGIC)
        return ERROR_INVALID_PARAMETER;
    registration->magic = 0;
    LocalFree(registration);
    return ERROR_SUCCESS;
}
