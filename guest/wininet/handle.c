/*
 * HINTERNET objects: sessions, connections and requests, their table, status callbacks and options.
 */
#include "wininet_private.h"

#define INET_MAX_SLOTS 16384

static SRWLOCK TableLock = SRWLOCK_INIT;
static INET_OBJECT **Slots;
static WORD *Generations;
static DWORD SlotCount;

/* process wide option defaults, copied into each new session. */
static DWORD DefaultTimeouts[INET_TIMEOUT_COUNT] = {60000, 5, 0, 30000, 30000, 30000, 30000};
static DWORD MaxConnsPerServer = 2;
static DWORD MaxConnsPer10Server = 4;

PWSTR InetDuplicate(LPCWSTR text)
{
    if (!text)
        return NULL;
    SIZE_T bytes = ((SIZE_T)lstrlenW(text) + 1) * sizeof(WCHAR);
    PWSTR copy = HeapAlloc(GetProcessHeap(), 0, bytes);
    if (copy)
        memcpy(copy, text, bytes);
    return copy;
}

void InetFree(void *memory)
{
    if (memory)
        HeapFree(GetProcessHeap(), 0, memory);
}

BOOL InetFail(DWORD error)
{
    SetLastError(error);
    return FALSE;
}

void InetRelease(INET_OBJECT *object)
{
    if (!object || InterlockedDecrement(&object->Refs))
        return;
    for (DWORD i = 0; i < object->HeaderCount; i++) {
        InetFree(object->Headers[i].Name);
        InetFree(object->Headers[i].Value);
    }
    InetFree(object->Headers);
    InetFree(object->Agent);
    InetFree(object->Server);
    InetFree(object->User);
    InetFree(object->Password);
    InetFree(object->Verb);
    InetFree(object->Object);
    InetFree(object->Version);
    InetFree(object->Referrer);
    InetRelease(object->Parent);
    InetFree(object);
}

static DWORD SlotOf(HINTERNET handle)
{
    ULONG_PTR value = (ULONG_PTR)handle;
    if (value & 3 || value >> 31)
        return UINT32_MAX;
    DWORD slot = (DWORD)((value & 0xFFFF) >> 2);
    if (!slot || slot > SlotCount || Generations[slot - 1] != (WORD)(value >> 16))
        return UINT32_MAX;
    return slot - 1;
}

INET_OBJECT *InetReference(HINTERNET handle)
{
    AcquireSRWLockShared(&TableLock);
    DWORD slot = SlotOf(handle);
    INET_OBJECT *object = slot == UINT32_MAX ? NULL : Slots[slot];
    if (object)
        InterlockedIncrement(&object->Refs);
    ReleaseSRWLockShared(&TableLock);
    if (!object)
        SetLastError(ERROR_INVALID_HANDLE);
    return object;
}

INET_OBJECT *InetReferenceType(HINTERNET handle, DWORD type)
{
    INET_OBJECT *object = InetReference(handle);
    if (object && object->Type != type) {
        InetRelease(object);
        SetLastError(ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
        return NULL;
    }
    return object;
}

/* a new object with one reference for the table and one for the caller, or NULL with the error set. */
static INET_OBJECT *CreateObject(DWORD type, INET_OBJECT *parent)
{
    INET_OBJECT *object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object));
    if (!object) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    object->Refs = 2;
    object->Type = type;
    if (parent) {
        InterlockedIncrement(&parent->Refs);
        object->Parent = parent;
        object->Flags = parent->Flags;
        object->Callback = parent->Callback;
        memcpy(object->Timeouts, parent->Timeouts, sizeof(object->Timeouts));
    } else {
        memcpy(object->Timeouts, DefaultTimeouts, sizeof(object->Timeouts));
    }
    AcquireSRWLockExclusive(&TableLock);
    DWORD slot = 0;
    while (slot < SlotCount && Slots[slot])
        slot++;
    if (slot == SlotCount && SlotCount < INET_MAX_SLOTS) {
        DWORD count = SlotCount ? SlotCount * 2 : 64;
        INET_OBJECT **slots = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * sizeof(*slots));
        WORD *generations = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * sizeof(*generations));
        if (slots && generations) {
            if (SlotCount) {
                memcpy(slots, Slots, SlotCount * sizeof(*slots));
                memcpy(generations, Generations, SlotCount * sizeof(*generations));
            }
            InetFree(Slots);
            InetFree(Generations);
            Slots = slots;
            Generations = generations;
            SlotCount = count;
        } else {
            InetFree(slots);
            InetFree(generations);
        }
    }
    if (slot == SlotCount) {
        ReleaseSRWLockExclusive(&TableLock);
        object->Refs = 1;
        InetRelease(object);
        SetLastError(ERROR_INTERNET_OUT_OF_HANDLES);
        return NULL;
    }
    WORD generation = (WORD)((Generations[slot] + 1) & 0x7FFF);
    Generations[slot] = generation ? generation : 1;
    Slots[slot] = object;
    object->Handle = (HINTERNET)(((ULONG_PTR)Generations[slot] << 16) | ((ULONG_PTR)(slot + 1) << 2));
    ReleaseSRWLockExclusive(&TableLock);
    return object;
}

/* windows only notifies handles that have both a callback and a nonzero context. */
static void Notify(INET_OBJECT *object, DWORD status, LPVOID information, DWORD length)
{
    if (object->Callback && object->Context)
        object->Callback(object->Handle, object->Context, status, information, length);
}

static void NotifyCreated(INET_OBJECT *object)
{
    INTERNET_ASYNC_RESULT result = {(DWORD_PTR)object->Handle, 0};
    Notify(object, INTERNET_STATUS_HANDLE_CREATED, &result, sizeof(result));
}

DECLSPEC_EXPORT HINTERNET WINAPI InternetOpenW(LPCWSTR agent, DWORD access_type, LPCWSTR proxy, LPCWSTR proxy_bypass,
                                              DWORD flags)
{
    if (access_type != INTERNET_OPEN_TYPE_PRECONFIG && access_type != INTERNET_OPEN_TYPE_DIRECT &&
        access_type != INTERNET_OPEN_TYPE_PROXY && access_type != INTERNET_OPEN_TYPE_PRECONFIG_WITH_NO_AUTOPROXY) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    INET_OBJECT *session = CreateObject(INTERNET_HANDLE_TYPE_INTERNET, NULL);
    if (!session)
        return NULL;
    session->AccessType = access_type;
    session->Flags = flags;
    if (agent && !(session->Agent = InetDuplicate(agent))) {
        InternetCloseHandle(session->Handle);
        InetRelease(session);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    HINTERNET handle = session->Handle;
    InetRelease(session);
    SetLastError(ERROR_SUCCESS);
    return handle;
}

DECLSPEC_EXPORT HINTERNET WINAPI InternetConnectW(HINTERNET session, LPCWSTR server, INTERNET_PORT port, LPCWSTR user,
                                                 LPCWSTR password, DWORD service, DWORD flags, DWORD_PTR context)
{
    INET_OBJECT *parent = InetReferenceType(session, INTERNET_HANDLE_TYPE_INTERNET);
    if (!parent)
        return NULL;
    DWORD type, error = ERROR_SUCCESS;
    if (!server || !*server)
        error = ERROR_INVALID_PARAMETER;
    else if (service == INTERNET_SERVICE_HTTP)
        type = INTERNET_HANDLE_TYPE_CONNECT_HTTP;
    /* ftp logs in during InternetConnect, which needs the network mime does not reach yet. */
    else if (service == INTERNET_SERVICE_FTP)
        error = ERROR_INTERNET_CANNOT_CONNECT;
    else if (service == INTERNET_SERVICE_GOPHER)
        error = ERROR_INTERNET_PROTOCOL_NOT_FOUND;
    else
        error = ERROR_INVALID_PARAMETER;
    if (error) {
        InetRelease(parent);
        SetLastError(error);
        return NULL;
    }
    INET_OBJECT *connect = CreateObject(type, parent);
    InetRelease(parent);
    if (!connect)
        return NULL;
    connect->Flags |= flags;
    connect->Context = context;
    connect->Service = service;
    connect->Port = port ? port : INTERNET_DEFAULT_HTTP_PORT;
    connect->Server = InetDuplicate(server);
    connect->User = InetDuplicate(user);
    connect->Password = InetDuplicate(password);
    if (!connect->Server || (user && !connect->User) || (password && !connect->Password)) {
        InternetCloseHandle(connect->Handle);
        InetRelease(connect);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    NotifyCreated(connect);
    HINTERNET handle = connect->Handle;
    InetRelease(connect);
    SetLastError(ERROR_SUCCESS);
    return handle;
}

DECLSPEC_EXPORT HINTERNET WINAPI HttpOpenRequestW(HINTERNET connect, LPCWSTR verb, LPCWSTR object, LPCWSTR version,
                                                 LPCWSTR referrer, LPCWSTR *accept_types, DWORD flags,
                                                 DWORD_PTR context)
{
    INET_OBJECT *parent = InetReferenceType(connect, INTERNET_HANDLE_TYPE_CONNECT_HTTP);
    if (!parent)
        return NULL;
    INET_OBJECT *request = CreateObject(INTERNET_HANDLE_TYPE_HTTP_REQUEST, parent);
    InetRelease(parent);
    if (!request)
        return NULL;
    request->Flags = flags;
    request->Context = context;
    request->Verb = InetDuplicate(verb && *verb ? verb : L"GET");
    request->Object = InetDuplicate(object && *object ? object : L"/");
    request->Version = InetDuplicate(version && *version ? version : L"HTTP/1.1");
    request->Referrer = InetDuplicate(referrer);
    if (!request->Verb || !request->Object || !request->Version || (referrer && !request->Referrer)) {
        InternetCloseHandle(request->Handle);
        InetRelease(request);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    NotifyCreated(request);
    HINTERNET handle = request->Handle;
    InetRelease(request);
    SetLastError(ERROR_SUCCESS);
    return handle;
}

static BOOL DescendsFrom(INET_OBJECT *object, INET_OBJECT *root)
{
    for (; object; object = object->Parent)
        if (object == root)
            return TRUE;
    return FALSE;
}

/* closing a handle closes the handles derived from it too, children first. */
DECLSPEC_EXPORT BOOL WINAPI InternetCloseHandle(HINTERNET handle)
{
    AcquireSRWLockExclusive(&TableLock);
    DWORD slot = SlotOf(handle);
    INET_OBJECT *root = slot == UINT32_MAX ? NULL : Slots[slot];
    if (!root) {
        ReleaseSRWLockExclusive(&TableLock);
        return InetFail(ERROR_INVALID_HANDLE);
    }
    DWORD count = 0;
    for (DWORD i = 0; i < SlotCount; i++)
        if (Slots[i] && DescendsFrom(Slots[i], root))
            count++;
    INET_OBJECT **closed = HeapAlloc(GetProcessHeap(), 0, count * sizeof(*closed));
    if (!closed) {
        ReleaseSRWLockExclusive(&TableLock);
        return InetFail(ERROR_NOT_ENOUGH_MEMORY);
    }
    count = 0;
    for (DWORD i = 0; i < SlotCount; i++)
        if (Slots[i] && DescendsFrom(Slots[i], root)) {
            closed[count++] = Slots[i];
            Slots[i] = NULL;
        }
    ReleaseSRWLockExclusive(&TableLock);
    /* the deepest handle is a request, two levels below its session. */
    for (int depth = 2; depth >= 0; depth--)
        for (DWORD i = 0; i < count; i++) {
            int level = 0;
            for (INET_OBJECT *p = closed[i]; p != root; p = p->Parent)
                level++;
            if (level != depth)
                continue;
            HINTERNET closing = closed[i]->Handle;
            Notify(closed[i], INTERNET_STATUS_HANDLE_CLOSING, &closing, sizeof(closing));
            InetRelease(closed[i]);
        }
    InetFree(closed);
    return TRUE;
}

DECLSPEC_EXPORT INTERNET_STATUS_CALLBACK WINAPI InternetSetStatusCallbackW(HINTERNET handle,
                                                                          INTERNET_STATUS_CALLBACK callback)
{
    INET_OBJECT *object = InetReference(handle);
    if (!object)
        return INTERNET_INVALID_STATUS_CALLBACK;
    INTERNET_STATUS_CALLBACK previous = (INTERNET_STATUS_CALLBACK)InterlockedExchangePointer(
        (PVOID volatile *)&object->Callback, (PVOID)callback);
    InetRelease(object);
    return previous;
}

/* copies a DWORD option out, or reports the size needed. */
static BOOL QueryDword(DWORD value, LPVOID buffer, LPDWORD length)
{
    if (*length < sizeof(DWORD)) {
        *length = sizeof(DWORD);
        return InetFail(ERROR_INSUFFICIENT_BUFFER);
    }
    if (!buffer)
        return InetFail(ERROR_INVALID_PARAMETER);
    *(DWORD *)buffer = value;
    *length = sizeof(DWORD);
    return TRUE;
}

/* copies a string option out; on success the length is in characters without the terminator. */
static BOOL QueryString(LPCWSTR text, LPVOID buffer, LPDWORD length)
{
    DWORD chars = text ? lstrlenW(text) : 0;
    DWORD bytes = (chars + 1) * sizeof(WCHAR);
    if (!buffer || *length < bytes) {
        *length = bytes;
        return InetFail(ERROR_INSUFFICIENT_BUFFER);
    }
    if (chars)
        memcpy(buffer, text, chars * sizeof(WCHAR));
    ((WCHAR *)buffer)[chars] = 0;
    *length = chars;
    return TRUE;
}

/* the url of a request, built from its connection and object. */
static PWSTR RequestUrl(INET_OBJECT *request)
{
    INET_OBJECT *connect = request->Parent;
    BOOL secure = (request->Flags & INTERNET_FLAG_SECURE) != 0;
    WCHAR port[8];
    port[0] = 0;
    if (connect->Port != (secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT)) {
        WCHAR digits[6];
        int n = 0;
        for (DWORD value = connect->Port; value || !n; value /= 10)
            digits[n++] = (WCHAR)(L'0' + value % 10);
        port[0] = L':';
        for (int i = 0; i < n; i++)
            port[1 + i] = digits[n - 1 - i];
        port[1 + n] = 0;
    }
    LPCWSTR scheme = secure ? L"https://" : L"http://";
    LPCWSTR parts[] = {scheme, connect->Server, port, request->Object[0] == L'/' ? L"" : L"/", request->Object};
    SIZE_T chars = 1;
    for (int i = 0; i < 5; i++)
        chars += lstrlenW(parts[i]);
    PWSTR url = HeapAlloc(GetProcessHeap(), 0, chars * sizeof(WCHAR));
    if (!url)
        return NULL;
    url[0] = 0;
    for (int i = 0; i < 5; i++)
        lstrcatW(url, parts[i]);
    return url;
}

DECLSPEC_EXPORT BOOL WINAPI InternetQueryOptionW(HINTERNET handle, DWORD option, LPVOID buffer, LPDWORD length)
{
    if (!length)
        return InetFail(ERROR_INVALID_PARAMETER);
    INET_OBJECT *object = NULL;
    if (handle && !(object = InetReference(handle)))
        return FALSE;
    BOOL ok;
    if (option >= INET_TIMEOUT_FIRST && option <= INET_TIMEOUT_LAST) {
        ok = QueryDword(object ? object->Timeouts[option - INET_TIMEOUT_FIRST]
                               : DefaultTimeouts[option - INET_TIMEOUT_FIRST],
                        buffer, length);
    } else if (option == INTERNET_OPTION_MAX_CONNS_PER_SERVER && !object) {
        ok = QueryDword(MaxConnsPerServer, buffer, length);
    } else if (option == INTERNET_OPTION_MAX_CONNS_PER_1_0_SERVER && !object) {
        ok = QueryDword(MaxConnsPer10Server, buffer, length);
    } else if (option == INTERNET_OPTION_CONNECTED_STATE && !object) {
        ok = QueryDword(INTERNET_STATE_CONNECTED, buffer, length);
    } else if (option == INTERNET_OPTION_VERSION && !object) {
        if (*length < sizeof(INTERNET_VERSION_INFO) || !buffer) {
            *length = sizeof(INTERNET_VERSION_INFO);
            ok = InetFail(ERROR_INSUFFICIENT_BUFFER);
        } else {
            INTERNET_VERSION_INFO version = {1, 2};
            *(INTERNET_VERSION_INFO *)buffer = version;
            *length = sizeof(version);
            ok = TRUE;
        }
    } else if (option == INTERNET_OPTION_HANDLE_TYPE || option == INTERNET_OPTION_CONTEXT_VALUE ||
               option == INTERNET_OPTION_USER_AGENT || option == INTERNET_OPTION_URL ||
               option == INTERNET_OPTION_REQUEST_FLAGS) {
        if (!object)
            ok = InetFail(ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
        else if (option == INTERNET_OPTION_HANDLE_TYPE)
            ok = QueryDword(object->Type, buffer, length);
        else if (option == INTERNET_OPTION_REQUEST_FLAGS)
            ok = object->Type == INTERNET_HANDLE_TYPE_HTTP_REQUEST ? QueryDword(0, buffer, length)
                                                                    : InetFail(ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
        else if (option == INTERNET_OPTION_CONTEXT_VALUE) {
            if (*length < sizeof(DWORD_PTR) || !buffer) {
                *length = sizeof(DWORD_PTR);
                ok = InetFail(ERROR_INSUFFICIENT_BUFFER);
            } else {
                *(DWORD_PTR *)buffer = object->Context;
                *length = sizeof(DWORD_PTR);
                ok = TRUE;
            }
        } else if (option == INTERNET_OPTION_USER_AGENT) {
            if (object->Type == INTERNET_HANDLE_TYPE_INTERNET) {
                AcquireSRWLockShared(&TableLock);
                ok = QueryString(object->Agent, buffer, length);
                ReleaseSRWLockShared(&TableLock);
            } else {
                ok = InetFail(ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
            }
        } else if (object->Type == INTERNET_HANDLE_TYPE_HTTP_REQUEST) {
            PWSTR url = RequestUrl(object);
            ok = url ? QueryString(url, buffer, length) : InetFail(ERROR_NOT_ENOUGH_MEMORY);
            InetFree(url);
        } else {
            ok = InetFail(ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
        }
    } else {
        ok = InetFail(ERROR_INTERNET_INVALID_OPTION);
    }
    InetRelease(object);
    return ok;
}

DECLSPEC_EXPORT BOOL WINAPI InternetSetOptionW(HINTERNET handle, DWORD option, LPVOID buffer, DWORD length)
{
    INET_OBJECT *object = NULL;
    if (handle && !(object = InetReference(handle)))
        return FALSE;
    BOOL ok = TRUE;
    BOOL is_dword = (option >= INET_TIMEOUT_FIRST && option <= INET_TIMEOUT_LAST) ||
                    option == INTERNET_OPTION_MAX_CONNS_PER_SERVER ||
                    option == INTERNET_OPTION_MAX_CONNS_PER_1_0_SERVER;
    if (is_dword && (!buffer || length != sizeof(DWORD))) {
        ok = InetFail(buffer ? ERROR_INTERNET_BAD_OPTION_LENGTH : ERROR_INVALID_PARAMETER);
    } else if (option >= INET_TIMEOUT_FIRST && option <= INET_TIMEOUT_LAST) {
        DWORD value = *(DWORD *)buffer;
        if (object)
            object->Timeouts[option - INET_TIMEOUT_FIRST] = value;
        else
            DefaultTimeouts[option - INET_TIMEOUT_FIRST] = value;
    } else if (option == INTERNET_OPTION_MAX_CONNS_PER_SERVER ||
               option == INTERNET_OPTION_MAX_CONNS_PER_1_0_SERVER) {
        DWORD value = *(DWORD *)buffer;
        if (object)
            ok = InetFail(ERROR_INTERNET_INVALID_OPERATION);
        else if (!value)
            ok = InetFail(ERROR_BAD_ARGUMENTS);
        else if (option == INTERNET_OPTION_MAX_CONNS_PER_SERVER)
            MaxConnsPerServer = value;
        else
            MaxConnsPer10Server = value;
    } else if (option == INTERNET_OPTION_SETTINGS_CHANGED || option == INTERNET_OPTION_REFRESH) {
        /* there are no cached proxy or registry settings to reload. */
    } else if (option == INTERNET_OPTION_HANDLE_TYPE || option == INTERNET_OPTION_VERSION ||
               option == INTERNET_OPTION_URL || option == INTERNET_OPTION_CONNECTED_STATE) {
        ok = InetFail(ERROR_INTERNET_OPTION_NOT_SETTABLE);
    } else if (option == INTERNET_OPTION_CONTEXT_VALUE) {
        if (!object)
            ok = InetFail(ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
        else if (!buffer || length != sizeof(DWORD_PTR))
            ok = InetFail(ERROR_INTERNET_BAD_OPTION_LENGTH);
        else
            object->Context = *(DWORD_PTR *)buffer;
    } else if (option == INTERNET_OPTION_USER_AGENT) {
        if (!object || object->Type != INTERNET_HANDLE_TYPE_INTERNET) {
            ok = InetFail(ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
        } else {
            PWSTR agent = InetDuplicate(buffer && length ? buffer : L"");
            if (!agent) {
                ok = InetFail(ERROR_NOT_ENOUGH_MEMORY);
            } else {
                AcquireSRWLockExclusive(&TableLock);
                PWSTR previous = object->Agent;
                object->Agent = agent;
                ReleaseSRWLockExclusive(&TableLock);
                InetFree(previous);
            }
        }
    } else {
        ok = InetFail(ERROR_INTERNET_INVALID_OPTION);
    }
    InetRelease(object);
    return ok;
}
