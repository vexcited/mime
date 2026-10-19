/*
 * com proxies and stubs: the objects that stand for an interface in the client's process, and for an object in the server's.
 */
#include "ndr_proc.h"
#include <mime/win32/rpcproxy.h>

const void *NdrpOleHooks(void)
{
    return nullptr;
}

BOOL NdrpProxyOf(void *interface_pointer, NDR_PROXY *proxy)
{
    return FALSE;
}

void NdrpProxyBegin(void *interface_pointer, PRPC_MESSAGE msg, PMIDL_STUB_MESSAGE m, PMIDL_STUB_DESC desc, ULONG proc_num)
{
    NDR_RAISE(RPC_E_UNEXPECTED);
}

void NdrpProxyEnd(PMIDL_STUB_MESSAGE m)
{
}

void NdrpChannelGetBuffer(PMIDL_STUB_MESSAGE m)
{
    NDR_RAISE(RPC_E_UNEXPECTED);
}

void NdrpChannelSendReceive(PMIDL_STUB_MESSAGE m)
{
    NDR_RAISE(RPC_E_UNEXPECTED);
}

void NdrpChannelFreeBuffer(PMIDL_STUB_MESSAGE m)
{
}

RPCRTAPI void RPC_ENTRY NdrStubGetBuffer(IRpcStubBuffer *This, IRpcChannelBuffer *channel, PMIDL_STUB_MESSAGE m)
{
    NDR_RAISE(RPC_E_UNEXPECTED);
}
