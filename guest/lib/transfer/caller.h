#ifndef MIME_LIB_TRANSFER_CALLER_H
#define MIME_LIB_TRANSFER_CALLER_H

#include <mime/win32/winbase.h>
#include <mime/win32/objbase.h>

#define TOKEN_DUPLICATE_ 0x0002
#define TOKEN_IMPERSONATE_ 0x0004
#define TOKEN_QUERY_ 0x0008
#define TOKEN_USER_CLASS_ 1
#define TOKEN_ELEVATION_CLASS_ 20
#define TOKEN_ELEVATION_TYPE_CLASS_ 18
#define TOKEN_INTEGRITY_CLASS_ 25
#define TOKEN_IMPERSONATION_LEVEL_CLASS_ 9
#define SECURITY_IMPERSONATION_ 2
#define FSCTL_SET_SPARSE_ 0x000900C4
#define CO_E_FAILEDTOIMPERSONATE_ ((HRESULT)0x80010123L)

WINBASEAPI BOOL WINAPI OpenThreadToken(HANDLE thread, DWORD access, BOOL as_self, PHANDLE token);
WINBASEAPI BOOL WINAPI OpenProcessToken(HANDLE process, DWORD access, PHANDLE token);
WINBASEAPI BOOL WINAPI GetTokenInformation(HANDLE token, DWORD klass, LPVOID info, DWORD length, PDWORD returned);
WINBASEAPI BOOL WINAPI SetThreadToken(PHANDLE thread, HANDLE token);
WINBASEAPI BOOL WINAPI ConvertSidToStringSidW(PSID sid, LPWSTR *text);
WINBASEAPI BOOL WINAPI ConvertStringSidToSidW(LPCWSTR text, PSID *sid);
WINBASEAPI BOOL WINAPI CheckTokenMembership(HANDLE token, PSID sid, PBOOL member);
WINBASEAPI BOOL WINAPI DuplicateTokenEx(HANDLE token, DWORD access, LPSECURITY_ATTRIBUTES sa, DWORD level, DWORD type,
                                        PHANDLE out);

/* the token of the com caller, at an impersonation level that opens files as it, or the error windows gives. */
HRESULT XferImpersonateCaller(void);
HANDLE XferCallerToken(void);
WCHAR *XferTokenSid(HANDLE token);
WCHAR *XferCallerSid(void);
BOOL XferCallerIsAdmin(void);
BOOL XferTokenIsAdmin(HANDLE token);
BOOL XferRunAs(HANDLE token);
void XferRunAsSelf(void);

#endif
