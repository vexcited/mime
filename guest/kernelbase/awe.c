/*
 * calls that databases and backup programs import and that this system answers without hardware or privilege for them:
 * address windowing extensions need a privilege the token of a program does not hold, nothing here is a tape drive, and
 * the processors of the machine are one node. every one of them is imported by name, so each has to be there for the
 * program to load.
 */
#include "kernelbase_private.h"
#include <mime/ntrtlsec.h>

#define TOKEN_QUERY 0x0008

#define ERROR_INVALID_FUNCTION_ 1

typedef struct GROUP_AFFINITY_ {
    ULONG_PTR Mask;
    USHORT Group;
    USHORT Reserved[3];
} GROUP_AFFINITY_;

WINBASEAPI BOOL WINAPI GetNumaNodeProcessorMask(UCHAR node, PULONGLONG mask);

/* ------------------------------------------------------------------ address windowing */

WINBASEAPI BOOL WINAPI AllocateUserPhysicalPages(HANDLE process, PULONG_PTR pages, PULONG_PTR array)
{
    KbSetError(ERROR_PRIVILEGE_NOT_HELD);
    return FALSE;
}

WINBASEAPI BOOL WINAPI AllocateUserPhysicalPagesNuma(HANDLE process, PULONG_PTR pages, PULONG_PTR array, DWORD node)
{
    KbSetError(ERROR_PRIVILEGE_NOT_HELD);
    return FALSE;
}

/* nothing was allocated, so no address is a region of pages and no page is one the program owns. */
WINBASEAPI BOOL WINAPI MapUserPhysicalPages(PVOID address, ULONG_PTR pages, PULONG_PTR array)
{
    KbSetError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

WINBASEAPI BOOL WINAPI MapUserPhysicalPagesScatter(PVOID *addresses, ULONG_PTR pages, PULONG_PTR array)
{
    KbSetError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

WINBASEAPI BOOL WINAPI FreeUserPhysicalPages(HANDLE process, PULONG_PTR pages, PULONG_PTR array)
{
    KbSetError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

WINBASEAPI BOOL WINAPI SetFileIoOverlappedRange(HANDLE file, PUCHAR buffer, ULONG length)
{
    KbSetError(ERROR_PRIVILEGE_NOT_HELD);
    return FALSE;
}

/* ------------------------------------------------------------------ tapes */

WINBASEAPI DWORD WINAPI GetTapeStatus(HANDLE device)
{
    return ERROR_INVALID_FUNCTION_;
}

WINBASEAPI DWORD WINAPI GetTapeParameters(HANDLE device, DWORD operation, LPDWORD size, LPVOID information)
{
    return ERROR_INVALID_FUNCTION_;
}

WINBASEAPI DWORD WINAPI SetTapeParameters(HANDLE device, DWORD operation, LPVOID information)
{
    return ERROR_INVALID_FUNCTION_;
}

WINBASEAPI DWORD WINAPI GetTapePosition(HANDLE device, DWORD type, LPDWORD partition, LPDWORD offset_low, LPDWORD offset_high)
{
    return ERROR_INVALID_FUNCTION_;
}

WINBASEAPI DWORD WINAPI SetTapePosition(HANDLE device, DWORD method, DWORD partition, DWORD offset_low, DWORD offset_high,
                                        BOOL immediate)
{
    return ERROR_INVALID_FUNCTION_;
}

WINBASEAPI DWORD WINAPI PrepareTape(HANDLE device, DWORD operation, BOOL immediate)
{
    return ERROR_INVALID_FUNCTION_;
}

WINBASEAPI DWORD WINAPI WriteTapemark(HANDLE device, DWORD type, DWORD count, BOOL immediate)
{
    return ERROR_INVALID_FUNCTION_;
}

WINBASEAPI DWORD WINAPI EraseTape(HANDLE device, DWORD type, BOOL immediate)
{
    return ERROR_INVALID_FUNCTION_;
}

WINBASEAPI DWORD WINAPI CreateTapePartition(HANDLE device, DWORD method, DWORD count, DWORD size)
{
    return ERROR_INVALID_FUNCTION_;
}

/* ------------------------------------------------------------------ processors and tokens */

#define PROCESS_AFFINITY_ENABLE_AUTO_UPDATE_ 1

/* the system does not move the affinity of a process by itself, so the mode is the one it was set to and stays off. */
WINBASEAPI BOOL WINAPI SetProcessAffinityUpdateMode(HANDLE process, DWORD flags)
{
    if (flags & ~(DWORD)PROCESS_AFFINITY_ENABLE_AUTO_UPDATE_) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

WINBASEAPI BOOL WINAPI QueryProcessAffinityUpdateMode(HANDLE process, LPDWORD flags)
{
    if (!flags) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *flags = 0;
    return TRUE;
}

WINBASEAPI BOOL WINAPI GetNumaNodeProcessorMaskEx(USHORT node, GROUP_AFFINITY_ *affinity)
{
    ULONGLONG mask;
    if (!affinity) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (node > 255 || !GetNumaNodeProcessorMask((UCHAR)node, &mask))
        return FALSE;
    memset(affinity, 0, sizeof(*affinity));
    affinity->Mask = (ULONG_PTR)mask;
    return TRUE;
}

/*
 * a token is a member of its user and of its groups that are enabled and not for denying only. the flags only ask whether
 * the groups of app containers count, which a token here does not have, so the check is the same with them.
 */
#define SE_GROUP_ENABLED_ 0x4
#define SE_GROUP_USE_FOR_DENY_ONLY_ 0x10

typedef struct SID_AND_ATTRIBUTES_ {
    PSID Sid;
    DWORD Attributes;
} SID_AND_ATTRIBUTES_;

static PVOID query_token(HANDLE token, int klass)
{
    ULONG length = 0;
    NtQueryInformationToken(token, klass, nullptr, 0, &length);
    PVOID buffer = length ? KbAlloc(length) : nullptr;
    if (buffer && !NT_SUCCESS(NtQueryInformationToken(token, klass, buffer, length, &length))) {
        KbFree(buffer);
        buffer = nullptr;
    }
    return buffer;
}

WINBASEAPI BOOL WINAPI CheckTokenMembershipEx(HANDLE token, PSID sid, DWORD flags, PBOOL member)
{
    if (flags & ~1u || !member || !sid || !RtlValidSid(sid)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *member = FALSE;
    HANDLE own = nullptr;
    if (!token) {
        if (!NT_SUCCESS(NtOpenThreadToken(NtCurrentThread(), TOKEN_QUERY, TRUE, &own)) &&
            !NT_SUCCESS(NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY, &own))) {
            KbSetError(ERROR_NO_TOKEN);
            return FALSE;
        }
        token = own;
    }
    SID_AND_ATTRIBUTES_ *user = query_token(token, 1 /* TokenUser */);
    struct {
        DWORD Count;
        SID_AND_ATTRIBUTES_ Groups[1];
    } *groups = query_token(token, 2 /* TokenGroups */);
    if (user && RtlEqualSid(user->Sid, sid))
        *member = TRUE;
    for (DWORD i = 0; groups && i < groups->Count && !*member; i++) {
        DWORD a = groups->Groups[i].Attributes;
        if ((a & SE_GROUP_ENABLED_) && !(a & SE_GROUP_USE_FOR_DENY_ONLY_) && RtlEqualSid(groups->Groups[i].Sid, sid))
            *member = TRUE;
    }
    BOOL ok = user && groups;
    KbFree(user);
    KbFree(groups);
    if (own)
        NtClose(own);
    if (!ok)
        KbSetError(ERROR_ACCESS_DENIED);
    return ok;
}
