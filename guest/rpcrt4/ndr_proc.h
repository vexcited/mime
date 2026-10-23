/* procedure descriptions of the ndr interpreter, shared by clients and servers. */
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

/* calls a server manager routine or an object stub with a built stack frame. */
ULONGLONG NdrpInvokeEx(void *function, const ULONG_PTR *slots, ULONG count, ULONG fp_kind, ULONGLONG *fp_value);

static inline ULONG_PTR NdrpInvoke(void *function, const ULONG_PTR *slots, ULONG count)
{
    return (ULONG_PTR)NdrpInvokeEx(function, slots, count, 0, nullptr);
}

/* the ndr syntax among the transfer syntaxes of a dual syntax stub, or null. */
const MIDL_SYNTAX_INFO *NdrpOldSyntax(const MIDL_SYNTAX_INFO *list, ULONG_PTR count);

/* shared by the rpc runtime stubs and the stubless proxy thunks. */
CLIENT_CALL_RETURN NdrpClientCall(PMIDL_STUB_DESC desc, PFORMAT_STRING format, uchar *args, BOOL promoted_floats);

#endif
