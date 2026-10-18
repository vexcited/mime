/*
 * rpcrt4 entry point.
 */
#include "rpcrt4_private.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

#define RPC_S_NO_CALL_ACTIVE 1725L

/* mime has no rpc server runtime, so no call is ever being served on this thread. */
RPCRTAPI RPC_STATUS RPC_ENTRY I_RpcOpenClientProcess(void *binding, ULONG access, void **process)
{
    if (process)
        *process = nullptr;
    return RPC_S_NO_CALL_ACTIVE;
}
