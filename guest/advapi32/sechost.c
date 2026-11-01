/*
 * functions windows exports from sechost.dll that advapi32 lacked: the service and security functions windows keeps
 * in sechost and calls from advapi32. mime's sechost.dll forwards to these. the credential store, the audit policy and
 * the event tracing sessions are in sechost_credman.c, sechost_audit.c and sechost_trace.c.
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


/* audit and credential store buffers are freed the same way. */
ADVAPI void WINAPI AuditFree(PVOID buffer)
{
    LocalFree(buffer);
}

ADVAPI void WINAPI CredFree(PVOID buffer)
{
    LocalFree(buffer);
}

/* ------------------------------------------------------------------ descriptor conditions, identity providers, isolation */

