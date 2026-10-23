/*
 * context handles. a server gives a client a twenty byte handle for state it keeps. the client holds it as an opaque
 * pointer that also names the server, and passes it back to calls using that state. the server side lives in
 * rpc_server.c, the client side and marshaling are here.
 */
#include "ndr_proc.h"

#define CCTX_MAGIC 0xFEDCBA98u

typedef struct CCTX {
    BINDING *Binding;
    ULONG Magic;
    UCHAR Wire[20];
} CCTX;

static BOOL wire_is_null(const UCHAR *wire)
{
    for (int i = 0; i < 20; i++)
        if (wire[i])
            return FALSE;
    return TRUE;
}

RPCRTAPI RPC_BINDING_HANDLE RPC_ENTRY NDRCContextBinding(NDR_CCONTEXT context)
{
    CCTX *c = context;
    if (!c || c->Magic != CCTX_MAGIC)
        NDR_RAISE(ERROR_INVALID_HANDLE);
    return c->Binding;
}

RPCRTAPI void RPC_ENTRY NDRCContextMarshall(NDR_CCONTEXT context, void *buffer)
{
    CCTX *c = context;
    if (c) {
        if (c->Magic != CCTX_MAGIC)
            NDR_RAISE(ERROR_INVALID_HANDLE);
        memcpy(buffer, c->Wire, 20);
    } else {
        memset(buffer, 0, 20);
    }
}

/* a null handle on the wire closes the context, a new one creates it and records the server it came from. */
RPCRTAPI void RPC_ENTRY NDRCContextUnmarshall(NDR_CCONTEXT *context, RPC_BINDING_HANDLE binding, void *buffer, ULONG drep)
{
    CCTX *c = *context;
    if (wire_is_null(buffer)) {
        if (c) {
            if (c->Magic == CCTX_MAGIC) {
                RpcpBindingFree(c->Binding);
                c->Magic = 0;
                HeapFree(GetProcessHeap(), 0, c);
            }
            *context = nullptr;
        }
        return;
    }
    if (!c) {
        BINDING *b = RpcpBindingLookup(binding);
        if (!b)
            NDR_RAISE(RPC_S_INVALID_BINDING);
        c = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*c));
        if (!c)
            NDR_RAISE(RPC_S_OUT_OF_MEMORY);
        c->Binding = RpcpBindingCopy(b);
        if (!c->Binding) {
            HeapFree(GetProcessHeap(), 0, c);
            NDR_RAISE(RPC_S_OUT_OF_MEMORY);
        }
        c->Magic = CCTX_MAGIC;
        *context = c;
    }
    memcpy(c->Wire, buffer, 20);
}

RPCRTAPI void RPC_ENTRY RpcSsDestroyClientContext(void **context)
{
    CCTX *c = context ? *context : nullptr;
    if (c && c->Magic == CCTX_MAGIC) {
        RpcpBindingFree(c->Binding);
        c->Magic = 0;
        HeapFree(GetProcessHeap(), 0, c);
    }
    if (context)
        *context = nullptr;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcSsGetContextBinding(void *context, RPC_BINDING_HANDLE *binding)
{
    CCTX *c = context;
    if (!c || c->Magic != CCTX_MAGIC)
        return ERROR_INVALID_HANDLE;
    *binding = c->Binding;
    return RPC_S_OK;
}

RPCRTAPI void RPC_ENTRY NdrClientContextMarshall(PMIDL_STUB_MESSAGE m, NDR_CCONTEXT context, int check)
{
    if (check && !context)
        NDR_RAISE(RPC_X_SS_IN_NULL_CONTEXT);
    m->Buffer = (uchar *)(((ULONG_PTR)m->Buffer + 3) & ~(ULONG_PTR)3);
    NDRCContextMarshall(context, m->Buffer);
    m->Buffer += 20;
}

RPCRTAPI void RPC_ENTRY NdrClientContextUnmarshall(PMIDL_STUB_MESSAGE m, NDR_CCONTEXT *context, RPC_BINDING_HANDLE binding)
{
    m->Buffer = (uchar *)(((ULONG_PTR)m->Buffer + 3) & ~(ULONG_PTR)3);
    if (m->BufferEnd && m->Buffer + 20 > m->BufferEnd)
        NDR_RAISE(RPC_X_BAD_STUB_DATA);
    NDRCContextUnmarshall(context, binding, m->Buffer, m->RpcMsg ? m->RpcMsg->DataRepresentation : 0x10);
    m->Buffer += 20;
}

RPCRTAPI void RPC_ENTRY NdrContextHandleSize(PMIDL_STUB_MESSAGE m, unsigned char *memory, PFORMAT_STRING format)
{
    m->BufferLength = ((m->BufferLength + 3) & ~3u) + 20;
}

RPCRTAPI NDR_SCONTEXT RPC_ENTRY NdrContextHandleInitialize(PMIDL_STUB_MESSAGE m, PFORMAT_STRING format)
{
    return NDRSContextUnmarshall2(m->RpcMsg ? m->RpcMsg->Handle : nullptr, nullptr, 0x10, nullptr, 0);
}

RPCRTAPI NDR_SCONTEXT RPC_ENTRY NdrServerContextNewUnmarshall(PMIDL_STUB_MESSAGE m, PFORMAT_STRING format)
{
    m->Buffer = (uchar *)(((ULONG_PTR)m->Buffer + 3) & ~(ULONG_PTR)3);
    if (m->BufferEnd && m->Buffer + 20 > m->BufferEnd)
        NDR_RAISE(RPC_X_BAD_STUB_DATA);
    ULONG flags = 0;
    if (format[1] & NDR_CONTEXT_HANDLE_NOSERIALIZE)
        flags |= RPC_CONTEXT_HANDLE_DONT_SERIALIZE;
    else if (format[1] & NDR_CONTEXT_HANDLE_SERIALIZE)
        flags |= RPC_CONTEXT_HANDLE_SERIALIZE;
    NDR_SCONTEXT c = NDRSContextUnmarshall2(m->RpcMsg->Handle, m->Buffer, m->RpcMsg->DataRepresentation, nullptr, flags);
    if (!c)
        NDR_RAISE(ERROR_INVALID_HANDLE);
    m->Buffer += 20;
    return c;
}

RPCRTAPI NDR_SCONTEXT RPC_ENTRY NdrServerContextUnmarshall(PMIDL_STUB_MESSAGE m)
{
    static const unsigned char format[4] = {FC_BIND_CONTEXT, 0, 0, 0};
    return NdrServerContextNewUnmarshall(m, format);
}

RPCRTAPI void RPC_ENTRY NdrServerContextNewMarshall(PMIDL_STUB_MESSAGE m, NDR_SCONTEXT context, NDR_RUNDOWN rundown,
                                                    PFORMAT_STRING format)
{
    m->Buffer = (uchar *)(((ULONG_PTR)m->Buffer + 3) & ~(ULONG_PTR)3);
    ULONG flags = 0;
    if (format[1] & NDR_CONTEXT_HANDLE_NOSERIALIZE)
        flags |= RPC_CONTEXT_HANDLE_DONT_SERIALIZE;
    else if (format[1] & NDR_CONTEXT_HANDLE_SERIALIZE)
        flags |= RPC_CONTEXT_HANDLE_SERIALIZE;
    NDRSContextMarshall2(m->RpcMsg->Handle, context, m->Buffer, rundown, nullptr, flags);
    m->Buffer += 20;
}

RPCRTAPI void RPC_ENTRY NdrServerContextMarshall(PMIDL_STUB_MESSAGE m, NDR_SCONTEXT context, NDR_RUNDOWN rundown)
{
    static const unsigned char format[4] = {FC_BIND_CONTEXT, 0, 0, 0};
    NdrServerContextNewMarshall(m, context, rundown, format);
}

/* what the server keeps of a call's context handles between its [in] and [out] parts. */
typedef struct CTXREC {
    struct CTXREC *Next;
    uchar **Cell;
    NDR_SCONTEXT Context;
} CTXREC;

void NdrpContextRecordsFree(PMIDL_STUB_MESSAGE m)
{
    CTXREC *r = m->ContextHandleHash;
    while (r) {
        CTXREC *next = r->Next;
        HeapFree(GetProcessHeap(), 0, r);
        r = next;
    }
    m->ContextHandleHash = nullptr;
}

static CTXREC *record_find(PMIDL_STUB_MESSAGE m, uchar **cell)
{
    for (CTXREC *r = m->ContextHandleHash; r; r = r->Next)
        if (r->Cell == cell)
            return r;
    return nullptr;
}

static void record_add(PMIDL_STUB_MESSAGE m, uchar **cell, NDR_SCONTEXT context)
{
    CTXREC *r = HeapAlloc(GetProcessHeap(), 0, sizeof(*r));
    if (!r)
        NDR_RAISE(RPC_S_OUT_OF_MEMORY);
    r->Cell = cell;
    r->Context = context;
    r->Next = m->ContextHandleHash;
    m->ContextHandleHash = r;
}

void NdrpContextHandle(NDRC *c, uchar **pm, PFORMAT_STRING f)
{
    PMIDL_STUB_MESSAGE m = c->M;
    uchar flags = f[1];
    uchar **cell = (flags & HANDLE_PARAM_IS_VIA_PTR) ? (uchar **)*pm : pm;
    if (c->Pass == NDR_SIZE || c->Pass == NDR_MEMSIZE || c->Pass == NDR_FREE) {
        NdrpAlign(c, 3);
        NdrpTake(c, 20);
        return;
    }
    if (m->IsClient) {
        if (c->Pass == NDR_MARSHAL) {
            NdrClientContextMarshall(m, (NDR_CCONTEXT)*cell, (flags & NDR_CONTEXT_HANDLE_CANNOT_BE_NULL) != 0);
        } else {
            if (!(flags & HANDLE_PARAM_IS_IN))
                *cell = nullptr;
            NdrClientContextUnmarshall(m, (NDR_CCONTEXT *)cell, m->RpcMsg->Handle);
        }
        return;
    }
    if (c->Pass == NDR_UNMARSHAL) {
        NDR_SCONTEXT sc = NdrServerContextNewUnmarshall(m, f);
        if (!sc && (flags & NDR_CONTEXT_HANDLE_CANNOT_BE_NULL))
            NDR_RAISE(RPC_X_SS_IN_NULL_CONTEXT);
        record_add(m, cell, sc);
        *cell = sc ? (uchar *)sc->userContext : nullptr;
    } else {
        CTXREC *r = record_find(m, cell);
        NDR_SCONTEXT sc = r ? r->Context : NdrContextHandleInitialize(m, f);
        sc->userContext = *cell;
        NdrServerContextNewMarshall(m, sc, m->StubDesc->apfnNdrRundownRoutines ? m->StubDesc->apfnNdrRundownRoutines[f[2]] : nullptr, f);
    }
}
