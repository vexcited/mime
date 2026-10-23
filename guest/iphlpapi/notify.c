/*
 * change notifications. while anyone listens, one thread polls the host every two seconds, diffs the snapshot
 * against the previous one, calls the address, interface and route callbacks, completes NotifyAddrChange and
 * NotifyRouteChange requests and updates the connectivity hint listeners. windows calls back on thread pool
 * threads, here it is this thread, which also delivers the initial notifications.
 */
#include "private.h"

#define POLL_MS 2000

typedef enum { KIND_ROUTE, KIND_INTERFACE, KIND_ADDRESS, KIND_HINT } KIND;

typedef struct REGISTRATION {
    struct REGISTRATION *next;
    KIND kind;
    ADDRESS_FAMILY family;
    void *callback;
    void *context;
    BOOL initial; /* the initial notification is due. */
    LONG active;  /* callbacks running. */
    BOOL cancelled;
    NL_NETWORK_CONNECTIVITY_HINT hint; /* the last hint delivered. */
} REGISTRATION;

typedef struct WAITER {
    struct WAITER *next;
    BOOL route; /* else an ipv4 address change. */
    LPOVERLAPPED overlapped;
    HANDLE event; /* for a synchronous caller. */
} WAITER;

typedef void(NETIOAPI_API_ *ROW_CALLBACK)(PVOID context, PVOID row, MIB_NOTIFICATION_TYPE type);

static SRWLOCK lock = SRWLOCK_INIT;
static CONDITION_VARIABLE idle = CONDITION_VARIABLE_INIT;
static REGISTRATION *registrations;
static WAITER *waiters;
static HANDLE wake;
static HANDLE thread;
static DWORD thread_id;
static HANDLE change_handle;
static IPH_SNAPSHOT *previous;

/* ------------------------------------------------------------------ calling back */

typedef struct {
    REGISTRATION **list;
    ULONG count;
} BATCH;

/* registrations of a kind and family, held active so they can be called outside the lock. */
static BATCH hold(KIND kind, ADDRESS_FAMILY family, BOOL initial_only)
{
    BATCH b = {};
    AcquireSRWLockExclusive(&lock);
    ULONG n = 0;
    for (REGISTRATION *r = registrations; r; r = r->next)
        n++;
    b.list = n ? IphAlloc(n * sizeof(*b.list)) : nullptr;
    for (REGISTRATION *r = registrations; b.list && r; r = r->next) {
        /* changes wait until the initial notification is delivered. */
        if (initial_only ? !r->initial
                         : (r->initial || r->kind != kind || (family && r->family && r->family != family)))
            continue;
        if (initial_only)
            r->initial = FALSE;
        r->active++;
        b.list[b.count++] = r;
    }
    ReleaseSRWLockExclusive(&lock);
    return b;
}

static void unhold(BATCH *b)
{
    AcquireSRWLockExclusive(&lock);
    for (ULONG i = 0; i < b->count; i++) {
        REGISTRATION *r = b->list[i];
        /* cancelled from its own callback: freed once it returns. */
        if (!--r->active && r->cancelled)
            IphFree(r);
    }
    ReleaseSRWLockExclusive(&lock);
    WakeAllConditionVariable(&idle);
    IphFree(b->list);
}

static void call_rows(KIND kind, ADDRESS_FAMILY family, void *row, MIB_NOTIFICATION_TYPE type)
{
    BATCH b = hold(kind, family, FALSE);
    for (ULONG i = 0; i < b.count; i++)
        if (!b.list[i]->cancelled)
            ((ROW_CALLBACK)b.list[i]->callback)(b.list[i]->context, row, type);
    unhold(&b);
}

static void initial_notifications(void)
{
    BATCH b = hold(KIND_ROUTE, AF_UNSPEC, TRUE);
    if (!b.count) {
        IphFree(b.list);
        return;
    }
    IPH_SNAPSHOT *s = IphAcquire();
    NL_NETWORK_CONNECTIVITY_HINT hint;
    IphHint(s, nullptr, &hint);
    IphRelease(s);
    for (ULONG i = 0; i < b.count; i++) {
        REGISTRATION *r = b.list[i];
        if (r->cancelled)
            continue;
        if (r->kind == KIND_HINT) {
            r->hint = hint;
            ((PNETWORK_CONNECTIVITY_HINT_CHANGE_CALLBACK)r->callback)(r->context, hint);
        } else {
            ((ROW_CALLBACK)r->callback)(r->context, nullptr, MibInitialNotification);
        }
    }
    unhold(&b);
}

/* ------------------------------------------------------------------ comparing snapshots */

static BOOL same_address(const IPH_ADDRESS *a, const IPH_ADDRESS *b)
{
    return a->family == b->family && a->index == b->index && !memcmp(a->bytes, b->bytes, 16);
}

static const IPH_ADDRESS *find_address(const IPH_SNAPSHOT *s, const IPH_ADDRESS *key)
{
    for (ULONG i = 0; i < s->address_count; i++)
        if (same_address(&s->addresses[i], key))
            return &s->addresses[i];
    return nullptr;
}

static void address_event(const IPH_SNAPSHOT *s, const IPH_ADDRESS *a, MIB_NOTIFICATION_TYPE type, BOOL *v4)
{
    MIB_UNICASTIPADDRESS_ROW row;
    IphUnicastRow(s, a, &row);
    call_rows(KIND_ADDRESS, a->family, &row, type);
    if (a->family == AF_INET)
        *v4 = TRUE;
}

static void diff_addresses(const IPH_SNAPSHOT *old, const IPH_SNAPSHOT *now, BOOL *v4)
{
    for (ULONG i = 0; i < old->address_count; i++)
        if (!find_address(now, &old->addresses[i]))
            address_event(old, &old->addresses[i], MibDeleteInstance, v4);
    for (ULONG i = 0; i < now->address_count; i++) {
        const IPH_ADDRESS *a = &now->addresses[i], *was = find_address(old, a);
        if (!was)
            address_event(now, a, MibAddInstance, v4);
        else if (was->prefix != a->prefix || was->dad != a->dad || was->flags != a->flags)
            address_event(now, a, MibParameterNotification, v4);
    }
}

static void interface_event(const IPH_ADAPTER *a, ADDRESS_FAMILY family, MIB_NOTIFICATION_TYPE type)
{
    MIB_IPINTERFACE_ROW row = {};
    row.Family = family;
    row.InterfaceLuid = a->luid;
    row.InterfaceIndex = a->index;
    call_rows(KIND_INTERFACE, family, &row, type);
}

static void diff_interfaces(const IPH_SNAPSHOT *old, const IPH_SNAPSHOT *now)
{
    for (int pass = 0; pass < 2; pass++) {
        ADDRESS_FAMILY family = pass ? AF_INET6 : AF_INET;
        for (ULONG i = 0; i < old->adapter_count; i++) {
            const IPH_ADAPTER *a = &old->adapters[i], *b = IphAdapterByIndex(now, a->index);
            if (IphFamilyEnabled(a, family) && (!b || !IphFamilyEnabled(b, family)))
                interface_event(a, family, MibDeleteInstance);
        }
        for (ULONG i = 0; i < now->adapter_count; i++) {
            const IPH_ADAPTER *a = &now->adapters[i], *was = IphAdapterByIndex(old, a->index);
            if (!IphFamilyEnabled(a, family))
                continue;
            if (!was || !IphFamilyEnabled(was, family))
                interface_event(a, family, MibAddInstance);
            else if (was->up != a->up || was->mtu != a->mtu || was->metric != a->metric)
                interface_event(a, family, MibParameterNotification);
        }
    }
}

static BOOL same_route(const IPH_ROUTE *a, const IPH_ROUTE *b)
{
    return a->family == b->family && a->index == b->index && a->prefix == b->prefix &&
           !memcmp(a->destination, b->destination, 16) && !memcmp(a->next_hop, b->next_hop, 16);
}

static const IPH_ROUTE *find_route(const IPH_SNAPSHOT *s, const IPH_ROUTE *key)
{
    for (ULONG i = 0; i < s->route_count; i++)
        if (same_route(&s->routes[i], key))
            return &s->routes[i];
    return nullptr;
}

static void route_event(const IPH_SNAPSHOT *s, const IPH_ROUTE *r, MIB_NOTIFICATION_TYPE type, BOOL *v4)
{
    /* like windows, the row holds only the route's key. */
    MIB_IPFORWARD_ROW2 full, row = {};
    IphRouteRow(s, r, &full);
    row.InterfaceLuid = full.InterfaceLuid;
    row.InterfaceIndex = full.InterfaceIndex;
    row.DestinationPrefix = full.DestinationPrefix;
    row.NextHop = full.NextHop;
    call_rows(KIND_ROUTE, r->family, &row, type);
    if (r->family == AF_INET)
        *v4 = TRUE;
}

static void diff_routes(const IPH_SNAPSHOT *old, const IPH_SNAPSHOT *now, BOOL *v4)
{
    for (ULONG i = 0; i < old->route_count; i++)
        if (!find_route(now, &old->routes[i]))
            route_event(old, &old->routes[i], MibDeleteInstance, v4);
    for (ULONG i = 0; i < now->route_count; i++) {
        const IPH_ROUTE *r = &now->routes[i], *was = find_route(old, r);
        if (!was)
            route_event(now, r, MibAddInstance, v4);
        else if (was->metric != r->metric)
            route_event(now, r, MibParameterNotification, v4);
    }
}

static void tell_hint(const IPH_SNAPSHOT *now)
{
    NL_NETWORK_CONNECTIVITY_HINT hint;
    IphHint(now, nullptr, &hint);
    BATCH b = hold(KIND_HINT, AF_UNSPEC, FALSE);
    for (ULONG i = 0; i < b.count; i++) {
        REGISTRATION *r = b.list[i];
        if (r->cancelled || !memcmp(&r->hint, &hint, sizeof(hint)))
            continue;
        r->hint = hint;
        ((PNETWORK_CONNECTIVITY_HINT_CHANGE_CALLBACK)r->callback)(r->context, hint);
    }
    unhold(&b);
}

static void complete(WAITER *w, NTSTATUS status)
{
    if (w->overlapped) {
        w->overlapped->InternalHigh = 0;
        w->overlapped->Internal = (ULONG_PTR)(ULONG)status;
        HANDLE event = (HANDLE)((ULONG_PTR)w->overlapped->hEvent & ~(ULONG_PTR)1);
        if (event)
            SetEvent(event);
        SetEvent(change_handle);
    } else {
        SetEvent(w->event);
    }
}

static void finish_waiters(BOOL address, BOOL route)
{
    AcquireSRWLockExclusive(&lock);
    for (WAITER **p = &waiters; *p;) {
        WAITER *w = *p;
        if (w->route ? route : address) {
            *p = w->next;
            complete(w, STATUS_SUCCESS);
            if (w->overlapped)
                IphFree(w);
        } else {
            p = &w->next;
        }
    }
    ReleaseSRWLockExclusive(&lock);
}

static DWORD WINAPI monitor(LPVOID parameter)
{
    for (;;) {
        AcquireSRWLockShared(&lock);
        BOOL listening = registrations || waiters;
        ReleaseSRWLockShared(&lock);
        WaitForSingleObject(wake, listening ? POLL_MS : INFINITE);
        initial_notifications();
        AcquireSRWLockShared(&lock);
        listening = registrations || waiters;
        ReleaseSRWLockShared(&lock);
        if (!listening) {
            IphRelease(previous);
            previous = nullptr;
            continue;
        }
        IPH_SNAPSHOT *now = IphFresh();
        if (previous) {
            BOOL address = FALSE, route = FALSE;
            diff_addresses(previous, now, &address);
            diff_interfaces(previous, now);
            diff_routes(previous, now, &route);
            tell_hint(now);
            finish_waiters(address, route);
        }
        IphRelease(previous);
        previous = now;
    }
    return 0;
}

/* starts the thread and wakes it for newly queued work. called with the lock held. */
static BOOL start(void)
{
    if (!wake && !(wake = CreateEventW(nullptr, FALSE, FALSE, nullptr)))
        return FALSE;
    if (!change_handle && !(change_handle = CreateEventW(nullptr, TRUE, FALSE, nullptr)))
        return FALSE;
    if (!thread && !(thread = CreateThread(nullptr, 0, monitor, nullptr, 0, &thread_id)))
        return FALSE;
    SetEvent(wake);
    return TRUE;
}

/* ------------------------------------------------------------------ NotifyAddrChange and NotifyRouteChange */

static DWORD request_change(BOOL route, PHANDLE handle, LPOVERLAPPED overlapped)
{
    WAITER *w = IphAlloc(sizeof(*w));
    if (!w)
        return ERROR_NOT_ENOUGH_MEMORY;
    w->route = route;
    w->overlapped = overlapped;
    if (!overlapped && !(w->event = CreateEventW(nullptr, TRUE, FALSE, nullptr))) {
        IphFree(w);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    AcquireSRWLockExclusive(&lock);
    if (!start()) {
        ReleaseSRWLockExclusive(&lock);
        if (w->event)
            CloseHandle(w->event);
        IphFree(w);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    if (overlapped) {
        overlapped->Internal = (ULONG_PTR)(ULONG)STATUS_PENDING;
        overlapped->InternalHigh = 0;
        HANDLE event = (HANDLE)((ULONG_PTR)overlapped->hEvent & ~(ULONG_PTR)1);
        if (event)
            ResetEvent(event);
        ResetEvent(change_handle);
        if (handle)
            *handle = change_handle;
    }
    w->next = waiters;
    waiters = w;
    ReleaseSRWLockExclusive(&lock);
    if (overlapped)
        return ERROR_IO_PENDING;
    /* without an OVERLAPPED the call blocks until the change. */
    WaitForSingleObject(w->event, INFINITE);
    CloseHandle(w->event);
    IphFree(w);
    return NO_ERROR;
}

DWORD WINAPI NotifyAddrChange(PHANDLE handle, LPOVERLAPPED overlapped)
{
    return request_change(FALSE, handle, overlapped);
}

DWORD WINAPI NotifyRouteChange(PHANDLE handle, LPOVERLAPPED overlapped)
{
    return request_change(TRUE, handle, overlapped);
}

BOOL WINAPI CancelIPChangeNotify(LPOVERLAPPED overlapped)
{
    if (!overlapped)
        return FALSE;
    WAITER *found = nullptr;
    AcquireSRWLockExclusive(&lock);
    for (WAITER **p = &waiters; *p; p = &(*p)->next) {
        if ((*p)->overlapped == overlapped) {
            found = *p;
            *p = found->next;
            complete(found, STATUS_CANCELLED);
            break;
        }
    }
    ReleaseSRWLockExclusive(&lock);
    IphFree(found);
    return found != nullptr;
}

/* ------------------------------------------------------------------ the callbacks */

static DWORD notify(KIND kind, ADDRESS_FAMILY family, void *callback, PVOID context, BOOLEAN initial, HANDLE *handle)
{
    if (!handle)
        return ERROR_INVALID_PARAMETER;
    *handle = nullptr;
    if (!callback || !IphValidFamily(family))
        return ERROR_INVALID_PARAMETER;
    REGISTRATION *r = IphAlloc(sizeof(*r));
    if (!r)
        return ERROR_NOT_ENOUGH_MEMORY;
    r->kind = kind;
    r->family = family;
    r->callback = callback;
    r->context = context;
    r->initial = initial;
    if (kind == KIND_HINT && !initial) {
        IPH_SNAPSHOT *s = IphAcquire();
        IphHint(s, nullptr, &r->hint);
        IphRelease(s);
    }
    AcquireSRWLockExclusive(&lock);
    if (!start()) {
        ReleaseSRWLockExclusive(&lock);
        IphFree(r);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    r->next = registrations;
    registrations = r;
    ReleaseSRWLockExclusive(&lock);
    *handle = (HANDLE)r;
    return NO_ERROR;
}

DWORD NETIOAPI_API_ NotifyRouteChange2(ADDRESS_FAMILY family, PIPFORWARD_CHANGE_CALLBACK callback, PVOID context,
                                       BOOLEAN initial, HANDLE *handle)
{
    return notify(KIND_ROUTE, family, (void *)callback, context, initial, handle);
}

DWORD NETIOAPI_API_ NotifyIpInterfaceChange(ADDRESS_FAMILY family, PIPINTERFACE_CHANGE_CALLBACK callback,
                                            PVOID context, BOOLEAN initial, HANDLE *handle)
{
    return notify(KIND_INTERFACE, family, (void *)callback, context, initial, handle);
}

DWORD NETIOAPI_API_ NotifyUnicastIpAddressChange(ADDRESS_FAMILY family, PUNICAST_IPADDRESS_CHANGE_CALLBACK callback,
                                                 PVOID context, BOOLEAN initial, HANDLE *handle)
{
    return notify(KIND_ADDRESS, family, (void *)callback, context, initial, handle);
}

DWORD NETIOAPI_API_ NotifyNetworkConnectivityHintChange(PNETWORK_CONNECTIVITY_HINT_CHANGE_CALLBACK callback,
                                                        PVOID context, BOOLEAN initial, HANDLE *handle)
{
    return notify(KIND_HINT, AF_UNSPEC, (void *)callback, context, initial, handle);
}

DWORD NETIOAPI_API_ CancelMibChangeNotify2(HANDLE handle)
{
    if (!handle)
        return ERROR_INVALID_HANDLE;
    REGISTRATION *found = nullptr;
    AcquireSRWLockExclusive(&lock);
    for (REGISTRATION **p = &registrations; *p; p = &(*p)->next) {
        if (*p == (REGISTRATION *)handle) {
            found = *p;
            *p = found->next;
            break;
        }
    }
    if (!found) {
        ReleaseSRWLockExclusive(&lock);
        return ERROR_INVALID_HANDLE;
    }
    found->cancelled = TRUE;
    /* wait for a running callback, unless that callback is the one cancelling: then it is freed once it returns. */
    if (found->active && GetCurrentThreadId() == thread_id) {
        ReleaseSRWLockExclusive(&lock);
        return NO_ERROR;
    }
    while (found->active)
        SleepConditionVariableSRW(&idle, &lock, INFINITE, 0);
    ReleaseSRWLockExclusive(&lock);
    IphFree(found);
    return NO_ERROR;
}
