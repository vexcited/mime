/* data protection api: CryptProtectData, CryptProtectMemory and their types. */
#ifndef MIME_WIN32_DPAPI_H
#define MIME_WIN32_DPAPI_H

#include <mime/win32/wincrypt.h>

#define szFORCE_KEY_PROTECTION "ForceKeyProtection"

#define CRYPTPROTECT_PROMPT_ON_UNPROTECT 0x1
#define CRYPTPROTECT_PROMPT_ON_PROTECT 0x2
#define CRYPTPROTECT_PROMPT_RESERVED 0x4
#define CRYPTPROTECT_PROMPT_STRONG 0x8
#define CRYPTPROTECT_PROMPT_REQUIRE_STRONG 0x10

#define CRYPTPROTECT_UI_FORBIDDEN 0x1
#define CRYPTPROTECT_LOCAL_MACHINE 0x4
#define CRYPTPROTECT_CRED_SYNC 0x8
#define CRYPTPROTECT_AUDIT 0x10
#define CRYPTPROTECT_NO_RECOVERY 0x20
#define CRYPTPROTECT_VERIFY_PROTECTION 0x40
#define CRYPTPROTECT_CRED_REGENERATE 0x80
#define CRYPTPROTECT_SYSTEM 0x20000000
#define CRYPTPROTECT_FIRST_RESERVED_FLAGVAL 0x0FFFFFFF
#define CRYPTPROTECT_LAST_RESERVED_FLAGVAL 0xFFFFFFFF

#define CRYPTPROTECTMEMORY_BLOCK_SIZE 16
#define CRYPTPROTECTMEMORY_SAME_PROCESS 0x00
#define CRYPTPROTECTMEMORY_CROSS_PROCESS 0x01
#define CRYPTPROTECTMEMORY_SAME_LOGON 0x02

#ifndef ERROR_PASSWORD_RESTRICTION
#define ERROR_PASSWORD_RESTRICTION 1325L
#endif
#ifndef CRYPT_I_NEW_PROTECTION_REQUIRED
#define CRYPT_I_NEW_PROTECTION_REQUIRED ((HRESULT)0x00091012L)
#endif

typedef struct _CRYPTPROTECT_PROMPTSTRUCT {
    DWORD cbSize;
    DWORD dwPromptFlags;
    HWND hwndApp;
    LPCWSTR szPrompt;
} CRYPTPROTECT_PROMPTSTRUCT, *PCRYPTPROTECT_PROMPTSTRUCT;

WINCRYPT32API BOOL WINAPI CryptProtectData(DATA_BLOB *pDataIn, LPCWSTR szDataDescr, DATA_BLOB *pOptionalEntropy,
                                           PVOID pvReserved, CRYPTPROTECT_PROMPTSTRUCT *pPromptStruct, DWORD dwFlags,
                                           DATA_BLOB *pDataOut);
WINCRYPT32API BOOL WINAPI CryptUnprotectData(DATA_BLOB *pDataIn, LPWSTR *ppszDataDescr, DATA_BLOB *pOptionalEntropy,
                                             PVOID pvReserved, CRYPTPROTECT_PROMPTSTRUCT *pPromptStruct,
                                             DWORD dwFlags, DATA_BLOB *pDataOut);
WINCRYPT32API BOOL WINAPI CryptUpdateProtectedState(PSID pOldSid, LPCWSTR pwszOldPassword, DWORD dwFlags,
                                                    DWORD *pdwSuccessCount, DWORD *pdwFailureCount);
WINCRYPT32API BOOL WINAPI CryptProtectMemory(LPVOID pDataIn, DWORD cbDataIn, DWORD dwFlags);
WINCRYPT32API BOOL WINAPI CryptUnprotectMemory(LPVOID pDataIn, DWORD cbDataIn, DWORD dwFlags);

#endif
