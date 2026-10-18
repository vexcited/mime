/*
 * the identity of a thread: acting as the process again, with a copy of its token.
 */
#include "ntdll_private.h"

NTSYSCALLAPI NTSTATUS NTAPI NtDuplicateToken(HANDLE token, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes,
                                             BOOLEAN effective_only, ULONG type, PHANDLE result);

/* the thread impersonates its own process, at the level given; the token can be kept. */
NTDLL_EXPORT NTSTATUS NTAPI RtlImpersonateSelfEx(ULONG level, ACCESS_MASK additional, PHANDLE thread_token)
{
    HANDLE process_token, copy;
    NTSTATUS status = NtOpenProcessToken(NtCurrentProcess(), 0x0002 /* TOKEN_DUPLICATE */, &process_token);
    if (!NT_SUCCESS(status))
        return status;
    /* dynamic tracking, as the security quality of service of every call that makes a copy for a thread. */
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
