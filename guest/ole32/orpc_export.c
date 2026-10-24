/*
 * objects of this process that other processes call: identity managers with a stub per handed out interface, their
 * apartments, the reference counting interfaces, and dispatch of calls arriving over rpc.
 */
#include "orpc.h"


enum { REMUNK_QUERY_INTERFACE = 3, REMUNK_ADD_REF = 4, REMUNK_RELEASE = 5 };

/* ------------------------------------------------------------------ apartments */

static SRWLOCK g_apartment_lock = SRWLOCK_INIT;
static APARTMENT *g_apartments;
static APARTMENT g_mta;
static LONG g_apartment_serial = 1;

#define WM_ORPC (WM_USER + 0x4F52)

/* a call waiting for an apartment's thread to run it. */
typedef struct APT_ITEM {
    struct APT_ITEM *Next;
    ORPC_RUN Function;
    void *Context;
    HANDLE Done;
    APARTMENT *Apartment;
} APT_ITEM;

/* calls waiting for apartment threads, and per apartment the event that wakes its thread inside a call. */
typedef struct APT_QUEUE {
    struct APT_QUEUE *Next;
    APARTMENT *Apartment;
    APT_ITEM *Head;
    HANDLE Wake;
} APT_QUEUE;

static APT_QUEUE *g_queues;
static SRWLOCK g_queue_lock = SRWLOCK_INIT;

static APT_QUEUE *queue_of(APARTMENT *a)
{
    AcquireSRWLockShared(&g_queue_lock);
    APT_QUEUE *q = g_queues;
    while (q && q->Apartment != a)
        q = q->Next;
    ReleaseSRWLockShared(&g_queue_lock);
    return q;
}

static ULONG64 make_oxid(ULONG serial)
{
    return ((ULONG64)GetCurrentProcessId() << 32) | serial;
}

static LRESULT CALLBACK apartment_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

/* runs the calls waiting for the calling thread's apartment. */
static void apartment_pump(APARTMENT *a)
{
    APT_QUEUE *q = queue_of(a);
    if (!q)
        return;
    for (;;) {
        APT_ITEM *item;
        AcquireSRWLockExclusive(&g_queue_lock);
        item = q->Head;
        if (item)
            q->Head = item->Next;
        ReleaseSRWLockExclusive(&g_queue_lock);
        if (!item)
            return;
        HANDLE done = item->Done;
        item->Function(item->Context);
        SetEvent(done);
    }
}

static LRESULT CALLBACK apartment_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_ORPC) {
        OLE_THREAD *t = OleThreadState(FALSE);
        if (t && t->Apartment)
            apartment_pump(t->Apartment);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static HWND make_window(void)
{
    static const WCHAR class_name[] = L"OleMainThreadWndClass";
    HINSTANCE module = GetModuleHandleW(L"ole32.dll");
    WNDCLASSW wc = {};
    wc.lpfnWndProc = apartment_proc;
    wc.hInstance = module;
    wc.lpszClassName = class_name;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return nullptr;
    return CreateWindowExW(0, class_name, nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, module, nullptr);
}

APARTMENT *OrpcCurrentApartment(void)
{
    OLE_THREAD *t = OleThreadState(FALSE);
    if (!t || !t->ComInits || !t->Sta) {
        if (!g_mta.Oxid) {
            AcquireSRWLockExclusive(&g_apartment_lock);
            if (!g_mta.Oxid) {
                g_mta.Mta = TRUE;
                g_mta.Oxid = make_oxid(1);
            }
            ReleaseSRWLockExclusive(&g_apartment_lock);
        }
        return &g_mta;
    }
    if (t->Apartment)
        return t->Apartment;
    APARTMENT *a = OleAllocZero(sizeof(*a));
    APT_QUEUE *q = OleAllocZero(sizeof(*q));
    HANDLE wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!a || !q || !wake) {
        OleFree(a);
        OleFree(q);
        if (wake)
            CloseHandle(wake);
        return nullptr;
    }
    a->Oxid = make_oxid((ULONG)InterlockedIncrement(&g_apartment_serial) + 1);
    a->ThreadId = GetCurrentThreadId();
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &a->Thread, SYNCHRONIZE, FALSE, 0);
    a->Window = make_window();
    q->Apartment = a;
    q->Wake = wake;
    t->Apartment = a;
    AcquireSRWLockExclusive(&g_apartment_lock);
    a->Next = g_apartments;
    g_apartments = a;
    ReleaseSRWLockExclusive(&g_apartment_lock);
    AcquireSRWLockExclusive(&g_queue_lock);
    q->Next = g_queues;
    g_queues = q;
    ReleaseSRWLockExclusive(&g_queue_lock);
    return a;
}

APARTMENT *OrpcApartmentOf(ULONG64 oxid)
{
    if (g_mta.Oxid == oxid)
        return &g_mta;
    APARTMENT *found = nullptr;
    AcquireSRWLockShared(&g_apartment_lock);
    for (APARTMENT *a = g_apartments; a; a = a->Next)
        if (a->Oxid == oxid && !a->Dead) {
            found = a;
            break;
        }
    ReleaseSRWLockShared(&g_apartment_lock);
    return found;
}

static void export_disconnect_apartment(APARTMENT *a);

void OrpcApartmentLeave(OLE_THREAD *t)
{
    APARTMENT *a = t->Apartment;
    if (!a)
        return;
    t->Apartment = nullptr;
    export_disconnect_apartment(a);
    a->Dead = TRUE;
    if (a->Window)
        DestroyWindow(a->Window);
    a->Window = nullptr;
    /* calls still waiting for the thread are answered as if it were gone. */
    APT_QUEUE *q = queue_of(a);
    if (q) {
        AcquireSRWLockExclusive(&g_queue_lock);
        APT_ITEM *item = q->Head;
        q->Head = nullptr;
        ReleaseSRWLockExclusive(&g_queue_lock);
        while (item) {
            APT_ITEM *next = item->Next;
            SetEvent(item->Done);
            item = next;
        }
    }
    /* the structure stays, since calls in flight may still look at it. */
}

HRESULT OrpcRunInApartment(APARTMENT *a, ORPC_RUN function, void *context)
{
    if (a->Dead)
        return RPC_E_DISCONNECTED_HR;
    if (a->Mta || a->ThreadId == GetCurrentThreadId()) {
        function(context);
        return S_OK;
    }
    if (a->Dead)
        return RPC_E_DISCONNECTED_HR;
    APT_QUEUE *q = queue_of(a);
    if (!q)
        return RPC_E_DISCONNECTED_HR;
    OLE_THREAD *me = OleThreadState(TRUE);
    /* nested apartment callbacks must each retain their own completion event. */
    HANDLE done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!done)
        return E_OUTOFMEMORY;
    APT_ITEM item = {nullptr, function, context, done, a};
    AcquireSRWLockExclusive(&g_queue_lock);
    if (a->Dead) {
        ReleaseSRWLockExclusive(&g_queue_lock);
        CloseHandle(done);
        return RPC_E_DISCONNECTED_HR;
    }
    APT_ITEM **link = &q->Head;
    while (*link)
        link = &(*link)->Next;
    *link = &item;
    ReleaseSRWLockExclusive(&g_queue_lock);
    SetEvent(q->Wake);
    if (a->Window)
        PostMessageW(a->Window, WM_ORPC, 0, 0);
    HANDLE waits[2] = {done, a->Thread};
    DWORD got = OrpcWait(2, waits, INFINITE, me ? me->Apartment : nullptr);
    if (got == WAIT_OBJECT_0) {
        CloseHandle(done);
        return S_OK;
    }
    /* the thread went away with the call still queued. */
    AcquireSRWLockExclusive(&g_queue_lock);
    for (link = &q->Head; *link; link = &(*link)->Next)
        if (*link == &item) {
            *link = item.Next;
            break;
        }
    ReleaseSRWLockExclusive(&g_queue_lock);
    CloseHandle(done);
    return RPC_E_SERVER_DIED_DNE_HR;
}

/*
 * while a single threaded apartment's thread waits for a reply, calls to its apartment still run, like windows' modal
 * loop does.
 */
DWORD WINAPI OrpcWait(DWORD count, const HANDLE *handles, DWORD timeout, void *context)
{
    APARTMENT *a = context;
    APT_QUEUE *q = a ? queue_of(a) : nullptr;
    if (!q || count >= MAXIMUM_WAIT_OBJECTS)
        return WaitForMultipleObjects(count, handles, FALSE, timeout);
    HANDLE all[MAXIMUM_WAIT_OBJECTS];
    memcpy(all, handles, count * sizeof(HANDLE));
    all[count] = q->Wake;
    ULONGLONG end = timeout == INFINITE ? 0 : GetTickCount64() + timeout;
    for (;;) {
        DWORD remaining = INFINITE;
        if (timeout != INFINITE) {
            ULONGLONG now = GetTickCount64();
            if (now >= end)
                return WAIT_TIMEOUT;
            remaining = (DWORD)(end - now);
        }
        DWORD got = WaitForMultipleObjects(count + 1, all, FALSE, remaining);
        if (got != WAIT_OBJECT_0 + count)
            return got;
        apartment_pump(a);
    }
}

/* ------------------------------------------------------------------ identity managers of exported objects */

typedef struct EXPORT_IPID {
    struct EXPORT_IPID *Next;
    GUID Ipid;
    IID Iid;
    IRpcStubBuffer *Stub;
    LONG PublicRefs;
} EXPORT_IPID;

typedef struct EXPORT {
    struct EXPORT *Next;
    ULONG64 Oid;
    IUnknown *Object;
    APARTMENT *Apartment;
    EXPORT_IPID *Ipids;
    LONG Refs;
    LONG Strong;
} EXPORT;

static SRWLOCK g_export_lock = SRWLOCK_INIT;
static EXPORT *g_exports;
static LONG g_ipid_serial;
static ULONG64 g_oid_serial;

void OrpcRemUnknownIpid(ULONG64 oxid, GUID *ipid)
{
    memset(ipid, 0, sizeof(*ipid));
    memcpy(ipid->Data4, &oxid, 8);
}

BOOL OrpcIsRemUnknownIpid(const GUID *ipid, ULONG64 *oxid)
{
    if (ipid->Data1 || ipid->Data2 || ipid->Data3)
        return FALSE;
    memcpy(oxid, ipid->Data4, 8);
    return TRUE;
}

static void new_ipid(GUID *ipid, ULONG64 oxid)
{
    ipid->Data1 = (ULONG)InterlockedIncrement(&g_ipid_serial);
    ipid->Data2 = (USHORT)GetCurrentProcessId();
    ipid->Data3 = (USHORT)oxid | 0x8000;
    memcpy(ipid->Data4, &oxid, 8);
}

/* this process's endpoint, used as the rpc address and in the dual string array of object references. */
static WCHAR g_endpoint[48];
static SRWLOCK g_server_lock = SRWLOCK_INIT;
static BOOL g_server_started;

static RPC_DISPATCH_FUNCTION g_dispatch_functions[1] = {OrpcDispatch};
static RPC_DISPATCH_TABLE g_dispatch_table = {1, g_dispatch_functions, 0};
static RPC_SERVER_INTERFACE g_orpc_interface;

HRESULT OrpcServerStart(const WCHAR **endpoint)
{
    if (g_server_started) {
        *endpoint = g_endpoint;
        return S_OK;
    }
    HRESULT hr = S_OK;
    AcquireSRWLockExclusive(&g_server_lock);
    if (!g_server_started) {
        static const WCHAR hex[] = L"0123456789abcdef";
        ULONGLONG seed = ((ULONGLONG)GetCurrentProcessId() << 32) ^ GetTickCount64() ^ (ULONGLONG)(ULONG_PTR)&seed;
        wcscpy(g_endpoint, L"OLE");
        for (int i = 0; i < 16; i++)
            g_endpoint[3 + i] = hex[(seed >> (4 * i)) & 15];
        g_endpoint[19] = 0;
        g_orpc_interface.Length = sizeof(g_orpc_interface);
        g_orpc_interface.InterfaceId.SyntaxGUID = (GUID){0x6d696d65, 0x6f72, 0x7063, {0x6f, 0x6c, 0x65, 0x69, 0x66, 0x00, 0x00, 0x01}};
        g_orpc_interface.TransferSyntax.SyntaxGUID =
            (GUID){0x8A885D04, 0x1CEB, 0x11C9, {0x9F, 0xE8, 0x08, 0x00, 0x2B, 0x10, 0x48, 0x60}};
        g_orpc_interface.TransferSyntax.SyntaxVersion = (RPC_VERSION){2, 0};
        g_orpc_interface.DispatchTable = &g_dispatch_table;
        RPC_STATUS status = RpcServerUseProtseqEpW((RPC_WSTR)L"ncalrpc", 20, (RPC_WSTR)g_endpoint, nullptr);
        if (!status)
            status = RpcServerRegisterIf3(&g_orpc_interface, nullptr, nullptr, RPC_IF_OLE | RPC_IF_AUTOLISTEN, 20,
                                          (ULONG)-1, nullptr, nullptr);
        if (!status) {
            status = RpcServerListen(1, 20, TRUE);
            if (status == RPC_S_ALREADY_LISTENING)
                status = RPC_S_OK;
        }
        if (status)
            hr = HRESULT_FROM_WIN32(status);
        else
            g_server_started = TRUE;
    }
    ReleaseSRWLockExclusive(&g_server_lock);
    *endpoint = g_endpoint;
    return hr;
}

static EXPORT *export_find_object(IUnknown *identity)
{
    for (EXPORT *e = g_exports; e; e = e->Next)
        if (e->Object == identity)
            return e;
    return nullptr;
}

static EXPORT_IPID *export_find_ipid(EXPORT *e, REFIID iid)
{
    for (EXPORT_IPID *ip = e->Ipids; ip; ip = ip->Next)
        if (IsEqualIID(&ip->Iid, iid))
            return ip;
    return nullptr;
}

/* ole32's own factory first, then the interface's registered proxy stub class. */
static IPSFactoryBuffer *factory_for(REFIID iid)
{
    IPSFactoryBuffer *factory = nullptr;
    if (SUCCEEDED(OrpcBuiltinFactory(iid, &factory)))
        return factory;
    CLSID clsid;
    if (FAILED(CoGetPSClsid(iid, &clsid)))
        return nullptr;
    if (FAILED(CoGetClassObject(&clsid, CLSCTX_INPROC_SERVER, nullptr, &IID_IPSFactoryBuffer, (void **)&factory)))
        return nullptr;
    return factory;
}

/* also used by the proxy side. */
IPSFactoryBuffer *OrpcFactoryFor(REFIID iid)
{
    return factory_for(iid);
}

static void export_destroy(EXPORT *e)
{
    EXPORT_IPID *ip = e->Ipids;
    while (ip) {
        EXPORT_IPID *next = ip->Next;
        if (ip->Stub) {
            ip->Stub->lpVtbl->Disconnect(ip->Stub);
            ip->Stub->lpVtbl->Release(ip->Stub);
        }
        OleFree(ip);
        ip = next;
    }
    e->Object->lpVtbl->Release(e->Object);
    OleFree(e);
}

/* unlinks and frees an export once its references are gone. */
static void export_unlink(EXPORT *e)
{
    for (EXPORT **link = &g_exports; *link; link = &(*link)->Next)
        if (*link == e) {
            *link = e->Next;
            break;
        }
}

static void export_drop_refs(ULONG64 oid, const GUID *ipid, ULONG count)
{
    EXPORT *gone = nullptr;
    AcquireSRWLockExclusive(&g_export_lock);
    for (EXPORT *e = g_exports; e; e = e->Next) {
        if (e->Oid != oid)
            continue;
        EXPORT_IPID *ip = nullptr;
        for (ip = e->Ipids; ip; ip = ip->Next)
            if (IsEqualGUID(&ip->Ipid, ipid))
                break;
        if (ip) {
            ULONG n = count < (ULONG)ip->PublicRefs ? count : (ULONG)ip->PublicRefs;
            ip->PublicRefs -= (LONG)n;
            e->Refs -= (LONG)n;
        }
        if (e->Refs <= 0 && e->Strong <= 0) {
            export_unlink(e);
            gone = e;
        }
        break;
    }
    ReleaseSRWLockExclusive(&g_export_lock);
    if (gone)
        export_destroy(gone);
}

static void export_disconnect_apartment(APARTMENT *a)
{
    for (;;) {
        EXPORT *gone = nullptr;
        AcquireSRWLockExclusive(&g_export_lock);
        for (EXPORT *e = g_exports; e; e = e->Next)
            if (e->Apartment == a) {
                export_unlink(e);
                gone = e;
                break;
            }
        ReleaseSRWLockExclusive(&g_export_lock);
        if (!gone)
            return;
        export_destroy(gone);
    }
}

HRESULT OrpcDisconnectObject(IUnknown *object)
{
    IUnknown *identity;
    if (FAILED(object->lpVtbl->QueryInterface(object, &IID_IUnknown, (void **)&identity)))
        return S_OK;
    EXPORT *gone = nullptr;
    AcquireSRWLockExclusive(&g_export_lock);
    EXPORT *e = export_find_object(identity);
    if (e) {
        export_unlink(e);
        gone = e;
    }
    ReleaseSRWLockExclusive(&g_export_lock);
    identity->lpVtbl->Release(identity);
    if (gone)
        export_destroy(gone);
    return S_OK;
}

/* makes the export when missing. takes over the identity reference. */
static HRESULT export_get(IUnknown *identity, APARTMENT *apartment, EXPORT **result)
{
    AcquireSRWLockExclusive(&g_export_lock);
    EXPORT *e = export_find_object(identity);
    if (!e) {
        e = OleAllocZero(sizeof(*e));
        if (!e) {
            ReleaseSRWLockExclusive(&g_export_lock);
            return E_OUTOFMEMORY;
        }
        e->Oid = ((ULONG64)GetCurrentProcessId() << 32) | (ULONG)(++g_oid_serial);
        e->Object = identity;
        identity->lpVtbl->AddRef(identity);
        e->Apartment = apartment;
        e->Next = g_exports;
        g_exports = e;
    }
    *result = e;
    ReleaseSRWLockExclusive(&g_export_lock);
    return S_OK;
}

/* the entry and stub are made the first time the interface is handed out. */
static HRESULT ipid_get(EXPORT *e, REFIID iid, EXPORT_IPID **result)
{
    AcquireSRWLockShared(&g_export_lock);
    EXPORT_IPID *ip = export_find_ipid(e, iid);
    ReleaseSRWLockShared(&g_export_lock);
    if (ip) {
        *result = ip;
        return S_OK;
    }
    IRpcStubBuffer *stub = nullptr;
    if (!IsEqualIID(iid, &IID_IUnknown)) {
        IPSFactoryBuffer *factory = factory_for(iid);
        if (!factory)
            return E_NOINTERFACE;
        HRESULT hr = factory->lpVtbl->CreateStub(factory, iid, e->Object, &stub);
        factory->lpVtbl->Release(factory);
        if (FAILED(hr))
            return hr;
    }
    EXPORT_IPID *made = OleAllocZero(sizeof(*made));
    if (!made) {
        if (stub)
            stub->lpVtbl->Release(stub);
        return E_OUTOFMEMORY;
    }
    made->Iid = *iid;
    made->Stub = stub;
    new_ipid(&made->Ipid, e->Apartment->Oxid);
    AcquireSRWLockExclusive(&g_export_lock);
    ip = export_find_ipid(e, iid);
    if (ip) {
        ReleaseSRWLockExclusive(&g_export_lock);
        if (stub)
            stub->lpVtbl->Release(stub);
        OleFree(made);
        *result = ip;
        return S_OK;
    }
    made->Next = e->Ipids;
    e->Ipids = made;
    ReleaseSRWLockExclusive(&g_export_lock);
    *result = made;
    return S_OK;
}

/* ------------------------------------------------------------------ object references on the wire */

static HRESULT write_all(IStream *stream, const void *data, ULONG size)
{
    ULONG done = 0;
    HRESULT hr = stream->lpVtbl->Write(stream, data, size, &done);
    return FAILED(hr) ? hr : done == size ? S_OK : STG_E_MEDIUMFULL;
}

static HRESULT read_all(IStream *stream, void *data, ULONG size)
{
    ULONG done = 0;
    HRESULT hr = stream->lpVtbl->Read(stream, data, size, &done);
    return FAILED(hr) ? hr : done == size ? S_OK : STG_E_READFAULT;
}

/* this process's dual string array: its ncalrpc endpoint and no security bindings. */
static HRESULT write_dual_string(IStream *stream, const WCHAR *endpoint)
{
    SIZE_T length = wcslen(endpoint);
    USHORT entries = (USHORT)(1 + length + 1 + 1 + 1 + 1);
    USHORT head[2] = {entries, (USHORT)(1 + length + 1 + 1)};
    HRESULT hr = write_all(stream, head, sizeof(head));
    USHORT tower = NCALRPC_TOWER_ID;
    if (SUCCEEDED(hr))
        hr = write_all(stream, &tower, sizeof(tower));
    if (SUCCEEDED(hr))
        hr = write_all(stream, endpoint, (ULONG)((length + 1) * sizeof(WCHAR)));
    USHORT zeros[3] = {0, 0, 0};
    if (SUCCEEDED(hr))
        hr = write_all(stream, zeros, sizeof(zeros));
    return hr;
}

/* keeps the ncalrpc endpoint the array names. */
static HRESULT read_dual_string(IStream *stream, WCHAR *endpoint, ULONG chars)
{
    USHORT head[2];
    HRESULT hr = read_all(stream, head, sizeof(head));
    if (FAILED(hr))
        return hr;
    ULONG entries = head[0];
    if (entries > 1024)
        return RPC_E_INVALID_OBJREF_HR;
    WCHAR *data = OleAlloc((entries + 1) * sizeof(WCHAR));
    if (!data)
        return E_OUTOFMEMORY;
    hr = read_all(stream, data, entries * sizeof(WCHAR));
    endpoint[0] = 0;
    if (SUCCEEDED(hr)) {
        ULONG i = 0;
        while (i < entries && data[i]) {
            USHORT tower = data[i++];
            ULONG start = i;
            while (i < entries && data[i])
                i++;
            if (i >= entries)
                break;
            if (tower == NCALRPC_TOWER_ID && !endpoint[0] && i - start < chars) {
                memcpy(endpoint, data + start, (i - start) * sizeof(WCHAR));
                endpoint[i - start] = 0;
            }
            i++;
        }
        if (!endpoint[0])
            hr = RPC_E_INVALID_OBJREF_HR;
    }
    OleFree(data);
    return hr;
}

HRESULT OrpcMarshalSizeMax(ULONG *size, REFIID iid, IUnknown *object, DWORD context, DWORD flags)
{
    /* the endpoint has a fixed length, so asking for a size does not start the server. */
    *size = sizeof(OBJREF_HEAD) + sizeof(STDOBJREF) + 4 + 8 * sizeof(WCHAR) + 24 * sizeof(WCHAR);
    return S_OK;
}

#define SORF_TABLE_STRONG 0x1
#define SORF_TABLE_WEAK 0x2

static HRESULT write_objref(IStream *stream, REFIID iid, const STDOBJREF *std, const WCHAR *endpoint)
{
    OBJREF_HEAD head = {OBJREF_SIGNATURE, OBJREF_STANDARD, *iid};
    HRESULT hr = write_all(stream, &head, sizeof(head));
    if (SUCCEEDED(hr))
        hr = write_all(stream, std, sizeof(*std));
    if (SUCCEEDED(hr))
        hr = write_dual_string(stream, endpoint);
    return hr;
}

HRESULT OrpcMarshal(IStream *stream, REFIID iid, IUnknown *object, DWORD context, DWORD flags)
{
    struct PROXYMGR *manager = OrpcProxyManagerOf(object);
    if (manager) {
        HRESULT hr = OrpcMarshalProxy(manager, stream, iid, flags);
        OrpcProxyManagerRelease(manager);
        return hr;
    }
    const WCHAR *endpoint;
    HRESULT hr = OrpcServerStart(&endpoint);
    if (FAILED(hr))
        return hr;
    APARTMENT *apartment = OrpcCurrentApartment();
    if (!apartment)
        return E_OUTOFMEMORY;
    IUnknown *identity;
    hr = object->lpVtbl->QueryInterface(object, &IID_IUnknown, (void **)&identity);
    if (FAILED(hr))
        return hr;
    EXPORT *e;
    hr = export_get(identity, apartment, &e);
    identity->lpVtbl->Release(identity);
    if (FAILED(hr))
        return hr;
    EXPORT_IPID *ip;
    hr = ipid_get(e, iid, &ip);
    if (FAILED(hr)) {
        /* drop the export made for nothing. */
        export_drop_refs(e->Oid, &GUID_NULL, 0);
        return hr;
    }
    STDOBJREF std = {};
    std.Oxid = e->Apartment->Oxid;
    std.Oid = e->Oid;
    std.Ipid = ip->Ipid;
    AcquireSRWLockExclusive(&g_export_lock);
    if (flags == MSHLFLAGS_TABLESTRONG) {
        e->Strong++;
        std.Flags = SORF_TABLE_STRONG;
    } else if (flags == MSHLFLAGS_TABLEWEAK) {
        e->Strong++;
        std.Flags = SORF_TABLE_WEAK;
    } else {
        std.PublicRefs = ORPC_PUBLIC_REFS;
        ip->PublicRefs += ORPC_PUBLIC_REFS;
        e->Refs += ORPC_PUBLIC_REFS;
    }
    ReleaseSRWLockExclusive(&g_export_lock);
    hr = write_objref(stream, iid, &std, endpoint);
    if (FAILED(hr)) {
        if (flags == MSHLFLAGS_NORMAL)
            export_drop_refs(e->Oid, &ip->Ipid, ORPC_PUBLIC_REFS);
        else {
            AcquireSRWLockExclusive(&g_export_lock);
            e->Strong--;
            ReleaseSRWLockExclusive(&g_export_lock);
            export_drop_refs(e->Oid, &ip->Ipid, 0);
        }
    }
    return hr;
}

/* resolves an object reference of this process to the object, in the requested interface. */
static HRESULT local_object(const STDOBJREF *std, REFIID iid, void **object)
{
    IUnknown *identity = nullptr;
    AcquireSRWLockShared(&g_export_lock);
    for (EXPORT *e = g_exports; e; e = e->Next)
        if (e->Oid == std->Oid) {
            identity = e->Object;
            identity->lpVtbl->AddRef(identity);
            break;
        }
    ReleaseSRWLockShared(&g_export_lock);
    if (!identity)
        return CO_E_OBJNOTCONNECTED_HR;
    HRESULT hr = identity->lpVtbl->QueryInterface(identity, iid, object);
    identity->lpVtbl->Release(identity);
    return hr;
}

HRESULT OrpcProxyUnmarshal(const STDOBJREF *std, const WCHAR *endpoint, REFIID object_iid, REFIID wanted, void **object);

HRESULT OrpcUnmarshalAfterHead(IStream *stream, const OBJREF_HEAD *head, REFIID wanted, void **object)
{
    STDOBJREF std;
    WCHAR endpoint[48];
    HRESULT hr = read_all(stream, &std, sizeof(std));
    if (SUCCEEDED(hr))
        hr = read_dual_string(stream, endpoint, ARRAYSIZE(endpoint));
    if (FAILED(hr))
        return hr;
    if ((ULONG)(std.Oxid >> 32) == GetCurrentProcessId()) {
        hr = local_object(&std, IsEqualGUID(wanted, &GUID_NULL) ? &head->Iid : wanted, object);
        /* a normal reference gives its references back, a table reference keeps them. */
        if (!(std.Flags & (SORF_TABLE_STRONG | SORF_TABLE_WEAK)) && std.PublicRefs)
            export_drop_refs(std.Oid, &std.Ipid, std.PublicRefs);
        return hr;
    }
    return OrpcProxyUnmarshal(&std, endpoint, &head->Iid, IsEqualGUID(wanted, &GUID_NULL) ? &head->Iid : wanted, object);
}

HRESULT OrpcReleaseMarshalData(IStream *stream)
{
    STDOBJREF std;
    WCHAR endpoint[48];
    HRESULT hr = read_all(stream, &std, sizeof(std));
    if (SUCCEEDED(hr))
        hr = read_dual_string(stream, endpoint, ARRAYSIZE(endpoint));
    if (FAILED(hr))
        return hr;
    if ((ULONG)(std.Oxid >> 32) == GetCurrentProcessId()) {
        if (std.Flags & (SORF_TABLE_STRONG | SORF_TABLE_WEAK)) {
            AcquireSRWLockExclusive(&g_export_lock);
            for (EXPORT *e = g_exports; e; e = e->Next)
                if (e->Oid == std.Oid && e->Strong > 0)
                    e->Strong--;
            ReleaseSRWLockExclusive(&g_export_lock);
            export_drop_refs(std.Oid, &std.Ipid, 0);
        } else {
            export_drop_refs(std.Oid, &std.Ipid, std.PublicRefs);
        }
        return S_OK;
    }
    /* references from another process are returned to it. */
    OXID_ENTRY *server = OrpcOxidEntry(std.Oxid, endpoint);
    if (server && std.PublicRefs) {
        BYTE in[4 + 24];
        BYTE *out = nullptr;
        ULONG out_size = 0;
        ULONG one = 1;
        memcpy(in, &one, 4);
        memcpy(in + 4, &std.Ipid, 16);
        ULONG refs[2] = {std.PublicRefs, 0};
        memcpy(in + 20, refs, 8);
        OrpcRemCall(server, REMUNK_RELEASE, in, sizeof(in), &out, &out_size);
        OleFree(out);
    }
    return S_OK;
}

/* ------------------------------------------------------------------ reference counting interfaces */

/* for queries: any interface entry of the object the ipid belongs to. */
static EXPORT_IPID *any_ipid_of(const GUID *ipid, EXPORT **owner)
{
    for (EXPORT *e = g_exports; e; e = e->Next)
        for (EXPORT_IPID *ip = e->Ipids; ip; ip = ip->Next)
            if (IsEqualGUID(&ip->Ipid, ipid)) {
                *owner = e;
                return ip;
            }
    return nullptr;
}

typedef struct REPLY {
    BYTE *Data;
    ULONG Size, Capacity;
} REPLY;

static BOOL reply_put(REPLY *r, const void *data, ULONG size)
{
    if (r->Size + size > r->Capacity) {
        ULONG capacity = r->Capacity ? r->Capacity * 2 : 256;
        while (capacity < r->Size + size)
            capacity *= 2;
        BYTE *grown = OleAlloc(capacity);
        if (!grown)
            return FALSE;
        if (r->Size)
            memcpy(grown, r->Data, r->Size);
        OleFree(r->Data);
        r->Data = grown;
        r->Capacity = capacity;
    }
    memcpy(r->Data + r->Size, data, size);
    r->Size += size;
    return TRUE;
}

static void reply_u32(REPLY *r, ULONG v)
{
    reply_put(r, &v, 4);
}

/*
 * IRemUnknown calls. each stream is little endian numbers in argument order:
 *   RemQueryInterface: ipid, refs, interface count, interface ids  ->  per interface: result, std object reference
 *   RemAddRef: entry count, entries of ipid, public refs, private refs  ->  per entry: result
 *   RemRelease: the same entries  ->  nothing
 * this is windows' layout without the alignment and the pointers.
 */
static HRESULT remunk_body(ULONG64 oxid, ULONG method, const BYTE *in, ULONG in_size, BYTE **out, ULONG *out_size)
{
    REPLY r = {};
    HRESULT hr = S_OK;
    *out = nullptr;
    *out_size = 0;
    if (method == REMUNK_QUERY_INTERFACE) {
        GUID ripid;
        ULONG refs, count;
        if (in_size < 16 + 8)
            return RPC_E_INVALID_DATA_HR;
        memcpy(&ripid, in, 16);
        memcpy(&refs, in + 16, 4);
        memcpy(&count, in + 20, 4);
        if (count > 64 || in_size < 24 + count * 16)
            return RPC_E_INVALID_DATA_HR;
        AcquireSRWLockShared(&g_export_lock);
        EXPORT *owner = nullptr;
        EXPORT_IPID *known = any_ipid_of(&ripid, &owner);
        IUnknown *identity = known ? owner->Object : nullptr;
        if (identity)
            identity->lpVtbl->AddRef(identity);
        ULONG64 e_oid = known ? owner->Oid : 0;
        APARTMENT *apartment = known ? owner->Apartment : nullptr;
        ReleaseSRWLockShared(&g_export_lock);
        if (!identity)
            return RPC_E_DISCONNECTED_HR;
        const WCHAR *endpoint;
        OrpcServerStart(&endpoint);
        (void)endpoint;
        reply_u32(&r, S_OK);
        for (ULONG i = 0; i < count; i++) {
            IID iid;
            memcpy(&iid, in + 24 + i * 16, 16);
            STDOBJREF std = {};
            IUnknown *object = nullptr;
            HRESULT item = identity->lpVtbl->QueryInterface(identity, &iid, (void **)&object);
            EXPORT_IPID *ip = nullptr;
            if (SUCCEEDED(item)) {
                EXPORT *e = nullptr;
                AcquireSRWLockShared(&g_export_lock);
                for (EXPORT *c = g_exports; c; c = c->Next)
                    if (c->Oid == e_oid) {
                        e = c;
                        break;
                    }
                ReleaseSRWLockShared(&g_export_lock);
                item = e ? ipid_get(e, &iid, &ip) : RPC_E_DISCONNECTED_HR;
                if (SUCCEEDED(item)) {
                    AcquireSRWLockExclusive(&g_export_lock);
                    ip->PublicRefs += (LONG)refs;
                    e->Refs += (LONG)refs;
                    ReleaseSRWLockExclusive(&g_export_lock);
                    std.PublicRefs = refs;
                    std.Oxid = apartment->Oxid;
                    std.Oid = e_oid;
                    std.Ipid = ip->Ipid;
                }
                object->lpVtbl->Release(object);
            }
            reply_u32(&r, (ULONG)item);
            reply_put(&r, &std, sizeof(std));
        }
        identity->lpVtbl->Release(identity);
    } else if (method == REMUNK_ADD_REF || method == REMUNK_RELEASE) {
        ULONG count;
        if (in_size < 4)
            return RPC_E_INVALID_DATA_HR;
        memcpy(&count, in, 4);
        if (count > 256 || in_size < 4 + count * 24)
            return RPC_E_INVALID_DATA_HR;
        if (method == REMUNK_ADD_REF)
            reply_u32(&r, S_OK);
        for (ULONG i = 0; i < count; i++) {
            GUID ipid;
            ULONG refs[2];
            memcpy(&ipid, in + 4 + i * 24, 16);
            memcpy(refs, in + 4 + i * 24 + 16, 8);
            HRESULT item = S_OK;
            if (method == REMUNK_ADD_REF) {
                AcquireSRWLockExclusive(&g_export_lock);
                EXPORT *owner = nullptr;
                EXPORT_IPID *ip = any_ipid_of(&ipid, &owner);
                if (ip) {
                    ip->PublicRefs += (LONG)refs[0];
                    owner->Refs += (LONG)refs[0];
                } else {
                    item = RPC_E_DISCONNECTED_HR;
                }
                ReleaseSRWLockExclusive(&g_export_lock);
                reply_u32(&r, (ULONG)item);
            } else {
                ULONG64 oid = 0;
                AcquireSRWLockShared(&g_export_lock);
                EXPORT *owner = nullptr;
                if (any_ipid_of(&ipid, &owner))
                    oid = owner->Oid;
                ReleaseSRWLockShared(&g_export_lock);
                if (oid)
                    export_drop_refs(oid, &ipid, refs[0]);
            }
        }
    } else {
        return RPC_E_INVALIDMETHOD_HR;
    }
    *out = r.Data;
    *out_size = r.Size;
    return hr;
}

typedef struct REMUNK_CALL {
    ULONG64 Oxid;
    ULONG Method;
    const BYTE *In;
    ULONG InSize;
    BYTE **Out;
    ULONG *OutSize;
    HRESULT Hr;
} REMUNK_CALL;

static void remunk_run(void *context)
{
    REMUNK_CALL *c = context;
    c->Hr = remunk_body(c->Oxid, c->Method, c->In, c->InSize, c->Out, c->OutSize);
}

/* a thread serving calls joins the multithreaded apartment if it has not yet. */
static void enter_mta(void)
{
    OLE_THREAD *t = OleThreadState(TRUE);
    if (t && !t->ComInits)
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
}

/*
 * reference counting calls run in the object's apartment, since a query asks the object and a release may be its
 * last.
 */
HRESULT OrpcRemUnknownCall(ULONG64 oxid, ULONG method, const BYTE *in, ULONG in_size, BYTE **out, ULONG *out_size)
{
    GUID first;
    *out = nullptr;
    *out_size = 0;
    if (method == REMUNK_QUERY_INTERFACE ? in_size < 16 : in_size < 4 + 16)
        return RPC_E_INVALID_DATA_HR;
    memcpy(&first, method == REMUNK_QUERY_INTERFACE ? in : in + 4, 16);
    APARTMENT *apartment = nullptr;
    AcquireSRWLockShared(&g_export_lock);
    EXPORT *owner = nullptr;
    if (any_ipid_of(&first, &owner))
        apartment = owner->Apartment;
    ReleaseSRWLockShared(&g_export_lock);
    if (!apartment)
        return method == REMUNK_QUERY_INTERFACE ? RPC_E_DISCONNECTED_HR : S_OK;
    REMUNK_CALL call = {oxid, method, in, in_size, out, out_size, S_OK};
    if (apartment->Mta)
        enter_mta();
    HRESULT hr = OrpcRunInApartment(apartment, remunk_run, &call);
    return FAILED(hr) ? hr : call.Hr;
}

/* ------------------------------------------------------------------ calls to the stubs */

HRESULT OrpcFindStub(const GUID *ipid, IPID_TARGET *target)
{
    HRESULT hr = RPC_E_DISCONNECTED_HR;
    AcquireSRWLockShared(&g_export_lock);
    EXPORT *owner = nullptr;
    EXPORT_IPID *ip = any_ipid_of(ipid, &owner);
    if (ip && ip->Stub) {
        ip->Stub->lpVtbl->AddRef(ip->Stub);
        target->Stub = ip->Stub;
        target->Apartment = owner->Apartment;
        target->Iid = ip->Iid;
        hr = S_OK;
    }
    ReleaseSRWLockShared(&g_export_lock);
    return hr;
}
