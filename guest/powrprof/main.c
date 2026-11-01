/*
 * powrprof: the power information windows reports for a desktop on mains power with no battery, sleep states or
 * hibernation file, and the balanced scheme the power service would hold.
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
