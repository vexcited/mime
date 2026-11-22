#ifndef MIME_BTHSERV_PRIVATE_H
#define MIME_BTHSERV_PRIVATE_H

#include <mime/win32/winbase.h>
#include <mime/win32/winsvc.h>
#include <mime/win32/rpc.h>

extern RPC_IF_HANDLE BthServ_s_ifspec;
extern RPC_IF_HANDLE BthGatt_s_ifspec;

void BthServInitClients(void);

#endif
