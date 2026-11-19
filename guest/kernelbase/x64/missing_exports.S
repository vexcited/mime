/* temporary probes: kernelbase exports mime lacks, each naming itself when called. */
.globl AcquireStateLock
.p2align 4
AcquireStateLock:
    mov $1, %ecx
    jmp MimeMissingKb

.globl AddDependencyToProcessPackageGraph
.p2align 4
AddDependencyToProcessPackageGraph:
    mov $2, %ecx
    jmp MimeMissingKb

.globl AddExtensionProgId
.p2align 4
AddExtensionProgId:
    mov $3, %ecx
    jmp MimeMissingKb

.globl _AddMUIStringToCache
.p2align 4
_AddMUIStringToCache:
    mov $4, %ecx
    jmp MimeMissingKb

.globl AddPackageDependency
.p2align 4
AddPackageDependency:
    mov $5, %ecx
    jmp MimeMissingKb

.globl AddPackageDependency2
.p2align 4
AddPackageDependency2:
    mov $6, %ecx
    jmp MimeMissingKb

.globl AddPackageNameAliasesByPackageDependency
.p2align 4
AddPackageNameAliasesByPackageDependency:
    mov $7, %ecx
    jmp MimeMissingKb

.globl AddPackageNameAliasesByPackageFullName
.p2align 4
AddPackageNameAliasesByPackageFullName:
    mov $8, %ecx
    jmp MimeMissingKb

.globl AllocateUserPhysicalPages2
.p2align 4
AllocateUserPhysicalPages2:
    mov $9, %ecx
    jmp MimeMissingKb

.globl AllocConsoleWithOptions
.p2align 4
AllocConsoleWithOptions:
    mov $10, %ecx
    jmp MimeMissingKb

.globl _amsg_exit
.p2align 4
_amsg_exit:
    mov $11, %ecx
    jmp MimeMissingKb

.globl AppContainerLookupDisplayNameMrtReference
.p2align 4
AppContainerLookupDisplayNameMrtReference:
    mov $12, %ecx
    jmp MimeMissingKb

.globl ApplicationUserModelIdFromProductId
.p2align 4
ApplicationUserModelIdFromProductId:
    mov $13, %ecx
    jmp MimeMissingKb

.globl AppXCheckPplSupport
.p2align 4
AppXCheckPplSupport:
    mov $14, %ecx
    jmp MimeMissingKb

.globl AppXFreeMemory
.p2align 4
AppXFreeMemory:
    mov $15, %ecx
    jmp MimeMissingKb

.globl AppXGetApplicationData
.p2align 4
AppXGetApplicationData:
    mov $16, %ecx
    jmp MimeMissingKb

.globl AppXGetDevelopmentMode
.p2align 4
AppXGetDevelopmentMode:
    mov $17, %ecx
    jmp MimeMissingKb

.globl AppXGetOSMaxVersionTested
.p2align 4
AppXGetOSMaxVersionTested:
    mov $18, %ecx
    jmp MimeMissingKb

.globl AppXGetOSMinVersion
.p2align 4
AppXGetOSMinVersion:
    mov $19, %ecx
    jmp MimeMissingKb

.globl AppXGetPackageCapabilities
.p2align 4
AppXGetPackageCapabilities:
    mov $20, %ecx
    jmp MimeMissingKb

.globl AppXGetPackageSid
.p2align 4
AppXGetPackageSid:
    mov $21, %ecx
    jmp MimeMissingKb

.globl AppXLookupDisplayName
.p2align 4
AppXLookupDisplayName:
    mov $22, %ecx
    jmp MimeMissingKb

.globl AppXLookupMoniker
.p2align 4
AppXLookupMoniker:
    mov $23, %ecx
    jmp MimeMissingKb

.globl AppXPostSuccessExtension
.p2align 4
AppXPostSuccessExtension:
    mov $24, %ecx
    jmp MimeMissingKb

.globl AppXPreCreationExtension
.p2align 4
AppXPreCreationExtension:
    mov $25, %ecx
    jmp MimeMissingKb

.globl AppXReleaseAppXContext
.p2align 4
AppXReleaseAppXContext:
    mov $26, %ecx
    jmp MimeMissingKb

.globl AppXUpdatePackageCapabilities
.p2align 4
AppXUpdatePackageCapabilities:
    mov $27, %ecx
    jmp MimeMissingKb

.globl AreExternalFeatureDependenciesEnabled
.p2align 4
AreExternalFeatureDependenciesEnabled:
    mov $28, %ecx
    jmp MimeMissingKb

.globl AreShortNamesEnabled
.p2align 4
AreShortNamesEnabled:
    mov $29, %ecx
    jmp MimeMissingKb

.globl AreThereVisibleLogoffScriptsInternal
.p2align 4
AreThereVisibleLogoffScriptsInternal:
    mov $30, %ecx
    jmp MimeMissingKb

.globl AreThereVisibleShutdownScriptsInternal
.p2align 4
AreThereVisibleShutdownScriptsInternal:
    mov $31, %ecx
    jmp MimeMissingKb

.globl __argc
.p2align 4
__argc:
    mov $32, %ecx
    jmp MimeMissingKb

.globl ArmFeatureUsageSubscriberFlushNotification
.p2align 4
ArmFeatureUsageSubscriberFlushNotification:
    mov $33, %ecx
    jmp MimeMissingKb

.globl atexit
.p2align 4
atexit:
    mov $34, %ecx
    jmp MimeMissingKb

.globl BaseCheckAppcompatCache
.p2align 4
BaseCheckAppcompatCache:
    mov $35, %ecx
    jmp MimeMissingKb

.globl BaseCheckAppcompatCacheEx
.p2align 4
BaseCheckAppcompatCacheEx:
    mov $36, %ecx
    jmp MimeMissingKb

.globl BaseCleanupAppcompatCacheSupport
.p2align 4
BaseCleanupAppcompatCacheSupport:
    mov $37, %ecx
    jmp MimeMissingKb

.globl BaseDllFreeResourceId
.p2align 4
BaseDllFreeResourceId:
    mov $38, %ecx
    jmp MimeMissingKb

.globl BaseDllMapResourceIdW
.p2align 4
BaseDllMapResourceIdW:
    mov $39, %ecx
    jmp MimeMissingKb

.globl BaseDumpAppcompatCache
.p2align 4
BaseDumpAppcompatCache:
    mov $40, %ecx
    jmp MimeMissingKb

.globl BaseFlushAppcompatCache
.p2align 4
BaseFlushAppcompatCache:
    mov $41, %ecx
    jmp MimeMissingKb

.globl BaseFormatObjectAttributes
.p2align 4
BaseFormatObjectAttributes:
    mov $42, %ecx
    jmp MimeMissingKb

.globl BaseFreeAppCompatDataForProcess
.p2align 4
BaseFreeAppCompatDataForProcess:
    mov $43, %ecx
    jmp MimeMissingKb

.globl BaseGetConsoleReference
.p2align 4
BaseGetConsoleReference:
    mov $44, %ecx
    jmp MimeMissingKb

.globl BaseGetNamedObjectDirectory
.p2align 4
BaseGetNamedObjectDirectory:
    mov $45, %ecx
    jmp MimeMissingKb

.globl BaseInitAppcompatCacheSupport
.p2align 4
BaseInitAppcompatCacheSupport:
    mov $46, %ecx
    jmp MimeMissingKb

.globl BaseIsAppcompatInfrastructureDisabled
.p2align 4
BaseIsAppcompatInfrastructureDisabled:
    mov $47, %ecx
    jmp MimeMissingKb

.globl BaseMarkFileForDelete
.p2align 4
BaseMarkFileForDelete:
    mov $48, %ecx
    jmp MimeMissingKb

.globl BasepAdjustObjectAttributesForPrivateNamespace
.p2align 4
BasepAdjustObjectAttributesForPrivateNamespace:
    mov $49, %ecx
    jmp MimeMissingKb

.globl BasepCopyFileCallback
.p2align 4
BasepCopyFileCallback:
    mov $50, %ecx
    jmp MimeMissingKb

.globl BasepCopyFileExW
.p2align 4
BasepCopyFileExW:
    mov $51, %ecx
    jmp MimeMissingKb

.globl BasepNotifyTrackingService
.p2align 4
BasepNotifyTrackingService:
    mov $52, %ecx
    jmp MimeMissingKb

.globl BaseReadAppCompatDataForProcess
.p2align 4
BaseReadAppCompatDataForProcess:
    mov $53, %ecx
    jmp MimeMissingKb

.globl BaseUpdateAppcompatCache
.p2align 4
BaseUpdateAppcompatCache:
    mov $54, %ecx
    jmp MimeMissingKb

.globl BuildIoRingCancelRequest
.p2align 4
BuildIoRingCancelRequest:
    mov $55, %ecx
    jmp MimeMissingKb

.globl BuildIoRingFlushFile
.p2align 4
BuildIoRingFlushFile:
    mov $56, %ecx
    jmp MimeMissingKb

.globl BuildIoRingReadFile
.p2align 4
BuildIoRingReadFile:
    mov $57, %ecx
    jmp MimeMissingKb

.globl BuildIoRingReadFileScatter
.p2align 4
BuildIoRingReadFileScatter:
    mov $58, %ecx
    jmp MimeMissingKb

.globl BuildIoRingRegisterBuffers
.p2align 4
BuildIoRingRegisterBuffers:
    mov $59, %ecx
    jmp MimeMissingKb

.globl BuildIoRingRegisterFileHandles
.p2align 4
BuildIoRingRegisterFileHandles:
    mov $60, %ecx
    jmp MimeMissingKb

.globl BuildIoRingWriteFile
.p2align 4
BuildIoRingWriteFile:
    mov $61, %ecx
    jmp MimeMissingKb

.globl BuildIoRingWriteFileGather
.p2align 4
BuildIoRingWriteFileGather:
    mov $62, %ecx
    jmp MimeMissingKb

.globl CallEnclave
.p2align 4
CallEnclave:
    mov $63, %ecx
    jmp MimeMissingKb

.globl CeipIsOptedIn
.p2align 4
CeipIsOptedIn:
    mov $64, %ecx
    jmp MimeMissingKb

.globl _c_exit
.p2align 4
_c_exit:
    mov $65, %ecx
    jmp MimeMissingKb

.globl _cexit
.p2align 4
_cexit:
    mov $66, %ecx
    jmp MimeMissingKb

.globl CheckAllowDecryptedRemoteDestinationPolicy
.p2align 4
CheckAllowDecryptedRemoteDestinationPolicy:
    mov $67, %ecx
    jmp MimeMissingKb

.globl CheckGroupPolicyEnabled
.p2align 4
CheckGroupPolicyEnabled:
    mov $68, %ecx
    jmp MimeMissingKb

.globl CheckIfStateChangeNotificationExists
.p2align 4
CheckIfStateChangeNotificationExists:
    mov $69, %ecx
    jmp MimeMissingKb

.globl CheckIsMSIXPackage
.p2align 4
CheckIsMSIXPackage:
    mov $70, %ecx
    jmp MimeMissingKb

.globl CheckTokenCapability
.p2align 4
CheckTokenCapability:
    mov $71, %ecx
    jmp MimeMissingKb

.globl CloseIoRing
.p2align 4
CloseIoRing:
    mov $72, %ecx
    jmp MimeMissingKb

.globl CLOSE_LOCAL_HANDLE_INTERNAL
.p2align 4
CLOSE_LOCAL_HANDLE_INTERNAL:
    mov $73, %ecx
    jmp MimeMissingKb

.globl CloseState
.p2align 4
CloseState:
    mov $74, %ecx
    jmp MimeMissingKb

.globl CloseStateAtom
.p2align 4
CloseStateAtom:
    mov $75, %ecx
    jmp MimeMissingKb

.globl CloseStateChangeNotification
.p2align 4
CloseStateChangeNotification:
    mov $76, %ecx
    jmp MimeMissingKb

.globl CloseStateContainer
.p2align 4
CloseStateContainer:
    mov $77, %ecx
    jmp MimeMissingKb

.globl CloseStateLock
.p2align 4
CloseStateLock:
    mov $78, %ecx
    jmp MimeMissingKb

.globl CommitStateAtom
.p2align 4
CommitStateAtom:
    mov $79, %ecx
    jmp MimeMissingKb

.globl ConvertAuxiliaryCounterToPerformanceCounter
.p2align 4
ConvertAuxiliaryCounterToPerformanceCounter:
    mov $80, %ecx
    jmp MimeMissingKb

.globl ConvertPerformanceCounterToAuxiliaryCounter
.p2align 4
ConvertPerformanceCounterToAuxiliaryCounter:
    mov $81, %ecx
    jmp MimeMissingKb

.globl CopyFileFromAppW
.p2align 4
CopyFileFromAppW:
    mov $82, %ecx
    jmp MimeMissingKb

.globl CopyMemoryNonTemporal
.p2align 4
CopyMemoryNonTemporal:
    mov $83, %ecx
    jmp MimeMissingKb

.globl CouldMultiUserAppsBehaviorBePossibleForPackage
.p2align 4
CouldMultiUserAppsBehaviorBePossibleForPackage:
    mov $84, %ecx
    jmp MimeMissingKb

.globl CreateAppContainerToken
.p2align 4
CreateAppContainerToken:
    mov $85, %ecx
    jmp MimeMissingKb

.globl CreateAppSiloToken
.p2align 4
CreateAppSiloToken:
    mov $86, %ecx
    jmp MimeMissingKb

.globl CreateDirectory2A
.p2align 4
CreateDirectory2A:
    mov $87, %ecx
    jmp MimeMissingKb

.globl CreateDirectory2W
.p2align 4
CreateDirectory2W:
    mov $88, %ecx
    jmp MimeMissingKb

.globl CreateDirectoryFromAppW
.p2align 4
CreateDirectoryFromAppW:
    mov $89, %ecx
    jmp MimeMissingKb

.globl CreateEnclave
.p2align 4
CreateEnclave:
    mov $90, %ecx
    jmp MimeMissingKb

.globl CreateFile2FromAppW
.p2align 4
CreateFile2FromAppW:
    mov $91, %ecx
    jmp MimeMissingKb

.globl CreateFile3
.p2align 4
CreateFile3:
    mov $92, %ecx
    jmp MimeMissingKb

.globl CreateFileFromAppW
.p2align 4
CreateFileFromAppW:
    mov $93, %ecx
    jmp MimeMissingKb

.globl CreateFileMapping2
.p2align 4
CreateFileMapping2:
    mov $94, %ecx
    jmp MimeMissingKb

.globl CreateIoRing
.p2align 4
CreateIoRing:
    mov $95, %ecx
    jmp MimeMissingKb

.globl CreatePseudoConsoleAsUser
.p2align 4
CreatePseudoConsoleAsUser:
    mov $96, %ecx
    jmp MimeMissingKb

.globl CreateStateAtom
.p2align 4
CreateStateAtom:
    mov $97, %ecx
    jmp MimeMissingKb

.globl CreateStateChangeNotification
.p2align 4
CreateStateChangeNotification:
    mov $98, %ecx
    jmp MimeMissingKb

.globl CreateStateContainer
.p2align 4
CreateStateContainer:
    mov $99, %ecx
    jmp MimeMissingKb

.globl CreateStateLock
.p2align 4
CreateStateLock:
    mov $100, %ecx
    jmp MimeMissingKb

.globl CreateStateSubcontainer
.p2align 4
CreateStateSubcontainer:
    mov $101, %ecx
    jmp MimeMissingKb

.globl __C_specific_handler
.p2align 4
__C_specific_handler:
    mov $102, %ecx
    jmp MimeMissingKb

.globl CtrlRoutine
.p2align 4
CtrlRoutine:
    mov $103, %ecx
    jmp MimeMissingKb

.globl CveEventWrite
.p2align 4
CveEventWrite:
    mov $104, %ecx
    jmp MimeMissingKb

.globl DecodeRemotePointer
.p2align 4
DecodeRemotePointer:
    mov $105, %ecx
    jmp MimeMissingKb

.globl DelayLoadFailureHookLookup
.p2align 4
DelayLoadFailureHookLookup:
    mov $106, %ecx
    jmp MimeMissingKb

.globl DeleteEnclave
.p2align 4
DeleteEnclave:
    mov $107, %ecx
    jmp MimeMissingKb

.globl DeleteFile2A
.p2align 4
DeleteFile2A:
    mov $108, %ecx
    jmp MimeMissingKb

.globl DeleteFile2W
.p2align 4
DeleteFile2W:
    mov $109, %ecx
    jmp MimeMissingKb

.globl DeleteFileFromAppW
.p2align 4
DeleteFileFromAppW:
    mov $110, %ecx
    jmp MimeMissingKb

.globl DeletePackageDependency
.p2align 4
DeletePackageDependency:
    mov $111, %ecx
    jmp MimeMissingKb

.globl DeletePersistedRegistryValue
.p2align 4
DeletePersistedRegistryValue:
    mov $112, %ecx
    jmp MimeMissingKb

.globl DeleteStateAtomValue
.p2align 4
DeleteStateAtomValue:
    mov $113, %ecx
    jmp MimeMissingKb

.globl DeleteStateContainer
.p2align 4
DeleteStateContainer:
    mov $114, %ecx
    jmp MimeMissingKb

.globl DeleteStateContainerValue
.p2align 4
DeleteStateContainerValue:
    mov $115, %ecx
    jmp MimeMissingKb

.globl DisablePredefinedHandleTableInternal
.p2align 4
DisablePredefinedHandleTableInternal:
    mov $116, %ecx
    jmp MimeMissingKb

.globl __dllonexit3
.p2align 4
__dllonexit3:
    mov $117, %ecx
    jmp MimeMissingKb

.globl DsBindWithSpnExW
.p2align 4
DsBindWithSpnExW:
    mov $118, %ecx
    jmp MimeMissingKb

.globl DsCrackNamesW
.p2align 4
DsCrackNamesW:
    mov $119, %ecx
    jmp MimeMissingKb

.globl DsFreeDomainControllerInfoW
.p2align 4
DsFreeDomainControllerInfoW:
    mov $120, %ecx
    jmp MimeMissingKb

.globl DsFreeNameResultW
.p2align 4
DsFreeNameResultW:
    mov $121, %ecx
    jmp MimeMissingKb

.globl DsFreeNgcKey
.p2align 4
DsFreeNgcKey:
    mov $122, %ecx
    jmp MimeMissingKb

.globl DsFreePasswordCredentials
.p2align 4
DsFreePasswordCredentials:
    mov $123, %ecx
    jmp MimeMissingKb

.globl DsGetDomainControllerInfoW
.p2align 4
DsGetDomainControllerInfoW:
    mov $124, %ecx
    jmp MimeMissingKb

.globl DsMakePasswordCredentialsW
.p2align 4
DsMakePasswordCredentialsW:
    mov $125, %ecx
    jmp MimeMissingKb

.globl DsReadNgcKeyW
.p2align 4
DsReadNgcKeyW:
    mov $126, %ecx
    jmp MimeMissingKb

.globl DsUnBindW
.p2align 4
DsUnBindW:
    mov $127, %ecx
    jmp MimeMissingKb

.globl DsWriteNgcKeyW
.p2align 4
DsWriteNgcKeyW:
    mov $128, %ecx
    jmp MimeMissingKb

.globl DuplicateStateContainerHandle
.p2align 4
DuplicateStateContainerHandle:
    mov $129, %ecx
    jmp MimeMissingKb

.globl EnableProcessOptionalXStateFeatures
.p2align 4
EnableProcessOptionalXStateFeatures:
    mov $130, %ecx
    jmp MimeMissingKb

.globl EncodeRemotePointer
.p2align 4
EncodeRemotePointer:
    mov $131, %ecx
    jmp MimeMissingKb

.globl EnterCriticalPolicySectionInternal
.p2align 4
EnterCriticalPolicySectionInternal:
    mov $132, %ecx
    jmp MimeMissingKb

.globl EnumDeviceDrivers
.p2align 4
EnumDeviceDrivers:
    mov $133, %ecx
    jmp MimeMissingKb

.globl EnumDynamicTimeZoneInformation
.p2align 4
EnumDynamicTimeZoneInformation:
    mov $134, %ecx
    jmp MimeMissingKb

.globl EnumerateExtensionNames
.p2align 4
EnumerateExtensionNames:
    mov $135, %ecx
    jmp MimeMissingKb

.globl EnumerateStateAtomValues
.p2align 4
EnumerateStateAtomValues:
    mov $136, %ecx
    jmp MimeMissingKb

.globl EnumerateStateContainerItems
.p2align 4
EnumerateStateContainerItems:
    mov $137, %ecx
    jmp MimeMissingKb

.globl EnumPageFilesA
.p2align 4
EnumPageFilesA:
    mov $138, %ecx
    jmp MimeMissingKb

.globl EnumPageFilesW
.p2align 4
EnumPageFilesW:
    mov $139, %ecx
    jmp MimeMissingKb

.globl EnumProcesses
.p2align 4
EnumProcesses:
    mov $140, %ecx
    jmp MimeMissingKb

.globl EnumProcessModules
.p2align 4
EnumProcessModules:
    mov $141, %ecx
    jmp MimeMissingKb

.globl EnumProcessModulesEx
.p2align 4
EnumProcessModulesEx:
    mov $142, %ecx
    jmp MimeMissingKb

.globl EnumResourceLanguagesExA
.p2align 4
EnumResourceLanguagesExA:
    mov $143, %ecx
    jmp MimeMissingKb

.globl EnumResourceNamesExA
.p2align 4
EnumResourceNamesExA:
    mov $144, %ecx
    jmp MimeMissingKb

.globl EnumResourceTypesExA
.p2align 4
EnumResourceTypesExA:
    mov $145, %ecx
    jmp MimeMissingKb

.globl EnumSystemGeoNames
.p2align 4
EnumSystemGeoNames:
    mov $146, %ecx
    jmp MimeMissingKb

.globl _exit
.p2align 4
_exit:
    mov $147, %ecx
    jmp MimeMissingKb

.globl exit
.p2align 4
exit:
    mov $148, %ecx
    jmp MimeMissingKb

.globl ExtensionProgIdExists
.p2align 4
ExtensionProgIdExists:
    mov $149, %ecx
    jmp MimeMissingKb

.globl __FC_Query_Config
.p2align 4
__FC_Query_Config:
    mov $150, %ecx
    jmp MimeMissingKb

.globl __FC_Query_System
.p2align 4
__FC_Query_System:
    mov $151, %ecx
    jmp MimeMissingKb

.globl __FC_Report
.p2align 4
__FC_Report:
    mov $152, %ecx
    jmp MimeMissingKb

.globl __FC_Subscribe
.p2align 4
__FC_Subscribe:
    mov $153, %ecx
    jmp MimeMissingKb

.globl __FC_Unsubscribe
.p2align 4
__FC_Unsubscribe:
    mov $154, %ecx
    jmp MimeMissingKb

.globl FindFirstFileExFromAppW
.p2align 4
FindFirstFileExFromAppW:
    mov $155, %ecx
    jmp MimeMissingKb

.globl FindPackageDependency
.p2align 4
FindPackageDependency:
    mov $156, %ecx
    jmp MimeMissingKb

.globl ForceSyncFgPolicyInternal
.p2align 4
ForceSyncFgPolicyInternal:
    mov $157, %ecx
    jmp MimeMissingKb

.globl FormatApplicationUserModelId
.p2align 4
FormatApplicationUserModelId:
    mov $158, %ecx
    jmp MimeMissingKb

.globl FormatApplicationUserModelIdA
.p2align 4
FormatApplicationUserModelIdA:
    mov $159, %ecx
    jmp MimeMissingKb

.globl FreeGPOListInternalA
.p2align 4
FreeGPOListInternalA:
    mov $160, %ecx
    jmp MimeMissingKb

.globl FreeGPOListInternalW
.p2align 4
FreeGPOListInternalW:
    mov $161, %ecx
    jmp MimeMissingKb

.globl GenerateGPNotificationInternal
.p2align 4
GenerateGPNotificationInternal:
    mov $162, %ecx
    jmp MimeMissingKb

.globl GetAdjustObjectAttributesForPrivateNamespaceRoutine
.p2align 4
GetAdjustObjectAttributesForPrivateNamespaceRoutine:
    mov $163, %ecx
    jmp MimeMissingKb

.globl GetAlternatePackageRoots
.p2align 4
GetAlternatePackageRoots:
    mov $164, %ecx
    jmp MimeMissingKb

.globl GetApiSetModuleBaseName
.p2align 4
GetApiSetModuleBaseName:
    mov $165, %ecx
    jmp MimeMissingKb

.globl GetAppContainerAce
.p2align 4
GetAppContainerAce:
    mov $166, %ecx
    jmp MimeMissingKb

.globl GetAppDataFolder
.p2align 4
GetAppDataFolder:
    mov $167, %ecx
    jmp MimeMissingKb

.globl GetApplicationUserModelIdFromToken
.p2align 4
GetApplicationUserModelIdFromToken:
    mov $168, %ecx
    jmp MimeMissingKb

.globl GetAppliedGPOListInternalA
.p2align 4
GetAppliedGPOListInternalA:
    mov $169, %ecx
    jmp MimeMissingKb

.globl GetAppliedGPOListInternalW
.p2align 4
GetAppliedGPOListInternalW:
    mov $170, %ecx
    jmp MimeMissingKb

.globl GetAppModelVersion
.p2align 4
GetAppModelVersion:
    mov $171, %ecx
    jmp MimeMissingKb

.globl GetCachedSigningLevel
.p2align 4
GetCachedSigningLevel:
    mov $172, %ecx
    jmp MimeMissingKb

.globl GetCalendar
.p2align 4
GetCalendar:
    mov $173, %ecx
    jmp MimeMissingKb

.globl GetCommConfig
.p2align 4
GetCommConfig:
    mov $174, %ecx
    jmp MimeMissingKb

.globl GetCommPorts
.p2align 4
GetCommPorts:
    mov $175, %ecx
    jmp MimeMissingKb

.globl GetCPFileNameFromRegistry
.p2align 4
GetCPFileNameFromRegistry:
    mov $176, %ecx
    jmp MimeMissingKb

.globl GetCPHashNode
.p2align 4
GetCPHashNode:
    mov $177, %ecx
    jmp MimeMissingKb

.globl GetCurrentPackageApplicationContext
.p2align 4
GetCurrentPackageApplicationContext:
    mov $178, %ecx
    jmp MimeMissingKb

.globl GetCurrentPackageApplicationResourcesContext
.p2align 4
GetCurrentPackageApplicationResourcesContext:
    mov $179, %ecx
    jmp MimeMissingKb

.globl GetCurrentPackageContext
.p2align 4
GetCurrentPackageContext:
    mov $180, %ecx
    jmp MimeMissingKb

.globl GetCurrentPackageGlobalizationContext
.p2align 4
GetCurrentPackageGlobalizationContext:
    mov $181, %ecx
    jmp MimeMissingKb

.globl GetCurrentPackageInfo2
.p2align 4
GetCurrentPackageInfo2:
    mov $182, %ecx
    jmp MimeMissingKb

.globl GetCurrentPackageInfo3
.p2align 4
GetCurrentPackageInfo3:
    mov $183, %ecx
    jmp MimeMissingKb

.globl GetCurrentPackageInfo_PackageNameAliases
.p2align 4
GetCurrentPackageInfo_PackageNameAliases:
    mov $184, %ecx
    jmp MimeMissingKb

.globl GetCurrentPackagePath2
.p2align 4
GetCurrentPackagePath2:
    mov $185, %ecx
    jmp MimeMissingKb

.globl GetCurrentPackageResourcesContext
.p2align 4
GetCurrentPackageResourcesContext:
    mov $186, %ecx
    jmp MimeMissingKb

.globl GetCurrentPackageSecurityContext
.p2align 4
GetCurrentPackageSecurityContext:
    mov $187, %ecx
    jmp MimeMissingKb

.globl GetCurrentTargetPlatformContext
.p2align 4
GetCurrentTargetPlatformContext:
    mov $188, %ecx
    jmp MimeMissingKb

.globl GetDeveloperDriveEnablementState
.p2align 4
GetDeveloperDriveEnablementState:
    mov $189, %ecx
    jmp MimeMissingKb

.globl GetDeviceDriverBaseNameA
.p2align 4
GetDeviceDriverBaseNameA:
    mov $190, %ecx
    jmp MimeMissingKb

.globl GetDeviceDriverBaseNameW
.p2align 4
GetDeviceDriverBaseNameW:
    mov $191, %ecx
    jmp MimeMissingKb

.globl GetDeviceDriverFileNameA
.p2align 4
GetDeviceDriverFileNameA:
    mov $192, %ecx
    jmp MimeMissingKb

.globl GetDeviceDriverFileNameW
.p2align 4
GetDeviceDriverFileNameW:
    mov $193, %ecx
    jmp MimeMissingKb

.globl GetDiskSpaceInformationA
.p2align 4
GetDiskSpaceInformationA:
    mov $194, %ecx
    jmp MimeMissingKb

.globl GetDiskSpaceInformationW
.p2align 4
GetDiskSpaceInformationW:
    mov $195, %ecx
    jmp MimeMissingKb

.globl GetDiskSpaceInformationWCOS
.p2align 4
GetDiskSpaceInformationWCOS:
    mov $196, %ecx
    jmp MimeMissingKb

.globl GetEffectivePackageStatusForUser
.p2align 4
GetEffectivePackageStatusForUser:
    mov $197, %ecx
    jmp MimeMissingKb

.globl GetEffectivePackageStatusForUserSid
.p2align 4
GetEffectivePackageStatusForUserSid:
    mov $198, %ecx
    jmp MimeMissingKb

.globl GetEightBitStringToUnicodeSizeRoutine
.p2align 4
GetEightBitStringToUnicodeSizeRoutine:
    mov $199, %ecx
    jmp MimeMissingKb

.globl GetEightBitStringToUnicodeStringRoutine
.p2align 4
GetEightBitStringToUnicodeStringRoutine:
    mov $200, %ecx
    jmp MimeMissingKb

.globl GetEraNameCountedString
.p2align 4
GetEraNameCountedString:
    mov $201, %ecx
    jmp MimeMissingKb

.globl GetExtensionApplicationUserModelId
.p2align 4
GetExtensionApplicationUserModelId:
    mov $202, %ecx
    jmp MimeMissingKb

.globl GetExtensionProgIds
.p2align 4
GetExtensionProgIds:
    mov $203, %ecx
    jmp MimeMissingKb

.globl GetExtensionProperty
.p2align 4
GetExtensionProperty:
    mov $204, %ecx
    jmp MimeMissingKb

.globl GetExtensionProperty2
.p2align 4
GetExtensionProperty2:
    mov $205, %ecx
    jmp MimeMissingKb

.globl GetFallbackDisplayName
.p2align 4
GetFallbackDisplayName:
    mov $206, %ecx
    jmp MimeMissingKb

.globl GetFeatureToggleConfiguration
.p2align 4
GetFeatureToggleConfiguration:
    mov $207, %ecx
    jmp MimeMissingKb

.globl GetFeatureTogglesChangeToken
.p2align 4
GetFeatureTogglesChangeToken:
    mov $208, %ecx
    jmp MimeMissingKb

.globl GetFileAttributesExFromAppW
.p2align 4
GetFileAttributesExFromAppW:
    mov $209, %ecx
    jmp MimeMissingKb

.globl GetFileInformationByName
.p2align 4
GetFileInformationByName:
    mov $210, %ecx
    jmp MimeMissingKb

.globl GetFileMUIInfo
.p2align 4
GetFileMUIInfo:
    mov $211, %ecx
    jmp MimeMissingKb

.globl GetFileVersionInfoByHandle
.p2align 4
GetFileVersionInfoByHandle:
    mov $212, %ecx
    jmp MimeMissingKb

.globl GetGamingDeviceModelInformation
.p2align 4
GetGamingDeviceModelInformation:
    mov $213, %ecx
    jmp MimeMissingKb

.globl GetGeoInfoEx
.p2align 4
GetGeoInfoEx:
    mov $214, %ecx
    jmp MimeMissingKb

.globl GetGPOListInternalA
.p2align 4
GetGPOListInternalA:
    mov $215, %ecx
    jmp MimeMissingKb

.globl GetGPOListInternalW
.p2align 4
GetGPOListInternalW:
    mov $216, %ecx
    jmp MimeMissingKb

.globl GetHivePath
.p2align 4
GetHivePath:
    mov $217, %ecx
    jmp MimeMissingKb

.globl GetIdForPackageDependencyContext
.p2align 4
GetIdForPackageDependencyContext:
    mov $218, %ecx
    jmp MimeMissingKb

.globl GetIntegratedDisplaySize
.p2align 4
GetIntegratedDisplaySize:
    mov $219, %ecx
    jmp MimeMissingKb

.globl GetIoRingInfo
.p2align 4
GetIoRingInfo:
    mov $220, %ecx
    jmp MimeMissingKb

.globl GetIsEdpEnabled
.p2align 4
GetIsEdpEnabled:
    mov $221, %ecx
    jmp MimeMissingKb

.globl GetIsWdagEnabled
.p2align 4
GetIsWdagEnabled:
    mov $222, %ecx
    jmp MimeMissingKb

.globl GetLocaleInfoHelper
.p2align 4
GetLocaleInfoHelper:
    mov $223, %ecx
    jmp MimeMissingKb

.globl GetMappedFileNameA
.p2align 4
GetMappedFileNameA:
    mov $224, %ecx
    jmp MimeMissingKb

.globl GetMappedFileNameW
.p2align 4
GetMappedFileNameW:
    mov $225, %ecx
    jmp MimeMissingKb

.globl GetMemoryNumaClosestInitiatorNode
.p2align 4
GetMemoryNumaClosestInitiatorNode:
    mov $226, %ecx
    jmp MimeMissingKb

.globl GetMemoryNumaPerformanceInformation
.p2align 4
GetMemoryNumaPerformanceInformation:
    mov $227, %ecx
    jmp MimeMissingKb

.globl GetModuleBaseNameA
.p2align 4
GetModuleBaseNameA:
    mov $228, %ecx
    jmp MimeMissingKb

.globl GetModuleBaseNameW
.p2align 4
GetModuleBaseNameW:
    mov $229, %ecx
    jmp MimeMissingKb

.globl GetModuleFileNameExA
.p2align 4
GetModuleFileNameExA:
    mov $230, %ecx
    jmp MimeMissingKb

.globl GetModuleFileNameExW
.p2align 4
GetModuleFileNameExW:
    mov $231, %ecx
    jmp MimeMissingKb

.globl GetModuleInformation
.p2align 4
GetModuleInformation:
    mov $232, %ecx
    jmp MimeMissingKb

.globl _GetMUIStringFromCache
.p2align 4
_GetMUIStringFromCache:
    mov $233, %ecx
    jmp MimeMissingKb

.globl GetNamedLocaleHashNode
.p2align 4
GetNamedLocaleHashNode:
    mov $234, %ecx
    jmp MimeMissingKb

.globl GetNamedPipeAttribute
.p2align 4
GetNamedPipeAttribute:
    mov $235, %ecx
    jmp MimeMissingKb

.globl GetNextFgPolicyRefreshInfoInternal
.p2align 4
GetNextFgPolicyRefreshInfoInternal:
    mov $236, %ecx
    jmp MimeMissingKb

.globl GetNumaNodeProcessorMask2
.p2align 4
GetNumaNodeProcessorMask2:
    mov $237, %ecx
    jmp MimeMissingKb

.globl GetNumaProximityNodeEx
.p2align 4
GetNumaProximityNodeEx:
    mov $238, %ecx
    jmp MimeMissingKb

.globl GetOsManufacturingMode
.p2align 4
GetOsManufacturingMode:
    mov $239, %ecx
    jmp MimeMissingKb

.globl GetPackageApplicationContext
.p2align 4
GetPackageApplicationContext:
    mov $240, %ecx
    jmp MimeMissingKb

.globl GetPackageApplicationProperty
.p2align 4
GetPackageApplicationProperty:
    mov $241, %ecx
    jmp MimeMissingKb

.globl GetPackageApplicationPropertyString
.p2align 4
GetPackageApplicationPropertyString:
    mov $242, %ecx
    jmp MimeMissingKb

.globl GetPackageApplicationResourcesContext
.p2align 4
GetPackageApplicationResourcesContext:
    mov $243, %ecx
    jmp MimeMissingKb

.globl GetPackageContext
.p2align 4
GetPackageContext:
    mov $244, %ecx
    jmp MimeMissingKb

.globl GetPackagedDataForFile
.p2align 4
GetPackagedDataForFile:
    mov $245, %ecx
    jmp MimeMissingKb

.globl GetPackageDependencyInformation
.p2align 4
GetPackageDependencyInformation:
    mov $246, %ecx
    jmp MimeMissingKb

.globl GetPackageFamilyNameFromFilePath
.p2align 4
GetPackageFamilyNameFromFilePath:
    mov $247, %ecx
    jmp MimeMissingKb

.globl GetPackageFamilyNameFromProgId
.p2align 4
GetPackageFamilyNameFromProgId:
    mov $248, %ecx
    jmp MimeMissingKb

.globl GetPackageGlobalizationContext
.p2align 4
GetPackageGlobalizationContext:
    mov $249, %ecx
    jmp MimeMissingKb

.globl GetPackageGlobalizationProperty
.p2align 4
GetPackageGlobalizationProperty:
    mov $250, %ecx
    jmp MimeMissingKb

.globl GetPackageGraphRevisionId
.p2align 4
GetPackageGraphRevisionId:
    mov $251, %ecx
    jmp MimeMissingKb

.globl GetPackageInfo2
.p2align 4
GetPackageInfo2:
    mov $252, %ecx
    jmp MimeMissingKb

.globl GetPackageInfo3
.p2align 4
GetPackageInfo3:
    mov $253, %ecx
    jmp MimeMissingKb

.globl GetPackageNameAliasesByPackageFullName
.p2align 4
GetPackageNameAliasesByPackageFullName:
    mov $254, %ecx
    jmp MimeMissingKb

.globl GetPackageOSMaxVersionTested
.p2align 4
GetPackageOSMaxVersionTested:
    mov $255, %ecx
    jmp MimeMissingKb

.globl GetPackagePathByFullName2
.p2align 4
GetPackagePathByFullName2:
    mov $256, %ecx
    jmp MimeMissingKb

.globl GetPackagePathOnVolume
.p2align 4
GetPackagePathOnVolume:
    mov $257, %ecx
    jmp MimeMissingKb

.globl GetPackageProperty
.p2align 4
GetPackageProperty:
    mov $258, %ecx
    jmp MimeMissingKb

.globl GetPackagePropertyString
.p2align 4
GetPackagePropertyString:
    mov $259, %ecx
    jmp MimeMissingKb

.globl GetPackageResourcesContext
.p2align 4
GetPackageResourcesContext:
    mov $260, %ecx
    jmp MimeMissingKb

.globl GetPackageResourcesProperty
.p2align 4
GetPackageResourcesProperty:
    mov $261, %ecx
    jmp MimeMissingKb

.globl GetPackageSecurityContext
.p2align 4
GetPackageSecurityContext:
    mov $262, %ecx
    jmp MimeMissingKb

.globl GetPackageSecurityProperty
.p2align 4
GetPackageSecurityProperty:
    mov $263, %ecx
    jmp MimeMissingKb

.globl GetPackageStatus
.p2align 4
GetPackageStatus:
    mov $264, %ecx
    jmp MimeMissingKb

.globl GetPackageStatusForUser
.p2align 4
GetPackageStatusForUser:
    mov $265, %ecx
    jmp MimeMissingKb

.globl GetPackageStatusForUserSid
.p2align 4
GetPackageStatusForUserSid:
    mov $266, %ecx
    jmp MimeMissingKb

.globl GetPackageTargetPlatformProperty
.p2align 4
GetPackageTargetPlatformProperty:
    mov $267, %ecx
    jmp MimeMissingKb

.globl GetPackageVolumeSisPath
.p2align 4
GetPackageVolumeSisPath:
    mov $268, %ecx
    jmp MimeMissingKb

.globl GetPerformanceInfo
.p2align 4
GetPerformanceInfo:
    mov $269, %ecx
    jmp MimeMissingKb

.globl GetPersistedFileLocationW
.p2align 4
GetPersistedFileLocationW:
    mov $270, %ecx
    jmp MimeMissingKb

.globl GetPersistedRegistryLocationW
.p2align 4
GetPersistedRegistryLocationW:
    mov $271, %ecx
    jmp MimeMissingKb

.globl GetPersistedRegistryValueW
.p2align 4
GetPersistedRegistryValueW:
    mov $272, %ecx
    jmp MimeMissingKb

.globl GetPreviousFgPolicyRefreshInfoInternal
.p2align 4
GetPreviousFgPolicyRefreshInfoInternal:
    mov $273, %ecx
    jmp MimeMissingKb

.globl GetProcessDefaultCpuSetMasks
.p2align 4
GetProcessDefaultCpuSetMasks:
    mov $274, %ecx
    jmp MimeMissingKb

.globl GetProcessesUsingPackageDependency
.p2align 4
GetProcessesUsingPackageDependency:
    mov $276, %ecx
    jmp MimeMissingKb

.globl GetProcessImageFileNameA
.p2align 4
GetProcessImageFileNameA:
    mov $277, %ecx
    jmp MimeMissingKb

.globl GetProcessImageFileNameW
.p2align 4
GetProcessImageFileNameW:
    mov $278, %ecx
    jmp MimeMissingKb

.globl GetProcessorSystemCycleTime
.p2align 4
GetProcessorSystemCycleTime:
    mov $279, %ecx
    jmp MimeMissingKb

.globl GetProtocolAumid
.p2align 4
GetProtocolAumid:
    mov $280, %ecx
    jmp MimeMissingKb

.globl GetProtocolProperty
.p2align 4
GetProtocolProperty:
    mov $281, %ecx
    jmp MimeMissingKb

.globl GetPtrCalData
.p2align 4
GetPtrCalData:
    mov $282, %ecx
    jmp MimeMissingKb

.globl GetPtrCalDataArray
.p2align 4
GetPtrCalDataArray:
    mov $283, %ecx
    jmp MimeMissingKb

.globl GetPublisherCacheFolder
.p2align 4
GetPublisherCacheFolder:
    mov $284, %ecx
    jmp MimeMissingKb

.globl GetPublisherRootFolder
.p2align 4
GetPublisherRootFolder:
    mov $285, %ecx
    jmp MimeMissingKb

.globl GetRegistryExtensionFlags
.p2align 4
GetRegistryExtensionFlags:
    mov $286, %ecx
    jmp MimeMissingKb

.globl GetRegistryValueWithFallbackW
.p2align 4
GetRegistryValueWithFallbackW:
    mov $287, %ecx
    jmp MimeMissingKb

.globl GetResolvedPackageFullNameForPackageDependency
.p2align 4
GetResolvedPackageFullNameForPackageDependency:
    mov $288, %ecx
    jmp MimeMissingKb

.globl GetResolvedPackageFullNameForPackageDependency2
.p2align 4
GetResolvedPackageFullNameForPackageDependency2:
    mov $289, %ecx
    jmp MimeMissingKb

.globl GetRoamingLastObservedChangeTime
.p2align 4
GetRoamingLastObservedChangeTime:
    mov $290, %ecx
    jmp MimeMissingKb

.globl GetRuntimeAttestationReport
.p2align 4
GetRuntimeAttestationReport:
    mov $291, %ecx
    jmp MimeMissingKb

.globl GetSecureSystemAppDataFolder
.p2align 4
GetSecureSystemAppDataFolder:
    mov $292, %ecx
    jmp MimeMissingKb

.globl GetSerializedAtomBytes
.p2align 4
GetSerializedAtomBytes:
    mov $293, %ecx
    jmp MimeMissingKb

.globl GetSharedLocalFolder
.p2align 4
GetSharedLocalFolder:
    mov $294, %ecx
    jmp MimeMissingKb

.globl GetStagedPackageOrigin
.p2align 4
GetStagedPackageOrigin:
    mov $295, %ecx
    jmp MimeMissingKb

.globl GetStagedPackageOrigin2
.p2align 4
GetStagedPackageOrigin2:
    mov $296, %ecx
    jmp MimeMissingKb

.globl GetStagedPackagePathByFullName
.p2align 4
GetStagedPackagePathByFullName:
    mov $297, %ecx
    jmp MimeMissingKb

.globl GetStagedPackagePathByFullName2
.p2align 4
GetStagedPackagePathByFullName2:
    mov $298, %ecx
    jmp MimeMissingKb

.globl GetStateContainerDepth
.p2align 4
GetStateContainerDepth:
    mov $299, %ecx
    jmp MimeMissingKb

.globl GetStateFolder
.p2align 4
GetStateFolder:
    mov $300, %ecx
    jmp MimeMissingKb

.globl GetStateRootFolder
.p2align 4
GetStateRootFolder:
    mov $301, %ecx
    jmp MimeMissingKb

.globl GetStateRootFolderBase
.p2align 4
GetStateRootFolderBase:
    mov $302, %ecx
    jmp MimeMissingKb

.globl GetStateSettingsFolder
.p2align 4
GetStateSettingsFolder:
    mov $303, %ecx
    jmp MimeMissingKb

.globl GetStateVersion
.p2align 4
GetStateVersion:
    mov $304, %ecx
    jmp MimeMissingKb

.globl GetStringTableEntry
.p2align 4
GetStringTableEntry:
    mov $305, %ecx
    jmp MimeMissingKb

.globl GetSystemAppDataFolder
.p2align 4
GetSystemAppDataFolder:
    mov $306, %ecx
    jmp MimeMissingKb

.globl GetSystemAppDataKey
.p2align 4
GetSystemAppDataKey:
    mov $307, %ecx
    jmp MimeMissingKb

.globl GetSystemFileCacheSize
.p2align 4
GetSystemFileCacheSize:
    mov $309, %ecx
    jmp MimeMissingKb

.globl GetSystemLeapSecondInformation
.p2align 4
GetSystemLeapSecondInformation:
    mov $310, %ecx
    jmp MimeMissingKb

.globl GetSystemMetadataPath
.p2align 4
GetSystemMetadataPath:
    mov $311, %ecx
    jmp MimeMissingKb

.globl GetSystemMetadataPathForPackage
.p2align 4
GetSystemMetadataPathForPackage:
    mov $312, %ecx
    jmp MimeMissingKb

.globl GetSystemMetadataPathForPackageFamily
.p2align 4
GetSystemMetadataPathForPackageFamily:
    mov $313, %ecx
    jmp MimeMissingKb

.globl GetSystemStateRootFolder
.p2align 4
GetSystemStateRootFolder:
    mov $314, %ecx
    jmp MimeMissingKb

.globl GetSystemTimeAdjustmentPrecise
.p2align 4
GetSystemTimeAdjustmentPrecise:
    mov $315, %ecx
    jmp MimeMissingKb

.globl GetSystemWow64Directory2A
.p2align 4
GetSystemWow64Directory2A:
    mov $316, %ecx
    jmp MimeMissingKb

.globl GetTargetPlatformContext
.p2align 4
GetTargetPlatformContext:
    mov $317, %ecx
    jmp MimeMissingKb

.globl GetThreadEnabledXStateFeatures
.p2align 4
GetThreadEnabledXStateFeatures:
    mov $318, %ecx
    jmp MimeMissingKb

.globl GetThreadSelectedCpuSetMasks
.p2align 4
GetThreadSelectedCpuSetMasks:
    mov $319, %ecx
    jmp MimeMissingKb

.globl GetThreadSelectedCpuSets
.p2align 4
GetThreadSelectedCpuSets:
    mov $320, %ecx
    jmp MimeMissingKb

.globl GetUILanguageInfo
.p2align 4
GetUILanguageInfo:
    mov $321, %ecx
    jmp MimeMissingKb

.globl GetUnicodeStringToEightBitSizeRoutine
.p2align 4
GetUnicodeStringToEightBitSizeRoutine:
    mov $322, %ecx
    jmp MimeMissingKb

.globl GetUnicodeStringToEightBitStringRoutine
.p2align 4
GetUnicodeStringToEightBitStringRoutine:
    mov $323, %ecx
    jmp MimeMissingKb

.globl GetUserInfo
.p2align 4
GetUserInfo:
    mov $324, %ecx
    jmp MimeMissingKb

.globl GetUserInfoWord
.p2align 4
GetUserInfoWord:
    mov $325, %ecx
    jmp MimeMissingKb

.globl GetUserOverrideString
.p2align 4
GetUserOverrideString:
    mov $326, %ecx
    jmp MimeMissingKb

.globl GetUserOverrideWord
.p2align 4
GetUserOverrideWord:
    mov $327, %ecx
    jmp MimeMissingKb

.globl GetWsChanges
.p2align 4
GetWsChanges:
    mov $328, %ecx
    jmp MimeMissingKb

.globl GetWsChangesEx
.p2align 4
GetWsChangesEx:
    mov $329, %ecx
    jmp MimeMissingKb

.globl GetXStateFeaturesMask
.p2align 4
GetXStateFeaturesMask:
    mov $330, %ecx
    jmp MimeMissingKb

.globl GuardCheckLongJumpTarget
.p2align 4
GuardCheckLongJumpTarget:
    mov $331, %ecx
    jmp MimeMissingKb

.globl HasPolicyForegroundProcessingCompletedInternal
.p2align 4
HasPolicyForegroundProcessingCompletedInternal:
    mov $332, %ecx
    jmp MimeMissingKb

.globl hgets
.p2align 4
hgets:
    mov $333, %ecx
    jmp MimeMissingKb

.globl hwprintf
.p2align 4
hwprintf:
    mov $334, %ecx
    jmp MimeMissingKb

.globl IncrementPackageStatusVersion
.p2align 4
IncrementPackageStatusVersion:
    mov $335, %ecx
    jmp MimeMissingKb

.globl InitializeContext2
.p2align 4
InitializeContext2:
    mov $336, %ecx
    jmp MimeMissingKb

.globl InitializeEnclave
.p2align 4
InitializeEnclave:
    mov $337, %ecx
    jmp MimeMissingKb

.globl InitializeProcessForWsWatch
.p2align 4
InitializeProcessForWsWatch:
    mov $338, %ecx
    jmp MimeMissingKb

.globl _initterm
.p2align 4
_initterm:
    mov $339, %ecx
    jmp MimeMissingKb

.globl _initterm_e
.p2align 4
_initterm_e:
    mov $340, %ecx
    jmp MimeMissingKb

.globl InstallELAMCertificateInfo
.p2align 4
InstallELAMCertificateInfo:
    mov $341, %ecx
    jmp MimeMissingKb

.globl Internal_EnumCalendarInfo
.p2align 4
Internal_EnumCalendarInfo:
    mov $342, %ecx
    jmp MimeMissingKb

.globl Internal_EnumDateFormats
.p2align 4
Internal_EnumDateFormats:
    mov $343, %ecx
    jmp MimeMissingKb

.globl Internal_EnumLanguageGroupLocales
.p2align 4
Internal_EnumLanguageGroupLocales:
    mov $344, %ecx
    jmp MimeMissingKb

.globl Internal_EnumSystemCodePages
.p2align 4
Internal_EnumSystemCodePages:
    mov $345, %ecx
    jmp MimeMissingKb

.globl Internal_EnumSystemLanguageGroups
.p2align 4
Internal_EnumSystemLanguageGroups:
    mov $346, %ecx
    jmp MimeMissingKb

.globl Internal_EnumSystemLocales
.p2align 4
Internal_EnumSystemLocales:
    mov $347, %ecx
    jmp MimeMissingKb

.globl Internal_EnumTimeFormats
.p2align 4
Internal_EnumTimeFormats:
    mov $348, %ecx
    jmp MimeMissingKb

.globl Internal_EnumUILanguages
.p2align 4
Internal_EnumUILanguages:
    mov $349, %ecx
    jmp MimeMissingKb

.globl InternalLcidToName
.p2align 4
InternalLcidToName:
    mov $350, %ecx
    jmp MimeMissingKb

.globl InvalidateAppModelVersionCache
.p2align 4
InvalidateAppModelVersionCache:
    mov $351, %ecx
    jmp MimeMissingKb

.globl _invalid_parameter
.p2align 4
_invalid_parameter:
    mov $352, %ecx
    jmp MimeMissingKb

.globl IsDeveloperModeEnabled
.p2align 4
IsDeveloperModeEnabled:
    mov $353, %ecx
    jmp MimeMissingKb

.globl IsDeveloperModePolicyApplied
.p2align 4
IsDeveloperModePolicyApplied:
    mov $354, %ecx
    jmp MimeMissingKb

.globl IsEnclaveTypeSupported
.p2align 4
IsEnclaveTypeSupported:
    mov $355, %ecx
    jmp MimeMissingKb

.globl IsIoRingOpSupported
.p2align 4
IsIoRingOpSupported:
    mov $356, %ecx
    jmp MimeMissingKb

.globl IsMrtResourceRedirectionEnabled
.p2align 4
IsMrtResourceRedirectionEnabled:
    mov $357, %ecx
    jmp MimeMissingKb

.globl IsOnDemandRegistrationSupportedForExtensionCategory
.p2align 4
IsOnDemandRegistrationSupportedForExtensionCategory:
    mov $358, %ecx
    jmp MimeMissingKb

.globl IsPackageFeatureSupported
.p2align 4
IsPackageFeatureSupported:
    mov $359, %ecx
    jmp MimeMissingKb

.globl IsSideloadingEnabled
.p2align 4
IsSideloadingEnabled:
    mov $360, %ecx
    jmp MimeMissingKb

.globl IsSideloadingPolicyApplied
.p2align 4
IsSideloadingPolicyApplied:
    mov $361, %ecx
    jmp MimeMissingKb

.globl IsSyncForegroundPolicyRefresh
.p2align 4
IsSyncForegroundPolicyRefresh:
    mov $362, %ecx
    jmp MimeMissingKb

.globl IsTimeZoneRedirectionEnabled
.p2align 4
IsTimeZoneRedirectionEnabled:
    mov $363, %ecx
    jmp MimeMissingKb

.globl IsUserCetAvailableInEnvironment
.p2align 4
IsUserCetAvailableInEnvironment:
    mov $364, %ecx
    jmp MimeMissingKb

.globl IsValidNLSVersion
.p2align 4
IsValidNLSVersion:
    mov $365, %ecx
    jmp MimeMissingKb

.globl IsWow64GuestMachineSupported
.p2align 4
IsWow64GuestMachineSupported:
    mov $366, %ecx
    jmp MimeMissingKb

.globl KernelBaseGetGlobalData
.p2align 4
KernelBaseGetGlobalData:
    mov $367, %ecx
    jmp MimeMissingKb

.globl LeaveCriticalPolicySectionInternal
.p2align 4
LeaveCriticalPolicySectionInternal:
    mov $369, %ecx
    jmp MimeMissingKb

.globl LoadAppInitDlls
.p2align 4
LoadAppInitDlls:
    mov $370, %ecx
    jmp MimeMissingKb

.globl LoadEnclaveData
.p2align 4
LoadEnclaveData:
    mov $371, %ecx
    jmp MimeMissingKb

.globl LoadEnclaveImageA
.p2align 4
LoadEnclaveImageA:
    mov $372, %ecx
    jmp MimeMissingKb

.globl LoadEnclaveImageW
.p2align 4
LoadEnclaveImageW:
    mov $373, %ecx
    jmp MimeMissingKb

.globl LoadStringBaseExW
.p2align 4
LoadStringBaseExW:
    mov $374, %ecx
    jmp MimeMissingKb

.globl LoadStringByReference
.p2align 4
LoadStringByReference:
    mov $375, %ecx
    jmp MimeMissingKb

.globl LocalFileTimeToLocalSystemTime
.p2align 4
LocalFileTimeToLocalSystemTime:
    mov $376, %ecx
    jmp MimeMissingKb

.globl LocalSystemTimeToLocalFileTime
.p2align 4
LocalSystemTimeToLocalFileTime:
    mov $377, %ecx
    jmp MimeMissingKb

.globl _local_unwind
.p2align 4
_local_unwind:
    mov $378, %ecx
    jmp MimeMissingKb

.globl LogFeatureProcessUsage
.p2align 4
LogFeatureProcessUsage:
    mov $379, %ecx
    jmp MimeMissingKb

.globl LogStagedFeatureUsage
.p2align 4
LogStagedFeatureUsage:
    mov $380, %ecx
    jmp MimeMissingKb

.globl lstrcmp
.p2align 4
lstrcmp:
    mov $381, %ecx
    jmp MimeMissingKb

.globl lstrcmpi
.p2align 4
lstrcmpi:
    mov $382, %ecx
    jmp MimeMissingKb

.globl lstrcpyn
.p2align 4
lstrcpyn:
    mov $383, %ecx
    jmp MimeMissingKb

.globl lstrlen
.p2align 4
lstrlen:
    mov $384, %ecx
    jmp MimeMissingKb

.globl MapPredefinedHandleInternal
.p2align 4
MapPredefinedHandleInternal:
    mov $385, %ecx
    jmp MimeMissingKb

.globl __misaligned_access
.p2align 4
__misaligned_access:
    mov $386, %ecx
    jmp MimeMissingKb

.globl MoveFileFromAppW
.p2align 4
MoveFileFromAppW:
    mov $387, %ecx
    jmp MimeMissingKb

.globl MoveFileWithProgressTransactedW
.p2align 4
MoveFileWithProgressTransactedW:
    mov $388, %ecx
    jmp MimeMissingKb

.globl MsixIsSystemPackageByAppUserModelId
.p2align 4
MsixIsSystemPackageByAppUserModelId:
    mov $389, %ecx
    jmp MimeMissingKb

.globl MsixIsSystemPackageByPackageFullName
.p2align 4
MsixIsSystemPackageByPackageFullName:
    mov $390, %ecx
    jmp MimeMissingKb

.globl NamedPipeEventEnum
.p2align 4
NamedPipeEventEnum:
    mov $391, %ecx
    jmp MimeMissingKb

.globl NamedPipeEventSelect
.p2align 4
NamedPipeEventSelect:
    mov $392, %ecx
    jmp MimeMissingKb

.globl NlsCheckPolicy
.p2align 4
NlsCheckPolicy:
    mov $393, %ecx
    jmp MimeMissingKb

.globl NlsDispatchAnsiEnumProc
.p2align 4
NlsDispatchAnsiEnumProc:
    mov $394, %ecx
    jmp MimeMissingKb

.globl NlsGetACPFromLocale
.p2align 4
NlsGetACPFromLocale:
    mov $395, %ecx
    jmp MimeMissingKb

.globl NlsIsUserDefaultLocale
.p2align 4
NlsIsUserDefaultLocale:
    mov $396, %ecx
    jmp MimeMissingKb

.globl NlsUpdateLocale
.p2align 4
NlsUpdateLocale:
    mov $397, %ecx
    jmp MimeMissingKb

.globl NlsUpdateSystemLocale
.p2align 4
NlsUpdateSystemLocale:
    mov $398, %ecx
    jmp MimeMissingKb

.globl NlsValidateLocale
.p2align 4
NlsValidateLocale:
    mov $399, %ecx
    jmp MimeMissingKb

.globl NotifyFeatureToggleUsage
.p2align 4
NotifyFeatureToggleUsage:
    mov $400, %ecx
    jmp MimeMissingKb

.globl NotifyMountMgr
.p2align 4
NotifyMountMgr:
    mov $401, %ecx
    jmp MimeMissingKb

.globl NotifyRedirectedStringChange
.p2align 4
NotifyRedirectedStringChange:
    mov $402, %ecx
    jmp MimeMissingKb

.globl NTDLL.__chkstk
.p2align 4
NTDLL.__chkstk:
    mov $403, %ecx
    jmp MimeMissingKb

.globl NTDLL.__C_specific_handler
.p2align 4
NTDLL.__C_specific_handler:
    mov $404, %ecx
    jmp MimeMissingKb

.globl NTDLL.EtwEventActivityIdControl
.p2align 4
NTDLL.EtwEventActivityIdControl:
    mov $405, %ecx
    jmp MimeMissingKb

.globl NTDLL.EtwEventEnabled
.p2align 4
NTDLL.EtwEventEnabled:
    mov $406, %ecx
    jmp MimeMissingKb

.globl NTDLL.EtwEventProviderEnabled
.p2align 4
NTDLL.EtwEventProviderEnabled:
    mov $407, %ecx
    jmp MimeMissingKb

.globl NTDLL.EtwEventRegister
.p2align 4
NTDLL.EtwEventRegister:
    mov $408, %ecx
    jmp MimeMissingKb

.globl NTDLL.EtwEventSetInformation
.p2align 4
NTDLL.EtwEventSetInformation:
    mov $409, %ecx
    jmp MimeMissingKb

.globl NTDLL.EtwEventUnregister
.p2align 4
NTDLL.EtwEventUnregister:
    mov $410, %ecx
    jmp MimeMissingKb

.globl NTDLL.EtwEventWrite
.p2align 4
NTDLL.EtwEventWrite:
    mov $411, %ecx
    jmp MimeMissingKb

.globl NTDLL.EtwEventWriteEx
.p2align 4
NTDLL.EtwEventWriteEx:
    mov $412, %ecx
    jmp MimeMissingKb

.globl NTDLL.EtwEventWriteString
.p2align 4
NTDLL.EtwEventWriteString:
    mov $413, %ecx
    jmp MimeMissingKb

.globl NTDLL.EtwEventWriteTransfer
.p2align 4
NTDLL.EtwEventWriteTransfer:
    mov $414, %ecx
    jmp MimeMissingKb

.globl NTDLL.EtwGetTraceEnableFlags
.p2align 4
NTDLL.EtwGetTraceEnableFlags:
    mov $415, %ecx
    jmp MimeMissingKb

.globl NTDLL.EtwGetTraceEnableLevel
.p2align 4
NTDLL.EtwGetTraceEnableLevel:
    mov $416, %ecx
    jmp MimeMissingKb

.globl NTDLL.EtwGetTraceLoggerHandle
.p2align 4
NTDLL.EtwGetTraceLoggerHandle:
    mov $417, %ecx
    jmp MimeMissingKb

.globl NTDLL.EtwLogTraceEvent
.p2align 4
NTDLL.EtwLogTraceEvent:
    mov $418, %ecx
    jmp MimeMissingKb

.globl NTDLL.EtwRegisterTraceGuidsW
.p2align 4
NTDLL.EtwRegisterTraceGuidsW:
    mov $419, %ecx
    jmp MimeMissingKb

.globl NTDLL.EtwTraceMessage
.p2align 4
NTDLL.EtwTraceMessage:
    mov $420, %ecx
    jmp MimeMissingKb

.globl NTDLL.EtwTraceMessageVa
.p2align 4
NTDLL.EtwTraceMessageVa:
    mov $421, %ecx
    jmp MimeMissingKb

.globl NTDLL.EtwUnregisterTraceGuids
.p2align 4
NTDLL.EtwUnregisterTraceGuids:
    mov $422, %ecx
    jmp MimeMissingKb

.globl NTDLL._local_unwind
.p2align 4
NTDLL._local_unwind:
    mov $423, %ecx
    jmp MimeMissingKb

.globl NTDLL.__misaligned_access
.p2align 4
NTDLL.__misaligned_access:
    mov $424, %ecx
    jmp MimeMissingKb

.globl NTDLL.NtFlushProcessWriteBuffers
.p2align 4
NTDLL.NtFlushProcessWriteBuffers:
    mov $425, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlAcquireSRWLockExclusive
.p2align 4
NTDLL.RtlAcquireSRWLockExclusive:
    mov $426, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlAcquireSRWLockShared
.p2align 4
NTDLL.RtlAcquireSRWLockShared:
    mov $427, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlAllocateHeap
.p2align 4
NTDLL.RtlAllocateHeap:
    mov $428, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlCopyMemoryNonTemporal
.p2align 4
NTDLL.RtlCopyMemoryNonTemporal:
    mov $429, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlDecodePointer
.p2align 4
NTDLL.RtlDecodePointer:
    mov $430, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlDecodeRemotePointer
.p2align 4
NTDLL.RtlDecodeRemotePointer:
    mov $431, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlDecodeSystemPointer
.p2align 4
NTDLL.RtlDecodeSystemPointer:
    mov $432, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlDeleteCriticalSection
.p2align 4
NTDLL.RtlDeleteCriticalSection:
    mov $433, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlEncodePointer
.p2align 4
NTDLL.RtlEncodePointer:
    mov $434, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlEncodeRemotePointer
.p2align 4
NTDLL.RtlEncodeRemotePointer:
    mov $435, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlEncodeSystemPointer
.p2align 4
NTDLL.RtlEncodeSystemPointer:
    mov $436, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlEnterCriticalSection
.p2align 4
NTDLL.RtlEnterCriticalSection:
    mov $437, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlExitUserProcess
.p2align 4
NTDLL.RtlExitUserProcess:
    mov $438, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlExitUserThread
.p2align 4
NTDLL.RtlExitUserThread:
    mov $439, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlFlsGetValue2
.p2align 4
NTDLL.RtlFlsGetValue2:
    mov $440, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlFreeHeap
.p2align 4
NTDLL.RtlFreeHeap:
    mov $441, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlGetCurrentProcessorNumber
.p2align 4
NTDLL.RtlGetCurrentProcessorNumber:
    mov $442, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlGetCurrentProcessorNumberEx
.p2align 4
NTDLL.RtlGetCurrentProcessorNumberEx:
    mov $443, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlGetFeatureToggleConfiguration
.p2align 4
NTDLL.RtlGetFeatureToggleConfiguration:
    mov $444, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlGetFeatureTogglesChangeToken
.p2align 4
NTDLL.RtlGetFeatureTogglesChangeToken:
    mov $445, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlInitializeConditionVariable
.p2align 4
NTDLL.RtlInitializeConditionVariable:
    mov $446, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlInitializeCriticalSection
.p2align 4
NTDLL.RtlInitializeCriticalSection:
    mov $447, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlInitializeSListHead
.p2align 4
NTDLL.RtlInitializeSListHead:
    mov $448, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlInitializeSRWLock
.p2align 4
NTDLL.RtlInitializeSRWLock:
    mov $449, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlInterlockedFlushSList
.p2align 4
NTDLL.RtlInterlockedFlushSList:
    mov $450, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlInterlockedPopEntrySList
.p2align 4
NTDLL.RtlInterlockedPopEntrySList:
    mov $451, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlInterlockedPushEntrySList
.p2align 4
NTDLL.RtlInterlockedPushEntrySList:
    mov $452, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlInterlockedPushListSList
.p2align 4
NTDLL.RtlInterlockedPushListSList:
    mov $453, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlInterlockedPushListSListEx
.p2align 4
NTDLL.RtlInterlockedPushListSListEx:
    mov $454, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlLeaveCriticalSection
.p2align 4
NTDLL.RtlLeaveCriticalSection:
    mov $455, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlNotifyFeatureToggleUsage
.p2align 4
NTDLL.RtlNotifyFeatureToggleUsage:
    mov $456, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlQueryDepthSList
.p2align 4
NTDLL.RtlQueryDepthSList:
    mov $457, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlQueryPerformanceCounter
.p2align 4
NTDLL.RtlQueryPerformanceCounter:
    mov $458, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlQueryPerformanceFrequency
.p2align 4
NTDLL.RtlQueryPerformanceFrequency:
    mov $459, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlQueryUnbiasedInterruptTime
.p2align 4
NTDLL.RtlQueryUnbiasedInterruptTime:
    mov $460, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlReAllocateHeap
.p2align 4
NTDLL.RtlReAllocateHeap:
    mov $461, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlReleaseSRWLockExclusive
.p2align 4
NTDLL.RtlReleaseSRWLockExclusive:
    mov $462, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlReleaseSRWLockShared
.p2align 4
NTDLL.RtlReleaseSRWLockShared:
    mov $463, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlRestoreLastWin32Error
.p2align 4
NTDLL.RtlRestoreLastWin32Error:
    mov $464, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlRunOnceInitialize
.p2align 4
NTDLL.RtlRunOnceInitialize:
    mov $465, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlSetCriticalSectionSpinCount
.p2align 4
NTDLL.RtlSetCriticalSectionSpinCount:
    mov $466, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlSetLastWin32Error
.p2align 4
NTDLL.RtlSetLastWin32Error:
    mov $467, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlSizeHeap
.p2align 4
NTDLL.RtlSizeHeap:
    mov $468, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlTryAcquireSRWLockExclusive
.p2align 4
NTDLL.RtlTryAcquireSRWLockExclusive:
    mov $469, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlTryAcquireSRWLockShared
.p2align 4
NTDLL.RtlTryAcquireSRWLockShared:
    mov $470, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlTryEnterCriticalSection
.p2align 4
NTDLL.RtlTryEnterCriticalSection:
    mov $471, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlWakeAddressAll
.p2align 4
NTDLL.RtlWakeAddressAll:
    mov $472, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlWakeAddressSingle
.p2align 4
NTDLL.RtlWakeAddressSingle:
    mov $473, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlWakeAllConditionVariable
.p2align 4
NTDLL.RtlWakeAllConditionVariable:
    mov $474, %ecx
    jmp MimeMissingKb

.globl NTDLL.RtlWakeConditionVariable
.p2align 4
NTDLL.RtlWakeConditionVariable:
    mov $475, %ecx
    jmp MimeMissingKb

.globl NTDLL.TpCallbackLeaveCriticalSectionOnCompletion
.p2align 4
NTDLL.TpCallbackLeaveCriticalSectionOnCompletion:
    mov $476, %ecx
    jmp MimeMissingKb

.globl NTDLL.TpCallbackReleaseMutexOnCompletion
.p2align 4
NTDLL.TpCallbackReleaseMutexOnCompletion:
    mov $477, %ecx
    jmp MimeMissingKb

.globl NTDLL.TpCallbackReleaseSemaphoreOnCompletion
.p2align 4
NTDLL.TpCallbackReleaseSemaphoreOnCompletion:
    mov $478, %ecx
    jmp MimeMissingKb

.globl NTDLL.TpCallbackSetEventOnCompletion
.p2align 4
NTDLL.TpCallbackSetEventOnCompletion:
    mov $479, %ecx
    jmp MimeMissingKb

.globl NTDLL.TpCallbackUnloadDllOnCompletion
.p2align 4
NTDLL.TpCallbackUnloadDllOnCompletion:
    mov $480, %ecx
    jmp MimeMissingKb

.globl NTDLL.TpCancelAsyncIoOperation
.p2align 4
NTDLL.TpCancelAsyncIoOperation:
    mov $481, %ecx
    jmp MimeMissingKb

.globl NTDLL.TpDisassociateCallback
.p2align 4
NTDLL.TpDisassociateCallback:
    mov $482, %ecx
    jmp MimeMissingKb

.globl NTDLL.TpIsTimerSet
.p2align 4
NTDLL.TpIsTimerSet:
    mov $483, %ecx
    jmp MimeMissingKb

.globl NTDLL.TpPostWork
.p2align 4
NTDLL.TpPostWork:
    mov $484, %ecx
    jmp MimeMissingKb

.globl NTDLL.TpReleaseCleanupGroup
.p2align 4
NTDLL.TpReleaseCleanupGroup:
    mov $485, %ecx
    jmp MimeMissingKb

.globl NTDLL.TpReleaseCleanupGroupMembers
.p2align 4
NTDLL.TpReleaseCleanupGroupMembers:
    mov $486, %ecx
    jmp MimeMissingKb

.globl NTDLL.TpReleaseIoCompletion
.p2align 4
NTDLL.TpReleaseIoCompletion:
    mov $487, %ecx
    jmp MimeMissingKb

.globl NTDLL.TpReleasePool
.p2align 4
NTDLL.TpReleasePool:
    mov $488, %ecx
    jmp MimeMissingKb

.globl NTDLL.TpReleaseTimer
.p2align 4
NTDLL.TpReleaseTimer:
    mov $489, %ecx
    jmp MimeMissingKb

.globl NTDLL.TpReleaseWait
.p2align 4
NTDLL.TpReleaseWait:
    mov $490, %ecx
    jmp MimeMissingKb

.globl NTDLL.TpReleaseWork
.p2align 4
NTDLL.TpReleaseWork:
    mov $491, %ecx
    jmp MimeMissingKb

.globl NTDLL.TpSetPoolMaxThreads
.p2align 4
NTDLL.TpSetPoolMaxThreads:
    mov $492, %ecx
    jmp MimeMissingKb

.globl NTDLL.TpSetTimer
.p2align 4
NTDLL.TpSetTimer:
    mov $493, %ecx
    jmp MimeMissingKb

.globl NTDLL.TpSetTimerEx
.p2align 4
NTDLL.TpSetTimerEx:
    mov $494, %ecx
    jmp MimeMissingKb

.globl NTDLL.TpSetWait
.p2align 4
NTDLL.TpSetWait:
    mov $495, %ecx
    jmp MimeMissingKb

.globl NTDLL.TpSetWaitEx
.p2align 4
NTDLL.TpSetWaitEx:
    mov $496, %ecx
    jmp MimeMissingKb

.globl NTDLL.TpStartAsyncIoOperation
.p2align 4
NTDLL.TpStartAsyncIoOperation:
    mov $497, %ecx
    jmp MimeMissingKb

.globl NTDLL.TpWaitForIoCompletion
.p2align 4
NTDLL.TpWaitForIoCompletion:
    mov $498, %ecx
    jmp MimeMissingKb

.globl NTDLL.TpWaitForTimer
.p2align 4
NTDLL.TpWaitForTimer:
    mov $499, %ecx
    jmp MimeMissingKb

.globl NTDLL.TpWaitForWait
.p2align 4
NTDLL.TpWaitForWait:
    mov $500, %ecx
    jmp MimeMissingKb

.globl NTDLL.TpWaitForWork
.p2align 4
NTDLL.TpWaitForWork:
    mov $501, %ecx
    jmp MimeMissingKb

.globl NTDLL.VerSetConditionMask
.p2align 4
NTDLL.VerSetConditionMask:
    mov $502, %ecx
    jmp MimeMissingKb

.globl _onexit
.p2align 4
_onexit:
    mov $503, %ecx
    jmp MimeMissingKb

.globl OpenCommPort
.p2align 4
OpenCommPort:
    mov $504, %ecx
    jmp MimeMissingKb

.globl OpenDedicatedMemoryPartition
.p2align 4
OpenDedicatedMemoryPartition:
    mov $505, %ecx
    jmp MimeMissingKb

.globl OpenGlobalizationUserSettingsKey
.p2align 4
OpenGlobalizationUserSettingsKey:
    mov $506, %ecx
    jmp MimeMissingKb

.globl _OpenMuiStringCache
.p2align 4
_OpenMuiStringCache:
    mov $507, %ecx
    jmp MimeMissingKb

.globl OpenPackageInfoByFullNameForMachine
.p2align 4
OpenPackageInfoByFullNameForMachine:
    mov $508, %ecx
    jmp MimeMissingKb

.globl OpenPackageInfoByFullNameForUser
.p2align 4
OpenPackageInfoByFullNameForUser:
    mov $509, %ecx
    jmp MimeMissingKb

.globl OpenRegKey
.p2align 4
OpenRegKey:
    mov $510, %ecx
    jmp MimeMissingKb

.globl OpenState
.p2align 4
OpenState:
    mov $511, %ecx
    jmp MimeMissingKb

.globl OpenStateAtom
.p2align 4
OpenStateAtom:
    mov $512, %ecx
    jmp MimeMissingKb

.globl OpenStateExplicit
.p2align 4
OpenStateExplicit:
    mov $513, %ecx
    jmp MimeMissingKb

.globl OpenStateExplicitForUserSid
.p2align 4
OpenStateExplicitForUserSid:
    mov $514, %ecx
    jmp MimeMissingKb

.globl OpenStateExplicitForUserSidString
.p2align 4
OpenStateExplicitForUserSidString:
    mov $515, %ecx
    jmp MimeMissingKb

.globl OverrideRoamingDataModificationTimesInRange
.p2align 4
OverrideRoamingDataModificationTimesInRange:
    mov $516, %ecx
    jmp MimeMissingKb

.globl PackageFamilyNameFromFullNameA
.p2align 4
PackageFamilyNameFromFullNameA:
    mov $517, %ecx
    jmp MimeMissingKb

.globl PackageFamilyNameFromId
.p2align 4
PackageFamilyNameFromId:
    mov $518, %ecx
    jmp MimeMissingKb

.globl PackageFamilyNameFromIdA
.p2align 4
PackageFamilyNameFromIdA:
    mov $519, %ecx
    jmp MimeMissingKb

.globl PackageFamilyNameFromProductId
.p2align 4
PackageFamilyNameFromProductId:
    mov $520, %ecx
    jmp MimeMissingKb

.globl PackageFullNameFromId
.p2align 4
PackageFullNameFromId:
    mov $521, %ecx
    jmp MimeMissingKb

.globl PackageFullNameFromIdA
.p2align 4
PackageFullNameFromIdA:
    mov $522, %ecx
    jmp MimeMissingKb

.globl PackageFullNameFromProductId
.p2align 4
PackageFullNameFromProductId:
    mov $523, %ecx
    jmp MimeMissingKb

.globl PackageIdFromFullNameA
.p2align 4
PackageIdFromFullNameA:
    mov $524, %ecx
    jmp MimeMissingKb

.globl PackageIdFromProductId
.p2align 4
PackageIdFromProductId:
    mov $525, %ecx
    jmp MimeMissingKb

.globl PackageIsEffectiveSupportedUsersMultiple
.p2align 4
PackageIsEffectiveSupportedUsersMultiple:
    mov $526, %ecx
    jmp MimeMissingKb

.globl PackageNameAndPublisherIdFromFamilyName
.p2align 4
PackageNameAndPublisherIdFromFamilyName:
    mov $527, %ecx
    jmp MimeMissingKb

.globl PackageNameAndPublisherIdFromFamilyNameA
.p2align 4
PackageNameAndPublisherIdFromFamilyNameA:
    mov $528, %ecx
    jmp MimeMissingKb

.globl PackagePublisherAddUserIfNecessary
.p2align 4
PackagePublisherAddUserIfNecessary:
    mov $529, %ecx
    jmp MimeMissingKb

.globl PackagePublisherIdFromPublisher
.p2align 4
PackagePublisherIdFromPublisher:
    mov $530, %ecx
    jmp MimeMissingKb

.globl PackageRelativeApplicationIdFromProductId
.p2align 4
PackageRelativeApplicationIdFromProductId:
    mov $531, %ecx
    jmp MimeMissingKb

.globl PackageSidFromFamilyName
.p2align 4
PackageSidFromFamilyName:
    mov $532, %ecx
    jmp MimeMissingKb

.globl PackageSidFromProductId
.p2align 4
PackageSidFromProductId:
    mov $533, %ecx
    jmp MimeMissingKb

.globl ParseApplicationUserModelId
.p2align 4
ParseApplicationUserModelId:
    mov $534, %ecx
    jmp MimeMissingKb

.globl ParseApplicationUserModelIdA
.p2align 4
ParseApplicationUserModelIdA:
    mov $535, %ecx
    jmp MimeMissingKb

.globl PcwAddQueryItem
.p2align 4
PcwAddQueryItem:
    mov $536, %ecx
    jmp MimeMissingKb

.globl PcwClearCounterSetSecurity
.p2align 4
PcwClearCounterSetSecurity:
    mov $537, %ecx
    jmp MimeMissingKb

.globl PcwCollectData
.p2align 4
PcwCollectData:
    mov $538, %ecx
    jmp MimeMissingKb

.globl PcwCompleteNotification
.p2align 4
PcwCompleteNotification:
    mov $539, %ecx
    jmp MimeMissingKb

.globl PcwCreateNotifier
.p2align 4
PcwCreateNotifier:
    mov $540, %ecx
    jmp MimeMissingKb

.globl PcwCreateQuery
.p2align 4
PcwCreateQuery:
    mov $541, %ecx
    jmp MimeMissingKb

.globl PcwDisconnectCounterSet
.p2align 4
PcwDisconnectCounterSet:
    mov $542, %ecx
    jmp MimeMissingKb

.globl PcwEnumerateInstances
.p2align 4
PcwEnumerateInstances:
    mov $543, %ecx
    jmp MimeMissingKb

.globl PcwIsNotifierAlive
.p2align 4
PcwIsNotifierAlive:
    mov $544, %ecx
    jmp MimeMissingKb

.globl PcwQueryCounterSetSecurity
.p2align 4
PcwQueryCounterSetSecurity:
    mov $545, %ecx
    jmp MimeMissingKb

.globl PcwReadNotificationData
.p2align 4
PcwReadNotificationData:
    mov $546, %ecx
    jmp MimeMissingKb

.globl PcwRegisterCounterSet
.p2align 4
PcwRegisterCounterSet:
    mov $547, %ecx
    jmp MimeMissingKb

.globl PcwRemoveQueryItem
.p2align 4
PcwRemoveQueryItem:
    mov $548, %ecx
    jmp MimeMissingKb

.globl PcwSendNotification
.p2align 4
PcwSendNotification:
    mov $549, %ecx
    jmp MimeMissingKb

.globl PcwSendStatelessNotification
.p2align 4
PcwSendStatelessNotification:
    mov $550, %ecx
    jmp MimeMissingKb

.globl PcwSetCounterSetSecurity
.p2align 4
PcwSetCounterSetSecurity:
    mov $551, %ecx
    jmp MimeMissingKb

.globl PcwSetQueryItemUserData
.p2align 4
PcwSetQueryItemUserData:
    mov $552, %ecx
    jmp MimeMissingKb

.globl PerfCreateInstance
.p2align 4
PerfCreateInstance:
    mov $553, %ecx
    jmp MimeMissingKb

.globl PerfDecrementULongCounterValue
.p2align 4
PerfDecrementULongCounterValue:
    mov $554, %ecx
    jmp MimeMissingKb

.globl PerfDecrementULongLongCounterValue
.p2align 4
PerfDecrementULongLongCounterValue:
    mov $555, %ecx
    jmp MimeMissingKb

.globl PerfDeleteInstance
.p2align 4
PerfDeleteInstance:
    mov $556, %ecx
    jmp MimeMissingKb

.globl PerfIncrementULongCounterValue
.p2align 4
PerfIncrementULongCounterValue:
    mov $557, %ecx
    jmp MimeMissingKb

.globl PerfIncrementULongLongCounterValue
.p2align 4
PerfIncrementULongLongCounterValue:
    mov $558, %ecx
    jmp MimeMissingKb

.globl PerfQueryInstance
.p2align 4
PerfQueryInstance:
    mov $559, %ecx
    jmp MimeMissingKb

.globl PerfSetCounterRefValue
.p2align 4
PerfSetCounterRefValue:
    mov $560, %ecx
    jmp MimeMissingKb

.globl PerfSetCounterSetInfo
.p2align 4
PerfSetCounterSetInfo:
    mov $561, %ecx
    jmp MimeMissingKb

.globl PerfSetULongCounterValue
.p2align 4
PerfSetULongCounterValue:
    mov $562, %ecx
    jmp MimeMissingKb

.globl PerfSetULongLongCounterValue
.p2align 4
PerfSetULongLongCounterValue:
    mov $563, %ecx
    jmp MimeMissingKb

.globl PerfStartProvider
.p2align 4
PerfStartProvider:
    mov $564, %ecx
    jmp MimeMissingKb

.globl PerfStartProviderEx
.p2align 4
PerfStartProviderEx:
    mov $565, %ecx
    jmp MimeMissingKb

.globl PerfStopProvider
.p2align 4
PerfStopProvider:
    mov $566, %ecx
    jmp MimeMissingKb

.globl PoolPerAppKeyStateInternal
.p2align 4
PoolPerAppKeyStateInternal:
    mov $567, %ecx
    jmp MimeMissingKb

.globl PopIoRingCompletion
.p2align 4
PopIoRingCompletion:
    mov $568, %ecx
    jmp MimeMissingKb

.globl PrivCopyFileExW
.p2align 4
PrivCopyFileExW:
    mov $569, %ecx
    jmp MimeMissingKb

.globl ProductIdFromPackageFamilyName
.p2align 4
ProductIdFromPackageFamilyName:
    mov $570, %ecx
    jmp MimeMissingKb

.globl PsmCreateKey
.p2align 4
PsmCreateKey:
    mov $571, %ecx
    jmp MimeMissingKb

.globl PsmCreateKeyWithDynamicId
.p2align 4
PsmCreateKeyWithDynamicId:
    mov $572, %ecx
    jmp MimeMissingKb

.globl PsmEqualApplication
.p2align 4
PsmEqualApplication:
    mov $573, %ecx
    jmp MimeMissingKb

.globl PsmEqualPackage
.p2align 4
PsmEqualPackage:
    mov $574, %ecx
    jmp MimeMissingKb

.globl PsmGetApplicationNameFromKey
.p2align 4
PsmGetApplicationNameFromKey:
    mov $575, %ecx
    jmp MimeMissingKb

.globl PsmGetAumidFromKey
.p2align 4
PsmGetAumidFromKey:
    mov $576, %ecx
    jmp MimeMissingKb

.globl PsmGetDynamicIdFromKey
.p2align 4
PsmGetDynamicIdFromKey:
    mov $577, %ecx
    jmp MimeMissingKb

.globl PsmGetKeyFromProcess
.p2align 4
PsmGetKeyFromProcess:
    mov $578, %ecx
    jmp MimeMissingKb

.globl PsmGetKeyFromToken
.p2align 4
PsmGetKeyFromToken:
    mov $579, %ecx
    jmp MimeMissingKb

.globl PsmGetPackageFullNameFromKey
.p2align 4
PsmGetPackageFullNameFromKey:
    mov $580, %ecx
    jmp MimeMissingKb

.globl PsmIsChildKey
.p2align 4
PsmIsChildKey:
    mov $581, %ecx
    jmp MimeMissingKb

.globl PsmIsDynamicKey
.p2align 4
PsmIsDynamicKey:
    mov $582, %ecx
    jmp MimeMissingKb

.globl PsmIsValidKey
.p2align 4
PsmIsValidKey:
    mov $583, %ecx
    jmp MimeMissingKb

.globl PssDuplicateSnapshot
.p2align 4
PssDuplicateSnapshot:
    mov $584, %ecx
    jmp MimeMissingKb

.globl PssWalkMarkerCreate
.p2align 4
PssWalkMarkerCreate:
    mov $585, %ecx
    jmp MimeMissingKb

.globl PssWalkMarkerFree
.p2align 4
PssWalkMarkerFree:
    mov $586, %ecx
    jmp MimeMissingKb

.globl PssWalkMarkerGetPosition
.p2align 4
PssWalkMarkerGetPosition:
    mov $587, %ecx
    jmp MimeMissingKb

.globl PssWalkMarkerSeekToBeginning
.p2align 4
PssWalkMarkerSeekToBeginning:
    mov $588, %ecx
    jmp MimeMissingKb

.globl PssWalkMarkerSetPosition
.p2align 4
PssWalkMarkerSetPosition:
    mov $589, %ecx
    jmp MimeMissingKb

.globl PssWalkSnapshot
.p2align 4
PssWalkSnapshot:
    mov $590, %ecx
    jmp MimeMissingKb

.globl PublisherFromPackageFullName
.p2align 4
PublisherFromPackageFullName:
    mov $591, %ecx
    jmp MimeMissingKb

.globl PublishStateChangeNotification
.p2align 4
PublishStateChangeNotification:
    mov $592, %ecx
    jmp MimeMissingKb

.globl _purecall
.p2align 4
_purecall:
    mov $593, %ecx
    jmp MimeMissingKb

.globl QueryAuxiliaryCounterFrequency
.p2align 4
QueryAuxiliaryCounterFrequency:
    mov $594, %ecx
    jmp MimeMissingKb

.globl QueryGlobalizationUserSettingsStatus
.p2align 4
QueryGlobalizationUserSettingsStatus:
    mov $595, %ecx
    jmp MimeMissingKb

.globl QueryIdleProcessorCycleTime
.p2align 4
QueryIdleProcessorCycleTime:
    mov $596, %ecx
    jmp MimeMissingKb

.globl QueryIdleProcessorCycleTimeEx
.p2align 4
QueryIdleProcessorCycleTimeEx:
    mov $597, %ecx
    jmp MimeMissingKb

.globl QueryIoRingCapabilities
.p2align 4
QueryIoRingCapabilities:
    mov $598, %ecx
    jmp MimeMissingKb

.globl QueryOptionalDelayLoadedAPI
.p2align 4
QueryOptionalDelayLoadedAPI:
    mov $599, %ecx
    jmp MimeMissingKb

.globl QueryPartitionInformation
.p2align 4
QueryPartitionInformation:
    mov $600, %ecx
    jmp MimeMissingKb

.globl QueryProtectedPolicy
.p2align 4
QueryProtectedPolicy:
    mov $601, %ecx
    jmp MimeMissingKb

.globl QueryStateAtomValueInfo
.p2align 4
QueryStateAtomValueInfo:
    mov $602, %ecx
    jmp MimeMissingKb

.globl QueryStateContainerCreatedNew
.p2align 4
QueryStateContainerCreatedNew:
    mov $603, %ecx
    jmp MimeMissingKb

.globl QueryStateContainerItemInfo
.p2align 4
QueryStateContainerItemInfo:
    mov $604, %ecx
    jmp MimeMissingKb

.globl QueryVirtualMemoryInformation
.p2align 4
QueryVirtualMemoryInformation:
    mov $605, %ecx
    jmp MimeMissingKb

.globl QueueUserAPC2
.p2align 4
QueueUserAPC2:
    mov $606, %ecx
    jmp MimeMissingKb

.globl QuirkGetData
.p2align 4
QuirkGetData:
    mov $607, %ecx
    jmp MimeMissingKb

.globl QuirkGetData2
.p2align 4
QuirkGetData2:
    mov $608, %ecx
    jmp MimeMissingKb

.globl QuirkIsEnabled2
.p2align 4
QuirkIsEnabled2:
    mov $609, %ecx
    jmp MimeMissingKb

.globl QuirkIsEnabled3
.p2align 4
QuirkIsEnabled3:
    mov $610, %ecx
    jmp MimeMissingKb

.globl QuirkIsEnabledForPackage
.p2align 4
QuirkIsEnabledForPackage:
    mov $611, %ecx
    jmp MimeMissingKb

.globl QuirkIsEnabledForPackage2
.p2align 4
QuirkIsEnabledForPackage2:
    mov $612, %ecx
    jmp MimeMissingKb

.globl QuirkIsEnabledForPackage3
.p2align 4
QuirkIsEnabledForPackage3:
    mov $613, %ecx
    jmp MimeMissingKb

.globl QuirkIsEnabledForPackage4
.p2align 4
QuirkIsEnabledForPackage4:
    mov $614, %ecx
    jmp MimeMissingKb

.globl RaiseCustomSystemEventTrigger
.p2align 4
RaiseCustomSystemEventTrigger:
    mov $615, %ecx
    jmp MimeMissingKb

.globl ReadDirectoryChangesExW
.p2align 4
ReadDirectoryChangesExW:
    mov $616, %ecx
    jmp MimeMissingKb

.globl ReadStateAtomValue
.p2align 4
ReadStateAtomValue:
    mov $617, %ecx
    jmp MimeMissingKb

.globl ReadStateContainerValue
.p2align 4
ReadStateContainerValue:
    mov $618, %ecx
    jmp MimeMissingKb

.globl RecordFeatureUsage2
.p2align 4
RecordFeatureUsage2:
    mov $619, %ecx
    jmp MimeMissingKb

.globl RefreshPackageInfo
.p2align 4
RefreshPackageInfo:
    mov $620, %ecx
    jmp MimeMissingKb

.globl RefreshPolicyExInternal
.p2align 4
RefreshPolicyExInternal:
    mov $621, %ecx
    jmp MimeMissingKb

.globl RefreshPolicyInternal
.p2align 4
RefreshPolicyInternal:
    mov $622, %ecx
    jmp MimeMissingKb

.globl RegCreateKeyExInternalA
.p2align 4
RegCreateKeyExInternalA:
    mov $623, %ecx
    jmp MimeMissingKb

.globl RegCreateKeyExInternalW
.p2align 4
RegCreateKeyExInternalW:
    mov $624, %ecx
    jmp MimeMissingKb

.globl RegDeleteKeyExInternalA
.p2align 4
RegDeleteKeyExInternalA:
    mov $625, %ecx
    jmp MimeMissingKb

.globl RegDeleteKeyExInternalW
.p2align 4
RegDeleteKeyExInternalW:
    mov $626, %ecx
    jmp MimeMissingKb

.globl RegisterBadMemoryNotification
.p2align 4
RegisterBadMemoryNotification:
    mov $627, %ecx
    jmp MimeMissingKb

.globl RegisterGPNotificationInternal
.p2align 4
RegisterGPNotificationInternal:
    mov $628, %ecx
    jmp MimeMissingKb

.globl RegisterStateChangeNotification
.p2align 4
RegisterStateChangeNotification:
    mov $629, %ecx
    jmp MimeMissingKb

.globl RegisterStateLock
.p2align 4
RegisterStateLock:
    mov $630, %ecx
    jmp MimeMissingKb

.globl RegKrnGetAppKeyEventAddressInternal
.p2align 4
RegKrnGetAppKeyEventAddressInternal:
    mov $631, %ecx
    jmp MimeMissingKb

.globl RegKrnGetAppKeyLoaded
.p2align 4
RegKrnGetAppKeyLoaded:
    mov $632, %ecx
    jmp MimeMissingKb

.globl RegKrnGetClassesEnumTableAddressInternal
.p2align 4
RegKrnGetClassesEnumTableAddressInternal:
    mov $633, %ecx
    jmp MimeMissingKb

.globl RegKrnGetHKEY_ClassesRootAddress
.p2align 4
RegKrnGetHKEY_ClassesRootAddress:
    mov $634, %ecx
    jmp MimeMissingKb

.globl RegKrnGetTermsrvRegistryExtensionFlags
.p2align 4
RegKrnGetTermsrvRegistryExtensionFlags:
    mov $635, %ecx
    jmp MimeMissingKb

.globl RegKrnResetAppKeyLoaded
.p2align 4
RegKrnResetAppKeyLoaded:
    mov $636, %ecx
    jmp MimeMissingKb

.globl RegKrnSetDllHasThreadStateGlobal
.p2align 4
RegKrnSetDllHasThreadStateGlobal:
    mov $637, %ecx
    jmp MimeMissingKb

.globl RegKrnSetTermsrvRegistryExtensionFlags
.p2align 4
RegKrnSetTermsrvRegistryExtensionFlags:
    mov $638, %ecx
    jmp MimeMissingKb

.globl RegOpenKeyExInternalA
.p2align 4
RegOpenKeyExInternalA:
    mov $639, %ecx
    jmp MimeMissingKb

.globl RegOpenKeyExInternalW
.p2align 4
RegOpenKeyExInternalW:
    mov $640, %ecx
    jmp MimeMissingKb

.globl ReleasePackagedDataForFile
.p2align 4
ReleasePackagedDataForFile:
    mov $641, %ecx
    jmp MimeMissingKb

.globl ReleaseStateLock
.p2align 4
ReleaseStateLock:
    mov $642, %ecx
    jmp MimeMissingKb

.globl RemapPredefinedHandleInternal
.p2align 4
RemapPredefinedHandleInternal:
    mov $643, %ecx
    jmp MimeMissingKb

.globl RemoveDirectory2A
.p2align 4
RemoveDirectory2A:
    mov $644, %ecx
    jmp MimeMissingKb

.globl RemoveDirectory2W
.p2align 4
RemoveDirectory2W:
    mov $645, %ecx
    jmp MimeMissingKb

.globl RemoveDirectoryFromAppW
.p2align 4
RemoveDirectoryFromAppW:
    mov $646, %ecx
    jmp MimeMissingKb

.globl RemoveExtensionProgIds
.p2align 4
RemoveExtensionProgIds:
    mov $647, %ecx
    jmp MimeMissingKb

.globl RemovePackageDependency
.p2align 4
RemovePackageDependency:
    mov $648, %ecx
    jmp MimeMissingKb

.globl RemovePackageStatus
.p2align 4
RemovePackageStatus:
    mov $649, %ecx
    jmp MimeMissingKb

.globl RemovePackageStatusForUser
.p2align 4
RemovePackageStatusForUser:
    mov $650, %ecx
    jmp MimeMissingKb

.globl ReplaceFileExInternal
.p2align 4
ReplaceFileExInternal:
    mov $651, %ecx
    jmp MimeMissingKb

.globl ReplaceFileFromAppW
.p2align 4
ReplaceFileFromAppW:
    mov $652, %ecx
    jmp MimeMissingKb

.globl ResetState
.p2align 4
ResetState:
    mov $653, %ecx
    jmp MimeMissingKb

.globl ResolveDelayLoadsFromDll
.p2align 4
ResolveDelayLoadsFromDll:
    mov $654, %ecx
    jmp MimeMissingKb

.globl RestoreThreadPreferredUILanguages
.p2align 4
RestoreThreadPreferredUILanguages:
    mov $655, %ecx
    jmp MimeMissingKb

.globl RsopLoggingEnabledInternal
.p2align 4
RsopLoggingEnabledInternal:
    mov $656, %ecx
    jmp MimeMissingKb

.globl SaveAlternatePackageRootPath
.p2align 4
SaveAlternatePackageRootPath:
    mov $657, %ecx
    jmp MimeMissingKb

.globl SaveStateRootFolderPath
.p2align 4
SaveStateRootFolderPath:
    mov $658, %ecx
    jmp MimeMissingKb

.globl SetCachedSigningLevel
.p2align 4
SetCachedSigningLevel:
    mov $659, %ecx
    jmp MimeMissingKb

.globl SetClientDynamicTimeZoneInformation
.p2align 4
SetClientDynamicTimeZoneInformation:
    mov $660, %ecx
    jmp MimeMissingKb

.globl SetClientTimeZoneInformation
.p2align 4
SetClientTimeZoneInformation:
    mov $661, %ecx
    jmp MimeMissingKb

.globl SetCommConfig
.p2align 4
SetCommConfig:
    mov $662, %ecx
    jmp MimeMissingKb

.globl SetDynamicTimeZoneInformation
.p2align 4
SetDynamicTimeZoneInformation:
    mov $663, %ecx
    jmp MimeMissingKb

.globl SetExtensionProperty
.p2align 4
SetExtensionProperty:
    mov $664, %ecx
    jmp MimeMissingKb

.globl SetFileAttributesFromAppW
.p2align 4
SetFileAttributesFromAppW:
    mov $665, %ecx
    jmp MimeMissingKb

.globl SetIoRingCompletionEvent
.p2align 4
SetIoRingCompletionEvent:
    mov $666, %ecx
    jmp MimeMissingKb

.globl SetIsDeveloperModeEnabled
.p2align 4
SetIsDeveloperModeEnabled:
    mov $667, %ecx
    jmp MimeMissingKb

.globl SetIsSideloadingEnabled
.p2align 4
SetIsSideloadingEnabled:
    mov $668, %ecx
    jmp MimeMissingKb

.globl SetLastConsoleEventActive
.p2align 4
SetLastConsoleEventActive:
    mov $669, %ecx
    jmp MimeMissingKb

.globl SetPersistedRegistryBOOL
.p2align 4
SetPersistedRegistryBOOL:
    mov $670, %ecx
    jmp MimeMissingKb

.globl SetPersistedRegistryDWORD
.p2align 4
SetPersistedRegistryDWORD:
    mov $671, %ecx
    jmp MimeMissingKb

.globl SetPersistedRegistryString
.p2align 4
SetPersistedRegistryString:
    mov $672, %ecx
    jmp MimeMissingKb

.globl SetPersistedRegistryValue
.p2align 4
SetPersistedRegistryValue:
    mov $673, %ecx
    jmp MimeMissingKb

.globl SetProcessDefaultCpuSetMasks
.p2align 4
SetProcessDefaultCpuSetMasks:
    mov $674, %ecx
    jmp MimeMissingKb

.globl SetProcessDefaultCpuSets
.p2align 4
SetProcessDefaultCpuSets:
    mov $675, %ecx
    jmp MimeMissingKb

.globl SetProcessDynamicEHContinuationTargets
.p2align 4
SetProcessDynamicEHContinuationTargets:
    mov $676, %ecx
    jmp MimeMissingKb

.globl SetProcessDynamicEnforcedCetCompatibleRanges
.p2align 4
SetProcessDynamicEnforcedCetCompatibleRanges:
    mov $677, %ecx
    jmp MimeMissingKb

.globl SetProcessGroupAffinity
.p2align 4
SetProcessGroupAffinity:
    mov $678, %ecx
    jmp MimeMissingKb

.globl SetProcessValidCallTargetsForMappedView
.p2align 4
SetProcessValidCallTargetsForMappedView:
    mov $679, %ecx
    jmp MimeMissingKb

.globl SetProtectedPolicy
.p2align 4
SetProtectedPolicy:
    mov $680, %ecx
    jmp MimeMissingKb

.globl SetProtocolProperty
.p2align 4
SetProtocolProperty:
    mov $681, %ecx
    jmp MimeMissingKb

.globl SetRoamingLastObservedChangeTime
.p2align 4
SetRoamingLastObservedChangeTime:
    mov $682, %ecx
    jmp MimeMissingKb

.globl SetStateVersion
.p2align 4
SetStateVersion:
    mov $683, %ecx
    jmp MimeMissingKb

.globl SetSystemFileCacheSize
.p2align 4
SetSystemFileCacheSize:
    mov $684, %ecx
    jmp MimeMissingKb

.globl SetSystemTimeAdjustmentPrecise
.p2align 4
SetSystemTimeAdjustmentPrecise:
    mov $685, %ecx
    jmp MimeMissingKb

.globl SetThreadPreferredUILanguages2
.p2align 4
SetThreadPreferredUILanguages2:
    mov $686, %ecx
    jmp MimeMissingKb

.globl SetThreadSelectedCpuSetMasks
.p2align 4
SetThreadSelectedCpuSetMasks:
    mov $687, %ecx
    jmp MimeMissingKb

.globl SetThreadSelectedCpuSets
.p2align 4
SetThreadSelectedCpuSets:
    mov $688, %ecx
    jmp MimeMissingKb

.globl SetUserGeoName
.p2align 4
SetUserGeoName:
    mov $689, %ecx
    jmp MimeMissingKb

.globl SharedLocalIsEnabled
.p2align 4
SharedLocalIsEnabled:
    mov $690, %ecx
    jmp MimeMissingKb

.globl SHCoCreateInstance
.p2align 4
SHCoCreateInstance:
    mov $691, %ecx
    jmp MimeMissingKb

.globl SHLoadIndirectStringInternal
.p2align 4
SHLoadIndirectStringInternal:
    mov $692, %ecx
    jmp MimeMissingKb

.globl SpecialMBToWC
.p2align 4
SpecialMBToWC:
    mov $693, %ecx
    jmp MimeMissingKb

.globl StrChrA_MB
.p2align 4
StrChrA_MB:
    mov $694, %ecx
    jmp MimeMissingKb

.globl SubmitIoRing
.p2align 4
SubmitIoRing:
    mov $695, %ecx
    jmp MimeMissingKb

.globl SubscribeEdpEnabledStateChange
.p2align 4
SubscribeEdpEnabledStateChange:
    mov $696, %ecx
    jmp MimeMissingKb

.globl SubscribeFeatureUsageFlush
.p2align 4
SubscribeFeatureUsageFlush:
    mov $697, %ecx
    jmp MimeMissingKb

.globl SubscribeStateChangeNotification
.p2align 4
SubscribeStateChangeNotification:
    mov $698, %ecx
    jmp MimeMissingKb

.globl SubscribeWdagEnabledStateChange
.p2align 4
SubscribeWdagEnabledStateChange:
    mov $699, %ecx
    jmp MimeMissingKb

.globl TerminateEnclave
.p2align 4
TerminateEnclave:
    mov $700, %ecx
    jmp MimeMissingKb

.globl TestClose
.p2align 4
TestClose:
    mov $701, %ecx
    jmp MimeMissingKb

.globl TestControlReporting
.p2align 4
TestControlReporting:
    mov $702, %ecx
    jmp MimeMissingKb

.globl TestCreate
.p2align 4
TestCreate:
    mov $703, %ecx
    jmp MimeMissingKb

.globl TestOpen
.p2align 4
TestOpen:
    mov $704, %ecx
    jmp MimeMissingKb

.globl TestQueryData
.p2align 4
TestQueryData:
    mov $705, %ecx
    jmp MimeMissingKb

.globl TestRecoverResults
.p2align 4
TestRecoverResults:
    mov $706, %ecx
    jmp MimeMissingKb

.globl TestReport
.p2align 4
TestReport:
    mov $707, %ecx
    jmp MimeMissingKb

.globl TestUnlockData
.p2align 4
TestUnlockData:
    mov $708, %ecx
    jmp MimeMissingKb

.globl time
.p2align 4
time:
    mov $709, %ecx
    jmp MimeMissingKb

.globl _time64
.p2align 4
_time64:
    mov $710, %ecx
    jmp MimeMissingKb

.globl TryCreatePackageDependency
.p2align 4
TryCreatePackageDependency:
    mov $711, %ecx
    jmp MimeMissingKb

.globl TryCreatePackageDependency2
.p2align 4
TryCreatePackageDependency2:
    mov $712, %ecx
    jmp MimeMissingKb

.globl UnregisterBadMemoryNotification
.p2align 4
UnregisterBadMemoryNotification:
    mov $713, %ecx
    jmp MimeMissingKb

.globl UnregisterGPNotificationInternal
.p2align 4
UnregisterGPNotificationInternal:
    mov $714, %ecx
    jmp MimeMissingKb

.globl UnregisterStateChangeNotification
.p2align 4
UnregisterStateChangeNotification:
    mov $715, %ecx
    jmp MimeMissingKb

.globl UnregisterStateLock
.p2align 4
UnregisterStateLock:
    mov $716, %ecx
    jmp MimeMissingKb

.globl UnsubscribeEdpEnabledStateChange
.p2align 4
UnsubscribeEdpEnabledStateChange:
    mov $717, %ecx
    jmp MimeMissingKb

.globl UnsubscribeFeatureUsageFlush
.p2align 4
UnsubscribeFeatureUsageFlush:
    mov $718, %ecx
    jmp MimeMissingKb

.globl UnsubscribeStateChangeNotification
.p2align 4
UnsubscribeStateChangeNotification:
    mov $719, %ecx
    jmp MimeMissingKb

.globl UnsubscribeWdagEnabledStateChange
.p2align 4
UnsubscribeWdagEnabledStateChange:
    mov $720, %ecx
    jmp MimeMissingKb

.globl UpdatePackageStatus
.p2align 4
UpdatePackageStatus:
    mov $721, %ecx
    jmp MimeMissingKb

.globl UpdatePackageStatusForUser
.p2align 4
UpdatePackageStatusForUser:
    mov $722, %ecx
    jmp MimeMissingKb

.globl UpdatePackageStatusForUserSid
.p2align 4
UpdatePackageStatusForUserSid:
    mov $723, %ecx
    jmp MimeMissingKb

.globl VerifyApplicationUserModelId
.p2align 4
VerifyApplicationUserModelId:
    mov $724, %ecx
    jmp MimeMissingKb

.globl VerifyApplicationUserModelIdA
.p2align 4
VerifyApplicationUserModelIdA:
    mov $725, %ecx
    jmp MimeMissingKb

.globl VerifyPackageFamilyName
.p2align 4
VerifyPackageFamilyName:
    mov $726, %ecx
    jmp MimeMissingKb

.globl VerifyPackageFamilyNameA
.p2align 4
VerifyPackageFamilyNameA:
    mov $727, %ecx
    jmp MimeMissingKb

.globl VerifyPackageFullName
.p2align 4
VerifyPackageFullName:
    mov $728, %ecx
    jmp MimeMissingKb

.globl VerifyPackageFullNameA
.p2align 4
VerifyPackageFullNameA:
    mov $729, %ecx
    jmp MimeMissingKb

.globl VerifyPackageId
.p2align 4
VerifyPackageId:
    mov $730, %ecx
    jmp MimeMissingKb

.globl VerifyPackageIdA
.p2align 4
VerifyPackageIdA:
    mov $731, %ecx
    jmp MimeMissingKb

.globl VerifyPackageName
.p2align 4
VerifyPackageName:
    mov $732, %ecx
    jmp MimeMissingKb

.globl VerifyPackageNameA
.p2align 4
VerifyPackageNameA:
    mov $733, %ecx
    jmp MimeMissingKb

.globl VerifyPackagePublisher
.p2align 4
VerifyPackagePublisher:
    mov $734, %ecx
    jmp MimeMissingKb

.globl VerifyPackagePublisherA
.p2align 4
VerifyPackagePublisherA:
    mov $735, %ecx
    jmp MimeMissingKb

.globl VerifyPackageRelativeApplicationId
.p2align 4
VerifyPackageRelativeApplicationId:
    mov $736, %ecx
    jmp MimeMissingKb

.globl VerifyPackageRelativeApplicationIdA
.p2align 4
VerifyPackageRelativeApplicationIdA:
    mov $737, %ecx
    jmp MimeMissingKb

.globl WaitForMachinePolicyForegroundProcessingInternal
.p2align 4
WaitForMachinePolicyForegroundProcessingInternal:
    mov $738, %ecx
    jmp MimeMissingKb

.globl WaitForUserPolicyForegroundProcessingInternal
.p2align 4
WaitForUserPolicyForegroundProcessingInternal:
    mov $739, %ecx
    jmp MimeMissingKb

.globl __wargv
.p2align 4
__wargv:
    mov $740, %ecx
    jmp MimeMissingKb

.globl WerpNotifyLoadStringResource
.p2align 4
WerpNotifyLoadStringResource:
    mov $741, %ecx
    jmp MimeMissingKb

.globl WerpNotifyUseStringResource
.p2align 4
WerpNotifyUseStringResource:
    mov $742, %ecx
    jmp MimeMissingKb

.globl WerRegisterAdditionalProcess
.p2align 4
WerRegisterAdditionalProcess:
    mov $743, %ecx
    jmp MimeMissingKb

.globl WerRegisterAppLocalDump
.p2align 4
WerRegisterAppLocalDump:
    mov $744, %ecx
    jmp MimeMissingKb

.globl WerRegisterExcludedMemoryBlock
.p2align 4
WerRegisterExcludedMemoryBlock:
    mov $745, %ecx
    jmp MimeMissingKb

.globl WerSetMaxProcessHoldMilliseconds
.p2align 4
WerSetMaxProcessHoldMilliseconds:
    mov $746, %ecx
    jmp MimeMissingKb

.globl WerUnregisterAdditionalProcess
.p2align 4
WerUnregisterAdditionalProcess:
    mov $747, %ecx
    jmp MimeMissingKb

.globl WerUnregisterAppLocalDump
.p2align 4
WerUnregisterAppLocalDump:
    mov $748, %ecx
    jmp MimeMissingKb

.globl WerUnregisterExcludedMemoryBlock
.p2align 4
WerUnregisterExcludedMemoryBlock:
    mov $749, %ecx
    jmp MimeMissingKb

.globl WerUnregisterFile
.p2align 4
WerUnregisterFile:
    mov $750, %ecx
    jmp MimeMissingKb

.globl WerUnregisterMemoryBlock
.p2align 4
WerUnregisterMemoryBlock:
    mov $751, %ecx
    jmp MimeMissingKb

.globl __wgetmainargs
.p2align 4
__wgetmainargs:
    mov $752, %ecx
    jmp MimeMissingKb

.globl WilFailureNotifyWatchers
.p2align 4
WilFailureNotifyWatchers:
    mov $753, %ecx
    jmp MimeMissingKb

.globl WilFailureWatcherResume
.p2align 4
WilFailureWatcherResume:
    mov $754, %ecx
    jmp MimeMissingKb

.globl WilFailureWatcherSubscribe
.p2align 4
WilFailureWatcherSubscribe:
    mov $755, %ecx
    jmp MimeMissingKb

.globl WilFailureWatcherSuspend
.p2align 4
WilFailureWatcherSuspend:
    mov $756, %ecx
    jmp MimeMissingKb

.globl WilFailureWatcherUnsubscribe
.p2align 4
WilFailureWatcherUnsubscribe:
    mov $757, %ecx
    jmp MimeMissingKb

.globl Wow64SuspendThread
.p2align 4
Wow64SuspendThread:
    mov $758, %ecx
    jmp MimeMissingKb

.globl wprintf
.p2align 4
wprintf:
    mov $759, %ecx
    jmp MimeMissingKb

.globl WriteStateAtomValue
.p2align 4
WriteStateAtomValue:
    mov $760, %ecx
    jmp MimeMissingKb

.globl WriteStateContainerValue
.p2align 4
WriteStateContainerValue:
    mov $761, %ecx
    jmp MimeMissingKb

.globl WTSGetServiceSessionId
.p2align 4
WTSGetServiceSessionId:
    mov $762, %ecx
    jmp MimeMissingKb

.globl WTSIsServerContainer
.p2align 4
WTSIsServerContainer:
    mov $763, %ecx
    jmp MimeMissingKb

