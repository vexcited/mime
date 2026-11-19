#ifndef MIME_RPCSS_H
#define MIME_RPCSS_H

#include <mime/win32/winbase.h>
#include <mime/win32/winsvc.h>
#include <mime/win32/rpc.h>

WINBASEAPI BOOLEAN WINAPI SystemFunction036(PVOID buffer, ULONG length);

/* an hstring as rpcss keeps one: the bytes of its characters. */
struct RPCSS_HSTRING_DATA {
    ULONG Bytes;
    BYTE Data[];
};

/* main.c */
void RsLog(const char *format, ...);
void *RsAlloc(SIZE_T size);
void *RsAllocZero(SIZE_T size);
void RsFree(void *p);
WCHAR *RsDupString(const WCHAR *s);
UINT64 RsNewId(void);

/* resolver.c */
DWORD RsStartResolver(void);
RPC_STATUS RsRegisterInterface(RPC_IF_HANDLE spec);
DWORD RsCallerOf(handle_t binding, void *process); /* the pid of the caller when it holds that context, else 0. */
ULONG RsNextThreadId(void);

/* scm.c */

/* a class a local server registered with CoRegisterClassObject: its class factory is the ipid of that oxid. */
typedef struct RS_CLASS {
    GUID Clsid;
    UINT64 Oxid;
    GUID Ipid;
    ULONG Flags;
    DWORD Pid;
    ULONG Key;
} RS_CLASS;

RPC_STATUS RsStartScm(void);
void RsProcessGone(DWORD pid);
BOOL RsScmFindClass(const GUID *clsid, RS_CLASS *out);

/* activate.c */
BOOL RsActivationRequest(const BYTE *objref, ULONG size, GUID *clsid, ULONG *clsctx, ULONG *flags);
BOOL RsClassHasLocalServer(const GUID *clsid);

/* rot.c */
RPC_STATUS RsStartRot(void);
void RsRotProcessGone(DWORD pid);

#endif
