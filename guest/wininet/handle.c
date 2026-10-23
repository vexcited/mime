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

void *InetAlloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
}

void *InetAllocZero(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

void *InetRealloc(void *memory, SIZE_T size)
{
    if (!memory)
        return InetAlloc(size);
    return HeapReAlloc(GetProcessHeap(), 0, memory, size ? size : 1);
}

void InetFree(void *memory)
{
    if (memory)
        HeapFree(GetProcessHeap(), 0, memory);
}

PWSTR InetDuplicateN(LPCWSTR text, DWORD length)
{
    if (!text)
        return nullptr;
    PWSTR copy = InetAlloc(((SIZE_T)length + 1) * sizeof(WCHAR));
    if (copy) {
        memcpy(copy, text, (SIZE_T)length * sizeof(WCHAR));
        copy[length] = 0;
    }
    return copy;
}

PWSTR InetDuplicate(LPCWSTR text)
{
    return text ? InetDuplicateN(text, (DWORD)lstrlenW(text)) : nullptr;
}

char *InetDuplicateA(const char *text, DWORD length)
{
    if (!text)
        return nullptr;
    if (length == (DWORD)-1)
        length = (DWORD)strlen(text);
    char *copy = InetAlloc((SIZE_T)length + 1);
    if (copy) {
        memcpy(copy, text, length);
        copy[length] = 0;
    }
    return copy;
}

char *InetNarrow(LPCWSTR text, int length)
{
    if (!text)
        return nullptr;
    if (length < 0)
        length = lstrlenW(text);
    int bytes = WideCharToMultiByte(CP_ACP, 0, text, length, nullptr, 0, nullptr, nullptr);
    char *narrow = InetAlloc((SIZE_T)bytes + 1);
    if (narrow) {
        WideCharToMultiByte(CP_ACP, 0, text, length, narrow, bytes, nullptr, nullptr);
        narrow[bytes] = 0;
    }
    return narrow;
}

PWSTR InetWiden(const char *text, int length)
{
    if (!text)
        return nullptr;
    if (length < 0)
        length = (int)strlen(text);
    int chars = MultiByteToWideChar(CP_ACP, 0, text, length, nullptr, 0);
    PWSTR wide = InetAlloc(((SIZE_T)chars + 1) * sizeof(WCHAR));
    if (wide) {
        MultiByteToWideChar(CP_ACP, 0, text, length, wide, chars);
        wide[chars] = 0;
    }
    return wide;
}

BOOL InetFail(DWORD error)
{
    SetLastError(error);
    return FALSE;
}

void InetAddRef(INET_OBJECT *object)
{
    InterlockedIncrement(&object->Refs);
}

void InetRelease(INET_OBJECT *object)
{
    if (!object || InterlockedDecrement(&object->Refs))
        return;
    if (object->Type == INTERNET_HANDLE_TYPE_HTTP_REQUEST)
        InetRequestCleanup(object);
    InetHeadersClear(&object->Request);
    InetFree(object->Agent);
    InetFree(object->Proxy);
    InetFree(object->ProxyBypass);
    InetFree(object->Server);
    InetFree(object->User);
    InetFree(object->Password);
    InetFree(object->ProxyUser);
    InetFree(object->ProxyPassword);
    InetFree(object->Verb);
    InetFree(object->Object);
    InetFree(object->Version);
    InetFree(object->Referrer);
    InetFree(object->Host);
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
    INET_OBJECT *object = slot == UINT32_MAX ? nullptr : Slots[slot];
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
        return nullptr;
    }
    return object;
}

INET_OBJECT *InetSession(INET_OBJECT *object)
{
    while (object && object->Type != INTERNET_HANDLE_TYPE_INTERNET)
        object = object->Parent;
    return object;
}

/* allocate with table and caller references, or return NULL with an error. */
static INET_OBJECT *CreateObject(DWORD type, INET_OBJECT *parent)
{
    INET_OBJECT *object = InetAllocZero(sizeof(*object));
    if (!object) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    object->Refs = 2;
    object->Type = type;
    object->ReadBufferSize = 4096;
    object->WriteBufferSize = 4096;
    if (parent) {
        InterlockedIncrement(&parent->Refs);
        object->Parent = parent;
        object->Flags = parent->Flags;
        object->Callback = parent->Callback;
        object->WideCallback = parent->WideCallback;
        object->Decoding = parent->Decoding;
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
        INET_OBJECT **slots = InetAllocZero(count * sizeof(*slots));
        WORD *generations = InetAllocZero(count * sizeof(*generations));
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
        return nullptr;
    }
    WORD generation = (WORD)((Generations[slot] + 1) & 0x7FFF);
    Generations[slot] = generation ? generation : 1;
    Slots[slot] = object;
    object->Handle = (HINTERNET)(((ULONG_PTR)Generations[slot] << 16) | ((ULONG_PTR)(slot + 1) << 2));
    ReleaseSRWLockExclusive(&TableLock);
    return object;
}

void InetNotify(INET_OBJECT *object, DWORD status, LPVOID information, DWORD length)
{
    INTERNET_STATUS_CALLBACK callback = object->Callback;
    if (callback && callback != INTERNET_INVALID_STATUS_CALLBACK && object->Context)
        callback(object->Handle, object->Context, status, information, length);
}

/*
 * strings use the ANSI code page. InternetSetStatusCallbackW widens resolved names and redirect URLs only; addresses
 * and other strings stay ANSI.
 */
void InetNotifyString(INET_OBJECT *object, DWORD status, LPCWSTR text)
{
    if (!object->Callback || !object->Context)
        return;
    BOOL wide = object->WideCallback &&
                (status == INTERNET_STATUS_RESOLVING_NAME || status == INTERNET_STATUS_REDIRECT);
    if (wide) {
        InetNotify(object, status, (LPVOID)text, (DWORD)lstrlenW(text) + 1);
        return;
    }
    char *narrow = InetNarrow(text, -1);
    if (narrow)
        InetNotify(object, status, narrow, (DWORD)strlen(narrow) + 1);
    InetFree(narrow);
}

BOOL InetIsAsync(INET_OBJECT *object, DWORD_PTR context)
{
    INET_OBJECT *session = InetSession(object);
    return session && session->Flags & INTERNET_FLAG_ASYNC && object->Callback &&
           object->Callback != INTERNET_INVALID_STATUS_CALLBACK && context;
}

static void NotifyCreated(INET_OBJECT *object)
{
    INTERNET_ASYNC_RESULT result = {(DWORD_PTR)object->Handle, 0};
    InetNotify(object, INTERNET_STATUS_HANDLE_CREATED, &result, sizeof(result));
}

DECLSPEC_EXPORT HINTERNET WINAPI InternetOpenW(LPCWSTR agent, DWORD access_type, LPCWSTR proxy, LPCWSTR proxy_bypass,
                                              DWORD flags)
{
    if (access_type != INTERNET_OPEN_TYPE_PRECONFIG && access_type != INTERNET_OPEN_TYPE_DIRECT &&
        access_type != INTERNET_OPEN_TYPE_PROXY && access_type != INTERNET_OPEN_TYPE_PRECONFIG_WITH_NO_AUTOPROXY) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    if (access_type == INTERNET_OPEN_TYPE_PROXY && (!proxy || !*proxy)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    InetStartup();
    INET_OBJECT *session = CreateObject(INTERNET_HANDLE_TYPE_INTERNET, nullptr);
    if (!session)
        return nullptr;
    session->AccessType = access_type;
    session->Flags = flags;
    BOOL failed = agent && !(session->Agent = InetDuplicate(agent));
    if (access_type == INTERNET_OPEN_TYPE_PROXY) {
        failed |= !(session->Proxy = InetDuplicate(proxy));
        failed |= proxy_bypass && !(session->ProxyBypass = InetDuplicate(proxy_bypass));
    }
    if (failed) {
        InternetCloseHandle(session->Handle);
        InetRelease(session);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    HINTERNET handle = session->Handle;
    InetRelease(session);
    SetLastError(ERROR_SUCCESS);
    return handle;
}

HINTERNET InetConnect(INET_OBJECT *parent, LPCWSTR server, INTERNET_PORT port, LPCWSTR user, LPCWSTR password,
                      DWORD service, DWORD flags, DWORD_PTR context)
{
    DWORD type, error = ERROR_SUCCESS;
    if (!server || !*server)
        error = ERROR_INVALID_PARAMETER;
    else if (service == INTERNET_SERVICE_HTTP)
        type = INTERNET_HANDLE_TYPE_CONNECT_HTTP;
    /* FTP login would happen during InternetConnect; no FTP client is implemented. */
    else if (service == INTERNET_SERVICE_FTP)
        error = ERROR_INTERNET_CANNOT_CONNECT;
    else if (service == INTERNET_SERVICE_GOPHER)
        error = ERROR_INTERNET_PROTOCOL_NOT_FOUND;
    else
        error = ERROR_INVALID_PARAMETER;
    if (error) {
        SetLastError(error);
        return nullptr;
    }
    INET_OBJECT *connect = CreateObject(type, parent);
    if (!connect)
        return nullptr;
    connect->Flags |= flags;
    connect->Context = context;
    connect->Service = service;
    connect->Port = port;
    connect->Server = InetDuplicate(server);
    connect->User = InetDuplicate(user);
    connect->Password = InetDuplicate(password);
    if (!connect->Server || (user && !connect->User) || (password && !connect->Password)) {
        InternetCloseHandle(connect->Handle);
        InetRelease(connect);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    NotifyCreated(connect);
    HINTERNET handle = connect->Handle;
    InetRelease(connect);
    SetLastError(ERROR_SUCCESS);
    return handle;
}

DECLSPEC_EXPORT HINTERNET WINAPI InternetConnectW(HINTERNET session, LPCWSTR server, INTERNET_PORT port, LPCWSTR user,
                                                 LPCWSTR password, DWORD service, DWORD flags, DWORD_PTR context)
{
    INET_OBJECT *parent = InetReferenceType(session, INTERNET_HANDLE_TYPE_INTERNET);
    if (!parent)
        return nullptr;
    HINTERNET handle = InetConnect(parent, server, port, user, password, service, flags, context);
    InetRelease(parent);
    return handle;
}

HINTERNET InetOpenRequest(INET_OBJECT *parent, LPCWSTR verb, LPCWSTR object, LPCWSTR version, LPCWSTR referrer,
                          LPCWSTR *accept_types, DWORD flags, DWORD_PTR context)
{
    INET_OBJECT *request = CreateObject(INTERNET_HANDLE_TYPE_HTTP_REQUEST, parent);
    if (!request)
        return nullptr;
    request->Flags = flags | (InetSession(parent)->Flags & INTERNET_FLAG_ASYNC);
    request->Context = context;
    request->Secure = (flags & INTERNET_FLAG_SECURE) != 0;
    request->HostPort = parent->Port ? parent->Port
                                     : request->Secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    request->Verb = InetDuplicate(verb && *verb ? verb : L"GET");
    request->Object = InetDuplicate(object && *object ? object : L"/");
    request->Version = InetDuplicate(version && *version ? version : L"HTTP/1.1");
    request->Referrer = InetDuplicate(referrer);
    request->Host = InetDuplicate(parent->Server);
    request->User = InetDuplicate(parent->User);
    request->Password = InetDuplicate(parent->Password);
    BOOL failed = !request->Verb || !request->Object || !request->Version || !request->Host ||
                  (referrer && !request->Referrer);
    /* the accept types and the referrer become request headers. */
    if (!failed && accept_types) {
        SIZE_T chars = 1;
        for (DWORD i = 0; accept_types[i]; i++)
            chars += lstrlenW(accept_types[i]) + 2;
        PWSTR joined = InetAlloc(chars * sizeof(WCHAR));
        failed = !joined;
        if (joined) {
            joined[0] = 0;
            for (DWORD i = 0; accept_types[i]; i++) {
                if (!*accept_types[i])
                    continue;
                if (joined[0])
                    lstrcatW(joined, L", ");
                lstrcatW(joined, accept_types[i]);
            }
            char *value = joined[0] ? InetNarrow(joined, -1) : nullptr;
            if (joined[0])
                failed = !value || !InetHeadersAppend(&request->Request, "Accept", 6, value, (DWORD)strlen(value));
            InetFree(value);
            InetFree(joined);
        }
    }
    if (!failed && referrer && *referrer) {
        char *value = InetNarrow(referrer, -1);
        failed |= !value || !InetHeadersAppend(&request->Request, "Referer", 7, value, (DWORD)strlen(value));
        InetFree(value);
    }
    if (failed) {
        InternetCloseHandle(request->Handle);
        InetRelease(request);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    NotifyCreated(request);
    HINTERNET handle = request->Handle;
    InetRelease(request);
    SetLastError(ERROR_SUCCESS);
    return handle;
}

DECLSPEC_EXPORT HINTERNET WINAPI HttpOpenRequestW(HINTERNET connect, LPCWSTR verb, LPCWSTR object, LPCWSTR version,
                                                 LPCWSTR referrer, LPCWSTR *accept_types, DWORD flags,
                                                 DWORD_PTR context)
{
    INET_OBJECT *parent = InetReferenceType(connect, INTERNET_HANDLE_TYPE_CONNECT_HTTP);
    if (!parent)
        return nullptr;
    HINTERNET handle = InetOpenRequest(parent, verb, object, version, referrer, accept_types, flags, context);
    InetRelease(parent);
    return handle;
}

static BOOL DescendsFrom(INET_OBJECT *object, INET_OBJECT *root)
{
    for (; object; object = object->Parent)
        if (object == root)
            return TRUE;
    return FALSE;
}

void InetDetach(INET_OBJECT *object)
{
    AcquireSRWLockExclusive(&TableLock);
    DWORD slot = SlotOf(object->Handle);
    BOOL owned = slot != UINT32_MAX && Slots[slot] == object;
    if (owned)
        Slots[slot] = nullptr;
    object->Hidden = TRUE;
    ReleaseSRWLockExclusive(&TableLock);
    if (owned)
        InetRelease(object);
}

/* closing a handle closes the handles derived from it too, children first. */
DECLSPEC_EXPORT BOOL WINAPI InternetCloseHandle(HINTERNET handle)
{
    AcquireSRWLockExclusive(&TableLock);
    DWORD slot = SlotOf(handle);
    INET_OBJECT *root = slot == UINT32_MAX ? nullptr : Slots[slot];
    if (!root) {
        ReleaseSRWLockExclusive(&TableLock);
        return InetFail(ERROR_INVALID_HANDLE);
    }
    DWORD count = 0;
    for (DWORD i = 0; i < SlotCount; i++)
        if (Slots[i] && DescendsFrom(Slots[i], root))
            count++;
    INET_OBJECT **closed = InetAlloc(count * sizeof(*closed));
    if (!closed) {
        ReleaseSRWLockExclusive(&TableLock);
        return InetFail(ERROR_NOT_ENOUGH_MEMORY);
    }
    count = 0;
    for (DWORD i = 0; i < SlotCount; i++)
        if (Slots[i] && DescendsFrom(Slots[i], root)) {
            closed[count++] = Slots[i];
            Slots[i] = nullptr;
        }
    ReleaseSRWLockExclusive(&TableLock);
    /* the worker sends closing notification after its operation notification. */
    BOOL *deferred = InetAllocZero(count * sizeof(*deferred));
    for (DWORD i = 0; i < count; i++)
        if (closed[i]->Type == INTERNET_HANDLE_TYPE_HTTP_REQUEST && InetRequestClosing(closed[i]) && deferred)
            deferred[i] = TRUE;
    /* the deepest handle is a request, two levels below its session. */
    for (int depth = 2; depth >= 0; depth--)
        for (DWORD i = 0; i < count; i++) {
            int level = 0;
            for (INET_OBJECT *p = closed[i]; p != root; p = p->Parent)
                level++;
            if (level != depth)
                continue;
            HINTERNET closing = closed[i]->Handle;
            if (!deferred || !deferred[i])
                InetNotify(closed[i], INTERNET_STATUS_HANDLE_CLOSING, &closing, sizeof(closing));
            InetRelease(closed[i]);
        }
    InetFree(deferred);
    InetFree(closed);
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

static INTERNET_STATUS_CALLBACK SetCallback(HINTERNET handle, INTERNET_STATUS_CALLBACK callback, BOOL wide)
{
    INET_OBJECT *object = InetReference(handle);
    if (!object)
        return INTERNET_INVALID_STATUS_CALLBACK;
    INTERNET_STATUS_CALLBACK previous = (INTERNET_STATUS_CALLBACK)InterlockedExchangePointer(
        (PVOID volatile *)&object->Callback, (PVOID)callback);
    object->WideCallback = wide;
    InetRelease(object);
    return previous;
}

DECLSPEC_EXPORT INTERNET_STATUS_CALLBACK WINAPI InternetSetStatusCallbackW(HINTERNET handle,
                                                                          INTERNET_STATUS_CALLBACK callback)
{
    return SetCallback(handle, callback, TRUE);
}

DECLSPEC_EXPORT INTERNET_STATUS_CALLBACK WINAPI InternetSetStatusCallbackA(HINTERNET handle,
                                                                          INTERNET_STATUS_CALLBACK callback)
{
    return SetCallback(handle, callback, FALSE);
}

DECLSPEC_EXPORT INTERNET_STATUS_CALLBACK WINAPI InternetSetStatusCallback(HINTERNET handle,
                                                                         INTERNET_STATUS_CALLBACK callback)
{
    return SetCallback(handle, callback, FALSE);
}

/* return a DWORD option or the required size. */
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

static BOOL QueryPointer(ULONG_PTR value, LPVOID buffer, LPDWORD length)
{
    if (*length < sizeof(ULONG_PTR) || !buffer) {
        *length = sizeof(ULONG_PTR);
        return InetFail(ERROR_INSUFFICIENT_BUFFER);
    }
    *(ULONG_PTR *)buffer = value;
    *length = sizeof(ULONG_PTR);
    return TRUE;
}

/* return string length without the terminator on success. */
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

PWSTR InetRequestUrl(INET_OBJECT *request)
{
    AcquireSRWLockShared(&request->Lock);
    BOOL secure = request->Secure;
    WCHAR port[8];
    port[0] = 0;
    if (request->HostPort != (secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT))
        wsprintfW(port, L":%u", request->HostPort);
    LPCWSTR scheme = secure ? L"https://" : L"http://";
    LPCWSTR object = request->Object;
    /* full-URL objects, such as proxies, are standalone. */
    BOOL absolute = !_wcsnicmp(object, L"http://", 7) || !_wcsnicmp(object, L"https://", 8);
    LPCWSTR parts[] = {scheme, request->Host, port, object[0] == L'/' ? L"" : L"/", object};
    SIZE_T chars = 1;
    for (int i = 0; i < 5; i++)
        chars += lstrlenW(parts[i]);
    PWSTR url = InetAlloc(chars * sizeof(WCHAR));
    if (url) {
        url[0] = 0;
        if (absolute)
            lstrcpyW(url, object);
        else
            for (int i = 0; i < 5; i++)
                lstrcatW(url, parts[i]);
    }
    ReleaseSRWLockShared(&request->Lock);
    return url;
}

static BOOL IsRequest(INET_OBJECT *object)
{
    return object && object->Type == INTERNET_HANDLE_TYPE_HTTP_REQUEST;
}

/* both A and W functions return INTERNET_PROXY_INFO followed by ANSI strings. */
static BOOL QueryProxy(INET_OBJECT *object, LPVOID buffer, LPDWORD length)
{
    DWORD type = INTERNET_OPEN_TYPE_DIRECT;
    char *proxy = nullptr, *bypass = nullptr;
    if (object && object->AccessType == INTERNET_OPEN_TYPE_PROXY) {
        type = INTERNET_OPEN_TYPE_PROXY;
        proxy = InetNarrow(object->Proxy, -1);
        bypass = InetNarrow(object->ProxyBypass, -1);
    } else if (!object || object->AccessType != INTERNET_OPEN_TYPE_DIRECT) {
        PWSTR list, skip;
        if (InetReadIEProxy(&list, &skip)) {
            type = INTERNET_OPEN_TYPE_PROXY;
            proxy = InetNarrow(list, -1);
            bypass = InetNarrow(skip, -1);
            InetFree(list);
            InetFree(skip);
        }
    }
    DWORD proxy_bytes = proxy ? (DWORD)strlen(proxy) + 1 : 0, bypass_bytes = bypass ? (DWORD)strlen(bypass) + 1 : 0;
    DWORD needed = sizeof(INTERNET_PROXY_INFO) + proxy_bytes + bypass_bytes;
    BOOL ok;
    if (!buffer || *length < needed) {
        *length = needed;
        ok = InetFail(ERROR_INSUFFICIENT_BUFFER);
    } else {
        INTERNET_PROXY_INFO *info = buffer;
        char *strings = (char *)(info + 1);
        info->dwAccessType = type;
        info->lpszProxy = nullptr;
        info->lpszProxyBypass = nullptr;
        if (proxy) {
            memcpy(strings, proxy, proxy_bytes);
            info->lpszProxy = (LPCWSTR)strings;
            strings += proxy_bytes;
        }
        if (bypass) {
            memcpy(strings, bypass, bypass_bytes);
            info->lpszProxyBypass = (LPCWSTR)strings;
        }
        *length = needed;
        ok = TRUE;
    }
    InetFree(proxy);
    InetFree(bypass);
    return ok;
}

/* both A and W functions return INTERNET_CERTIFICATE_INFO with ANSI strings. */
static BOOL QueryCertificate(INET_OBJECT *request, LPVOID buffer, LPDWORD length)
{
    if (!request->Certificate)
        return InetFail(ERROR_INTERNET_INVALID_OPERATION);
    if (!buffer || *length < sizeof(INTERNET_CERTIFICATE_INFOA)) {
        *length = sizeof(INTERNET_CERTIFICATE_INFOA);
        return InetFail(ERROR_INSUFFICIENT_BUFFER);
    }
    INTERNET_CERTIFICATE_INFOW wide;
    if (!InetCertificateInfo(request->Certificate, request->CipherStrength, &wide))
        return InetFail(ERROR_NOT_ENOUGH_MEMORY);
    INTERNET_CERTIFICATE_INFOA *info = buffer;
    memset(info, 0, sizeof(*info));
    info->ftExpiry = wide.ftExpiry;
    info->ftStart = wide.ftStart;
    info->dwKeySize = wide.dwKeySize;
    LPWSTR sources[] = {wide.lpszSubjectInfo, wide.lpszIssuerInfo, wide.lpszProtocolName, wide.lpszSignatureAlgName,
                        wide.lpszEncryptionAlgName};
    LPSTR *targets[] = {&info->lpszSubjectInfo, &info->lpszIssuerInfo, &info->lpszProtocolName,
                        &info->lpszSignatureAlgName, &info->lpszEncryptionAlgName};
    for (int i = 0; i < 5; i++) {
        if (!sources[i])
            continue;
        int bytes = WideCharToMultiByte(CP_ACP, 0, sources[i], -1, nullptr, 0, nullptr, nullptr);
        *targets[i] = LocalAlloc(LMEM_FIXED, (SIZE_T)bytes);
        if (*targets[i])
            WideCharToMultiByte(CP_ACP, 0, sources[i], -1, *targets[i], bytes, nullptr, nullptr);
        LocalFree(sources[i]);
    }
    *length = sizeof(INTERNET_CERTIFICATE_INFOA);
    return TRUE;
}

/* INTERNET_OPTION_SECURITY_CERTIFICATE: the certificate described in text. */
static BOOL QueryCertificateText(INET_OBJECT *request, LPVOID buffer, LPDWORD length)
{
    if (!request->Certificate)
        return InetFail(ERROR_INTERNET_INVALID_OPERATION);
    INTERNET_CERTIFICATE_INFOW info;
    if (!InetCertificateInfo(request->Certificate, request->CipherStrength, &info))
        return InetFail(ERROR_NOT_ENOUGH_MEMORY);
    SYSTEMTIME start, expiry;
    FileTimeToSystemTime(&info.ftStart, &start);
    FileTimeToSystemTime(&info.ftExpiry, &expiry);
    SIZE_T chars = 200;
    LPWSTR parts[] = {info.lpszSubjectInfo, info.lpszIssuerInfo, info.lpszProtocolName, info.lpszSignatureAlgName,
                      info.lpszEncryptionAlgName};
    for (int i = 0; i < 5; i++)
        chars += parts[i] ? lstrlenW(parts[i]) : 0;
    PWSTR text = InetAlloc(chars * sizeof(WCHAR));
    BOOL ok;
    if (!text) {
        ok = InetFail(ERROR_NOT_ENOUGH_MEMORY);
    } else {
        wsprintfW(text,
                  L"Subject:\r\n%s\r\nIssuer:\r\n%s\r\nEffective Date:\t%u/%u/%u %u:%02u:%02u\r\n"
                  L"Expiration Date:\t%u/%u/%u %u:%02u:%02u\r\nSecurity Protocol:\t%s\r\nSignature Type:\t%s\r\n"
                  L"Encryption Type:\t%s\r\nPrivacy Strength:\t%s (%u bits)",
                  parts[0] ? parts[0] : L"", parts[1] ? parts[1] : L"", start.wMonth, start.wDay, start.wYear,
                  start.wHour, start.wMinute, start.wSecond, expiry.wMonth, expiry.wDay, expiry.wYear, expiry.wHour,
                  expiry.wMinute, expiry.wSecond, parts[2] ? parts[2] : L"", parts[3] ? parts[3] : L"",
                  parts[4] ? parts[4] : L"", info.dwKeySize >= 128 ? L"High" : L"Low", info.dwKeySize);
        ok = QueryString(text, buffer, length);
        InetFree(text);
    }
    for (int i = 0; i < 5; i++)
        LocalFree(parts[i]);
    return ok;
}

static DWORD SecurityFlags(INET_OBJECT *request)
{
    DWORD flags = request->SecurityFlags | request->CertErrors;
    if (request->Secure)
        flags |= SECURITY_FLAG_SECURE;
    if (request->Certificate)
        flags |= request->CipherStrength >= 128  ? SECURITY_FLAG_STRENGTH_STRONG
                 : request->CipherStrength >= 56 ? SECURITY_FLAG_STRENGTH_MEDIUM
                                                 : SECURITY_FLAG_STRENGTH_WEAK;
    return flags;
}

DECLSPEC_EXPORT BOOL WINAPI InternetQueryOptionW(HINTERNET handle, DWORD option, LPVOID buffer, LPDWORD length)
{
    if (!length)
        return InetFail(ERROR_INVALID_PARAMETER);
    INET_OBJECT *object = nullptr;
    if (handle && !(object = InetReference(handle)))
        return FALSE;
    BOOL ok;
    if (option >= INET_TIMEOUT_FIRST && option <= INET_TIMEOUT_LAST) {
        ok = QueryDword(object ? INET_TIMEOUT(object, option) : DefaultTimeouts[option - INET_TIMEOUT_FIRST], buffer,
                        length);
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
    } else if (option == INTERNET_OPTION_HTTP_VERSION) {
        if (*length < sizeof(HTTP_VERSION_INFO) || !buffer) {
            *length = sizeof(HTTP_VERSION_INFO);
            ok = InetFail(ERROR_INSUFFICIENT_BUFFER);
        } else {
            HTTP_VERSION_INFO version = {1, 1};
            *(HTTP_VERSION_INFO *)buffer = version;
            *length = sizeof(version);
            ok = TRUE;
        }
    } else if (option == INTERNET_OPTION_PROXY) {
        ok = !object || object->Type == INTERNET_HANDLE_TYPE_INTERNET
                 ? QueryProxy(object, buffer, length)
                 : InetFail(ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
    } else if (!object) {
        ok = InetFail(option == INTERNET_OPTION_HANDLE_TYPE || option == INTERNET_OPTION_CONTEXT_VALUE ||
                              option == INTERNET_OPTION_URL || option == INTERNET_OPTION_USER_AGENT
                          ? ERROR_INTERNET_INCORRECT_HANDLE_TYPE
                          : ERROR_INTERNET_INVALID_OPTION);
    } else if (option == INTERNET_OPTION_HANDLE_TYPE) {
        ok = QueryDword(object->Type, buffer, length);
    } else if (option == INTERNET_OPTION_CONTEXT_VALUE) {
        ok = QueryPointer(object->Context, buffer, length);
    } else if (option == INTERNET_OPTION_PARENT_HANDLE) {
        INET_OBJECT *parent = object->Parent;
        while (parent && parent->Hidden)
            parent = parent->Parent;
        ok = QueryPointer((ULONG_PTR)(parent ? parent->Handle : nullptr), buffer, length);
    } else if (option == INTERNET_OPTION_READ_BUFFER_SIZE || option == INTERNET_OPTION_WRITE_BUFFER_SIZE) {
        ok = QueryDword(option == INTERNET_OPTION_READ_BUFFER_SIZE ? object->ReadBufferSize : object->WriteBufferSize,
                        buffer, length);
    } else if (option == INTERNET_OPTION_EXTENDED_ERROR) {
        ok = QueryDword(0, buffer, length);
    } else if (option == INTERNET_OPTION_USER_AGENT) {
        if (object->Type == INTERNET_HANDLE_TYPE_INTERNET) {
            AcquireSRWLockShared(&TableLock);
            ok = QueryString(object->Agent, buffer, length);
            ReleaseSRWLockShared(&TableLock);
        } else {
            ok = InetFail(ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
        }
    } else if (option == INTERNET_OPTION_USERNAME || option == INTERNET_OPTION_PASSWORD ||
               option == INTERNET_OPTION_PROXY_USERNAME || option == INTERNET_OPTION_PROXY_PASSWORD) {
        LPCWSTR value = option == INTERNET_OPTION_USERNAME         ? object->User
                        : option == INTERNET_OPTION_PASSWORD       ? object->Password
                        : option == INTERNET_OPTION_PROXY_USERNAME ? object->ProxyUser
                                                                   : object->ProxyPassword;
        ok = object->Type == INTERNET_HANDLE_TYPE_INTERNET ? InetFail(ERROR_INTERNET_INCORRECT_HANDLE_TYPE)
                                                           : QueryString(value ? value : L"", buffer, length);
    } else if (!IsRequest(object)) {
        ok = InetFail(option == INTERNET_OPTION_URL || option == INTERNET_OPTION_REQUEST_FLAGS ||
                              option == INTERNET_OPTION_SECURITY_FLAGS ||
                              option == INTERNET_OPTION_SECURITY_CERTIFICATE_STRUCT ||
                              option == INTERNET_OPTION_SECURITY_CERTIFICATE ||
                              option == INTERNET_OPTION_SECURITY_KEY_BITNESS ||
                              option == INTERNET_OPTION_DATAFILE_NAME || option == INTERNET_OPTION_KEEP_CONNECTION
                          ? ERROR_INTERNET_INCORRECT_HANDLE_TYPE
                          : ERROR_INTERNET_INVALID_OPTION);
    } else if (option == INTERNET_OPTION_URL) {
        PWSTR url = InetRequestUrl(object);
        ok = url ? QueryString(url, buffer, length) : InetFail(ERROR_NOT_ENOUGH_MEMORY);
        InetFree(url);
    } else if (option == INTERNET_OPTION_REQUEST_FLAGS) {
        DWORD flags = INTERNET_REQFLAG_CACHE_WRITE_DISABLED;
        if (object->ViaProxy)
            flags |= INTERNET_REQFLAG_VIA_PROXY;
        ok = QueryDword(flags, buffer, length);
    } else if (option == INTERNET_OPTION_KEEP_CONNECTION) {
        ok = QueryDword(object->Flags & INTERNET_FLAG_KEEP_CONNECTION ? 1 : 0, buffer, length);
    } else if (option == INTERNET_OPTION_SECURITY_FLAGS) {
        ok = QueryDword(SecurityFlags(object), buffer, length);
    } else if (option == INTERNET_OPTION_SECURITY_KEY_BITNESS) {
        ok = object->Certificate ? QueryDword(object->CipherStrength, buffer, length)
                                 : InetFail(ERROR_INTERNET_INVALID_OPERATION);
    } else if (option == INTERNET_OPTION_SECURITY_CERTIFICATE_STRUCT) {
        ok = QueryCertificate(object, buffer, length);
    } else if (option == INTERNET_OPTION_SECURITY_CERTIFICATE) {
        ok = QueryCertificateText(object, buffer, length);
    } else if (option == INTERNET_OPTION_DATAFILE_NAME) {
        /* without a cache, responses have no backing file. */
        ok = InetFail(ERROR_INTERNET_ITEM_NOT_FOUND);
    } else {
        ok = InetFail(ERROR_INTERNET_INVALID_OPTION);
    }
    InetRelease(object);
    return ok;
}

static BOOL SetString(PWSTR *field, LPCWSTR value, DWORD length)
{
    PWSTR copy = InetDuplicateN(value ? value : L"", value ? length : 0);
    if (!copy)
        return InetFail(ERROR_NOT_ENOUGH_MEMORY);
    AcquireSRWLockExclusive(&TableLock);
    PWSTR previous = *field;
    *field = copy;
    ReleaseSRWLockExclusive(&TableLock);
    InetFree(previous);
    return TRUE;
}

/* measure wide option text, honoring terminators before the byte limit. */
static DWORD StringLength(LPCWSTR value, DWORD bytes)
{
    DWORD chars = 0;
    if (!bytes)
        return value ? (DWORD)lstrlenW(value) : 0;
    while (chars < bytes / sizeof(WCHAR) && value[chars])
        chars++;
    return chars;
}

DECLSPEC_EXPORT BOOL WINAPI InternetSetOptionW(HINTERNET handle, DWORD option, LPVOID buffer, DWORD length)
{
    INET_OBJECT *object = nullptr;
    if (handle && !(object = InetReference(handle)))
        return FALSE;
    BOOL ok = TRUE;
    BOOL is_dword = (option >= INET_TIMEOUT_FIRST && option <= INET_TIMEOUT_LAST) ||
                    option == INTERNET_OPTION_MAX_CONNS_PER_SERVER ||
                    option == INTERNET_OPTION_MAX_CONNS_PER_1_0_SERVER || option == INTERNET_OPTION_SECURITY_FLAGS ||
                    option == INTERNET_OPTION_READ_BUFFER_SIZE || option == INTERNET_OPTION_WRITE_BUFFER_SIZE ||
                    option == INTERNET_OPTION_HTTP_DECODING;
    if (is_dword && (!buffer || length < sizeof(DWORD))) {
        ok = InetFail(buffer ? ERROR_INTERNET_BAD_OPTION_LENGTH : ERROR_INVALID_PARAMETER);
    } else if (option >= INET_TIMEOUT_FIRST && option <= INET_TIMEOUT_LAST) {
        DWORD value = *(DWORD *)buffer;
        if (object)
            INET_TIMEOUT(object, option) = value;
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
    } else if (option == INTERNET_OPTION_SETTINGS_CHANGED || option == INTERNET_OPTION_REFRESH ||
               option == INTERNET_OPTION_END_BROWSER_SESSION || option == INTERNET_OPTION_RESET_URLCACHE_SESSION ||
               option == INTERNET_OPTION_PROXY_SETTINGS_CHANGED || option == INTERNET_OPTION_IGNORE_OFFLINE ||
               option == INTERNET_OPTION_DISABLE_AUTODIAL || option == INTERNET_OPTION_ERROR_MASK ||
               option == INTERNET_OPTION_SUPPRESS_BEHAVIOR || option == INTERNET_OPTION_CODEPAGE ||
               option == INTERNET_OPTION_ENCODE_EXTRA || option == INTERNET_OPTION_REQUEST_PRIORITY ||
               option == INTERNET_OPTION_IDN || option == INTERNET_OPTION_COOKIES_3RD_PARTY) {
        /* no effect: proxy settings are read per request and caching is absent. */
    } else if (option == INTERNET_OPTION_HANDLE_TYPE || option == INTERNET_OPTION_VERSION ||
               option == INTERNET_OPTION_URL || option == INTERNET_OPTION_CONNECTED_STATE ||
               option == INTERNET_OPTION_SECURITY_CERTIFICATE_STRUCT ||
               option == INTERNET_OPTION_SECURITY_CERTIFICATE || option == INTERNET_OPTION_SECURITY_KEY_BITNESS ||
               option == INTERNET_OPTION_PARENT_HANDLE || option == INTERNET_OPTION_REQUEST_FLAGS ||
               option == INTERNET_OPTION_EXTENDED_ERROR || option == INTERNET_OPTION_DATAFILE_NAME) {
        ok = InetFail(ERROR_INTERNET_OPTION_NOT_SETTABLE);
    } else if (option == INTERNET_OPTION_HTTP_VERSION) {
        if (!buffer || length < sizeof(HTTP_VERSION_INFO))
            ok = InetFail(ERROR_INTERNET_BAD_OPTION_LENGTH);
    } else if (option == INTERNET_OPTION_HTTP_DECODING) {
        if (!object)
            ok = InetFail(ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
        else
            object->Decoding = *(DWORD *)buffer != 0;
    } else if (option == INTERNET_OPTION_PROXY) {
        if (!buffer || length < sizeof(INTERNET_PROXY_INFO)) {
            ok = InetFail(ERROR_INTERNET_BAD_OPTION_LENGTH);
        } else if (object && object->Type != INTERNET_HANDLE_TYPE_INTERNET) {
            ok = InetFail(ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
        } else if (object) {
            const INTERNET_PROXY_INFO *info = buffer;
            object->AccessType = info->dwAccessType;
            if (info->dwAccessType == INTERNET_OPEN_TYPE_PROXY) {
                ok = SetString(&object->Proxy, info->lpszProxy, info->lpszProxy ? lstrlenW(info->lpszProxy) : 0) &&
                     SetString(&object->ProxyBypass, info->lpszProxyBypass,
                               info->lpszProxyBypass ? lstrlenW(info->lpszProxyBypass) : 0);
            }
        }
    } else if (!object) {
        ok = InetFail(option == INTERNET_OPTION_CONTEXT_VALUE || option == INTERNET_OPTION_USER_AGENT ||
                              option == INTERNET_OPTION_SECURITY_FLAGS
                          ? ERROR_INTERNET_INCORRECT_HANDLE_TYPE
                          : ERROR_INTERNET_INVALID_OPTION);
    } else if (option == INTERNET_OPTION_CONTEXT_VALUE) {
        if (!buffer || length != sizeof(DWORD_PTR))
            ok = InetFail(ERROR_INTERNET_BAD_OPTION_LENGTH);
        else
            object->Context = *(DWORD_PTR *)buffer;
    } else if (option == INTERNET_OPTION_READ_BUFFER_SIZE) {
        object->ReadBufferSize = *(DWORD *)buffer;
    } else if (option == INTERNET_OPTION_WRITE_BUFFER_SIZE) {
        object->WriteBufferSize = *(DWORD *)buffer;
    } else if (option == INTERNET_OPTION_USER_AGENT) {
        if (object->Type != INTERNET_HANDLE_TYPE_INTERNET)
            ok = InetFail(ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
        else
            ok = SetString(&object->Agent, buffer, StringLength(buffer, length));
    } else if (option == INTERNET_OPTION_USERNAME || option == INTERNET_OPTION_PASSWORD ||
               option == INTERNET_OPTION_PROXY_USERNAME || option == INTERNET_OPTION_PROXY_PASSWORD) {
        PWSTR *field = option == INTERNET_OPTION_USERNAME         ? &object->User
                       : option == INTERNET_OPTION_PASSWORD       ? &object->Password
                       : option == INTERNET_OPTION_PROXY_USERNAME ? &object->ProxyUser
                                                                  : &object->ProxyPassword;
        if (object->Type == INTERNET_HANDLE_TYPE_INTERNET)
            ok = InetFail(ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
        else
            ok = SetString(field, buffer, StringLength(buffer, length));
    } else if (option == INTERNET_OPTION_SECURITY_FLAGS) {
        if (!IsRequest(object))
            ok = InetFail(ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
        else
            object->SecurityFlags = *(DWORD *)buffer & (SECURITY_SET_MASK | SECURITY_FLAG_IGNORE_REDIRECT_TO_HTTP |
                                                        SECURITY_FLAG_IGNORE_REDIRECT_TO_HTTPS);
    } else {
        ok = InetFail(ERROR_INTERNET_INVALID_OPTION);
    }
    InetRelease(object);
    if (ok)
        SetLastError(ERROR_SUCCESS);
    return ok;
}
