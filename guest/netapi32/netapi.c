/*
 * lan manager functions for a standalone machine.
 *
 * mime has no network stack, so there is no server service sharing folders
 * and no remote machine to reach. the machine is in the default workgroup,
 * as Windows setup leaves it.
 */
#include <mime/nt.h>
#include <mime/ntrtl.h>
#include <mime/win32/winbase.h>

#define NETAPI DECLSPEC_EXPORT

typedef DWORD NET_API_STATUS;
typedef WCHAR *LMSTR;
typedef const WCHAR *LMCSTR;

#define NERR_Success 0
#define NERR_ServerNotStarted 2114
#define MAX_PREFERRED_LENGTH ((DWORD)-1)

typedef enum _NETSETUP_JOIN_STATUS {
    NetSetupUnknownStatus,
    NetSetupUnjoined,
    NetSetupWorkgroupName,
    NetSetupDomainName,
} NETSETUP_JOIN_STATUS, *PNETSETUP_JOIN_STATUS;

NETAPI NET_API_STATUS WINAPI NetApiBufferAllocate(DWORD size, LPVOID *buffer)
{
    if (!buffer)
        return ERROR_INVALID_PARAMETER;
    *buffer = LocalAlloc(LMEM_FIXED, size);
    return *buffer ? NERR_Success : ERROR_NOT_ENOUGH_MEMORY;
}

NETAPI NET_API_STATUS WINAPI NetApiBufferFree(LPVOID buffer)
{
    if (buffer)
        LocalFree(buffer);
    return NERR_Success;
}

NETAPI NET_API_STATUS WINAPI NetApiBufferReallocate(LPVOID old, DWORD size, LPVOID *buffer)
{
    if (!buffer)
        return ERROR_INVALID_PARAMETER;
    if (!size) {
        NetApiBufferFree(old);
        *buffer = nullptr;
        return NERR_Success;
    }
    LPVOID p = old ? LocalReAlloc(old, size, LMEM_MOVEABLE) : LocalAlloc(LMEM_FIXED, size);
    if (!p)
        return ERROR_NOT_ENOUGH_MEMORY;
    *buffer = p;
    return NERR_Success;
}

NETAPI NET_API_STATUS WINAPI NetApiBufferSize(LPVOID buffer, LPDWORD size)
{
    if (!buffer || !size)
        return ERROR_INVALID_PARAMETER;
    *size = (DWORD)LocalSize(buffer);
    return NERR_Success;
}

static BOOL local_server(LMCSTR server)
{
    if (!server || !*server)
        return TRUE;
    while (*server == L'\\')
        server++;
    WCHAR computer[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD n = RTL_NUMBER_OF(computer);
    return !_wcsicmp(server, L".") || !_wcsicmp(server, L"localhost") ||
           (GetComputerNameW(computer, &n) && !_wcsicmp(server, computer));
}

/* the workstation calls live in wkscli.dll, which netapi32 loads at the first of them, as Windows does. */
static HMODULE g_wkscli;

static FARPROC wkscli_function(const char *name, DWORD *error)
{
    *error = NERR_Success;
    HMODULE module = g_wkscli;
    if (!module) {
        module = LoadLibraryExW(L"wkscli.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) {
            *error = GetLastError();
            return nullptr;
        }
        if (InterlockedCompareExchangePointer((PVOID volatile *)&g_wkscli, module, nullptr))
            FreeLibrary(module);
        module = g_wkscli;
    }
    FARPROC function = GetProcAddress(module, name);
    if (!function)
        *error = GetLastError();
    return function;
}

NETAPI NET_API_STATUS WINAPI NetGetJoinInformation(LPCWSTR server, LPWSTR *name, PNETSETUP_JOIN_STATUS type)
{
    DWORD error;
    NET_API_STATUS(WINAPI * function)(LPCWSTR, LPWSTR *, PNETSETUP_JOIN_STATUS) =
        (void *)wkscli_function("NetGetJoinInformation", &error);
    return function ? function(server, name, type) : error;
}

NETAPI NET_API_STATUS WINAPI NetWkstaGetInfo(LMSTR server, DWORD level, LPBYTE *buffer)
{
    DWORD error;
    NET_API_STATUS(WINAPI * function)(LMSTR, DWORD, LPBYTE *) = (void *)wkscli_function("NetWkstaGetInfo", &error);
    return function ? function(server, level, buffer) : error;
}

NETAPI NET_API_STATUS WINAPI NetWkstaUserGetInfo(LMSTR reserved, DWORD level, LPBYTE *buffer)
{
    DWORD error;
    NET_API_STATUS(WINAPI * function)(LMSTR, DWORD, LPBYTE *) = (void *)wkscli_function("NetWkstaUserGetInfo", &error);
    return function ? function(reserved, level, buffer) : error;
}

/* the server service is not running, and other machines cannot be reached. */
NETAPI NET_API_STATUS WINAPI NetShareEnum(LMSTR server, DWORD level, LPBYTE *buffer, DWORD max_length,
                                          LPDWORD read, LPDWORD total, LPDWORD resume)
{
    if (level != 0 && level != 1 && level != 2 && level != 502 && level != 503)
        return ERROR_INVALID_LEVEL;
    if (!buffer || !read || !total)
        return ERROR_INVALID_PARAMETER;
    *buffer = nullptr;
    *read = 0;
    *total = 0;
    return local_server(server) ? NERR_ServerNotStarted : ERROR_BAD_NETPATH;
}

NETAPI NET_API_STATUS WINAPI NetShareAdd(LMSTR server, DWORD level, LPBYTE buffer, LPDWORD parameter)
{
    if (parameter)
        *parameter = 0;
    if (level != 2 && level != 502 && level != 503)
        return ERROR_INVALID_LEVEL;
    if (!buffer)
        return ERROR_INVALID_PARAMETER;
    return local_server(server) ? NERR_ServerNotStarted : ERROR_BAD_NETPATH;
}

NETAPI NET_API_STATUS WINAPI NetValidatePasswordPolicy(LPCWSTR server, LPVOID qualifier, int type,
                                                       LPVOID input, LPVOID *output)
{
    if (!output)
        return ERROR_INVALID_PARAMETER;
    *output = nullptr;
    if (qualifier || type < 1 || type > 3 || !input)
        return ERROR_INVALID_PARAMETER;
    return local_server(server) ? ERROR_NOT_SUPPORTED : ERROR_BAD_NETPATH;
}

NETAPI NET_API_STATUS WINAPI NetValidatePasswordPolicyFree(LPVOID *output)
{
    if (!output)
        return ERROR_INVALID_PARAMETER;
    if (*output && LocalFree(*output))
        return ERROR_INVALID_PARAMETER;
    *output = nullptr;
    return NERR_Success;
}
