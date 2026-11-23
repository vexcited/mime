#ifndef MIME_NGCSVC_PRIVATE_H
#define MIME_NGCSVC_PRIVATE_H

#include <mime/win32/winbase.h>
#include <mime/win32/winsvc.h>
#include <mime/win32/rpc.h>
#include <mime/win32/ncrypt.h>

#define TOKEN_QUERY 0x0008
#define TokenUser 1
#define TokenIsAppContainer 29
#define WinLocalSystemSid 22
#define SDDL_REVISION_1 1

#define NTE_BAD_DATA ((LONG)0x80090005)
#define NTE_BAD_FLAGS ((LONG)0x80090009)
#define NTE_BAD_TYPE ((LONG)0x8009000A)
#define NTE_BAD_KEY_STATE ((LONG)0x8009000B)
#define NTE_EXISTS ((LONG)0x8009000F)
#define NTE_PERM ((LONG)0x80090010)
#define NTE_NO_KEY ((LONG)0x80090011)
#define NTE_BAD_KEYSET ((LONG)0x80090016)
#define NTE_NO_MEMORY ((LONG)0x8009000E)
#define NTE_FAIL ((LONG)0x80090020)
#define NTE_INVALID_PARAMETER ((LONG)0x80090027)
#define NTE_BUFFER_TOO_SMALL ((LONG)0x80090028)
#define NTE_NOT_SUPPORTED ((LONG)0x80090029)
#define NTE_NO_MORE_ITEMS ((LONG)0x8009002A)
#define NGC_E_INVALIDARG ((LONG)0x80070057)
#define NGC_E_OUTOFMEMORY ((LONG)0x8007000E)

#define NGC_MAX_NAME 1075
#define NGC_MAX_PART 260
#define NGC_MAX_SID_TEXT 255
#define NGC_GUID_TEXT 36

WINBASEAPI BOOL WINAPI OpenThreadToken(HANDLE thread, DWORD access, BOOL as_self, PHANDLE token);
WINBASEAPI BOOL WINAPI GetTokenInformation(HANDLE token, DWORD klass, LPVOID info, DWORD length, PDWORD returned);
WINBASEAPI BOOL WINAPI ConvertSidToStringSidW(PSID sid, LPWSTR *text);
WINBASEAPI BOOL WINAPI ConvertStringSidToSidW(LPCWSTR text, PSID *sid);
WINBASEAPI BOOL WINAPI ConvertStringSecurityDescriptorToSecurityDescriptorW(LPCWSTR text, DWORD revision,
                                                                            PSECURITY_DESCRIPTOR *sd, PULONG size);
WINBASEAPI BOOL WINAPI EqualSid(PSID a, PSID b);
WINBASEAPI BOOL WINAPI IsWellKnownSid(PSID sid, int type);

extern RPC_IF_HANDLE NgcKsp_s_ifspec;

/* a key name: the sid of its user, the container, the domain of the identity provider, the tenant and the user,
 * between slashes. the sid and the container may be left out. */
typedef struct NGC_KEY_NAME {
    PSID Sid;
    GUID Container;
    size_t Domain, Tenant, User;
} NGC_KEY_NAME;

void NgcKspInit(void);
LONG NgcParseKeyName(const WCHAR *text, NGC_KEY_NAME *name);
void NgcFreeKeyName(NGC_KEY_NAME *name);
LONG NgcStatusFromHresult(LONG hr);
LONG NgcCheckKeyAccess(NGC_KEY_NAME *name, BOOL *other);

#endif
