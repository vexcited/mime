/* power management: powrprof.h, powerbase.h and powersetting.h. */
#ifndef MIME_WIN32_POWRPROF_H
#define MIME_WIN32_POWRPROF_H

#include <mime/win32/winbase.h>

#ifndef POWRPROFAPI
#define POWRPROFAPI DECLSPEC_IMPORT
#endif

typedef enum POWER_INFORMATION_LEVEL {
    SystemPowerPolicyAc = 0,
    SystemPowerPolicyDc = 1,
    VerifySystemPolicyAc = 2,
    VerifySystemPolicyDc = 3,
    SystemPowerCapabilities = 4,
    SystemBatteryState = 5,
    SystemPowerStateHandler = 6,
    ProcessorStateHandler = 7,
    SystemPowerPolicyCurrent = 8,
    AdministratorPowerPolicy = 9,
    SystemReserveHiberFile = 10,
    ProcessorInformation = 11,
    SystemPowerInformation = 12,
    ProcessorStateHandler2 = 13,
    LastWakeTime = 14,
    LastSleepTime = 15,
    SystemExecutionState = 16,
    SystemPowerStateNotifyHandler = 17,
    ProcessorPowerPolicyAc = 18,
    ProcessorPowerPolicyDc = 19,
    VerifyProcessorPowerPolicyAc = 20,
    VerifyProcessorPowerPolicyDc = 21,
    ProcessorPowerPolicyCurrent = 22,
    SystemPowerStateLogging = 23,
    SystemPowerLoggingEntry = 24,
    SetPowerSettingValue = 25,
    NotifyUserPowerSetting = 26,
    PowerInformationLevelUnused0 = 27,
    SystemMonitorHiberBootPowerOff = 28,
    SystemVideoState = 29,
    TraceApplicationPowerMessage = 30,
    TraceApplicationPowerMessageEnd = 31,
    ProcessorPerfStates = 32,
    ProcessorIdleStates = 33,
    ProcessorCap = 34,
    SystemWakeSource = 35,
    SystemHiberFileInformation = 36,
    TraceServicePowerMessage = 37,
    ProcessorLoad = 38,
    PowerShutdownNotification = 39,
    MonitorCapabilities = 40,
    SessionPowerInit = 41,
    SessionDisplayState = 42,
    PowerRequestCreate = 43,
    PowerRequestAction = 44,
    GetPowerRequestList = 45,
    ProcessorInformationEx = 46,
    NotifyUserModeLegacyPowerEvent = 47,
    GroupPark = 48,
    ProcessorIdleDomains = 49,
    WakeTimerList = 50,
    SystemHiberFileSize = 51,
    PlatformRole = 75,
    PlatformInformation = 79,
} POWER_INFORMATION_LEVEL;

typedef enum _POWER_PLATFORM_ROLE {
    PlatformRoleUnspecified = 0,
    PlatformRoleDesktop = 1,
    PlatformRoleMobile = 2,
    PlatformRoleWorkstation = 3,
    PlatformRoleEnterpriseServer = 4,
    PlatformRoleSOHOServer = 5,
    PlatformRoleAppliancePC = 6,
    PlatformRolePerformanceServer = 7,
    PlatformRoleSlate = 8,
    PlatformRoleMaximum = 9,
} POWER_PLATFORM_ROLE;

typedef enum _SYSTEM_POWER_STATE {
    PowerSystemUnspecified = 0,
    PowerSystemWorking = 1,
    PowerSystemSleeping1 = 2,
    PowerSystemSleeping2 = 3,
    PowerSystemSleeping3 = 4,
    PowerSystemHibernate = 5,
    PowerSystemShutdown = 6,
    PowerSystemMaximum = 7,
} SYSTEM_POWER_STATE;

typedef struct BATTERY_REPORTING_SCALE {
    DWORD Granularity;
    DWORD Capacity;
} BATTERY_REPORTING_SCALE;

typedef struct SYSTEM_POWER_CAPABILITIES {
    BOOLEAN PowerButtonPresent;
    BOOLEAN SleepButtonPresent;
    BOOLEAN LidPresent;
    BOOLEAN SystemS1;
    BOOLEAN SystemS2;
    BOOLEAN SystemS3;
    BOOLEAN SystemS4;
    BOOLEAN SystemS5;
    BOOLEAN HiberFilePresent;
    BOOLEAN FullWake;
    BOOLEAN VideoDimPresent;
    BOOLEAN ApmPresent;
    BOOLEAN UpsPresent;
    BOOLEAN ThermalControl;
    BOOLEAN ProcessorThrottle;
    BYTE ProcessorMinThrottle;
    BYTE ProcessorMaxThrottle;
    BOOLEAN FastSystemS4;
    BOOLEAN Hiberboot;
    BOOLEAN WakeAlarmPresent;
    BOOLEAN AoAc;
    BOOLEAN DiskSpinDown;
    BYTE HiberFileType;
    BOOLEAN AoAcConnectivitySupported;
    BYTE spare3[6];
    BOOLEAN SystemBatteriesPresent;
    BOOLEAN BatteriesAreShortTerm;
    BATTERY_REPORTING_SCALE BatteryScale[3];
    SYSTEM_POWER_STATE AcOnLineWake;
    SYSTEM_POWER_STATE SoftLidWake;
    SYSTEM_POWER_STATE RtcWake;
    SYSTEM_POWER_STATE MinDeviceWakeState;
    SYSTEM_POWER_STATE DefaultLowLatencyWake;
} SYSTEM_POWER_CAPABILITIES, *PSYSTEM_POWER_CAPABILITIES;

typedef struct SYSTEM_BATTERY_STATE {
    BOOLEAN AcOnLine;
    BOOLEAN BatteryPresent;
    BOOLEAN Charging;
    BOOLEAN Discharging;
    BOOLEAN Spare1[3];
    BYTE Tag;
    DWORD MaxCapacity;
    DWORD RemainingCapacity;
    DWORD Rate;
    DWORD EstimatedTime;
    DWORD DefaultAlert1;
    DWORD DefaultAlert2;
} SYSTEM_BATTERY_STATE, *PSYSTEM_BATTERY_STATE;

typedef struct _PROCESSOR_POWER_INFORMATION {
    ULONG Number;
    ULONG MaxMhz;
    ULONG CurrentMhz;
    ULONG MhzLimit;
    ULONG MaxIdleState;
    ULONG CurrentIdleState;
} PROCESSOR_POWER_INFORMATION, *PPROCESSOR_POWER_INFORMATION;

typedef struct _SYSTEM_POWER_INFORMATION {
    ULONG MaxIdlenessAllowed;
    ULONG Idleness;
    ULONG TimeRemaining;
    UCHAR CoolingMode;
} SYSTEM_POWER_INFORMATION, *PSYSTEM_POWER_INFORMATION;

typedef PVOID HPOWERNOTIFY, *PHPOWERNOTIFY;
typedef ULONG(CALLBACK *PDEVICE_NOTIFY_CALLBACK_ROUTINE)(PVOID context, ULONG type, PVOID setting);
typedef struct _DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS {
    PDEVICE_NOTIFY_CALLBACK_ROUTINE Callback;
    PVOID Context;
} DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS, *PDEVICE_NOTIFY_SUBSCRIBE_PARAMETERS;

#define DEVICE_NOTIFY_SERVICE_HANDLE 0x00000001
#define DEVICE_NOTIFY_CALLBACK 0x00000002

POWRPROFAPI LONG WINAPI CallNtPowerInformation(POWER_INFORMATION_LEVEL level, PVOID input, ULONG input_size,
                                               PVOID output, ULONG output_size);
POWRPROFAPI BOOLEAN WINAPI GetPwrCapabilities(PSYSTEM_POWER_CAPABILITIES capabilities);
POWRPROFAPI POWER_PLATFORM_ROLE WINAPI PowerDeterminePlatformRole(void);
POWRPROFAPI POWER_PLATFORM_ROLE WINAPI PowerDeterminePlatformRoleEx(ULONG version);
POWRPROFAPI DWORD WINAPI PowerGetActiveScheme(HKEY root, GUID **scheme);
POWRPROFAPI DWORD WINAPI PowerReadACValue(HKEY root, const GUID *scheme, const GUID *subgroup, const GUID *setting,
                                          PULONG type, LPBYTE buffer, LPDWORD size);
POWRPROFAPI DWORD WINAPI PowerReadDCValue(HKEY root, const GUID *scheme, const GUID *subgroup, const GUID *setting,
                                          PULONG type, PUCHAR buffer, LPDWORD size);
POWRPROFAPI DWORD WINAPI PowerReadACValueIndex(HKEY root, const GUID *scheme, const GUID *subgroup,
                                               const GUID *setting, LPDWORD index);
POWRPROFAPI DWORD WINAPI PowerSettingRegisterNotification(const GUID *setting, DWORD flags, HANDLE recipient,
                                                          PHPOWERNOTIFY handle);
POWRPROFAPI DWORD WINAPI PowerSettingUnregisterNotification(HPOWERNOTIFY handle);
POWRPROFAPI DWORD WINAPI PowerRegisterSuspendResumeNotification(DWORD flags, HANDLE recipient, PHPOWERNOTIFY handle);
POWRPROFAPI DWORD WINAPI PowerUnregisterSuspendResumeNotification(HPOWERNOTIFY handle);

#endif
