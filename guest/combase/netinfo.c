/*
 * Windows.Networking.Connectivity.NetworkInformation: the connection profiles of the network and the changes of its
 * status. windows asks the network list manager and the connection manager; mime asks iphlpapi, which reports the
 * adapters the guest sees, the connectivity of each and the host going online or offline. a profile is an adapter
 * that is up. the profiles are not kept, a program that holds one asks the current state of its adapter.
 */
#include "combase_private.h"
#include <mime/winrt_object.h>
#include <mime/win32/connectivity.h>
#include <mime/win32/iphlpapi.h>

const WCHAR CbNetworkInformationClass[] = L"Windows.Networking.Connectivity.NetworkInformation";

#define ERROR_NOT_FOUND_HR ((HRESULT)0x80070490L)
#define MEDIA_ETHERNET 1
#define MEDIA_WLAN 2

/* ------------------------------------------------------------------ the adapters of the guest */

typedef struct NET_ADAPTER {
    GUID Id, Network;
    ULONG Index, Type, Media, Metric;
    UINT64 Outbound, Inbound;
    NetworkConnectivityLevel Level;
    NetworkCostType Cost;
    WCHAR Name[48];
} NET_ADAPTER;

typedef ULONG(WINAPI *GET_ADAPTERS_ADDRESSES)(ULONG, ULONG, PVOID, PIP_ADAPTER_ADDRESSES, PULONG);
typedef DWORD(NETIOAPI_API_ *GET_HINT_OF_INTERFACE)(NET_IFINDEX, NL_NETWORK_CONNECTIVITY_HINT *);
typedef DWORD(NETIOAPI_API_ *NOTIFY_HINT)(PNETWORK_CONNECTIVITY_HINT_CHANGE_CALLBACK, PVOID, BOOLEAN, HANDLE *);
typedef DWORD(NETIOAPI_API_ *NOTIFY_INTERFACE)(ADDRESS_FAMILY, PIPINTERFACE_CHANGE_CALLBACK, PVOID, BOOLEAN, HANDLE *);
typedef DWORD(NETIOAPI_API_ *CANCEL_NOTIFY)(HANDLE);

static struct {
    GET_ADAPTERS_ADDRESSES GetAdaptersAddresses;
    GET_HINT_OF_INTERFACE GetHintOfInterface;
    NOTIFY_HINT NotifyHint;
    NOTIFY_INTERFACE NotifyInterface;
    CANCEL_NOTIFY Cancel;
} g_ip;
static INIT_ONCE g_ip_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK load_ip(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    HMODULE iphlpapi = LoadLibraryW(L"iphlpapi.dll");
    if (!iphlpapi)
        return TRUE;
    g_ip.GetAdaptersAddresses = (GET_ADAPTERS_ADDRESSES)(void *)GetProcAddress(iphlpapi, "GetAdaptersAddresses");
    g_ip.GetHintOfInterface =
        (GET_HINT_OF_INTERFACE)(void *)GetProcAddress(iphlpapi, "GetNetworkConnectivityHintForInterface");
    g_ip.NotifyHint = (NOTIFY_HINT)(void *)GetProcAddress(iphlpapi, "NotifyNetworkConnectivityHintChange");
    g_ip.NotifyInterface = (NOTIFY_INTERFACE)(void *)GetProcAddress(iphlpapi, "NotifyIpInterfaceChange");
    g_ip.Cancel = (CANCEL_NOTIFY)(void *)GetProcAddress(iphlpapi, "CancelMibChangeNotify2");
    return TRUE;
}

static void ip_api(void)
{
    InitOnceExecuteOnce(&g_ip_once, load_ip, nullptr, nullptr);
}

static int hex_digit(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* the name of an adapter is its guid in braces. */
static BOOL parse_guid(const char *text, GUID *guid)
{
    static const BYTE order[16] = {3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15};
    BYTE *bytes = (BYTE *)guid;
    if (!text || *text++ != '{')
        return FALSE;
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            if (*text++ != '-')
                return FALSE;
        }
        int high = hex_digit((BYTE)text[0]), low = high < 0 ? -1 : hex_digit((BYTE)text[1]);
        if (low < 0)
            return FALSE;
        bytes[order[i]] = (BYTE)(high << 4 | low);
        text += 2;
    }
    return *text == '}';
}

static NetworkConnectivityLevel level_of(NL_NETWORK_CONNECTIVITY_LEVEL_HINT hint)
{
    switch (hint) {
    case NetworkConnectivityLevelHintLocalAccess:
        return NetworkConnectivityLevel_LocalAccess;
    case NetworkConnectivityLevelHintConstrainedInternetAccess:
        return NetworkConnectivityLevel_ConstrainedInternetAccess;
    case NetworkConnectivityLevelHintInternetAccess:
        return NetworkConnectivityLevel_InternetAccess;
    default:
        return NetworkConnectivityLevel_None;
    }
}

/* the adapters that are up, but for loopback and tunnels. a list the caller frees. */
static BOOL net_scan(NET_ADAPTER **list, ULONG *count)
{
    *list = nullptr;
    *count = 0;
    ip_api();
    if (!g_ip.GetAdaptersAddresses)
        return TRUE;
    ULONG size = 16384, error;
    IP_ADAPTER_ADDRESSES *addresses = nullptr;
    do {
        CbFree(addresses);
        addresses = CbAlloc(size);
        if (!addresses)
            return FALSE;
        error = g_ip.GetAdaptersAddresses(AF_UNSPEC,
                                          GAA_FLAG_SKIP_UNICAST | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                              GAA_FLAG_SKIP_DNS_SERVER,
                                          nullptr, addresses, &size);
    } while (error == ERROR_BUFFER_OVERFLOW);
    if (error != NO_ERROR) {
        CbFree(addresses);
        return TRUE;
    }
    ULONG n = 0;
    for (IP_ADAPTER_ADDRESSES *a = addresses; a; a = a->Next)
        n++;
    NET_ADAPTER *out = n ? CbAlloc(n * sizeof(*out)) : nullptr;
    if (n && !out) {
        CbFree(addresses);
        return FALSE;
    }
    ULONG named = 0;
    for (IP_ADAPTER_ADDRESSES *a = addresses; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK || a->IfType == IF_TYPE_TUNNEL)
            continue;
        NET_ADAPTER *e = &out[*count];
        memset(e, 0, sizeof(*e));
        if (!parse_guid(a->AdapterName, &e->Id))
            continue;
        e->Network = a->NetworkGuid;
        e->Index = a->IfIndex;
        e->Type = a->IfType;
        e->Media = a->IfType == IF_TYPE_IEEE80211 ? MEDIA_WLAN : MEDIA_ETHERNET;
        e->Metric = a->Ipv4Metric;
        e->Outbound = a->TransmitLinkSpeed;
        e->Inbound = a->ReceiveLinkSpeed;
        NL_NETWORK_CONNECTIVITY_HINT hint = {};
        if (g_ip.GetHintOfInterface && g_ip.GetHintOfInterface(a->IfIndex, &hint) == NO_ERROR) {
            e->Level = level_of(hint.ConnectivityLevel);
            e->Cost = (NetworkCostType)hint.ConnectivityCost;
        }
        /* a network with a way out is named by the first free number, one without is not identified. */
        if (e->Level >= NetworkConnectivityLevel_ConstrainedInternetAccess) {
            if (++named > 1)
                _snwprintf(e->Name, RTL_NUMBER_OF(e->Name), L"Network %lu", (unsigned long)named);
            else
                wcscpy(e->Name, L"Network");
        } else {
            wcscpy(e->Name, L"Unidentified network");
        }
        (*count)++;
    }
    CbFree(addresses);
    *list = out;
    return TRUE;
}

static BOOL net_find(const GUID *id, NET_ADAPTER *found)
{
    NET_ADAPTER *list;
    ULONG count;
    BOOL ok = FALSE;
    if (net_scan(&list, &count))
        for (ULONG i = 0; i < count && !ok; i++)
            if (IsEqualGUID(&list[i].Id, id)) {
                *found = list[i];
                ok = TRUE;
            }
    CbFree(list);
    return ok;
}

/* the adapter that carries the traffic to the internet: the best route among those with a way out. */
static const NET_ADAPTER *net_preferred(const NET_ADAPTER *list, ULONG count)
{
    const NET_ADAPTER *best = nullptr;
    for (ULONG i = 0; i < count; i++)
        if (list[i].Level >= NetworkConnectivityLevel_ConstrainedInternetAccess && (!best || list[i].Metric < best->Metric))
            best = &list[i];
    return best;
}

/* ------------------------------------------------------------------ the objects */

typedef struct PROFILE {
    WRT_OBJECT Base;
    GUID Id;
    ULONG Media;
    WCHAR Name[48];
} PROFILE;

typedef struct COST {
    WRT_OBJECT Base;
    NetworkCostType Type;
} COST;

typedef struct ADAPTER {
    WRT_OBJECT Base;
    NET_ADAPTER Adapter;
} ADAPTER;

typedef struct NETWORK_ITEM {
    WRT_OBJECT Base;
    GUID Network;
    NetworkTypes Types;
} NETWORK_ITEM;

static void plain_destroy(WRT_OBJECT *object)
{
    CbFree(object);
}

static HRESULT out_string(const WCHAR *text, HSTRING *value)
{
    if (!value)
        return E_POINTER;
    return WindowsCreateString(text, (UINT32)wcslen(text), value);
}

/* ------------------------------------------------------------------ the cost */

WRT_INSPECTABLE_THUNKS(IConnectionCost)
WRT_INSPECTABLE_THUNKS(IConnectionCost2)

static COST *cost_of(void *iface)
{
    return (COST *)((WRT_IFACE *)iface)->Owner;
}

static HRESULT STDMETHODCALLTYPE cost_get_type(IConnectionCost *This, NetworkCostType *value)
{
    if (!value)
        return E_POINTER;
    *value = cost_of(This)->Type;
    return S_OK;
}

/* the host is not roaming, and no plan has a limit to go over. */
static HRESULT STDMETHODCALLTYPE cost_get_false(IConnectionCost *This, BOOLEAN *value)
{
    if (!value)
        return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cost2_get_false(IConnectionCost2 *This, BOOLEAN *value)
{
    if (!value)
        return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static const IConnectionCostVtbl cost_vtbl = {WRT_INSPECTABLE_ENTRIES(IConnectionCost), cost_get_type, cost_get_false,
                                              cost_get_false, cost_get_false};
static const IConnectionCost2Vtbl cost2_vtbl = {WRT_INSPECTABLE_ENTRIES(IConnectionCost2), cost2_get_false};
static const WRT_CLASS cost_class = {L"Windows.Networking.Connectivity.ConnectionCost",
                                     2,
                                     {&IID_IConnectionCost, &IID_IConnectionCost2},
                                     0,
                                     plain_destroy};

static HRESULT new_cost(NetworkCostType type, IConnectionCost **value)
{
    COST *cost = CbAlloc(sizeof(*cost));
    *value = nullptr;
    if (!cost)
        return E_OUTOFMEMORY;
    memset(cost, 0, sizeof(*cost));
    const void *vtables[2] = {&cost_vtbl, &cost2_vtbl};
    WrtInit(&cost->Base, &cost_class, vtables);
    cost->Type = type;
    *value = (IConnectionCost *)&cost->Base.Ifaces[0];
    return S_OK;
}

/* ------------------------------------------------------------------ the network item and the adapter */

WRT_INSPECTABLE_THUNKS(INetworkItem)
WRT_INSPECTABLE_THUNKS(INetworkAdapter)

static HRESULT STDMETHODCALLTYPE item_get_network_id(INetworkItem *This, GUID *value)
{
    if (!value)
        return E_POINTER;
    *value = ((NETWORK_ITEM *)((WRT_IFACE *)This)->Owner)->Network;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE item_get_network_types(INetworkItem *This, NetworkTypes *value)
{
    if (!value)
        return E_POINTER;
    *value = ((NETWORK_ITEM *)((WRT_IFACE *)This)->Owner)->Types;
    return S_OK;
}

static const INetworkItemVtbl item_vtbl = {WRT_INSPECTABLE_ENTRIES(INetworkItem), item_get_network_id,
                                           item_get_network_types};
static const WRT_CLASS item_class = {L"Windows.Networking.Connectivity.NetworkItem", 1, {&IID_INetworkItem}, 0,
                                     plain_destroy};

static ADAPTER *adapter_of(void *iface)
{
    return (ADAPTER *)((WRT_IFACE *)iface)->Owner;
}

static HRESULT STDMETHODCALLTYPE adapter_get_outbound(INetworkAdapter *This, UINT64 *value)
{
    if (!value)
        return E_POINTER;
    *value = adapter_of(This)->Adapter.Outbound;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE adapter_get_inbound(INetworkAdapter *This, UINT64 *value)
{
    if (!value)
        return E_POINTER;
    *value = adapter_of(This)->Adapter.Inbound;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE adapter_get_type(INetworkAdapter *This, UINT32 *value)
{
    if (!value)
        return E_POINTER;
    *value = adapter_of(This)->Adapter.Type;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE adapter_get_item(INetworkAdapter *This, INetworkItem **value)
{
    if (!value)
        return E_POINTER;
    *value = nullptr;
    NETWORK_ITEM *item = CbAlloc(sizeof(*item));
    if (!item)
        return E_OUTOFMEMORY;
    memset(item, 0, sizeof(*item));
    const void *vtables[1] = {&item_vtbl};
    WrtInit(&item->Base, &item_class, vtables);
    const NET_ADAPTER *a = &adapter_of(This)->Adapter;
    item->Network = a->Network;
    item->Types = a->Level >= NetworkConnectivityLevel_ConstrainedInternetAccess ? NetworkTypes_Internet
                  : a->Level == NetworkConnectivityLevel_LocalAccess             ? NetworkTypes_PrivateNetwork
                                                                                 : NetworkTypes_None;
    *value = (INetworkItem *)&item->Base.Ifaces[0];
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE adapter_get_id(INetworkAdapter *This, GUID *value)
{
    if (!value)
        return E_POINTER;
    *value = adapter_of(This)->Adapter.Id;
    return S_OK;
}

static HRESULT new_profile(const NET_ADAPTER *adapter, IConnectionProfile **value);

/* the profile of the adapter while it is up, else none. */
static HRESULT STDMETHODCALLTYPE adapter_get_connected_profile(INetworkAdapter *This, IAsyncOperation **operation)
{
    if (!operation)
        return E_POINTER;
    *operation = nullptr;
    NET_ADAPTER now;
    IConnectionProfile *profile = nullptr;
    if (net_find(&adapter_of(This)->Adapter.Id, &now)) {
        HRESULT hr = new_profile(&now, &profile);
        if (FAILED(hr))
            return hr;
    }
    return CbAsyncFinished(CB_ASYNC_PROFILE, S_OK, FALSE, (IUnknown *)profile, (void **)operation);
}

static const INetworkAdapterVtbl adapter_vtbl = {WRT_INSPECTABLE_ENTRIES(INetworkAdapter),
                                                 adapter_get_outbound,
                                                 adapter_get_inbound,
                                                 adapter_get_type,
                                                 adapter_get_item,
                                                 adapter_get_id,
                                                 adapter_get_connected_profile};
static const WRT_CLASS adapter_class = {L"Windows.Networking.Connectivity.NetworkAdapter", 1, {&IID_INetworkAdapter}, 0,
                                        plain_destroy};

static HRESULT new_adapter(const NET_ADAPTER *source, INetworkAdapter **value)
{
    ADAPTER *adapter = CbAlloc(sizeof(*adapter));
    *value = nullptr;
    if (!adapter)
        return E_OUTOFMEMORY;
    memset(adapter, 0, sizeof(*adapter));
    const void *vtables[1] = {&adapter_vtbl};
    WrtInit(&adapter->Base, &adapter_class, vtables);
    adapter->Adapter = *source;
    *value = (INetworkAdapter *)&adapter->Base.Ifaces[0];
    return S_OK;
}

/* ------------------------------------------------------------------ the connection profile */

WRT_INSPECTABLE_THUNKS(IConnectionProfile)
WRT_INSPECTABLE_THUNKS(IConnectionProfile2)
WRT_INSPECTABLE_THUNKS(IConnectionProfile3)
WRT_INSPECTABLE_THUNKS(IConnectionProfile4)
WRT_INSPECTABLE_THUNKS(IConnectionProfile5)
WRT_INSPECTABLE_THUNKS(IConnectionProfile6)

static PROFILE *profile_of(void *iface)
{
    return (PROFILE *)((WRT_IFACE *)iface)->Owner;
}

static HRESULT STDMETHODCALLTYPE profile_get_name(IConnectionProfile *This, HSTRING *value)
{
    return out_string(profile_of(This)->Name, value);
}

/* an adapter that is gone has no connectivity. */
static HRESULT STDMETHODCALLTYPE profile_get_level(IConnectionProfile *This, NetworkConnectivityLevel *value)
{
    NET_ADAPTER now;
    if (!value)
        return E_POINTER;
    *value = net_find(&profile_of(This)->Id, &now) ? now.Level : NetworkConnectivityLevel_None;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE profile_get_network_names(IConnectionProfile *This, IVectorViewPtr **value)
{
    if (!value)
        return E_POINTER;
    *value = nullptr;
    void **items = CbAlloc(sizeof(void *));
    if (!items)
        return E_OUTOFMEMORY;
    HRESULT hr = out_string(profile_of(This)->Name, (HSTRING *)&items[0]);
    if (SUCCEEDED(hr))
        hr = CbNewVectorView(CB_VECTOR_STRINGS, items, 1, (void **)value);
    if (FAILED(hr)) {
        if (items[0])
            WindowsDeleteString((HSTRING)items[0]);
        CbFree(items);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE profile_get_cost(IConnectionProfile *This, IConnectionCost **value)
{
    NET_ADAPTER now;
    if (!value)
        return E_POINTER;
    return new_cost(net_find(&profile_of(This)->Id, &now) ? now.Cost : NetworkCostType_Unknown, value);
}

/* a plan with data to count is the matter of mobile broadband, and usage the matter of the usage database. */
static HRESULT STDMETHODCALLTYPE profile_get_plan(IConnectionProfile *This, IInspectable **value)
{
    if (!value)
        return E_POINTER;
    *value = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE profile_get_adapter(IConnectionProfile *This, INetworkAdapter **value)
{
    NET_ADAPTER now;
    if (!value)
        return E_POINTER;
    *value = nullptr;
    return net_find(&profile_of(This)->Id, &now) ? new_adapter(&now, value) : ERROR_NOT_FOUND_HR;
}

static HRESULT STDMETHODCALLTYPE profile_get_usage(IConnectionProfile *This, DateTime start, DateTime end,
                                                   DataUsage *value)
{
    if (!value)
        return E_POINTER;
    memset(value, 0, sizeof(*value));
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE profile_get_usage_states(IConnectionProfile *This, DateTime start, DateTime end,
                                                          NetworkUsageStates states, DataUsage *value)
{
    if (!value)
        return E_POINTER;
    memset(value, 0, sizeof(*value));
    return E_NOTIMPL;
}

static const IConnectionProfileVtbl profile_vtbl = {WRT_INSPECTABLE_ENTRIES(IConnectionProfile),
                                                    profile_get_name,
                                                    profile_get_level,
                                                    profile_get_network_names,
                                                    profile_get_cost,
                                                    profile_get_plan,
                                                    profile_get_adapter,
                                                    profile_get_usage,
                                                    profile_get_usage_states,
                                                    profile_get_plan};

/* only mobile broadband is an other kind than the two the guest has. */
static HRESULT STDMETHODCALLTYPE profile2_get_wwan(IConnectionProfile2 *This, BOOLEAN *value)
{
    if (!value)
        return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE profile2_get_wlan(IConnectionProfile2 *This, BOOLEAN *value)
{
    if (!value)
        return E_POINTER;
    *value = profile_of(This)->Media == MEDIA_WLAN;
    return S_OK;
}

/* a profile that is not of the kind has no details of it. */
static HRESULT STDMETHODCALLTYPE profile2_get_none(IConnectionProfile2 *This, IInspectable **value)
{
    if (!value)
        return E_POINTER;
    *value = nullptr;
    return S_OK;
}

/* the details of a wireless profile are the ssid and the radio, which the guest does not know. */
static HRESULT STDMETHODCALLTYPE profile2_get_wlan_details(IConnectionProfile2 *This, IInspectable **value)
{
    if (!value)
        return E_POINTER;
    *value = nullptr;
    return profile_of(This)->Media == MEDIA_WLAN ? E_NOTIMPL : S_OK;
}

static HRESULT STDMETHODCALLTYPE profile2_get_domain_level(IConnectionProfile2 *This, DomainConnectivityLevel *value)
{
    if (!value)
        return E_POINTER;
    *value = DomainConnectivityLevel_None;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE profile2_get_usage(IConnectionProfile2 *This, DateTime start, DateTime end,
                                                    INT32 granularity, NetworkUsageStates states,
                                                    IAsyncOperation **operation)
{
    if (!operation)
        return E_POINTER;
    *operation = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE profile2_get_intervals(IConnectionProfile2 *This, DateTime start, DateTime end,
                                                        NetworkUsageStates states, IAsyncOperation **operation)
{
    if (!operation)
        return E_POINTER;
    *operation = nullptr;
    return E_NOTIMPL;
}

static const IConnectionProfile2Vtbl profile2_vtbl = {WRT_INSPECTABLE_ENTRIES(IConnectionProfile2),
                                                      profile2_get_wwan,
                                                      profile2_get_wlan,
                                                      profile2_get_none,
                                                      profile2_get_wlan_details,
                                                      profile2_get_none,
                                                      profile2_get_none,
                                                      profile2_get_domain_level,
                                                      profile2_get_usage,
                                                      profile2_get_intervals};

static HRESULT STDMETHODCALLTYPE profile3_get_usage(IConnectionProfile3 *This, DateTime start, DateTime end,
                                                    NetworkUsageStates states, IAsyncOperation **operation)
{
    if (!operation)
        return E_POINTER;
    *operation = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE profile4_get_usage(IConnectionProfile4 *This, DateTime start, DateTime end,
                                                    NetworkUsageStates states, IAsyncOperation **operation)
{
    if (!operation)
        return E_POINTER;
    *operation = nullptr;
    return E_NOTIMPL;
}

/* only the profiles of wireless networks that were joined can be deleted. */
static HRESULT STDMETHODCALLTYPE profile5_get_can_delete(IConnectionProfile5 *This, BOOLEAN *value)
{
    if (!value)
        return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE profile5_try_delete(IConnectionProfile5 *This, IAsyncOperation **operation)
{
    if (!operation)
        return E_POINTER;
    *operation = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE profile6_is_domain_authenticated(IConnectionProfile6 *This, INT32 kind,
                                                                  BOOLEAN *value)
{
    if (!value)
        return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static const IConnectionProfile3Vtbl profile3_vtbl = {WRT_INSPECTABLE_ENTRIES(IConnectionProfile3), profile3_get_usage};
static const IConnectionProfile4Vtbl profile4_vtbl = {WRT_INSPECTABLE_ENTRIES(IConnectionProfile4), profile4_get_usage};
static const IConnectionProfile5Vtbl profile5_vtbl = {WRT_INSPECTABLE_ENTRIES(IConnectionProfile5),
                                                      profile5_get_can_delete, profile5_try_delete};
static const IConnectionProfile6Vtbl profile6_vtbl = {WRT_INSPECTABLE_ENTRIES(IConnectionProfile6),
                                                      profile6_is_domain_authenticated};

static const WRT_CLASS profile_class = {L"Windows.Networking.Connectivity.ConnectionProfile",
                                        6,
                                        {&IID_IConnectionProfile, &IID_IConnectionProfile2, &IID_IConnectionProfile3,
                                         &IID_IConnectionProfile4, &IID_IConnectionProfile5, &IID_IConnectionProfile6},
                                        0,
                                        plain_destroy};

static HRESULT new_profile(const NET_ADAPTER *adapter, IConnectionProfile **value)
{
    PROFILE *profile = CbAlloc(sizeof(*profile));
    *value = nullptr;
    if (!profile)
        return E_OUTOFMEMORY;
    memset(profile, 0, sizeof(*profile));
    const void *vtables[6] = {&profile_vtbl, &profile2_vtbl, &profile3_vtbl, &profile4_vtbl, &profile5_vtbl,
                              &profile6_vtbl};
    WrtInit(&profile->Base, &profile_class, vtables);
    profile->Id = adapter->Id;
    profile->Media = adapter->Media;
    wcscpy(profile->Name, adapter->Name);
    *value = (IConnectionProfile *)&profile->Base.Ifaces[0];
    return S_OK;
}

/* ------------------------------------------------------------------ the change of status */

typedef struct LISTENER {
    EventRegistrationToken Token;
    INetworkStatusChangedEventHandler *Handler;
} LISTENER;

static CRITICAL_SECTION g_listen_lock;
static SRWLOCK g_list_lock = SRWLOCK_INIT;
static INIT_ONCE g_listen_once = INIT_ONCE_STATIC_INIT;
static LISTENER *g_listeners;
static ULONG g_listener_count, g_listener_capacity;
static INT64 g_next_token;
static HANDLE g_hint_registration, g_interface_registration;
static UINT64 g_fingerprint;

static BOOL CALLBACK init_listen(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    InitializeCriticalSection(&g_listen_lock);
    return TRUE;
}

/* what a program would see change: the adapters that are up, how far each reaches and what it costs. */
static UINT64 net_fingerprint(void)
{
    NET_ADAPTER *list;
    ULONG count;
    UINT64 hash = 14695981039346656037ull;
    if (net_scan(&list, &count)) {
        for (ULONG i = 0; i < count; i++) {
            const BYTE *bytes = (const BYTE *)&list[i].Id;
            for (ULONG k = 0; k < sizeof(GUID); k++)
                hash = (hash ^ bytes[k]) * 1099511628211ull;
            hash = (hash ^ (UINT64)list[i].Level) * 1099511628211ull;
            hash = (hash ^ (UINT64)list[i].Cost) * 1099511628211ull;
        }
    }
    CbFree(list);
    return hash;
}

/* the handlers are called by the thread of the change, outside the lists, with the first change of the status. */
static void network_changed(void)
{
    UINT64 now = net_fingerprint();
    AcquireSRWLockExclusive(&g_list_lock);
    BOOL changed = now != g_fingerprint;
    g_fingerprint = now;
    INetworkStatusChangedEventHandler **handlers =
        changed && g_listener_count ? CbAlloc(g_listener_count * sizeof(*handlers)) : nullptr;
    ULONG count = 0;
    for (ULONG i = 0; handlers && i < g_listener_count; i++) {
        handlers[count] = g_listeners[i].Handler;
        handlers[count]->lpVtbl->AddRef(handlers[count]);
        count++;
    }
    ReleaseSRWLockExclusive(&g_list_lock);
    for (ULONG i = 0; i < count; i++) {
        handlers[i]->lpVtbl->Invoke(handlers[i], nullptr);
        handlers[i]->lpVtbl->Release(handlers[i]);
    }
    CbFree(handlers);
}

static void NETIOAPI_API_ on_hint(PVOID context, NL_NETWORK_CONNECTIVITY_HINT hint)
{
    network_changed();
}

static void NETIOAPI_API_ on_interface(PVOID context, PMIB_IPINTERFACE_ROW row, MIB_NOTIFICATION_TYPE type)
{
    if (type != MibInitialNotification)
        network_changed();
}

/*
 * the notifications start with the first handler. callbacks run on a thread no apartment owns, so the process holds
 * the multithreaded one.
 */
static void listen_start(void)
{
    typedef HRESULT(WINAPI * INCREMENT_MTA)(void **);
    static void *mta;
    ip_api();
    if (!mta) {
        INCREMENT_MTA increment = (INCREMENT_MTA)(void *)CbOleProc("CoIncrementMTAUsage");
        if (increment)
            increment(&mta);
    }
    g_fingerprint = net_fingerprint();
    if (g_ip.NotifyHint)
        g_ip.NotifyHint(on_hint, nullptr, FALSE, &g_hint_registration);
    if (g_ip.NotifyInterface)
        g_ip.NotifyInterface(AF_UNSPEC, on_interface, nullptr, FALSE, &g_interface_registration);
}

static void listen_stop(HANDLE hint, HANDLE interfaces)
{
    if (hint)
        g_ip.Cancel(hint);
    if (interfaces)
        g_ip.Cancel(interfaces);
}

WRT_INSPECTABLE_THUNKS(INetworkInformationStatics)
WRT_INSPECTABLE_THUNKS(INetworkInformationStatics2)

typedef struct INetworkInformationFactory INetworkInformationFactory;
#define INetworkInformationFactory_METHODS(T)                                                                          \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *ActivateInstance)(T * This, IInspectable * *instance);
MIME_COM_INTERFACE(INetworkInformationFactory)

WRT_INSPECTABLE_THUNKS(INetworkInformationFactory)

static HRESULT STDMETHODCALLTYPE statics_add_status_changed(INetworkInformationStatics *This,
                                                            INetworkStatusChangedEventHandler *handler,
                                                            EventRegistrationToken *token)
{
    if (!handler || !token)
        return E_POINTER;
    InitOnceExecuteOnce(&g_listen_once, init_listen, nullptr, nullptr);
    EnterCriticalSection(&g_listen_lock);
    HRESULT hr = S_OK;
    AcquireSRWLockExclusive(&g_list_lock);
    if (g_listener_count == g_listener_capacity) {
        ULONG capacity = g_listener_capacity ? g_listener_capacity * 2 : 4;
        LISTENER *grown = CbAlloc(capacity * sizeof(*grown));
        if (grown) {
            if (g_listener_count)
                memcpy(grown, g_listeners, g_listener_count * sizeof(*grown));
            CbFree(g_listeners);
            g_listeners = grown;
            g_listener_capacity = capacity;
        } else {
            hr = E_OUTOFMEMORY;
        }
    }
    BOOL first = SUCCEEDED(hr) && !g_listener_count;
    if (SUCCEEDED(hr)) {
        handler->lpVtbl->AddRef(handler);
        g_listeners[g_listener_count].Token.value = ++g_next_token;
        g_listeners[g_listener_count].Handler = handler;
        token->value = g_next_token;
        g_listener_count++;
    } else {
        token->value = 0;
    }
    ReleaseSRWLockExclusive(&g_list_lock);
    if (first)
        listen_start();
    LeaveCriticalSection(&g_listen_lock);
    return hr;
}

/* a token that is not a handler's is not an error. */
static HRESULT STDMETHODCALLTYPE statics_remove_status_changed(INetworkInformationStatics *This,
                                                               EventRegistrationToken token)
{
    InitOnceExecuteOnce(&g_listen_once, init_listen, nullptr, nullptr);
    EnterCriticalSection(&g_listen_lock);
    INetworkStatusChangedEventHandler *removed = nullptr;
    HANDLE hint = nullptr, interfaces = nullptr;
    AcquireSRWLockExclusive(&g_list_lock);
    for (ULONG i = 0; i < g_listener_count; i++)
        if (g_listeners[i].Token.value == token.value) {
            removed = g_listeners[i].Handler;
            memmove(&g_listeners[i], &g_listeners[i + 1], (g_listener_count - i - 1) * sizeof(LISTENER));
            g_listener_count--;
            break;
        }
    if (removed && !g_listener_count) {
        hint = g_hint_registration;
        interfaces = g_interface_registration;
        g_hint_registration = g_interface_registration = nullptr;
    }
    ReleaseSRWLockExclusive(&g_list_lock);
    LeaveCriticalSection(&g_listen_lock);
    /* stopping waits for a running callback, which may be a handler removing itself. */
    listen_stop(hint, interfaces);
    if (removed)
        removed->lpVtbl->Release(removed);
    return S_OK;
}

/* ------------------------------------------------------------------ the statics */

static HRESULT STDMETHODCALLTYPE statics_get_profiles(INetworkInformationStatics *This, IVectorViewPtr **value)
{
    if (!value)
        return E_POINTER;
    *value = nullptr;
    NET_ADAPTER *list;
    ULONG count;
    if (!net_scan(&list, &count))
        return E_OUTOFMEMORY;
    void **items = count ? CbAlloc(count * sizeof(void *)) : nullptr;
    HRESULT hr = count && !items ? E_OUTOFMEMORY : S_OK;
    ULONG made = 0;
    for (; SUCCEEDED(hr) && made < count; made++)
        hr = new_profile(&list[made], (IConnectionProfile **)&items[made]);
    CbFree(list);
    if (SUCCEEDED(hr))
        hr = CbNewVectorView(CB_VECTOR_PROFILES, items, count, (void **)value);
    if (FAILED(hr)) {
        for (ULONG i = 0; i < made; i++)
            if (items[i])
                ((IUnknown *)items[i])->lpVtbl->Release((IUnknown *)items[i]);
        CbFree(items);
    }
    return hr;
}

/* without a way to the internet there is no profile, and no error. */
static HRESULT STDMETHODCALLTYPE statics_get_internet_profile(INetworkInformationStatics *This,
                                                              IConnectionProfile **value)
{
    if (!value)
        return E_POINTER;
    *value = nullptr;
    NET_ADAPTER *list;
    ULONG count;
    if (!net_scan(&list, &count))
        return E_OUTOFMEMORY;
    const NET_ADAPTER *best = net_preferred(list, count);
    HRESULT hr = best ? new_profile(best, value) : S_OK;
    CbFree(list);
    return hr;
}

static HRESULT STDMETHODCALLTYPE statics_get_lan_identifiers(INetworkInformationStatics *This, IVectorViewPtr **value)
{
    if (!value)
        return E_POINTER;
    *value = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE statics_get_host_names(INetworkInformationStatics *This, IVectorViewPtr **value)
{
    if (!value)
        return E_POINTER;
    *value = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE statics_get_proxy(INetworkInformationStatics *This, IInspectable *uri,
                                                   IAsyncOperation **operation)
{
    if (!operation)
        return E_POINTER;
    *operation = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE statics_get_endpoint_pairs(INetworkInformationStatics *This, IInspectable *pairs,
                                                            INT32 options, IVectorViewPtr **value)
{
    if (!value)
        return E_POINTER;
    *value = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE statics2_find_profiles(INetworkInformationStatics2 *This, IInspectable *filter,
                                                        IAsyncOperation **operation)
{
    if (!operation)
        return E_POINTER;
    *operation = nullptr;
    return E_NOTIMPL;
}

/* the class has statics and no instances. */
static HRESULT STDMETHODCALLTYPE factory_activate(INetworkInformationFactory *This, IInspectable **instance)
{
    *instance = nullptr;
    return E_NOTIMPL;
}

static const INetworkInformationFactoryVtbl factory_vtbl = {WRT_INSPECTABLE_ENTRIES(INetworkInformationFactory),
                                                            factory_activate};
static const INetworkInformationStaticsVtbl statics_vtbl = {WRT_INSPECTABLE_ENTRIES(INetworkInformationStatics),
                                                            statics_get_profiles,
                                                            statics_get_internet_profile,
                                                            statics_get_lan_identifiers,
                                                            statics_get_host_names,
                                                            statics_get_proxy,
                                                            statics_get_endpoint_pairs,
                                                            statics_add_status_changed,
                                                            statics_remove_status_changed};
static const INetworkInformationStatics2Vtbl statics2_vtbl = {WRT_INSPECTABLE_ENTRIES(INetworkInformationStatics2),
                                                              statics2_find_profiles};

static void factory_destroy(WRT_OBJECT *object)
{
    object->Refs = 1;
}

static const WRT_CLASS factory_class = {CbNetworkInformationClass,
                                        3,
                                        {&IID_IActivationFactory, &IID_INetworkInformationStatics,
                                         &IID_INetworkInformationStatics2},
                                        0,
                                        factory_destroy};
static WRT_OBJECT g_factory;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK make(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    const void *vtables[3] = {&factory_vtbl, &statics_vtbl, &statics2_vtbl};
    WrtInit(&g_factory, &factory_class, vtables);
    return TRUE;
}

HRESULT CbNetworkInformationFactory(REFIID riid, void **factory)
{
    InitOnceExecuteOnce(&g_once, make, nullptr, nullptr);
    return WrtQuery(&g_factory.Ifaces[0], riid, factory);
}
