/*
 * the identity of a thread: acting as the process again, with a copy of its token.
 */
#include "ntdll_private.h"

/* the win32 errors of the statuses tokens report, sorted for a binary search; 0 when there is none. */
ULONG RtlpTokenStatusToDosError(NTSTATUS status)
{
    static const struct {
        ULONG Status;
        USHORT Error;
    } map[] = {
        {0xC000005A, 1307}, /* STATUS_INVALID_OWNER */
        {0xC000005B, 1308}, /* STATUS_INVALID_PRIMARY_GROUP */
        {0xC000005D, 1310}, /* STATUS_CANT_DISABLE_MANDATORY */
        {0xC000005F, 1312}, /* STATUS_NO_SUCH_LOGON_SESSION */
        {0xC000006D, 1326}, /* STATUS_LOGON_FAILURE */
        {0xC0000078, 1337}, /* STATUS_INVALID_SID */
        {0xC0000095, 534},  /* STATUS_INTEGER_OVERFLOW -> ERROR_ARITHMETIC_OVERFLOW */
        {0xC0000099, 1344}, /* STATUS_ALLOTTED_SPACE_EXCEEDED */
        {0xC00000A5, 1346}, /* STATUS_BAD_IMPERSONATION_LEVEL */
        {0xC00000A6, 1347}, /* STATUS_CANT_OPEN_ANONYMOUS */
        {0xC00000A8, 1349}, /* STATUS_BAD_TOKEN_TYPE */
        {0xC00000AF, 1},    /* STATUS_ILLEGAL_FUNCTION -> ERROR_INVALID_FUNCTION */
        {0xC000010D, 558},  /* STATUS_CANNOT_IMPERSONATE */
        {0xC000012B, 1375}, /* STATUS_TOKEN_ALREADY_IN_USE */
        {0xC0000446, 1789}, /* STATUS_INVALID_LABEL */
    };
    ULONG lo = 0, hi = RTL_NUMBER_OF(map);
    while (lo < hi) {
        ULONG mid = (lo + hi) / 2;
        if (map[mid].Status == (ULONG)status)
            return map[mid].Error;
        if (map[mid].Status < (ULONG)status)
            lo = mid + 1;
        else
            hi = mid;
    }
    return 0;
}

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
