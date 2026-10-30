#ifndef MIME_WKSSVC_PRIVATE_H
#define MIME_WKSSVC_PRIVATE_H

#include <mime/win32/winbase.h>
#include <mime/win32/winsvc.h>
#include <mime/win32/rpc.h>

extern RPC_IF_HANDLE wkssvc_s_ifspec;
DWORD WsLoadConfiguration(void);

#endif
