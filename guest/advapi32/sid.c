/*
 * security identifiers: allocation, comparison and the S-1-... string form.
 */
#include "advapi32_private.h"
#include <stdarg.h>

DWORD AdvSidLength(const SID *sid)
{
    return 8 + 4 * sid->SubAuthorityCount;
}

BOOL AdvSidValid(const SID *sid)
{
    return sid && sid->Revision == SID_REVISION && sid->SubAuthorityCount <= SID_MAX_SUB_AUTHORITIES;
}

BOOL AdvSidEqual(const SID *a, const SID *b)
{
    return AdvSidValid(a) && AdvSidValid(b) && AdvSidLength(a) == AdvSidLength(b) &&
           memcmp(a, b, AdvSidLength(a)) == 0;
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
    if (!authority || !result) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (count > 8) {
        AdvSetError(ERROR_INVALID_SID);
        return FALSE;
    }
    SID *sid = AdvAlloc(8 + 4 * count);
    if (!sid) {
        AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    const DWORD sub[8] = {s0, s1, s2, s3, s4, s5, s6, s7};
    sid->Revision = SID_REVISION;
    sid->SubAuthorityCount = count;
    sid->IdentifierAuthority = *authority;
    memcpy(sid->SubAuthority, sub, 4 * count);
    *result = sid;
    return TRUE;
}

ADVAPI PVOID WINAPI FreeSid(PSID sid)
{
    return sid && RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, sid) ? NULL : sid;
}

ADVAPI BOOL WINAPI InitializeSid(PSID sid, PSID_IDENTIFIER_AUTHORITY authority, BYTE count)
{
    if (count > SID_MAX_SUB_AUTHORITIES) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    SID *s = sid;
    s->Revision = SID_REVISION;
    s->SubAuthorityCount = count;
    s->IdentifierAuthority = *authority;
    return TRUE;
}

ADVAPI DWORD WINAPI GetSidLengthRequired(UCHAR count)
{
    return 8 + 4 * (DWORD)count;
}

ADVAPI DWORD WINAPI GetLengthSid(PSID sid)
{
    return AdvSidLength(sid);
}

ADVAPI BOOL WINAPI IsValidSid(PSID sid)
{
    return AdvSidValid(sid);
}

ADVAPI BOOL WINAPI EqualSid(PSID a, PSID b)
{
    AdvSetError(ERROR_SUCCESS);
    return AdvSidEqual(a, b);
}

/* equal authorities and sub authorities except the last. */
ADVAPI BOOL WINAPI EqualPrefixSid(PSID a, PSID b)
{
    const SID *x = a, *y = b;
    AdvSetError(ERROR_SUCCESS);
    if (!AdvSidValid(x) || !AdvSidValid(y) || x->SubAuthorityCount != y->SubAuthorityCount)
        return FALSE;
    if (memcmp(&x->IdentifierAuthority, &y->IdentifierAuthority, 6))
        return FALSE;
    return x->SubAuthorityCount == 0 || !memcmp(x->SubAuthority, y->SubAuthority, 4 * (x->SubAuthorityCount - 1));
}

ADVAPI BOOL WINAPI CopySid(DWORD length, PSID dst, PSID src)
{
    if (!AdvSidValid(src)) {
        AdvSetError(ERROR_INVALID_SID);
        return FALSE;
    }
    DWORD n = AdvSidLength(src);
    if (length < n) {
        AdvSetError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    memmove(dst, src, n);
    return TRUE;
}

ADVAPI PSID_IDENTIFIER_AUTHORITY WINAPI GetSidIdentifierAuthority(PSID sid)
{
    AdvSetError(ERROR_SUCCESS);
    return &((SID *)sid)->IdentifierAuthority;
}

ADVAPI PDWORD WINAPI GetSidSubAuthority(PSID sid, DWORD index)
{
    AdvSetError(ERROR_SUCCESS);
    return &((SID *)sid)->SubAuthority[index];
}

ADVAPI PUCHAR WINAPI GetSidSubAuthorityCount(PSID sid)
{
    AdvSetError(ERROR_SUCCESS);
    return &((SID *)sid)->SubAuthorityCount;
}

/* ---------------------------------------------------------------- string form */

/* the authority in decimal below 2^32, else as 0x and twelve hex digits. */
static int format_sid(const SID *sid, PWSTR out, int size)
{
    ULONG64 authority = 0;
    for (int i = 0; i < 6; i++)
        authority = authority << 8 | sid->IdentifierAuthority.Value[i];
    int n = authority >> 32 ? _snwprintf(out, size, L"S-%u-0x%012llX", sid->Revision, authority)
                            : _snwprintf(out, size, L"S-%u-%llu", sid->Revision, authority);
    for (BYTE i = 0; i < sid->SubAuthorityCount && n >= 0 && n < size; i++)
        n += _snwprintf(out + n, size - n, L"-%lu", sid->SubAuthority[i]);
    return n;
}

ADVAPI BOOL WINAPI ConvertSidToStringSidW(PSID sid, LPWSTR *result)
{
    if (!sid || !result) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!AdvSidValid(sid)) {
        AdvSetError(ERROR_INVALID_SID);
        return FALSE;
    }
    WCHAR text[200];
    int n = format_sid(sid, text, RTL_NUMBER_OF(text));
    PWSTR out = LocalAlloc(LMEM_FIXED, (n + 1) * sizeof(WCHAR));
    if (!out)
        return FALSE;
    memcpy(out, text, (n + 1) * sizeof(WCHAR));
    *result = out;
    return TRUE;
}

ADVAPI BOOL WINAPI ConvertSidToStringSidA(PSID sid, LPSTR *result)
{
    if (!sid || !result) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!AdvSidValid(sid)) {
        AdvSetError(ERROR_INVALID_SID);
        return FALSE;
    }
    WCHAR text[200];
    int n = format_sid(sid, text, RTL_NUMBER_OF(text));
    PSTR out = LocalAlloc(LMEM_FIXED, n + 1);
    if (!out)
        return FALSE;
    for (int i = 0; i <= n; i++)
        out[i] = (CHAR)text[i];
    *result = out;
    return TRUE;
}

/* the two letter aliases of the sddl language, for the sids that do not depend on a domain. */
static const struct {
    WCHAR Alias[3];
    BYTE Authority;
    BYTE Count;
    DWORD Sub[2];
} g_aliases[] = {
    {L"WD", 1, 1, {0}},       {L"CO", 3, 1, {0}},       {L"CG", 3, 1, {1}},       {L"OW", 3, 1, {4}},
    {L"NU", 5, 1, {2}},       {L"IU", 5, 1, {4}},       {L"SU", 5, 1, {6}},       {L"AN", 5, 1, {7}},
    {L"ED", 5, 1, {9}},       {L"PS", 5, 1, {10}},      {L"AU", 5, 1, {11}},      {L"RC", 5, 1, {12}},
    {L"SY", 5, 1, {18}},      {L"LS", 5, 1, {19}},      {L"NS", 5, 1, {20}},      {L"BA", 5, 2, {32, 544}},
    {L"BU", 5, 2, {32, 545}}, {L"BG", 5, 2, {32, 546}}, {L"PU", 5, 2, {32, 547}}, {L"AO", 5, 2, {32, 548}},
    {L"SO", 5, 2, {32, 549}}, {L"PO", 5, 2, {32, 550}}, {L"BO", 5, 2, {32, 551}}, {L"RE", 5, 2, {32, 552}},
    {L"RU", 5, 2, {32, 554}}, {L"RD", 5, 2, {32, 555}}, {L"NO", 5, 2, {32, 556}}, {L"MU", 5, 2, {32, 558}},
    {L"LU", 5, 2, {32, 559}}, {L"IS", 5, 2, {32, 568}}, {L"CY", 5, 2, {32, 569}}, {L"ER", 5, 2, {32, 573}},
    {L"RM", 5, 2, {32, 580}}, {L"LW", 16, 1, {0x1000}}, {L"ME", 16, 1, {0x2000}}, {L"MP", 16, 1, {0x2100}},
    {L"HI", 16, 1, {0x3000}}, {L"SI", 16, 1, {0x4000}}, {L"AC", 15, 2, {2, 1}},
};

static BOOL parse_number(PCWSTR *p, ULONG64 limit, ULONG64 *value)
{
    PCWSTR s = *p;
    ULONG64 v = 0;
    int base = 10, digits = 0;
    if (s[0] == L'0' && (s[1] == L'x' || s[1] == L'X')) {
        base = 16;
        s += 2;
    }
    for (;; s++, digits++) {
        int d;
        if (*s >= L'0' && *s <= L'9')
            d = *s - L'0';
        else if (base == 16 && *s >= L'a' && *s <= L'f')
            d = *s - L'a' + 10;
        else if (base == 16 && *s >= L'A' && *s <= L'F')
            d = *s - L'A' + 10;
        else
            break;
        v = v * base + d;
        if (v > limit)
            return FALSE;
    }
    *p = s;
    *value = v;
    return digits > 0;
}

static BOOL parse_sid(PCWSTR s, BYTE *storage)
{
    SID *sid = (SID *)storage;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_aliases); i++) {
        if (s[0] == g_aliases[i].Alias[0] && s[1] == g_aliases[i].Alias[1] && !s[2]) {
            AdvMakeSid(sid, g_aliases[i].Authority, g_aliases[i].Count, g_aliases[i].Sub[0], g_aliases[i].Sub[1]);
            return TRUE;
        }
    }
    if ((s[0] != L'S' && s[0] != L's') || s[1] != L'-')
        return FALSE;
    s += 2;
    ULONG64 revision, authority, sub;
    if (!parse_number(&s, 0xFF, &revision) || revision != SID_REVISION || *s++ != L'-' ||
        !parse_number(&s, 0xFFFFFFFFFFFFull, &authority))
        return FALSE;
    sid->Revision = SID_REVISION;
    sid->SubAuthorityCount = 0;
    for (int i = 0; i < 6; i++)
        sid->IdentifierAuthority.Value[i] = (BYTE)(authority >> (40 - 8 * i));
    while (*s == L'-') {
        s++;
        if (sid->SubAuthorityCount == SID_MAX_SUB_AUTHORITIES || !parse_number(&s, 0xFFFFFFFFu, &sub))
            return FALSE;
        sid->SubAuthority[sid->SubAuthorityCount++] = (DWORD)sub;
    }
    return *s == 0;
}

BOOL AdvParseSidString(PCWSTR string, void *storage)
{
    return parse_sid(string, storage);
}

ADVAPI BOOL WINAPI ConvertStringSidToSidW(LPCWSTR string, PSID *result)
{
    if (!string || !result) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BYTE storage[SECURITY_MAX_SID_SIZE];
    if (!parse_sid(string, storage)) {
        AdvSetError(ERROR_INVALID_SID);
        return FALSE;
    }
    DWORD n = AdvSidLength((SID *)storage);
    PSID out = LocalAlloc(LMEM_FIXED, n);
    if (!out)
        return FALSE;
    memcpy(out, storage, n);
    *result = out;
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
