#include <mime/win32/winbase.h>

#define ERROR_BAD_DEVICE 1200L
#define ERROR_NOT_CONNECTED 2250L

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

DECLSPEC_EXPORT DWORD WINAPI WNetGetConnectionW(LPCWSTR local, LPWSTR remote, LPDWORD size)
{
    if (!local || !size || (!remote && *size))
        return ERROR_INVALID_PARAMETER;
    WCHAR drive = local[0];
    if (drive >= L'a' && drive <= L'z')
        drive -= L'a' - L'A';
    if (drive < L'A' || drive > L'Z' || local[1] != L':' || local[2])
        return ERROR_BAD_DEVICE;
    return ERROR_NOT_CONNECTED;
}

DECLSPEC_EXPORT DWORD WINAPI WNetGetConnectionA(LPCSTR local, LPSTR remote, LPDWORD size)
{
    if (!local || !size || (!remote && *size))
        return ERROR_INVALID_PARAMETER;
    char drive = local[0];
    if (drive >= 'a' && drive <= 'z')
        drive -= 'a' - 'A';
    if (drive < 'A' || drive > 'Z' || local[1] != ':' || local[2])
        return ERROR_BAD_DEVICE;
    return ERROR_NOT_CONNECTED;
}

/* the network resource of the use of a connection: the local name and the remote one, among fields that stay. */
typedef struct {
    DWORD Scope, Type, DisplayType, Usage;
    LPWSTR LocalName, RemoteName, Comment, Provider;
} NETRESOURCE_W;

#define ERROR_NO_NETWORK 1222L

/* there is no network provider, so a connection is never made, and the connection of a name is never found. */
DECLSPEC_EXPORT DWORD WINAPI WNetUseConnectionW(HWND window, NETRESOURCE_W *resource, LPCWSTR password, LPCWSTR user,
                                                DWORD flags, LPWSTR access_name, LPDWORD access_size, LPDWORD result)
{
    if (!resource)
        return ERROR_INVALID_PARAMETER;
    if (result)
        *result = 0;
    return ERROR_NO_NETWORK;
}

DECLSPEC_EXPORT DWORD WINAPI WNetCancelConnectionW(LPCWSTR name, BOOL force)
{
    if (!name)
        return ERROR_INVALID_PARAMETER;
    return ERROR_NOT_CONNECTED;
}
