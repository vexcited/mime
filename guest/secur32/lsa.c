/*
 * the lsa client calls, the per thread call data and the target info
 * marshaling of sspicli.
 *
 * these talk to lsa over a local rpc connection. mime runs no lsa, so a
 * connection can never be made: the functions fail the way sspicli fails
 * when lsa is not running, and no lsa handle is ever handed out.
 */
#include "secur32_private.h"

static const WCHAR g_lsa_ready[] = L"\\SECURITY\\LSA_AUTHENTICATION_INITIALIZED";

/*
 * sspicli opens the event lsa sets once it accepts clients and returns the
 * failure of that open. mime never creates the event; if something else
 * did, there would still be no lsa behind it.
 */
static NTSTATUS connect_lsa(void)
{
    UNICODE_STRING name = {sizeof(g_lsa_ready) - sizeof(WCHAR), sizeof(g_lsa_ready), (PWSTR)g_lsa_ready};
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    HANDLE event;
    NTSTATUS status = NtOpenEvent(&event, SYNCHRONIZE, &oa);
    if (!NT_SUCCESS(status))
        return status;
    NtClose(event);
    return STATUS_NOT_SUPPORTED;
}

SECURITYAPI NTSTATUS NTAPI LsaConnectUntrusted(PHANDLE handle)
{
    if (!handle)
        return STATUS_INVALID_PARAMETER;
    return connect_lsa();
}

SECURITYAPI NTSTATUS NTAPI LsaRegisterLogonProcess(PLSA_STRING name, PHANDLE handle, PLSA_OPERATIONAL_MODE mode)
{
    if (!name || !handle)
        return STATUS_INVALID_PARAMETER;
    if (name->Length > 127)
        return STATUS_NAME_TOO_LONG;
    return connect_lsa();
}

SECURITYAPI NTSTATUS NTAPI LsaDeregisterLogonProcess(HANDLE handle)
{
    return STATUS_INVALID_HANDLE;
}

SECURITYAPI NTSTATUS NTAPI LsaLookupAuthenticationPackage(HANDLE handle, PLSA_STRING name, PULONG package)
{
    if (!name)
        return STATUS_INVALID_PARAMETER;
    if (name->Length > 127)
        return STATUS_NAME_TOO_LONG;
    /* sspicli stores the package id of the reply even when the call fails. */
    if (package)
        *package = 0;
    return STATUS_INVALID_HANDLE;
}

SECURITYAPI NTSTATUS NTAPI LsaCallAuthenticationPackage(HANDLE handle, ULONG package, PVOID submit, ULONG submit_length,
                                                        PVOID *result, PULONG result_length, PNTSTATUS protocol_status)
{
    return STATUS_INVALID_HANDLE;
}

SECURITYAPI NTSTATUS NTAPI LsaLogonUser(HANDLE handle, PLSA_STRING origin, SECURITY_LOGON_TYPE type, ULONG package,
                                        PVOID information, ULONG information_length, PVOID local_groups,
                                        PVOID source, PVOID *profile, PULONG profile_length, PSSPI_LUID logon_id,
                                        PHANDLE token, PVOID quotas, PNTSTATUS sub_status)
{
    return STATUS_INVALID_HANDLE;
}

/* lsa returns its buffers as virtual memory mapped into the client. */
SECURITYAPI NTSTATUS NTAPI LsaFreeReturnBuffer(PVOID buffer)
{
    SIZE_T size = 0;
    return NtFreeVirtualMemory(NtCurrentProcess(), &buffer, &size, MEM_RELEASE);
}

SECURITYAPI NTSTATUS NTAPI LsaEnumerateLogonSessions(PULONG count, PSSPI_LUID *sessions)
{
    return STATUS_NOT_IMPLEMENTED;
}

SECURITYAPI NTSTATUS NTAPI LsaGetLogonSessionData(PSSPI_LUID logon_id, PVOID *data)
{
    return STATUS_NOT_IMPLEMENTED;
}

SECURITYAPI NTSTATUS NTAPI LsaRegisterPolicyChangeNotification(POLICY_NOTIFICATION_INFORMATION_CLASS klass,
                                                               HANDLE event)
{
    return STATUS_NOT_IMPLEMENTED;
}

SECURITYAPI NTSTATUS NTAPI LsaUnregisterPolicyChangeNotification(POLICY_NOTIFICATION_INFORMATION_CLASS klass,
                                                                 HANDLE event)
{
    return STATUS_NOT_IMPLEMENTED;
}

SECURITYAPI NTSTATUS NTAPI GetSecurityUserInfo(PSSPI_LUID logon_id, ULONG flags, PVOID *information)
{
    return STATUS_NOT_IMPLEMENTED;
}

/* ------------------------------------------------------------------ call data */

/* what a thread tells lsa about the call it makes next, kept until SeciFreeCallContext. */
typedef struct SEC_CALL_DATA {
    ULONG Flags;
    ULONG AddressLength;
    PVOID Address;
} SEC_CALL_DATA;

static SEC_CALL_DATA *call_data(PBOOL allocated)
{
    *allocated = FALSE;
    if (SecTlsCallData == TLS_OUT_OF_INDEXES)
        return NULL;
    SEC_CALL_DATA *data = TlsGetValue(SecTlsCallData);
    if (data)
        return data;
    data = LocalAlloc(LPTR, sizeof(*data));
    if (data && TlsSetValue(SecTlsCallData, data)) {
        *allocated = TRUE;
        return data;
    }
    LocalFree(data);
    return NULL;
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY SeciAllocateAndSetCallFlags(ULONG flags, PBOOL allocated)
{
    if (!allocated)
        return SEC_E_INVALID_PARAMETER;
    SEC_CALL_DATA *data = call_data(allocated);
    if (!data)
        return SEC_E_INSUFFICIENT_MEMORY;
    data->Flags = flags;
    return SEC_E_OK;
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY SeciAllocateAndSetIPAddress(PVOID address, SIZE_T length, PBOOL allocated)
{
    if (!allocated || (!address && length) || length > 0xFFFF)
        return SEC_E_INVALID_PARAMETER;
    SEC_CALL_DATA *data = call_data(allocated);
    if (!data)
        return SEC_E_INSUFFICIENT_MEMORY;
    PVOID copy = NULL;
    if (length) {
        copy = LocalAlloc(LMEM_FIXED, length);
        if (!copy)
            return SEC_E_INSUFFICIENT_MEMORY;
        memcpy(copy, address, length);
    }
    LocalFree(data->Address);
    data->Address = copy;
    data->AddressLength = (ULONG)length;
    return SEC_E_OK;
}

SECURITYAPI void SEC_ENTRY SeciFreeCallContext(void)
{
    if (SecTlsCallData == TLS_OUT_OF_INDEXES)
        return;
    SEC_CALL_DATA *data = TlsGetValue(SecTlsCallData);
    if (!data)
        return;
    LocalFree(data->Address);
    LocalFree(data);
    TlsSetValue(SecTlsCallData, NULL);
}

/* ------------------------------------------------------------------ target information */

/* the packed form kerberos and ntlm keep with credential manager targets; not implemented. */
SECURITYAPI NTSTATUS NTAPI CredMarshalTargetInfo(PVOID target, PUSHORT *buffer, PULONG size)
{
    if (buffer)
        *buffer = NULL;
    return STATUS_NOT_SUPPORTED;
}

SECURITYAPI NTSTATUS NTAPI CredUnmarshalTargetInfo(PUSHORT buffer, ULONG size, PVOID *target, PULONG target_size)
{
    /* sspicli's checks on the trailer: more than its 12 bytes, whole characters and no wrap. */
    if (!buffer || size <= 12 || (size & 1) || (ULONG_PTR)buffer + size < (ULONG_PTR)buffer)
        return STATUS_INVALID_PARAMETER;
    return STATUS_NOT_SUPPORTED;
}
