/*
 * sids as text and the current user's registry key. the sid, acl and descriptor routines are in rtl/security.c.
 */
#include "ntdll_private.h"
#include <mime/ntrtlsec.h>

/* the authority is decimal below 2^32, else hex like windows. */
NTDLL_EXPORT NTSTATUS NTAPI RtlConvertSidToUnicodeString(PUNICODE_STRING out, PVOID sid, BOOLEAN allocate)
{
    const SID *s = sid;
    if (!RtlValidSid(sid) || s->Revision != SID_REVISION)
        return STATUS_INVALID_SID;
    WCHAR text[256];
    ULONG64 authority = 0;
    for (int i = 0; i < 6; i++)
        authority = authority << 8 | s->IdentifierAuthority.Value[i];
    int n = authority >> 32 ? _snwprintf(text, 255, L"S-1-0x%llX", authority)
                            : _snwprintf(text, 255, L"S-1-%llu", authority);
    for (UCHAR i = 0; i < s->SubAuthorityCount; i++)
        n += _snwprintf(text + n, 255 - n, L"-%u", s->SubAuthority[i]);
    USHORT bytes = (USHORT)(n * sizeof(WCHAR));
    if (allocate) {
        out->Buffer = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, bytes + sizeof(WCHAR));
        if (!out->Buffer)
            return STATUS_NO_MEMORY;
        out->MaximumLength = bytes + sizeof(WCHAR);
    } else if (out->MaximumLength < bytes + sizeof(WCHAR)) {
        return STATUS_BUFFER_OVERFLOW;
    }
    memcpy(out->Buffer, text, bytes);
    out->Buffer[n] = 0;
    out->Length = bytes;
    return STATUS_SUCCESS;
}

/* bytes without the terminator, assuming the longest text for each part. */
NTDLL_EXPORT NTSTATUS NTAPI RtlLengthSidAsUnicodeString(PVOID sid, PULONG length)
{
    const SID *s = sid;
    if (!RtlValidSid(sid))
        return STATUS_INVALID_SID;
    *length = (s->IdentifierAuthority.Value[0] || s->IdentifierAuthority.Value[1] ? 36 : 28) + 22 * s->SubAuthorityCount;
    return STATUS_SUCCESS;
}

/* \REGISTRY\USER\<process token sid>, what HKEY_CURRENT_USER opens. */
NTDLL_EXPORT NTSTATUS NTAPI RtlFormatCurrentUserKeyPath(PUNICODE_STRING path)
{
    HANDLE token;
    NTSTATUS status = NtOpenProcessToken(NtCurrentProcess(), 0x0008 /* TOKEN_QUERY */, &token);
    if (!NT_SUCCESS(status))
        return status;
    ULONG64 buffer[32];
    ULONG length;
    status = NtQueryInformationToken(token, 1 /* TokenUser */, buffer, sizeof(buffer), &length);
    NtClose(token);
    if (!NT_SUCCESS(status))
        return status;
    UNICODE_STRING sid;
    status = RtlConvertSidToUnicodeString(&sid, *(PVOID *)buffer, TRUE);
    if (!NT_SUCCESS(status))
        return status;
    static const WCHAR prefix[] = L"\\REGISTRY\\USER\\";
    USHORT bytes = (USHORT)(sizeof(prefix) - sizeof(WCHAR) + sid.Length);
    path->Buffer = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, bytes + sizeof(WCHAR));
    if (path->Buffer) {
        memcpy(path->Buffer, prefix, sizeof(prefix) - sizeof(WCHAR));
        memcpy((PUCHAR)path->Buffer + sizeof(prefix) - sizeof(WCHAR), sid.Buffer, sid.Length + sizeof(WCHAR));
        path->Length = bytes;
        path->MaximumLength = bytes + sizeof(WCHAR);
    }
    RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, sid.Buffer);
    return path->Buffer ? STATUS_SUCCESS : STATUS_NO_MEMORY;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlOpenCurrentUser(ACCESS_MASK access, PHANDLE key)
{
    UNICODE_STRING path;
    NTSTATUS status = RtlFormatCurrentUserKeyPath(&path);
    OBJECT_ATTRIBUTES oa;
    if (NT_SUCCESS(status)) {
        InitializeObjectAttributes(&oa, &path, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
        status = NtOpenKey(key, access, &oa);
        RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, path.Buffer);
    }
    if (!NT_SUCCESS(status)) {
        /* without a loaded user hive windows falls back to the default user. */
        UNICODE_STRING fallback = RTL_CONSTANT_STRING(L"\\REGISTRY\\USER\\.DEFAULT");
        InitializeObjectAttributes(&oa, &fallback, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
        status = NtOpenKey(key, access, &oa);
    }
    return status;
}
