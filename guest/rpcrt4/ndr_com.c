/*
 * com proxies and stubs: the objects that stand for an interface in the client's process, and for an object in the server's.
 *
 * A proxy dll built from midl lists its interfaces in ProxyFileInfo tables. This file is the half of rpcrt4 that turns those
 * tables into proxies, stubs and the factory that makes them, and the half of the interpreter that talks to the channel a
 * proxy is connected to. The channel itself belongs to ole32, which marshals interface pointers for the engine as well.
 *
 * Own layouts, not the reference's: the interface pointer of a proxy is a pointer to the first field of a PROXY, so the
 * buffer is found from it by a cast. A table version 2 proxy vtable is generated with a placeholder in each method that
 * has no code of its own, and a copy of the table with thunks in the places of the placeholders is made when the first
 * proxy of the interface is.
 */
#include "ndr_proc.h"
#include <mime/win32/rpcproxy.h>
#include <mime/win32/objidl.h>

#define MSHLFLAGS_NORMAL 0
#define CO_E_OBJNOTCONNECTED ((HRESULT)0x800401FDL)

/* the most methods a thunk exists for. */
#define NDR_MAX_STUBLESS_METHODS 1024
#define NDR_MAX_DELEGATED_METHODS 128
#define NDR_THUNK_SIZE 16
#define NDR_DELEGATE_THUNK_SIZE 32

static HRESULT NdrStubErrorHandler(DWORD code);

/* the first instruction of the thunk of a method, in thunks.S. */
extern const unsigned char NdrpStublessThunks[];
extern const unsigned char NdrpDelegateThunks[];

/* ------------------------------------------------------------------ the layouts */

typedef struct PROXY {
    /* the interface pointer of the proxy points here, to the vtable it uses. */
    const void *Vtbl;
    /* the interface pointer of the proxy of the base interface the thunks of delegated methods go to. */
    void *DelegateInterface;
    const IRpcProxyBufferVtbl *lpVtbl;
    LONG Refs;
    IUnknown *Outer;
    const IID *Iid;
    PCInterfaceName Name;
    IRpcChannelBuffer *Channel;
    IPSFactoryBuffer *Factory;
    IRpcProxyBuffer *Delegate;
    const IID *DelegatedIid;
} PROXY;

#define PROXY_OF_INTERFACE(p) ((PROXY *)(p))
#define PROXY_OF_BUFFER(p) CONTAINING_RECORD((p), PROXY, lpVtbl)

typedef struct STUB {
    CStdStubBuffer Base;
    IRpcStubBuffer *Delegate;
} STUB;

/* the head of a proxy vtable in the two table versions. */
typedef struct {
    const MIDL_STUBLESS_PROXY_INFO *Info;
    const IID *Iid;
} PROXY_HEAD2;

static const IID *proxy_vtbl_iid(const ProxyFileInfo *file, const CInterfaceProxyVtbl *v)
{
    if (file->TableVersion >= 2)
        return ((const PROXY_HEAD2 *)v)->Iid;
    return *(const IID *const *)v;
}

static const void *const *proxy_vtbl_methods(const ProxyFileInfo *file, const CInterfaceProxyVtbl *v)
{
    return (const void *const *)((const uchar *)v + (file->TableVersion >= 2 ? sizeof(PROXY_HEAD2) : sizeof(void *)));
}

/* the entry of an interface in a list of proxy files. */
typedef struct {
    const ProxyFileInfo *File;
    ULONG Index;
    const CInterfaceProxyVtbl *Proxy;
    const CInterfaceStubVtbl *Stub;
    const IID *Delegated;
    PCInterfaceName Name;
} PS_ENTRY;

static BOOL find_entry(const ProxyFileInfo **list, REFIID iid, PS_ENTRY *out)
{
    if (!list)
        return FALSE;
    for (; *list; list++) {
        const ProxyFileInfo *file = *list;
        for (ULONG i = 0; i < file->TableSize; i++) {
            const CInterfaceProxyVtbl *proxy = file->pProxyVtblList ? (const CInterfaceProxyVtbl *)file->pProxyVtblList[i] : nullptr;
            const CInterfaceStubVtbl *stub = file->pStubVtblList ? (const CInterfaceStubVtbl *)file->pStubVtblList[i] : nullptr;
            const IID *found = proxy ? proxy_vtbl_iid(file, proxy) : stub ? stub->header.piid : nullptr;
            if (!found || !IsEqualGUID(found, iid))
                continue;
            out->File = file;
            out->Index = i;
            out->Proxy = proxy;
            out->Stub = stub;
            out->Delegated = file->pDelegatedIIDs ? file->pDelegatedIIDs[i] : nullptr;
            out->Name = file->pNamesArray ? file->pNamesArray[i] : nullptr;
            return TRUE;
        }
    }
    return FALSE;
}

/* ------------------------------------------------------------------ ole32 */

typedef struct {
    HRESULT(WINAPI *CoGetPSClsid)(REFIID, CLSID *);
    HRESULT(WINAPI *CoGetClassObject)(REFCLSID, DWORD, void *, REFIID, void **);
    HRESULT(WINAPI *CoMarshalInterface)(IStream *, REFIID, IUnknown *, DWORD, void *, DWORD);
    HRESULT(WINAPI *CoUnmarshalInterface)(IStream *, REFIID, void **);
    HRESULT(WINAPI *CoGetMarshalSizeMax)(ULONG *, REFIID, IUnknown *, DWORD, void *, DWORD);
} OLE32_FUNCTIONS;

static const OLE32_FUNCTIONS *ole32(void)
{
    static OLE32_FUNCTIONS table;
    static LONG state;
    if (state != 2) {
        if (InterlockedCompareExchange(&state, 1, 0) == 0) {
            HMODULE m = LoadLibraryW(L"ole32.dll");
            if (m) {
                table.CoGetPSClsid = (void *)GetProcAddress(m, "CoGetPSClsid");
                table.CoGetClassObject = (void *)GetProcAddress(m, "CoGetClassObject");
                table.CoMarshalInterface = (void *)GetProcAddress(m, "CoMarshalInterface");
                table.CoUnmarshalInterface = (void *)GetProcAddress(m, "CoUnmarshalInterface");
                table.CoGetMarshalSizeMax = (void *)GetProcAddress(m, "CoGetMarshalSizeMax");
            }
            state = 2;
        } else {
            while (state != 2)
                Sleep(1);
        }
    }
    return &table;
}

/* the factory of the proxies and stubs of an interface that another dll provides, or null. */
static IPSFactoryBuffer *factory_of(REFIID iid)
{
    const OLE32_FUNCTIONS *o = ole32();
    CLSID clsid;
    IPSFactoryBuffer *factory = nullptr;
    if (!o->CoGetPSClsid || !o->CoGetClassObject || FAILED(o->CoGetPSClsid(iid, &clsid)))
        return nullptr;
    if (FAILED(o->CoGetClassObject(&clsid, CLSCTX_INPROC_SERVER, nullptr, &IID_IPSFactoryBuffer, (void **)&factory)))
        return nullptr;
    return factory;
}

/* ------------------------------------------------------------------ the vtables of proxies */

/*
 * The vtable a proxy of an interface uses: the one of the proxy file with thunks where it has placeholders. A placeholder of
 * -1 is a method that midl left to the interpreter, 0 after the three methods of IUnknown is one of a base interface that
 * has a proxy of its own.
 */
typedef struct VTBL_COPY {
    struct VTBL_COPY *Next;
    const CInterfaceProxyVtbl *Original;
    const void **Methods;
} VTBL_COPY;

static VTBL_COPY *g_copies;
static SRWLOCK g_copies_lock = SRWLOCK_INIT;

static const void *vtbl_for(const PS_ENTRY *e)
{
    const ProxyFileInfo *file = e->File;
    const void *const *methods = proxy_vtbl_methods(file, e->Proxy);
    AcquireSRWLockExclusive(&g_copies_lock);
    for (VTBL_COPY *c = g_copies; c; c = c->Next)
        if (c->Original == e->Proxy) {
            ReleaseSRWLockExclusive(&g_copies_lock);
            return c->Methods;
        }
    ULONG count = e->Stub ? e->Stub->header.DispatchTableCount : 0;
    if (count < 3)
        count = 3;
    SIZE_T head = file->TableVersion >= 2 ? sizeof(PROXY_HEAD2) : sizeof(void *);
    VTBL_COPY *c = HeapAlloc(GetProcessHeap(), 0, sizeof(*c));
    uchar *block = HeapAlloc(GetProcessHeap(), 0, head + (SIZE_T)count * sizeof(void *));
    if (!c || !block) {
        HeapFree(GetProcessHeap(), 0, c);
        HeapFree(GetProcessHeap(), 0, block);
        ReleaseSRWLockExclusive(&g_copies_lock);
        return nullptr;
    }
    memcpy(block, e->Proxy, head);
    const void **copy = (const void **)(block + head);
    for (ULONG i = 0; i < count; i++) {
        const void *entry = methods[i];
        if (entry == (const void *)(LONG_PTR)-1 && i < NDR_MAX_STUBLESS_METHODS)
            entry = NdrpStublessThunks + (SIZE_T)i * NDR_THUNK_SIZE;
        else if (entry == nullptr && i >= 3 && e->Delegated && i < NDR_MAX_DELEGATED_METHODS)
            entry = NdrpDelegateThunks + (SIZE_T)i * NDR_DELEGATE_THUNK_SIZE;
        copy[i] = entry;
    }
    c->Original = e->Proxy;
    c->Methods = copy;
    c->Next = g_copies;
    g_copies = c;
    ReleaseSRWLockExclusive(&g_copies_lock);
    return copy;
}

/* ------------------------------------------------------------------ the proxy buffer */

static HRESULT STDMETHODCALLTYPE proxy_query(IRpcProxyBuffer *This, REFIID riid, void **object)
{
    PROXY *p = PROXY_OF_BUFFER(This);
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IRpcProxyBuffer)) {
        *object = &p->lpVtbl;
    } else if (IsEqualGUID(riid, p->Iid)) {
        *object = &p->Vtbl;
    } else {
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&p->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE proxy_add_ref(IRpcProxyBuffer *This)
{
    return (ULONG)InterlockedIncrement(&PROXY_OF_BUFFER(This)->Refs);
}

static void STDMETHODCALLTYPE proxy_disconnect(IRpcProxyBuffer *This)
{
    PROXY *p = PROXY_OF_BUFFER(This);
    IRpcChannelBuffer *channel = InterlockedExchangePointer((void *volatile *)&p->Channel, nullptr);
    if (p->Delegate)
        p->Delegate->lpVtbl->Disconnect(p->Delegate);
    if (channel)
        channel->lpVtbl->Release(channel);
}

static ULONG STDMETHODCALLTYPE proxy_release(IRpcProxyBuffer *This)
{
    PROXY *p = PROXY_OF_BUFFER(This);
    LONG refs = InterlockedDecrement(&p->Refs);
    if (refs)
        return (ULONG)refs;
    proxy_disconnect(This);
    if (p->Delegate)
        p->Delegate->lpVtbl->Release(p->Delegate);
    IPSFactoryBuffer *factory = p->Factory;
    HeapFree(GetProcessHeap(), 0, p);
    if (factory)
        factory->lpVtbl->Release(factory);
    return 0;
}

static HRESULT STDMETHODCALLTYPE proxy_connect(IRpcProxyBuffer *This, IRpcChannelBuffer *channel)
{
    PROXY *p = PROXY_OF_BUFFER(This);
    if (!channel)
        return E_INVALIDARG;
    channel->lpVtbl->AddRef(channel);
    IRpcChannelBuffer *old = InterlockedExchangePointer((void *volatile *)&p->Channel, channel);
    if (old)
        old->lpVtbl->Release(old);
    if (p->Delegate) {
        HRESULT hr = p->Delegate->lpVtbl->Connect(p->Delegate, channel);
        if (SUCCEEDED(hr) && !p->DelegateInterface) {
            hr = p->Delegate->lpVtbl->QueryInterface(p->Delegate, p->DelegatedIid, &p->DelegateInterface);
            if (SUCCEEDED(hr))
                ((IUnknown *)p->DelegateInterface)->lpVtbl->Release((IUnknown *)p->DelegateInterface);
        }
        return hr;
    }
    return S_OK;
}

static const IRpcProxyBufferVtbl g_proxy_buffer_vtbl = {proxy_query, proxy_add_ref, proxy_release, proxy_connect,
                                                       proxy_disconnect};

RPCRTAPI HRESULT WINAPI IUnknown_QueryInterface_Proxy(IUnknown *This, REFIID riid, void **object)
{
    IUnknown *outer = PROXY_OF_INTERFACE(This)->Outer;
    return outer->lpVtbl->QueryInterface(outer, riid, object);
}

RPCRTAPI ULONG WINAPI IUnknown_AddRef_Proxy(IUnknown *This)
{
    IUnknown *outer = PROXY_OF_INTERFACE(This)->Outer;
    return outer->lpVtbl->AddRef(outer);
}

RPCRTAPI ULONG WINAPI IUnknown_Release_Proxy(IUnknown *This)
{
    IUnknown *outer = PROXY_OF_INTERFACE(This)->Outer;
    return outer->lpVtbl->Release(outer);
}

static HRESULT proxy_create(IPSFactoryBuffer *factory, const ProxyFileInfo **list, IUnknown *outer, REFIID riid,
                            IRpcProxyBuffer **proxy_out, void **object)
{
    PS_ENTRY e;
    if (!proxy_out || !object)
        return E_POINTER;
    *proxy_out = nullptr;
    *object = nullptr;
    if (!find_entry(list, riid, &e) || !e.Proxy)
        return E_NOINTERFACE;
    const void *vtbl = vtbl_for(&e);
    if (!vtbl)
        return E_OUTOFMEMORY;
    PROXY *p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*p));
    if (!p)
        return E_OUTOFMEMORY;
    p->Vtbl = vtbl;
    p->lpVtbl = &g_proxy_buffer_vtbl;
    p->Refs = 1;
    p->Outer = outer ? outer : (IUnknown *)&p->lpVtbl;
    p->Iid = e.Stub && e.Stub->header.piid ? e.Stub->header.piid : proxy_vtbl_iid(e.File, e.Proxy);
    p->Name = e.Name;
    p->Factory = factory;
    factory->lpVtbl->AddRef(factory);

    if (e.Delegated) {
        IPSFactoryBuffer *base = factory_of(e.Delegated);
        if (!base) {
            proxy_release((IRpcProxyBuffer *)&p->lpVtbl);
            return E_NOINTERFACE;
        }
        HRESULT hr = base->lpVtbl->CreateProxy(base, p->Outer, e.Delegated, &p->Delegate, &p->DelegateInterface);
        base->lpVtbl->Release(base);
        if (SUCCEEDED(hr) && !p->Delegate)
            hr = E_NOINTERFACE;
        if (FAILED(hr)) {
            proxy_release((IRpcProxyBuffer *)&p->lpVtbl);
            return hr;
        }
        p->DelegatedIid = e.Delegated;
        /*
         * the delegate was referenced through the outer unknown, which the interface of this proxy is going to be. A factory
         * may leave the interface to be asked for once the proxy is connected, which is when this one gets it.
         */
        if (p->DelegateInterface)
            ((IUnknown *)p->DelegateInterface)->lpVtbl->Release((IUnknown *)p->DelegateInterface);
    }
    *proxy_out = (IRpcProxyBuffer *)&p->lpVtbl;
    *object = &p->Vtbl;
    IUnknown *iface = (IUnknown *)*object;
    iface->lpVtbl->AddRef(iface);
    return S_OK;
}

/* ------------------------------------------------------------------ calls through a proxy */

static IRpcChannelBuffer *channel_of(PMIDL_STUB_MESSAGE m)
{
    return m->pRpcChannelBuffer;
}

/* the interface of the proxy a call is made on, kept in the message for the buffer calls. */
#define MESSAGE_IID(m) (*(const IID **)&(m)->Reserved51_4)

void NdrpProxyBegin(void *interface_pointer, PRPC_MESSAGE msg, PMIDL_STUB_MESSAGE m, PMIDL_STUB_DESC desc, ULONG proc_num)
{
    PROXY *p = PROXY_OF_INTERFACE(interface_pointer);
    IRpcChannelBuffer *channel = p ? p->Channel : nullptr;
    if (!channel)
        NDR_RAISE(CO_E_OBJNOTCONNECTED);
    channel->lpVtbl->AddRef(channel);
    m->pRpcChannelBuffer = channel;
    MESSAGE_IID(m) = p->Iid;
    msg->ProcNum = proc_num;
    channel->lpVtbl->GetDestCtx(channel, (DWORD *)&m->dwDestContext, &m->pvDestContext);
}

void NdrpProxyEnd(PMIDL_STUB_MESSAGE m)
{
    IRpcChannelBuffer *channel = channel_of(m);
    m->pRpcChannelBuffer = nullptr;
    if (channel)
        channel->lpVtbl->Release(channel);
}

void NdrpChannelGetBuffer(PMIDL_STUB_MESSAGE m)
{
    IRpcChannelBuffer *channel = channel_of(m);
    RPCOLEMESSAGE *msg = (RPCOLEMESSAGE *)m->RpcMsg;
    msg->cbBuffer = m->BufferLength;
    HRESULT hr = channel->lpVtbl->GetBuffer(channel, msg, MESSAGE_IID(m));
    if (FAILED(hr))
        NDR_RAISE(hr);
    m->Buffer = m->BufferStart = (uchar *)msg->pvBuffer;
    m->BufferEnd = m->BufferStart + msg->cbBuffer;
    m->fBufferValid = 1;
}

void NdrpChannelSendReceive(PMIDL_STUB_MESSAGE m)
{
    IRpcChannelBuffer *channel = channel_of(m);
    RPCOLEMESSAGE *msg = (RPCOLEMESSAGE *)m->RpcMsg;
    ULONG status = 0;
    m->fBufferValid = 0;
    HRESULT hr = channel->lpVtbl->SendReceive(channel, msg, &status);
    /* a channel that fails has seen the end of the buffer it was given. */
    if (FAILED(hr) || status)
        msg->pvBuffer = nullptr;
    if (FAILED(hr))
        NDR_RAISE(hr);
    if (status)
        NDR_RAISE(status);
    m->Buffer = m->BufferStart = (uchar *)msg->pvBuffer;
    m->BufferEnd = m->BufferStart + msg->cbBuffer;
    m->fBufferValid = 1;
}

void NdrpChannelFreeBuffer(PMIDL_STUB_MESSAGE m)
{
    IRpcChannelBuffer *channel = channel_of(m);
    RPCOLEMESSAGE *msg = (RPCOLEMESSAGE *)m->RpcMsg;
    if (channel && msg->pvBuffer)
        channel->lpVtbl->FreeBuffer(channel, msg);
    msg->pvBuffer = nullptr;
    m->fBufferValid = 0;
}

RPCRTAPI void RPC_ENTRY NdrProxyInitialize(void *This, PRPC_MESSAGE message, PMIDL_STUB_MESSAGE m, PMIDL_STUB_DESC desc,
                                           unsigned int proc_num)
{
    NdrClientInitializeNew(message, m, desc, proc_num);
    PROXY *p = PROXY_OF_INTERFACE(This);
    if (!p->Channel)
        NDR_RAISE(CO_E_OBJNOTCONNECTED);
    m->pRpcChannelBuffer = p->Channel;
    MESSAGE_IID(m) = p->Iid;
    p->Channel->lpVtbl->GetDestCtx(p->Channel, (DWORD *)&m->dwDestContext, &m->pvDestContext);
}

RPCRTAPI void RPC_ENTRY NdrProxyGetBuffer(void *This, PMIDL_STUB_MESSAGE m)
{
    NdrpChannelGetBuffer(m);
}

RPCRTAPI void RPC_ENTRY NdrProxySendReceive(void *This, MIDL_STUB_MESSAGE *m)
{
    ((RPCOLEMESSAGE *)m->RpcMsg)->cbBuffer = (ULONG)(m->Buffer - (uchar *)((RPCOLEMESSAGE *)m->RpcMsg)->pvBuffer);
    NdrpChannelSendReceive(m);
}

RPCRTAPI void RPC_ENTRY NdrProxyFreeBuffer(void *This, MIDL_STUB_MESSAGE *m)
{
    NdrpChannelFreeBuffer(m);
}

/* ------------------------------------------------------------------ the stub buffer */

static const CInterfaceStubHeader *stub_header(const CStdStubBuffer *s)
{
    return (const CInterfaceStubHeader *)((const uchar *)s->lpVtbl - sizeof(CInterfaceStubHeader));
}

RPCRTAPI HRESULT WINAPI CStdStubBuffer_QueryInterface(IRpcStubBuffer *This, REFIID riid, void **object)
{
    CStdStubBuffer *s = (CStdStubBuffer *)This;
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_IRpcStubBuffer))
        return E_NOINTERFACE;
    InterlockedIncrement(&s->RefCount);
    *object = This;
    return S_OK;
}

RPCRTAPI ULONG WINAPI CStdStubBuffer_AddRef(IRpcStubBuffer *This)
{
    return (ULONG)InterlockedIncrement(&((CStdStubBuffer *)This)->RefCount);
}

RPCRTAPI void WINAPI CStdStubBuffer_Disconnect(IRpcStubBuffer *This)
{
    STUB *s = (STUB *)This;
    IUnknown *server = InterlockedExchangePointer((void *volatile *)&s->Base.pvServerObject, nullptr);
    if (s->Delegate)
        s->Delegate->lpVtbl->Disconnect(s->Delegate);
    if (server)
        server->lpVtbl->Release(server);
}

RPCRTAPI ULONG WINAPI NdrCStdStubBuffer_Release(IRpcStubBuffer *This, IPSFactoryBuffer *factory)
{
    STUB *s = (STUB *)This;
    LONG refs = InterlockedDecrement(&s->Base.RefCount);
    if (refs)
        return (ULONG)refs;
    This->lpVtbl->Disconnect(This);
    if (s->Delegate)
        s->Delegate->lpVtbl->Release(s->Delegate);
    HeapFree(GetProcessHeap(), 0, s);
    if (factory)
        factory->lpVtbl->Release(factory);
    return 0;
}

RPCRTAPI ULONG WINAPI NdrCStdStubBuffer2_Release(IRpcStubBuffer *This, IPSFactoryBuffer *factory)
{
    return NdrCStdStubBuffer_Release(This, factory);
}

static ULONG WINAPI std_release(IRpcStubBuffer *This)
{
    return NdrCStdStubBuffer_Release(This, ((CStdStubBuffer *)This)->pPSFactory);
}

RPCRTAPI HRESULT WINAPI CStdStubBuffer_Connect(IRpcStubBuffer *This, IUnknown *server)
{
    STUB *s = (STUB *)This;
    const CInterfaceStubHeader *header = stub_header(&s->Base);
    IUnknown *object = nullptr;
    if (!server)
        return E_POINTER;
    HRESULT hr = server->lpVtbl->QueryInterface(server, header->piid, (void **)&object);
    if (FAILED(hr))
        return hr;
    IUnknown *old = InterlockedExchangePointer((void *volatile *)&s->Base.pvServerObject, object);
    if (old)
        old->lpVtbl->Release(old);
    if (s->Delegate) {
        hr = s->Delegate->lpVtbl->Connect(s->Delegate, server);
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

RPCRTAPI HRESULT WINAPI CStdStubBuffer_Invoke(IRpcStubBuffer *This, RPCOLEMESSAGE *message, IRpcChannelBuffer *channel)
{
    CStdStubBuffer *s = (CStdStubBuffer *)This;
    const CInterfaceStubHeader *header = stub_header(s);
    if (!message || !channel)
        return E_INVALIDARG;
    if (message->iMethod < 3 || message->iMethod >= header->DispatchTableCount)
        return RPC_E_INVALIDMETHOD;
    if (!s->pvServerObject)
        return CO_E_OBJNOTCONNECTED;
    DWORD phase = 0;
    HRESULT hr = S_OK;
    __try {
        /* a table midl leaves out serves every method with the interpreter, which the server information describes. */
        if (header->pDispatchTable)
            header->pDispatchTable[message->iMethod](This, channel, (PRPC_MESSAGE)message, &phase);
        else
            NdrStubCall2(This, channel, (PRPC_MESSAGE)message, &phase);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        ULONG code = __exception_code();
        /* a fault in the object itself is not the stub's: it is reported as the object's. */
        hr = phase == 1 ? RPC_E_SERVERFAULT : NdrStubErrorHandler(code);
    }
    return hr;
}

RPCRTAPI IRpcStubBuffer *WINAPI CStdStubBuffer_IsIIDSupported(IRpcStubBuffer *This, REFIID riid)
{
    CStdStubBuffer *s = (CStdStubBuffer *)This;
    if (!IsEqualGUID(riid, stub_header(s)->piid))
        return nullptr;
    return s->pvServerObject ? This : nullptr;
}

RPCRTAPI ULONG WINAPI CStdStubBuffer_CountRefs(IRpcStubBuffer *This)
{
    return ((CStdStubBuffer *)This)->pvServerObject ? 1 : 0;
}

RPCRTAPI HRESULT WINAPI CStdStubBuffer_DebugServerQueryInterface(IRpcStubBuffer *This, void **object)
{
    if (!object)
        return E_POINTER;
    *object = ((CStdStubBuffer *)This)->pvServerObject;
    return S_OK;
}

RPCRTAPI void WINAPI CStdStubBuffer_DebugServerRelease(IRpcStubBuffer *This, void *object)
{
}

static HRESULT NdrStubErrorHandler(DWORD code)
{
    return NdrProxyErrorHandler(code);
}

RPCRTAPI void __RPC_STUB NdrStubForwardingFunction(IRpcStubBuffer *This, IRpcChannelBuffer *channel, PRPC_MESSAGE message,
                                                   DWORD *phase)
{
    STUB *s = (STUB *)This;
    if (!s->Delegate)
        NDR_RAISE(RPC_E_INVALIDMETHOD);
    HRESULT hr = s->Delegate->lpVtbl->Invoke(s->Delegate, (RPCOLEMESSAGE *)message, channel);
    if (FAILED(hr))
        NDR_RAISE(hr);
}

RPCRTAPI void RPC_ENTRY NdrStubInitialize(PRPC_MESSAGE message, PMIDL_STUB_MESSAGE m, PMIDL_STUB_DESC desc,
                                          IRpcChannelBuffer *channel)
{
    NdrServerInitializeNew(message, m, desc);
    m->pRpcChannelBuffer = channel;
    channel->lpVtbl->GetDestCtx(channel, (DWORD *)&m->dwDestContext, &m->pvDestContext);
}

RPCRTAPI void RPC_ENTRY NdrStubInitializePartial(PRPC_MESSAGE message, PMIDL_STUB_MESSAGE m, PMIDL_STUB_DESC desc,
                                                 IRpcChannelBuffer *channel, ULONG requested)
{
    NdrStubInitialize(message, m, desc, channel);
}

RPCRTAPI void RPC_ENTRY NdrStubGetBuffer(IRpcStubBuffer *This, IRpcChannelBuffer *channel, PMIDL_STUB_MESSAGE m)
{
    RPCOLEMESSAGE *msg = (RPCOLEMESSAGE *)m->RpcMsg;
    msg->cbBuffer = m->BufferLength;
    m->pRpcChannelBuffer = channel;
    HRESULT hr = channel->lpVtbl->GetBuffer(channel, msg, stub_header((CStdStubBuffer *)This)->piid);
    if (FAILED(hr))
        NDR_RAISE(hr);
    m->Buffer = m->BufferStart = (uchar *)msg->pvBuffer;
    m->BufferEnd = m->BufferStart + msg->cbBuffer;
    m->fBufferValid = 1;
}

/* ------------------------------------------------------------------ the factory */

static HRESULT STDMETHODCALLTYPE factory_query(IPSFactoryBuffer *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_IPSFactoryBuffer))
        return E_NOINTERFACE;
    InterlockedIncrement(&((CStdPSFactoryBuffer *)This)->RefCount);
    *object = This;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE factory_add_ref(IPSFactoryBuffer *This)
{
    return (ULONG)InterlockedIncrement(&((CStdPSFactoryBuffer *)This)->RefCount);
}

static ULONG STDMETHODCALLTYPE factory_release(IPSFactoryBuffer *This)
{
    return (ULONG)InterlockedDecrement(&((CStdPSFactoryBuffer *)This)->RefCount);
}

static HRESULT STDMETHODCALLTYPE factory_create_proxy(IPSFactoryBuffer *This, IUnknown *outer, REFIID riid,
                                                      IRpcProxyBuffer **proxy, void **object)
{
    return proxy_create(This, ((CStdPSFactoryBuffer *)This)->pProxyFileList, outer, riid, proxy, object);
}

/* stub vtables with empty places are completed with the standard methods in a copy, made once for each. */
typedef struct STUB_COPY {
    struct STUB_COPY *Next;
    const CInterfaceStubVtbl *Original;
    CInterfaceStubVtbl *Copy;
} STUB_COPY;

static STUB_COPY *g_stub_copies;

static const IRpcStubBufferVtbl *stub_vtbl_for(const CInterfaceStubVtbl *v)
{
    const void *const *slots = (const void *const *)&v->Vtbl;
    BOOL complete = TRUE;
    for (ULONG i = 0; i < sizeof(v->Vtbl) / sizeof(void *); i++)
        if (!slots[i])
            complete = FALSE;
    if (complete)
        return &v->Vtbl;
    AcquireSRWLockExclusive(&g_copies_lock);
    for (STUB_COPY *c = g_stub_copies; c; c = c->Next)
        if (c->Original == v) {
            ReleaseSRWLockExclusive(&g_copies_lock);
            return &c->Copy->Vtbl;
        }
    STUB_COPY *c = HeapAlloc(GetProcessHeap(), 0, sizeof(*c));
    CInterfaceStubVtbl *copy = HeapAlloc(GetProcessHeap(), 0, sizeof(*copy));
    if (!c || !copy) {
        HeapFree(GetProcessHeap(), 0, c);
        HeapFree(GetProcessHeap(), 0, copy);
        ReleaseSRWLockExclusive(&g_copies_lock);
        return nullptr;
    }
    *copy = *v;
    IRpcStubBufferVtbl *t = &copy->Vtbl;
    if (!t->QueryInterface)
        t->QueryInterface = CStdStubBuffer_QueryInterface;
    if (!t->AddRef)
        t->AddRef = CStdStubBuffer_AddRef;
    if (!t->Release)
        t->Release = std_release;
    if (!t->Connect)
        t->Connect = CStdStubBuffer_Connect;
    if (!t->Disconnect)
        t->Disconnect = CStdStubBuffer_Disconnect;
    if (!t->Invoke)
        t->Invoke = CStdStubBuffer_Invoke;
    if (!t->IsIIDSupported)
        t->IsIIDSupported = CStdStubBuffer_IsIIDSupported;
    if (!t->CountRefs)
        t->CountRefs = CStdStubBuffer_CountRefs;
    if (!t->DebugServerQueryInterface)
        t->DebugServerQueryInterface = CStdStubBuffer_DebugServerQueryInterface;
    if (!t->DebugServerRelease)
        t->DebugServerRelease = CStdStubBuffer_DebugServerRelease;
    c->Original = v;
    c->Copy = copy;
    c->Next = g_stub_copies;
    g_stub_copies = c;
    ReleaseSRWLockExclusive(&g_copies_lock);
    return t;
}

static HRESULT STDMETHODCALLTYPE factory_create_stub(IPSFactoryBuffer *This, REFIID riid, IUnknown *server,
                                                     IRpcStubBuffer **stub_out)
{
    PS_ENTRY e;
    if (!stub_out)
        return E_POINTER;
    *stub_out = nullptr;
    if (!find_entry(((CStdPSFactoryBuffer *)This)->pProxyFileList, riid, &e) || !e.Stub)
        return E_NOINTERFACE;
    const IRpcStubBufferVtbl *vtbl = stub_vtbl_for(e.Stub);
    if (!vtbl)
        return E_OUTOFMEMORY;
    STUB *s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s)
        return E_OUTOFMEMORY;
    s->Base.lpVtbl = vtbl;
    s->Base.RefCount = 1;
    s->Base.pPSFactory = This;
    This->lpVtbl->AddRef(This);
    if (e.Delegated) {
        IPSFactoryBuffer *base = factory_of(e.Delegated);
        if (!base || FAILED(base->lpVtbl->CreateStub(base, e.Delegated, server, &s->Delegate))) {
            if (base)
                base->lpVtbl->Release(base);
            NdrCStdStubBuffer_Release((IRpcStubBuffer *)s, This);
            return E_NOINTERFACE;
        }
        base->lpVtbl->Release(base);
    }
    HRESULT hr = ((IRpcStubBuffer *)s)->lpVtbl->Connect((IRpcStubBuffer *)s, server);
    if (FAILED(hr)) {
        NdrCStdStubBuffer_Release((IRpcStubBuffer *)s, This);
        return hr;
    }
    *stub_out = (IRpcStubBuffer *)s;
    return S_OK;
}

static const IPSFactoryBufferVtbl g_factory_vtbl = {factory_query, factory_add_ref, factory_release,
                                                    factory_create_proxy, factory_create_stub};

RPCRTAPI HRESULT RPC_ENTRY NdrDllGetClassObject(REFCLSID clsid, REFIID iid, void **object,
                                                const ProxyFileInfo **proxy_file_list, const CLSID *proxy_clsid,
                                                CStdPSFactoryBuffer *factory)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    /* the class of the file, or an interface of its list, which proxy files register themselves under. */
    PS_ENTRY e;
    if ((!proxy_clsid || !IsEqualGUID(clsid, proxy_clsid)) && !find_entry(proxy_file_list, clsid, &e))
        return CLASS_E_CLASSNOTAVAILABLE;
    if (!factory->lpVtbl) {
        factory->lpVtbl = &g_factory_vtbl;
        factory->pProxyFileList = proxy_file_list;
        factory->RefCount = 0;
    }
    return factory->lpVtbl->QueryInterface((IPSFactoryBuffer *)factory, iid, object);
}

RPCRTAPI HRESULT RPC_ENTRY NdrDllCanUnloadNow(CStdPSFactoryBuffer *factory)
{
    return factory->RefCount > 0 ? S_FALSE : S_OK;
}

/* ------------------------------------------------------------------ registration */

typedef struct {
    LSTATUS(WINAPI *RegCreateKeyExW)(HKEY, LPCWSTR, DWORD, LPWSTR, DWORD, DWORD, void *, HKEY *, DWORD *);
    LSTATUS(WINAPI *RegSetValueExW)(HKEY, LPCWSTR, DWORD, DWORD, const BYTE *, DWORD);
    LSTATUS(WINAPI *RegCloseKey)(HKEY);
    LSTATUS(WINAPI *RegDeleteTreeW)(HKEY, LPCWSTR);
    LSTATUS(WINAPI *RegDeleteKeyW)(HKEY, LPCWSTR);
} REGISTRY_FUNCTIONS;

static const REGISTRY_FUNCTIONS *registry(void)
{
    static REGISTRY_FUNCTIONS table;
    static LONG state;
    if (state != 2) {
        if (InterlockedCompareExchange(&state, 1, 0) == 0) {
            HMODULE m = LoadLibraryW(L"advapi32.dll");
            if (m) {
                table.RegCreateKeyExW = (void *)GetProcAddress(m, "RegCreateKeyExW");
                table.RegSetValueExW = (void *)GetProcAddress(m, "RegSetValueExW");
                table.RegCloseKey = (void *)GetProcAddress(m, "RegCloseKey");
                table.RegDeleteTreeW = (void *)GetProcAddress(m, "RegDeleteTreeW");
                table.RegDeleteKeyW = (void *)GetProcAddress(m, "RegDeleteKeyW");
            }
            state = 2;
        } else {
            while (state != 2)
                Sleep(1);
        }
    }
    return &table;
}

static void format_guid(REFGUID g, WCHAR *out)
{
    static const WCHAR hex[] = L"0123456789ABCDEF";
    const uchar *b = (const uchar *)g;
    WCHAR *o = out;
    *o++ = L'{';
    static const uchar order[16] = {3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15};
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            *o++ = L'-';
        *o++ = hex[b[order[i]] >> 4];
        *o++ = hex[b[order[i]] & 15];
    }
    *o++ = L'}';
    *o = 0;
}

static LSTATUS set_default(const REGISTRY_FUNCTIONS *r, const WCHAR *path, const WCHAR *name, const WCHAR *value)
{
    HKEY key;
    LSTATUS status = r->RegCreateKeyExW(HKEY_CLASSES_ROOT, path, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
    if (status)
        return status;
    status = r->RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)value, (DWORD)((wcslen(value) + 1) * sizeof(WCHAR)));
    r->RegCloseKey(key);
    return status;
}

static void ansi_to_wide(const char *text, WCHAR *out, SIZE_T count)
{
    SIZE_T i = 0;
    for (; text && text[i] && i + 1 < count; i++)
        out[i] = (uchar)text[i];
    out[i] = 0;
}

RPCRTAPI HRESULT RPC_ENTRY NdrDllRegisterProxy(HMODULE module, const ProxyFileInfo **proxy_file_list,
                                               const CLSID *proxy_clsid)
{
    const REGISTRY_FUNCTIONS *r = registry();
    if (!r->RegCreateKeyExW || !proxy_file_list || !proxy_clsid)
        return E_UNEXPECTED;
    WCHAR clsid[40], path[MAX_PATH + 64], module_path[MAX_PATH];
    format_guid(proxy_clsid, clsid);
    if (!GetModuleFileNameW(module, module_path, MAX_PATH))
        return HRESULT_FROM_WIN32(GetLastError());

    wcscpy(path, L"CLSID\\");
    wcscat(path, clsid);
    if (set_default(r, path, nullptr, L"PSFactoryBuffer"))
        return REGDB_E_WRITEREGDB;
    wcscat(path, L"\\InprocServer32");
    if (set_default(r, path, nullptr, module_path) || set_default(r, path, L"ThreadingModel", L"Both"))
        return REGDB_E_WRITEREGDB;

    for (const ProxyFileInfo **list = proxy_file_list; *list; list++) {
        const ProxyFileInfo *file = *list;
        for (ULONG i = 0; i < file->TableSize; i++) {
            const CInterfaceStubVtbl *stub = file->pStubVtblList ? (const CInterfaceStubVtbl *)file->pStubVtblList[i] : nullptr;
            const CInterfaceProxyVtbl *proxy = file->pProxyVtblList ? (const CInterfaceProxyVtbl *)file->pProxyVtblList[i] : nullptr;
            const IID *iid = proxy ? proxy_vtbl_iid(file, proxy) : stub ? stub->header.piid : nullptr;
            if (!iid)
                continue;
            WCHAR text[40], name[128], count[16];
            format_guid(iid, text);
            ansi_to_wide(file->pNamesArray ? file->pNamesArray[i] : "", name, ARRAYSIZE(name));
            wcscpy(path, L"Interface\\");
            wcscat(path, text);
            if (set_default(r, path, nullptr, name))
                return REGDB_E_WRITEREGDB;
            WCHAR sub[MAX_PATH + 64];
            wcscpy(sub, path);
            wcscat(sub, L"\\ProxyStubClsid32");
            if (set_default(r, sub, nullptr, clsid))
                return REGDB_E_WRITEREGDB;
            if (stub) {
                ULONG n = stub->header.DispatchTableCount, len = 0;
                WCHAR digits[12];
                do {
                    digits[len++] = (WCHAR)(L'0' + n % 10);
                    n /= 10;
                } while (n);
                for (ULONG k = 0; k < len; k++)
                    count[k] = digits[len - 1 - k];
                count[len] = 0;
                wcscpy(sub, path);
                wcscat(sub, L"\\NumMethods");
                if (set_default(r, sub, nullptr, count))
                    return REGDB_E_WRITEREGDB;
            }
            if (file->pDelegatedIIDs && file->pDelegatedIIDs[i]) {
                WCHAR base[40];
                format_guid(file->pDelegatedIIDs[i], base);
                wcscpy(sub, path);
                wcscat(sub, L"\\BaseInterface");
                if (set_default(r, sub, nullptr, base))
                    return REGDB_E_WRITEREGDB;
            }
        }
    }
    return S_OK;
}

RPCRTAPI HRESULT RPC_ENTRY NdrDllUnregisterProxy(HMODULE module, const ProxyFileInfo **proxy_file_list,
                                                 const CLSID *proxy_clsid)
{
    const REGISTRY_FUNCTIONS *r = registry();
    if (!r->RegDeleteTreeW || !proxy_file_list || !proxy_clsid)
        return E_UNEXPECTED;
    WCHAR clsid[40], path[MAX_PATH + 64];
    for (const ProxyFileInfo **list = proxy_file_list; *list; list++) {
        const ProxyFileInfo *file = *list;
        for (ULONG i = 0; i < file->TableSize; i++) {
            const CInterfaceStubVtbl *stub = file->pStubVtblList ? (const CInterfaceStubVtbl *)file->pStubVtblList[i] : nullptr;
            const CInterfaceProxyVtbl *proxy = file->pProxyVtblList ? (const CInterfaceProxyVtbl *)file->pProxyVtblList[i] : nullptr;
            const IID *iid = proxy ? proxy_vtbl_iid(file, proxy) : stub ? stub->header.piid : nullptr;
            if (!iid)
                continue;
            wcscpy(path, L"Interface\\");
            format_guid(iid, path + wcslen(path));
            r->RegDeleteTreeW(HKEY_CLASSES_ROOT, path);
        }
    }
    format_guid(proxy_clsid, clsid);
    wcscpy(path, L"CLSID\\");
    wcscat(path, clsid);
    r->RegDeleteTreeW(HKEY_CLASSES_ROOT, path);
    return S_OK;
}

/* ------------------------------------------------------------------ stubless proxies */

/*
 * A call of a method that midl left to the interpreter: the thunk of its index has put the arguments in a frame, and the
 * proxy info in the head of the vtable says which procedure it is. On x64 a float or double that arrived in one of the first
 * four registers is in fp, and replaces what the integer register held. On x86 the thunk takes the arguments off the stack
 * after the call, and needs to be told how many bytes they are.
 */
#if defined(__aarch64__)
#define NDR_FP_SLOTS 8
#else
#define NDR_FP_SLOTS 4
#endif

LONG_PTR NdrpStublessClient(ULONG method, uchar *args, const ULONG_PTR *fp, ULONG *pop)
{
    void *This = *(void **)args;
    const void *const *vtbl = *(const void *const **)This;
    const MIDL_STUBLESS_PROXY_INFO *info = ((const MIDL_STUBLESS_PROXY_INFO *const *)vtbl)[-2];
    PFORMAT_STRING format = info->ProcFormatString + info->FormatStringOffset[method];
    NDR_PROC p;
    NdrpParseProc(format, &p);
    /* the frame of the procedure holds a slot for the return value, which is not on the stack of the caller. */
    if (pop)
        *pop = p.StackSize - ((p.Oi2Flags & OIF_HAS_RETURN) ? sizeof(void *) : 0);
    if (fp && p.FloatMask) {
        for (ULONG i = 0; i < NDR_FP_SLOTS; i++)
            if ((p.FloatMask >> (2 * i)) & 3)
                ((ULONG_PTR *)args)[i] = fp[i];
    }
    return NdrpClientCall(info->pStubDesc, format, args, FALSE).Simple;
}

/* ------------------------------------------------------------------ interface pointers in the buffer */

typedef struct MEMSTREAM {
    const IStreamVtbl *lpVtbl;
    LONG Refs;
    uchar *Data;
    ULONG Size;
    ULONG Position;
} MEMSTREAM;

static HRESULT STDMETHODCALLTYPE ms_query(IStream *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IStream) || IsEqualGUID(riid, &IID_ISequentialStream)) {
        *object = This;
        InterlockedIncrement(&((MEMSTREAM *)This)->Refs);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE ms_add_ref(IStream *This)
{
    return (ULONG)InterlockedIncrement(&((MEMSTREAM *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE ms_release(IStream *This)
{
    MEMSTREAM *s = (MEMSTREAM *)This;
    LONG refs = InterlockedDecrement(&s->Refs);
    if (!refs)
        HeapFree(GetProcessHeap(), 0, s);
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE ms_read(IStream *This, void *buffer, ULONG size, ULONG *done)
{
    MEMSTREAM *s = (MEMSTREAM *)This;
    ULONG n = s->Size - s->Position < size ? s->Size - s->Position : size;
    memcpy(buffer, s->Data + s->Position, n);
    s->Position += n;
    if (done)
        *done = n;
    return n == size ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE ms_write(IStream *This, const void *buffer, ULONG size, ULONG *done)
{
    MEMSTREAM *s = (MEMSTREAM *)This;
    if (s->Size - s->Position < size) {
        if (done)
            *done = 0;
        return STG_E_MEDIUMFULL;
    }
    memcpy(s->Data + s->Position, buffer, size);
    s->Position += size;
    if (done)
        *done = size;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ms_seek(IStream *This, LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER *position)
{
    MEMSTREAM *s = (MEMSTREAM *)This;
    LONGLONG base = origin == 0 ? 0 : origin == 1 ? s->Position : s->Size;
    LONGLONG target = base + move.QuadPart;
    if (target < 0 || target > s->Size)
        return STG_E_INVALIDFUNCTION;
    s->Position = (ULONG)target;
    if (position)
        position->QuadPart = (ULONGLONG)target;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ms_set_size(IStream *This, ULARGE_INTEGER size)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE ms_copy_to(IStream *This, IStream *to, ULARGE_INTEGER n, ULARGE_INTEGER *read,
                                            ULARGE_INTEGER *written)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE ms_commit(IStream *This, DWORD flags)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ms_revert(IStream *This)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ms_lock(IStream *This, ULARGE_INTEGER offset, ULARGE_INTEGER n, DWORD type)
{
    return STG_E_INVALIDFUNCTION;
}

static HRESULT STDMETHODCALLTYPE ms_stat(IStream *This, STATSTG *stat, DWORD flags)
{
    MEMSTREAM *s = (MEMSTREAM *)This;
    if (!stat)
        return STG_E_INVALIDPOINTER;
    memset(stat, 0, sizeof(*stat));
    stat->type = 2;
    stat->cbSize.QuadPart = s->Size;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ms_clone(IStream *This, IStream **clone)
{
    return E_NOTIMPL;
}

static const IStreamVtbl g_memstream_vtbl = {ms_query, ms_add_ref, ms_release, ms_read, ms_write, ms_seek, ms_set_size,
                                             ms_copy_to, ms_commit, ms_revert, ms_lock, ms_lock, ms_stat, ms_clone};

static MEMSTREAM *memstream_new(uchar *data, ULONG size)
{
    MEMSTREAM *s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s)
        NDR_RAISE(RPC_S_OUT_OF_MEMORY);
    s->lpVtbl = &g_memstream_vtbl;
    s->Refs = 1;
    s->Data = data;
    s->Size = size;
    return s;
}

/* the interface of a pointer: a constant given with the pointer, or what a parameter holds. */
static void interface_iid(NDRC *c, PFORMAT_STRING f, IID *iid)
{
    PMIDL_STUB_MESSAGE m = c->M;
    if (f[1] == FC_CONSTANT_IID) {
        memcpy(iid, f + 2, sizeof(*iid));
        return;
    }
    ULONG_PTR max = m->MaxCount;
    ULONG_PTR value = NdrpComputeConformance(m, nullptr, f + 2);
    m->MaxCount = max;
    if (!value)
        NDR_RAISE(RPC_X_NULL_REF_POINTER);
    memcpy(iid, (const void *)value, sizeof(*iid));
}

static void hook_size(NDRC *c, uchar *pointer, PFORMAT_STRING f)
{
    const OLE32_FUNCTIONS *o = ole32();
    IID iid;
    ULONG max = 0;
    if (!o->CoGetMarshalSizeMax)
        NDR_RAISE(RPC_E_UNEXPECTED);
    interface_iid(c, f, &iid);
    HRESULT hr = o->CoGetMarshalSizeMax(&max, &iid, (IUnknown *)pointer, c->M->dwDestContext, c->M->pvDestContext,
                                        MSHLFLAGS_NORMAL);
    if (FAILED(hr))
        NDR_RAISE(hr);
    NdrpAlign(c, 3);
    NdrpTake(c, 8);
    NdrpTake(c, max);
}

static void hook_marshal(NDRC *c, uchar *pointer, PFORMAT_STRING f)
{
    PMIDL_STUB_MESSAGE m = c->M;
    const OLE32_FUNCTIONS *o = ole32();
    IID iid;
    if (!o->CoMarshalInterface)
        NDR_RAISE(RPC_E_UNEXPECTED);
    interface_iid(c, f, &iid);
    NdrpAlign(c, 3);
    ULONG *counts = (ULONG *)NdrpTake(c, 8);
    ULONG room = (ULONG)(m->BufferEnd - m->Buffer);
    MEMSTREAM *s = memstream_new(m->Buffer, room);
    HRESULT hr = o->CoMarshalInterface((IStream *)s, &iid, (IUnknown *)pointer, m->dwDestContext, m->pvDestContext,
                                       MSHLFLAGS_NORMAL);
    ULONG used = s->Position;
    ms_release((IStream *)s);
    if (FAILED(hr))
        NDR_RAISE(hr);
    counts[0] = used;
    counts[1] = used;
    m->Buffer += used;
}

static void hook_unmarshal(NDRC *c, uchar **slot, PFORMAT_STRING f)
{
    PMIDL_STUB_MESSAGE m = c->M;
    const OLE32_FUNCTIONS *o = ole32();
    IID iid;
    if (!o->CoUnmarshalInterface)
        NDR_RAISE(RPC_E_UNEXPECTED);
    interface_iid(c, f, &iid);
    NdrpAlign(c, 3);
    ULONG *counts = (ULONG *)NdrpTake(c, 8);
    ULONG size = counts[1];
    if (counts[0] != size || size > (ULONG)(m->BufferEnd - m->Buffer))
        NDR_RAISE(RPC_X_BAD_STUB_DATA);
    MEMSTREAM *s = memstream_new(m->Buffer, size);
    void *object = nullptr;
    HRESULT hr = o->CoUnmarshalInterface((IStream *)s, &iid, &object);
    ms_release((IStream *)s);
    m->Buffer += size;
    if (FAILED(hr))
        NDR_RAISE(hr);
    *slot = (uchar *)object;
}

static void hook_skip(NDRC *c)
{
    NdrpAlign(c, 3);
    uchar *counts = NdrpTake(c, 8);
    ULONG size = ((ULONG *)counts)[1];
    if (size > (ULONG)(c->M->BufferEnd - c->M->Buffer))
        NDR_RAISE(RPC_X_BAD_STUB_DATA);
    c->M->Buffer += size;
}

static void hook_free(NDRC *c, uchar *pointer, PFORMAT_STRING f)
{
    if (pointer)
        ((IUnknown *)pointer)->lpVtbl->Release((IUnknown *)pointer);
}

static const NDR_OLE_HOOKS g_hooks = {hook_size, hook_marshal, hook_unmarshal, hook_free, hook_skip};

const NDR_OLE_HOOKS *NdrpOleHooks(void)
{
    return &g_hooks;
}
