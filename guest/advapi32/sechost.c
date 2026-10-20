/*
 * what sechost.dll of windows exports and mime's advapi32 had not: the services and security functions that windows
 * keeps in sechost and has advapi32 call, the credential store, the audit policy, event tracing sessions and the
 * private entry points of the service control manager. sechost.dll of mime forwards to these (docs/sechost.md). the
 * service and account functions are real; the rest either answer what an empty machine would or fail with the error of
 * a function that is not there, and log their name once.
 */
#define MIME_BUILD_ADVAPI32 1
#include "advapi32_private.h"
#include <mime/stubexport.h>
#include <mime/win32/winsvc.h>

#define LSA_NOT_IMPLEMENTED ((NTSTATUS)0xC0000002L)

/* ------------------------------------------------------------------ services */

/* the unsuffixed name of windows' sechost is the wide function. */
ADVAPI DWORD WINAPI NotifyServiceStatusChange(SC_HANDLE service, DWORD mask, PSERVICE_NOTIFYW notify)
{
    return NotifyServiceStatusChangeW(service, mask, notify);
}

/* what SetServiceBits calls: the bits of the service the manager keeps, the string is the name of the service. */
ADVAPI DWORD WINAPI I_ScSetServiceBitsW(SERVICE_STATUS_HANDLE handle, DWORD bits, BOOL set, BOOL update, LPWSTR name)
{
    (void)name;
    return SetServiceBits(handle, bits, set, update) ? ERROR_SUCCESS : GetLastError();
}

ADVAPI DWORD WINAPI I_ScSetServiceBitsA(SERVICE_STATUS_HANDLE handle, DWORD bits, BOOL set, BOOL update, LPSTR name)
{
    (void)name;
    return SetServiceBits(handle, bits, set, update) ? ERROR_SUCCESS : GetLastError();
}

/* svchost.exe hands the library the function that answers the manager's low resource notices. the manager sends none. */
ADVAPI DWORD WINAPI I_RegisterSvchostNotificationCallback(PVOID callback)
{
    (void)callback;
    return ERROR_SUCCESS;
}

MIME_STUB_ERROR("sechost", SC_HANDLE, CreateServiceEx, 16, ERROR_CALL_NOT_IMPLEMENTED, nullptr)
MIME_STUB_ERROR("sechost", BOOL, QueryServiceDynamicInformation, 3, ERROR_CALL_NOT_IMPLEMENTED, FALSE)
MIME_STUB_RETURN("sechost", DWORD, QueryUserServiceName, 5, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", DWORD, QueryUserServiceNameForContext, 4, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", DWORD, QueryLocalUserServiceName, 3, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", DWORD, ReparseServiceConfig, 3, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", DWORD, I_ScReparseServiceDatabase, 1, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", DWORD, SubscribeServiceChangeNotifications, 5, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", PVOID, UnsubscribeServiceChangeNotifications, 1, nullptr)
MIME_STUB_RETURN("sechost", DWORD, GetServiceDirectory, 5, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", DWORD, GetSharedServiceDirectory, 5, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", DWORD, GetServiceRegistryStateKey, 4, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", DWORD, GetSharedServiceRegistryStateKey, 4, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", DWORD, GetServiceProcessToken, 3, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", DWORD, I_QueryTagInformation, 3, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", DWORD, I_ScBroadcastServiceControlMessage, 4, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", DWORD, I_ScSendTSMessage, 4, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", BOOL, I_ScIsSecurityProcess, 0, FALSE)
MIME_STUB_RETURN("sechost", DWORD, I_ScPnPGetServiceName, 3, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", DWORD, I_ScQueryServiceConfig, 3, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", PVOID, I_ScRegisterDeviceNotification, 3, nullptr)
MIME_STUB_RETURN("sechost", DWORD, I_ScUnregisterDeviceNotification, 1, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", DWORD, I_ScRegisterPreshutdownRestart, 2, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", PVOID, I_ScRpcBindA, 1, nullptr)
MIME_STUB_RETURN("sechost", PVOID, I_ScRpcBindW, 1, nullptr)
MIME_STUB_RETURN("sechost", DWORD, I_ScSendPnPMessage, 6, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", DWORD, I_ScValidatePnPService, 4, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", DWORD, ScSendSynchronousPowerMessage, 4, ERROR_CALL_NOT_IMPLEMENTED)

/* ------------------------------------------------------------------ rpc helpers of the service processes */

MIME_STUB_RETURN("sechost", DWORD, LocalRpcBindingCreateWithSecurity, 5, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", DWORD, LocalRpcBindingSetAuthInfoEx, 2, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", DWORD, SetLocalRpcServerInterfaceSecurity, 3, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", DWORD, SetLocalRpcServerProtseqSecurity, 0, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", DWORD, RpcClientCapabilityCheck, 1, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", NTSTATUS, CapabilityCheck, 3, LSA_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", NTSTATUS, CapabilityCheckForSingleSessionSku, 2, LSA_NOT_IMPLEMENTED)

/* ------------------------------------------------------------------ lsa, the parts without a policy database */

MIME_STUB_RETURN("sechost", NTSTATUS, LsaICLookupNames, 10, LSA_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", NTSTATUS, LsaICLookupNamesWithCreds, 10, LSA_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", NTSTATUS, LsaICLookupSids, 9, LSA_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", NTSTATUS, LsaICLookupSidsWithCreds, 10, LSA_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", NTSTATUS, LsaIOpenPolicyWithCreds, 6, LSA_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", NTSTATUS, LsaLookupManageSidNameMapping, 3, LSA_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", NTSTATUS, LsaLookupUserAccountType, 2, LSA_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", NTSTATUS, BuildSecurityDescriptorForSharingAccess, 4, LSA_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", NTSTATUS, BuildSecurityDescriptorForSharingAccessEx, 5, LSA_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", NTSTATUS, QueryTransientObjectSecurityDescriptor, 3, LSA_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", BOOLEAN, FreeTransientObjectSecurityDescriptor, 1, FALSE)

/* ------------------------------------------------------------------ the audit policy: it is the administrator's, and nobody here is */

#define AUDIT_STUB(name, args) MIME_STUB_ERROR("sechost", BOOLEAN, name, args, ERROR_PRIVILEGE_NOT_HELD, FALSE)
AUDIT_STUB(AuditComputeEffectivePolicyBySid, 4)
AUDIT_STUB(AuditEnumerateCategories, 2)
AUDIT_STUB(AuditEnumeratePerUserPolicy, 1)
AUDIT_STUB(AuditEnumerateSubCategories, 4)
AUDIT_STUB(AuditLookupCategoryNameW, 2)
AUDIT_STUB(AuditLookupSubCategoryNameW, 2)
AUDIT_STUB(AuditQueryGlobalSaclW, 2)
AUDIT_STUB(AuditQueryPerUserPolicy, 4)
AUDIT_STUB(AuditQuerySecurity, 2)
AUDIT_STUB(AuditQuerySystemPolicy, 3)
AUDIT_STUB(AuditSetGlobalSaclW, 2)
AUDIT_STUB(AuditSetPerUserPolicy, 3)
AUDIT_STUB(AuditSetSecurity, 2)
AUDIT_STUB(AuditSetSystemPolicy, 2)

/* what audit and the credential store give out is freed alike. */
ADVAPI void WINAPI AuditFree(PVOID buffer)
{
    AdvFree(buffer);
}

ADVAPI void WINAPI CredFree(PVOID buffer)
{
    AdvFree(buffer);
}

/* ------------------------------------------------------------------ the credential store: empty, and it keeps nothing */

#define CRED_READ_STUB(type, name, args) MIME_STUB_ERROR("sechost", type, name, args, ERROR_NOT_FOUND, FALSE)
#define CRED_STUB(type, name, args) MIME_STUB_ERROR("sechost", type, name, args, ERROR_NOT_SUPPORTED, FALSE)
CRED_READ_STUB(BOOL, CredReadA, 4)
CRED_READ_STUB(BOOL, CredReadW, 4)
CRED_READ_STUB(BOOL, CredDeleteA, 3)
CRED_READ_STUB(BOOL, CredDeleteW, 3)
CRED_READ_STUB(BOOL, CredEnumerateA, 4)
CRED_READ_STUB(BOOL, CredEnumerateW, 4)
CRED_READ_STUB(BOOL, CredFindBestCredentialA, 4)
CRED_READ_STUB(BOOL, CredFindBestCredentialW, 4)
CRED_READ_STUB(BOOL, CredGetTargetInfoA, 3)
CRED_READ_STUB(BOOL, CredGetTargetInfoW, 3)
CRED_READ_STUB(BOOL, CredReadDomainCredentialsA, 4)
CRED_READ_STUB(BOOL, CredReadDomainCredentialsW, 4)
CRED_STUB(BOOL, CredWriteA, 2)
CRED_STUB(BOOL, CredWriteW, 2)
CRED_STUB(BOOL, CredWriteDomainCredentialsA, 3)
CRED_STUB(BOOL, CredWriteDomainCredentialsW, 3)
CRED_STUB(BOOL, CredGetSessionTypes, 2)
CRED_STUB(BOOL, CredIsMarshaledCredentialW, 1)
CRED_STUB(BOOL, CredIsProtectedA, 2)
CRED_STUB(BOOL, CredIsProtectedW, 2)
CRED_STUB(BOOL, CredMarshalCredentialA, 3)
CRED_STUB(BOOL, CredMarshalCredentialW, 3)
CRED_STUB(BOOL, CredUnmarshalCredentialA, 3)
CRED_STUB(BOOL, CredUnmarshalCredentialW, 3)
CRED_STUB(BOOL, CredProtectA, 6)
CRED_STUB(BOOL, CredProtectW, 6)
CRED_STUB(BOOL, CredProtectEx, 6)
CRED_STUB(BOOL, CredUnprotectA, 5)
CRED_STUB(BOOL, CredUnprotectW, 5)
CRED_STUB(BOOL, CredUnprotectEx, 5)
MIME_STUB_RETURN("sechost", DWORD, CredBackupCredentials, 5, ERROR_NOT_SUPPORTED)
MIME_STUB_RETURN("sechost", DWORD, CredRestoreCredentials, 4, ERROR_NOT_SUPPORTED)
MIME_STUB_RETURN("sechost", DWORD, CredEncryptAndMarshalBinaryBlob, 3, ERROR_NOT_SUPPORTED)
MIME_STUB_RETURN("sechost", DWORD, CredReadByTokenHandle, 5, ERROR_NOT_FOUND)
MIME_STUB_RETURN("sechost", NTSTATUS, CredParseUserNameWithType, 4, LSA_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", DWORD, CredProfileLoaded, 0, ERROR_SUCCESS)
MIME_STUB_RETURN("sechost", DWORD, CredProfileLoadedEx, 0, ERROR_SUCCESS)
MIME_STUB_RETURN("sechost", DWORD, CredProfileUnloaded, 0, ERROR_SUCCESS)
MIME_STUB_RETURN("sechost", DWORD, CredpConvertCredential, 4, ERROR_NOT_SUPPORTED)
MIME_STUB_RETURN("sechost", DWORD, CredpConvertOneCredentialSize, 2, 0)
MIME_STUB_RETURN("sechost", DWORD, CredpConvertTargetInfo, 4, ERROR_NOT_SUPPORTED)
MIME_STUB_RETURN("sechost", DWORD, CredpDecodeCredential, 1, ERROR_NOT_SUPPORTED)
MIME_STUB_RETURN("sechost", DWORD, CredpEncodeCredential, 1, ERROR_NOT_SUPPORTED)
MIME_STUB_RETURN("sechost", DWORD, CredpEncodeSecret, 5, ERROR_NOT_SUPPORTED)

/* ------------------------------------------------------------------ event tracing sessions: none can be made, none exist */

/* a session handle is 64 bits, which two arguments of a 32-bit process make. */
#define TRACE_STUB(name, args) MIME_STUB_RETURN("sechost", ULONG, name, args, ERROR_NOT_SUPPORTED)
TRACE_STUB(StartTraceA, 3)
TRACE_STUB(StartTraceW, 3)
TRACE_STUB(ControlTraceA, 5)
TRACE_STUB(ControlTraceW, 5)
TRACE_STUB(StopTraceW, 4)
TRACE_STUB(StopTraceA, 4)
TRACE_STUB(QueryTraceA, 4)
TRACE_STUB(QueryTraceW, 4)
TRACE_STUB(UpdateTraceA, 4)
TRACE_STUB(UpdateTraceW, 4)
TRACE_STUB(FlushTraceA, 4)
TRACE_STUB(FlushTraceW, 4)
TRACE_STUB(EnableTrace, 6)
TRACE_STUB(EnableTraceEx2, 11)
TRACE_STUB(EnumerateTraceGuidsEx, 6)
TRACE_STUB(TraceSetInformation, 5)
TRACE_STUB(TraceQueryInformation, 5)
TRACE_STUB(TraceConfigureLastBranchRecord, 4)
TRACE_STUB(EventAccessControl, 5)
TRACE_STUB(EventAccessQuery, 3)
TRACE_STUB(EventAccessRemove, 1)
TRACE_STUB(CloseTrace, 2)
TRACE_STUB(ProcessTrace, 4)
TRACE_STUB(QueryTraceProcessingHandle, 8)
TRACE_STUB(SetTraceCallback, 2)
TRACE_STUB(RemoveTraceCallback, 1)
TRACE_STUB(ProcessTraceAddBufferToBufferStream, 5)
TRACE_STUB(ProcessTraceBufferDecrementReference, 1)
TRACE_STUB(ProcessTraceBufferIncrementReference, 2)
MIME_STUB_RETURN("sechost", ULONGLONG, OpenTraceW, 1, ~0ull)
MIME_STUB_RETURN("sechost", ULONGLONG, OpenTraceA, 1, ~0ull)
MIME_STUB_RETURN("sechost", ULONGLONG, OpenTraceFromBufferStream, 5, ~0ull)
MIME_STUB_RETURN("sechost", ULONGLONG, OpenTraceFromFile, 3, ~0ull)
MIME_STUB_RETURN("sechost", ULONGLONG, OpenTraceFromRealTimeLogger, 3, ~0ull)
MIME_STUB_RETURN("sechost", ULONGLONG, OpenTraceFromRealTimeLoggerWithAllocationOptions, 7, ~0ull)

/* no provider registers itself with a session, so the list of the guids is empty. */
ADVAPI ULONG WINAPI EnumerateTraceGuids(PVOID *properties, ULONG count, PULONG guids)
{
    (void)properties;
    (void)count;
    if (!guids)
        return ERROR_INVALID_PARAMETER;
    *guids = 0;
    return ERROR_SUCCESS;
}

/* no session runs: the list of them is empty. */
ADVAPI ULONG WINAPI QueryAllTracesW(PVOID *properties, ULONG count, PULONG loggers)
{
    (void)properties;
    (void)count;
    if (!loggers)
        return ERROR_INVALID_PARAMETER;
    *loggers = 0;
    return ERROR_SUCCESS;
}

ADVAPI ULONG WINAPI QueryAllTracesA(PVOID *properties, ULONG count, PULONG loggers)
{
    return QueryAllTracesW(properties, count, loggers);
}

/* ------------------------------------------------------------------ conditions of security descriptors, identity providers, isolation */

MIME_STUB_RETURN("sechost", DWORD, LocalGetConditionForString, 10, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", DWORD, LocalGetReferencedTokenTypesForCondition, 3, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", DWORD, LocalGetSidForString, 10, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", DWORD, LocalGetStringForCondition, 7, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", DWORD, LocalGetStringForRelativeAttribute, 7, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", DWORD, GetOperandValue, 10, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", DWORD, GetPrintableOperandValue, 8, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", PVOID, FreeOperandValue, 1, nullptr)
MIME_STUB_RETURN("sechost", DWORD, DecodeAttributeName, 3, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", DWORD, EncodeAttributeName, 3, ERROR_CALL_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", UCHAR, GetCharFromDigit, 2, 0)
MIME_STUB_RETURN("sechost", UCHAR, GetDigitFromChar, 2, 0)
MIME_STUB_RETURN("sechost", UCHAR, GetOperatorCodeAtIndex, 1, 0)
MIME_STUB_RETURN("sechost", DWORD, GetOperatorIndexByToken, 1, 0)
MIME_STUB_RETURN("sechost", UCHAR, GetOperatorUnaryAtIndex, 1, 0)
MIME_STUB_RETURN("sechost", UCHAR, IsArrayType, 1, 0)
MIME_STUB_RETURN("sechost", UCHAR, IsValueSizeFixed, 1, 0)
MIME_STUB_RETURN("sechost", NTSTATUS, EnumerateIdentityProviders, 3, LSA_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", NTSTATUS, GetDefaultIdentityProvider, 1, LSA_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", NTSTATUS, GetIdentityProviderInfoByGUID, 3, LSA_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", NTSTATUS, GetIdentityProviderInfoByName, 3, LSA_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", NTSTATUS, ReleaseIdentityProviderEnumContext, 1, LSA_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", NTSTATUS, CreateIsolatedProcess, 8, LSA_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", NTSTATUS, CreateIsolationContainer, 4, LSA_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", NTSTATUS, DeleteIsolationContainer, 3, LSA_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", NTSTATUS, FreeContainer, 1, LSA_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", NTSTATUS, GetEmbeddedContainerIsolationPolicy, 2, LSA_NOT_IMPLEMENTED)
MIME_STUB_RETURN("sechost", NTSTATUS, GetEmbeddedImageMitigationPolicy, 4, LSA_NOT_IMPLEMENTED)
