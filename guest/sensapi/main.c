/*
 * sensapi.dll: the system event notification service's connectivity checks. the host is always online.
 */
#include <mime/win32/winbase.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

typedef struct _QOCINFO_LOCAL {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwInSpeed;
    DWORD dwOutSpeed;
} QOCINFO_LOCAL, *LPQOCINFO_LOCAL;

BOOL WINAPI IsNetworkAlive(LPDWORD flags)
{
    if (flags)
        *flags = 1; /* NETWORK_ALIVE_LAN */
    return TRUE;
}

BOOL WINAPI IsDestinationReachableA(LPCSTR destination, LPQOCINFO_LOCAL info)
{
    (void)destination;
    if (info && info->dwSize >= sizeof(QOCINFO_LOCAL)) {
        info->dwFlags = 0;
        info->dwInSpeed = 10000000;
        info->dwOutSpeed = 10000000;
    }
    return TRUE;
}

BOOL WINAPI IsDestinationReachableW(LPCWSTR destination, LPQOCINFO_LOCAL info)
{
    (void)destination;
    if (info && info->dwSize >= sizeof(QOCINFO_LOCAL)) {
        info->dwFlags = 0;
        info->dwInSpeed = 10000000;
        info->dwOutSpeed = 10000000;
    }
    return TRUE;
}
