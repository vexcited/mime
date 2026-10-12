/*
 * lan manager network functions of a standalone machine.
 *
 * mime has no network stack, so there is no server service sharing folders
 * and no remote machine to reach. the machine is a member of the default
 * workgroup, the state windows setup leaves a computer in.
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
#define PLATFORM_ID_NT 500

typedef enum _NETSETUP_JOIN_STATUS {
    NetSetupUnknownStatus,
    NetSetupUnjoined,
    NetSetupWorkgroupName,
    NetSetupDomainName,
} NETSETUP_JOIN_STATUS, *PNETSETUP_JOIN_STATUS;

typedef struct _WKSTA_INFO_102 {
    DWORD wki102_platform_id;
    LMSTR wki102_computername;
    LMSTR wki102_langroup;
    DWORD wki102_ver_major;
    DWORD wki102_ver_minor;
    LMSTR wki102_lanroot;
    DWORD wki102_logged_on_users;
} WKSTA_INFO_102;

/* the workgroup windows setup joins, recorded nowhere else in mime. */
static const WCHAR g_workgroup[] = L"WORKGROUP";

/* ---------------------------------------------------------------- buffers */

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
        *buffer = NULL;
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

/* ---------------------------------------------------------------- machine */

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

NETAPI NET_API_STATUS WINAPI NetGetJoinInformation(LPCWSTR server, LPWSTR *name, PNETSETUP_JOIN_STATUS type)
{
    if (!name || !type)
        return ERROR_INVALID_PARAMETER;
    if (!local_server(server))
        return ERROR_BAD_NETPATH;
    NET_API_STATUS status = NetApiBufferAllocate(sizeof(g_workgroup), (LPVOID *)name);
    if (status)
        return status;
    memcpy(*name, g_workgroup, sizeof(g_workgroup));
    *type = NetSetupWorkgroupName;
    return NERR_Success;
}

/* levels 100, 101 and 102 share their leading fields. */
NETAPI NET_API_STATUS WINAPI NetWkstaGetInfo(LMSTR server, DWORD level, LPBYTE *buffer)
{
    if (!buffer)
        return ERROR_INVALID_PARAMETER;
    if (level != 100 && level != 101 && level != 102)
        return ERROR_INVALID_LEVEL;
    if (!local_server(server))
        return ERROR_BAD_NETPATH;
    WCHAR computer[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD n = RTL_NUMBER_OF(computer);
    if (!GetComputerNameW(computer, &n))
        return GetLastError();
    SIZE_T fixed = level == 100 ? offsetof(WKSTA_INFO_102, wki102_lanroot)
                   : level == 101 ? offsetof(WKSTA_INFO_102, wki102_logged_on_users)
                                  : sizeof(WKSTA_INFO_102);
    fixed = (fixed + 7) & ~(SIZE_T)7;
    SIZE_T size = fixed + (n + 1) * sizeof(WCHAR) + sizeof(g_workgroup) + sizeof(WCHAR);
    WKSTA_INFO_102 *info;
    NET_API_STATUS status = NetApiBufferAllocate((DWORD)size, (LPVOID *)&info);
    if (status)
        return status;
    memset(info, 0, size);
    PWSTR text = (PWSTR)((BYTE *)info + fixed);
    info->wki102_platform_id = PLATFORM_ID_NT;
    info->wki102_computername = text;
    memcpy(text, computer, (n + 1) * sizeof(WCHAR));
    text += n + 1;
    info->wki102_langroup = text;
    memcpy(text, g_workgroup, sizeof(g_workgroup));
    text += RTL_NUMBER_OF(g_workgroup);
    info->wki102_ver_major = 10;
    info->wki102_ver_minor = 0;
    if (level >= 101)
        info->wki102_lanroot = text; /* empty */
    if (level == 102)
        info->wki102_logged_on_users = 1;
    *buffer = (LPBYTE)info;
    return NERR_Success;
}

/* ---------------------------------------------------------------- shares */

/* the server service is not running, and other machines cannot be reached. */
NETAPI NET_API_STATUS WINAPI NetShareEnum(LMSTR server, DWORD level, LPBYTE *buffer, DWORD max_length,
                                          LPDWORD read, LPDWORD total, LPDWORD resume)
{
    if (level != 0 && level != 1 && level != 2 && level != 502 && level != 503)
        return ERROR_INVALID_LEVEL;
    if (!buffer || !read || !total)
        return ERROR_INVALID_PARAMETER;
    *buffer = NULL;
    *read = 0;
    *total = 0;
    return local_server(server) ? NERR_ServerNotStarted : ERROR_BAD_NETPATH;
}
