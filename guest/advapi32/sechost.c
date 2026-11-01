/*
 * functions windows exports from sechost.dll that advapi32 lacked: the service and security functions windows keeps
 * in sechost and calls from advapi32, the credential store, audit policy, event tracing sessions and the private
 * service control manager entry points. mime's sechost.dll forwards to these. the service and account functions are
 * real; the rest answer like an empty machine would, or fail like a missing function and log their name once.
 */
#define MIME_BUILD_ADVAPI32 1
#include "advapi32_private.h"
#include <mime/stubexport.h>
#include <mime/win32/winsvc.h>

#define LSA_NOT_IMPLEMENTED ((NTSTATUS)0xC0000002L)

/* ------------------------------------------------------------------ services */

/* windows' sechost exports the wide function under the unsuffixed name. */
ADVAPI DWORD WINAPI NotifyServiceStatusChange(SC_HANDLE service, DWORD mask, PSERVICE_NOTIFYW notify)
{
    return NotifyServiceStatusChangeW(service, mask, notify);
}

/* backs SetServiceBits: the manager keeps the service's bits, and name is the service name. */
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

/* svchost.exe passes the callback for the manager's low resource notices, which the manager never sends. */
ADVAPI DWORD WINAPI I_RegisterSvchostNotificationCallback(PVOID callback)
{
    (void)callback;
    return ERROR_SUCCESS;
}


/* ------------------------------------------------------------------ audit policy: administrators only, and nobody here is one */

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

/* audit and credential store buffers are freed the same way. */
ADVAPI void WINAPI AuditFree(PVOID buffer)
{
    AdvFree(buffer);
}

ADVAPI void WINAPI CredFree(PVOID buffer)
{
    AdvFree(buffer);
}

/* ------------------------------------------------------------------ credential store: empty, stores nothing */

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
CRED_STUB(BOOL, CredIsProtectedA, 2)
CRED_STUB(BOOL, CredIsProtectedW, 2)
CRED_STUB(BOOL, CredProtectA, 6)
CRED_STUB(BOOL, CredProtectW, 6)
CRED_STUB(BOOL, CredProtectEx, 6)
CRED_STUB(BOOL, CredUnprotectA, 5)
CRED_STUB(BOOL, CredUnprotectW, 5)
CRED_STUB(BOOL, CredUnprotectEx, 5)

/* ------------------------------------------------------------------ event tracing sessions: none exist or can be created */

/* a 64-bit session handle takes two arguments in a 32-bit process. */
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
TRACE_STUB(EnableTraceEx, 12)
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

/* no provider registers with a session, so the guid list is empty. */
ADVAPI ULONG WINAPI EnumerateTraceGuids(PVOID *properties, ULONG count, PULONG guids)
{
    (void)properties;
    (void)count;
    if (!guids)
        return ERROR_INVALID_PARAMETER;
    *guids = 0;
    return ERROR_SUCCESS;
}

/* no session runs, so the list is empty. */
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

/* ------------------------------------------------------------------ descriptor conditions, identity providers, isolation */

