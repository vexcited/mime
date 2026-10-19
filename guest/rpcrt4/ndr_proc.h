/*
 * the procedure descriptions of the ndr interpreter, shared by the calls of clients and servers.
 */
#ifndef MIME_RPCRT4_NDR_PROC_H
#define MIME_RPCRT4_NDR_PROC_H

#include "ndr.h"
#include <mime/win32/rpcproxy.h>

typedef struct {
    PFORMAT_STRING Format;
    PFORMAT_STRING Types;
    PFORMAT_STRING HandleDesc;
    PFORMAT_STRING Oif;
    PFORMAT_STRING Ext;
    PFORMAT_STRING Params;
    uchar *Args;
    ULONG RpcFlags;
    ULONG ProcNum;
    ULONG StackSize;
    ULONG ClientSize;
    ULONG ServerSize;
    ULONG NumParams;
    ULONG CorrIncrement;
    ULONG FloatMask;
    ULONG NotifyIndex;
    uchar HandleType;
    uchar OiFlags;
    uchar Oi2Flags;
    uchar Flags2;
} NDR_PROC;

typedef struct {
    ULONG Attrs;
    ULONG Offset;
    ULONG TypeOffset;
    uchar BaseType;
} NDR_PARAM;

void NdrpParseProc(PFORMAT_STRING f, NDR_PROC *p);
void NdrpInitStubMessage(PMIDL_STUB_MESSAGE m, PRPC_MESSAGE msg, PMIDL_STUB_DESC desc, BOOL client, const NDR_PROC *p);

/* the stack frame calls: the manager routine of a server and the stub of an object. */
ULONG_PTR NdrpInvoke(void *function, const ULONG_PTR *slots, ULONG count);

/* the parts of com that the interpreter reaches. */
typedef struct {
    void *This;
    struct IRpcChannelBuffer *Channel;
    const IID *Iid;
} NDR_PROXY;

BOOL NdrpProxyOf(void *interface_pointer, NDR_PROXY *proxy);

#endif
