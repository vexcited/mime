/*
 * lsa client calls, per thread call data and target info marshaling.
 *
 * these talk to lsa over local rpc. there is no lsa, so they fail the way windows does when lsa is not running, and no
 * lsa handle is ever handed out.
 */
#include "secur32_private.h"

static constexpr WCHAR g_lsa_ready[] = L"\\SECURITY\\LSA_AUTHENTICATION_INITIALIZED";

/*
 * windows opens the event lsa sets once it accepts clients and returns the failure of that open. mime never creates the
 * event; if something else did, there would still be no lsa behind it.
 */
static NTSTATUS connect_lsa(void)
{
    UNICODE_STRING name = {sizeof(g_lsa_ready) - sizeof(WCHAR), sizeof(g_lsa_ready), (PWSTR)g_lsa_ready};
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
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
    /* windows stores the reply's package id even when the call fails. */
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

/*
 * what a thread tells lsa about its next call, kept until SeciFreeCallContext. sspicli 26300 keeps 48 bytes: the
 * socket address of the peer in the first 32, then the flags, then the name of the target.
 */
typedef struct SEC_CALL_DATA {
    UCHAR Address[32];
    ULONG Flags;
    PWSTR Target;
} SEC_CALL_DATA;

SECURITYAPI void SEC_ENTRY SeciFreeCallContext(void);

static SEC_CALL_DATA *call_data(PBOOL allocated)
{
    *allocated = FALSE;
    if (SecTlsCallData == TLS_OUT_OF_INDEXES)
        return nullptr;
    SEC_CALL_DATA *data = TlsGetValue(SecTlsCallData);
    if (data)
        return data;
    data = LocalAlloc(LPTR, sizeof(*data));
    if (data && TlsSetValue(SecTlsCallData, data)) {
        *allocated = TRUE;
        return data;
    }
    LocalFree(data);
    return nullptr;
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

/*
 * as sspicli 26300: an address or a name is needed, an address longer than its 32 bytes is answered as no memory,
 * the name replaces the one kept and the address is left alone when none comes. wininet calls it with the peer
 * and the host before each step of a handshake.
 */
SECURITYAPI SECURITY_STATUS SEC_ENTRY SeciAllocateAndSetCallTarget(PVOID address, ULONG length, PCWSTR target,
                                                                   PBOOL allocated)
{
    SIZE_T characters = target ? wcslen(target) : 0;
    if ((!address && !characters) || !allocated)
        return SEC_E_INVALID_PARAMETER;
    *allocated = FALSE;
    if (address && length > sizeof(((SEC_CALL_DATA *)nullptr)->Address))
        return SEC_E_INSUFFICIENT_MEMORY;
    SEC_CALL_DATA *data = call_data(allocated);
    if (!data)
        return SEC_E_INSUFFICIENT_MEMORY;
    if (characters) {
        LocalFree(data->Target);
        data->Target = LocalAlloc(LPTR, (characters + 1) * sizeof(WCHAR));
        if (!data->Target) {
            if (*allocated)
                SeciFreeCallContext();
            *allocated = FALSE;
            return SEC_E_INSUFFICIENT_MEMORY;
        }
        memcpy(data->Target, target, characters * sizeof(WCHAR));
    }
    if (address)
        memcpy(data->Address, address, length);
    return SEC_E_OK;
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY SeciAllocateAndSetIPAddress(PVOID address, SIZE_T length, PBOOL allocated)
{
    return SeciAllocateAndSetCallTarget(address, (ULONG)length, nullptr, allocated);
}

SECURITYAPI void SEC_ENTRY SeciFreeCallContext(void)
{
    if (SecTlsCallData == TLS_OUT_OF_INDEXES)
        return;
    SEC_CALL_DATA *data = TlsGetValue(SecTlsCallData);
    if (!data)
        return;
    LocalFree(data->Target);
    LocalFree(data);
    TlsSetValue(SecTlsCallData, nullptr);
}

/* the packed form kerberos and ntlm keep with credential manager targets; not implemented. */
SECURITYAPI NTSTATUS NTAPI CredMarshalTargetInfo(PVOID target, PUSHORT *buffer, PULONG size)
{
    if (buffer)
        *buffer = nullptr;
    return STATUS_NOT_SUPPORTED;
}

SECURITYAPI NTSTATUS NTAPI CredUnmarshalTargetInfo(PUSHORT buffer, ULONG size, PVOID *target, PULONG target_size)
{
    /* the trailer must exceed its 12 bytes, hold whole characters and not wrap. */
    if (!buffer || size <= 12 || (size & 1) || (ULONG_PTR)buffer + size < (ULONG_PTR)buffer)
        return STATUS_INVALID_PARAMETER;
    return STATUS_NOT_SUPPORTED;
}
