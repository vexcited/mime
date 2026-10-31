/*
 * wevtapi with empty event logs. the classic channels exist but hold no
 * events, so queries and subscriptions on them succeed and never return one.
 * other channels are not found, and remote sessions have no server to reach.
 */
#include "wevtapi_private.h"

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

static SRWLOCK g_lock = SRWLOCK_INIT;
static OBJECT g_objects[MAX_OBJECTS];

/* handles are small numbers like on windows: the slot plus one. */
EVT_HANDLE EvtCreateObject(KIND kind, BOOL remote)
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

OBJECT *EvtLookupObject(EVT_HANDLE handle, KIND kind)
{
    ULONG_PTR slot = (ULONG_PTR)handle;
    if (!slot || slot > MAX_OBJECTS)
        return nullptr;
    OBJECT *object = &g_objects[slot - 1];
    if (object->kind == OBJ_FREE || (kind != OBJ_FREE && object->kind != kind))
        return nullptr;
    return object;
}

BOOL EvtFail(DWORD error)
{
    SetLastError(error);
    return FALSE;
}

EVT_HANDLE EvtFailHandle(DWORD error)
{
    SetLastError(error);
    return nullptr;
}

const LPCWSTR g_evt_channels[CHANNEL_COUNT] = {L"Application", L"System", L"Setup", L"Security", L"ForwardedEvents"};

int EvtFindChannel(LPCWSTR path)
{
    for (UINT i = 0; i < CHANNEL_COUNT; i++)
        if (!_wcsicmp(path, g_evt_channels[i]))
            return (int)i;
    return -1;
}

static BOOL known_channel(LPCWSTR path)
{
    return EvtFindChannel(path) >= 0;
}

/* the path of a query is kept for the questions about it. */
static void keep_path(EVT_HANDLE handle, LPCWSTR path, BOOL file)
{
    OBJECT *object = EvtLookupObject(handle, OBJ_FREE);
    if (!object || !path)
        return;
    SIZE_T size = (wcslen(path) + 1) * sizeof(WCHAR);
    object->path = HeapAlloc(GetProcessHeap(), 0, size);
    if (object->path)
        memcpy(object->path, path, size);
    object->file = file;
}

/* null is the local session; a remote one has nothing to talk to. */
DWORD EvtCheckSession(EVT_HANDLE session)
{
    if (!session)
        return ERROR_SUCCESS;
    OBJECT *object = EvtLookupObject(session, OBJ_SESSION);
    if (!object)
        return ERROR_INVALID_HANDLE;
    return object->remote ? RPC_S_SERVER_UNAVAILABLE : ERROR_SUCCESS;
}

DWORD EvtCheckSource(LPCWSTR path, BOOL file)
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
        return EvtFailHandle(ERROR_INVALID_PARAMETER);
    return EvtCreateObject(OBJ_SESSION, TRUE);
}

EVT_HANDLE WINAPI EvtQuery(EVT_HANDLE session, LPCWSTR path, LPCWSTR query, DWORD flags)
{
    if (!flags || (flags & ~0x1303u))
        return EvtFailHandle(ERROR_INVALID_PARAMETER);
    if ((flags & 3) == 3 || (flags & 0x300) == 0x300)
        return EvtFailHandle(ERROR_INVALID_PARAMETER);
    DWORD error = EvtCheckSession(session);
    if (!error)
        error = EvtCheckSource(path, (flags & EvtQueryFilePath) != 0);
    if (error)
        return EvtFailHandle(error);
    EVT_HANDLE handle = EvtCreateObject(OBJ_RESULTS, FALSE);
    if (handle) {
        keep_path(handle, path, (flags & EvtQueryFilePath) != 0);
        SetLastError(ERROR_SUCCESS);
    }
    return handle;
}

BOOL WINAPI EvtNext(EVT_HANDLE results, DWORD size, PEVT_HANDLE events, DWORD timeout, DWORD flags, PDWORD returned)
{
    if (!returned || (size && !events))
        return EvtFail(ERROR_INVALID_PARAMETER);
    *returned = 0;
    if (!EvtLookupObject(results, OBJ_RESULTS) && !EvtLookupObject(results, OBJ_SUBSCRIPTION))
        return EvtFail(ERROR_INVALID_HANDLE);
    return EvtFail(ERROR_NO_MORE_ITEMS);
}

BOOL WINAPI EvtSeek(EVT_HANDLE results, LONGLONG position, EVT_HANDLE bookmark, DWORD timeout, DWORD flags)
{
    if (!EvtLookupObject(results, OBJ_RESULTS))
        return EvtFail(ERROR_INVALID_HANDLE);
    if (bookmark && !EvtLookupObject(bookmark, OBJ_BOOKMARK))
        return EvtFail(ERROR_INVALID_HANDLE);
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

BOOL WINAPI EvtClose(EVT_HANDLE handle)
{
    OBJECT *object = EvtLookupObject(handle, OBJ_FREE);
    if (!object)
        return EvtFail(ERROR_INVALID_HANDLE);
    HeapFree(GetProcessHeap(), 0, object->path);
    HeapFree(GetProcessHeap(), 0, object->data);
    AcquireSRWLockExclusive(&g_lock);
    memset(object, 0, sizeof(*object));
    ReleaseSRWLockExclusive(&g_lock);
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

BOOL WINAPI EvtCancel(EVT_HANDLE handle)
{
    if (handle && !EvtLookupObject(handle, OBJ_FREE))
        return EvtFail(ERROR_INVALID_HANDLE);
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

EVT_HANDLE WINAPI EvtCreateRenderContext(DWORD count, LPCWSTR *paths, DWORD flags)
{
    if (flags > EvtRenderContextUser)
        return EvtFailHandle(ERROR_INVALID_PARAMETER);
    if (flags != EvtRenderContextValues ? paths || count : count && !paths)
        return EvtFailHandle(ERROR_INVALID_PARAMETER);
    return EvtCreateObject(OBJ_CONTEXT, FALSE);
}

/* xml and bookmarks render without a context, values only with one. no event ever exists to render. */
BOOL WINAPI EvtRender(EVT_HANDLE context, EVT_HANDLE fragment, DWORD flags, DWORD size, PVOID buffer, PDWORD used,
                      PDWORD count)
{
    if (flags == EvtRenderEventValues) {
        if (!context || !EvtLookupObject(context, OBJ_CONTEXT))
            return EvtFail(context ? ERROR_INVALID_HANDLE : ERROR_INVALID_PARAMETER);
        return EvtFail(ERROR_INVALID_HANDLE);
    }
    if (flags > EvtRenderBookmark || context)
        return EvtFail(ERROR_INVALID_PARAMETER);
    if (flags == EvtRenderEventXml)
        return EvtFail(ERROR_INVALID_HANDLE);
    if (!EvtLookupObject(fragment, OBJ_BOOKMARK))
        return EvtFail(ERROR_INVALID_HANDLE);
    static const WCHAR xml[] = L"<BookmarkList>\r\n</BookmarkList>";
    if (used)
        *used = sizeof(xml);
    if (count)
        *count = 0;
    if (size < sizeof(xml) || !buffer)
        return EvtFail(ERROR_INSUFFICIENT_BUFFER);
    memcpy(buffer, xml, sizeof(xml));
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

EVT_HANDLE WINAPI EvtCreateBookmark(LPCWSTR xml)
{
    return EvtCreateObject(OBJ_BOOKMARK, FALSE);
}

BOOL WINAPI EvtUpdateBookmark(EVT_HANDLE bookmark, EVT_HANDLE event)
{
    if (!EvtLookupObject(bookmark, OBJ_BOOKMARK))
        return EvtFail(ERROR_INVALID_HANDLE);
    return EvtFail(ERROR_INVALID_HANDLE);
}

/* the subscription waits for events that never come. */
EVT_HANDLE WINAPI EvtSubscribe(EVT_HANDLE session, HANDLE signal, LPCWSTR path, LPCWSTR query, EVT_HANDLE bookmark,
                               PVOID context, EVT_SUBSCRIBE_CALLBACK callback, DWORD flags)
{
    DWORD origin = flags & EvtSubscribeOriginMask;
    if (!origin || (flags & ~(EvtSubscribeOriginMask | EvtSubscribeTolerateQueryErrors | EvtSubscribeStrict)))
        return EvtFailHandle(ERROR_INVALID_PARAMETER);
    if (!signal == !callback)
        return EvtFailHandle(ERROR_INVALID_PARAMETER);
    if ((origin == EvtSubscribeStartAfterBookmark) != (bookmark != nullptr))
        return EvtFailHandle(ERROR_INVALID_PARAMETER);
    if (bookmark && !EvtLookupObject(bookmark, OBJ_BOOKMARK))
        return EvtFailHandle(ERROR_INVALID_HANDLE);
    DWORD error = EvtCheckSession(session);
    if (!error)
        error = EvtCheckSource(path, FALSE);
    if (error)
        return EvtFailHandle(error);
    EVT_HANDLE handle = EvtCreateObject(OBJ_SUBSCRIPTION, FALSE);
    if (handle)
        keep_path(handle, path, FALSE);
    return handle;
}

EVT_HANDLE WINAPI EvtOpenLog(EVT_HANDLE session, LPCWSTR path, DWORD flags)
{
    if (!path || (flags != EvtQueryChannelPath && flags != EvtQueryFilePath))
        return EvtFailHandle(ERROR_INVALID_PARAMETER);
    DWORD error = EvtCheckSession(session);
    if (!error)
        error = EvtCheckSource(path, flags == EvtQueryFilePath);
    if (error)
        return EvtFailHandle(error);
    return EvtCreateObject(OBJ_LOG, FALSE);
}


BOOL WINAPI EvtGetLogInfo(EVT_HANDLE log, int property, DWORD size, EVT_VARIANT *value, PDWORD used)
{
    if (!EvtLookupObject(log, OBJ_LOG))
        return EvtFail(ERROR_INVALID_HANDLE);
    if (!used || property < 0 || property > 7)
        return EvtFail(ERROR_INVALID_PARAMETER);
    *used = sizeof(EVT_VARIANT);
    if (size < sizeof(EVT_VARIANT) || !value)
        return EvtFail(ERROR_INSUFFICIENT_BUFFER);
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
