#include "netprofmsvc.h"

/*
 * The private interfaces of the network list service of Windows 11 26300, whose tables are read from netprofmsvc.dll.
 * npmproxy.dll marshals them. iphlpapi asks the service for the interfaces of the machine and for the state and the
 * cost of one in GetNetworkConnectivityHintForInterface.
 */
enum {
    MANAGER_ENUM_NETWORK_INTERFACES = 5,
    MANAGER_IS_CONNECTED_TO_INTERNET = 15,
    MANAGER_IS_CONNECTED = 16,
    MANAGER_GET_CONNECTIVITY = 17,
    MANAGER_GET_COST = 18,
    MANAGER_ENUM_NETWORK_INTERFACES_EX = 29,
    MANAGER_METHODS = 34,
};
enum { INTERFACE_GET_INTERFACE_GUID = 4, INTERFACE_GET_STATE = 7, INTERFACE_GET_COST = 8, INTERFACE_METHODS = 10 };
enum { ENUM_NEXT = 3, ENUM_SKIP, ENUM_RESET, ENUM_CLONE, ENUM_METHODS };

typedef struct NP_INTERFACE_INFO {
    GUID Guid;
    ULONG State;
    ULONG Cost;
} NP_INTERFACE_INFO;

/* Every object, so the ones clients still hold can be disconnected when the service stops. */
typedef struct NP_OBJECT {
    const void *const *Table;
    LIST_ENTRY Links;
    LONG Refs;
    const GUID *Iid;
} NP_OBJECT;

typedef struct NP_INTERFACE {
    NP_OBJECT Object;
    NP_INTERFACE_INFO Info;
} NP_INTERFACE;

typedef struct NP_ENUM {
    NP_OBJECT Object;
    ULONG At, Count;
    NP_INTERFACE_INFO Items[];
} NP_ENUM;

static SRWLOCK g_lock = SRWLOCK_INIT;
static LIST_ENTRY g_objects = {&g_objects, &g_objects};

static void *object_new(SIZE_T size, const void *const *table, const GUID *iid)
{
    NP_OBJECT *o = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, size);
    if (!o)
        return nullptr;
    o->Table = table;
    o->Refs = 1;
    o->Iid = iid;
    AcquireSRWLockExclusive(&g_lock);
    InsertTailList(&g_objects, &o->Links);
    ReleaseSRWLockExclusive(&g_lock);
    return o;
}

static HRESULT STDMETHODCALLTYPE object_qi(NP_OBJECT *o, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    if (!IsEqualGUID(iid, &IID_IUnknown) && !IsEqualGUID(iid, o->Iid)) {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&o->Refs);
    *out = o;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE object_addref(NP_OBJECT *o)
{
    return (ULONG)InterlockedIncrement(&o->Refs);
}

static ULONG STDMETHODCALLTYPE object_release(NP_OBJECT *o)
{
    AcquireSRWLockExclusive(&g_lock);
    LONG refs = InterlockedDecrement(&o->Refs);
    if (!refs)
        RemoveEntryList(&o->Links);
    ReleaseSRWLockExclusive(&g_lock);
    if (!refs)
        LocalFree(o);
    return (ULONG)refs;
}

void NpDisconnectObjects(void)
{
    for (;;) {
        NP_OBJECT *o = nullptr;
        AcquireSRWLockExclusive(&g_lock);
        if (!IsListEmpty(&g_objects)) {
            o = CONTAINING_RECORD(g_objects.Flink, NP_OBJECT, Links);
            /* Out of the list, so the loop ends, and an entry of its own, so the release unlinks nothing else. */
            RemoveEntryList(&o->Links);
            InitializeListHead(&o->Links);
            InterlockedIncrement(&o->Refs);
        }
        ReleaseSRWLockExclusive(&g_lock);
        if (!o)
            return;
        CoDisconnectObject((IUnknown *)o, 0);
        object_release(o);
    }
}

/* A method of the tables that the service does not have. The stub of the proxy has zeroed what it marshals back. */
static HRESULT STDMETHODCALLTYPE not_implemented(void *self)
{
    return E_NOTIMPL;
}

/*
 * The state of an interface, in the bits of the state the kernel publishes for the machine: a default route makes
 * the family reach the internet, an address alone a local network. Measured on Windows 11 26100: a leased ipv4
 * address with a gateway and a link local ipv6 address are 0x19, with the unrestricted cost of 1.
 */
static ULONG state_of(const IP_ADAPTER_ADDRESSES *a)
{
    ULONG state = 0;
    for (const IP_ADAPTER_GATEWAY_ADDRESS_LH *g = a->FirstGatewayAddress; g; g = g->Next)
        if (g->Address.lpSockaddr)
            state |= g->Address.lpSockaddr->sa_family == AF_INET ? NP_STATE_IPV4_INTERNET : NP_STATE_IPV6_INTERNET;
    for (const IP_ADAPTER_UNICAST_ADDRESS_LH *u = a->FirstUnicastAddress; u; u = u->Next) {
        if (!u->Address.lpSockaddr)
            continue;
        if (u->Address.lpSockaddr->sa_family == AF_INET && !(state & NP_STATE_IPV4_INTERNET))
            state |= NP_STATE_IPV4_LOCAL;
        if (u->Address.lpSockaddr->sa_family == AF_INET6 && !(state & NP_STATE_IPV6_INTERNET))
            state |= NP_STATE_IPV6_LOCAL;
    }
    return state ? state | NP_STATE_CONNECTED : 0;
}

/* The interfaces a network is connected through: the loopback and an adapter with no address are none. */
static HRESULT take_interfaces(NP_INTERFACE_INFO **out, ULONG *count)
{
    *out = nullptr;
    *count = 0;
    ULONG size = 16384, error;
    IP_ADAPTER_ADDRESSES *list = nullptr;
    for (int i = 0; i < 4; i++) {
        list = LocalAlloc(LMEM_FIXED, size);
        if (!list)
            return E_OUTOFMEMORY;
        error = GetAdaptersAddresses(AF_UNSPEC,
                                     GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                         GAA_FLAG_SKIP_DNS_SERVER,
                                     nullptr, list, &size);
        if (error != ERROR_BUFFER_OVERFLOW)
            break;
        LocalFree(list);
        list = nullptr;
    }
    if (error == ERROR_NO_DATA) {
        LocalFree(list);
        return S_OK;
    }
    if (error) {
        LocalFree(list);
        return HRESULT_FROM_WIN32(error);
    }
    ULONG adapters = 0;
    for (const IP_ADAPTER_ADDRESSES *a = list; a; a = a->Next)
        adapters++;
    NP_INTERFACE_INFO *items = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, (adapters ? adapters : 1) * sizeof(*items));
    if (!items) {
        LocalFree(list);
        return E_OUTOFMEMORY;
    }
    ULONG n = 0;
    for (const IP_ADAPTER_ADDRESSES *a = list; a; a = a->Next) {
        if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK || a->OperStatus != IfOperStatusUp)
            continue;
        ULONG state = state_of(a);
        if (!state || ConvertInterfaceLuidToGuid(&a->Luid, &items[n].Guid))
            continue;
        items[n].State = state;
        items[n].Cost = NP_COST_UNRESTRICTED;
        n++;
    }
    LocalFree(list);
    *out = items;
    *count = n;
    return S_OK;
}

/* ------------------------------------------------------------------ INetworkInterfacePrivate */

static HRESULT STDMETHODCALLTYPE interface_guid(NP_INTERFACE *i, GUID *out)
{
    if (!out)
        return E_POINTER;
    *out = i->Info.Guid;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE interface_state(NP_INTERFACE *i, ULONG *out)
{
    if (!out)
        return E_POINTER;
    *out = i->Info.State;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE interface_cost(NP_INTERFACE *i, ULONG *out)
{
    if (!out)
        return E_POINTER;
    *out = i->Info.Cost;
    return S_OK;
}

static const void *const g_interface_table[INTERFACE_METHODS] = {
    object_qi,
    object_addref,
    object_release,
    not_implemented,
    [INTERFACE_GET_INTERFACE_GUID] = interface_guid,
    not_implemented,
    not_implemented,
    [INTERFACE_GET_STATE] = interface_state,
    [INTERFACE_GET_COST] = interface_cost,
    not_implemented,
};

/* ------------------------------------------------------------------ IEnumNetworkInterfacePrivate */

static const void *const g_enum_table[ENUM_METHODS];

static NP_ENUM *enum_new(const NP_INTERFACE_INFO *items, ULONG count, ULONG at)
{
    NP_ENUM *e = object_new(sizeof(*e) + count * sizeof(*items), g_enum_table, &IID_IEnumNetworkInterfacePrivate);
    if (!e)
        return nullptr;
    e->Count = count;
    e->At = at;
    if (count)
        memcpy(e->Items, items, count * sizeof(*items));
    return e;
}

/* Measured: fewer than asked is S_FALSE with the count that came, and none asked is S_OK. */
static HRESULT STDMETHODCALLTYPE enum_next(NP_ENUM *e, ULONG count, NP_INTERFACE **out, ULONG *fetched)
{
    if (!out || (!fetched && count != 1))
        return E_POINTER;
    ULONG done = 0;
    HRESULT hr = S_OK;
    AcquireSRWLockExclusive(&g_lock);
    ULONG at = e->At;
    ULONG available = e->Count - at < count ? e->Count - at : count;
    e->At += available;
    ReleaseSRWLockExclusive(&g_lock);
    for (; done < available; done++) {
        NP_INTERFACE *i = object_new(sizeof(*i), g_interface_table, &IID_INetworkInterfacePrivate);
        if (!i) {
            hr = E_OUTOFMEMORY;
            break;
        }
        i->Info = e->Items[at + done];
        out[done] = i;
    }
    if (FAILED(hr)) {
        while (done)
            object_release(&out[--done]->Object);
    }
    if (fetched)
        *fetched = done;
    return FAILED(hr) ? hr : done == count ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE enum_skip(NP_ENUM *e, ULONG count)
{
    AcquireSRWLockExclusive(&g_lock);
    ULONG available = e->Count - e->At < count ? e->Count - e->At : count;
    e->At += available;
    ReleaseSRWLockExclusive(&g_lock);
    return available == count ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE enum_reset(NP_ENUM *e)
{
    AcquireSRWLockExclusive(&g_lock);
    e->At = 0;
    ReleaseSRWLockExclusive(&g_lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_clone(NP_ENUM *e, NP_ENUM **out)
{
    if (!out)
        return E_POINTER;
    *out = enum_new(e->Items, e->Count, e->At);
    return *out ? S_OK : E_OUTOFMEMORY;
}

static const void *const g_enum_table[ENUM_METHODS] = {
    object_qi, object_addref, object_release, enum_next, enum_skip, enum_reset, enum_clone,
};

/* ------------------------------------------------------------------ INetworkListManagerPrivate */

/*
 * Measured on a machine with one adapter: every kind of the extended enumeration lists the same interfaces as the
 * plain one. iphlpapi asks kind 1 for an interface the plain one has not, and tells that one as hidden.
 */
static HRESULT STDMETHODCALLTYPE manager_enum_ex(NP_OBJECT *m, int kind, NP_ENUM **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    NP_INTERFACE_INFO *items;
    ULONG count;
    HRESULT hr = take_interfaces(&items, &count);
    if (FAILED(hr))
        return hr;
    *out = enum_new(items, count, 0);
    LocalFree(items);
    return *out ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE manager_enum(NP_OBJECT *m, NP_ENUM **out)
{
    return manager_enum_ex(m, 0, out);
}

static HRESULT machine_state(ULONG *state)
{
    NP_INTERFACE_INFO *items;
    ULONG count;
    *state = 0;
    HRESULT hr = take_interfaces(&items, &count);
    if (FAILED(hr))
        return hr;
    for (ULONG i = 0; i < count; i++)
        *state |= items[i].State;
    LocalFree(items);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE manager_connected_to_internet(NP_OBJECT *m, VARIANT_BOOL *out)
{
    if (!out)
        return E_POINTER;
    ULONG state;
    HRESULT hr = machine_state(&state);
    *out = state & (NP_STATE_IPV4_INTERNET | NP_STATE_IPV6_INTERNET) ? VARIANT_TRUE : VARIANT_FALSE;
    return hr;
}

static HRESULT STDMETHODCALLTYPE manager_connected(NP_OBJECT *m, VARIANT_BOOL *out)
{
    if (!out)
        return E_POINTER;
    ULONG state;
    HRESULT hr = machine_state(&state);
    *out = state ? VARIANT_TRUE : VARIANT_FALSE;
    return hr;
}

static HRESULT STDMETHODCALLTYPE manager_connectivity(NP_OBJECT *m, ULONG *out)
{
    if (!out)
        return E_POINTER;
    return machine_state(out);
}

static HRESULT STDMETHODCALLTYPE manager_cost(NP_OBJECT *m, ULONG *out)
{
    if (!out)
        return E_POINTER;
    *out = NP_COST_UNRESTRICTED;
    return S_OK;
}

static const void *const g_manager_table[MANAGER_METHODS] = {
    object_qi,       object_addref,   object_release,  not_implemented, not_implemented,
    [MANAGER_ENUM_NETWORK_INTERFACES] = manager_enum,
    not_implemented, not_implemented, not_implemented, not_implemented, not_implemented,
    not_implemented, not_implemented, not_implemented, not_implemented,
    [MANAGER_IS_CONNECTED_TO_INTERNET] = manager_connected_to_internet,
    [MANAGER_IS_CONNECTED] = manager_connected,
    [MANAGER_GET_CONNECTIVITY] = manager_connectivity,
    [MANAGER_GET_COST] = manager_cost,
    not_implemented, not_implemented, not_implemented, not_implemented, not_implemented,
    not_implemented, not_implemented, not_implemented, not_implemented, not_implemented,
    [MANAGER_ENUM_NETWORK_INTERFACES_EX] = manager_enum_ex,
    not_implemented, not_implemented, not_implemented, not_implemented,
};

/* Measured: the factory answers IUnknown and the manager's interface, and no interface for any other. */
HRESULT NpManagerCreate(REFIID iid, void **out)
{
    if (!IsEqualGUID(iid, &IID_IUnknown) && !IsEqualGUID(iid, &IID_INetworkListManagerPrivate))
        return E_NOINTERFACE;
    NP_OBJECT *m = object_new(sizeof(*m), g_manager_table, &IID_INetworkListManagerPrivate);
    if (!m)
        return E_OUTOFMEMORY;
    *out = m;
    return S_OK;
}
