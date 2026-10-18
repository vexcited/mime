/*
 * wevtapi: the event log of a machine whose logs are empty. the classic
 * channels exist and hold no event, queries and subscriptions on them
 * succeed and never return one; other channels are not found, and remote
 * sessions have no server to reach.
 */
#define WINEVTAPI
#include <mime/win32/winevt.h>

#define RPC_S_SERVER_UNAVAILABLE 1722L
#define ERROR_INVALID_OPERATION 4317L
#define MAX_OBJECTS 4096

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

typedef enum { OBJ_FREE, OBJ_RESULTS, OBJ_CONTEXT, OBJ_SUBSCRIPTION, OBJ_BOOKMARK, OBJ_LOG, OBJ_SESSION } KIND;

typedef struct {
    KIND kind;
    BOOL remote;
} OBJECT;

static SRWLOCK g_lock = SRWLOCK_INIT;
static OBJECT g_objects[MAX_OBJECTS];

/* handles are small numbers, as windows' are: the slot plus one. */
static EVT_HANDLE create(KIND kind, BOOL remote)
{
    EVT_HANDLE handle = nullptr;
    AcquireSRWLockExclusive(&g_lock);
    for (UINT i = 0; i < MAX_OBJECTS; i++)
        if (g_objects[i].kind == OBJ_FREE) {
            g_objects[i].kind = kind;
            g_objects[i].remote = remote;
            handle = (EVT_HANDLE)(ULONG_PTR)(i + 1);
            break;
        }
    ReleaseSRWLockExclusive(&g_lock);
    if (!handle)
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return handle;
}

static OBJECT *lookup(EVT_HANDLE handle, KIND kind)
{
    ULONG_PTR slot = (ULONG_PTR)handle;
    if (!slot || slot > MAX_OBJECTS)
        return nullptr;
    OBJECT *object = &g_objects[slot - 1];
    if (object->kind == OBJ_FREE || (kind != OBJ_FREE && object->kind != kind))
        return nullptr;
    return object;
}

static BOOL fail(DWORD error)
{
    SetLastError(error);
    return FALSE;
}

static EVT_HANDLE fail_handle(DWORD error)
{
    SetLastError(error);
    return nullptr;
}

static BOOL known_channel(LPCWSTR path)
{
    static const LPCWSTR channels[] = {L"Application", L"System", L"Setup", L"Security", L"ForwardedEvents"};
    for (UINT i = 0; i < sizeof(channels) / sizeof(channels[0]); i++)
        if (!_wcsicmp(path, channels[i]))
            return TRUE;
    return FALSE;
}

/* the session a call names, null for the local one; a remote one has nothing to talk to. */
static DWORD check_session(EVT_HANDLE session)
{
    if (!session)
        return ERROR_SUCCESS;
    OBJECT *object = lookup(session, OBJ_SESSION);
    if (!object)
        return ERROR_INVALID_HANDLE;
    return object->remote ? RPC_S_SERVER_UNAVAILABLE : ERROR_SUCCESS;
}

static DWORD check_source(LPCWSTR path, BOOL file)
{
    if (file) {
        if (!path)
            return ERROR_INVALID_PARAMETER;
        return GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
    }
    if (path && !known_channel(path))
        return ERROR_EVT_CHANNEL_NOT_FOUND;
    return ERROR_SUCCESS;
}

EVT_HANDLE WINAPI EvtOpenSession(int login_class, PVOID login, DWORD timeout, DWORD flags)
{
    if (!login || flags || login_class != 1)
        return fail_handle(ERROR_INVALID_PARAMETER);
    return create(OBJ_SESSION, TRUE);
}

EVT_HANDLE WINAPI EvtQuery(EVT_HANDLE session, LPCWSTR path, LPCWSTR query, DWORD flags)
{
    if (!flags || (flags & ~0x1303u))
        return fail_handle(ERROR_INVALID_PARAMETER);
    if ((flags & 3) == 3 || (flags & 0x300) == 0x300)
        return fail_handle(ERROR_INVALID_PARAMETER);
    DWORD error = check_session(session);
    if (!error)
        error = check_source(path, (flags & EvtQueryFilePath) != 0);
    if (error)
        return fail_handle(error);
    EVT_HANDLE handle = create(OBJ_RESULTS, FALSE);
    if (handle)
        SetLastError(ERROR_SUCCESS);
    return handle;
}

/* an empty log has no next event. */
BOOL WINAPI EvtNext(EVT_HANDLE results, DWORD size, PEVT_HANDLE events, DWORD timeout, DWORD flags, PDWORD returned)
{
    if (!returned || (size && !events))
        return fail(ERROR_INVALID_PARAMETER);
    *returned = 0;
    if (!lookup(results, OBJ_RESULTS) && !lookup(results, OBJ_SUBSCRIPTION))
        return fail(ERROR_INVALID_HANDLE);
    return fail(ERROR_NO_MORE_ITEMS);
}

BOOL WINAPI EvtSeek(EVT_HANDLE results, LONGLONG position, EVT_HANDLE bookmark, DWORD timeout, DWORD flags)
{
    if (!lookup(results, OBJ_RESULTS))
        return fail(ERROR_INVALID_HANDLE);
    if (bookmark && !lookup(bookmark, OBJ_BOOKMARK))
        return fail(ERROR_INVALID_HANDLE);
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

BOOL WINAPI EvtClose(EVT_HANDLE handle)
{
    OBJECT *object = lookup(handle, OBJ_FREE);
    if (!object)
        return fail(ERROR_INVALID_HANDLE);
    AcquireSRWLockExclusive(&g_lock);
    object->kind = OBJ_FREE;
    ReleaseSRWLockExclusive(&g_lock);
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

BOOL WINAPI EvtCancel(EVT_HANDLE handle)
{
    if (handle && !lookup(handle, OBJ_FREE))
        return fail(ERROR_INVALID_HANDLE);
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

EVT_HANDLE WINAPI EvtCreateRenderContext(DWORD count, LPCWSTR *paths, DWORD flags)
{
    if (flags > EvtRenderContextUser)
        return fail_handle(ERROR_INVALID_PARAMETER);
    if (flags != EvtRenderContextValues ? paths || count : count && !paths)
        return fail_handle(ERROR_INVALID_PARAMETER);
    return create(OBJ_CONTEXT, FALSE);
}

/* xml and bookmarks render without a context, values only with one, and no event exists to render. */
BOOL WINAPI EvtRender(EVT_HANDLE context, EVT_HANDLE fragment, DWORD flags, DWORD size, PVOID buffer, PDWORD used,
                      PDWORD count)
{
    if (flags == EvtRenderEventValues) {
        if (!context || !lookup(context, OBJ_CONTEXT))
            return fail(context ? ERROR_INVALID_HANDLE : ERROR_INVALID_PARAMETER);
        return fail(ERROR_INVALID_HANDLE);
    }
    if (flags > EvtRenderBookmark || context)
        return fail(ERROR_INVALID_PARAMETER);
    if (flags == EvtRenderEventXml)
        return fail(ERROR_INVALID_HANDLE);
    if (!lookup(fragment, OBJ_BOOKMARK))
        return fail(ERROR_INVALID_HANDLE);
    static const WCHAR xml[] = L"<BookmarkList>\r\n</BookmarkList>";
    if (used)
        *used = sizeof(xml);
    if (count)
        *count = 0;
    if (size < sizeof(xml) || !buffer)
        return fail(ERROR_INSUFFICIENT_BUFFER);
    memcpy(buffer, xml, sizeof(xml));
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

EVT_HANDLE WINAPI EvtCreateBookmark(LPCWSTR xml)
{
    return create(OBJ_BOOKMARK, FALSE);
}

BOOL WINAPI EvtUpdateBookmark(EVT_HANDLE bookmark, EVT_HANDLE event)
{
    if (!lookup(bookmark, OBJ_BOOKMARK))
        return fail(ERROR_INVALID_HANDLE);
    return fail(ERROR_INVALID_HANDLE);
}

/* a subscription that waits for events which never come. */
EVT_HANDLE WINAPI EvtSubscribe(EVT_HANDLE session, HANDLE signal, LPCWSTR path, LPCWSTR query, EVT_HANDLE bookmark,
                               PVOID context, EVT_SUBSCRIBE_CALLBACK callback, DWORD flags)
{
    DWORD origin = flags & EvtSubscribeOriginMask;
    if (!origin || (flags & ~(EvtSubscribeOriginMask | EvtSubscribeTolerateQueryErrors | EvtSubscribeStrict)))
        return fail_handle(ERROR_INVALID_PARAMETER);
    if (!signal == !callback)
        return fail_handle(ERROR_INVALID_PARAMETER);
    if ((origin == EvtSubscribeStartAfterBookmark) != (bookmark != nullptr))
        return fail_handle(ERROR_INVALID_PARAMETER);
    if (bookmark && !lookup(bookmark, OBJ_BOOKMARK))
        return fail_handle(ERROR_INVALID_HANDLE);
    DWORD error = check_session(session);
    if (!error)
        error = check_source(path, FALSE);
    if (error)
        return fail_handle(error);
    return create(OBJ_SUBSCRIPTION, FALSE);
}

EVT_HANDLE WINAPI EvtOpenLog(EVT_HANDLE session, LPCWSTR path, DWORD flags)
{
    if (!path || (flags != EvtQueryChannelPath && flags != EvtQueryFilePath))
        return fail_handle(ERROR_INVALID_PARAMETER);
    DWORD error = check_session(session);
    if (!error)
        error = check_source(path, flags == EvtQueryFilePath);
    if (error)
        return fail_handle(error);
    return create(OBJ_LOG, FALSE);
}

typedef struct {
    union {
        ULONGLONG UInt64Val;
        BOOL BooleanVal;
        ULONG UInt32Val;
    };
    DWORD Count;
    DWORD Type;
} EVT_VARIANT;

/* the counts of an empty log. */
BOOL WINAPI EvtGetLogInfo(EVT_HANDLE log, int property, DWORD size, EVT_VARIANT *value, PDWORD used)
{
    if (!lookup(log, OBJ_LOG))
        return fail(ERROR_INVALID_HANDLE);
    if (!used || property < 0 || property > 7)
        return fail(ERROR_INVALID_PARAMETER);
    *used = sizeof(EVT_VARIANT);
    if (size < sizeof(EVT_VARIANT) || !value)
        return fail(ERROR_INSUFFICIENT_BUFFER);
    memset(value, 0, sizeof(*value));
    switch (property) {
    case 0: case 1: case 2:
        value->Type = 17; /* EvtVarTypeFileTime */
        break;
    case 4:
        value->Type = 8; /* EvtVarTypeUInt32 */
        break;
    case 7:
        value->Type = 13; /* EvtVarTypeBoolean */
        break;
    default:
        value->Type = 10; /* EvtVarTypeUInt64 */
        break;
    }
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

DWORD WINAPI EvtGetExtendedStatus(DWORD size, LPWSTR buffer, PDWORD used)
{
    if (!used)
        return ERROR_INVALID_PARAMETER;
    *used = 1;
    if (!buffer || !size)
        return ERROR_INSUFFICIENT_BUFFER;
    buffer[0] = 0;
    return ERROR_SUCCESS;
}
