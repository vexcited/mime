/*
 * resutils.dll, the helpers of the dlls that run resources of a cluster. there are no clusters here, so what the helpers
 * would work on is not there: a call that gives back a block or a handle gives none, a test is false, and the others say
 * they are not implemented. generated from the export table of windows, one function for each name.
 */
#include <mime/win32/winbase.h>

#define RESUTILS_EXPORT DECLSPEC_EXPORT

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

#define NOTHING_CALL(name)                                                                                             \
    RESUTILS_EXPORT PVOID WINAPI name(void)                                                                            \
    {                                                                                                                  \
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);                                                                      \
        return nullptr;                                                                                                \
    }

#define HANDLE_CALL(name) NOTHING_CALL(name)

#define BOOL_CALL(name)                                                                                                \
    RESUTILS_EXPORT BOOL WINAPI name(void)                                                                             \
    {                                                                                                                  \
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);                                                                      \
        return FALSE;                                                                                                  \
    }

#define ERROR_CALL(name)                                                                                               \
    RESUTILS_EXPORT DWORD WINAPI name(void)                                                                            \
    {                                                                                                                  \
        return ERROR_CALL_NOT_IMPLEMENTED;                                                                             \
    }

#define VOID_CALL(name)                                                                                                \
    RESUTILS_EXPORT void WINAPI name(void)                                                                             \
    {                                                                                                                  \
    }

ERROR_CALL(ClusWorkerStart)
BOOL_CALL(CloseClusterCryptProvider)
ERROR_CALL(ClusAddClusterHealthFault)
ERROR_CALL(ClusGetClusterHealthFaults)
ERROR_CALL(ClusRemoveClusterHealthFault)
BOOL_CALL(ClusWorkerCheckTerminate)
ERROR_CALL(ClusWorkerCreate)
ERROR_CALL(ClusWorkerTerminate)
ERROR_CALL(ClusWorkerTerminateEx)
ERROR_CALL(ClusWorkersTerminate)
ERROR_CALL(ClusterClearBackupStateForSharedVolume)
ERROR_CALL(ClusterDecrypt)
ERROR_CALL(ClusterEncrypt)
ERROR_CALL(ClusterEnumTasks)
ERROR_CALL(ClusterFileShareCreate)
ERROR_CALL(ClusterFileShareDelete)
ERROR_CALL(ClusterFileShareUpdate)
VOID_CALL(ClusterFreeTaskInfo)
VOID_CALL(ClusterFreeTaskList)
ERROR_CALL(ClusterGetTaskNode)
ERROR_CALL(ClusterGetVolumeNameForVolumeMountPoint)
ERROR_CALL(ClusterGetVolumePathName)
BOOL_CALL(ClusterIsClusterDisk)
BOOL_CALL(ClusterIsPathOnSharedVolume)
ERROR_CALL(ClusterPrepareSharedVolumeForBackup)
ERROR_CALL(ClusterSharedVolumeCheckSnapshotPresence)
ERROR_CALL(ClusterSharedVolumeCreateSnapshot)
ERROR_CALL(ClusterSharedVolumeReleaseSnapshot)
ERROR_CALL(ClusterTaskChangeFromXML)
ERROR_CALL(ClusterTaskChangeFromXMLFile)
ERROR_CALL(ClusterTaskChange_TS_V1)
ERROR_CALL(ClusterTaskCreateFromXML)
ERROR_CALL(ClusterTaskCreateFromXMLFile)
ERROR_CALL(ClusterTaskCreate_TS_V1)
ERROR_CALL(ClusterTaskDelete)
ERROR_CALL(ClusterTaskDelete_TS_V1)
ERROR_CALL(ClusterTaskExists_TS_V1)
ERROR_CALL(ClusterTaskQuery)
ERROR_CALL(CreateClusterStorageSpacesClustering)
ERROR_CALL(CreateClusterStorageSpacesResourceLocator)
ERROR_CALL(CreateClusterStorageSpacesSubProvider)
VOID_CALL(FreeClusterCrypt)
VOID_CALL(FreeClusterHealthFault)
VOID_CALL(FreeClusterHealthFaultArray)
ERROR_CALL(InitializeClusterHealthFault)
ERROR_CALL(InitializeClusterHealthFaultArray)
NOTHING_CALL(OpenClusterCryptProvider)
NOTHING_CALL(OpenClusterCryptProviderEx)
ERROR_CALL(ResUtilAddUnknownProperties)
ERROR_CALL(ResUtilCreateDirectoryTree)
NOTHING_CALL(ResUtilDupGroup)
NOTHING_CALL(ResUtilDupParameterBlock)
NOTHING_CALL(ResUtilDupResource)
NOTHING_CALL(ResUtilDupString)
HANDLE_CALL(ResUtilEnumGroups)
HANDLE_CALL(ResUtilEnumGroupsEx)
HANDLE_CALL(ResUtilEnumPrivateProperties)
HANDLE_CALL(ResUtilEnumProperties)
HANDLE_CALL(ResUtilEnumResources)
HANDLE_CALL(ResUtilEnumResourcesEx)
HANDLE_CALL(ResUtilEnumResourcesEx2)
ERROR_CALL(ResUtilExpandEnvironmentStrings)
ERROR_CALL(ResUtilFindBinaryProperty)
ERROR_CALL(ResUtilFindDependentDiskResourceDriveLetter)
ERROR_CALL(ResUtilFindDwordProperty)
ERROR_CALL(ResUtilFindExpandSzProperty)
ERROR_CALL(ResUtilFindExpandedSzProperty)
ERROR_CALL(ResUtilFindFileTimeProperty)
ERROR_CALL(ResUtilFindLongProperty)
ERROR_CALL(ResUtilFindMultiSzProperty)
ERROR_CALL(ResUtilFindSzProperty)
ERROR_CALL(ResUtilFindULargeIntegerProperty)
VOID_CALL(ResUtilFreeEnvironment)
VOID_CALL(ResUtilFreeParameterBlock)
ERROR_CALL(ResUtilGetAllProperties)
ERROR_CALL(ResUtilGetBinaryProperty)
ERROR_CALL(ResUtilGetBinaryValue)
ERROR_CALL(ResUtilGetClusterGroupType)
ERROR_CALL(ResUtilGetClusterId)
ERROR_CALL(ResUtilGetClusterRoleState)
ERROR_CALL(ResUtilGetCoreClusterResources)
ERROR_CALL(ResUtilGetCoreClusterResourcesEx)
ERROR_CALL(ResUtilGetCoreGroup)
ERROR_CALL(ResUtilGetDwordProperty)
ERROR_CALL(ResUtilGetDwordValue)
NOTHING_CALL(ResUtilGetEnvironmentWithNetName)
ERROR_CALL(ResUtilGetFileTimeProperty)
ERROR_CALL(ResUtilGetLongProperty)
ERROR_CALL(ResUtilGetMultiSzProperty)
ERROR_CALL(ResUtilGetPrivateProperties)
ERROR_CALL(ResUtilGetProperties)
ERROR_CALL(ResUtilGetPropertiesToParameterBlock)
ERROR_CALL(ResUtilGetProperty)
ERROR_CALL(ResUtilGetPropertyFormats)
ERROR_CALL(ResUtilGetPropertySize)
ERROR_CALL(ResUtilGetQwordValue)
ERROR_CALL(ResUtilGetResourceDependency)
ERROR_CALL(ResUtilGetResourceDependencyByClass)
ERROR_CALL(ResUtilGetResourceDependencyByClassEx)
ERROR_CALL(ResUtilGetResourceDependencyByName)
ERROR_CALL(ResUtilGetResourceDependencyByNameAndClass)
ERROR_CALL(ResUtilGetResourceDependencyByNameEx)
ERROR_CALL(ResUtilGetResourceDependencyEx)
ERROR_CALL(ResUtilGetResourceDependentIPAddressProps)
ERROR_CALL(ResUtilGetResourceName)
ERROR_CALL(ResUtilGetResourceNameDependency)
ERROR_CALL(ResUtilGetResourceNameDependencyEx)
ERROR_CALL(ResUtilGetSzProperty)
ERROR_CALL(ResUtilGetSzValue)
BOOL_CALL(ResUtilGroupsEqual)
BOOL_CALL(ResUtilIsPathValid)
BOOL_CALL(ResUtilIsResourceClassEqual)
ERROR_CALL(ResUtilLeftPaxosIsLessThanRight)
HANDLE_CALL(ResUtilNodeEnum)
ERROR_CALL(ResUtilPaxosComparer)
ERROR_CALL(ResUtilPropertyListFromParameterBlock)
ERROR_CALL(ResUtilRemoveResourceServiceEnvironment)
ERROR_CALL(ResUtilResourceDepEnum)
BOOL_CALL(ResUtilResourceTypesEqual)
BOOL_CALL(ResUtilResourcesEqual)
ERROR_CALL(ResUtilSetBinaryValue)
ERROR_CALL(ResUtilSetDwordValue)
ERROR_CALL(ResUtilSetExpandSzValue)
ERROR_CALL(ResUtilSetMultiSzValue)
ERROR_CALL(ResUtilSetPrivatePropertyList)
ERROR_CALL(ResUtilSetPropertyParameterBlock)
ERROR_CALL(ResUtilSetPropertyParameterBlockEx)
ERROR_CALL(ResUtilSetPropertyTable)
ERROR_CALL(ResUtilSetPropertyTableEx)
ERROR_CALL(ResUtilSetQwordValue)
ERROR_CALL(ResUtilSetResourceServiceEnvironment)
ERROR_CALL(ResUtilSetResourceServiceStartParameters)
ERROR_CALL(ResUtilSetResourceServiceStartParametersEx)
ERROR_CALL(ResUtilSetSzValue)
ERROR_CALL(ResUtilSetUnknownProperties)
ERROR_CALL(ResUtilSetValueEx)
ERROR_CALL(ResUtilStartResourceService)
ERROR_CALL(ResUtilStopResourceService)
ERROR_CALL(ResUtilStopService)
ERROR_CALL(ResUtilTerminateServiceProcessFromResDll)
ERROR_CALL(ResUtilVerifyPrivatePropertyList)
ERROR_CALL(ResUtilVerifyPropertyTable)
ERROR_CALL(ResUtilVerifyResourceService)
ERROR_CALL(ResUtilVerifyService)
ERROR_CALL(ResUtilVerifyShutdownSafe)
ERROR_CALL(ResUtilsDeleteKeyTree)
