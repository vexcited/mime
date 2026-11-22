#ifndef MIME_NSISVC_PRIVATE_H
#define MIME_NSISVC_PRIVATE_H

#include <mime/win32/winbase.h>
#include <mime/win32/winsvc.h>
#include <mime/win32/rpc.h>
#include <mime/nsi.h>

extern RPC_IF_HANDLE nsirpc_s_ifspec;

/* rpc.c: the device and the tables watched for the registered notifications. */
DWORD NsiSvcStartNotifications(void);
void NsiSvcStopNotifications(void);

#endif
