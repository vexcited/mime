#ifndef NDRGEN_LAUNCH_H
#define NDRGEN_LAUNCH_H

#include <mime/win32/rpcndr.h>

#define NDRGEN_UNUSED __attribute__((unused))



#ifdef _WIN64
#else
#endif

/* ------------------------------------------------------------------ interface IDcomLaunch */

#ifndef IDCOMLAUNCH_CLIENT_ONLY
extern long WINAPI IDcomLaunch_LaunchServer_s(handle_t hRpc, unsigned short *service, unsigned short *parameters, unsigned short *command, unsigned long client, unsigned long *pid);
#endif
void *WINAPI MIDL_user_allocate(size_t);
void WINAPI MIDL_user_free(void *);

static const unsigned char IDcomLaunch_TypeFormat[] = {
#ifdef _WIN64
    0x00, 0x00, /* 0: pad */
    0x12, 0x08, 0x25, 0x5c, /* 2: pointer */
#else
    0x00, 0x00, /* 0: pad */
    0x12, 0x08, 0x25, 0x5c, /* 2: pointer */
#endif
};

static const unsigned char IDcomLaunch_ProcFormat[] = {
#ifdef _WIN64
    0x00, 0x00, /* 0: pad */
    0x00, 0x40, 0x00, 0x00, 0x38, 0x00, 0x32, 0x00, 0x00, 0x00, 0x07, 0x00, /* 2: IDcomLaunch.LaunchServer */
    0x0f, 0x00, 0x46, 0x06, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x00, 0x08, 0x00, 0x02, 0x00, 0x0b, 0x00, 0x10, 0x00,
    0x02, 0x00, 0x0b, 0x00, 0x18, 0x00, 0x02, 0x00, 0x48, 0x00, 0x20, 0x00,
    0x08, 0x5c, 0x50, 0x21, 0x28, 0x00, 0x08, 0x5c, 0x70, 0x00, 0x30, 0x00,
    0x08, 0x5c,
#else
    0x00, 0x00, /* 0: pad */
    0x00, 0x40, 0x00, 0x00, 0x1c, 0x00, 0x32, 0x00, 0x00, 0x00, 0x07, 0x00, /* 2: IDcomLaunch.LaunchServer */
    0x0f, 0x00, 0x46, 0x06, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0b, 0x00, 0x04, 0x00, 0x02, 0x00, 0x0b, 0x00, 0x08, 0x00, 0x02, 0x00,
    0x0b, 0x00, 0x0c, 0x00, 0x02, 0x00, 0x48, 0x00, 0x10, 0x00, 0x08, 0x5c,
    0x50, 0x21, 0x14, 0x00, 0x08, 0x5c, 0x70, 0x00, 0x18, 0x00, 0x08, 0x5c,
#endif
};

#ifdef _WIN64
#else
#endif

static const unsigned short IDcomLaunch_FormatStringOffsetTable[] = {
#ifdef _WIN64
    2
#else
    2
#endif
};

static RPC_CLIENT_INTERFACE IDcomLaunch_c_ifspec_data = { sizeof(RPC_CLIENT_INTERFACE),
    {{0x8b5e6a2c, 0x41f0, 0x4d3a, {0x9c, 0x6e, 0x2f, 0x7d, 0x1a, 0x0b, 0x9e, 0x51}}, {1, 0}},
    {{0x8A885D04, 0x1CEB, 0x11C9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    0, 0, 0, 0, 0, 0 };
RPC_IF_HANDLE IDcomLaunch_c_ifspec = (RPC_IF_HANDLE)&IDcomLaunch_c_ifspec_data;

static MIDL_STUB_DESC IDcomLaunch_StubDesc = {
    .RpcInterfaceInformation = &IDcomLaunch_c_ifspec_data,
    .pfnAllocate = MIDL_user_allocate,
    .pfnFree = MIDL_user_free,
    .pFormatTypes = IDcomLaunch_TypeFormat,
    .fCheckBounds = 1,
    .Version = 0x60001,
    .MIDLVersion = 0x8000253,
    .mFlags = 1,
};

#ifndef IDCOMLAUNCH_CLIENT_ONLY
static const SERVER_ROUTINE IDcomLaunch_ServerRoutineTable[] = {
    (SERVER_ROUTINE)(void *)IDcomLaunch_LaunchServer_s
};

int (*IDcomLaunch_hook)(PRPC_MESSAGE);

static void __RPC_STUB IDcomLaunch_dispatch_0(PRPC_MESSAGE m)
{
    if (IDcomLaunch_hook && IDcomLaunch_hook(m))
        return;
    NdrServerCall2(m);
}
static RPC_DISPATCH_FUNCTION IDcomLaunch_DispatchFunctions[] = {
IDcomLaunch_dispatch_0
};
static RPC_DISPATCH_TABLE IDcomLaunch_DispatchTable = { 1, IDcomLaunch_DispatchFunctions, 0 };
static MIDL_SERVER_INFO IDcomLaunch_ServerInfo = {
    .pStubDesc = &IDcomLaunch_StubDesc,
    .DispatchTable = IDcomLaunch_ServerRoutineTable,
    .ProcString = IDcomLaunch_ProcFormat,
    .FmtStringOffset = IDcomLaunch_FormatStringOffsetTable,
    .nCount = 0,
};
static RPC_SERVER_INTERFACE IDcomLaunch_s_ifspec_data = { sizeof(RPC_SERVER_INTERFACE),
    {{0x8b5e6a2c, 0x41f0, 0x4d3a, {0x9c, 0x6e, 0x2f, 0x7d, 0x1a, 0x0b, 0x9e, 0x51}}, {1, 0}},
    {{0x8A885D04, 0x1CEB, 0x11C9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    &IDcomLaunch_DispatchTable, 0, 0, 0, &IDcomLaunch_ServerInfo, 0 };
RPC_IF_HANDLE IDcomLaunch_s_ifspec = (RPC_IF_HANDLE)&IDcomLaunch_s_ifspec_data;
#endif

static NDRGEN_UNUSED long WINAPI IDcomLaunch_LaunchServer(handle_t hRpc, unsigned short *service, unsigned short *parameters, unsigned short *command, unsigned long client, unsigned long *pid)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&IDcomLaunch_StubDesc, IDcomLaunch_ProcFormat + IDcomLaunch_FormatStringOffsetTable[0], hRpc, service, parameters, command, client, pid).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t hRpc;
        unsigned short *service;
        unsigned short *parameters;
        unsigned short *command;
        unsigned long client;
        unsigned long *pid;
    } frame = { hRpc, service, parameters, command, client, pid };
#pragma pack(pop)
    return (long)NdrClientCall2(&IDcomLaunch_StubDesc, IDcomLaunch_ProcFormat + IDcomLaunch_FormatStringOffsetTable[0], (unsigned char *)&frame).Simple;
#endif
}

#endif
