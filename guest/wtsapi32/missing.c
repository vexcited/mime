/* temporary probes for the wtsapi32 exports mime lacks. */
#include <mime/nt.h>
#include <mime/win32/winbase.h>

__declspec(dllexport) ULONG_PTR WINAPI IsInteractiveUserSession(void)
{
    DbgPrint("MISSING wtsapi %s\n", "IsInteractiveUserSession");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI QueryActiveSession(void)
{
    DbgPrint("MISSING wtsapi %s\n", "QueryActiveSession");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI QueryUserToken(void)
{
    DbgPrint("MISSING wtsapi %s\n", "QueryUserToken");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI RegisterUsertokenForNoWinlogon(void)
{
    DbgPrint("MISSING wtsapi %s\n", "RegisterUsertokenForNoWinlogon");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSActiveSessionExists(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSActiveSessionExists");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSCloudAuthClose(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSCloudAuthClose");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSCloudAuthConvertAssertionToSerializedUserCredential(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSCloudAuthConvertAssertionToSerializedUserCredential");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSCloudAuthDuplicateSerializedUserCredential(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSCloudAuthDuplicateSerializedUserCredential");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSCloudAuthGetServerNonce(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSCloudAuthGetServerNonce");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSCloudAuthNetworkLogonWithSerializedCredential(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSCloudAuthNetworkLogonWithSerializedCredential");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSCloudAuthOpen(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSCloudAuthOpen");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSConnectSessionA(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSConnectSessionA");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSConnectSessionW(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSConnectSessionW");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSCreateListenerA(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSCreateListenerA");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSCreateListenerW(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSCreateListenerW");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSEnableChildSessions(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSEnableChildSessions");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSEnumerateListenersA(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSEnumerateListenersA");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSEnumerateListenersW(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSEnumerateListenersW");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSEnumerateProcessesA(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSEnumerateProcessesA");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSEnumerateProcessesExA(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSEnumerateProcessesExA");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSEnumerateProcessesExW(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSEnumerateProcessesExW");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSEnumerateServersA(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSEnumerateServersA");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSEnumerateServersW(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSEnumerateServersW");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSEnumerateSessionsA(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSEnumerateSessionsA");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSEnumerateSessionsExA(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSEnumerateSessionsExA");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSEnumerateSessionsExW(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSEnumerateSessionsExW");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSFreeMemoryExA(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSFreeMemoryExA");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSFreeMemoryExW(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSFreeMemoryExW");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSGetChildSessionId(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSGetChildSessionId");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSGetListenerSecurityA(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSGetListenerSecurityA");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSGetListenerSecurityW(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSGetListenerSecurityW");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSIsChildSessionsEnabled(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSIsChildSessionsEnabled");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSOpenServerA(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSOpenServerA");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSOpenServerExA(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSOpenServerExA");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSOpenServerExW(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSOpenServerExW");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSQueryListenerConfigA(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSQueryListenerConfigA");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSQueryListenerConfigW(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSQueryListenerConfigW");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSQueryUserConfigA(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSQueryUserConfigA");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSQueryUserConfigW(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSQueryUserConfigW");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSSendMessageA(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSSendMessageA");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSSetListenerSecurityA(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSSetListenerSecurityA");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSSetListenerSecurityW(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSSetListenerSecurityW");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSSetRenderHint(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSSetRenderHint");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSSetSessionInformationA(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSSetSessionInformationA");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSSetSessionInformationW(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSSetSessionInformationW");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSSetUserConfigA(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSSetUserConfigA");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSSetUserConfigW(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSSetUserConfigW");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSShutdownSystem(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSShutdownSystem");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSStartRemoteControlSessionA(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSStartRemoteControlSessionA");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSStartRemoteControlSessionW(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSStartRemoteControlSessionW");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSStopRemoteControlSession(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSStopRemoteControlSession");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSTerminateProcess(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSTerminateProcess");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSVirtualChannelClose(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSVirtualChannelClose");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSVirtualChannelOpen(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSVirtualChannelOpen");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSVirtualChannelOpenEx(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSVirtualChannelOpenEx");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSVirtualChannelPurgeInput(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSVirtualChannelPurgeInput");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSVirtualChannelPurgeOutput(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSVirtualChannelPurgeOutput");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSVirtualChannelQuery(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSVirtualChannelQuery");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSVirtualChannelRead(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSVirtualChannelRead");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSVirtualChannelWrite(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSVirtualChannelWrite");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI WTSWaitSystemEvent(void)
{
    DbgPrint("MISSING wtsapi %s\n", "WTSWaitSystemEvent");
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}
