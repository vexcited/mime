#include "caller.h"
#include <mime/ntrtlsec.h>

HRESULT XferImpersonateCaller(void)
{
    HRESULT hr = CoImpersonateClient();
    if (FAILED(hr))
        return hr;
    HANDLE token;
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY_, TRUE, &token) &&
        !OpenThreadToken(GetCurrentThread(), TOKEN_QUERY_, FALSE, &token)) {
        CoRevertToSelf();
        return CO_E_FAILEDTOIMPERSONATE_;
    }
    DWORD level = 0, size;
    GetTokenInformation(token, TOKEN_IMPERSONATION_LEVEL_CLASS_, &level, sizeof(level), &size);
    CloseHandle(token);
    if (level < SECURITY_IMPERSONATION_) {
        CoRevertToSelf();
        return CO_E_FAILEDTOIMPERSONATE_;
    }
    return S_OK;
}

/* a duplicate of the caller's token that later work can impersonate, whatever level the caller gave. */
HANDLE XferCallerToken(void)
{
    if (FAILED(CoImpersonateClient()))
        return nullptr;
    HANDLE token = nullptr;
    /* a service account may not open the caller's token as itself: the caller's own access opens it then. */
    DWORD access = TOKEN_IMPERSONATE_ | TOKEN_QUERY_ | TOKEN_DUPLICATE_;
    if (!OpenThreadToken(GetCurrentThread(), access, TRUE, &token) &&
        !OpenThreadToken(GetCurrentThread(), access, FALSE, &token))
        token = nullptr;
    CoRevertToSelf();
    return token;
}

WCHAR *XferTokenSid(HANDLE token)
{
    BYTE buffer[sizeof(PSID) * 2 + SECURITY_MAX_SID_SIZE];
    DWORD size;
    if (!GetTokenInformation(token, TOKEN_USER_CLASS_, buffer, sizeof(buffer), &size))
        return nullptr;
    WCHAR *local = nullptr;
    if (!ConvertSidToStringSidW(*(PSID *)buffer, &local))
        return nullptr;
    size_t length = (wcslen(local) + 1) * sizeof(WCHAR);
    WCHAR *text = CoTaskMemAlloc(length);
    if (text)
        memcpy(text, local, length);
    LocalFree(local);
    return text;
}

WCHAR *XferCallerSid(void)
{
    HANDLE token = XferCallerToken();
    if (!token)
        return nullptr;
    WCHAR *text = XferTokenSid(token);
    CloseHandle(token);
    return text;
}

/* membership as the token holds it, so a filtered administrator is no administrator. */
BOOL XferTokenIsAdmin(HANDLE token)
{
    PSID admins = nullptr;
    BOOL member = FALSE;
    if (ConvertStringSidToSidW(L"S-1-5-32-544", &admins)) {
        CheckTokenMembership(token, admins, &member);
        LocalFree(admins);
    }
    return member;
}

BOOL XferCallerIsAdmin(void)
{
    HANDLE token = XferCallerToken();
    if (!token)
        return FALSE;
    BOOL admin = XferTokenIsAdmin(token);
    CloseHandle(token);
    return admin;
}

BOOL XferRunAs(HANDLE token)
{
    return token && SetThreadToken(nullptr, token);
}

void XferRunAsSelf(void)
{
    SetThreadToken(nullptr, nullptr);
}
