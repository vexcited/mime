/*
 * the proxies and stubs of the interfaces that ole32 itself knows how to carry between processes: IClassFactory, which
 * every local server is reached through, and INotificationActivationCallback, which a toast is activated through. They
 * are what midl writes for a stubless proxy file of these two interfaces, table version 2, assembled by hand; the
 * interpreter of rpcrt4 does the rest.
 */
#include "orpc.h"
#include <mime/win32/notificationactivationcallback.h>

#ifdef _WIN64
#define SLOT 8
#define EXT_SIZE 10
#define EXT_MASK , 0, 0
#define CORR_PARAM_TYPE 0x0B
#define ALIGN_MASK 7
#else
#define SLOT 4
#define EXT_SIZE 8
#define EXT_MASK
#define CORR_PARAM_TYPE 0x08
#define ALIGN_MASK 3
#endif

#define AT(offset) (offset) & 0xFF, ((offset) >> 8) & 0xFF
#define OBJECT_PROC(num, stack, client, server, nparams)                                                              \
    FC_AUTO_HANDLE, Oi_OBJECT_PROC | Oi_HAS_RPCFLAGS | Oi_USE_NEW_INIT_ROUTINES, 0, 0, 0, 0, AT(num), AT(stack),      \
        AT(client), AT(server), OIF_HAS_RETURN | OIF_HAS_EXTENSIONS | OIF_CLIENT_MUST_SIZE | OIF_SERVER_MUST_SIZE,    \
        nparams, EXT_SIZE, 1, 0, 0, 0, 0, 0, 0 EXT_MASK
#define PARAM(attrs, slot, type) AT(attrs), AT(SLOT * (slot)), AT(type)
#define RET_LONG(slot) 0x70, 0, AT(SLOT * (slot)), FC_LONG, 0
#define IN_LONG(slot) 0x48, 0, AT(SLOT * (slot)), FC_LONG, 0
#define IN_ULONG(slot) 0x48, 0, AT(SLOT * (slot)), FC_ULONG, 0

/* ------------------------------------------------------------------ IClassFactory */

#define CF_T_IUNKNOWN 0
#define CF_T_GUID 18
#define CF_T_PPV 34

static const unsigned char g_cf_types[] = {
    /* 0: an interface pointer to IUnknown. */
    FC_IP, FC_CONSTANT_IID, 0, 0, 0, 0, 0, 0, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46,
    /* 18: a guid. */
    FC_STRUCT, 3, 16, 0, FC_LONG, FC_SHORT, FC_SHORT, FC_BYTE, FC_BYTE, FC_BYTE, FC_BYTE, FC_BYTE, FC_BYTE, FC_BYTE,
    FC_BYTE, FC_END,
    /* 34: a reference to a pointer to an interface whose id the second parameter holds. */
    FC_RP, FC_ALLOCED_ON_STACK | FC_POINTER_DEREF, 2, 0,
    FC_IP, FC_PAD, FC_TOP_LEVEL_CONFORMANCE | CORR_PARAM_TYPE, 0, AT(SLOT * 2), 0, 0,
};

_Static_assert(sizeof(g_cf_types) == 46, "the layout of the type string of IClassFactory");

static const unsigned char g_cf_procs[] = {
    /* 3: CreateInstance(pUnkOuter, riid, ppvObject) */
    OBJECT_PROC(3, SLOT * 5, 16, 8, 4),
    PARAM(0x000B, 1, CF_T_IUNKNOWN), PARAM(0x010A, 2, CF_T_GUID), PARAM(0x2013, 3, CF_T_PPV), RET_LONG(4),
    /* 4: LockServer(fLock) */
    OBJECT_PROC(4, SLOT * 3, 4, 4, 2),
    IN_LONG(1), RET_LONG(2),
};

#define CF_PROC_SIZE(params) (16 + EXT_SIZE + 6 * (params))
static const unsigned short g_cf_offsets[] = {0, 0, 0, 0, CF_PROC_SIZE(4)};

static MIDL_STUB_DESC g_cf_desc;
static MIDL_SERVER_INFO g_cf_server;
static MIDL_STUBLESS_PROXY_INFO g_cf_proxy_info;

typedef struct {
    const MIDL_STUBLESS_PROXY_INFO *Info;
    const IID *Iid;
    const void *Methods[5];
} CF_PROXY_VTBL;

static CF_PROXY_VTBL g_cf_proxy_vtbl = {
    &g_cf_proxy_info, &IID_IClassFactory,
    {IUnknown_QueryInterface_Proxy, IUnknown_AddRef_Proxy, IUnknown_Release_Proxy, (void *)(LONG_PTR)-1,
     (void *)(LONG_PTR)-1}};

static void __RPC_STUB stub_call(IRpcStubBuffer *This, IRpcChannelBuffer *channel, PRPC_MESSAGE message, DWORD *phase)
{
    NdrStubCall2(This, channel, message, phase);
}

static const PRPC_STUB_FUNCTION g_cf_stub_table[] = {stub_call, stub_call};
static CInterfaceStubVtbl g_cf_stub_vtbl = {
    {&IID_IClassFactory, &g_cf_server, 5, &g_cf_stub_table[-3]},
    {CStdStubBuffer_METHODS},
};

/* ------------------------------------------------------------------ INotificationActivationCallback */

#define NA_T_STRING 0
#define NA_T_STRUCT 2
#define NA_T_ARRAY 22
#define NA_T_DATA 42

#ifdef _WIN64
#define STRUCT_SIZE 16
#else
#define STRUCT_SIZE 8
#endif

/* the offset of the pointee from the place its short stands at. */
#define REL(target, here) AT((target) - (here))

static const unsigned char g_na_types[] = {
    /* 0: a string. */
    FC_C_WSTRING, FC_PAD,
    /* 2: a complex structure of two pointers to strings. */
    FC_BOGUS_STRUCT, ALIGN_MASK, AT(STRUCT_SIZE), AT(0), AT(6),
    FC_POINTER, FC_POINTER, FC_END, FC_PAD,
    FC_UP, FC_SIMPLE_POINTER, FC_C_WSTRING, FC_PAD,
    FC_UP, FC_SIMPLE_POINTER, FC_C_WSTRING, FC_PAD,
    /* 22: a conformant array of them, sized by the last parameter. */
    FC_BOGUS_ARRAY, ALIGN_MASK, AT(0),
    FC_TOP_LEVEL_CONFORMANCE | FC_ULONG, 0, AT(SLOT * 4), 0, 0,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    FC_EMBEDDED_COMPLEX, 0, REL(NA_T_STRUCT, 40),
    /* 42: a unique pointer to the array, which a toast with nothing to report leaves out. */
    FC_UP, 0, REL(NA_T_ARRAY, 44),
};

_Static_assert(sizeof(g_na_types) == 46, "the layout of the type string of INotificationActivationCallback");

static const unsigned char g_na_procs[] = {
    /* 3: Activate(appUserModelId, invokedArgs, data, count) */
    OBJECT_PROC(3, SLOT * 6, 4, 4, 5),
    PARAM(0x010B, 1, NA_T_STRING), PARAM(0x010B, 2, NA_T_STRING), PARAM(0x000B, 3, NA_T_DATA), IN_ULONG(4), RET_LONG(5),
};
static const unsigned short g_na_offsets[] = {0, 0, 0, 0};

static MIDL_STUB_DESC g_na_desc;
static MIDL_SERVER_INFO g_na_server;
static MIDL_STUBLESS_PROXY_INFO g_na_proxy_info;

typedef struct {
    const MIDL_STUBLESS_PROXY_INFO *Info;
    const IID *Iid;
    const void *Methods[4];
} NA_PROXY_VTBL;

static NA_PROXY_VTBL g_na_proxy_vtbl = {
    &g_na_proxy_info, &IID_INotificationActivationCallback,
    {IUnknown_QueryInterface_Proxy, IUnknown_AddRef_Proxy, IUnknown_Release_Proxy, (void *)(LONG_PTR)-1}};

static const PRPC_STUB_FUNCTION g_na_stub_table[] = {stub_call};
static CInterfaceStubVtbl g_na_stub_vtbl = {
    {&IID_INotificationActivationCallback, &g_na_server, 4, &g_na_stub_table[-3]},
    {CStdStubBuffer_METHODS},
};

/* ------------------------------------------------------------------ the proxy file */

static const CInterfaceProxyVtbl *g_proxy_list[] = {(const CInterfaceProxyVtbl *)&g_cf_proxy_vtbl,
                                                    (const CInterfaceProxyVtbl *)&g_na_proxy_vtbl, nullptr};
static const CInterfaceStubVtbl *g_stub_list[] = {&g_cf_stub_vtbl, &g_na_stub_vtbl, nullptr};
static const char *g_names[] = {"IClassFactory", "INotificationActivationCallback", nullptr};

static int WINAPI lookup(const IID *iid, int *index)
{
    *index = IsEqualGUID(iid, &IID_IClassFactory) ? 0 : 1;
    return IsEqualGUID(iid, &IID_IClassFactory) || IsEqualGUID(iid, &IID_INotificationActivationCallback);
}

static ProxyFileInfo g_file = {(const PCInterfaceProxyVtblList *)g_proxy_list,
                               (const PCInterfaceStubVtblList *)g_stub_list, g_names, nullptr, lookup, 2, 2, nullptr, 0,
                               0, 0};
static const ProxyFileInfo *g_file_list[] = {&g_file, nullptr};
static CStdPSFactoryBuffer g_factory;

static const CLSID CLSID_BuiltinPS = {0x00000320, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};

CSTDSTUBBUFFERRELEASE(&g_factory)

static LONG g_ready;

static void setup(void)
{
    if (g_ready == 2)
        return;
    if (InterlockedCompareExchange(&g_ready, 1, 0) != 0) {
        while (g_ready != 2)
            Sleep(1);
        return;
    }
    g_cf_desc.pfnAllocate = (void *(__RPC_API *)(size_t))CoTaskMemAlloc;
    g_cf_desc.pfnFree = CoTaskMemFree;
    g_cf_desc.pFormatTypes = g_cf_types;
    g_cf_desc.Version = 0x60001;
    g_cf_desc.MIDLVersion = 0x8000253;
    g_cf_proxy_info.pStubDesc = &g_cf_desc;
    g_cf_proxy_info.ProcFormatString = g_cf_procs;
    g_cf_proxy_info.FormatStringOffset = g_cf_offsets;
    g_cf_server.pStubDesc = &g_cf_desc;
    g_cf_server.ProcString = g_cf_procs;
    g_cf_server.FmtStringOffset = g_cf_offsets;
    g_cf_server.nCount = 5;

    g_na_desc = g_cf_desc;
    g_na_desc.pFormatTypes = g_na_types;
    g_na_proxy_info.pStubDesc = &g_na_desc;
    g_na_proxy_info.ProcFormatString = g_na_procs;
    g_na_proxy_info.FormatStringOffset = g_na_offsets;
    g_na_server.pStubDesc = &g_na_desc;
    g_na_server.ProcString = g_na_procs;
    g_na_server.FmtStringOffset = g_na_offsets;
    g_na_server.nCount = 4;
    g_ready = 2;
}

HRESULT OrpcBuiltinFactory(REFIID iid, IPSFactoryBuffer **factory)
{
    if (!IsEqualGUID(iid, &IID_IClassFactory) && !IsEqualGUID(iid, &IID_INotificationActivationCallback))
        return E_NOINTERFACE;
    setup();
    return NdrDllGetClassObject(&CLSID_BuiltinPS, &IID_IPSFactoryBuffer, (void **)factory, g_file_list, &CLSID_BuiltinPS,
                                &g_factory);
}

/* the class that marshals an interface of this file, for CoGetPSClsid. */
BOOL OrpcBuiltinClass(REFIID iid, CLSID *clsid)
{
    if (!IsEqualGUID(iid, &IID_IClassFactory) && !IsEqualGUID(iid, &IID_INotificationActivationCallback))
        return FALSE;
    *clsid = CLSID_BuiltinPS;
    return TRUE;
}
