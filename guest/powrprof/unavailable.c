/*
 * the exports that need what the machine does not have: the power service, which windows asks over rpc for the policies of
 * the processors and the limits of the hardware, and the legacy registry of the power control panel. a call that needs
 * the service gets the answer of a machine whose service is not running, and one that needs the legacy keys the answer of
 * a machine without them.
 */
#define POWRPROFAPI
#include <mime/win32/powrprof.h>

#define RPC_S_SERVER_UNAVAILABLE 1722L

#define NEEDS_SERVICE(name, args) DWORD WINAPI name(NEEDS_ARGS_##args) { return RPC_S_SERVER_UNAVAILABLE; }
#define NEEDS_LEGACY_KEYS(name, args) \
    BOOLEAN WINAPI name(NEEDS_ARGS_##args) \
    { \
        SetLastError(ERROR_FILE_NOT_FOUND); \
        return FALSE; \
    }

#define NEEDS_NO_DEVICES(name, args) \
    BOOLEAN WINAPI name(NEEDS_ARGS_##args) \
    { \
        SetLastError(ERROR_NOT_SUPPORTED); \
        return FALSE; \
    }

#define NEEDS_ARGS_0 void
#define NEEDS_ARGS_1 ULONG_PTR a1
#define NEEDS_ARGS_2 NEEDS_ARGS_1, ULONG_PTR a2
#define NEEDS_ARGS_3 NEEDS_ARGS_2, ULONG_PTR a3
#define NEEDS_ARGS_4 NEEDS_ARGS_3, ULONG_PTR a4
#define NEEDS_ARGS_5 NEEDS_ARGS_4, ULONG_PTR a5
#define NEEDS_ARGS_6 NEEDS_ARGS_5, ULONG_PTR a6
#define NEEDS_ARGS_7 NEEDS_ARGS_6, ULONG_PTR a7

/* ------------------------------------------------------------------ the power service */

NEEDS_SERVICE(DevicePowerSetDeviceState, 3)
NEEDS_SERVICE(GUIDFormatToGlobalPowerPolicy, 2)
NEEDS_SERVICE(GUIDFormatToPowerPolicy, 2)
NEEDS_SERVICE(LoadCurrentPwrScheme, 4)
NEEDS_SERVICE(MergeLegacyPwrScheme, 4)
NEEDS_SERVICE(PowerApplyPowerRequestOverride, 0)
NEEDS_SERVICE(PowerApplySettingChanges, 2)
NEEDS_SERVICE(PowerCleanupOverrides, 0)
NEEDS_SERVICE(PowerClearUserAwayPrediction, 1)
NEEDS_SERVICE(PowerCloseEnvironmentalMonitor, 1)
NEEDS_SERVICE(PowerCloseLimitsMitigation, 1)
NEEDS_SERVICE(PowerCloseLimitsPolicy, 1)
NEEDS_SERVICE(PowerDebugDifPowerPolicies, 4)
NEEDS_SERVICE(PowerDebugDifSystemPowerPolicies, 4)
NEEDS_SERVICE(PowerDebugDumpPowerPolicy, 3)
NEEDS_SERVICE(PowerDebugDumpPowerScheme, 3)
NEEDS_SERVICE(PowerDebugDumpSystemPowerCapabilities, 3)
NEEDS_SERVICE(PowerDebugDumpSystemPowerPolicy, 3)
NEEDS_SERVICE(PowerEnumerateSettings, 2)
NEEDS_SERVICE(PowerGetAdaptiveStandbyDiagnostics, 3)
NEEDS_SERVICE(PowerGetProfiles, 2)
NEEDS_SERVICE(PowerGetUserAwayMinPredictionConfidence, 1)
NEEDS_SERVICE(PowerImportPowerScheme, 3)
NEEDS_SERVICE(PowerPolicyToGUIDFormat, 2)
NEEDS_SERVICE(PowerReadACValueIndexEx, 7)
NEEDS_SERVICE(PowerReadDCValueIndexEx, 7)
NEEDS_SERVICE(PowerReadProfileAlias, 4)
NEEDS_SERVICE(PowerReadSecurityDescriptor, 3)
NEEDS_SERVICE(PowerRefreshProfileSettings, 0)
NEEDS_SERVICE(PowerRegisterEnvironmentalMonitor, 2)
NEEDS_SERVICE(PowerRegisterForEffectivePowerModeNotifications, 4)
NEEDS_SERVICE(PowerRegisterLimitsMitigation, 2)
NEEDS_SERVICE(PowerRegisterLimitsPolicy, 2)
NEEDS_SERVICE(PowerReplaceDefaultPowerSchemes, 0)
NEEDS_SERVICE(PowerReportLimitsEvent, 1)
NEEDS_SERVICE(PowerReportThermalEvent, 1)
NEEDS_SERVICE(PowerRestoreACProfileIndex, 3)
NEEDS_SERVICE(PowerRestoreDCProfileIndex, 3)
NEEDS_SERVICE(PowerRestoreDefaultPowerSchemes, 0)
NEEDS_SERVICE(PowerSetAlsBrightnessOffset, 1)
NEEDS_SERVICE(PowerSetBrightnessAndTransitionTimes, 1)
NEEDS_SERVICE(PowerSetUserAwayPrediction, 4)
NEEDS_SERVICE(PowerUnregisterFromEffectivePowerModeNotifications, 1)
NEEDS_SERVICE(PowerUpdateEnvironmentalMonitorState, 3)
NEEDS_SERVICE(PowerUpdateEnvironmentalMonitorThresholds, 3)
NEEDS_SERVICE(PowerUpdateLimitsMitigation, 2)
NEEDS_SERVICE(PowerWriteACProfileIndex, 4)
NEEDS_SERVICE(PowerWriteDCProfileIndex, 4)
NEEDS_SERVICE(PowerWriteSecurityDescriptor, 3)

/* ------------------------------------------------------------------ the legacy power control panel */

NEEDS_LEGACY_KEYS(CanUserWritePwrScheme, 0)
NEEDS_LEGACY_KEYS(DeletePwrScheme, 1)
NEEDS_LEGACY_KEYS(EnumPwrSchemes, 2)
NEEDS_LEGACY_KEYS(GetActivePwrScheme, 1)
NEEDS_LEGACY_KEYS(GetCurrentPowerPolicies, 2)
NEEDS_LEGACY_KEYS(GetPwrDiskSpindownRange, 2)
NEEDS_LEGACY_KEYS(IsAdminOverrideActive, 1)
NEEDS_LEGACY_KEYS(ReadGlobalPwrPolicy, 1)
NEEDS_LEGACY_KEYS(ReadProcessorPwrScheme, 2)
NEEDS_LEGACY_KEYS(ReadPwrScheme, 2)
NEEDS_LEGACY_KEYS(SetActivePwrScheme, 3)
NEEDS_LEGACY_KEYS(ValidatePowerPolicies, 2)
NEEDS_LEGACY_KEYS(WriteGlobalPwrPolicy, 1)
NEEDS_LEGACY_KEYS(WriteProcessorPwrScheme, 2)
NEEDS_LEGACY_KEYS(WritePwrScheme, 4)

/* ------------------------------------------------------------------ the device power list, which names devices the machine does not have */

NEEDS_NO_DEVICES(DevicePowerClose, 0)
NEEDS_NO_DEVICES(DevicePowerEnumDevices, 5)
NEEDS_NO_DEVICES(DevicePowerOpen, 1)
