/*
 * objects of other processes as seen from this one: one identity manager per object, the IUnknown of all its interface
 * proxies, holding the references the server gave for them.
 */
#include "orpc.h"

enum { REMUNK_QUERY_INTERFACE = 3, REMUNK_ADD_REF = 4, REMUNK_RELEASE = 5 };

IPSFactoryBuffer *OrpcFactoryFor(REFIID iid);

typedef struct IFPROXY {
    struct IFPROXY *Next;
    GUID Ipid;
    IID Iid;
    IRpcProxyBuffer *Proxy;
    void *Interface;
    LONG PublicRefs;
} IFPROXY;

typedef struct PROXYMGR {
    const IUnknownVtbl *lpVtbl;
    LONG Refs;
    struct PROXYMGR *Next;
    ULONG64 Oxid, Oid;
    OXID_ENTRY *Server;
    IFPROXY *Interfaces;
    SRWLOCK Lock;
} PROXYMGR;

static SRWLOCK g_manager_lock = SRWLOCK_INIT;
static PROXYMGR *g_managers;

/* ------------------------------------------------------------------ oxid entries */

static OXID_ENTRY *g_oxids;
static SRWLOCK g_oxid_lock = SRWLOCK_INIT;

OXID_ENTRY *OrpcOxidEntry(ULONG64 oxid, const WCHAR *endpoint)
{
    OXID_ENTRY *found = nullptr;
    AcquireSRWLockExclusive(&g_oxid_lock);
    for (OXID_ENTRY *e = g_oxids; e; e = e->Next)
        if (e->Oxid == oxid) {
            found = e;
            break;
        }
    /* the same process id with another endpoint is another process. */
    if (found && wcscmp(found->Endpoint, endpoint)) {
        RPC_BINDING_HANDLE old = found->Binding;
        found->Binding = nullptr;
        wcscpy(found->Endpoint, endpoint);
        if (old)
            RpcBindingFree(&old);
    }
    if (!found) {
        found = OleAllocZero(sizeof(*found));
        if (found) {
            found->Oxid = oxid;
            wcscpy(found->Endpoint, endpoint);
            found->Next = g_oxids;
            g_oxids = found;
        }
    }
    if (found && !found->Binding) {
        RPC_WSTR text = nullptr;
        if (!RpcStringBindingComposeW(nullptr, (RPC_WSTR)L"ncalrpc", nullptr, (RPC_WSTR)found->Endpoint, nullptr, &text)) {
            RpcBindingFromStringBindingW(text, &found->Binding);
            RpcStringFreeW(&text);
        }
    }
    ReleaseSRWLockExclusive(&g_oxid_lock);
    return found && found->Binding ? found : nullptr;
}

/* ------------------------------------------------------------------ reference counting calls */

static void put_u32(BYTE *p, ULONG v)
{
    memcpy(p, &v, 4);
}

/* returns the references an interface proxy holds. */
static void rem_release(OXID_ENTRY *server, const GUID *ipid, ULONG refs)
{
    if (!refs)
        return;
    BYTE in[4 + 24];
    BYTE *out = nullptr;
    ULONG out_size = 0;
    put_u32(in, 1);
    memcpy(in + 4, ipid, 16);
    put_u32(in + 20, refs);
    put_u32(in + 24, 0);
    OrpcRemCall(server, REMUNK_RELEASE, in, sizeof(in), &out, &out_size);
    OleFree(out);
}

static HRESULT rem_add_ref(OXID_ENTRY *server, const GUID *ipid, ULONG refs)
{
    BYTE in[4 + 24];
    BYTE *out = nullptr;
    ULONG out_size = 0;
    put_u32(in, 1);
    memcpy(in + 4, ipid, 16);
    put_u32(in + 20, refs);
    put_u32(in + 24, 0);
    HRESULT hr = OrpcRemCall(server, REMUNK_ADD_REF, in, sizeof(in), &out, &out_size);
    if (SUCCEEDED(hr)) {
        hr = E_FAIL;
        if (out_size >= 8) {
            ULONG overall, item;
            memcpy(&overall, out, 4);
            memcpy(&item, out + 4, 4);
            hr = FAILED((HRESULT)overall) ? (HRESULT)overall : (HRESULT)item;
        }
    }
    OleFree(out);
    return hr;
}

/* ------------------------------------------------------------------ interface proxies */

static HRESULT manager_add_interface(PROXYMGR *m, REFIID iid, const GUID *ipid, ULONG refs, IFPROXY **result)
{
    IFPROXY *ifp = OleAllocZero(sizeof(*ifp));
    if (!ifp)
        return E_OUTOFMEMORY;
    ifp->Ipid = *ipid;
    ifp->Iid = *iid;
    ifp->PublicRefs = (LONG)refs;
    if (!IsEqualIID(iid, &IID_IUnknown)) {
        IPSFactoryBuffer *factory = OrpcFactoryFor(iid);
        if (!factory) {
            OleFree(ifp);
            return E_NOINTERFACE;
        }
        IRpcChannelBuffer *channel;
        HRESULT hr = OrpcChannelCreate(m->Server, ipid, iid, &channel);
        if (SUCCEEDED(hr)) {
            hr = factory->lpVtbl->CreateProxy(factory, (IUnknown *)m, iid, &ifp->Proxy, &ifp->Interface);
            /*
             * windows' oleaut32 factory makes a wrapper that has no interface to give until it is connected, and makes
             * the proxy it stands for then.
             */
            BOOL connected = FALSE;
            if (SUCCEEDED(hr) && ifp->Proxy && !ifp->Interface) {
                hr = ifp->Proxy->lpVtbl->Connect(ifp->Proxy, channel);
                connected = SUCCEEDED(hr);
                if (SUCCEEDED(hr))
                    hr = ifp->Proxy->lpVtbl->QueryInterface(ifp->Proxy, iid, &ifp->Interface);
            }
            if (SUCCEEDED(hr) && (!ifp->Proxy || !ifp->Interface))
                hr = E_NOINTERFACE;
            if (SUCCEEDED(hr)) {
                /* the interface was referenced through the manager, which the caller keeps alive. */
                ((IUnknown *)ifp->Interface)->lpVtbl->Release((IUnknown *)ifp->Interface);
                if (!connected)
                    hr = ifp->Proxy->lpVtbl->Connect(ifp->Proxy, channel);
            }
            channel->lpVtbl->Release(channel);
        }
        factory->lpVtbl->Release(factory);
        if (FAILED(hr)) {
            if (ifp->Proxy) {
                ifp->Proxy->lpVtbl->Disconnect(ifp->Proxy);
                ifp->Proxy->lpVtbl->Release(ifp->Proxy);
            }
            OleFree(ifp);
            return hr;
        }
    }
    AcquireSRWLockExclusive(&m->Lock);
    IFPROXY *existing = nullptr;
    for (IFPROXY *i = m->Interfaces; i; i = i->Next)
        if (IsEqualIID(&i->Iid, iid)) {
            existing = i;
            break;
        }
    if (existing) {
        existing->PublicRefs += (LONG)refs;
        ReleaseSRWLockExclusive(&m->Lock);
        if (ifp->Proxy) {
            ifp->Proxy->lpVtbl->Disconnect(ifp->Proxy);
            ifp->Proxy->lpVtbl->Release(ifp->Proxy);
        }
        OleFree(ifp);
        *result = existing;
        return S_OK;
    }
    ifp->Next = m->Interfaces;
    m->Interfaces = ifp;
    ReleaseSRWLockExclusive(&m->Lock);
    *result = ifp;
    return S_OK;
}

static IFPROXY *manager_find(PROXYMGR *m, REFIID iid)
{
    IFPROXY *found = nullptr;
    AcquireSRWLockShared(&m->Lock);
    for (IFPROXY *i = m->Interfaces; i; i = i->Next)
        if (IsEqualIID(&i->Iid, iid)) {
            found = i;
            break;
        }
    ReleaseSRWLockShared(&m->Lock);
    return found;
}

/* ------------------------------------------------------------------ identity */

static HRESULT STDMETHODCALLTYPE manager_query(IUnknown *This, REFIID riid, void **object);
static ULONG STDMETHODCALLTYPE manager_add_ref(IUnknown *This);
static ULONG STDMETHODCALLTYPE manager_release(IUnknown *This);

static const IUnknownVtbl g_manager_vtbl = {manager_query, manager_add_ref, manager_release};

static ULONG STDMETHODCALLTYPE manager_add_ref(IUnknown *This)
{
    return (ULONG)InterlockedIncrement(&((PROXYMGR *)This)->Refs);
}

static void manager_destroy(PROXYMGR *m)
{
    IFPROXY *i = m->Interfaces;
    m->Interfaces = nullptr;
    while (i) {
        IFPROXY *next = i->Next;
        if (i->Proxy) {
            i->Proxy->lpVtbl->Disconnect(i->Proxy);
            i->Proxy->lpVtbl->Release(i->Proxy);
        }
        rem_release(m->Server, &i->Ipid, (ULONG)i->PublicRefs);
        OleFree(i);
        i = next;
    }
    OleFree(m);
}

static ULONG STDMETHODCALLTYPE manager_release(IUnknown *This)
{
    PROXYMGR *m = (PROXYMGR *)This;
    LONG refs = InterlockedDecrement(&m->Refs);
    if (refs)
        return (ULONG)refs;
    AcquireSRWLockExclusive(&g_manager_lock);
    if (m->Refs) {
        ReleaseSRWLockExclusive(&g_manager_lock);
        return (ULONG)m->Refs;
    }
    for (PROXYMGR **link = &g_managers; *link; link = &(*link)->Next)
        if (*link == m) {
            *link = m->Next;
            break;
        }
    ReleaseSRWLockExclusive(&g_manager_lock);
    manager_destroy(m);
    return 0;
}

/* an interface the proxy lacks is queried from the object. */
static HRESULT manager_remote_query(PROXYMGR *m, REFIID riid, IFPROXY **result)
{
    IFPROXY *any = m->Interfaces;
    if (!any)
        return E_NOINTERFACE;
    BYTE in[24 + 16];
    BYTE *out = nullptr;
    ULONG out_size = 0;
    memcpy(in, &any->Ipid, 16);
    put_u32(in + 16, ORPC_PUBLIC_REFS);
    put_u32(in + 20, 1);
    memcpy(in + 24, riid, 16);
    HRESULT hr = OrpcRemCall(m->Server, REMUNK_QUERY_INTERFACE, in, sizeof(in), &out, &out_size);
    if (FAILED(hr)) {
        OleFree(out);
        return hr;
    }
    hr = E_NOINTERFACE;
    if (out_size >= 4 + 4 + sizeof(STDOBJREF)) {
        ULONG overall, item;
        STDOBJREF std;
        memcpy(&overall, out, 4);
        memcpy(&item, out + 4, 4);
        memcpy(&std, out + 8, sizeof(std));
        if (FAILED((HRESULT)overall))
            hr = (HRESULT)overall;
        else if (FAILED((HRESULT)item))
            hr = (HRESULT)item;
        else {
            hr = manager_add_interface(m, riid, &std.Ipid, std.PublicRefs, result);
            if (FAILED(hr))
                rem_release(m->Server, &std.Ipid, std.PublicRefs);
        }
    }
    OleFree(out);
    return hr;
}

static HRESULT STDMETHODCALLTYPE manager_query(IUnknown *This, REFIID riid, void **object)
{
    PROXYMGR *m = (PROXYMGR *)This;
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IMimeProxyManager)) {
        *object = This;
        manager_add_ref(This);
        return S_OK;
    }
    IFPROXY *ifp = manager_find(m, riid);
    if (!ifp) {
        HRESULT hr = manager_remote_query(m, riid, &ifp);
        if (FAILED(hr))
            return hr;
    }
    if (!ifp->Interface)
        return E_NOINTERFACE;
    *object = ifp->Interface;
    manager_add_ref(This);
    return S_OK;
}

struct PROXYMGR *OrpcProxyManagerOf(IUnknown *object)
{
    PROXYMGR *m = nullptr;
    if (FAILED(object->lpVtbl->QueryInterface(object, &IID_IMimeProxyManager, (void **)&m)))
        return nullptr;
    return m;
}

void OrpcProxyManagerRelease(struct PROXYMGR *manager)
{
    manager_release((IUnknown *)manager);
}

/* marshals a proxy on: a reference to its object, carrying references of its own. */
HRESULT OrpcMarshalProxy(struct PROXYMGR *m, IStream *stream, REFIID iid, DWORD flags)
{
    IFPROXY *ifp = manager_find(m, iid);
    if (!ifp) {
        HRESULT hr = manager_remote_query(m, iid, &ifp);
        if (FAILED(hr))
            return hr;
        /* the new reference carries the references from the query. */
        ifp->PublicRefs -= ORPC_PUBLIC_REFS;
    } else {
        HRESULT hr = rem_add_ref(m->Server, &ifp->Ipid, ORPC_PUBLIC_REFS);
        if (FAILED(hr))
            return hr;
    }
    STDOBJREF std = {};
    std.PublicRefs = ORPC_PUBLIC_REFS;
    std.Oxid = m->Oxid;
    std.Oid = m->Oid;
    std.Ipid = ifp->Ipid;
    OBJREF_HEAD head = {OBJREF_SIGNATURE, OBJREF_STANDARD, *iid};
    ULONG done = 0;
    HRESULT hr = stream->lpVtbl->Write(stream, &head, sizeof(head), &done);
    if (SUCCEEDED(hr))
        hr = stream->lpVtbl->Write(stream, &std, sizeof(std), &done);
    if (SUCCEEDED(hr)) {
        SIZE_T length = wcslen(m->Server->Endpoint);
        USHORT entries = (USHORT)(1 + length + 1 + 1 + 1 + 1);
        USHORT counts[2] = {entries, (USHORT)(1 + length + 1 + 1)};
        USHORT tower = NCALRPC_TOWER_ID, zeros[3] = {0, 0, 0};
        hr = stream->lpVtbl->Write(stream, counts, sizeof(counts), &done);
        if (SUCCEEDED(hr))
            hr = stream->lpVtbl->Write(stream, &tower, sizeof(tower), &done);
        if (SUCCEEDED(hr))
            hr = stream->lpVtbl->Write(stream, m->Server->Endpoint, (ULONG)((length + 1) * sizeof(WCHAR)), &done);
        if (SUCCEEDED(hr))
            hr = stream->lpVtbl->Write(stream, zeros, sizeof(zeros), &done);
    }
    if (FAILED(hr))
        rem_release(m->Server, &ifp->Ipid, ORPC_PUBLIC_REFS);
    return hr;
}

/* ------------------------------------------------------------------ unmarshaling an object of another process */

HRESULT OrpcProxyUnmarshal(const STDOBJREF *std, const WCHAR *endpoint, REFIID object_iid, REFIID wanted, void **object)
{
    OXID_ENTRY *server = OrpcOxidEntry(std->Oxid, endpoint);
    if (!server)
        return RPC_E_SERVER_DIED_DNE_HR;
    ULONG refs = std->PublicRefs;
    BOOL table = (std->Flags & 3) != 0;
    if (table || !refs) {
        HRESULT hr = rem_add_ref(server, &std->Ipid, ORPC_PUBLIC_REFS);
        if (FAILED(hr))
            return hr;
        refs = ORPC_PUBLIC_REFS;
    }

    PROXYMGR *m = nullptr;
    AcquireSRWLockExclusive(&g_manager_lock);
    for (PROXYMGR *c = g_managers; c; c = c->Next)
        if (c->Oid == std->Oid && c->Oxid == std->Oxid) {
            m = c;
            InterlockedIncrement(&m->Refs);
            break;
        }
    if (!m) {
        m = OleAllocZero(sizeof(*m));
        if (m) {
            m->lpVtbl = &g_manager_vtbl;
            m->Refs = 1;
            m->Oxid = std->Oxid;
            m->Oid = std->Oid;
            m->Server = server;
            m->Lock = (SRWLOCK)SRWLOCK_INIT;
            m->Next = g_managers;
            g_managers = m;
        }
    }
    ReleaseSRWLockExclusive(&g_manager_lock);
    if (!m) {
        rem_release(server, &std->Ipid, refs);
        return E_OUTOFMEMORY;
    }
    IFPROXY *ifp;
    HRESULT hr = manager_add_interface(m, object_iid, &std->Ipid, refs, &ifp);
    if (FAILED(hr)) {
        rem_release(server, &std->Ipid, refs);
        manager_release((IUnknown *)m);
        return hr;
    }
    hr = manager_query((IUnknown *)m, wanted, object);
    manager_release((IUnknown *)m);
    return hr;
}
