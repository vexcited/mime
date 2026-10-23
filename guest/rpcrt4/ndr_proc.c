/*
 * the interpreter of procedure format strings. a procedure is a header: handle, flags, number and stack frame size, then
 * its buffer sizes, then one descriptor per parameter giving its attributes, frame offset and type.
 */
#include "ndr.h"
#include "ndr_proc.h"

void NdrpParseProc(PFORMAT_STRING f, NDR_PROC *p)
{
    memset(p, 0, sizeof(*p));
    p->Format = f;
    p->HandleType = f[0];
    p->OiFlags = f[1];
    PFORMAT_STRING at = f + 2;
    if (p->OiFlags & Oi_HAS_RPCFLAGS) {
        p->RpcFlags = *(const ULONG *)at;
        at += 4;
    }
    p->ProcNum = *(const USHORT *)at;
    p->StackSize = *(const USHORT *)(at + 2);
    at += 4;
    p->HandleDesc = at;
    if (p->HandleType == 0)
        at += at[0] == FC_BIND_PRIMITIVE ? 4 : 6;
    p->Oif = at;
    p->ClientSize = *(const USHORT *)at;
    p->ServerSize = *(const USHORT *)(at + 2);
    p->Oi2Flags = at[4];
    p->NumParams = at[5];
    at += 6;
    if (p->Oi2Flags & OIF_HAS_EXTENSIONS) {
        p->Ext = at;
        p->Flags2 = at[1];
        p->NotifyIndex = *(const USHORT *)(at + 6);
        if (at[0] >= 10)
            p->FloatMask = *(const USHORT *)(at + 8);
        if (p->Flags2 & OIF2_HAS_NEW_CORR_DESC)
            p->CorrIncrement = 2;
        if (p->Flags2 & 0x40)
            p->CorrIncrement = 12;
        at += at[0];
    }
    p->Params = at;
}

PFORMAT_STRING NdrpParamTypeFormat(const NDR_PROC *p, const NDR_PARAM *param)
{
    return p->Types + param->TypeOffset;
}

void NdrpInitStubMessage(PMIDL_STUB_MESSAGE m, PRPC_MESSAGE msg, PMIDL_STUB_DESC desc, BOOL client, const NDR_PROC *p)
{
    memset(m, 0, sizeof(*m));
    m->RpcMsg = msg;
    m->StubDesc = desc;
    m->pfnAllocate = desc->pfnAllocate;
    m->pfnFree = desc->pfnFree;
    m->IsClient = client;
    m->dwDestContext = 2;
    m->fIsOicf = 1;
    if (p) {
        m->CorrDespIncrement = (unsigned char)p->CorrIncrement;
        m->fHasNewCorrDesc = (p->Flags2 & OIF2_HAS_NEW_CORR_DESC) != 0;
        m->fHasExtensions = p->Ext != nullptr;
        m->fHasReturn = (p->Oi2Flags & OIF_HAS_RETURN) != 0;
    }
    if (desc->pMallocFreeStruct) {
        m->pfnAllocate = desc->pMallocFreeStruct->pfnAllocate;
        m->pfnFree = desc->pMallocFreeStruct->pfnFree;
    }
    if (desc->Version > NDR_STUB_VERSION_MAX)
        NDR_RAISE(RPC_X_WRONG_STUB_VERSION);
}

RPCRTAPI unsigned char *RPC_ENTRY NdrGetBuffer(PMIDL_STUB_MESSAGE m, ULONG length, RPC_BINDING_HANDLE handle)
{
    m->RpcMsg->BufferLength = length;
    m->RpcMsg->Handle = handle;
    m->BufferLength = length;
    RPC_STATUS status = I_RpcGetBuffer(m->RpcMsg);
    if (status)
        NDR_RAISE(status);
    m->Buffer = m->BufferStart = m->RpcMsg->Buffer;
    m->BufferEnd = m->BufferStart + length;
    m->fBufferValid = 1;
    return m->Buffer;
}

RPCRTAPI unsigned char *RPC_ENTRY NdrNsGetBuffer(PMIDL_STUB_MESSAGE m, ULONG length, RPC_BINDING_HANDLE handle)
{
    return NdrGetBuffer(m, length, handle);
}

RPCRTAPI unsigned char *RPC_ENTRY NdrSendReceive(PMIDL_STUB_MESSAGE m, unsigned char *buffer_end)
{
    if (!m->fBufferValid)
        NDR_RAISE(RPC_S_INTERNAL_ERROR);
    m->RpcMsg->BufferLength = (ULONG)(buffer_end - (unsigned char *)m->RpcMsg->Buffer);
    m->fBufferValid = 0;
    RPC_STATUS status = I_RpcSendReceive(m->RpcMsg);
    if (status)
        NDR_RAISE(status);
    m->Buffer = m->BufferStart = m->RpcMsg->Buffer;
    m->BufferEnd = m->BufferStart + m->RpcMsg->BufferLength;
    m->fBufferValid = 1;
    return m->Buffer;
}

RPCRTAPI unsigned char *RPC_ENTRY NdrNsSendReceive(PMIDL_STUB_MESSAGE m, unsigned char *buffer_end,
                                                   RPC_BINDING_HANDLE *handle)
{
    return NdrSendReceive(m, buffer_end);
}

RPCRTAPI void RPC_ENTRY NdrFreeBuffer(PMIDL_STUB_MESSAGE m)
{
    if (m->RpcMsg && m->RpcMsg->Buffer) {
        I_RpcFreeBuffer(m->RpcMsg);
        m->RpcMsg->Buffer = nullptr;
    }
    m->fBufferValid = 0;
}

RPCRTAPI void RPC_ENTRY NdrClientInitializeNew(PRPC_MESSAGE rpc_msg, PMIDL_STUB_MESSAGE m, PMIDL_STUB_DESC desc,
                                               unsigned int proc_num)
{
    memset(rpc_msg, 0, sizeof(*rpc_msg));
    rpc_msg->RpcInterfaceInformation = desc->RpcInterfaceInformation;
    rpc_msg->ProcNum = proc_num;
    rpc_msg->DataRepresentation = NDR_LOCAL_DATA_REPRESENTATION;
    NdrpInitStubMessage(m, rpc_msg, desc, TRUE, nullptr);
    m->fIsOicf = 0;
}

RPCRTAPI void RPC_ENTRY NdrClientInitialize(PRPC_MESSAGE rpc_msg, PMIDL_STUB_MESSAGE m, PMIDL_STUB_DESC desc,
                                            unsigned int proc_num)
{
    NdrClientInitializeNew(rpc_msg, m, desc, proc_num);
}

RPCRTAPI unsigned char *RPC_ENTRY NdrServerInitializeNew(PRPC_MESSAGE rpc_msg, PMIDL_STUB_MESSAGE m,
                                                         PMIDL_STUB_DESC desc)
{
    NdrpInitStubMessage(m, rpc_msg, desc, FALSE, nullptr);
    m->fIsOicf = 0;
    m->Buffer = m->BufferStart = rpc_msg->Buffer;
    m->BufferEnd = m->BufferStart + rpc_msg->BufferLength;
    m->fBufferValid = 1;
    return m->Buffer;
}

RPCRTAPI unsigned char *RPC_ENTRY NdrServerInitialize(PRPC_MESSAGE rpc_msg, PMIDL_STUB_MESSAGE m, PMIDL_STUB_DESC desc)
{
    return NdrServerInitializeNew(rpc_msg, m, desc);
}

RPCRTAPI unsigned char *RPC_ENTRY NdrServerInitializeUnmarshall(PMIDL_STUB_MESSAGE m, PMIDL_STUB_DESC desc,
                                                                PRPC_MESSAGE rpc_msg)
{
    return NdrServerInitializeNew(rpc_msg, m, desc);
}

RPCRTAPI void RPC_ENTRY NdrServerInitializeMarshall(PRPC_MESSAGE rpc_msg, PMIDL_STUB_MESSAGE m)
{
    m->RpcMsg = rpc_msg;
}

RPCRTAPI unsigned char *RPC_ENTRY NdrServerInitializePartial(PRPC_MESSAGE rpc_msg, PMIDL_STUB_MESSAGE m,
                                                             PMIDL_STUB_DESC desc, ULONG requested)
{
    return NdrServerInitializeNew(rpc_msg, m, desc);
}

RPCRTAPI void RPC_ENTRY NdrConvert2(PMIDL_STUB_MESSAGE m, PFORMAT_STRING format, LONG number_params)
{
    if (m->RpcMsg && (m->RpcMsg->DataRepresentation & NDR_INT_REP_MASK) != NDR_LITTLE_ENDIAN)
        NDR_RAISE(RPC_S_UNSUPPORTED_TRANS_SYN);
}

RPCRTAPI void RPC_ENTRY NdrConvert(PMIDL_STUB_MESSAGE m, PFORMAT_STRING format)
{
    NdrConvert2(m, format, 0);
}

/*
 * the communication failures a procedure's comm status parameter can receive. the rest go to the fault status, and a
 * status neither parameter can take is raised.
 */
static const LONG g_comm_statuses[] = {
    RPC_S_OUT_OF_RESOURCES, RPC_S_SERVER_UNAVAILABLE, RPC_S_SERVER_TOO_BUSY,  RPC_S_CALL_FAILED_DNE,
    RPC_S_PROTOCOL_ERROR,   RPC_S_UNSUPPORTED_TRANS_SYN, RPC_S_UNSUPPORTED_TYPE, RPC_S_PROCNUM_OUT_OF_RANGE,
    RPC_S_UNKNOWN_IF,       RPC_S_CALL_FAILED,           RPC_S_COMM_FAILURE,    RPC_S_UNKNOWN_MGR_TYPE,
    RPC_S_SERVER_UNAVAILABLE + 0,
};

RPCRTAPI RPC_STATUS RPC_ENTRY NdrMapCommAndFaultStatus(PMIDL_STUB_MESSAGE m, ULONG *comm, ULONG *fault, RPC_STATUS status)
{
    if (!status)
        return 0;
    BOOL is_comm = FALSE;
    for (ULONG i = 0; i < ARRAYSIZE(g_comm_statuses); i++)
        if (g_comm_statuses[i] == status)
            is_comm = TRUE;
    if (is_comm) {
        if (comm) {
            *comm = status;
            return 0;
        }
        return status;
    }
    if (fault) {
        *fault = status;
        return 0;
    }
    return status;
}

/* the HRESULT a proxy call returns when the rpc runtime failed. */
RPCRTAPI HRESULT RPC_ENTRY NdrProxyErrorHandler(DWORD code)
{
    if ((LONG)code < 0 && (code >> 16) == 0x8001)
        return (HRESULT)code;
    if (code == 0xC0000005 || code == RPC_S_ADDRESS_ERROR || code == ERROR_NOACCESS)
        return (HRESULT)RPC_E_SERVERFAULT;
    if (code == RPC_S_SERVER_UNAVAILABLE || code == RPC_S_CALL_FAILED_DNE || code == RPC_S_CALL_FAILED)
        return (HRESULT)RPC_E_DISCONNECTED;
    if (code == RPC_S_CALL_CANCELLED)
        return (HRESULT)RPC_E_CALL_CANCELED;
    if ((LONG)code < 0)
        return (HRESULT)code;
    return (HRESULT)(code <= 0 ? code : ((code & 0xFFFF) | 0x80070000));
}
