#ifndef MIME_DHCPCORE_PRIVATE_H
#define MIME_DHCPCORE_PRIVATE_H

#include <mime/win32/winbase.h>
#include <mime/win32/winsvc.h>
#include <mime/win32/rpc.h>

extern RPC_IF_HANDLE Dhcpv6_s_ifspec;
extern int (*Dhcpv6_hook)(PRPC_MESSAGE);

void Dhcpv6Init(void);

#endif
