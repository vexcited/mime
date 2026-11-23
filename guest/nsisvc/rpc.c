/*
 * the rpc interface of the nsi service as winnsi.dll calls it. the requests of the store go to the device as the
 * caller's own would. a change notification is a context handle: RegisterChangeNotification makes it for a table,
 * RequestChangeNotification answers with the next change of the table, the key of the object and whether it came, went
 * or changed. that call returns at once and is answered when there is a change, as an asynchronous one: rpc serves
 * the calls of a thread on a binding in their order, and winnsi registers its next notification on the same binding
 * while this one waits. winnsi asks again after each answer, and ends with DeregisterChangeNotification, which the
 * waiting call answers with ERROR_CANCELLED.
 */
#include "nsisvc_private.h"
#include "nsisvc/nsi.h"

#define NOTIFY_PARAMETER 0
#define NOTIFY_ADD 1
#define NOTIFY_DELETE 2

/* a table some registration watches, with the objects it had when last looked at. */
typedef struct WATCH {
    struct WATCH *Next;
    NSI_MIME_OBJECTS_QUERY Query;
    NSI_MIME_OBJECTS *Objects;
    ULONG Users;
    HANDLE Thread;
    HANDLE Stop;
} WATCH;

typedef struct RECORD {
    struct RECORD *Next;
    ULONG Type;
    ULONG KeySize;
    UCHAR Key[];
} RECORD;

typedef struct REGISTRATION {
    struct REGISTRATION *Next;
    WATCH *Watch;
    RECORD *First, *Last;
    BOOL Closed;
    /* the call that waits for a change, and where its answer goes. */
    PRPC_ASYNC_STATE Async;
    NSI_RPC_KEY *Key;
    NSI_RPC_NOTIFICATION *Type;
} REGISTRATION;

typedef enum { RpcNotificationCallNone, RpcNotificationClientDisconnect, RpcNotificationCallCancel } RPC_NOTIFICATIONS;

typedef union {
    struct {
        PFN_RPCNOTIFICATION_ROUTINE NotificationRoutine;
        HANDLE hThread;
    } APC;
    struct {
        HANDLE hIOPort;
        DWORD dwNumberOfBytesTransferred;
        DWORD_PTR dwCompletionKey;
        LPOVERLAPPED lpOverlapped;
    } IOC;
    HANDLE hEvent;
    PFN_RPCNOTIFICATION_ROUTINE NotificationRoutine;
} RPC_ASYNC_NOTIFICATION_INFO;

RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerSubscribeForNotification(RPC_BINDING_HANDLE binding, RPC_NOTIFICATIONS notification,
                                                                RPC_NOTIFICATION_TYPES type,
                                                                RPC_ASYNC_NOTIFICATION_INFO *info);
RPCRTAPI RPC_STATUS RPC_ENTRY RpcServerUnsubscribeForNotification(RPC_BINDING_HANDLE binding,
                                                                  RPC_NOTIFICATIONS notification, ULONG *queued);

static CRITICAL_SECTION g_lock;
static BOOL g_started;
static HANDLE g_device = INVALID_HANDLE_VALUE; /* requests answered at once */
static HANDLE g_waits = INVALID_HANDLE_VALUE;  /* the overlapped waits for a change */
static WATCH *g_watches;
static REGISTRATION *g_registrations;

void *WINAPI MIDL_user_allocate(size_t size)
{
    return LocalAlloc(LMEM_FIXED, size);
}

void WINAPI MIDL_user_free(void *p)
{
    LocalFree(p);
}

static void *allocate(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

static void release(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

/* ------------------------------------------------------------------ the store's requests */

static DWORD control(ULONG code, void *request, ULONG size)
{
    DWORD returned = 0;
    if (g_device == INVALID_HANDLE_VALUE)
        return ERROR_NOT_READY;
    return DeviceIoControl(g_device, code, request, size, request, size, &returned, nullptr) ? ERROR_SUCCESS
                                                                                            : GetLastError();
}

/* the module of a request as the device takes it: a guid in a structure of 24 bytes, the layout rpc gave it. */
static const NSI_RPC_MODULEID *module_of(const NSI_RPC_OBJECT *object)
{
    return object && object->ModuleId && object->ModuleId->Type == NsiRpcModuleGuid ? object->ModuleId : nullptr;
}

unsigned long WINAPI nsirpc_GetParameter_s(handle_t h, NSI_RPC_OBJECT *object, NSI_RPC_STORE store,
                                           NSI_RPC_GET_ACTION action, NSI_RPC_KEY *key, NSI_RPC_PARAMETER *parameter)
{
    if (!module_of(object) || (parameter->Length && !parameter->Data))
        return ERROR_INVALID_PARAMETER;
    NSI_GET_PARAMETER r = {};
    r.Module = (ULONG_PTR)module_of(object);
    r.Table = object->Table;
    r.Store = store;
    r.Action = action;
    r.Key = (ULONG_PTR)key->Data;
    r.KeySize = key->Length;
    r.Type = parameter->Type;
    r.Data = (ULONG_PTR)parameter->Data;
    r.DataSize = parameter->Length;
    r.DataOffset = parameter->Offset;
    return control(IOCTL_NSI_GET_PARAMETER, &r, sizeof(r));
}

unsigned long WINAPI nsirpc_GetAllParameters_s(handle_t h, NSI_RPC_OBJECT *object, NSI_RPC_STORE store,
                                               NSI_RPC_GET_ACTION action, NSI_RPC_KEY *key,
                                               NSI_RPC_ALL_PARAMETERS *parameters)
{
    if (!module_of(object))
        return ERROR_INVALID_PARAMETER;
    NSI_GET_ALL_PARAMETERS r = {};
    r.Module = (ULONG_PTR)module_of(object);
    r.Table = object->Table;
    r.Store = store;
    r.Action = action;
    r.Key = (ULONG_PTR)key->Data;
    r.KeySize = key->Length;
    r.Rw = (ULONG_PTR)parameters->Rw;
    r.RwSize = parameters->RwLength;
    r.Dynamic = (ULONG_PTR)parameters->Dynamic;
    r.DynamicSize = parameters->DynamicLength;
    r.Static = (ULONG_PTR)parameters->Static;
    r.StaticSize = parameters->StaticLength;
    return control(IOCTL_NSI_GET_ALL_PARAMETERS, &r, sizeof(r));
}

unsigned long WINAPI nsirpc_EnumerateObjectsAllParameters_s(handle_t h, NSI_RPC_OBJECT *object, NSI_RPC_STORE store,
                                                            NSI_RPC_GET_ACTION action, NSI_RPC_KEYS *keys,
                                                            NSI_RPC_ALL_OBJECTS *objects, unsigned long *count)
{
    if (!module_of(object))
        return ERROR_INVALID_PARAMETER;
    /* the buffers hold what their totals say, whatever count the caller gives. */
    ULONG64 room = *count;
    if ((ULONG64)keys->Size * room > keys->Total || (ULONG64)objects->RwSize * room > objects->RwTotal ||
        (ULONG64)objects->DynamicSize * room > objects->DynamicTotal ||
        (ULONG64)objects->StaticSize * room > objects->StaticTotal)
        return ERROR_INVALID_PARAMETER;
    NSI_ENUMERATE_ALL r = {};
    r.Module = (ULONG_PTR)module_of(object);
    r.Table = object->Table;
    r.Store = store;
    r.Action = action;
    r.Key = (ULONG_PTR)keys->Data;
    r.KeySize = keys->Size;
    r.Rw = (ULONG_PTR)objects->Rw;
    r.RwSize = objects->RwSize;
    r.Dynamic = (ULONG_PTR)objects->Dynamic;
    r.DynamicSize = objects->DynamicSize;
    r.Static = (ULONG_PTR)objects->Static;
    r.StaticSize = objects->StaticSize;
    r.Count = *count;
    DWORD error = control(IOCTL_NSI_ENUMERATE_ALL, &r, sizeof(r));
    *count = (ULONG)r.Count;
    return error;
}

/* the stack is the host's and takes no value from a program, see the device. */
unsigned long WINAPI nsirpc_SetParameter_s(handle_t h, NSI_RPC_OBJECT *object, NSI_RPC_STORE store,
                                           NSI_RPC_SET_ACTION action, NSI_RPC_KEY *key, NSI_RPC_PARAMETER *parameter)
{
    return module_of(object) ? ERROR_ACCESS_DENIED : ERROR_INVALID_PARAMETER;
}

unsigned long WINAPI nsirpc_SetAllParameters_s(handle_t h, NSI_RPC_OBJECT *object, NSI_RPC_STORE store,
                                               NSI_RPC_SET_ACTION action, NSI_RPC_KEY *key, NSI_RPC_RW *parameters)
{
    return module_of(object) ? ERROR_ACCESS_DENIED : ERROR_INVALID_PARAMETER;
}

/* the kernel's own procedure on windows, which no program may call. */
unsigned long WINAPI nsirpc_ParameterChange_s(handle_t h, NSI_RPC_OBJECT *object, NSI_RPC_KEY *key,
                                              NSI_RPC_PARAMETER *parameter, NSI_RPC_COMPARTMENT_ID compartment,
                                              NSI_RPC_NOTIFICATION type)
{
    return ERROR_INVALID_PARAMETER;
}

/* ------------------------------------------------------------------ watching a table */

/* the objects of a table, each a key and a hash of the rest. */
static NSI_MIME_OBJECTS *list_objects(const NSI_MIME_OBJECTS_QUERY *query, DWORD *error)
{
    for (ULONG size = 4096; size <= 1u << 24; size *= 4) {
        NSI_MIME_OBJECTS *list = allocate(size);
        if (!list) {
            *error = ERROR_NOT_ENOUGH_MEMORY;
            return nullptr;
        }
        DWORD returned = 0;
        if (DeviceIoControl(g_device, IOCTL_NSI_MIME_OBJECTS, (void *)query, sizeof(*query), list, size, &returned,
                            nullptr)) {
            *error = ERROR_SUCCESS;
            return list;
        }
        *error = GetLastError();
        release(list);
        if (*error != ERROR_MORE_DATA)
            return nullptr;
    }
    return nullptr;
}

static const UCHAR *find_object(const NSI_MIME_OBJECTS *list, const UCHAR *key)
{
    SIZE_T each = (SIZE_T)list->KeySize + sizeof(ULONG64);
    for (ULONG i = 0; i < list->Count; i++)
        if (!memcmp(list->Objects + i * each, key, list->KeySize))
            return list->Objects + i * each;
    return nullptr;
}

/* answers the call waiting on a registration; the lock is held. */
static void answer(REGISTRATION *r, ULONG reply)
{
    PRPC_ASYNC_STATE async = r->Async;
    ULONG queued = 0;
    r->Async = nullptr;
    RpcServerUnsubscribeForNotification(async->RuntimeInfo, RpcNotificationClientDisconnect, &queued);
    RpcAsyncCompleteCall(async, &reply);
}

/* gives the waiting call the oldest change of a registration; the lock is held. */
static void deliver(REGISTRATION *r)
{
    RECORD *record = r->First;
    UCHAR *copy = record->KeySize ? MIDL_user_allocate(record->KeySize) : nullptr;
    if (record->KeySize && !copy) {
        answer(r, ERROR_NOT_ENOUGH_MEMORY);
        return;
    }
    if (copy)
        memcpy(copy, record->Key, record->KeySize);
    r->Key->Data = copy;
    r->Key->Length = record->KeySize;
    *r->Type = record->Type;
    r->First = record->Next;
    if (!r->First)
        r->Last = nullptr;
    release(record);
    answer(r, ERROR_SUCCESS);
}

/* tells every registration of the table about an object; the lock is held. */
static void report(WATCH *w, const UCHAR *key, ULONG key_size, ULONG type)
{
    for (REGISTRATION *r = g_registrations; r; r = r->Next) {
        if (r->Watch != w || r->Closed)
            continue;
        RECORD *record = allocate(sizeof(*record) + key_size);
        if (!record)
            continue;
        record->Type = type;
        record->KeySize = key_size;
        memcpy(record->Key, key, key_size);
        if (r->Last)
            r->Last->Next = record;
        else
            r->First = record;
        r->Last = record;
        if (r->Async)
            deliver(r);
    }
}

/* what went, what came and what changed between two lists of a table. */
static void compare(WATCH *w, const NSI_MIME_OBJECTS *before, const NSI_MIME_OBJECTS *after)
{
    SIZE_T each = (SIZE_T)after->KeySize + sizeof(ULONG64);
    for (ULONG i = 0; i < before->Count; i++)
        if (!find_object(after, before->Objects + i * each))
            report(w, before->Objects + i * each, before->KeySize, NOTIFY_DELETE);
    for (ULONG i = 0; i < after->Count; i++) {
        const UCHAR *now = after->Objects + i * each;
        const UCHAR *was = find_object(before, now);
        if (!was)
            report(w, now, after->KeySize, NOTIFY_ADD);
        else if (memcmp(was + after->KeySize, now + after->KeySize, sizeof(ULONG64)))
            report(w, now, after->KeySize, NOTIFY_PARAMETER);
    }
}

/*
 * waits on the device for the table to change, then tells the registrations what did. the wait is asked for before
 * the table is read, so that no change falls between the two.
 */
static DWORD WINAPI watch_thread(void *context)
{
    WATCH *w = context;
    OVERLAPPED overlapped = {};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent)
        return 0;
    for (;;) {
        NSI_REQUEST_CHANGE request = {};
        request.Module = (ULONG_PTR)&w->Query.Module;
        request.Table = w->Query.Table;
        request.Overlapped = (ULONG_PTR)&overlapped;
        ResetEvent(overlapped.hEvent);
        DWORD returned;
        BOOL pending = !DeviceIoControl(g_waits, IOCTL_NSI_REQUEST_CHANGE, &request, sizeof(request), nullptr, 0,
                                        &returned, &overlapped) &&
                       GetLastError() == ERROR_IO_PENDING;
        DWORD error;
        NSI_MIME_OBJECTS *now = list_objects(&w->Query, &error);
        if (now) {
            EnterCriticalSection(&g_lock);
            if (w->Objects && w->Objects->KeySize == now->KeySize)
                compare(w, w->Objects, now);
            release(w->Objects);
            w->Objects = now;
            LeaveCriticalSection(&g_lock);
        }
        HANDLE handles[2] = {w->Stop, overlapped.hEvent};
        DWORD wait = WaitForMultipleObjects(pending ? 2 : 1, handles, FALSE, pending ? INFINITE : 1000);
        if (wait == WAIT_OBJECT_0) {
            if (pending) {
                CancelIoEx(g_waits, &overlapped);
                WaitForSingleObject(overlapped.hEvent, INFINITE);
            }
            break;
        }
    }
    CloseHandle(overlapped.hEvent);
    return 0;
}

/* the watch of a table, made with the first registration for it; the lock is held. */
static WATCH *acquire_watch(const NSI_RPC_MODULEID *module, ULONG table, DWORD *error)
{
    for (WATCH *w = g_watches; w; w = w->Next) {
        if (w->Query.Table == table && !memcmp(&w->Query.Module.Guid, &module->u.Guid, sizeof(GUID))) {
            w->Users++;
            return w;
        }
    }
    WATCH *w = allocate(sizeof(*w));
    if (!w) {
        *error = ERROR_NOT_ENOUGH_MEMORY;
        return nullptr;
    }
    w->Query.Module.Length = sizeof(w->Query.Module);
    w->Query.Module.Type = 1;
    w->Query.Module.Guid = module->u.Guid;
    w->Query.Table = table;
    /* a table the device cannot list has nothing to watch. */
    w->Objects = list_objects(&w->Query, error);
    if (w->Objects)
        w->Stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (w->Stop)
        w->Thread = CreateThread(nullptr, 0, watch_thread, w, 0, nullptr);
    if (!w->Thread) {
        if (w->Objects)
            *error = GetLastError();
        if (w->Stop)
            CloseHandle(w->Stop);
        release(w->Objects);
        release(w);
        return nullptr;
    }
    w->Users = 1;
    w->Next = g_watches;
    g_watches = w;
    return w;
}

/* ends the watch with its last registration. the lock is held and released while the thread ends. */
static void release_watch(WATCH *w)
{
    if (--w->Users)
        return;
    for (WATCH **at = &g_watches; *at; at = &(*at)->Next) {
        if (*at == w) {
            *at = w->Next;
            break;
        }
    }
    LeaveCriticalSection(&g_lock);
    SetEvent(w->Stop);
    WaitForSingleObject(w->Thread, INFINITE);
    CloseHandle(w->Thread);
    CloseHandle(w->Stop);
    release(w->Objects);
    release(w);
    EnterCriticalSection(&g_lock);
}

/* ------------------------------------------------------------------ registrations */

/* a handle of rpc may be one that is gone. the lock is held. */
static BOOL registered(const REGISTRATION *r)
{
    for (const REGISTRATION *e = g_registrations; e; e = e->Next)
        if (e == r)
            return TRUE;
    return FALSE;
}

/* ends a registration: the call waiting on it is answered, its changes are forgotten. the lock is held. */
static void close_registration(REGISTRATION *r)
{
    r->Closed = TRUE;
    if (r->Async)
        answer(r, ERROR_CANCELLED);
    for (REGISTRATION **at = &g_registrations; *at; at = &(*at)->Next) {
        if (*at == r) {
            *at = r->Next;
            break;
        }
    }
    while (r->First) {
        RECORD *next = r->First->Next;
        release(r->First);
        r->First = next;
    }
    WATCH *w = r->Watch;
    release(r);
    release_watch(w);
}

unsigned long WINAPI nsirpc_RegisterChangeNotification_s(handle_t h, NSI_RPC_OBJECT *object,
                                                         NSI_RPC_COMPARTMENT compartment, NOTIFICATION_HANDLE *handle)
{
    *handle = nullptr;
    const NSI_RPC_MODULEID *module = module_of(object);
    if (!module)
        return ERROR_INVALID_PARAMETER;
    REGISTRATION *r = allocate(sizeof(*r));
    if (!r)
        return ERROR_NOT_ENOUGH_MEMORY;
    DWORD error = ERROR_SUCCESS;
    EnterCriticalSection(&g_lock);
    r->Watch = g_started ? acquire_watch(module, object->Table, &error) : nullptr;
    if (r->Watch) {
        r->Next = g_registrations;
        g_registrations = r;
    } else if (!error) {
        error = ERROR_SERVICE_NOT_ACTIVE;
    }
    LeaveCriticalSection(&g_lock);
    if (error) {
        release(r);
        return error;
    }
    *handle = r;
    return ERROR_SUCCESS;
}

unsigned long WINAPI nsirpc_DeregisterChangeNotification_s(handle_t h, NSI_RPC_OBJECT *object,
                                                           NOTIFICATION_HANDLE *handle)
{
    REGISTRATION *r = *handle;
    DWORD error = ERROR_NOT_FOUND;
    EnterCriticalSection(&g_lock);
    if (registered(r)) {
        close_registration(r);
        *handle = nullptr;
        error = ERROR_SUCCESS;
    }
    LeaveCriticalSection(&g_lock);
    return error;
}

/* the client is gone with its registration. */
void WINAPI NOTIFICATION_HANDLE_rundown(void *handle)
{
    EnterCriticalSection(&g_lock);
    if (registered(handle))
        close_registration(handle);
    LeaveCriticalSection(&g_lock);
}

/* the client of a waiting call went away: rpc tells, and the registration ends with it. */
static void RPC_ENTRY client_gone(PRPC_ASYNC_STATE async, void *context, RPC_ASYNC_EVENT event)
{
    REGISTRATION *r = async->UserInfo;
    EnterCriticalSection(&g_lock);
    if (registered(r) && r->Async == async)
        close_registration(r);
    LeaveCriticalSection(&g_lock);
}

void WINAPI nsirpc_RequestChangeNotification_s(PRPC_ASYNC_STATE async, handle_t h, NOTIFICATION_HANDLE handle,
                                               NSI_RPC_KEY *key, NSI_RPC_PARAMETER *parameter,
                                               NSI_RPC_NOTIFICATION *type)
{
    memset(key, 0, sizeof(*key));
    memset(parameter, 0, sizeof(*parameter));
    *type = NsiRpcParameterNotification;
    REGISTRATION *r = handle;
    ULONG reply = ERROR_SUCCESS;
    EnterCriticalSection(&g_lock);
    if (!r || r->Async) {
        reply = ERROR_INVALID_PARAMETER;
    } else if (!registered(r) || r->Closed) {
        reply = ERROR_CANCELLED;
    } else {
        r->Async = async;
        r->Key = key;
        r->Type = type;
        async->UserInfo = r;
        RPC_ASYNC_NOTIFICATION_INFO info = {};
        info.NotificationRoutine = client_gone;
        reply = RpcServerSubscribeForNotification(async->RuntimeInfo, RpcNotificationClientDisconnect,
                                                  RpcNotificationTypeCallback, &info);
        if (reply)
            r->Async = nullptr;
        else if (r->First)
            deliver(r);
    }
    LeaveCriticalSection(&g_lock);
    if (reply)
        RpcAsyncCompleteCall(async, &reply);
}

/* ------------------------------------------------------------------ the service */

DWORD NsiSvcStartNotifications(void)
{
    InitializeCriticalSection(&g_lock);
    g_device = CreateFileW(L"\\\\.\\Nsi", 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    g_waits = CreateFileW(L"\\\\.\\Nsi", 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                          FILE_FLAG_OVERLAPPED, nullptr);
    if (g_device == INVALID_HANDLE_VALUE || g_waits == INVALID_HANDLE_VALUE)
        return GetLastError();
    g_started = TRUE;
    return ERROR_SUCCESS;
}

/* the service stops: every registration closes, and the calls waiting on them answer. */
void NsiSvcStopNotifications(void)
{
    if (!g_started)
        return;
    EnterCriticalSection(&g_lock);
    g_started = FALSE;
    while (g_registrations)
        close_registration(g_registrations);
    LeaveCriticalSection(&g_lock);
}
