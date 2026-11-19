#if defined(__x86_64__)
#define REGISTER_ARGS 4
#else
#define REGISTER_ARGS 8
#endif
#define STACK_ARGS(n) ((n) > REGISTER_ARGS ? (n) - REGISTER_ARGS : 0)
#include "mime_svc_nt.h"

void KiRegisterService(unsigned int number, void *routine, unsigned char stack_args);
void NtAccessCheck(void);
void NtAccessCheckByType(void);
void NtCopyFileChunk(void);
void NtLoadDriver(void);
void NtUnloadDriver(void);
void NtAddAtom(void);
void NtAddAtomEx(void);
void NtAdjustGroupsToken(void);
void NtAdjustPrivilegesToken(void);
void NtAlertThread(void);
void NtAlertThreadByThreadId(void);
void NtAlertThreadByThreadIdEx(void);
void NtConnectPort(void);
void NtAlpcSendWaitReceivePort(void);
void NtAlpcAcceptConnectPort(void);
void NtAlpcCancelMessage(void);
void NtAlpcConnectPort(void);
void NtAlpcConnectPortEx(void);
void NtAlpcCreatePort(void);
void NtAlpcCreatePortSection(void);
void NtAlpcCreateResourceReserve(void);
void NtAlpcCreateSectionView(void);
void NtAlpcCreateSecurityContext(void);
void NtAlpcDeletePortSection(void);
void NtAlpcDeleteResourceReserve(void);
void NtAlpcDeleteSectionView(void);
void NtAlpcDeleteSecurityContext(void);
void NtAlpcDisconnectPort(void);
void NtAlpcImpersonateClientContainerOfPort(void);
void NtAlpcImpersonateClientOfPort(void);
void NtAlpcOpenSenderProcess(void);
void NtAlpcOpenSenderThread(void);
void NtAlpcQueryInformation(void);
void NtAlpcQueryInformationMessage(void);
void NtAlpcRevokeSecurityContext(void);
void NtAlpcSetInformation(void);
void NtAllocateLocallyUniqueId(void);
void NtCompareTokens(void);
void NtQuerySemaphore(void);
void NtQueryEvent(void);
void NtCreateWaitCompletionPacket(void);
void NtAssociateWaitCompletionPacket(void);
void NtCancelWaitCompletionPacket(void);
void NtAllocateVirtualMemory(void);
void NtAllocateVirtualMemoryEx(void);
void NtApphelpCacheControl(void);
void NtAssignProcessToJobObject(void);
void NtCallbackReturn(void);
void NtCancelIoFile(void);
void NtCancelIoFileEx(void);
void NtCancelTimer(void);
void NtClearEvent(void);
void NtClose(void);
void NtContinue(void);
void NtContinueEx(void);
void NtCreateDirectoryObject(void);
void NtCreateEvent(void);
void NtCreateFile(void);
void NtCreateIoCompletion(void);
void NtCreateJobObject(void);
void NtCreateKey(void);
void NtCreateLowBoxToken(void);
void NtCreateMutant(void);
void NtCreateNamedPipeFile(void);
void NtCreateSection(void);
void NtCreateSemaphore(void);
void NtCreateDebugObject(void);
void NtLockVirtualMemory(void);
void NtUnlockVirtualMemory(void);
void NtSetInformationDebugObject(void);
void NtWaitForDebugEvent(void);
void NtCreateSymbolicLinkObject(void);
void NtCreateThreadEx(void);
void NtCreateTimer(void);
void NtCreateToken(void);
void NtCreateUserProcess(void);
void NtCreateWnfStateName(void);
void NtDelayExecution(void);
void NtDeleteAtom(void);
void NtDeleteKey(void);
void NtDeleteValueKey(void);
void NtDeleteWnfStateData(void);
void NtDeleteWnfStateName(void);
void NtDeviceIoControlFile(void);
void NtDisplayString(void);
void NtRaiseHardError(void);
void NtDuplicateObject(void);
void NtDuplicateToken(void);
void NtEnumerateKey(void);
void NtEnumerateSystemEnvironmentValuesEx(void);
void NtEnumerateValueKey(void);
void NtQueryMultipleValueKey(void);
void NtFilterToken(void);
void NtFindAtom(void);
void NtFlushBuffersFile(void);
void NtFlushBuffersFileEx(void);
void NtFlushInstructionCache(void);
void NtFlushKey(void);
void NtFlushProcessWriteBuffers(void);
void NtFlushVirtualMemory(void);
void NtFreeVirtualMemory(void);
void NtFsControlFile(void);
void NtGetCompleteWnfStateSubscription(void);
void NtGetContextThread(void);
void NtGetCurrentProcessorNumber(void);
void NtGetTickCount(void);
void NtGetWriteWatch(void);
void NtImpersonateAnonymousToken(void);
void NtInitiatePowerAction(void);
void NtPowerInformation(void);
void NtIsProcessInJob(void);
void NtLoadKey(void);
void NtLoadKey2(void);
void NtLoadKeyEx(void);
void NtMakeTemporaryObject(void);
void NtMapViewOfSection(void);
void NtMapViewOfSectionEx(void);
void NtNotifyChangeDirectoryFile(void);
void NtNotifyChangeDirectoryFileEx(void);
void NtNotifyChangeKey(void);
void NtNotifyChangeMultipleKeys(void);
void NtOpenDirectoryObject(void);
void NtOpenEvent(void);
void NtOpenFile(void);
void NtOpenIoCompletion(void);
void NtOpenJobObject(void);
void NtOpenKey(void);
void NtOpenKeyEx(void);
void NtOpenMutant(void);
void NtOpenProcess(void);
void NtOpenProcessToken(void);
void NtOpenProcessTokenEx(void);
void NtOpenSection(void);
void NtOpenSemaphore(void);
void NtOpenSymbolicLinkObject(void);
void NtOpenThread(void);
void NtOpenThreadToken(void);
void NtOpenThreadTokenEx(void);
void NtOpenTimer(void);
void NtPlugPlayControl(void);
void NtProtectVirtualMemory(void);
void NtPulseEvent(void);
void NtQueryAttributesFile(void);
void NtQueryDebugFilterState(void);
void NtSystemDebugControl(void);
void NtSetDebugFilterState(void);
void NtQueryDefaultLocale(void);
void NtQueryDefaultUILanguage(void);
void NtQueryDirectoryFile(void);
void NtQueryDirectoryFileEx(void);
void NtQueryDirectoryObject(void);
void NtQueryFullAttributesFile(void);
void NtQueryInformationAtom(void);
void NtQueryInformationFile(void);
void NtQueryInformationJobObject(void);
void NtQueryInformationProcess(void);
void NtQueryInformationThread(void);
void NtQueryInformationToken(void);
void NtQueryInstallUILanguage(void);
void NtQueryIoCompletion(void);
void NtQueryKey(void);
void NtSetInformationKey(void);
void NtQueryLicenseValue(void);
void NtGetNlsSectionPtr(void);
void NtInitializeNlsFiles(void);
void NtIsUILanguageComitted(void);
void NtGetMUIRegistryInfo(void);
void NtManageHotPatch(void);
void NtCreateTimer2(void);
void NtSetTimer2(void);
void NtCancelTimer2(void);
void NtCreateWorkerFactory(void);
void NtQueryInformationWorkerFactory(void);
void NtSetInformationWorkerFactory(void);
void NtShutdownWorkerFactory(void);
void NtReleaseWorkerFactoryWorker(void);
void NtWorkerFactoryWorkerReady(void);
void NtWaitForWorkViaWorkerFactory(void);
void NtQueryObject(void);
void NtQueryPerformanceCounter(void);
void NtQueryAuxiliaryCounterFrequency(void);
void NtConvertBetweenAuxiliaryCounterAndPerformanceCounter(void);
void NtQuerySection(void);
void NtQuerySecurityObject(void);
void NtQuerySecurityAttributesToken(void);
void NtQuerySymbolicLinkObject(void);
void NtQuerySystemInformation(void);
void NtQuerySystemEnvironmentValue(void);
void NtQuerySystemEnvironmentValueEx(void);
void NtSetSystemEnvironmentValue(void);
void NtSetSystemEnvironmentValueEx(void);
void NtQuerySystemTime(void);
void NtQueryTimerResolution(void);
void NtQueryValueKey(void);
void NtQueryVirtualMemory(void);
void NtQueryVolumeInformationFile(void);
void NtSetVolumeInformationFile(void);
void NtQueryWnfStateData(void);
void NtQueryWnfStateNameInformation(void);
void NtQueueApcThread(void);
void NtQueueApcThreadEx(void);
void NtQueueApcThreadEx2(void);
void NtRaiseException(void);
void NtReadFile(void);
void NtReadFileScatter(void);
void NtWriteFileGather(void);
void NtLockFile(void);
void NtUnlockFile(void);
void NtReadVirtualMemory(void);
void NtReleaseMutant(void);
void NtReleaseSemaphore(void);
void NtRemoveIoCompletion(void);
void NtRemoveIoCompletionEx(void);
void NtResetEvent(void);
void NtResetWriteWatch(void);
void NtResumeProcess(void);
void NtResumeThread(void);
void NtSetContextThread(void);
void NtSetEvent(void);
void NtSetInformationFile(void);
void NtSetInformationJobObject(void);
void NtSetInformationObject(void);
void NtSetInformationProcess(void);
void NtSetInformationThread(void);
void NtSetInformationToken(void);
void NtSetInformationVirtualMemory(void);
void NtSetIoCompletion(void);
void NtSetIoCompletionEx(void);
void NtSetSecurityObject(void);
void NtSetSystemInformation(void);
void NtSetTimer(void);
void NtSetTimerEx(void);
void NtSetTimerResolution(void);
void NtSetValueKey(void);
void NtSetWnfProcessNotificationEvent(void);
void NtShutdownSystem(void);
void NtSignalAndWaitForSingleObject(void);
void NtSubscribeWnfStateChange(void);
void NtSuspendProcess(void);
void NtSuspendThread(void);
void NtTerminateJobObject(void);
void NtTerminateProcess(void);
void NtTerminateThread(void);
void NtTestAlert(void);
void NtTraceControl(void);
void NtTraceEvent(void);
void NtUnloadKey(void);
void NtUnloadKey2(void);
void NtUnloadKeyEx(void);
void NtUnmapViewOfSection(void);
void NtUnmapViewOfSectionEx(void);
void NtUnsubscribeWnfStateChange(void);
void NtUpdateWnfStateData(void);
void NtWaitForAlertByThreadId(void);
void NtWaitForMultipleObjects(void);
void NtWaitForMultipleObjects32(void);
void NtWaitForSingleObject(void);
void NtWriteFile(void);
void NtWriteVirtualMemory(void);
void NtYieldExecution(void);

void KiInitializeSystemServices(void)
{
#ifdef MIME_SVC_NtAccessCheck
    KiRegisterService(MIME_SVC_NtAccessCheck, (void *)NtAccessCheck, STACK_ARGS(8));
#endif
#ifdef MIME_SVC_NtAccessCheckByType
    KiRegisterService(MIME_SVC_NtAccessCheckByType, (void *)NtAccessCheckByType, STACK_ARGS(11));
#endif
#ifdef MIME_SVC_NtCopyFileChunk
    KiRegisterService(MIME_SVC_NtCopyFileChunk, (void *)NtCopyFileChunk, STACK_ARGS(10));
#endif
#ifdef MIME_SVC_NtAddAtom
    KiRegisterService(MIME_SVC_NtAddAtom, (void *)NtAddAtom, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtAddAtomEx
    KiRegisterService(MIME_SVC_NtAddAtomEx, (void *)NtAddAtomEx, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtAdjustGroupsToken
    KiRegisterService(MIME_SVC_NtAdjustGroupsToken, (void *)NtAdjustGroupsToken, STACK_ARGS(6));
#endif
#ifdef MIME_SVC_NtAdjustPrivilegesToken
    KiRegisterService(MIME_SVC_NtAdjustPrivilegesToken, (void *)NtAdjustPrivilegesToken, STACK_ARGS(6));
#endif
#ifdef MIME_SVC_NtAlertThread
    KiRegisterService(MIME_SVC_NtAlertThread, (void *)NtAlertThread, STACK_ARGS(1));
#endif
#ifdef MIME_SVC_NtAlertThreadByThreadId
    KiRegisterService(MIME_SVC_NtAlertThreadByThreadId, (void *)NtAlertThreadByThreadId, STACK_ARGS(1));
#endif
#ifdef MIME_SVC_NtConnectPort
    KiRegisterService(MIME_SVC_NtConnectPort, (void *)NtConnectPort, STACK_ARGS(8));
#endif
#ifdef MIME_SVC_NtAlpcSendWaitReceivePort
    KiRegisterService(MIME_SVC_NtAlpcSendWaitReceivePort, (void *)NtAlpcSendWaitReceivePort, STACK_ARGS(8));
#endif
#ifdef MIME_SVC_NtAlpcAcceptConnectPort
    KiRegisterService(MIME_SVC_NtAlpcAcceptConnectPort, (void *)NtAlpcAcceptConnectPort, STACK_ARGS(9));
#endif
#ifdef MIME_SVC_NtAlpcCancelMessage
    KiRegisterService(MIME_SVC_NtAlpcCancelMessage, (void *)NtAlpcCancelMessage, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtAlpcConnectPort
    KiRegisterService(MIME_SVC_NtAlpcConnectPort, (void *)NtAlpcConnectPort, STACK_ARGS(11));
#endif
#ifdef MIME_SVC_NtAlpcConnectPortEx
    KiRegisterService(MIME_SVC_NtAlpcConnectPortEx, (void *)NtAlpcConnectPortEx, STACK_ARGS(11));
#endif
#ifdef MIME_SVC_NtAlpcCreatePort
    KiRegisterService(MIME_SVC_NtAlpcCreatePort, (void *)NtAlpcCreatePort, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtAlpcCreatePortSection
    KiRegisterService(MIME_SVC_NtAlpcCreatePortSection, (void *)NtAlpcCreatePortSection, STACK_ARGS(6));
#endif
#ifdef MIME_SVC_NtAlpcCreateResourceReserve
    KiRegisterService(MIME_SVC_NtAlpcCreateResourceReserve, (void *)NtAlpcCreateResourceReserve, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtAlpcCreateSectionView
    KiRegisterService(MIME_SVC_NtAlpcCreateSectionView, (void *)NtAlpcCreateSectionView, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtAlpcCreateSecurityContext
    KiRegisterService(MIME_SVC_NtAlpcCreateSecurityContext, (void *)NtAlpcCreateSecurityContext, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtAlpcDeletePortSection
    KiRegisterService(MIME_SVC_NtAlpcDeletePortSection, (void *)NtAlpcDeletePortSection, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtAlpcDeleteResourceReserve
    KiRegisterService(MIME_SVC_NtAlpcDeleteResourceReserve, (void *)NtAlpcDeleteResourceReserve, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtAlpcDeleteSectionView
    KiRegisterService(MIME_SVC_NtAlpcDeleteSectionView, (void *)NtAlpcDeleteSectionView, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtAlpcDeleteSecurityContext
    KiRegisterService(MIME_SVC_NtAlpcDeleteSecurityContext, (void *)NtAlpcDeleteSecurityContext, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtAlpcDisconnectPort
    KiRegisterService(MIME_SVC_NtAlpcDisconnectPort, (void *)NtAlpcDisconnectPort, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtAlpcImpersonateClientContainerOfPort
    KiRegisterService(MIME_SVC_NtAlpcImpersonateClientContainerOfPort, (void *)NtAlpcImpersonateClientContainerOfPort, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtAlpcImpersonateClientOfPort
    KiRegisterService(MIME_SVC_NtAlpcImpersonateClientOfPort, (void *)NtAlpcImpersonateClientOfPort, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtAlpcOpenSenderProcess
    KiRegisterService(MIME_SVC_NtAlpcOpenSenderProcess, (void *)NtAlpcOpenSenderProcess, STACK_ARGS(6));
#endif
#ifdef MIME_SVC_NtAlpcOpenSenderThread
    KiRegisterService(MIME_SVC_NtAlpcOpenSenderThread, (void *)NtAlpcOpenSenderThread, STACK_ARGS(6));
#endif
#ifdef MIME_SVC_NtAlpcQueryInformation
    KiRegisterService(MIME_SVC_NtAlpcQueryInformation, (void *)NtAlpcQueryInformation, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtAlpcQueryInformationMessage
    KiRegisterService(MIME_SVC_NtAlpcQueryInformationMessage, (void *)NtAlpcQueryInformationMessage, STACK_ARGS(6));
#endif
#ifdef MIME_SVC_NtAlpcRevokeSecurityContext
    KiRegisterService(MIME_SVC_NtAlpcRevokeSecurityContext, (void *)NtAlpcRevokeSecurityContext, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtAlpcSetInformation
    KiRegisterService(MIME_SVC_NtAlpcSetInformation, (void *)NtAlpcSetInformation, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtAlertThreadByThreadIdEx
    KiRegisterService(MIME_SVC_NtAlertThreadByThreadIdEx, (void *)NtAlertThreadByThreadIdEx, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtAllocateLocallyUniqueId
    KiRegisterService(MIME_SVC_NtAllocateLocallyUniqueId, (void *)NtAllocateLocallyUniqueId, STACK_ARGS(1));
#endif
#ifdef MIME_SVC_NtCompareTokens
    KiRegisterService(MIME_SVC_NtCompareTokens, (void *)NtCompareTokens, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtQuerySemaphore
    KiRegisterService(MIME_SVC_NtQuerySemaphore, (void *)NtQuerySemaphore, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtQueryEvent
    KiRegisterService(MIME_SVC_NtQueryEvent, (void *)NtQueryEvent, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtCreateWaitCompletionPacket
    KiRegisterService(MIME_SVC_NtCreateWaitCompletionPacket, (void *)NtCreateWaitCompletionPacket, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtAssociateWaitCompletionPacket
    KiRegisterService(MIME_SVC_NtAssociateWaitCompletionPacket, (void *)NtAssociateWaitCompletionPacket, STACK_ARGS(8));
#endif
#ifdef MIME_SVC_NtCancelWaitCompletionPacket
    KiRegisterService(MIME_SVC_NtCancelWaitCompletionPacket, (void *)NtCancelWaitCompletionPacket, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtAllocateVirtualMemory
    KiRegisterService(MIME_SVC_NtAllocateVirtualMemory, (void *)NtAllocateVirtualMemory, STACK_ARGS(6));
#endif
#ifdef MIME_SVC_NtAllocateVirtualMemoryEx
    KiRegisterService(MIME_SVC_NtAllocateVirtualMemoryEx, (void *)NtAllocateVirtualMemoryEx, STACK_ARGS(7));
#endif
#ifdef MIME_SVC_NtApphelpCacheControl
    KiRegisterService(MIME_SVC_NtApphelpCacheControl, (void *)NtApphelpCacheControl, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtAssignProcessToJobObject
    KiRegisterService(MIME_SVC_NtAssignProcessToJobObject, (void *)NtAssignProcessToJobObject, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtCallbackReturn
    KiRegisterService(MIME_SVC_NtCallbackReturn, (void *)NtCallbackReturn, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtCancelIoFile
    KiRegisterService(MIME_SVC_NtCancelIoFile, (void *)NtCancelIoFile, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtCancelIoFileEx
    KiRegisterService(MIME_SVC_NtCancelIoFileEx, (void *)NtCancelIoFileEx, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtCancelTimer
    KiRegisterService(MIME_SVC_NtCancelTimer, (void *)NtCancelTimer, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtClearEvent
    KiRegisterService(MIME_SVC_NtClearEvent, (void *)NtClearEvent, STACK_ARGS(1));
#endif
#ifdef MIME_SVC_NtClose
    KiRegisterService(MIME_SVC_NtClose, (void *)NtClose, STACK_ARGS(1));
#endif
#ifdef MIME_SVC_NtContinue
    KiRegisterService(MIME_SVC_NtContinue, (void *)NtContinue, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtContinueEx
    KiRegisterService(MIME_SVC_NtContinueEx, (void *)NtContinueEx, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtCreateDirectoryObject
    KiRegisterService(MIME_SVC_NtCreateDirectoryObject, (void *)NtCreateDirectoryObject, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtCreateEvent
    KiRegisterService(MIME_SVC_NtCreateEvent, (void *)NtCreateEvent, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtLoadDriver
    KiRegisterService(MIME_SVC_NtLoadDriver, (void *)NtLoadDriver, STACK_ARGS(1));
#endif
#ifdef MIME_SVC_NtUnloadDriver
    KiRegisterService(MIME_SVC_NtUnloadDriver, (void *)NtUnloadDriver, STACK_ARGS(1));
#endif
#ifdef MIME_SVC_NtCreateFile
    KiRegisterService(MIME_SVC_NtCreateFile, (void *)NtCreateFile, STACK_ARGS(11));
#endif
#ifdef MIME_SVC_NtCreateIoCompletion
    KiRegisterService(MIME_SVC_NtCreateIoCompletion, (void *)NtCreateIoCompletion, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtCreateJobObject
    KiRegisterService(MIME_SVC_NtCreateJobObject, (void *)NtCreateJobObject, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtCreateKey
    KiRegisterService(MIME_SVC_NtCreateKey, (void *)NtCreateKey, STACK_ARGS(7));
#endif
#ifdef MIME_SVC_NtCreateLowBoxToken
    KiRegisterService(MIME_SVC_NtCreateLowBoxToken, (void *)NtCreateLowBoxToken, STACK_ARGS(9));
#endif
#ifdef MIME_SVC_NtCreateMutant
    KiRegisterService(MIME_SVC_NtCreateMutant, (void *)NtCreateMutant, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtCreateNamedPipeFile
    KiRegisterService(MIME_SVC_NtCreateNamedPipeFile, (void *)NtCreateNamedPipeFile, STACK_ARGS(14));
#endif
#ifdef MIME_SVC_NtCreateSection
    KiRegisterService(MIME_SVC_NtCreateSection, (void *)NtCreateSection, STACK_ARGS(7));
#endif
#ifdef MIME_SVC_NtCreateSemaphore
    KiRegisterService(MIME_SVC_NtCreateSemaphore, (void *)NtCreateSemaphore, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtLockVirtualMemory
    KiRegisterService(MIME_SVC_NtLockVirtualMemory, (void *)NtLockVirtualMemory, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtUnlockVirtualMemory
    KiRegisterService(MIME_SVC_NtUnlockVirtualMemory, (void *)NtUnlockVirtualMemory, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtCreateDebugObject
    KiRegisterService(MIME_SVC_NtCreateDebugObject, (void *)NtCreateDebugObject, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtSetInformationDebugObject
    KiRegisterService(MIME_SVC_NtSetInformationDebugObject, (void *)NtSetInformationDebugObject, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtWaitForDebugEvent
    KiRegisterService(MIME_SVC_NtWaitForDebugEvent, (void *)NtWaitForDebugEvent, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtCreateSymbolicLinkObject
    KiRegisterService(MIME_SVC_NtCreateSymbolicLinkObject, (void *)NtCreateSymbolicLinkObject, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtCreateThreadEx
    KiRegisterService(MIME_SVC_NtCreateThreadEx, (void *)NtCreateThreadEx, STACK_ARGS(11));
#endif
#ifdef MIME_SVC_NtCreateTimer
    KiRegisterService(MIME_SVC_NtCreateTimer, (void *)NtCreateTimer, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtCreateToken
    KiRegisterService(MIME_SVC_NtCreateToken, (void *)NtCreateToken, STACK_ARGS(13));
#endif
#ifdef MIME_SVC_NtCreateUserProcess
    KiRegisterService(MIME_SVC_NtCreateUserProcess, (void *)NtCreateUserProcess, STACK_ARGS(11));
#endif
#ifdef MIME_SVC_NtCreateWnfStateName
    KiRegisterService(MIME_SVC_NtCreateWnfStateName, (void *)NtCreateWnfStateName, STACK_ARGS(7));
#endif
#ifdef MIME_SVC_NtDelayExecution
    KiRegisterService(MIME_SVC_NtDelayExecution, (void *)NtDelayExecution, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtDeleteAtom
    KiRegisterService(MIME_SVC_NtDeleteAtom, (void *)NtDeleteAtom, STACK_ARGS(1));
#endif
#ifdef MIME_SVC_NtDeleteKey
    KiRegisterService(MIME_SVC_NtDeleteKey, (void *)NtDeleteKey, STACK_ARGS(1));
#endif
#ifdef MIME_SVC_NtDeleteValueKey
    KiRegisterService(MIME_SVC_NtDeleteValueKey, (void *)NtDeleteValueKey, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtDeleteWnfStateData
    KiRegisterService(MIME_SVC_NtDeleteWnfStateData, (void *)NtDeleteWnfStateData, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtDeleteWnfStateName
    KiRegisterService(MIME_SVC_NtDeleteWnfStateName, (void *)NtDeleteWnfStateName, STACK_ARGS(1));
#endif
#ifdef MIME_SVC_NtDeviceIoControlFile
    KiRegisterService(MIME_SVC_NtDeviceIoControlFile, (void *)NtDeviceIoControlFile, STACK_ARGS(10));
#endif
#ifdef MIME_SVC_NtDisplayString
    KiRegisterService(MIME_SVC_NtDisplayString, (void *)NtDisplayString, STACK_ARGS(1));
#endif
#ifdef MIME_SVC_NtRaiseHardError
    KiRegisterService(MIME_SVC_NtRaiseHardError, (void *)NtRaiseHardError, STACK_ARGS(6));
#endif
#ifdef MIME_SVC_NtDuplicateObject
    KiRegisterService(MIME_SVC_NtDuplicateObject, (void *)NtDuplicateObject, STACK_ARGS(7));
#endif
#ifdef MIME_SVC_NtDuplicateToken
    KiRegisterService(MIME_SVC_NtDuplicateToken, (void *)NtDuplicateToken, STACK_ARGS(6));
#endif
#ifdef MIME_SVC_NtEnumerateKey
    KiRegisterService(MIME_SVC_NtEnumerateKey, (void *)NtEnumerateKey, STACK_ARGS(6));
#endif
#ifdef MIME_SVC_NtEnumerateSystemEnvironmentValuesEx
    KiRegisterService(MIME_SVC_NtEnumerateSystemEnvironmentValuesEx, (void *)NtEnumerateSystemEnvironmentValuesEx, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtEnumerateValueKey
    KiRegisterService(MIME_SVC_NtEnumerateValueKey, (void *)NtEnumerateValueKey, STACK_ARGS(6));
#endif
#ifdef MIME_SVC_NtQueryMultipleValueKey
    KiRegisterService(MIME_SVC_NtQueryMultipleValueKey, (void *)NtQueryMultipleValueKey, STACK_ARGS(6));
#endif
#ifdef MIME_SVC_NtFilterToken
    KiRegisterService(MIME_SVC_NtFilterToken, (void *)NtFilterToken, STACK_ARGS(6));
#endif
#ifdef MIME_SVC_NtFindAtom
    KiRegisterService(MIME_SVC_NtFindAtom, (void *)NtFindAtom, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtFlushBuffersFile
    KiRegisterService(MIME_SVC_NtFlushBuffersFile, (void *)NtFlushBuffersFile, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtFlushBuffersFileEx
    KiRegisterService(MIME_SVC_NtFlushBuffersFileEx, (void *)NtFlushBuffersFileEx, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtFlushInstructionCache
    KiRegisterService(MIME_SVC_NtFlushInstructionCache, (void *)NtFlushInstructionCache, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtFlushKey
    KiRegisterService(MIME_SVC_NtFlushKey, (void *)NtFlushKey, STACK_ARGS(1));
#endif
#ifdef MIME_SVC_NtFlushProcessWriteBuffers
    KiRegisterService(MIME_SVC_NtFlushProcessWriteBuffers, (void *)NtFlushProcessWriteBuffers, STACK_ARGS(0));
#endif
#ifdef MIME_SVC_NtFlushVirtualMemory
    KiRegisterService(MIME_SVC_NtFlushVirtualMemory, (void *)NtFlushVirtualMemory, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtFreeVirtualMemory
    KiRegisterService(MIME_SVC_NtFreeVirtualMemory, (void *)NtFreeVirtualMemory, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtFsControlFile
    KiRegisterService(MIME_SVC_NtFsControlFile, (void *)NtFsControlFile, STACK_ARGS(10));
#endif
#ifdef MIME_SVC_NtGetCompleteWnfStateSubscription
    KiRegisterService(MIME_SVC_NtGetCompleteWnfStateSubscription, (void *)NtGetCompleteWnfStateSubscription, STACK_ARGS(6));
#endif
#ifdef MIME_SVC_NtGetContextThread
    KiRegisterService(MIME_SVC_NtGetContextThread, (void *)NtGetContextThread, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtGetCurrentProcessorNumber
    KiRegisterService(MIME_SVC_NtGetCurrentProcessorNumber, (void *)NtGetCurrentProcessorNumber, STACK_ARGS(0));
#endif
#ifdef MIME_SVC_NtGetTickCount
    KiRegisterService(MIME_SVC_NtGetTickCount, (void *)NtGetTickCount, STACK_ARGS(0));
#endif
#ifdef MIME_SVC_NtGetWriteWatch
    KiRegisterService(MIME_SVC_NtGetWriteWatch, (void *)NtGetWriteWatch, STACK_ARGS(7));
#endif
#ifdef MIME_SVC_NtImpersonateAnonymousToken
    KiRegisterService(MIME_SVC_NtImpersonateAnonymousToken, (void *)NtImpersonateAnonymousToken, STACK_ARGS(1));
#endif
#ifdef MIME_SVC_NtInitiatePowerAction
    KiRegisterService(MIME_SVC_NtInitiatePowerAction, (void *)NtInitiatePowerAction, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtPowerInformation
    KiRegisterService(MIME_SVC_NtPowerInformation, (void *)NtPowerInformation, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtIsProcessInJob
    KiRegisterService(MIME_SVC_NtIsProcessInJob, (void *)NtIsProcessInJob, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtLoadKey
    KiRegisterService(MIME_SVC_NtLoadKey, (void *)NtLoadKey, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtLoadKey2
    KiRegisterService(MIME_SVC_NtLoadKey2, (void *)NtLoadKey2, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtLoadKeyEx
    KiRegisterService(MIME_SVC_NtLoadKeyEx, (void *)NtLoadKeyEx, STACK_ARGS(8));
#endif
#ifdef MIME_SVC_NtMakeTemporaryObject
    KiRegisterService(MIME_SVC_NtMakeTemporaryObject, (void *)NtMakeTemporaryObject, STACK_ARGS(1));
#endif
#ifdef MIME_SVC_NtMapViewOfSection
    KiRegisterService(MIME_SVC_NtMapViewOfSection, (void *)NtMapViewOfSection, STACK_ARGS(10));
#endif
#ifdef MIME_SVC_NtMapViewOfSectionEx
    KiRegisterService(MIME_SVC_NtMapViewOfSectionEx, (void *)NtMapViewOfSectionEx, STACK_ARGS(9));
#endif
#ifdef MIME_SVC_NtNotifyChangeDirectoryFile
    KiRegisterService(MIME_SVC_NtNotifyChangeDirectoryFile, (void *)NtNotifyChangeDirectoryFile, STACK_ARGS(9));
#endif
#ifdef MIME_SVC_NtNotifyChangeDirectoryFileEx
    KiRegisterService(MIME_SVC_NtNotifyChangeDirectoryFileEx, (void *)NtNotifyChangeDirectoryFileEx, STACK_ARGS(10));
#endif
#ifdef MIME_SVC_NtNotifyChangeKey
    KiRegisterService(MIME_SVC_NtNotifyChangeKey, (void *)NtNotifyChangeKey, STACK_ARGS(10));
#endif
#ifdef MIME_SVC_NtNotifyChangeMultipleKeys
    KiRegisterService(MIME_SVC_NtNotifyChangeMultipleKeys, (void *)NtNotifyChangeMultipleKeys, STACK_ARGS(12));
#endif
#ifdef MIME_SVC_NtOpenDirectoryObject
    KiRegisterService(MIME_SVC_NtOpenDirectoryObject, (void *)NtOpenDirectoryObject, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtOpenEvent
    KiRegisterService(MIME_SVC_NtOpenEvent, (void *)NtOpenEvent, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtOpenFile
    KiRegisterService(MIME_SVC_NtOpenFile, (void *)NtOpenFile, STACK_ARGS(6));
#endif
#ifdef MIME_SVC_NtOpenIoCompletion
    KiRegisterService(MIME_SVC_NtOpenIoCompletion, (void *)NtOpenIoCompletion, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtOpenJobObject
    KiRegisterService(MIME_SVC_NtOpenJobObject, (void *)NtOpenJobObject, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtOpenKey
    KiRegisterService(MIME_SVC_NtOpenKey, (void *)NtOpenKey, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtOpenKeyEx
    KiRegisterService(MIME_SVC_NtOpenKeyEx, (void *)NtOpenKeyEx, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtOpenMutant
    KiRegisterService(MIME_SVC_NtOpenMutant, (void *)NtOpenMutant, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtOpenProcess
    KiRegisterService(MIME_SVC_NtOpenProcess, (void *)NtOpenProcess, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtOpenProcessToken
    KiRegisterService(MIME_SVC_NtOpenProcessToken, (void *)NtOpenProcessToken, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtOpenProcessTokenEx
    KiRegisterService(MIME_SVC_NtOpenProcessTokenEx, (void *)NtOpenProcessTokenEx, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtOpenSection
    KiRegisterService(MIME_SVC_NtOpenSection, (void *)NtOpenSection, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtOpenSemaphore
    KiRegisterService(MIME_SVC_NtOpenSemaphore, (void *)NtOpenSemaphore, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtOpenSymbolicLinkObject
    KiRegisterService(MIME_SVC_NtOpenSymbolicLinkObject, (void *)NtOpenSymbolicLinkObject, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtOpenThread
    KiRegisterService(MIME_SVC_NtOpenThread, (void *)NtOpenThread, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtOpenThreadToken
    KiRegisterService(MIME_SVC_NtOpenThreadToken, (void *)NtOpenThreadToken, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtOpenThreadTokenEx
    KiRegisterService(MIME_SVC_NtOpenThreadTokenEx, (void *)NtOpenThreadTokenEx, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtOpenTimer
    KiRegisterService(MIME_SVC_NtOpenTimer, (void *)NtOpenTimer, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtPlugPlayControl
    KiRegisterService(MIME_SVC_NtPlugPlayControl, (void *)NtPlugPlayControl, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtProtectVirtualMemory
    KiRegisterService(MIME_SVC_NtProtectVirtualMemory, (void *)NtProtectVirtualMemory, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtPulseEvent
    KiRegisterService(MIME_SVC_NtPulseEvent, (void *)NtPulseEvent, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtQueryAttributesFile
    KiRegisterService(MIME_SVC_NtQueryAttributesFile, (void *)NtQueryAttributesFile, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtSetDebugFilterState
    KiRegisterService(MIME_SVC_NtSetDebugFilterState, (void *)NtSetDebugFilterState, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtSystemDebugControl
    KiRegisterService(MIME_SVC_NtSystemDebugControl, (void *)NtSystemDebugControl, STACK_ARGS(6));
#endif
#ifdef MIME_SVC_NtQueryDebugFilterState
    KiRegisterService(MIME_SVC_NtQueryDebugFilterState, (void *)NtQueryDebugFilterState, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtQueryDefaultLocale
    KiRegisterService(MIME_SVC_NtQueryDefaultLocale, (void *)NtQueryDefaultLocale, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtQueryDefaultUILanguage
    KiRegisterService(MIME_SVC_NtQueryDefaultUILanguage, (void *)NtQueryDefaultUILanguage, STACK_ARGS(1));
#endif
#ifdef MIME_SVC_NtQueryDirectoryFile
    KiRegisterService(MIME_SVC_NtQueryDirectoryFile, (void *)NtQueryDirectoryFile, STACK_ARGS(11));
#endif
#ifdef MIME_SVC_NtQueryDirectoryFileEx
    KiRegisterService(MIME_SVC_NtQueryDirectoryFileEx, (void *)NtQueryDirectoryFileEx, STACK_ARGS(10));
#endif
#ifdef MIME_SVC_NtQueryDirectoryObject
    KiRegisterService(MIME_SVC_NtQueryDirectoryObject, (void *)NtQueryDirectoryObject, STACK_ARGS(7));
#endif
#ifdef MIME_SVC_NtQueryFullAttributesFile
    KiRegisterService(MIME_SVC_NtQueryFullAttributesFile, (void *)NtQueryFullAttributesFile, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtQueryInformationAtom
    KiRegisterService(MIME_SVC_NtQueryInformationAtom, (void *)NtQueryInformationAtom, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtQueryInformationFile
    KiRegisterService(MIME_SVC_NtQueryInformationFile, (void *)NtQueryInformationFile, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtQueryInformationJobObject
    KiRegisterService(MIME_SVC_NtQueryInformationJobObject, (void *)NtQueryInformationJobObject, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtQueryInformationProcess
    KiRegisterService(MIME_SVC_NtQueryInformationProcess, (void *)NtQueryInformationProcess, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtQueryInformationThread
    KiRegisterService(MIME_SVC_NtQueryInformationThread, (void *)NtQueryInformationThread, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtQueryInformationToken
    KiRegisterService(MIME_SVC_NtQueryInformationToken, (void *)NtQueryInformationToken, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtQueryInstallUILanguage
    KiRegisterService(MIME_SVC_NtQueryInstallUILanguage, (void *)NtQueryInstallUILanguage, STACK_ARGS(1));
#endif
#ifdef MIME_SVC_NtQueryIoCompletion
    KiRegisterService(MIME_SVC_NtQueryIoCompletion, (void *)NtQueryIoCompletion, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtQueryKey
    KiRegisterService(MIME_SVC_NtQueryKey, (void *)NtQueryKey, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtSetInformationKey
    KiRegisterService(MIME_SVC_NtSetInformationKey, (void *)NtSetInformationKey, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtGetNlsSectionPtr
    KiRegisterService(MIME_SVC_NtGetNlsSectionPtr, (void *)NtGetNlsSectionPtr, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtInitializeNlsFiles
    KiRegisterService(MIME_SVC_NtInitializeNlsFiles, (void *)NtInitializeNlsFiles, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtIsUILanguageComitted
    KiRegisterService(MIME_SVC_NtIsUILanguageComitted, (void *)NtIsUILanguageComitted, STACK_ARGS(0));
#endif
#ifdef MIME_SVC_NtGetMUIRegistryInfo
    KiRegisterService(MIME_SVC_NtGetMUIRegistryInfo, (void *)NtGetMUIRegistryInfo, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtManageHotPatch
    KiRegisterService(MIME_SVC_NtManageHotPatch, (void *)NtManageHotPatch, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtCreateWorkerFactory
    KiRegisterService(MIME_SVC_NtCreateWorkerFactory, (void *)NtCreateWorkerFactory, STACK_ARGS(10));
#endif
#ifdef MIME_SVC_NtQueryInformationWorkerFactory
    KiRegisterService(MIME_SVC_NtQueryInformationWorkerFactory, (void *)NtQueryInformationWorkerFactory, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtSetInformationWorkerFactory
    KiRegisterService(MIME_SVC_NtSetInformationWorkerFactory, (void *)NtSetInformationWorkerFactory, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtShutdownWorkerFactory
    KiRegisterService(MIME_SVC_NtShutdownWorkerFactory, (void *)NtShutdownWorkerFactory, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtReleaseWorkerFactoryWorker
    KiRegisterService(MIME_SVC_NtReleaseWorkerFactoryWorker, (void *)NtReleaseWorkerFactoryWorker, STACK_ARGS(1));
#endif
#ifdef MIME_SVC_NtWorkerFactoryWorkerReady
    KiRegisterService(MIME_SVC_NtWorkerFactoryWorkerReady, (void *)NtWorkerFactoryWorkerReady, STACK_ARGS(1));
#endif
#ifdef MIME_SVC_NtWaitForWorkViaWorkerFactory
    KiRegisterService(MIME_SVC_NtWaitForWorkViaWorkerFactory, (void *)NtWaitForWorkViaWorkerFactory, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtCreateTimer2
    KiRegisterService(MIME_SVC_NtCreateTimer2, (void *)NtCreateTimer2, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtSetTimer2
    KiRegisterService(MIME_SVC_NtSetTimer2, (void *)NtSetTimer2, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtCancelTimer2
    KiRegisterService(MIME_SVC_NtCancelTimer2, (void *)NtCancelTimer2, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtQueryLicenseValue
    KiRegisterService(MIME_SVC_NtQueryLicenseValue, (void *)NtQueryLicenseValue, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtQueryObject
    KiRegisterService(MIME_SVC_NtQueryObject, (void *)NtQueryObject, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtQueryPerformanceCounter
    KiRegisterService(MIME_SVC_NtQueryPerformanceCounter, (void *)NtQueryPerformanceCounter, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtQueryAuxiliaryCounterFrequency
    KiRegisterService(MIME_SVC_NtQueryAuxiliaryCounterFrequency, (void *)NtQueryAuxiliaryCounterFrequency, STACK_ARGS(1));
#endif
#ifdef MIME_SVC_NtConvertBetweenAuxiliaryCounterAndPerformanceCounter
    KiRegisterService(MIME_SVC_NtConvertBetweenAuxiliaryCounterAndPerformanceCounter,
                      (void *)NtConvertBetweenAuxiliaryCounterAndPerformanceCounter, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtQuerySection
    KiRegisterService(MIME_SVC_NtQuerySection, (void *)NtQuerySection, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtQuerySecurityObject
    KiRegisterService(MIME_SVC_NtQuerySecurityObject, (void *)NtQuerySecurityObject, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtQuerySecurityAttributesToken
    KiRegisterService(MIME_SVC_NtQuerySecurityAttributesToken, (void *)NtQuerySecurityAttributesToken, STACK_ARGS(6));
#endif
#ifdef MIME_SVC_NtQuerySymbolicLinkObject
    KiRegisterService(MIME_SVC_NtQuerySymbolicLinkObject, (void *)NtQuerySymbolicLinkObject, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtQuerySystemInformation
    KiRegisterService(MIME_SVC_NtQuerySystemInformation, (void *)NtQuerySystemInformation, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtQuerySystemEnvironmentValue
    KiRegisterService(MIME_SVC_NtQuerySystemEnvironmentValue, (void *)NtQuerySystemEnvironmentValue, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtQuerySystemEnvironmentValueEx
    KiRegisterService(MIME_SVC_NtQuerySystemEnvironmentValueEx, (void *)NtQuerySystemEnvironmentValueEx, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtSetSystemEnvironmentValue
    KiRegisterService(MIME_SVC_NtSetSystemEnvironmentValue, (void *)NtSetSystemEnvironmentValue, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtSetSystemEnvironmentValueEx
    KiRegisterService(MIME_SVC_NtSetSystemEnvironmentValueEx, (void *)NtSetSystemEnvironmentValueEx, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtQuerySystemTime
    KiRegisterService(MIME_SVC_NtQuerySystemTime, (void *)NtQuerySystemTime, STACK_ARGS(1));
#endif
#ifdef MIME_SVC_NtQueryTimerResolution
    KiRegisterService(MIME_SVC_NtQueryTimerResolution, (void *)NtQueryTimerResolution, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtQueryValueKey
    KiRegisterService(MIME_SVC_NtQueryValueKey, (void *)NtQueryValueKey, STACK_ARGS(6));
#endif
#ifdef MIME_SVC_NtQueryVirtualMemory
    KiRegisterService(MIME_SVC_NtQueryVirtualMemory, (void *)NtQueryVirtualMemory, STACK_ARGS(6));
#endif
#ifdef MIME_SVC_NtQueryVolumeInformationFile
    KiRegisterService(MIME_SVC_NtQueryVolumeInformationFile, (void *)NtQueryVolumeInformationFile, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtSetVolumeInformationFile
    KiRegisterService(MIME_SVC_NtSetVolumeInformationFile, (void *)NtSetVolumeInformationFile, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtQueryWnfStateData
    KiRegisterService(MIME_SVC_NtQueryWnfStateData, (void *)NtQueryWnfStateData, STACK_ARGS(6));
#endif
#ifdef MIME_SVC_NtQueryWnfStateNameInformation
    KiRegisterService(MIME_SVC_NtQueryWnfStateNameInformation, (void *)NtQueryWnfStateNameInformation, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtQueueApcThread
    KiRegisterService(MIME_SVC_NtQueueApcThread, (void *)NtQueueApcThread, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtQueueApcThreadEx
    KiRegisterService(MIME_SVC_NtQueueApcThreadEx, (void *)NtQueueApcThreadEx, STACK_ARGS(6));
#endif
#ifdef MIME_SVC_NtQueueApcThreadEx2
    KiRegisterService(MIME_SVC_NtQueueApcThreadEx2, (void *)NtQueueApcThreadEx2, STACK_ARGS(7));
#endif
#ifdef MIME_SVC_NtRaiseException
    KiRegisterService(MIME_SVC_NtRaiseException, (void *)NtRaiseException, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtLockFile
    KiRegisterService(MIME_SVC_NtLockFile, (void *)NtLockFile, STACK_ARGS(10));
#endif
#ifdef MIME_SVC_NtUnlockFile
    KiRegisterService(MIME_SVC_NtUnlockFile, (void *)NtUnlockFile, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtReadFile
    KiRegisterService(MIME_SVC_NtReadFile, (void *)NtReadFile, STACK_ARGS(9));
#endif
#ifdef MIME_SVC_NtReadFileScatter
    KiRegisterService(MIME_SVC_NtReadFileScatter, (void *)NtReadFileScatter, STACK_ARGS(9));
#endif
#ifdef MIME_SVC_NtWriteFileGather
    KiRegisterService(MIME_SVC_NtWriteFileGather, (void *)NtWriteFileGather, STACK_ARGS(9));
#endif
#ifdef MIME_SVC_NtReadVirtualMemory
    KiRegisterService(MIME_SVC_NtReadVirtualMemory, (void *)NtReadVirtualMemory, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtReleaseMutant
    KiRegisterService(MIME_SVC_NtReleaseMutant, (void *)NtReleaseMutant, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtReleaseSemaphore
    KiRegisterService(MIME_SVC_NtReleaseSemaphore, (void *)NtReleaseSemaphore, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtRemoveIoCompletion
    KiRegisterService(MIME_SVC_NtRemoveIoCompletion, (void *)NtRemoveIoCompletion, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtRemoveIoCompletionEx
    KiRegisterService(MIME_SVC_NtRemoveIoCompletionEx, (void *)NtRemoveIoCompletionEx, STACK_ARGS(6));
#endif
#ifdef MIME_SVC_NtResetEvent
    KiRegisterService(MIME_SVC_NtResetEvent, (void *)NtResetEvent, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtResetWriteWatch
    KiRegisterService(MIME_SVC_NtResetWriteWatch, (void *)NtResetWriteWatch, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtResumeProcess
    KiRegisterService(MIME_SVC_NtResumeProcess, (void *)NtResumeProcess, STACK_ARGS(1));
#endif
#ifdef MIME_SVC_NtResumeThread
    KiRegisterService(MIME_SVC_NtResumeThread, (void *)NtResumeThread, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtSetContextThread
    KiRegisterService(MIME_SVC_NtSetContextThread, (void *)NtSetContextThread, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtSetEvent
    KiRegisterService(MIME_SVC_NtSetEvent, (void *)NtSetEvent, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtSetInformationFile
    KiRegisterService(MIME_SVC_NtSetInformationFile, (void *)NtSetInformationFile, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtSetInformationJobObject
    KiRegisterService(MIME_SVC_NtSetInformationJobObject, (void *)NtSetInformationJobObject, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtSetInformationObject
    KiRegisterService(MIME_SVC_NtSetInformationObject, (void *)NtSetInformationObject, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtSetInformationProcess
    KiRegisterService(MIME_SVC_NtSetInformationProcess, (void *)NtSetInformationProcess, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtSetInformationThread
    KiRegisterService(MIME_SVC_NtSetInformationThread, (void *)NtSetInformationThread, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtSetInformationToken
    KiRegisterService(MIME_SVC_NtSetInformationToken, (void *)NtSetInformationToken, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtSetInformationVirtualMemory
    KiRegisterService(MIME_SVC_NtSetInformationVirtualMemory, (void *)NtSetInformationVirtualMemory, STACK_ARGS(6));
#endif
#ifdef MIME_SVC_NtSetIoCompletion
    KiRegisterService(MIME_SVC_NtSetIoCompletion, (void *)NtSetIoCompletion, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtSetIoCompletionEx
    KiRegisterService(MIME_SVC_NtSetIoCompletionEx, (void *)NtSetIoCompletionEx, STACK_ARGS(6));
#endif
#ifdef MIME_SVC_NtSetSecurityObject
    KiRegisterService(MIME_SVC_NtSetSecurityObject, (void *)NtSetSecurityObject, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtSetSystemInformation
    KiRegisterService(MIME_SVC_NtSetSystemInformation, (void *)NtSetSystemInformation, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtSetTimerEx
    KiRegisterService(MIME_SVC_NtSetTimerEx, (void *)NtSetTimerEx, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtSetTimer
    KiRegisterService(MIME_SVC_NtSetTimer, (void *)NtSetTimer, STACK_ARGS(7));
#endif
#ifdef MIME_SVC_NtSetTimerResolution
    KiRegisterService(MIME_SVC_NtSetTimerResolution, (void *)NtSetTimerResolution, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtSetValueKey
    KiRegisterService(MIME_SVC_NtSetValueKey, (void *)NtSetValueKey, STACK_ARGS(6));
#endif
#ifdef MIME_SVC_NtSetWnfProcessNotificationEvent
    KiRegisterService(MIME_SVC_NtSetWnfProcessNotificationEvent, (void *)NtSetWnfProcessNotificationEvent, STACK_ARGS(1));
#endif
#ifdef MIME_SVC_NtShutdownSystem
    KiRegisterService(MIME_SVC_NtShutdownSystem, (void *)NtShutdownSystem, STACK_ARGS(1));
#endif
#ifdef MIME_SVC_NtSignalAndWaitForSingleObject
    KiRegisterService(MIME_SVC_NtSignalAndWaitForSingleObject, (void *)NtSignalAndWaitForSingleObject, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtSubscribeWnfStateChange
    KiRegisterService(MIME_SVC_NtSubscribeWnfStateChange, (void *)NtSubscribeWnfStateChange, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtSuspendProcess
    KiRegisterService(MIME_SVC_NtSuspendProcess, (void *)NtSuspendProcess, STACK_ARGS(1));
#endif
#ifdef MIME_SVC_NtSuspendThread
    KiRegisterService(MIME_SVC_NtSuspendThread, (void *)NtSuspendThread, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtTerminateJobObject
    KiRegisterService(MIME_SVC_NtTerminateJobObject, (void *)NtTerminateJobObject, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtTerminateProcess
    KiRegisterService(MIME_SVC_NtTerminateProcess, (void *)NtTerminateProcess, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtTerminateThread
    KiRegisterService(MIME_SVC_NtTerminateThread, (void *)NtTerminateThread, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtTestAlert
    KiRegisterService(MIME_SVC_NtTestAlert, (void *)NtTestAlert, STACK_ARGS(0));
#endif
#ifdef MIME_SVC_NtTraceControl
    KiRegisterService(MIME_SVC_NtTraceControl, (void *)NtTraceControl, STACK_ARGS(6));
#endif
#ifdef MIME_SVC_NtTraceEvent
    KiRegisterService(MIME_SVC_NtTraceEvent, (void *)NtTraceEvent, STACK_ARGS(4));
#endif
#ifdef MIME_SVC_NtUnloadKey
    KiRegisterService(MIME_SVC_NtUnloadKey, (void *)NtUnloadKey, STACK_ARGS(1));
#endif
#ifdef MIME_SVC_NtUnloadKey2
    KiRegisterService(MIME_SVC_NtUnloadKey2, (void *)NtUnloadKey2, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtUnloadKeyEx
    KiRegisterService(MIME_SVC_NtUnloadKeyEx, (void *)NtUnloadKeyEx, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtUnmapViewOfSection
    KiRegisterService(MIME_SVC_NtUnmapViewOfSection, (void *)NtUnmapViewOfSection, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtUnmapViewOfSectionEx
    KiRegisterService(MIME_SVC_NtUnmapViewOfSectionEx, (void *)NtUnmapViewOfSectionEx, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtUnsubscribeWnfStateChange
    KiRegisterService(MIME_SVC_NtUnsubscribeWnfStateChange, (void *)NtUnsubscribeWnfStateChange, STACK_ARGS(1));
#endif
#ifdef MIME_SVC_NtUpdateWnfStateData
    KiRegisterService(MIME_SVC_NtUpdateWnfStateData, (void *)NtUpdateWnfStateData, STACK_ARGS(7));
#endif
#ifdef MIME_SVC_NtWaitForAlertByThreadId
    KiRegisterService(MIME_SVC_NtWaitForAlertByThreadId, (void *)NtWaitForAlertByThreadId, STACK_ARGS(2));
#endif
#ifdef MIME_SVC_NtWaitForMultipleObjects
    KiRegisterService(MIME_SVC_NtWaitForMultipleObjects, (void *)NtWaitForMultipleObjects, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtWaitForMultipleObjects32
    KiRegisterService(MIME_SVC_NtWaitForMultipleObjects32, (void *)NtWaitForMultipleObjects32, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtWaitForSingleObject
    KiRegisterService(MIME_SVC_NtWaitForSingleObject, (void *)NtWaitForSingleObject, STACK_ARGS(3));
#endif
#ifdef MIME_SVC_NtWriteFile
    KiRegisterService(MIME_SVC_NtWriteFile, (void *)NtWriteFile, STACK_ARGS(9));
#endif
#ifdef MIME_SVC_NtWriteVirtualMemory
    KiRegisterService(MIME_SVC_NtWriteVirtualMemory, (void *)NtWriteVirtualMemory, STACK_ARGS(5));
#endif
#ifdef MIME_SVC_NtYieldExecution
    KiRegisterService(MIME_SVC_NtYieldExecution, (void *)NtYieldExecution, STACK_ARGS(0));
#endif
}
