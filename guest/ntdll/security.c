/*
 * security identifiers and the current user's registry key.
 */
#include "ntdll_private.h"

typedef struct NTDLL_SID {
    UCHAR Revision;
    UCHAR SubAuthorityCount;
    UCHAR IdentifierAuthority[6];
    ULONG SubAuthority[];
} NTDLL_SID;

NTDLL_EXPORT BOOLEAN NTAPI RtlValidSid(PVOID sid)
{
    const NTDLL_SID *s = sid;
    return s && s->Revision == 1 && s->SubAuthorityCount <= 15;
}

NTDLL_EXPORT ULONG NTAPI RtlLengthSid(PVOID sid)
{
    return 8 + 4 * ((const NTDLL_SID *)sid)->SubAuthorityCount;
}

NTDLL_EXPORT BOOLEAN NTAPI RtlEqualSid(PVOID a, PVOID b)
{
    return RtlLengthSid(a) == RtlLengthSid(b) && memcmp(a, b, RtlLengthSid(a)) == 0;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlCopySid(ULONG length, PVOID dst, PVOID src)
{
    ULONG n = RtlLengthSid(src);
    if (length < n)
        return STATUS_BUFFER_TOO_SMALL;
    memcpy(dst, src, n);
    return STATUS_SUCCESS;
}

NTDLL_EXPORT PULONG NTAPI RtlSubAuthoritySid(PVOID sid, ULONG index)
{
    return &((NTDLL_SID *)sid)->SubAuthority[index];
}

NTDLL_EXPORT PUCHAR NTAPI RtlSubAuthorityCountSid(PVOID sid)
{
    return &((NTDLL_SID *)sid)->SubAuthorityCount;
}

/* S-1-5-21-..., the authority in decimal below 2^32, else in hex like windows. */
NTDLL_EXPORT NTSTATUS NTAPI RtlConvertSidToUnicodeString(PUNICODE_STRING out, PVOID sid, BOOLEAN allocate)
{
    const NTDLL_SID *s = sid;
    if (!RtlValidSid(sid))
        return (NTSTATUS)0xC0000078L; /* STATUS_INVALID_SID */
    WCHAR text[256];
    ULONG64 authority = 0;
    for (int i = 0; i < 6; i++)
        authority = authority << 8 | s->IdentifierAuthority[i];
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

/* \REGISTRY\USER\<sid of the process token>, what HKEY_CURRENT_USER opens. */
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
