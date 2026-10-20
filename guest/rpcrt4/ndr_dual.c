/*
 * the entry points of stubs that midl wrote for two transfer syntaxes. A dual syntax stub carries the format strings of ndr
 * and of ndr64 and calls NdrClientCall3 whichever the binding agreed on; this runtime only negotiates the older syntax, so
 * these find the description of it in the tables and run it as NdrClientCall2 does.
 */
#include "ndr_proc.h"
#include <stdarg.h>

static const GUID g_ndr20_id = {0x8A885D04, 0x1CEB, 0x11C9, {0x9F, 0xE8, 0x08, 0x00, 0x2B, 0x10, 0x48, 0x60}};

/* the description of the older transfer syntax among those of an interface, or null. */
const MIDL_SYNTAX_INFO *NdrpOldSyntax(const MIDL_SYNTAX_INFO *list, ULONG_PTR count)
{
    for (ULONG_PTR i = 0; list && i < count; i++)
        if (!memcmp(&list[i].TransferSyntax.SyntaxGUID, &g_ndr20_id, sizeof(GUID)) &&
            list[i].TransferSyntax.SyntaxVersion.MajorVersion == 2)
            return &list[i];
    return nullptr;
}

#ifdef _WIN64
RPCRTAPI CLIENT_CALL_RETURN RPC_VAR_ENTRY NdrClientCall3(MIDL_STUBLESS_PROXY_INFO *info, ULONG proc, void *return_value, ...)
{
    va_list ap;
    va_start(ap, return_value);
    uchar *args = (uchar *)ap;
    BOOL promoted = TRUE;
    MIDL_STUB_DESC desc = *info->pStubDesc;
    PFORMAT_STRING procs = info->ProcFormatString;
    const unsigned short *offsets = info->FormatStringOffset;
    if (info->pSyntaxInfo) {
        const MIDL_SYNTAX_INFO *old = NdrpOldSyntax(info->pSyntaxInfo, info->nCount ? info->nCount : 2);
        if (!old)
            RpcRaiseException(RPC_S_UNSUPPORTED_TRANS_SYN);
        procs = old->ProcString;
        offsets = old->FmtStringOffset;
        desc.pFormatTypes = old->TypeString;
        if (old->aUserMarshalQuadruple)
            desc.aUserMarshalQuadruple = (const USER_MARSHAL_ROUTINE_QUADRUPLE *)old->aUserMarshalQuadruple;
    }
    CLIENT_CALL_RETURN r = NdrpClientCall(&desc, procs + offsets[proc], args, promoted);
    va_end(ap);
    /* the return value comes back as the result of the call; where the stub says it keeps it is not written. */
    return r;
}
#else
/*
 * on x86 the arguments of the procedure follow the three of the call on the stack, as the next of its variable arguments,
 * and nothing is widened.
 */
RPCRTAPI CLIENT_CALL_RETURN RPC_VAR_ENTRY NdrClientCall4(MIDL_STUBLESS_PROXY_INFO *info, ULONG proc, void *return_value, ...)
{
    va_list ap;
    va_start(ap, return_value);
    uchar *args = (uchar *)ap;
    MIDL_STUB_DESC desc = *info->pStubDesc;
    PFORMAT_STRING procs = info->ProcFormatString;
    const unsigned short *offsets = info->FormatStringOffset;
    if (info->pSyntaxInfo) {
        const MIDL_SYNTAX_INFO *old = NdrpOldSyntax(info->pSyntaxInfo, info->nCount ? info->nCount : 2);
        if (!old)
            RpcRaiseException(RPC_S_UNSUPPORTED_TRANS_SYN);
        procs = old->ProcString;
        offsets = old->FmtStringOffset;
        desc.pFormatTypes = old->TypeString;
        if (old->aUserMarshalQuadruple)
            desc.aUserMarshalQuadruple = (const USER_MARSHAL_ROUTINE_QUADRUPLE *)old->aUserMarshalQuadruple;
    }
    CLIENT_CALL_RETURN r = NdrpClientCall(&desc, procs + offsets[proc], args, FALSE);
    va_end(ap);
    return r;
}
#endif

#ifdef _WIN64
/* a server that carries both syntaxes dispatches to these, and a com stub that does to the next. */
RPCRTAPI void RPC_ENTRY NdrServerCallAll(PRPC_MESSAGE msg)
{
    NdrServerCall2(msg);
}

RPCRTAPI LONG RPC_ENTRY NdrStubCall3(struct IRpcStubBuffer *stub, struct IRpcChannelBuffer *channel, PRPC_MESSAGE msg,
                                     ULONG *phase)
{
    return NdrStubCall2(stub, channel, msg, phase);
}
#endif
