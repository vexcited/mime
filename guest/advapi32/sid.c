/*
 * security identifiers: allocation, comparison and the S-1-... string form.
 *
 * these wrap the sid routines of ntdll, which validate and answer the way windows' do, and set the last error the
 * way kernelbase does: the functions that cannot fail clear it, the others convert the status.
 */
#include "advapi32_private.h"
#include <stdarg.h>

DWORD AdvSidLength(const SID *sid)
{
    return 8 + 4 * sid->SubAuthorityCount;
}

BOOL AdvSidValid(const SID *sid)
{
    return sid && (sid->Revision & 0xF) == SID_REVISION && sid->SubAuthorityCount <= SID_MAX_SUB_AUTHORITIES;
}

BOOL AdvSidEqual(const SID *a, const SID *b)
{
    return AdvSidValid(a) && AdvSidValid(b) && RtlEqualSid((PSID)a, (PSID)b);
}

PSID AdvMakeSid(void *storage, BYTE authority, int count, ...)
{
    SID *sid = storage;
    sid->Revision = SID_REVISION;
    sid->SubAuthorityCount = (BYTE)count;
    memset(sid->IdentifierAuthority.Value, 0, 6);
    sid->IdentifierAuthority.Value[5] = authority;
    va_list ap;
    va_start(ap, count);
    for (int i = 0; i < count; i++)
        sid->SubAuthority[i] = va_arg(ap, DWORD);
    va_end(ap);
    return sid;
}

ADVAPI BOOL WINAPI AllocateAndInitializeSid(PSID_IDENTIFIER_AUTHORITY authority, BYTE count, DWORD s0, DWORD s1,
                                            DWORD s2, DWORD s3, DWORD s4, DWORD s5, DWORD s6, DWORD s7, PSID *result)
{
    NTSTATUS status = RtlAllocateAndInitializeSid(authority, count, s0, s1, s2, s3, s4, s5, s6, s7, result);
    return NT_SUCCESS(status) ? TRUE : AdvSetStatus(status);
}

ADVAPI PVOID WINAPI FreeSid(PSID sid)
{
    return RtlFreeSid(sid);
}

ADVAPI BOOL WINAPI InitializeSid(PSID sid, PSID_IDENTIFIER_AUTHORITY authority, BYTE count)
{
    NTSTATUS status = RtlInitializeSid(sid, authority, count);
    return NT_SUCCESS(status) ? TRUE : AdvSetStatus(status);
}

ADVAPI DWORD WINAPI GetSidLengthRequired(UCHAR count)
{
    return RtlLengthRequiredSid(count);
}

ADVAPI DWORD WINAPI GetLengthSid(PSID sid)
{
    AdvSetError(ERROR_SUCCESS);
    return RtlLengthSid(sid);
}

ADVAPI BOOL WINAPI IsValidSid(PSID sid)
{
    if (RtlValidSid(sid))
        return TRUE;
    AdvSetError(ERROR_INVALID_SID);
    return FALSE;
}

ADVAPI BOOL WINAPI EqualSid(PSID a, PSID b)
{
    AdvSetError(ERROR_SUCCESS);
    return RtlEqualSid(a, b);
}

ADVAPI BOOL WINAPI EqualPrefixSid(PSID a, PSID b)
{
    AdvSetError(ERROR_SUCCESS);
    return RtlEqualPrefixSid(a, b);
}

ADVAPI BOOL WINAPI CopySid(DWORD length, PSID dst, PSID src)
{
    NTSTATUS status = RtlCopySid(length, dst, src);
    return NT_SUCCESS(status) ? TRUE : AdvSetStatus(status);
}

ADVAPI PSID_IDENTIFIER_AUTHORITY WINAPI GetSidIdentifierAuthority(PSID sid)
{
    AdvSetError(ERROR_SUCCESS);
    return RtlIdentifierAuthoritySid(sid);
}

ADVAPI PDWORD WINAPI GetSidSubAuthority(PSID sid, DWORD index)
{
    AdvSetError(ERROR_SUCCESS);
    return RtlSubAuthoritySid(sid, index);
}

ADVAPI PUCHAR WINAPI GetSidSubAuthorityCount(PSID sid)
{
    AdvSetError(ERROR_SUCCESS);
    return RtlSubAuthorityCountSid(sid);
}

/* ---------------------------------------------------------------- string form */

/* the text of a sid into a buffer of LocalAlloc, wide characters including the terminator. */
static PWSTR sid_text(PSID sid, DWORD *chars)
{
    UNICODE_STRING text;
    NTSTATUS status = sid ? RtlConvertSidToUnicodeString(&text, sid, TRUE) : STATUS_INVALID_PARAMETER;
    if (!NT_SUCCESS(status)) {
        AdvSetStatus(status);
        return nullptr;
    }
    PWSTR out = LocalAlloc(LMEM_FIXED, text.Length + sizeof(WCHAR));
    if (out) {
        memcpy(out, text.Buffer, text.Length);
        out[text.Length / sizeof(WCHAR)] = 0;
        *chars = text.Length / sizeof(WCHAR);
    } else {
        AdvSetStatus(STATUS_NO_MEMORY);
    }
    RtlFreeUnicodeString(&text);
    return out;
}

ADVAPI BOOL WINAPI ConvertSidToStringSidW(PSID sid, LPWSTR *result)
{
    DWORD chars;
    if (!result) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PWSTR text = sid_text(sid, &chars);
    if (!text)
        return FALSE;
    *result = text;
    AdvSetError(ERROR_SUCCESS);
    return TRUE;
}

ADVAPI BOOL WINAPI ConvertSidToStringSidA(PSID sid, LPSTR *result)
{
    DWORD chars;
    if (!result) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PWSTR text = sid_text(sid, &chars);
    if (!text)
        return FALSE;
    PSTR out = LocalAlloc(LMEM_FIXED, chars + 1);
    if (!out) {
        LocalFree(text);
        AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    /* sid text is ascii. */
    for (DWORD i = 0; i <= chars; i++)
        out[i] = (CHAR)text[i];
    LocalFree(text);
    *result = out;
    AdvSetError(ERROR_SUCCESS);
    return TRUE;
}

ADVAPI BOOL WINAPI ConvertStringSidToSidW(LPCWSTR string, PSID *result)
{
    if (!string || !result) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BYTE storage[SECURITY_MAX_SID_SIZE];
    DWORD error = AdvParseSidString(string, nullptr, storage, nullptr);
    if (error) {
        AdvSetError(error);
        return FALSE;
    }
    DWORD n = AdvSidLength((SID *)storage);
    PSID out = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, n);
    if (!out) {
        AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    memcpy(out, storage, n);
    *result = out;
    AdvSetError(ERROR_SUCCESS);
    return TRUE;
}

ADVAPI BOOL WINAPI ConvertStringSidToSidA(LPCSTR string, PSID *result)
{
    if (!string || !result) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PWSTR w = AdvAnsiToWide(string);
    if (!w)
        return FALSE;
    BOOL ok = ConvertStringSidToSidW(w, result);
    AdvFree(w);
    return ok;
}
