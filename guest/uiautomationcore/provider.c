/*
 * the provider side: the window host provider, the answer to WM_GETOBJECT, the events providers raise and the ways a
 * provider is taken away. what a client sees of a provider is served by the wrappers of remote.c.
 */
#include "uia.h"

WINOLEAPI HRESULT WINAPI CoCreateFreeThreadedMarshaler(IUnknown *outer, IUnknown **result);

extern void ServerDisconnectProvider(IRawElementProviderSimple *provider, const int *rid, int rid_len);
extern void ServerDisconnectAll(void);

static const IID IID_IMarshal_ = {0x00000003, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_DETACH && !reserved)
        UiaEventsProcessDetach();
    return TRUE;
}

/* ------------------------------------------------------------------ the reserved values */

static HRESULT WINAPI reserved_query(IUnknown *iface, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown)) {
        *object = iface;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

/* process wide singletons, handed out without a reference like windows does. */
static ULONG WINAPI reserved_add_ref(IUnknown *iface)
{
    return 2;
}

static ULONG WINAPI reserved_release(IUnknown *iface)
{
    return 1;
}

static const IUnknownVtbl reserved_vtbl = {reserved_query, reserved_add_ref, reserved_release};
static IUnknown g_mixed = {&reserved_vtbl}, g_not_supported = {&reserved_vtbl};

IUnknown *UiaNotSupportedValue(void)
{
    return &g_not_supported;
}

IUnknown *UiaMixedValue(void)
{
    return &g_mixed;
}

HRESULT WINAPI UiaGetReservedMixedAttributeValue(IUnknown **value)
{
    if (!value)
        return E_INVALIDARG;
    *value = &g_mixed;
    return S_OK;
}

HRESULT WINAPI UiaGetReservedNotSupportedValue(IUnknown **value)
{
    if (!value)
        return E_INVALIDARG;
    *value = &g_not_supported;
    return S_OK;
}

/* ------------------------------------------------------------------ the host provider of a window */

typedef struct HOST {
    IRawElementProviderSimple iface;
    LONG refs;
    HWND hwnd;
    IUnknown *marshaler;
} HOST;

static HRESULT WINAPI host_query(IRawElementProviderSimple *iface, REFIID riid, void **object)
{
    HOST *host = (HOST *)iface;
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IRawElementProviderSimple)) {
        *object = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *object = nullptr;
    /* the free threaded marshaler is aggregated so every apartment calls the provider itself. */
    if (IsEqualIID(riid, &IID_IMarshal_) && host->marshaler)
        return host->marshaler->lpVtbl->QueryInterface(host->marshaler, riid, object);
    return E_NOINTERFACE;
}

static ULONG WINAPI host_add_ref(IRawElementProviderSimple *iface)
{
    return (ULONG)InterlockedIncrement(&((HOST *)iface)->refs);
}

static ULONG WINAPI host_release(IRawElementProviderSimple *iface)
{
    HOST *host = (HOST *)iface;
    LONG refs = InterlockedDecrement(&host->refs);
    if (!refs) {
        if (host->marshaler)
            host->marshaler->lpVtbl->Release(host->marshaler);
        UiaFree(host);
    }
    return (ULONG)refs;
}

static HRESULT WINAPI host_options(IRawElementProviderSimple *iface, ProviderOptions *options)
{
    *options = ProviderOptions_ServerSideProvider;
    return S_OK;
}

static HRESULT WINAPI host_pattern(IRawElementProviderSimple *iface, PATTERNID id, IUnknown **pattern)
{
    *pattern = nullptr;
    return S_OK;
}

static HRESULT WINAPI host_property(IRawElementProviderSimple *iface, PROPERTYID id, VARIANT *value)
{
    HOST *host = (HOST *)iface;
    value->vt = VT_EMPTY;
    if (id == UIA_NativeWindowHandlePropertyId) {
        value->vt = VT_I4;
        value->lVal = (LONG)(LONG_PTR)host->hwnd;
    } else if (id == UIA_ProviderDescriptionPropertyId) {
        value->bstrVal = SysAllocString(L"Microsoft: Mini-HWND Proxy");
        if (!value->bstrVal)
            return E_OUTOFMEMORY;
        value->vt = VT_BSTR;
    }
    return S_OK;
}

static HRESULT WINAPI host_host(IRawElementProviderSimple *iface, IRawElementProviderSimple **host)
{
    *host = nullptr;
    return S_OK;
}

static const IRawElementProviderSimpleVtbl host_vtbl = {
    host_query, host_add_ref, host_release, host_options, host_pattern, host_property, host_host,
};

HRESULT WINAPI UiaHostProviderFromHwnd(HWND hwnd, IRawElementProviderSimple **provider)
{
    if (!provider)
        return E_INVALIDARG;
    *provider = nullptr;
    if (!hwnd)
        return E_INVALIDARG;
    if (!IsWindow(hwnd))
        return UIA_E_ELEMENTNOTAVAILABLE;
    HOST *host = UiaAlloc(sizeof(*host));
    if (!host)
        return E_OUTOFMEMORY;
    host->iface.lpVtbl = &host_vtbl;
    host->refs = 1;
    host->hwnd = hwnd;
    HRESULT hr = CoCreateFreeThreadedMarshaler((IUnknown *)&host->iface, &host->marshaler);
    if (FAILED(hr)) {
        UiaFree(host);
        return hr;
    }
    *provider = &host->iface;
    return S_OK;
}

/* ------------------------------------------------------------------ events of the msaa bridge */

#define EVENT_MAP_SIZE 100

typedef struct EVENT_ENTRY {
    HWND hwnd;
    NODE *node;
    LONG id;
} EVENT_ENTRY;

static CRITICAL_SECTION g_map_lock;
static INIT_ONCE g_map_once = INIT_ONCE_STATIC_INIT;
static EVENT_ENTRY g_map[EVENT_MAP_SIZE];
static LONG g_map_next = 1;
static int g_map_oldest;

static BOOL CALLBACK map_init(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    InitializeCriticalSection(&g_map_lock);
    return TRUE;
}

static void map_lock(void)
{
    InitOnceExecuteOnce(&g_map_once, map_init, nullptr, nullptr);
    EnterCriticalSection(&g_map_lock);
}

/* the id a provider has for the windows events about it, made on first use. */
static LONG map_add(HWND hwnd, NODE *node)
{
    map_lock();
    LONG id = 0;
    for (int i = 0; i < EVENT_MAP_SIZE; i++)
        if (g_map[i].node && g_map[i].hwnd == hwnd && NodeSame(g_map[i].node, node)) {
            id = g_map[i].id;
            break;
        }
    if (!id) {
        int slot = -1;
        for (int i = 0; i < EVENT_MAP_SIZE; i++)
            if (!g_map[i].node) {
                slot = i;
                break;
            }
        if (slot < 0) {
            slot = g_map_oldest++ % EVENT_MAP_SIZE;
            NodeRelease(g_map[slot].node);
        }
        id = g_map_next++;
        g_map[slot].hwnd = hwnd;
        g_map[slot].id = id;
        g_map[slot].node = node;
        NodeAddRef(node);
    }
    LeaveCriticalSection(&g_map_lock);
    return id;
}

static NODE *map_find(HWND hwnd, LONG id)
{
    NODE *found = nullptr;
    map_lock();
    for (int i = 0; i < EVENT_MAP_SIZE; i++)
        if (g_map[i].node && g_map[i].hwnd == hwnd && g_map[i].id == id) {
            found = g_map[i].node;
            NodeAddRef(found);
            break;
        }
    LeaveCriticalSection(&g_map_lock);
    return found;
}

static void map_release_dead(HWND hwnd)
{
    map_lock();
    for (int i = 0; i < EVENT_MAP_SIZE; i++)
        if (g_map[i].node && (g_map[i].hwnd == hwnd || !IsWindow(g_map[i].hwnd))) {
            NodeRelease(g_map[i].node);
            g_map[i].node = nullptr;
        }
    LeaveCriticalSection(&g_map_lock);
}

static void map_release_all(void)
{
    map_lock();
    for (int i = 0; i < EVENT_MAP_SIZE; i++) {
        NodeRelease(g_map[i].node);
        g_map[i].node = nullptr;
    }
    LeaveCriticalSection(&g_map_lock);
}

static void map_release_runtime_id(const int *rid, int count)
{
    map_lock();
    for (int i = 0; i < EVENT_MAP_SIZE; i++) {
        if (!g_map[i].node)
            continue;
        int *ids = nullptr, n = 0;
        BOOL same = FALSE;
        if (SUCCEEDED(NodeRuntimeId(g_map[i].node, &ids, &n)))
            same = n && !UiaCompareRuntimeIds(ids, n, rid, count);
        UiaFree(ids);
        if (same) {
            NodeRelease(g_map[i].node);
            g_map[i].node = nullptr;
        }
    }
    LeaveCriticalSection(&g_map_lock);
}

/* what windows event an automation event is told as to clients of active accessibility. */
static DWORD winevent_of_event(int event)
{
    switch (event) {
    case UIA_AutomationFocusChangedEventId: return 0x8005;
    case UIA_MenuOpenedEventId: return 6;
    case UIA_MenuClosedEventId: return 7;
    case UIA_MenuModeStartEventId: return 4;
    case UIA_MenuModeEndEventId: return 5;
    case UIA_Invoke_InvokedEventId: return 0x8013;
    case UIA_SelectionItem_ElementSelectedEventId: return 0x8006;
    case UIA_SelectionItem_ElementAddedToSelectionEventId: return 0x8007;
    case UIA_SelectionItem_ElementRemovedFromSelectionEventId: return 0x8008;
    case UIA_Selection_InvalidatedEventId: return 0x8009;
    case UIA_Text_TextSelectionChangedEventId: return 0x8014;
    case UIA_LiveRegionChangedEventId: return 0x8019;
    case UIA_ToolTipOpenedEventId: return 0x8002;
    case UIA_ToolTipClosedEventId: return 0x8003;
    case UIA_SystemAlertEventId: return 2;
    case UIA_Window_WindowOpenedEventId: return 0x8002;
    case UIA_Window_WindowClosedEventId: return 0x8003;
    }
    return 0;
}

static DWORD winevent_of_property(int property)
{
    switch (property) {
    case UIA_NamePropertyId: return 0x800C;
    case UIA_ValueValuePropertyId:
    case UIA_RangeValueValuePropertyId: return 0x800E;
    case UIA_IsEnabledPropertyId:
    case UIA_IsOffscreenPropertyId:
    case UIA_ToggleToggleStatePropertyId:
    case UIA_ExpandCollapseExpandCollapseStatePropertyId:
    case UIA_SelectionItemIsSelectedPropertyId:
    case UIA_HasKeyboardFocusPropertyId: return 0x800A;
    case UIA_BoundingRectanglePropertyId: return 0x800B;
    case UIA_HelpTextPropertyId: return 0x8010;
    case UIA_AcceleratorKeyPropertyId:
    case UIA_AccessKeyPropertyId: return 0x8012;
    case UIA_LegacyIAccessibleDescriptionPropertyId: return 0x800D;
    }
    return 0;
}

/* tells the windows event hooks of this process about an event of a provider. */
static void raise_winevent(NODE *node, DWORD event)
{
    if (!event || !IsWinEventHookInstalled(event))
        return;
    HWND hwnd = NodeOwnerWindow(node);
    if (!hwnd || !IsWindow(hwnd))
        return;
    NODE *root = nullptr;
    HWND host = nullptr;
    node->ops->host_hwnd(node, &host);
    LONG object = OBJID_CLIENT;
    if (host != hwnd || !node->ops->is_fragment(node)) {
        object = map_add(hwnd, node);
    } else {
        (void)root;
    }
    NotifyWinEvent(event, hwnd, object, 0);
}

/* ------------------------------------------------------------------ the answer to WM_GETOBJECT */

static const int g_bridge_events[] = {20005, 20010, 20011, 20012, 20013, 20003, 20007, 20018, 20019, 20002, 20036};

static LRESULT bridge_get_object(HWND hwnd, WPARAM wparam, LPARAM lparam, IRawElementProviderSimple *el)
{
    if (!IsWindow(hwnd) || (lparam == 0 && !el)) {
        map_release_dead(hwnd);
        return 0;
    }
    LONG object = (LONG)lparam;
    IAccessible *accessible = nullptr;
    if (object == OBJID_CLIENT) {
        NODE *node = nullptr;
        if (el && SUCCEEDED(NodeFromProvider(el, &node))) {
            for (int i = 0; i < (int)RTL_NUMBER_OF(g_bridge_events); i++)
                node->ops->advise(node, TRUE, g_bridge_events[i], nullptr);
            int props[] = {UIA_ValueValuePropertyId, UIA_RangeValueValuePropertyId,
                           UIA_ExpandCollapseExpandCollapseStatePropertyId, UIA_ToggleToggleStatePropertyId,
                           UIA_IsEnabledPropertyId, UIA_ScrollHorizontalScrollPercentPropertyId,
                           UIA_ScrollVerticalScrollPercentPropertyId};
            SAFEARRAY *array = nullptr;
            if (SUCCEEDED(UiaIntArrayToSafeArray(props, RTL_NUMBER_OF(props), &array))) {
                node->ops->advise(node, TRUE, UIA_AutomationPropertyChangedEventId, array);
                SafeArrayDestroy(array);
            }
            NodeRelease(node);
        }
        LONG child = 0;
        HRESULT hr = el ? AccessibleWrapProvider(el, &accessible, &child, hwnd) : E_INVALIDARG;
        if (FAILED(hr))
            return hr;
        LRESULT result = LresultFromObject(&IID_IAccessible, wparam, (IUnknown *)accessible);
        accessible->lpVtbl->Release(accessible);
        return result;
    }
    if (object < 1)
        return 0;
    NODE *node = map_find(hwnd, object);
    if (!node)
        return 0;
    LONG child = 0;
    HRESULT hr = AccessibleWrapProvider(node->ops->provider(node), &accessible, &child, hwnd);
    NodeRelease(node);
    if (FAILED(hr))
        return hr;
    LRESULT result = LresultFromObject(&IID_IAccessible, wparam, (IUnknown *)accessible);
    accessible->lpVtbl->Release(accessible);
    return result;
}

LRESULT WINAPI UiaReturnRawElementProvider(HWND hwnd, WPARAM wparam, LPARAM lparam, IRawElementProviderSimple *el)
{
    LRESULT bridged = bridge_get_object(hwnd, wparam, lparam, el);
    if (bridged)
        return bridged;
    if ((LONG)lparam != UiaRootObjectId)
        return 0;
    if (!el)
        return (LRESULT)(LONG_PTR)E_INVALIDARG;
    NODE *node = nullptr;
    HRESULT hr = NodeFromProvider(el, &node);
    if (FAILED(hr))
        return hr;
    IDispatch *wrapper = nullptr;
    hr = ServerWrapNode(node, &wrapper);
    if (SUCCEEDED(hr)) {
        UiaProviderWatchRemember(node);
        UiaProviderTableAdd(hwnd);
        LRESULT result = LresultFromObject(&IID_IDispatch, wparam, (IUnknown *)wrapper);
        wrapper->lpVtbl->Release(wrapper);
        NodeRelease(node);
        return result;
    }
    NodeRelease(node);
    return hr;
}

/* ------------------------------------------------------------------ raising events */

static HRESULT raise(IRawElementProviderSimple *provider, EVENT_ARGS *args, DWORD winevent, BOOL check_options)
{
    NODE *node = nullptr;
    HRESULT hr = NodeFromProvider(provider, &node);
    if (FAILED(hr))
        return hr;
    int options = 0;
    hr = node->ops->options(node, &options);
    if (FAILED(hr)) {
        NodeRelease(node);
        return hr;
    }
    /* providers that are not for the windows events say so with this option. */
    if (!(options & 0x80))
        raise_winevent(node, winevent);
    hr = UiaDispatchEvent(node, args);
    NodeRelease(node);
    return hr;
}

HRESULT WINAPI UiaRaiseAutomationEvent(IRawElementProviderSimple *provider, EVENTID id)
{
    if (!provider)
        return E_INVALIDARG;
    EVENT_ARGS args = {};
    args.type = EventArgsType_Simple;
    args.event_id = id;
    return raise(provider, &args, id ? winevent_of_event(id) : 0, TRUE);
}

HRESULT WINAPI UiaRaiseAutomationPropertyChangedEvent(IRawElementProviderSimple *provider, PROPERTYID id,
                                                      VARIANT old_value, VARIANT new_value)
{
    if (!provider)
        return E_INVALIDARG;
    const PROPERTY_INFO *info = UiaPropertyInfo(id);
    if (!info)
        return E_INVALIDARG;
    if (new_value.vt != VT_EMPTY && new_value.vt != info->vt)
        return E_INVALIDARG;
    if (old_value.vt != VT_EMPTY && old_value.vt != info->vt)
        return E_INVALIDARG;
    EVENT_ARGS args = {};
    args.type = EventArgsType_PropertyChanged;
    args.event_id = UIA_AutomationPropertyChangedEventId;
    args.property = id;
    args.old_value = old_value;
    args.new_value = new_value;
    return raise(provider, &args, winevent_of_property(id), TRUE);
}

HRESULT WINAPI UiaRaiseStructureChangedEvent(IRawElementProviderSimple *provider, StructureChangeType type,
                                             int *runtime_id, int length)
{
    if (!provider)
        return E_INVALIDARG;
    EVENT_ARGS args = {};
    args.type = EventArgsType_StructureChanged;
    args.event_id = UIA_StructureChangedEventId;
    args.structure = type;
    args.runtime_id = runtime_id;
    args.runtime_len = length;
    DWORD winevent = type == StructureChangeType_ChildAdded     ? 0x8000
                     : type == StructureChangeType_ChildRemoved ? 0x8001
                                                                : 0x8004;
    return raise(provider, &args, winevent, TRUE);
}

HRESULT WINAPI UiaRaiseAsyncContentLoadedEvent(IRawElementProviderSimple *provider, AsyncContentLoadedState state,
                                               double percent)
{
    if (!provider || !(percent >= 0.0 && percent <= 100.0))
        return E_INVALIDARG;
    EVENT_ARGS args = {};
    args.type = EventArgsType_AsyncContentLoaded;
    args.event_id = UIA_AsyncContentLoadedEventId;
    args.loaded_state = state;
    args.loaded_percent = percent;
    return raise(provider, &args, 0, FALSE);
}

HRESULT WINAPI UiaRaiseNotificationEvent(IRawElementProviderSimple *provider, NotificationKind kind,
                                         NotificationProcessing processing, BSTR display, BSTR activity)
{
    if (!provider)
        return E_INVALIDARG;
    EVENT_ARGS args = {};
    args.type = EventArgsType_Notification;
    args.event_id = UIA_NotificationEventId;
    args.notification_kind = kind;
    args.notification_processing = processing;
    args.display = display;
    args.activity = activity;
    return raise(provider, &args, 0, FALSE);
}

HRESULT WINAPI UiaRaiseTextEditTextChangedEvent(IRawElementProviderSimple *provider, TextEditChangeType type,
                                                SAFEARRAY *changed)
{
    if (!provider || (unsigned)(type - 1) > 3)
        return E_INVALIDARG;
    EVENT_ARGS args = {};
    args.type = EventArgsType_TextEditTextChanged;
    args.event_id = UIA_TextEdit_TextChangedEventId;
    args.edit_type = type;
    args.edit_data = changed;
    return raise(provider, &args, 0, FALSE);
}

HRESULT WINAPI UiaRaiseChangesEvent(IRawElementProviderSimple *provider, int count, UiaChangeInfo *changes)
{
    if (!provider || count <= 0 || !changes)
        return E_INVALIDARG;
    EVENT_ARGS args = {};
    args.type = EventArgsType_Changes;
    args.event_id = UIA_ChangesEventId;
    args.change_count = count;
    args.changes = changes;
    return raise(provider, &args, 0, FALSE);
}

HRESULT WINAPI UiaRaiseActiveTextPositionChangedEvent(IRawElementProviderSimple *provider, ITextRangeProvider *range)
{
    if (!provider)
        return E_INVALIDARG;
    EVENT_ARGS args = {};
    args.type = EventArgsType_Simple;
    args.event_id = UIA_ActiveTextPositionChangedEventId;
    return raise(provider, &args, 0x4E44, FALSE);
}

/* ------------------------------------------------------------------ taking providers away */

HRESULT WINAPI UiaDisconnectProvider(IRawElementProviderSimple *provider)
{
    if (!provider)
        return E_INVALIDARG;
    if ((InSendMessageEx(nullptr) & (ISMEX_SEND | ISMEX_REPLIED)) == ISMEX_SEND)
        return RPC_E_CANTCALLOUT_ININPUTSYNCCALL;
    NODE *node = nullptr;
    HRESULT hr = NodeFromProvider(provider, &node);
    if (FAILED(hr))
        return hr;
    int *rid = nullptr, count = 0;
    hr = NodeRuntimeId(node, &rid, &count);
    NodeRelease(node);
    if (FAILED(hr))
        return hr;
    if (!count) {
        UiaFree(rid);
        return E_INVALIDARG;
    }
    ServerDisconnectProvider(provider, rid, count);
    map_release_runtime_id(rid, count);
    UiaFree(rid);
    return S_OK;
}

HRESULT WINAPI UiaDisconnectAllProviders(void)
{
    ServerDisconnectAll();
    map_release_all();
    return S_OK;
}
