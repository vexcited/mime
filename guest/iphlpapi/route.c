/* only the software loopback has routes in this backend. */
#define MIME_BUILD_IPHLPAPI
#include <mime/win32/iphlpapi.h>

#define ERROR_HOST_UNREACHABLE 1232L

DECLSPEC_EXPORT DWORD WINAPI GetBestInterfaceEx(const struct sockaddr *address, DWORD *index)
{
    if (!address || !index)
        return ERROR_INVALID_PARAMETER;
    *index = 0;
    if (address->sa_family == AF_INET) {
        const struct sockaddr_in *v4 = (const struct sockaddr_in *)address;
        if (((const BYTE *)&v4->sin_addr)[0] == 127) {
            *index = 1;
            return NO_ERROR;
        }
    } else if (address->sa_family == AF_INET6) {
        const struct sockaddr_in6 *v6 = (const struct sockaddr_in6 *)address;
        static const BYTE loopback[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
        if (!memcmp(&v6->sin6_addr, loopback, sizeof(loopback))) {
            *index = 1;
            return NO_ERROR;
        }
    } else {
        return ERROR_INVALID_PARAMETER;
    }
    return ERROR_HOST_UNREACHABLE;
}
