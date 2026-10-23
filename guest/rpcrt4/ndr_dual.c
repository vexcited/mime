/*
 * entry points of midl stubs carrying both ndr and ndr64 format strings, which call NdrClientCall3 whichever syntax was
 * negotiated. this runtime only negotiates ndr, so these find its description and run it like NdrClientCall2.
 */
#include "ndr_proc.h"
#include <stdarg.h>

static const GUID g_ndr20_id = {0x8A885D04, 0x1CEB, 0x11C9, {0x9F, 0xE8, 0x08, 0x00, 0x2B, 0x10, 0x48, 0x60}};

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
    /* the return value is the call's result, the stub's return_value location is not written. */
    return r;
}
#else
/* on x86 the procedure's arguments follow the call's three on the stack, unwidened. */
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
/* servers carrying both syntaxes dispatch to these, com stubs to the next. */
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
