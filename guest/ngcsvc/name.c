#include "ngcsvc_private.h"

#define HRESULT_FROM_WIN32_(e) ((LONG)(e) > 0 ? (LONG)(((e) & 0xFFFF) | 0x80070000) : (LONG)(e))

/* HResultToSecurityStatus of ngcsvc.dll: what the service turns a failure of its helpers into. */
LONG NgcStatusFromHresult(LONG hr)
{
    static const struct {
        ULONG From, To;
    } map[] = {
        {0x8007000E, 0x8009000E}, {0x80004001, 0x80090029}, {0x80004003, 0x80090027}, {0x80004005, 0x80090020},
        {0x8000FFFF, 0x8009002D}, {0x80070002, 0x80090011}, {0x80070005, 0x80090010}, {0x80070057, 0x80090027},
        {0x800700B7, 0x8009000F}, {0x80070490, 0x80090011}, {0x800704C7, 0x80090036}, {0x80072742, 0x80090030},
        {0x80072741, 0x80090035}, {0x80072744, 0x80090035}, {0x80072745, 0x80090035}, {0x80072746, 0x80090035},
        {0x80072749, 0x80090035}, {0x8007274A, 0x80090035}, {0x8007274C, 0x80090035}, {0x8007274D, 0x80090035},
        {0x80072751, 0x80090035}, {0x8010006B, 0x80090033}, {0x8010006C, 0x80090031}, {0x83760002, 0x80090005},
    };
    for (size_t i = 0; i < RTL_NUMBER_OF(map); i++)
        if ((ULONG)hr == map[i].From)
            return (LONG)map[i].To;
    return hr;
}

void NgcFreeKeyName(NGC_KEY_NAME *name)
{
    if (name->Sid)
        LocalFree(name->Sid);
    name->Sid = nullptr;
}

static const WCHAR *slash(const WCHAR *p)
{
    while (*p && *p != L'/')
        p++;
    return *p ? p : nullptr;
}

/*
 * NgcKeyName::ParseNgcKeyNameString of ngcsvc.dll: five parts between four slashes, 1075 characters at most. the sid
 * and the container may be empty, something has to follow each of the first two slashes, and the last part holds no
 * slash. a failure is the HRESULT the service maps to its status.
 */
LONG NgcParseKeyName(const WCHAR *text, NGC_KEY_NAME *name)
{
    memset(name, 0, sizeof(*name));
    size_t length = wcslen(text);
    if (length > NGC_MAX_NAME)
        return NGC_E_INVALIDARG;
    const WCHAR *end = slash(text);
    if (!end)
        return NGC_E_INVALIDARG;
    WCHAR part[NGC_MAX_NAME + 1];
    if (end != text) {
        memcpy(part, text, (size_t)(end - text) * sizeof(WCHAR));
        part[end - text] = 0;
        if (!ConvertStringSidToSidW(part, &name->Sid)) {
            name->Sid = nullptr;
            return HRESULT_FROM_WIN32_(GetLastError());
        }
    }
    const WCHAR *start = end + 1;
    LONG result = NGC_E_INVALIDARG;
    if (!*start || !(end = slash(start)))
        goto fail;
    if (end != start) {
        memcpy(part, start, (size_t)(end - start) * sizeof(WCHAR));
        part[end - start] = 0;
        RPC_STATUS status = UuidFromStringW(part, &name->Container);
        if (status) {
            result = HRESULT_FROM_WIN32_(status);
            goto fail;
        }
    }
    start = end + 1;
    if (!*start || !(end = slash(start)))
        goto fail;
    name->Domain = (size_t)(end - start);
    start = end + 1;
    if (!*start || !(end = slash(start)))
        goto fail;
    name->Tenant = (size_t)(end - start);
    start = end + 1;
    if (slash(start))
        goto fail;
    name->User = wcslen(start);
    return 0;
fail:
    NgcFreeKeyName(name);
    return result;
}

/*
 * NgcKspCheckKeyAccessRight and AddSidAndCheckKeyAccess of ngcsvc.dll for a caller outside an application package:
 * a name without a sid takes the caller's, and then the whole name has to fit as BuildNgcKeyNameString writes it, with
 * the 36 characters of a container. a name with another sid is refused, except from the system. the capabilities an
 * application package needs are not evaluated: such a caller is refused.
 */
LONG NgcCheckKeyAccess(NGC_KEY_NAME *name, BOOL *other)
{
    *other = FALSE;
    RPC_STATUS status = RpcImpersonateClient(nullptr);
    if (status)
        return HRESULT_FROM_WIN32_(status);
    HANDLE token = nullptr;
    BYTE user[128];
    DWORD size = 0, container = 0;
    LONG result = 0;
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token) ||
        !GetTokenInformation(token, TokenUser, user, sizeof(user), &size))
        result = HRESULT_FROM_WIN32_(GetLastError());
    else if (!GetTokenInformation(token, TokenIsAppContainer, &container, sizeof(container), &size))
        result = HRESULT_FROM_WIN32_(GetLastError());
    if (token)
        CloseHandle(token);
    RpcRevertToSelf();
    if (result < 0)
        return result;
    if (container)
        return NTE_PERM;
    PSID caller = *(PSID *)user;
    if (name->Sid) {
        if (IsWellKnownSid(caller, WinLocalSystemSid) || EqualSid(name->Sid, caller))
            return 0;
        *other = TRUE;
        return NTE_PERM;
    }
    WCHAR *text = nullptr;
    if (!ConvertSidToStringSidW(caller, &text))
        return NgcStatusFromHresult(HRESULT_FROM_WIN32_(GetLastError()));
    size_t sid = wcslen(text);
    if (sid > NGC_MAX_SID_TEXT || name->Domain > NGC_MAX_PART || name->Tenant > NGC_MAX_PART ||
        name->User > NGC_MAX_PART || sid + NGC_GUID_TEXT + name->Domain + name->Tenant + name->User + 4 > NGC_MAX_NAME)
        result = NGC_E_INVALIDARG;
    else if (!ConvertStringSidToSidW(text, &name->Sid))
        result = NgcStatusFromHresult(HRESULT_FROM_WIN32_(GetLastError()));
    LocalFree(text);
    return result;
}
