/* unsupported connection policy, proxy service, HVSI and protocol-handle exports. each logs once and fails. */
#include "winhttp_private.h"
#include <mime/stub.h>

DWORD WINAPI WinHttpConnectionDeletePolicyEntries(DWORD_PTR a0, DWORD_PTR a1)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI WinHttpConnectionDeletePolicyEntriesByAppSid(DWORD_PTR a0, DWORD_PTR a1, DWORD_PTR a2)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI WinHttpConnectionDeleteProxyInfo(DWORD_PTR a0, DWORD_PTR a1)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

void WINAPI WinHttpConnectionFreeNameList(DWORD_PTR a0)
{
    MIME_STUB("winhttp");
}

void WINAPI WinHttpConnectionFreeProxyInfo(DWORD_PTR a0)
{
    MIME_STUB("winhttp");
}

void WINAPI WinHttpConnectionFreeProxyList(DWORD_PTR a0)
{
    MIME_STUB("winhttp");
}

DWORD WINAPI WinHttpConnectionGetNameList(DWORD_PTR a0)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI WinHttpConnectionGetProxyInfo(DWORD_PTR a0, DWORD_PTR a1, DWORD_PTR a2)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI WinHttpConnectionGetProxyList(DWORD_PTR a0, DWORD_PTR a1)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI WinHttpConnectionOnlyConvert(DWORD_PTR a0, DWORD_PTR a1)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI WinHttpConnectionOnlyReceive(DWORD_PTR a0, DWORD_PTR a1, DWORD_PTR a2, DWORD_PTR a3, DWORD_PTR a4, DWORD_PTR a5)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI WinHttpConnectionOnlySend(DWORD_PTR a0, DWORD_PTR a1, DWORD_PTR a2, DWORD_PTR a3, DWORD_PTR a4)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI WinHttpConnectionSetPolicyEntries(DWORD_PTR a0, DWORD_PTR a1, DWORD_PTR a2)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI WinHttpConnectionSetProxyInfo(DWORD_PTR a0, DWORD_PTR a1, DWORD_PTR a2)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI WinHttpConnectionUpdateIfIndexTable(DWORD_PTR a0, DWORD_PTR a1)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI WinHttpCreateProxyList(DWORD_PTR a0, DWORD_PTR a1, DWORD_PTR a2, DWORD_PTR a3, DWORD_PTR a4)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI WinHttpCreateProxyManager(DWORD_PTR a0, DWORD_PTR a1, DWORD_PTR a2, DWORD_PTR a3)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI WinHttpCreateProxyResult(DWORD_PTR a0, DWORD_PTR a1, DWORD_PTR a2, DWORD_PTR a3, DWORD_PTR a4, DWORD_PTR a5)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI WinHttpCreateUiCompatibleProxyString(DWORD_PTR a0, DWORD_PTR a1)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

void WINAPI WinHttpFreeProxySettings(DWORD_PTR a0)
{
    MIME_STUB("winhttp");
}

void WINAPI WinHttpFreeProxySettingsEx(DWORD_PTR a0, DWORD_PTR a1)
{
    MIME_STUB("winhttp");
}

void WINAPI WinHttpFreeQueryConnectionGroupResult(DWORD_PTR a0)
{
    MIME_STUB("winhttp");
}

DWORD WINAPI WinHttpGetProxyForUrlHvsi(DWORD_PTR a0, DWORD_PTR a1, DWORD_PTR a2, DWORD_PTR a3, DWORD_PTR a4, DWORD_PTR a5, DWORD_PTR a6, DWORD_PTR a7, DWORD_PTR a8)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI WinHttpGetProxySettingsEx(DWORD_PTR a0, DWORD_PTR a1, DWORD_PTR a2, DWORD_PTR a3)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI WinHttpGetProxySettingsResultEx(DWORD_PTR a0, DWORD_PTR a1)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI WinHttpGetProxySettingsVersion(DWORD_PTR a0, DWORD_PTR a1)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI WinHttpGetTunnelSocket(DWORD_PTR a0, DWORD_PTR a1, DWORD_PTR a2, DWORD_PTR a3)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI WinHttpProbeConnectivity(DWORD_PTR a0, DWORD_PTR a1, DWORD_PTR a2, DWORD_PTR a3, DWORD_PTR a4, DWORD_PTR a5)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

HINTERNET WINAPI WinHttpProtocolCompleteUpgrade(DWORD_PTR a0, DWORD_PTR a1)
{
    MIME_STUB("winhttp");
    SetLastError(ERROR_NOT_SUPPORTED);
    return nullptr;
}

DWORD WINAPI WinHttpProtocolReceive(DWORD_PTR a0, DWORD_PTR a1, DWORD_PTR a2, DWORD_PTR a3, DWORD_PTR a4, DWORD_PTR a5)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI WinHttpProtocolSend(DWORD_PTR a0, DWORD_PTR a1, DWORD_PTR a2, DWORD_PTR a3, DWORD_PTR a4)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI WinHttpQueryConnectionGroup(DWORD_PTR a0, DWORD_PTR a1, DWORD_PTR a2, DWORD_PTR a3, DWORD_PTR a4)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI WinHttpReadProxySettings(DWORD_PTR a0, DWORD_PTR a1, DWORD_PTR a2, DWORD_PTR a3, DWORD_PTR a4, DWORD_PTR a5, DWORD_PTR a6)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI WinHttpReadProxySettingsHvsi(DWORD_PTR a0, DWORD_PTR a1, DWORD_PTR a2, DWORD_PTR a3, DWORD_PTR a4, DWORD_PTR a5, DWORD_PTR a6, DWORD_PTR a7)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI WinHttpRefreshProxySettings(DWORD_PTR a0, DWORD_PTR a1, DWORD_PTR a2, DWORD_PTR a3)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI WinHttpRegisterProxyChangeNotification(DWORD_PTR a0, DWORD_PTR a1, DWORD_PTR a2, DWORD_PTR a3, DWORD_PTR a4)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI WinHttpResolverGetProxyForUrl(DWORD_PTR a0, DWORD_PTR a1, DWORD_PTR a2, DWORD_PTR a3, DWORD_PTR a4, DWORD_PTR a5, DWORD_PTR a6)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI WinHttpSaveProxyCredentials(DWORD_PTR a0, DWORD_PTR a1, DWORD_PTR a2, DWORD_PTR a3)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI WinHttpSetProxySettingsPerUser(DWORD_PTR a0)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI WinHttpSetSecureLegacyServersAppCompat(void)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI WinHttpUnregisterProxyChangeNotification(DWORD_PTR a0)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI WinHttpWriteProxySettings(DWORD_PTR a0, DWORD_PTR a1, DWORD_PTR a2)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}
