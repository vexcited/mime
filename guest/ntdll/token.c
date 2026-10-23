/*
 * thread impersonation of its own process.
 */
#include "ntdll_private.h"

NTSYSCALLAPI NTSTATUS NTAPI NtDuplicateToken(HANDLE token, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes,
                                             BOOLEAN effective_only, ULONG type, PHANDLE result);

/* thread_token, when given, receives the impersonation token. */
NTDLL_EXPORT NTSTATUS NTAPI RtlImpersonateSelfEx(ULONG level, ACCESS_MASK additional, PHANDLE thread_token)
{
    HANDLE process_token, copy;
    NTSTATUS status = NtOpenProcessToken(NtCurrentProcess(), 0x0002 /* TOKEN_DUPLICATE */, &process_token);
    if (!NT_SUCCESS(status))
        return status;
    /* dynamic tracking, like every call that copies a token for a thread. */
    struct {
        ULONG Length, Level;
        UCHAR Tracking, Effective;
    } qos = {sizeof(qos), level, TRUE, FALSE};
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, nullptr, 0, nullptr, nullptr);
    oa.SecurityQualityOfService = &qos;
    status = NtDuplicateToken(process_token, 0x0004 /* TOKEN_IMPERSONATE */ | additional, &oa, FALSE,
                              2 /* TokenImpersonation */, &copy);
    NtClose(process_token);
    if (!NT_SUCCESS(status))
        return status;
    status = NtSetInformationThread(NtCurrentThread(), ThreadImpersonationToken, &copy, sizeof(copy));
    if (NT_SUCCESS(status) && thread_token)
        *thread_token = copy;
    else
        NtClose(copy);
    return status;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlImpersonateSelf(ULONG level)
{
    return RtlImpersonateSelfEx(level, 0, nullptr);
}
