/*
 * the prompts of credui. Windows leaves them to the internal credential user interface api set and answers
 * ERROR_NOT_SUPPORTED when the set is not there. Here too, and a library that supplies the set is called as Windows
 * calls it. The SSPI prompts check their flags first.
 */
#include <mime/stubexport.h>
#include <mime/win32/winbase.h>
#include <mime/win32/winnls.h>

#define CREDUI_API_SET L"ext-ms-win-security-credui-internal-l1-1-0.dll"

typedef struct {
    USHORT Length, MaximumLength;
    PWSTR Buffer;
} API_SET_NAME;

/* 1 when the api set is there, 2 when it is not. */
static LONG volatile g_api_set;
static HMODULE g_internal;

static HMODULE internal_library(void)
{
    LONG known = g_api_set;
    if (known == 2)
        return nullptr;
    if (known == 1)
        return g_internal;
    typedef LONG(WINAPI * QUERY)(const API_SET_NAME *, BOOLEAN *);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    QUERY query = ntdll ? (QUERY)GetProcAddress(ntdll, "ApiSetQueryApiSetPresence") : nullptr;
    static WCHAR name[] = CREDUI_API_SET;
    API_SET_NAME set = {sizeof(CREDUI_API_SET) - 3 * sizeof(WCHAR), sizeof(CREDUI_API_SET) - 2 * sizeof(WCHAR), name};
    BOOLEAN present = FALSE;
    if (query && query(&set, &present) >= 0 && present) {
        g_internal = LoadLibraryW(CREDUI_API_SET);
        if (g_internal) {
            g_api_set = 1;
            return g_internal;
        }
    }
    g_api_set = 2;
    return nullptr;
}

static FARPROC internal_function(const char *name)
{
    HMODULE library = internal_library();
    return library ? GetProcAddress(library, name) : nullptr;
}

#define CALL_0
#define CALL_1 a1
#define CALL_2 CALL_1, a2
#define CALL_3 CALL_2, a3
#define CALL_4 CALL_3, a4
#define CALL_5 CALL_4, a5
#define CALL_6 CALL_5, a6
#define CALL_7 CALL_6, a7
#define CALL_8 CALL_7, a8
#define CALL_9 CALL_8, a9
#define CALL_10 CALL_9, a10
#define CALL_11 CALL_10, a11

/* passes the call on to the function of the api set, which has the same arguments. */
#define CREDUI_FORWARD(name, internal, args)                                                                           \
    DECLSPEC_EXPORT DWORD WINAPI name(MIME_STUB_ARGS_##args)                                                           \
    {                                                                                                                  \
        DWORD(WINAPI * function)(MIME_STUB_ARGS_##args) = (DWORD(WINAPI *)(MIME_STUB_ARGS_##args))internal_function(internal); \
        if (!function)                                                                                                 \
            return ERROR_NOT_SUPPORTED;                                                                                \
        return function(CALL_##args);                                                                                  \
    }

CREDUI_FORWARD(CredUIPromptForCredentialsW, "CredUIInternalPromptForCredentialsW", 10)
CREDUI_FORWARD(CredUIPromptForCredentialsA, "CredUIInternalPromptForCredentialsA", 10)
CREDUI_FORWARD(CredUIConfirmCredentialsW, "CredUIInternalConfirmCredentialsW", 2)
CREDUI_FORWARD(CredUIConfirmCredentialsA, "CredUIInternalConfirmCredentialsA", 2)
CREDUI_FORWARD(CredUICmdLinePromptForCredentialsW, "CredUIInternalCmdLinePromptForCredentialsW", 9)
CREDUI_FORWARD(CredUICmdLinePromptForCredentialsA, "CredUIInternalCmdLinePromptForCredentialsA", 9)
CREDUI_FORWARD(CredUIPromptForWindowsCredentialsW, "CredUIInternalPromptForWindowsCredentialsW", 9)
CREDUI_FORWARD(CredUIPromptForWindowsCredentialsA, "CredUIInternalPromptForWindowsCredentialsA", 9)
CREDUI_FORWARD(CredUIPromptForWindowsCredentialsWorker, "CredUIInternalPromptForWindowsCredentialsWorker", 11)

/* the packed credentials of the ANSI form are not given out, and both calls fail the same way. */
DECLSPEC_EXPORT BOOL WINAPI CredPackAuthenticationBufferA(ULONG_PTR a1, ULONG_PTR a2, ULONG_PTR a3, ULONG_PTR a4,
                                                          ULONG_PTR a5)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

DECLSPEC_EXPORT BOOL WINAPI CredUnPackAuthenticationBufferA(ULONG_PTR a1, ULONG_PTR a2, ULONG_PTR a3, ULONG_PTR a4,
                                                            ULONG_PTR a5, ULONG_PTR a6, ULONG_PTR a7, ULONG_PTR a8,
                                                            ULONG_PTR a9)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

/*
 * the single sign on credential is kept in memory for the moment and is written to the credential store only when it is
 * to persist, which fails without a store.
 */
DECLSPEC_EXPORT DWORD WINAPI CredUIStoreSSOCredW(LPCWSTR realm, LPCWSTR user, LPCWSTR password, BOOL persist)
{
    if (!user || !password)
        return ERROR_GEN_FAILURE;
    if (wcslen(user) > 0x200 || wcslen(password) > 0xFF || (realm && !*realm))
        return ERROR_INVALID_PARAMETER;
    return persist ? ERROR_GEN_FAILURE : ERROR_SUCCESS;
}

DECLSPEC_EXPORT DWORD WINAPI CredUIStoreSSOCredA(LPCSTR realm, LPCSTR user, LPCSTR password, BOOL persist)
{
    WCHAR wide_realm[337], wide_user[514], wide_password[257];
    DWORD result = ERROR_GEN_FAILURE;
    if (realm && !MultiByteToWideChar(CP_ACP, 0, realm, -1, wide_realm, 337))
        goto done;
    if (user && !MultiByteToWideChar(CP_ACP, 0, user, -1, wide_user, 514))
        goto done;
    if (password && !MultiByteToWideChar(CP_ACP, 0, password, -1, wide_password, 257))
        goto done;
    result = CredUIStoreSSOCredW(realm ? wide_realm : nullptr, user ? wide_user : nullptr,
                                 password ? wide_password : nullptr, persist);
done:
    SecureZeroMemory(wide_password, sizeof(wide_password));
    return result;
}

/* the options of an SSPI prompt are the four lowest flags. */
DECLSPEC_EXPORT DWORD WINAPI SspiPromptForCredentialsW(LPCWSTR target, PVOID ui_info, DWORD error, LPCWSTR package,
                                                       PVOID input, PVOID *output, int *save, DWORD flags)
{
    if (flags & 0xFFFFFFF0)
        return ERROR_INVALID_PARAMETER;
    return ERROR_NOT_SUPPORTED;
}

DECLSPEC_EXPORT DWORD WINAPI SspiPromptForCredentialsA(LPCSTR target, PVOID ui_info, DWORD error, LPCSTR package,
                                                       PVOID input, PVOID *output, int *save, DWORD flags)
{
    return SspiPromptForCredentialsW(nullptr, ui_info, error, nullptr, input, output, save, flags);
}

/* the credential user interface context lives with the authentication package, which is not here. */
MIME_STUB_RETURN("credui", DWORD, SspiGetCredUIContext, 5, ERROR_NOT_SUPPORTED)
MIME_STUB_RETURN("credui", DWORD, SspiUnmarshalCredUIContext, 3, ERROR_NOT_SUPPORTED)
MIME_STUB_RETURN("credui", DWORD, SspiUpdateCredentials, 4, ERROR_NOT_SUPPORTED)
