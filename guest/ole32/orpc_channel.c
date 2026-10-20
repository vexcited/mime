/*
 * the channels: the calls of interface proxies sent to an object of another process, and the calls of the rpc runtime
 * dispatched to the stubs of this one.
 */
#include "orpc.h"

#define RPC_E_VERSION_MISMATCH_HR ((HRESULT)0x80010110L)
#define RPC_E_INVALID_DATAPACKET_HR ((HRESULT)0x80010009L)

static const GUID g_ndr20 = {0x8A885D04, 0x1CEB, 0x11C9, {0x9F, 0xE8, 0x08, 0x00, 0x2B, 0x10, 0x48, 0x60}};

#pragma pack(push, 1)
typedef struct ORPCTHIS {
    USHORT Major, Minor;
    ULONG Flags;
    ULONG Reserved;
    GUID Cid;
    ULONG Extensions;
} ORPCTHIS;

typedef struct ORPCTHAT {
    ULONG Flags;
    ULONG Extensions;
} ORPCTHAT;
#pragma pack(pop)

/* what an rpc status of a failed call means to a caller of an object. */
static HRESULT map_status(RPC_STATUS status)
{
    if (status < 0)
        return (HRESULT)status;
    if (status == RPC_S_CALL_CANCELLED)
        return RPC_E_CALL_CANCELED;
    return HRESULT_FROM_WIN32((ULONG)status);
}

/* ------------------------------------------------------------------ the calls */

typedef struct OCALL {
    RPC_MESSAGE Rpc;
    RPC_CLIENT_INTERFACE Interface;
    GUID Ipid;
} OCALL;

static OCALL *call_begin(OXID_ENTRY *server, const GUID *ipid, REFIID iid, ULONG method, ULONG body_size, BYTE **body)
{
    OCALL *call = OleAllocZero(sizeof(*call));
    if (!call)
        return nullptr;
    call->Interface.Length = sizeof(call->Interface);
    call->Interface.InterfaceId.SyntaxGUID = *iid;
    call->Interface.TransferSyntax.SyntaxGUID = g_ndr20;
    call->Interface.TransferSyntax.SyntaxVersion = (RPC_VERSION){2, 0};
    call->Ipid = *ipid;
    call->Rpc.Handle = server->Binding;
    call->Rpc.RpcInterfaceInformation = &call->Interface;
    call->Rpc.ProcNum = method;
    call->Rpc.DataRepresentation = 0x10;
    call->Rpc.BufferLength = ORPC_THIS_SIZE + body_size;
    if (I_RpcGetBufferWithObject(&call->Rpc, &call->Ipid)) {
        OleFree(call);
        return nullptr;
    }
    *body = (BYTE *)call->Rpc.Buffer + ORPC_THIS_SIZE;
    return call;
}

/* sends the request and waits for the reply, serving the calls to the apartment of the thread meanwhile. */
static HRESULT call_send(OCALL *call, ULONG body_size, BYTE **reply, ULONG *reply_size)
{
    ORPCTHIS *head = call->Rpc.Buffer;
    memset(head, 0, sizeof(*head));
    head->Major = 5;
    head->Minor = 7;
    CoGetCurrentLogicalThreadId(&head->Cid);
    call->Rpc.BufferLength = ORPC_THIS_SIZE + body_size;

    OLE_THREAD *t = OleThreadState(FALSE);
    void *previous[2] = {};
    BOOL waits = t && t->ComInits && t->Sta && t->Apartment;
    if (waits)
        I_RpcMimeSetWaitFunction(OrpcWait, t->Apartment, previous);
    RPC_STATUS status = I_RpcSendReceive(&call->Rpc);
    if (waits)
        I_RpcMimeSetWaitFunction((DWORD(WINAPI *)(DWORD, const HANDLE *, DWORD, void *))previous[0], previous[1], nullptr);
    if (status) {
        /* the request buffer is gone with the call. */
        call->Rpc.Buffer = nullptr;
        return map_status(status);
    }
    if (call->Rpc.BufferLength < ORPC_THAT_SIZE) {
        I_RpcFreeBuffer(&call->Rpc);
        return RPC_E_INVALID_DATAPACKET_HR;
    }
    *reply = (BYTE *)call->Rpc.Buffer + ORPC_THAT_SIZE;
    *reply_size = call->Rpc.BufferLength - ORPC_THAT_SIZE;
    return S_OK;
}

HRESULT OrpcRemCall(OXID_ENTRY *server, ULONG method, const BYTE *in, ULONG in_size, BYTE **out, ULONG *out_size)
{
    GUID ipid;
    BYTE *body;
    OrpcRemUnknownIpid(server->Oxid, &ipid);
    *out = nullptr;
    *out_size = 0;
    OCALL *call = call_begin(server, &ipid, &IID_IRemUnknown, method, in_size, &body);
    if (!call)
        return RPC_E_SERVER_DIED_DNE_HR;
    memcpy(body, in, in_size);
    BYTE *reply;
    ULONG reply_size;
    HRESULT hr = call_send(call, in_size, &reply, &reply_size);
    if (SUCCEEDED(hr)) {
        if (reply_size) {
            *out = OleAlloc(reply_size);
            if (*out) {
                memcpy(*out, reply, reply_size);
                *out_size = reply_size;
            } else {
                hr = E_OUTOFMEMORY;
            }
        }
        I_RpcFreeBuffer(&call->Rpc);
    }
    OleFree(call);
    return hr;
}

/* ------------------------------------------------------------------ the channel of an interface proxy */

typedef struct CHANNEL {
    IRpcChannelBuffer2 Interface;
    LONG Refs;
    OXID_ENTRY *Server;
    GUID Ipid;
    IID Iid;
} CHANNEL;

static HRESULT STDMETHODCALLTYPE ch_query(IRpcChannelBuffer2 *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IRpcChannelBuffer) ||
        IsEqualIID(riid, &IID_IRpcChannelBuffer2)) {
        *object = This;
        InterlockedIncrement(&((CHANNEL *)This)->Refs);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE ch_add_ref(IRpcChannelBuffer2 *This)
{
    return (ULONG)InterlockedIncrement(&((CHANNEL *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE ch_release(IRpcChannelBuffer2 *This)
{
    LONG refs = InterlockedDecrement(&((CHANNEL *)This)->Refs);
    if (!refs)
        OleFree(This);
    return (ULONG)refs;
}

/* the state of a call lives in the message the proxy owns, in the slot the runtime of rpc keeps for itself. */
#define CALL_OF(message) (*(OCALL **)&(message)->reserved2[0])

static HRESULT STDMETHODCALLTYPE ch_get_buffer(IRpcChannelBuffer2 *This, RPCOLEMESSAGE *message, REFIID iid)
{
    CHANNEL *c = (CHANNEL *)This;
    BYTE *body;
    OCALL *call = call_begin(c->Server, &c->Ipid, &c->Iid, message->iMethod, message->cbBuffer, &body);
    if (!call)
        return RPC_E_SERVER_DIED_DNE_HR;
    CALL_OF(message) = call;
    message->pvBuffer = body;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ch_send_receive(IRpcChannelBuffer2 *This, RPCOLEMESSAGE *message, ULONG *status)
{
    OCALL *call = CALL_OF(message);
    BYTE *reply;
    ULONG reply_size;
    *status = 0;
    if (!call)
        return E_UNEXPECTED;
    HRESULT hr = call_send(call, message->cbBuffer, &reply, &reply_size);
    if (FAILED(hr)) {
        OleFree(call);
        CALL_OF(message) = nullptr;
        message->pvBuffer = nullptr;
        return hr;
    }
    message->pvBuffer = reply;
    message->cbBuffer = reply_size;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ch_free_buffer(IRpcChannelBuffer2 *This, RPCOLEMESSAGE *message)
{
    OCALL *call = CALL_OF(message);
    if (!call)
        return S_OK;
    if (call->Rpc.Buffer)
        I_RpcFreeBuffer(&call->Rpc);
    OleFree(call);
    CALL_OF(message) = nullptr;
    message->pvBuffer = nullptr;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ch_get_dest_ctx(IRpcChannelBuffer2 *This, DWORD *context, void **data)
{
    *context = MSHCTX_LOCAL;
    *data = nullptr;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ch_is_connected(IRpcChannelBuffer2 *This)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ch_get_protocol_version(IRpcChannelBuffer2 *This, DWORD *version)
{
    *version = (5 << 16) | 7;
    return S_OK;
}

static const IRpcChannelBuffer2Vtbl g_channel_vtbl = {ch_query,        ch_add_ref,       ch_release,
                                                      ch_get_buffer,   ch_send_receive,  ch_free_buffer,
                                                      ch_get_dest_ctx, ch_is_connected,  ch_get_protocol_version};

HRESULT OrpcChannelCreate(OXID_ENTRY *server, const GUID *ipid, REFIID iid, IRpcChannelBuffer **channel)
{
    CHANNEL *c = OleAllocZero(sizeof(*c));
    if (!c)
        return E_OUTOFMEMORY;
    c->Interface.lpVtbl = &g_channel_vtbl;
    c->Refs = 1;
    c->Server = server;
    c->Ipid = *ipid;
    c->Iid = *iid;
    *channel = (IRpcChannelBuffer *)c;
    return S_OK;
}

/* ------------------------------------------------------------------ the server side of a call */

typedef struct SCHANNEL {
    IRpcChannelBuffer2 Interface;
    LONG Refs;
    PRPC_MESSAGE Rpc;
    BOOL Allocated;
} SCHANNEL;

typedef struct SERVED {
    IRpcStubBuffer *Stub;
    RPCOLEMESSAGE Message;
    SCHANNEL Channel;
    HRESULT Hr;
} SERVED;

static ULONG STDMETHODCALLTYPE sch_add_ref(IRpcChannelBuffer2 *This)
{
    return (ULONG)InterlockedIncrement(&((SCHANNEL *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE sch_release(IRpcChannelBuffer2 *This)
{
    return (ULONG)InterlockedDecrement(&((SCHANNEL *)This)->Refs);
}

static HRESULT STDMETHODCALLTYPE sch_get_buffer(IRpcChannelBuffer2 *This, RPCOLEMESSAGE *message, REFIID iid)
{
    SCHANNEL *c = (SCHANNEL *)This;
    c->Rpc->BufferLength = ORPC_THAT_SIZE + message->cbBuffer;
    if (I_RpcGetBuffer(c->Rpc))
        return E_OUTOFMEMORY;
    c->Allocated = TRUE;
    ORPCTHAT *that = c->Rpc->Buffer;
    that->Flags = 0;
    that->Extensions = 0;
    message->pvBuffer = (BYTE *)c->Rpc->Buffer + ORPC_THAT_SIZE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sch_send_receive(IRpcChannelBuffer2 *This, RPCOLEMESSAGE *message, ULONG *status)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE sch_free_buffer(IRpcChannelBuffer2 *This, RPCOLEMESSAGE *message)
{
    return S_OK;
}

static const IRpcChannelBuffer2Vtbl g_server_channel_vtbl = {ch_query,         sch_add_ref,    sch_release,
                                                             sch_get_buffer,   sch_send_receive, sch_free_buffer,
                                                             ch_get_dest_ctx,  ch_is_connected, ch_get_protocol_version};

static void run_call(void *context)
{
    SERVED *s = context;
    OLE_THREAD *t = OleThreadState(TRUE);
    void *saved = t ? t->OrpcCall : nullptr;
    if (t)
        t->OrpcCall = s;
    __try {
        s->Hr = s->Stub->lpVtbl->Invoke(s->Stub, &s->Message, (IRpcChannelBuffer *)&s->Channel);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        s->Hr = RPC_E_SERVERFAULT;
    }
    if (t)
        t->OrpcCall = saved;
}

/* an rpc call that carries a call to an object: found by its ipid, run in the apartment of the object. */
void WINAPI OrpcDispatch(PRPC_MESSAGE rpc)
{
    UUID ipid;
    /* the threads of the rpc runtime are threads of the multithreaded apartment, as com has them. */
    OLE_THREAD *thread = OleThreadState(TRUE);
    if (thread && !thread->ComInits)
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (RpcBindingInqObject(rpc->Handle, &ipid))
        RpcRaiseException((ULONG)RPC_E_INVALID_IPID_HR);
    const RPC_SERVER_INTERFACE *interface_info = rpc->RpcInterfaceInformation;
    const GUID *iid = &interface_info->InterfaceId.SyntaxGUID;
    const ORPCTHIS *head = rpc->Buffer;
    if (rpc->BufferLength < ORPC_THIS_SIZE)
        RpcRaiseException((ULONG)RPC_E_INVALID_DATAPACKET_HR);
    if (head->Major != 5)
        RpcRaiseException((ULONG)RPC_E_VERSION_MISMATCH_HR);

    ULONG64 oxid;
    if (OrpcIsRemUnknownIpid(&ipid, &oxid)) {
        BYTE *out = nullptr;
        ULONG out_size = 0;
        HRESULT hr = OrpcRemUnknownCall(oxid, rpc->ProcNum, (const BYTE *)rpc->Buffer + ORPC_THIS_SIZE,
                                        rpc->BufferLength - ORPC_THIS_SIZE, &out, &out_size);
        if (FAILED(hr))
            RpcRaiseException((ULONG)hr);
        rpc->BufferLength = ORPC_THAT_SIZE + out_size;
        if (I_RpcGetBuffer(rpc)) {
            OleFree(out);
            RpcRaiseException((ULONG)E_OUTOFMEMORY);
        }
        memset(rpc->Buffer, 0, ORPC_THAT_SIZE);
        if (out_size)
            memcpy((BYTE *)rpc->Buffer + ORPC_THAT_SIZE, out, out_size);
        OleFree(out);
        return;
    }

    IPID_TARGET target;
    HRESULT hr = OrpcFindStub(&ipid, &target);
    if (FAILED(hr))
        RpcRaiseException((ULONG)hr);
    if (!IsEqualIID(&target.Iid, iid)) {
        target.Stub->lpVtbl->Release(target.Stub);
        RpcRaiseException((ULONG)RPC_E_INVALID_IPID_HR);
    }
    SERVED served = {};
    served.Stub = target.Stub;
    served.Channel.Interface.lpVtbl = &g_server_channel_vtbl;
    served.Channel.Refs = 1;
    served.Channel.Rpc = rpc;
    served.Message.pvBuffer = (BYTE *)rpc->Buffer + ORPC_THIS_SIZE;
    served.Message.cbBuffer = rpc->BufferLength - ORPC_THIS_SIZE;
    served.Message.iMethod = rpc->ProcNum;
    served.Message.dataRepresentation = rpc->DataRepresentation;
    served.Message.reserved1 = rpc->Handle;
    hr = OrpcRunInApartment(target.Apartment, run_call, &served);
    if (SUCCEEDED(hr))
        hr = served.Hr;
    target.Stub->lpVtbl->Release(target.Stub);
    if (FAILED(hr))
        RpcRaiseException((ULONG)hr);
    if (!served.Channel.Allocated) {
        rpc->BufferLength = ORPC_THAT_SIZE;
        if (I_RpcGetBuffer(rpc))
            RpcRaiseException((ULONG)E_OUTOFMEMORY);
        memset(rpc->Buffer, 0, ORPC_THAT_SIZE);
    } else {
        rpc->BufferLength = ORPC_THAT_SIZE + served.Message.cbBuffer;
    }
}
