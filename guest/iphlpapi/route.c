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

/* the loopback's route and address for a loopback destination; nothing else is reachable. */
DWORD NETIOAPI_API_ GetBestRoute2(NET_LUID *luid, NET_IFINDEX index, const SOCKADDR_INET *source,
                                  const SOCKADDR_INET *destination, ULONG options, PMIB_IPFORWARD_ROW2 best,
                                  SOCKADDR_INET *best_source)
{
    if (!destination || !best || !best_source)
        return ERROR_INVALID_PARAMETER;
    DWORD found;
    DWORD error = GetBestInterfaceEx((const struct sockaddr *)destination, &found);
    if (error)
        return error;
    memset(best, 0, sizeof(*best));
    memset(best_source, 0, sizeof(*best_source));
    best->InterfaceLuid.Info.IfType = IF_TYPE_SOFTWARE_LOOPBACK;
    best->InterfaceIndex = found;
    best->ValidLifetime = best->PreferredLifetime = 0xFFFFFFFF;
    best->Metric = 256;
    best->Protocol = MIB_IPPROTO_LOCAL;
    best->Loopback = TRUE;
    best->Immortal = TRUE;
    best->Origin = NlroWellKnown;
    if (destination->si_family == AF_INET) {
        best->DestinationPrefix.Prefix.Ipv4.sin_family = AF_INET;
        ((BYTE *)&best->DestinationPrefix.Prefix.Ipv4.sin_addr)[0] = 127;
        best->DestinationPrefix.PrefixLength = 8;
        best->NextHop.si_family = AF_INET;
        best_source->Ipv4.sin_family = AF_INET;
        ((BYTE *)&best_source->Ipv4.sin_addr)[0] = 127;
        ((BYTE *)&best_source->Ipv4.sin_addr)[3] = 1;
    } else {
        best->DestinationPrefix.Prefix.Ipv6.sin6_family = AF_INET6;
        ((BYTE *)&best->DestinationPrefix.Prefix.Ipv6.sin6_addr)[15] = 1;
        best->DestinationPrefix.PrefixLength = 128;
        best->NextHop.si_family = AF_INET6;
        best_source->Ipv6.sin6_family = AF_INET6;
        ((BYTE *)&best_source->Ipv6.sin6_addr)[15] = 1;
    }
    return NO_ERROR;
}
