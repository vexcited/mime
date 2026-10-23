/* com proxies and stubs of rpcrt4: channel, proxy and stub interfaces, and the structures midl emits for a proxy dll. */
#ifndef MIME_WIN32_RPCPROXY_H
#define MIME_WIN32_RPCPROXY_H

#include <mime/win32/objbase.h>
#include <mime/win32/rpcndr.h>

typedef struct tagRPCOLEMESSAGE {
    void *reserved1;
    ULONG dataRepresentation;
    void *pvBuffer;
    ULONG cbBuffer;
    ULONG iMethod;
    void *reserved2[5];
    ULONG rpcFlags;
} RPCOLEMESSAGE;

typedef struct IRpcChannelBuffer IRpcChannelBuffer;
typedef struct IRpcProxyBuffer IRpcProxyBuffer;
typedef struct IRpcStubBuffer IRpcStubBuffer;
typedef struct IPSFactoryBuffer IPSFactoryBuffer;

typedef struct IRpcChannelBufferVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IRpcChannelBuffer *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IRpcChannelBuffer *This);
    ULONG(STDMETHODCALLTYPE *Release)(IRpcChannelBuffer *This);
    HRESULT(STDMETHODCALLTYPE *GetBuffer)(IRpcChannelBuffer *This, RPCOLEMESSAGE *message, REFIID riid);
    HRESULT(STDMETHODCALLTYPE *SendReceive)(IRpcChannelBuffer *This, RPCOLEMESSAGE *message, ULONG *status);
    HRESULT(STDMETHODCALLTYPE *FreeBuffer)(IRpcChannelBuffer *This, RPCOLEMESSAGE *message);
    HRESULT(STDMETHODCALLTYPE *GetDestCtx)(IRpcChannelBuffer *This, DWORD *dest_context, void **dest_context_data);
    HRESULT(STDMETHODCALLTYPE *IsConnected)(IRpcChannelBuffer *This);
} IRpcChannelBufferVtbl;
struct IRpcChannelBuffer {
    const IRpcChannelBufferVtbl *lpVtbl;
};

typedef struct IRpcChannelBuffer2 IRpcChannelBuffer2;
typedef struct IRpcChannelBuffer2Vtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IRpcChannelBuffer2 *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IRpcChannelBuffer2 *This);
    ULONG(STDMETHODCALLTYPE *Release)(IRpcChannelBuffer2 *This);
    HRESULT(STDMETHODCALLTYPE *GetBuffer)(IRpcChannelBuffer2 *This, RPCOLEMESSAGE *message, REFIID riid);
    HRESULT(STDMETHODCALLTYPE *SendReceive)(IRpcChannelBuffer2 *This, RPCOLEMESSAGE *message, ULONG *status);
    HRESULT(STDMETHODCALLTYPE *FreeBuffer)(IRpcChannelBuffer2 *This, RPCOLEMESSAGE *message);
    HRESULT(STDMETHODCALLTYPE *GetDestCtx)(IRpcChannelBuffer2 *This, DWORD *dest_context, void **dest_context_data);
    HRESULT(STDMETHODCALLTYPE *IsConnected)(IRpcChannelBuffer2 *This);
    HRESULT(STDMETHODCALLTYPE *GetProtocolVersion)(IRpcChannelBuffer2 *This, DWORD *version);
} IRpcChannelBuffer2Vtbl;
struct IRpcChannelBuffer2 {
    const IRpcChannelBuffer2Vtbl *lpVtbl;
};

typedef struct IRpcProxyBufferVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IRpcProxyBuffer *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IRpcProxyBuffer *This);
    ULONG(STDMETHODCALLTYPE *Release)(IRpcProxyBuffer *This);
    HRESULT(STDMETHODCALLTYPE *Connect)(IRpcProxyBuffer *This, IRpcChannelBuffer *channel);
    void(STDMETHODCALLTYPE *Disconnect)(IRpcProxyBuffer *This);
} IRpcProxyBufferVtbl;
struct IRpcProxyBuffer {
    const IRpcProxyBufferVtbl *lpVtbl;
};

typedef struct IRpcStubBufferVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IRpcStubBuffer *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IRpcStubBuffer *This);
    ULONG(STDMETHODCALLTYPE *Release)(IRpcStubBuffer *This);
    HRESULT(STDMETHODCALLTYPE *Connect)(IRpcStubBuffer *This, IUnknown *server);
    void(STDMETHODCALLTYPE *Disconnect)(IRpcStubBuffer *This);
    HRESULT(STDMETHODCALLTYPE *Invoke)(IRpcStubBuffer *This, RPCOLEMESSAGE *message, IRpcChannelBuffer *channel);
    IRpcStubBuffer *(STDMETHODCALLTYPE *IsIIDSupported)(IRpcStubBuffer *This, REFIID riid);
    ULONG(STDMETHODCALLTYPE *CountRefs)(IRpcStubBuffer *This);
    HRESULT(STDMETHODCALLTYPE *DebugServerQueryInterface)(IRpcStubBuffer *This, void **object);
    void(STDMETHODCALLTYPE *DebugServerRelease)(IRpcStubBuffer *This, void *object);
} IRpcStubBufferVtbl;
struct IRpcStubBuffer {
    const IRpcStubBufferVtbl *lpVtbl;
};

typedef struct IPSFactoryBufferVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IPSFactoryBuffer *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IPSFactoryBuffer *This);
    ULONG(STDMETHODCALLTYPE *Release)(IPSFactoryBuffer *This);
    HRESULT(STDMETHODCALLTYPE *CreateProxy)(IPSFactoryBuffer *This, IUnknown *outer, REFIID riid,
                                            IRpcProxyBuffer **proxy, void **object);
    HRESULT(STDMETHODCALLTYPE *CreateStub)(IPSFactoryBuffer *This, REFIID riid, IUnknown *server, IRpcStubBuffer **stub);
} IPSFactoryBufferVtbl;
struct IPSFactoryBuffer {
    const IPSFactoryBufferVtbl *lpVtbl;
};

MIME_DEFINE_GUID(IID_IRpcChannelBuffer, 0xD5F56B60, 0x593B, 0x101A, 0xB5, 0x69, 0x08, 0x00, 0x2B, 0x2D, 0xBF, 0x7A);
MIME_DEFINE_GUID(IID_IRpcProxyBuffer, 0xD5F56A34, 0x593B, 0x101A, 0xB5, 0x69, 0x08, 0x00, 0x2B, 0x2D, 0xBF, 0x7A);
MIME_DEFINE_GUID(IID_IRpcStubBuffer, 0xD5F56AFC, 0x593B, 0x101A, 0xB5, 0x69, 0x08, 0x00, 0x2B, 0x2D, 0xBF, 0x7A);
MIME_DEFINE_GUID(IID_IPSFactoryBuffer, 0xD5F569D0, 0x593B, 0x101A, 0xB5, 0x69, 0x08, 0x00, 0x2B, 0x2D, 0xBF, 0x7A);
MIME_DEFINE_GUID(IID_IRpcChannelBuffer2, 0x594F31D0, 0x7F19, 0x11D0, 0xB1, 0x94, 0x00, 0xA0, 0xC9, 0x0D, 0xC8, 0xBF);

struct tagCInterfaceStubVtbl;
struct tagCInterfaceProxyVtbl;
typedef struct tagCInterfaceStubVtbl *PCInterfaceStubVtblList;
typedef struct tagCInterfaceProxyVtbl *PCInterfaceProxyVtblList;
typedef const char *PCInterfaceName;
typedef int(STDAPICALLTYPE IIDLookupRtn)(const IID *iid, int *index);
typedef IIDLookupRtn *PIIDLookup;

typedef struct tagProxyFileInfo {
    const PCInterfaceProxyVtblList *pProxyVtblList;
    const PCInterfaceStubVtblList *pStubVtblList;
    const PCInterfaceName *pNamesArray;
    const IID **pDelegatedIIDs;
    const PIIDLookup pIIDLookupRtn;
    unsigned short TableSize;
    unsigned short TableVersion;
    const IID **pAsyncIIDLookup;
    LONG_PTR Filler2;
    LONG_PTR Filler3;
    LONG_PTR Filler4;
} ProxyFileInfo;
typedef ProxyFileInfo ExtendedProxyFileInfo;

/* in table version 2 proxy files, the stubless proxy info comes first. */
typedef struct tagCInterfaceProxyHeader {
#ifdef USE_STUBLESS_PROXY
    const void *pStublessProxyInfo;
#endif
    const IID *piid;
} CInterfaceProxyHeader;

typedef struct tagCInterfaceProxyVtbl {
    CInterfaceProxyHeader header;
    void *Vtbl[1];
} CInterfaceProxyVtbl;

typedef void(__RPC_STUB *PRPC_STUB_FUNCTION)(IRpcStubBuffer *This, IRpcChannelBuffer *channel, PRPC_MESSAGE message,
                                             DWORD *phase);

typedef struct tagCInterfaceStubHeader {
    const IID *piid;
    const MIDL_SERVER_INFO *pServerInfo;
    ULONG DispatchTableCount;
    const PRPC_STUB_FUNCTION *pDispatchTable;
} CInterfaceStubHeader;

typedef struct tagCInterfaceStubVtbl {
    CInterfaceStubHeader header;
    IRpcStubBufferVtbl Vtbl;
} CInterfaceStubVtbl;

typedef struct tagCStdStubBuffer {
    const IRpcStubBufferVtbl *lpVtbl;
    LONG RefCount;
    IUnknown *pvServerObject;
    const void *pCallFactoryVtbl;
    const IID *pAsyncIID;
    IPSFactoryBuffer *pPSFactory;
    const void *pRMBVtbl;
} CStdStubBuffer;

typedef struct tagCStdPSFactoryBuffer {
    const IPSFactoryBufferVtbl *lpVtbl;
    LONG RefCount;
    const ProxyFileInfo **pProxyFileList;
    LONG Filler1;
} CStdPSFactoryBuffer;

RPCRTAPI void RPC_ENTRY NdrProxyInitialize(void *This, PRPC_MESSAGE message, PMIDL_STUB_MESSAGE m,
                                           PMIDL_STUB_DESC desc, unsigned int proc_num);
RPCRTAPI void RPC_ENTRY NdrProxyGetBuffer(void *This, PMIDL_STUB_MESSAGE m);
RPCRTAPI void RPC_ENTRY NdrProxySendReceive(void *This, MIDL_STUB_MESSAGE *m);
RPCRTAPI void RPC_ENTRY NdrProxyFreeBuffer(void *This, MIDL_STUB_MESSAGE *m);
RPCRTAPI HRESULT RPC_ENTRY NdrProxyErrorHandler(DWORD code);
RPCRTAPI void RPC_ENTRY NdrStubInitialize(PRPC_MESSAGE message, PMIDL_STUB_MESSAGE m, PMIDL_STUB_DESC desc,
                                          IRpcChannelBuffer *channel);
RPCRTAPI void RPC_ENTRY NdrStubInitializePartial(PRPC_MESSAGE message, PMIDL_STUB_MESSAGE m, PMIDL_STUB_DESC desc,
                                                 IRpcChannelBuffer *channel, ULONG requested);
RPCRTAPI void RPC_ENTRY NdrStubGetBuffer(IRpcStubBuffer *This, IRpcChannelBuffer *channel, PMIDL_STUB_MESSAGE m);
RPCRTAPI void __RPC_STUB NdrStubForwardingFunction(IRpcStubBuffer *This, IRpcChannelBuffer *channel, PRPC_MESSAGE message,
                                                   DWORD *phase);
RPCRTAPI HRESULT WINAPI CStdStubBuffer_QueryInterface(IRpcStubBuffer *This, REFIID riid, void **object);
RPCRTAPI ULONG WINAPI CStdStubBuffer_AddRef(IRpcStubBuffer *This);
/* each proxy dll defines its stub vtables' Release with these, since only it knows its factory. */
ULONG WINAPI CStdStubBuffer_Release(IRpcStubBuffer *This);
ULONG WINAPI CStdStubBuffer2_Release(IRpcStubBuffer *This);
#define CSTDSTUBBUFFERRELEASE(factory)                                                                                \
    ULONG WINAPI CStdStubBuffer_Release(IRpcStubBuffer *This)                                                         \
    {                                                                                                                 \
        return NdrCStdStubBuffer_Release(This, (IPSFactoryBuffer *)(factory));                                       \
    }
#define CSTDSTUBBUFFER2RELEASE(factory)                                                                               \
    ULONG WINAPI CStdStubBuffer2_Release(IRpcStubBuffer *This)                                                        \
    {                                                                                                                 \
        return NdrCStdStubBuffer2_Release(This, (IPSFactoryBuffer *)(factory));                                      \
    }
RPCRTAPI ULONG WINAPI NdrCStdStubBuffer_Release(IRpcStubBuffer *This, IPSFactoryBuffer *factory);
RPCRTAPI ULONG WINAPI NdrCStdStubBuffer2_Release(IRpcStubBuffer *This, IPSFactoryBuffer *factory);
RPCRTAPI HRESULT WINAPI CStdStubBuffer_Connect(IRpcStubBuffer *This, IUnknown *server);
RPCRTAPI void WINAPI CStdStubBuffer_Disconnect(IRpcStubBuffer *This);
RPCRTAPI HRESULT WINAPI CStdStubBuffer_Invoke(IRpcStubBuffer *This, RPCOLEMESSAGE *message, IRpcChannelBuffer *channel);
RPCRTAPI IRpcStubBuffer *WINAPI CStdStubBuffer_IsIIDSupported(IRpcStubBuffer *This, REFIID riid);
RPCRTAPI ULONG WINAPI CStdStubBuffer_CountRefs(IRpcStubBuffer *This);
RPCRTAPI HRESULT WINAPI CStdStubBuffer_DebugServerQueryInterface(IRpcStubBuffer *This, void **object);
RPCRTAPI void WINAPI CStdStubBuffer_DebugServerRelease(IRpcStubBuffer *This, void *object);
/* proxies and stubs built from type library info. ITypeInfo comes from oleauto.h. */
struct ITypeInfo;
RPCRTAPI HRESULT WINAPI CreateProxyFromTypeInfo(struct ITypeInfo *info, IUnknown *outer, REFIID riid,
                                                IRpcProxyBuffer **proxy, void **object);
RPCRTAPI HRESULT WINAPI CreateStubFromTypeInfo(struct ITypeInfo *info, REFIID riid, IUnknown *server,
                                               IRpcStubBuffer **stub);
RPCRTAPI HRESULT RPC_ENTRY NdrDllGetClassObject(REFCLSID clsid, REFIID iid, void **object,
                                                const ProxyFileInfo **proxy_file_list, const CLSID *proxy_clsid,
                                                CStdPSFactoryBuffer *factory);
RPCRTAPI HRESULT RPC_ENTRY NdrDllCanUnloadNow(CStdPSFactoryBuffer *factory);
RPCRTAPI HRESULT RPC_ENTRY NdrDllRegisterProxy(HMODULE module, const ProxyFileInfo **proxy_file_list,
                                               const CLSID *proxy_clsid);
RPCRTAPI HRESULT RPC_ENTRY NdrDllUnregisterProxy(HMODULE module, const ProxyFileInfo **proxy_file_list,
                                                 const CLSID *proxy_clsid);
#define CStdStubBuffer_METHODS                                                                                        \
    CStdStubBuffer_QueryInterface, CStdStubBuffer_AddRef, CStdStubBuffer_Release, CStdStubBuffer_Connect,             \
        CStdStubBuffer_Disconnect, CStdStubBuffer_Invoke, CStdStubBuffer_IsIIDSupported, CStdStubBuffer_CountRefs,    \
        CStdStubBuffer_DebugServerQueryInterface, CStdStubBuffer_DebugServerRelease
/* stub for an interface whose base has its own stub: the runtime fills the zero slots. */
#define CStdStubBuffer_DELEGATING_METHODS 0, 0, CStdStubBuffer2_Release, 0, 0, 0, 0, 0, 0, 0

RPCRTAPI HRESULT WINAPI IUnknown_QueryInterface_Proxy(IUnknown *This, REFIID riid, void **object);
RPCRTAPI ULONG WINAPI IUnknown_AddRef_Proxy(IUnknown *This);
RPCRTAPI ULONG WINAPI IUnknown_Release_Proxy(IUnknown *This);

#endif
