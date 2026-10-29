/*
 * the asynchronous call functions and the authorization contexts of clients. calls are made one at a time, so there
 * is no asynchronous call to complete or cancel, and no client of this server is authenticated.
 */
#include "rpcrt4_private.h"
#include <mime/win32/rpcndr.h>

#define RPC_ASYNC_SIGNATURE 0x43594E41u

static RPC_STATUS async_handle(PRPC_ASYNC_STATE state)
{
    if (!state || state->Size != sizeof(*state) || state->Signature != RPC_ASYNC_SIGNATURE)
        return RPC_S_INVALID_ASYNC_HANDLE;
    /* a handle that was initialized and never began a call. */
    return RPC_S_INVALID_ASYNC_CALL;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcAsyncCompleteCall(PRPC_ASYNC_STATE state, void *reply)
{
    return async_handle(state);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcAsyncCancelCall(PRPC_ASYNC_STATE state, BOOL abortive)
{
    return async_handle(state);
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcGetAuthorizationContextForClient(RPC_BINDING_HANDLE binding, BOOL impersonate,
                                                                  PVOID reserved1, PLARGE_INTEGER expiration,
                                                                  LUID reserved2, DWORD reserved3, PVOID reserved4,
                                                                  PVOID *context)
{
    if (!context)
        return ERROR_INVALID_PARAMETER;
    *context = nullptr;
    return binding ? RPC_S_BINDING_HAS_NO_AUTH : RPC_S_INVALID_BINDING;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcFreeAuthorizationContext(PVOID *context)
{
    if (!context)
        return ERROR_INVALID_PARAMETER;
    *context = nullptr;
    return RPC_S_OK;
}

/* the stubs of asynchronous procedures are reached only by an asynchronous call, which cannot be made. */
RPCRTAPI void RPC_ENTRY NdrAsyncServerCall(PRPC_MESSAGE message)
{
    RpcRaiseException(RPC_S_CANNOT_SUPPORT);
}

#ifdef _WIN64
RPCRTAPI CLIENT_CALL_RETURN RPC_VAR_ENTRY Ndr64AsyncClientCall(MIDL_STUBLESS_PROXY_INFO *info, ULONG proc, void *return_value, ...)
{
    RpcRaiseException(RPC_S_CANNOT_SUPPORT);
}

RPCRTAPI void RPC_ENTRY Ndr64AsyncServerCallAll(PRPC_MESSAGE message)
{
    RpcRaiseException(RPC_S_CANNOT_SUPPORT);
}
#endif
