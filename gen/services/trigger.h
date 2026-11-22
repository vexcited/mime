#ifndef NDRGEN_TRIGGER_H
#define NDRGEN_TRIGGER_H

#include <mime/win32/rpcndr.h>

#define NDRGEN_UNUSED __attribute__((unused))



#ifdef _WIN64
_Static_assert(sizeof(GUID) == 16, "size of GUID");
_Static_assert(__builtin_offsetof(GUID, Data1) == 0, "GUID.Data1");
_Static_assert(__builtin_offsetof(GUID, Data2) == 4, "GUID.Data2");
_Static_assert(__builtin_offsetof(GUID, Data3) == 6, "GUID.Data3");
_Static_assert(__builtin_offsetof(GUID, Data4) == 8, "GUID.Data4");
#else
_Static_assert(sizeof(GUID) == 16, "size of GUID");
_Static_assert(__builtin_offsetof(GUID, Data1) == 0, "GUID.Data1");
_Static_assert(__builtin_offsetof(GUID, Data2) == 4, "GUID.Data2");
_Static_assert(__builtin_offsetof(GUID, Data3) == 6, "GUID.Data3");
_Static_assert(__builtin_offsetof(GUID, Data4) == 8, "GUID.Data4");
#endif

/* ------------------------------------------------------------------ interface sctrigger */

#ifndef SCTRIGGER_CLIENT_ONLY
extern unsigned long WINAPI sctrigger_Raise_s(handle_t h, GUID *event, unsigned short *data);
#endif
void *WINAPI MIDL_user_allocate(size_t);
void WINAPI MIDL_user_free(void *);

static const unsigned char sctrigger_TypeFormat[] = {
#ifdef _WIN64
    0x00, 0x00, /* 0: pad */
    0x15, 0x03, 0x10, 0x00, 0x08, 0x06, 0x06, 0x43, 0x3d, 0x5b, /* 2: struct GUID (STRUCT) */
    0x11, 0x08, 0x25, 0x5c, /* 12: pointer */
#else
    0x00, 0x00, /* 0: pad */
    0x15, 0x03, 0x10, 0x00, 0x08, 0x06, 0x06, 0x43, 0x3d, 0x5b, /* 2: struct GUID (STRUCT) */
    0x11, 0x08, 0x25, 0x5c, /* 12: pointer */
#endif
};

static const unsigned char sctrigger_ProcFormat[] = {
#ifdef _WIN64
    0x00, 0x00, /* 0: pad */
    0x00, 0x40, 0x00, 0x00, 0x20, 0x00, 0x32, 0x00, 0x00, 0x00, 0x13, 0x00, /* 2: sctrigger.Raise */
    0x07, 0x00, 0x46, 0x03, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0a, 0x01, 0x08, 0x00, 0x02, 0x00, 0x0b, 0x00, 0x10, 0x00,
    0x0c, 0x00, 0x70, 0x00, 0x18, 0x00, 0x08, 0x5c,
#else
    0x00, 0x00, /* 0: pad */
    0x00, 0x40, 0x00, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x13, 0x00, /* 2: sctrigger.Raise */
    0x07, 0x00, 0x46, 0x03, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0a, 0x01, 0x04, 0x00, 0x02, 0x00, 0x0b, 0x00, 0x08, 0x00, 0x0c, 0x00,
    0x70, 0x00, 0x0c, 0x00, 0x08, 0x5c,
#endif
};

#ifdef _WIN64
#else
#endif

static const unsigned short sctrigger_FormatStringOffsetTable[] = {
#ifdef _WIN64
    2
#else
    2
#endif
};

static RPC_CLIENT_INTERFACE sctrigger_c_ifspec_data = { sizeof(RPC_CLIENT_INTERFACE),
    {{0x4d494d45, 0x7363, 0x7472, {0x69, 0x67, 0x67, 0x65, 0x72, 0x00, 0x00, 0x01}}, {1, 0}},
    {{0x8A885D04, 0x1CEB, 0x11C9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    0, 0, 0, 0, 0, 0 };
RPC_IF_HANDLE sctrigger_c_ifspec = (RPC_IF_HANDLE)&sctrigger_c_ifspec_data;

static MIDL_STUB_DESC sctrigger_StubDesc = {
    .RpcInterfaceInformation = &sctrigger_c_ifspec_data,
    .pfnAllocate = MIDL_user_allocate,
    .pfnFree = MIDL_user_free,
    .pFormatTypes = sctrigger_TypeFormat,
    .fCheckBounds = 1,
    .Version = 0x60001,
    .MIDLVersion = 0x8000253,
    .mFlags = 1,
};

#ifndef SCTRIGGER_CLIENT_ONLY
static const SERVER_ROUTINE sctrigger_ServerRoutineTable[] = {
    (SERVER_ROUTINE)(void *)sctrigger_Raise_s
};

int (*sctrigger_hook)(PRPC_MESSAGE);

static void __RPC_STUB sctrigger_dispatch_0(PRPC_MESSAGE m)
{
    if (sctrigger_hook && sctrigger_hook(m))
        return;
    NdrServerCall2(m);
}
static RPC_DISPATCH_FUNCTION sctrigger_DispatchFunctions[] = {
sctrigger_dispatch_0
};
static RPC_DISPATCH_TABLE sctrigger_DispatchTable = { 1, sctrigger_DispatchFunctions, 0 };
static MIDL_SERVER_INFO sctrigger_ServerInfo = {
    .pStubDesc = &sctrigger_StubDesc,
    .DispatchTable = sctrigger_ServerRoutineTable,
    .ProcString = sctrigger_ProcFormat,
    .FmtStringOffset = sctrigger_FormatStringOffsetTable,
    .nCount = 0,
};
static RPC_SERVER_INTERFACE sctrigger_s_ifspec_data = { sizeof(RPC_SERVER_INTERFACE),
    {{0x4d494d45, 0x7363, 0x7472, {0x69, 0x67, 0x67, 0x65, 0x72, 0x00, 0x00, 0x01}}, {1, 0}},
    {{0x8A885D04, 0x1CEB, 0x11C9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    &sctrigger_DispatchTable, 0, 0, 0, &sctrigger_ServerInfo, 0 };
RPC_IF_HANDLE sctrigger_s_ifspec = (RPC_IF_HANDLE)&sctrigger_s_ifspec_data;
#endif

static NDRGEN_UNUSED unsigned long WINAPI sctrigger_Raise(handle_t h, GUID *event, unsigned short *data)
{
#ifdef _WIN64
    return (unsigned long)NdrClientCall2(&sctrigger_StubDesc, sctrigger_ProcFormat + sctrigger_FormatStringOffsetTable[0], h, event, data).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t h;
        GUID *event;
        unsigned short *data;
    } frame = { h, event, data };
#pragma pack(pop)
    return (unsigned long)NdrClientCall2(&sctrigger_StubDesc, sctrigger_ProcFormat + sctrigger_FormatStringOffsetTable[0], (unsigned char *)&frame).Simple;
#endif
}

#endif
