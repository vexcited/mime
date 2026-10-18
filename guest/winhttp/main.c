/*
 * winhttp entry point, the handle table, sessions, connections, requests, status callbacks and options.
 */
#include "winhttp_private.h"
#include <mime/stub.h>

#define WH_MAX_SLOTS 16384

static SRWLOCK TableLock = SRWLOCK_INIT;
static WH_OBJECT **Slots;
static WORD *Generations;
static DWORD SlotCount;

/* the process wide http version of WINHTTP_OPTION_HTTP_VERSION with a NULL handle. */
static DWORD DefaultMajor = 1, DefaultMinor = 1;
static DWORD WorkerThreads;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

static INIT_ONCE StartupOnce = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK Startup(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
    return TRUE;
}

/* winsock for the process, started outside the loader lock by the first call that needs it. */
void WhStartup(void)
{
    InitOnceExecuteOnce(&StartupOnce, Startup, nullptr, nullptr);
}

void *WhAlloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
}

void *WhAllocZero(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

void *WhRealloc(void *memory, SIZE_T size)
{
    if (!memory)
        return WhAlloc(size);
    return HeapReAlloc(GetProcessHeap(), 0, memory, size ? size : 1);
}

void WhFree(void *memory)
{
    if (memory)
        HeapFree(GetProcessHeap(), 0, memory);
}

PWSTR WhDupWn(LPCWSTR text, DWORD length)
{
    if (!text)
        return nullptr;
    PWSTR copy = WhAlloc(((SIZE_T)length + 1) * sizeof(WCHAR));
    if (copy) {
        memcpy(copy, text, (SIZE_T)length * sizeof(WCHAR));
        copy[length] = 0;
    }
    return copy;
}

PWSTR WhDupW(LPCWSTR text)
{
    return text ? WhDupWn(text, (DWORD)wcslen(text)) : nullptr;
}

char *WhDupAn(const char *text, DWORD length)
{
    if (!text)
        return nullptr;
    char *copy = WhAlloc((SIZE_T)length + 1);
    if (copy) {
        memcpy(copy, text, length);
        copy[length] = 0;
    }
    return copy;
}

char *WhDupA(const char *text)
{
    return text ? WhDupAn(text, (DWORD)strlen(text)) : nullptr;
}

PWSTR WhAnsiToWide(const char *text, DWORD length)
{
    int count = MultiByteToWideChar(CP_ACP, 0, text, (int)length, nullptr, 0);
    PWSTR wide = WhAlloc(((SIZE_T)count + 1) * sizeof(WCHAR));
    if (!wide)
        return nullptr;
    MultiByteToWideChar(CP_ACP, 0, text, (int)length, wide, count);
    wide[count] = 0;
    return wide;
}

/* the ansi form windows sends on the wire, refusing best fit replacements like winhttp does. */
char *WhWideToAnsi(LPCWSTR text, DWORD length, BOOL *lossy)
{
    BOOL used = FALSE;
    int count = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, text, (int)length, nullptr, 0, nullptr, &used);
    char *ansi = WhAlloc((SIZE_T)count + 1);
    if (!ansi)
        return nullptr;
    WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, text, (int)length, ansi, count, nullptr, &used);
    ansi[count] = 0;
    if (lossy)
        *lossy = used;
    return ansi;
}

BOOL WhFail(DWORD error)
{
    SetLastError(error);
    return FALSE;
}

/* ------------------------------------------------------------------ handles */

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

static BOOL Insert(WH_OBJECT *object)
{
    AcquireSRWLockExclusive(&TableLock);
    DWORD slot = 0;
    while (slot < SlotCount && Slots[slot])
        slot++;
    if (slot == SlotCount) {
        if (SlotCount == WH_MAX_SLOTS) {
            ReleaseSRWLockExclusive(&TableLock);
            return FALSE;
        }
        DWORD count = SlotCount ? SlotCount * 2 : 64;
        WH_OBJECT **slots = WhRealloc(Slots, count * sizeof(*slots));
        WORD *generations = slots ? WhRealloc(Generations, count * sizeof(*generations)) : nullptr;
        if (!generations) {
            if (slots)
                Slots = slots;
            ReleaseSRWLockExclusive(&TableLock);
            return FALSE;
        }
        for (DWORD i = SlotCount; i < count; i++) {
            slots[i] = nullptr;
            generations[i] = 1;
        }
        Slots = slots;
        Generations = generations;
        SlotCount = count;
    }
    Slots[slot] = object;
    object->Handle = (HINTERNET)(ULONG_PTR)(((ULONG_PTR)(Generations[slot] & 0x7FFF) << 16) | ((slot + 1) << 2));
    ReleaseSRWLockExclusive(&TableLock);
    return TRUE;
}

static WH_OBJECT *Remove(HINTERNET handle)
{
    AcquireSRWLockExclusive(&TableLock);
    DWORD slot = SlotOf(handle);
    WH_OBJECT *object = nullptr;
    if (slot != UINT32_MAX) {
        object = Slots[slot];
        Slots[slot] = nullptr;
        Generations[slot] = (WORD)((Generations[slot] + 1) & 0x7FFF);
        if (!Generations[slot])
            Generations[slot] = 1;
    }
    ReleaseSRWLockExclusive(&TableLock);
    return object;
}

WH_OBJECT *WhReference(HINTERNET handle)
{
    AcquireSRWLockShared(&TableLock);
    DWORD slot = SlotOf(handle);
    WH_OBJECT *object = slot == UINT32_MAX ? nullptr : Slots[slot];
    if (object)
        InterlockedIncrement(&object->Refs);
    ReleaseSRWLockShared(&TableLock);
    if (!object)
        SetLastError(ERROR_INVALID_HANDLE);
    return object;
}

void WhAddRef(WH_OBJECT *object)
{
    InterlockedIncrement(&object->Refs);
}

WH_OBJECT *WhCreate(DWORD type, WH_OBJECT *parent)
{
    WH_OBJECT *object = WhAllocZero(sizeof(*object));
    if (!object)
        return nullptr;
    object->Refs = 1;
    object->Type = type;
    InitializeCriticalSection(&object->Lock);
    if (parent) {
        WhAddRef(parent);
        object->Parent = parent;
        object->Callback = parent->Callback;
        object->NotifyFlags = parent->NotifyFlags;
        object->Context = 0;
        object->Timeouts = parent->Timeouts;
        object->Flags = parent->Flags;
        object->Decompression = parent->Decompression;
        object->RedirectPolicy = parent->RedirectPolicy;
        object->MaxRedirects = parent->MaxRedirects;
        object->MaxResponseHeaderSize = parent->MaxResponseHeaderSize;
        object->MaxResponseDrainSize = parent->MaxResponseDrainSize;
        object->SecureProtocols = parent->SecureProtocols;
        object->RejectUserPassword = parent->RejectUserPassword;
        object->AccessType = parent->AccessType;
        object->Proxy = WhDupW(parent->Proxy);
        object->ProxyBypass = WhDupW(parent->ProxyBypass);
    }
    if (!Insert(object)) {
        WhRelease(object);
        SetLastError(ERROR_WINHTTP_OUT_OF_HANDLES);
        return nullptr;
    }
    WhAddRef(object);
    return object;
}

WH_OBJECT *WhSession(WH_OBJECT *object)
{
    while (object && object->Type != WH_SESSION)
        object = object->Parent;
    return object;
}

BOOL WhIsAsync(WH_OBJECT *object)
{
    WH_OBJECT *session = WhSession(object);
    return session && (session->Flags & WINHTTP_FLAG_ASYNC);
}

void WhNotify(WH_OBJECT *object, DWORD status, LPVOID info, DWORD length)
{
    WINHTTP_STATUS_CALLBACK callback = object->Callback;
    if (callback && (object->NotifyFlags & status))
        callback(object->Handle, object->Context, status, info, length);
}

void WhRelease(WH_OBJECT *object)
{
    if (!object || InterlockedDecrement(&object->Refs))
        return;
    HINTERNET handle = object->Handle;
    if (object->Type == WH_REQUEST)
        WhDropConnection(object);
    if (object->Type == WH_SESSION)
        WhSessionCleanup(object);
    if (object->Type == WH_WEBSOCKET)
        WhWebSocketReleased(object);
    /* the last notification of a handle, after every operation on it ended. */
    if (handle && object->Closed)
        WhNotify(object, WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING, &handle, sizeof(handle));
    if (object->Worker)
        CloseHandle(object->Worker);
    if (object->Wake)
        CloseHandle(object->Wake);
    WhHeadersClear(&object->Request);
    WhHeadersClear(&object->Response);
    WhFree(object->Agent);
    WhFree(object->Proxy);
    WhFree(object->ProxyBypass);
    WhFree(object->Server);
    WhFree(object->Verb);
    WhFree(object->Path);
    WhFree(object->Referrer);
    WhFree(object->Host);
    WhFree(object->User);
    WhFree(object->Password);
    WhFree(object->ProxyUser);
    WhFree(object->ProxyPassword);
    WhFree(object->Optional);
    WhFree(object->StatusText);
    WhFree(object->RawStatusLine);
    WhFree(object->Memory);
    for (DWORD i = 0; i < object->ResolverResult.cEntries; i++)
        GlobalFree(object->ResolverResult.pEntries[i].pwszProxy);
    if (object->ResolverResult.pEntries)
        GlobalFree(object->ResolverResult.pEntries);
    WH_OBJECT *parent = object->Parent;
    WH_OBJECT *owner = object->Owner;
    DeleteCriticalSection(&object->Lock);
    WhFree(object);
    WhRelease(owner);
    WhRelease(parent);
}

/* ------------------------------------------------------------------ sessions */

static void InitSession(WH_OBJECT *session, DWORD flags)
{
    session->Flags = flags;
    session->Timeouts = (WH_TIMEOUTS){0, 60000, 30000, 30000, 90000};
    session->RedirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DEFAULT;
    session->MaxRedirects = 10;
    session->MaxResponseHeaderSize = 64 * 1024;
    session->MaxResponseDrainSize = 1024 * 1024;
    session->MaxConnsPerServer = 2;
    session->MaxConnsPer10Server = 2;
    session->SecureProtocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    session->ReadBufferSize = 8192;
    session->WriteBufferSize = 8192;
}

static DWORD SetProxy(WH_OBJECT *object, DWORD access, LPCWSTR proxy, LPCWSTR bypass)
{
    if (access == WINHTTP_ACCESS_TYPE_NAMED_PROXY && (!proxy || !*proxy))
        return ERROR_INVALID_PARAMETER;
    if (access != WINHTTP_ACCESS_TYPE_DEFAULT_PROXY && access != WINHTTP_ACCESS_TYPE_NO_PROXY &&
        access != WINHTTP_ACCESS_TYPE_NAMED_PROXY && access != WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY)
        return ERROR_INVALID_PARAMETER;
    PWSTR proxy_copy = access == WINHTTP_ACCESS_TYPE_NAMED_PROXY ? WhDupW(proxy) : nullptr;
    PWSTR bypass_copy = access == WINHTTP_ACCESS_TYPE_NAMED_PROXY ? WhDupW(bypass) : nullptr;
    EnterCriticalSection(&object->Lock);
    WhFree(object->Proxy);
    WhFree(object->ProxyBypass);
    object->AccessType = access;
    object->Proxy = proxy_copy;
    object->ProxyBypass = bypass_copy;
    LeaveCriticalSection(&object->Lock);
    return ERROR_SUCCESS;
}

HINTERNET WINAPI WinHttpOpen(LPCWSTR agent, DWORD access, LPCWSTR proxy, LPCWSTR bypass, DWORD flags)
{
    WhStartup();
    if (flags & ~WINHTTP_FLAG_SECURE_DEFAULTS || (flags & WINHTTP_FLAG_SECURE_DEFAULTS) == 0x20000000) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    WH_OBJECT *session = WhCreate(WH_SESSION, nullptr);
    if (!session)
        return nullptr;
    InitSession(session, flags);
    DWORD error = ERROR_SUCCESS;
    if (agent && !(session->Agent = WhDupW(agent)))
        error = ERROR_NOT_ENOUGH_MEMORY;
    if (!error)
        error = SetProxy(session, access, proxy, bypass);
    HINTERNET handle = session->Handle;
    WhRelease(session);
    if (error) {
        WinHttpCloseHandle(handle);
        SetLastError(error);
        return nullptr;
    }
    SetLastError(ERROR_SUCCESS);
    return handle;
}

/* ------------------------------------------------------------------ connections */

/* rfc 3492 punycode of one label, for the host names of WinHttpConnect. */
static BOOL Punycode(const DWORD *code, DWORD count, char *out, DWORD *length, DWORD capacity)
{
    DWORD n = 128, delta = 0, bias = 72, h = 0, b = 0, o = *length;
    for (DWORD i = 0; i < count; i++)
        if (code[i] < 128) {
            if (o >= capacity)
                return FALSE;
            out[o++] = (char)code[i];
            b++;
        }
    h = b;
    if (b) {
        if (o >= capacity)
            return FALSE;
        out[o++] = '-';
    }
    while (h < count) {
        DWORD m = UINT32_MAX;
        for (DWORD i = 0; i < count; i++)
            if (code[i] >= n && code[i] < m)
                m = code[i];
        delta += (m - n) * (h + 1);
        n = m;
        for (DWORD i = 0; i < count; i++) {
            if (code[i] < n)
                delta++;
            if (code[i] != n)
                continue;
            DWORD q = delta;
            for (DWORD k = 36;; k += 36) {
                DWORD t = k <= bias ? 1 : k >= bias + 26 ? 26 : k - bias;
                if (q < t)
                    break;
                DWORD digit = t + (q - t) % (36 - t);
                if (o >= capacity)
                    return FALSE;
                out[o++] = (char)(digit < 26 ? 'a' + digit : '0' + digit - 26);
                q = (q - t) / (36 - t);
            }
            if (o >= capacity)
                return FALSE;
            out[o++] = (char)(q < 26 ? 'a' + q : '0' + q - 26);
            DWORD d = h == b ? delta / 700 : delta / 2;
            d += d / (h + 1);
            DWORD k = 0;
            while (d > 455) {
                d /= 35;
                k += 36;
            }
            bias = k + 36 * d / (d + 38);
            delta = 0;
            h++;
        }
        delta++;
        n++;
    }
    *length = o;
    return TRUE;
}

/* the ascii form of a host name: ascii passes through, other labels become xn-- punycode. */
static BOOL HostToAscii(LPCWSTR host, char *out, DWORD capacity)
{
    DWORD o = 0;
    LPCWSTR label = host;
    while (TRUE) {
        LPCWSTR end = label;
        while (*end && *end != '.')
            end++;
        DWORD code[256], count = 0;
        BOOL ascii = TRUE;
        for (LPCWSTR p = label; p < end; p++) {
            DWORD c = *p;
            if (c >= 0xD800 && c < 0xDC00 && p + 1 < end && p[1] >= 0xDC00 && p[1] < 0xE000) {
                c = 0x10000 + ((c - 0xD800) << 10) + (p[1] - 0xDC00);
                p++;
            }
            if (c >= 128)
                ascii = FALSE;
            else if (c >= 'A' && c <= 'Z' && !ascii)
                c += 32;
            if (count == 256)
                return FALSE;
            code[count++] = c;
        }
        if (ascii) {
            for (DWORD i = 0; i < count; i++) {
                if (o >= capacity)
                    return FALSE;
                out[o++] = (char)code[i];
            }
        } else {
            for (DWORD i = 0; i < count; i++)
                if (code[i] >= 'A' && code[i] <= 'Z')
                    code[i] += 32;
            if (o + 4 >= capacity)
                return FALSE;
            memcpy(out + o, "xn--", 4);
            o += 4;
            if (!Punycode(code, count, out, &o, capacity))
                return FALSE;
        }
        if (!*end)
            break;
        if (o >= capacity)
            return FALSE;
        out[o++] = '.';
        label = end + 1;
    }
    if (o >= capacity)
        return FALSE;
    out[o] = 0;
    return TRUE;
}

/* characters winhttp refuses in a host name that is not a literal address. */
static BOOL BadHostChar(char c)
{
    if (c < 0x21 || c > 0x7E)
        return TRUE;
    return strchr("\"#%<>[\\]^`{|}~!$&'()*+,/:;=?@", c) != nullptr;
}

static BOOL IsBracketedIpv6(const char *host)
{
    size_t length = strlen(host);
    if (length < 3 || host[0] != '[' || host[length - 1] != ']')
        return FALSE;
    char inner[64];
    if (length - 2 >= sizeof(inner))
        return FALSE;
    memcpy(inner, host + 1, length - 2);
    inner[length - 2] = 0;
    IN6_ADDR address;
    return inet_pton(AF_INET6, inner, &address) == 1;
}

HINTERNET WINAPI WinHttpConnect(HINTERNET session_handle, LPCWSTR server, INTERNET_PORT port, DWORD reserved)
{
    if (reserved || !server) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    char ascii[256];
    if (!*server || !HostToAscii(server, ascii, sizeof(ascii))) {
        SetLastError(ERROR_WINHTTP_INVALID_URL);
        return nullptr;
    }
    if (!IsBracketedIpv6(ascii)) {
        for (const char *p = ascii; *p; p++)
            if (BadHostChar(*p)) {
                SetLastError(ERROR_WINHTTP_INVALID_URL);
                return nullptr;
            }
    }
    WH_OBJECT *session = WhReference(session_handle);
    if (!session)
        return nullptr;
    if (session->Type != WH_SESSION) {
        WhRelease(session);
        SetLastError(ERROR_WINHTTP_INCORRECT_HANDLE_TYPE);
        return nullptr;
    }
    WH_OBJECT *connect = WhCreate(WH_CONNECT, session);
    WhRelease(session);
    if (!connect)
        return nullptr;
    connect->Server = WhAnsiToWide(ascii, (DWORD)strlen(ascii));
    connect->Port = port;
    HINTERNET handle = connect->Handle;
    WhNotify(connect, WINHTTP_CALLBACK_STATUS_HANDLE_CREATED, &handle, sizeof(handle));
    WhRelease(connect);
    SetLastError(ERROR_SUCCESS);
    return handle;
}

/* ------------------------------------------------------------------ requests */

static BOOL TokenChar(WCHAR c)
{
    if (c >= '0' && c <= '9')
        return TRUE;
    if ((c | 0x20) >= 'a' && (c | 0x20) <= 'z')
        return TRUE;
    return c && c < 128 && strchr("!#$%&'*+-.^_`|~", (char)c) != nullptr;
}

/* the verb without the blanks around it, or FALSE for characters http does not allow in a method. */
static BOOL ParseVerb(LPCWSTR verb, LPCWSTR *start, DWORD *length)
{
    while (*verb == ' ' || *verb == '\t')
        verb++;
    *start = verb;
    LPCWSTR p = verb;
    while (TokenChar(*p))
        p++;
    *length = (DWORD)(p - verb);
    while (*p == ' ' || *p == '\t')
        p++;
    return !*p;
}

static void ParseVersion(LPCWSTR version, DWORD *major, DWORD *minor)
{
    *major = DefaultMajor;
    *minor = DefaultMinor;
    if (!version || !*version || _wcsnicmp(version, L"HTTP/", 5))
        return;
    LPCWSTR p = version + 5;
    DWORD a = 0, b = 0;
    LPCWSTR digits = p;
    while (*p >= '0' && *p <= '9')
        a = a * 10 + (*p++ - '0');
    if (p == digits || *p++ != '.')
        return;
    digits = p;
    while (*p >= '0' && *p <= '9')
        b = b * 10 + (*p++ - '0');
    if (p == digits || *p || a != 1 || b > 1)
        return;
    *major = a;
    *minor = b;
}

static BOOL EscapedInPath(WCHAR c, DWORD flags)
{
    if (c >= 128)
        return FALSE;
    if (c <= 0x20 || c == 0x7F)
        return TRUE;
    if (c == '%')
        return (flags & WINHTTP_FLAG_ESCAPE_PERCENT) != 0;
    return strchr("\"#<>[\\]^`{|}", (char)c) != nullptr;
}

static PWSTR EscapePath(LPCWSTR object, DWORD flags)
{
    if (flags & WINHTTP_FLAG_ESCAPE_DISABLE)
        return WhDupW(object);
    SIZE_T length = wcslen(object);
    PWSTR path = WhAlloc((length * 3 + 1) * sizeof(WCHAR));
    if (!path)
        return nullptr;
    PWSTR out = path;
    BOOL query = FALSE;
    for (LPCWSTR p = object; *p; p++) {
        if (*p == '?' && !query) {
            query = TRUE;
            *out++ = *p;
            continue;
        }
        if (EscapedInPath(*p, flags) && !(query && flags & WINHTTP_FLAG_ESCAPE_DISABLE_QUERY)) {
            static const char hex[] = "0123456789ABCDEF";
            *out++ = '%';
            *out++ = hex[(*p >> 4) & 15];
            *out++ = hex[*p & 15];
        } else {
            *out++ = *p;
        }
    }
    *out = 0;
    return path;
}

static BOOL BadRequestPort(INTERNET_PORT port)
{
    static const INTERNET_PORT ports[] = {19, 21, 25, 70, 110, 119, 143, 220, 993};
    for (DWORD i = 0; i < ARRAYSIZE(ports); i++)
        if (port == ports[i])
            return TRUE;
    return FALSE;
}

HINTERNET WINAPI WinHttpOpenRequest(HINTERNET connect_handle, LPCWSTR verb, LPCWSTR object, LPCWSTR version,
                                    LPCWSTR referrer, LPCWSTR *accept, DWORD flags)
{
    if (flags & ~0x008003CCu) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    WH_OBJECT *connect = WhReference(connect_handle);
    if (!connect)
        return nullptr;
    if (connect->Type != WH_CONNECT) {
        WhRelease(connect);
        SetLastError(ERROR_WINHTTP_INCORRECT_HANDLE_TYPE);
        return nullptr;
    }
    LPCWSTR verb_start = L"GET";
    DWORD verb_length = 3;
    if (verb && *verb && !ParseVerb(verb, &verb_start, &verb_length)) {
        WhRelease(connect);
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    if (version)
        for (LPCWSTR p = version; *p; p++)
            if (*p == '\r' || *p == '\n') {
                WhRelease(connect);
                SetLastError(ERROR_INVALID_PARAMETER);
                return nullptr;
            }
    if (BadRequestPort(connect->Port)) {
        WhRelease(connect);
        SetLastError(ERROR_WINHTTP_INVALID_URL);
        return nullptr;
    }
    WH_OBJECT *session = WhSession(connect);
    if ((session->Flags & WINHTTP_FLAG_SECURE_DEFAULTS) == WINHTTP_FLAG_SECURE_DEFAULTS &&
        !(flags & WINHTTP_FLAG_SECURE)) {
        WhRelease(connect);
        SetLastError(ERROR_ACCESS_DENIED);
        return nullptr;
    }
    WH_OBJECT *request = WhCreate(WH_REQUEST, connect);
    WhRelease(connect);
    if (!request)
        return nullptr;
    request->Flags = flags;
    request->Secure = (flags & WINHTTP_FLAG_SECURE) != 0;
    request->HostPort = connect->Port ? connect->Port
                                      : (request->Secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT);
    request->Host = WhDupW(connect->Server);
    request->Verb = WhDupWn(verb_start, verb_length);
    request->Path = object && *object ? EscapePath(object, flags) : WhDupW(L"/");
    ParseVersion(version, &request->Major, &request->Minor);
    request->ReadBufferSize = session->ReadBufferSize;
    request->WriteBufferSize = session->WriteBufferSize;
    request->Agent = WhDupW(session->Agent);
    DWORD error = ERROR_SUCCESS;
    if (!request->Host || !request->Verb || !request->Path)
        error = ERROR_NOT_ENOUGH_MEMORY;
    if (!error && referrer && *referrer) {
        request->Referrer = WhDupW(referrer);
        char *text = WhWideToAnsi(referrer, (DWORD)wcslen(referrer), nullptr);
        if (!text || !WhHeadersAppend(&request->Request, "Referer", 7, text, (DWORD)strlen(text)))
            error = ERROR_NOT_ENOUGH_MEMORY;
        WhFree(text);
    }
    for (LPCWSTR *type = accept; !error && type && *type; type++) {
        char *text = WhWideToAnsi(*type, (DWORD)wcslen(*type), nullptr);
        if (!text)
            error = ERROR_NOT_ENOUGH_MEMORY;
        if (text && *text) {
            char *line = WhAlloc(strlen(text) + 9);
            if (!line) {
                error = ERROR_NOT_ENOUGH_MEMORY;
            } else {
                memcpy(line, "Accept: ", 8);
                strcpy(line + 8, text);
                error = WhAddHeaders(request, line, (DWORD)strlen(line),
                                     WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_COALESCE_WITH_COMMA);
                if (error == ERROR_INSUFFICIENT_BUFFER)
                    error = ERROR_INVALID_PARAMETER;
                WhFree(line);
            }
        }
        WhFree(text);
    }
    HINTERNET handle = request->Handle;
    if (error) {
        WhRelease(request);
        WinHttpCloseHandle(handle);
        SetLastError(error);
        return nullptr;
    }
    WhNotify(request, WINHTTP_CALLBACK_STATUS_HANDLE_CREATED, &handle, sizeof(handle));
    WhRelease(request);
    SetLastError(ERROR_SUCCESS);
    return handle;
}

BOOL WINAPI WinHttpCloseHandle(HINTERNET handle)
{
    WH_OBJECT *object = Remove(handle);
    if (!object)
        return WhFail(ERROR_INVALID_HANDLE);
    object->Closed = TRUE;
    /* a pending operation ends with ERROR_WINHTTP_OPERATION_CANCELLED once its socket is gone. */
    if (object->Type == WH_REQUEST || object->Type == WH_WEBSOCKET) {
        EnterCriticalSection(&object->Lock);
        if (object->Busy && object->Conn)
            shutdown(object->Conn->Socket, SD_BOTH);
        if (object->Wake)
            SetEvent(object->Wake);
        LeaveCriticalSection(&object->Lock);
    }
    WhRelease(object);
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

WINHTTP_STATUS_CALLBACK WINAPI WinHttpSetStatusCallback(HINTERNET handle, WINHTTP_STATUS_CALLBACK callback,
                                                        DWORD flags, DWORD_PTR reserved)
{
    if ((callback && !flags) || reserved) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return WINHTTP_INVALID_STATUS_CALLBACK;
    }
    if (!handle) {
        SetLastError(ERROR_WINHTTP_INCORRECT_HANDLE_TYPE);
        return WINHTTP_INVALID_STATUS_CALLBACK;
    }
    WH_OBJECT *object = WhReference(handle);
    if (!object)
        return WINHTTP_INVALID_STATUS_CALLBACK;
    EnterCriticalSection(&object->Lock);
    WINHTTP_STATUS_CALLBACK previous = object->Callback;
    object->Callback = callback;
    object->NotifyFlags = callback ? flags : 0;
    LeaveCriticalSection(&object->Lock);
    WhRelease(object);
    SetLastError(ERROR_SUCCESS);
    return previous;
}

BOOL WINAPI WinHttpSetTimeouts(HINTERNET handle, int resolve, int connect, int send, int receive)
{
    if (!handle)
        return WhFail(ERROR_WINHTTP_INCORRECT_HANDLE_TYPE);
    int *values[] = {&resolve, &connect, &send, &receive};
    for (DWORD i = 0; i < 4; i++) {
        if (!*values[i])
            *values[i] = -1;
        else if (*values[i] < -1)
            return WhFail(ERROR_INVALID_PARAMETER);
    }
    WH_OBJECT *object = WhReference(handle);
    if (!object)
        return FALSE;
    if (object->Type != WH_SESSION && object->Type != WH_REQUEST) {
        WhRelease(object);
        return WhFail(ERROR_WINHTTP_INCORRECT_HANDLE_TYPE);
    }
    object->Timeouts.Resolve = resolve;
    object->Timeouts.Connect = connect;
    object->Timeouts.Send = send;
    object->Timeouts.Receive = receive;
    if ((DWORD)receive > (DWORD)object->Timeouts.ReceiveResponse)
        object->Timeouts.ReceiveResponse = receive;
    WhRelease(object);
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

BOOL WINAPI WinHttpCheckPlatform(void)
{
    return TRUE;
}

/* ------------------------------------------------------------------ options */

/* the options winhttp takes without a handle, the others failing as an incorrect handle type. */
static BOOL GlobalOption(DWORD option)
{
    if (option >= 38 && option <= 98 && (0x1800840000200005ull >> (option - 38) & 1))
        return TRUE;
    if (option >= 121 && option <= 184 && (0x80E0041010000001ull >> (option - 121) & 1))
        return TRUE;
    return option == WINHTTP_OPTION_EXTENDED_ERROR;
}

static DWORD *TimeoutSlot(WH_OBJECT *object, DWORD option)
{
    switch (option) {
    case WINHTTP_OPTION_RESOLVE_TIMEOUT:
        return (DWORD *)&object->Timeouts.Resolve;
    case WINHTTP_OPTION_CONNECT_TIMEOUT:
        return (DWORD *)&object->Timeouts.Connect;
    case WINHTTP_OPTION_SEND_TIMEOUT:
        return (DWORD *)&object->Timeouts.Send;
    case WINHTTP_OPTION_RECEIVE_TIMEOUT:
        return (DWORD *)&object->Timeouts.Receive;
    case WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT:
        return (DWORD *)&object->Timeouts.ReceiveResponse;
    }
    return nullptr;
}

static DWORD ConnectRetries = 5;

static DWORD SetOption(WH_OBJECT *object, DWORD option, LPVOID buffer, DWORD length)
{
    DWORD type = object ? object->Type : 0;
    BOOL session = type == WH_SESSION, request = type == WH_REQUEST;
    DWORD value = buffer && length >= 4 ? *(DWORD *)buffer : 0;
    switch (option) {
    case WINHTTP_OPTION_CALLBACK:
    case WINHTTP_OPTION_HANDLE_TYPE:
        return ERROR_WINHTTP_OPTION_NOT_SETTABLE;
    case WINHTTP_OPTION_RESOLVE_TIMEOUT:
    case WINHTTP_OPTION_CONNECT_TIMEOUT:
    case WINHTTP_OPTION_CONNECT_RETRIES:
    case WINHTTP_OPTION_SEND_TIMEOUT:
    case WINHTTP_OPTION_RECEIVE_TIMEOUT:
    case WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT:
    case WINHTTP_OPTION_AUTOLOGON_POLICY:
    case WINHTTP_OPTION_REDIRECT_POLICY:
    case WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS:
    case WINHTTP_OPTION_MAX_HTTP_STATUS_CONTINUE:
    case WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE:
    case WINHTTP_OPTION_MAX_RESPONSE_DRAIN_SIZE:
        if (length != 4)
            return ERROR_INSUFFICIENT_BUFFER;
        if (!session && !request && type != WH_WEBSOCKET)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        if (option == WINHTTP_OPTION_AUTOLOGON_POLICY && value > 3)
            return ERROR_INVALID_PARAMETER;
        if (option == WINHTTP_OPTION_REDIRECT_POLICY && value > WINHTTP_OPTION_REDIRECT_POLICY_LAST)
            return ERROR_INVALID_PARAMETER;
        if (TimeoutSlot(object, option))
            *TimeoutSlot(object, option) = value;
        else if (option == WINHTTP_OPTION_CONNECT_RETRIES)
            ConnectRetries = value;
        else if (option == WINHTTP_OPTION_REDIRECT_POLICY)
            object->RedirectPolicy = value;
        else if (option == WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS)
            object->MaxRedirects = value;
        else if (option == WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE)
            object->MaxResponseHeaderSize = value;
        else if (option == WINHTTP_OPTION_MAX_RESPONSE_DRAIN_SIZE)
            object->MaxResponseDrainSize = value;
        return ERROR_SUCCESS;
    case WINHTTP_OPTION_READ_BUFFER_SIZE:
    case WINHTTP_OPTION_WRITE_BUFFER_SIZE:
        if (!request)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        if (length != 4)
            return ERROR_INSUFFICIENT_BUFFER;
        if (!value)
            return ERROR_INVALID_PARAMETER;
        if (option == WINHTTP_OPTION_READ_BUFFER_SIZE)
            object->ReadBufferSize = value;
        else
            object->WriteBufferSize = value;
        return ERROR_SUCCESS;
    case WINHTTP_OPTION_SECURITY_FLAGS:
        if (length != 4)
            return ERROR_INSUFFICIENT_BUFFER;
        if (!request)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        if (value & ~0x3304u)
            return ERROR_INVALID_PARAMETER;
        object->SecurityFlags = value;
        return ERROR_SUCCESS;
    case WINHTTP_OPTION_PROXY: {
        if (!session && !request)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        if (length != sizeof(WINHTTP_PROXY_INFO))
            return ERROR_INSUFFICIENT_BUFFER;
        WINHTTP_PROXY_INFO *info = buffer;
        if (info->dwAccessType == WINHTTP_ACCESS_TYPE_NAMED_PROXY && (!info->lpszProxy || !*info->lpszProxy))
            return ERROR_INVALID_PARAMETER;
        return SetProxy(object, info->dwAccessType, info->lpszProxy, info->lpszProxyBypass);
    }
    case WINHTTP_OPTION_USER_AGENT: {
        if (!session && !request)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        DWORD count = 0;
        while (buffer && count < length && ((LPCWSTR)buffer)[count])
            count++;
        PWSTR agent = WhDupWn(buffer ? buffer : L"", count);
        if (!agent)
            return ERROR_NOT_ENOUGH_MEMORY;
        WhFree(object->Agent);
        object->Agent = agent;
        return ERROR_SUCCESS;
    }
    case WINHTTP_OPTION_CONTEXT_VALUE:
        if (length != sizeof(DWORD_PTR))
            return ERROR_INSUFFICIENT_BUFFER;
        if (!object)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        object->Context = *(DWORD_PTR *)buffer;
        return ERROR_SUCCESS;
    case WINHTTP_OPTION_CLIENT_CERT_CONTEXT:
        if (!request)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        if (!object->Secure)
            return ERROR_WINHTTP_INCORRECT_HANDLE_STATE;
        return ERROR_SUCCESS;
    case WINHTTP_OPTION_REQUEST_PRIORITY:
        if (!request)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        return length == 4 ? ERROR_SUCCESS : ERROR_INSUFFICIENT_BUFFER;
    case WINHTTP_OPTION_HTTP_VERSION: {
        if (length != 8)
            return ERROR_INSUFFICIENT_BUFFER;
        DWORD *version = buffer;
        if (!version[0] || version[1] > 1)
            return ERROR_INVALID_PARAMETER;
        DefaultMajor = version[0];
        DefaultMinor = version[1];
        return ERROR_SUCCESS;
    }
    case WINHTTP_OPTION_DISABLE_FEATURE:
        if (length < 4)
            return ERROR_INVALID_PARAMETER;
        if (!request)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        object->DisableFeatures |= value;
        if (value & WINHTTP_DISABLE_REDIRECTS)
            object->RedirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        return ERROR_SUCCESS;
    case WINHTTP_OPTION_CODEPAGE:
        if (request || !object)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        return length == 4 ? ERROR_SUCCESS : ERROR_INSUFFICIENT_BUFFER;
    case WINHTTP_OPTION_MAX_CONNS_PER_SERVER:
    case WINHTTP_OPTION_MAX_CONNS_PER_1_0_SERVER:
        if (!session)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        if (length != 4)
            return ERROR_INSUFFICIENT_BUFFER;
        if (option == WINHTTP_OPTION_MAX_CONNS_PER_SERVER)
            object->MaxConnsPerServer = value ? value : 2;
        else
            object->MaxConnsPer10Server = value ? value : 2;
        return ERROR_SUCCESS;
    case WINHTTP_OPTION_ENABLE_FEATURE:
        if (length < 4)
            return ERROR_INVALID_PARAMETER;
        if (session)
            return value & ~2u ? ERROR_INVALID_PARAMETER : ERROR_SUCCESS;
        if (!request)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        return value & 2 ? ERROR_INVALID_PARAMETER : ERROR_SUCCESS;
    case WINHTTP_OPTION_WORKER_THREAD_COUNT:
        if (length < 4)
            return ERROR_INSUFFICIENT_BUFFER;
        if (WorkerThreads)
            return ERROR_WINHTTP_OPTION_NOT_SETTABLE;
        WorkerThreads = value;
        return ERROR_SUCCESS;
    case WINHTTP_OPTION_CONFIGURE_PASSPORT_AUTH:
    case WINHTTP_OPTION_PASSPORT_SIGN_OUT:
        return session ? ERROR_SUCCESS : ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
    case WINHTTP_OPTION_SECURE_PROTOCOLS:
        if (!session)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        if (length < 4)
            return ERROR_INSUFFICIENT_BUFFER;
        if (value & ~0x2AA8u)
            return ERROR_INVALID_PARAMETER;
        object->SecureProtocols = value;
        return ERROR_SUCCESS;
    case WINHTTP_OPTION_SPN:
        if (!request)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        if (length != 4 || value & ~1u)
            return ERROR_INVALID_PARAMETER;
        return ERROR_SUCCESS;
    case WINHTTP_OPTION_GLOBAL_PROXY_CREDS:
    case WINHTTP_OPTION_GLOBAL_SERVER_CREDS:
        return ERROR_SUCCESS;
    case WINHTTP_OPTION_UNLOAD_NOTIFY_EVENT:
        if (length != sizeof(HANDLE))
            return ERROR_INVALID_PARAMETER;
        return session ? ERROR_SUCCESS : ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
    case WINHTTP_OPTION_REJECT_USERPWD_IN_URL:
        if (!session && !request)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        if (length != 4)
            return ERROR_INVALID_PARAMETER;
        object->RejectUserPassword = value != 0;
        return ERROR_SUCCESS;
    case WINHTTP_OPTION_USE_GLOBAL_SERVER_CREDENTIALS:
        if (length != 4)
            return ERROR_INVALID_PARAMETER;
        return session ? ERROR_SUCCESS : ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
    case 102:
        return ERROR_NOT_SUPPORTED;
    case WINHTTP_OPTION_RECEIVE_PROXY_CONNECT_RESPONSE:
        if (length != 4)
            return ERROR_INVALID_PARAMETER;
        return request ? ERROR_SUCCESS : ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
    case WINHTTP_OPTION_UNSAFE_HEADER_PARSING:
        if (length != 4)
            return ERROR_INVALID_PARAMETER;
        if (!request)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        return value ? ERROR_SUCCESS : ERROR_INVALID_PARAMETER;
    case WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET:
        if (!request)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        if (length)
            return ERROR_INVALID_PARAMETER;
        if (object->UpgradeRequested || object->State)
            return ERROR_INVALID_PARAMETER;
        object->UpgradeRequested = TRUE;
        return ERROR_SUCCESS;
    case WINHTTP_OPTION_WEB_SOCKET_CLOSE_TIMEOUT:
    case WINHTTP_OPTION_WEB_SOCKET_KEEPALIVE_INTERVAL:
        if (type != WH_WEBSOCKET)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        return length == 4 ? ERROR_SUCCESS : ERROR_INVALID_PARAMETER;
    case WINHTTP_OPTION_DECOMPRESSION:
        if (length != 4)
            return ERROR_INVALID_PARAMETER;
        if (!session && !request && type != WH_CONNECT)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        if (value & ~WINHTTP_DECOMPRESSION_FLAG_ALL)
            return ERROR_INVALID_PARAMETER;
        object->Decompression = value;
        return ERROR_SUCCESS;
    case WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL:
    case WINHTTP_OPTION_IPV6_FAST_FALLBACK:
    case WINHTTP_OPTION_DISABLE_SECURE_PROTOCOL_FALLBACK:
    case WINHTTP_OPTION_TCP_KEEPALIVE:
    case WINHTTP_OPTION_TCP_FAST_OPEN:
    case WINHTTP_OPTION_TLS_FALSE_START:
    case WINHTTP_OPTION_IGNORE_CERT_REVOCATION_OFFLINE:
    case WINHTTP_OPTION_EXPIRE_CONNECTION:
    case WINHTTP_OPTION_REQUIRE_STREAM_END:
    case WINHTTP_OPTION_DISABLE_STREAM_QUEUE:
    case WINHTTP_OPTION_HTTP2_KEEPALIVE:
    case WINHTTP_OPTION_DISABLE_CERT_CHAIN_BUILDING:
    case WINHTTP_OPTION_FIRST_AVAILABLE_CONNECTION:
        return object ? ERROR_SUCCESS : ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
    case WINHTTP_OPTION_USERNAME:
    case WINHTTP_OPTION_PASSWORD:
    case WINHTTP_OPTION_PROXY_USERNAME:
    case WINHTTP_OPTION_PROXY_PASSWORD: {
        if (!request)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        DWORD count = 0;
        while (buffer && count < length && ((LPCWSTR)buffer)[count])
            count++;
        PWSTR text = WhDupWn(buffer ? buffer : L"", count);
        PWSTR *slot = option == WINHTTP_OPTION_USERNAME   ? &object->User
                      : option == WINHTTP_OPTION_PASSWORD ? &object->Password
                      : option == WINHTTP_OPTION_PROXY_USERNAME ? &object->ProxyUser
                                                                : &object->ProxyPassword;
        WhFree(*slot);
        *slot = text;
        if (option <= WINHTTP_OPTION_PASSWORD && !object->AuthScheme)
            object->AuthScheme = WINHTTP_AUTH_SCHEME_BASIC;
        if (option >= WINHTTP_OPTION_PROXY_USERNAME && !object->ProxyAuthScheme)
            object->ProxyAuthScheme = WINHTTP_AUTH_SCHEME_BASIC;
        return ERROR_SUCCESS;
    }
    }
    return ERROR_WINHTTP_INVALID_OPTION;
}

BOOL WINAPI WinHttpSetOption(HINTERNET handle, DWORD option, LPVOID buffer, DWORD length)
{
    WH_OBJECT *object = nullptr;
    if (handle) {
        object = WhReference(handle);
        if (!object)
            return FALSE;
    } else if (!GlobalOption(option)) {
        return WhFail(option - 1 <= 212 ? ERROR_WINHTTP_INCORRECT_HANDLE_TYPE : ERROR_WINHTTP_INVALID_OPTION);
    }
    DWORD error = SetOption(object, option, buffer, length);
    WhRelease(object);
    SetLastError(error);
    return !error;
}

static DWORD CopyOut(const void *data, DWORD size, LPVOID buffer, LPDWORD length)
{
    if (*length < size) {
        *length = size;
        return ERROR_INSUFFICIENT_BUFFER;
    }
    memcpy(buffer, data, size);
    *length = size;
    return ERROR_SUCCESS;
}

static DWORD CopyString(LPCWSTR text, LPVOID buffer, LPDWORD length)
{
    DWORD size = ((DWORD)wcslen(text) + 1) * sizeof(WCHAR);
    if (*length < size) {
        *length = size;
        return ERROR_INSUFFICIENT_BUFFER;
    }
    memcpy(buffer, text, size);
    *length = size - sizeof(WCHAR);
    return ERROR_SUCCESS;
}

/* the url of a request as WinHttpCreateUrl writes it. */
static PWSTR RequestUrl(WH_OBJECT *request)
{
    SIZE_T size = wcslen(request->Host) + wcslen(request->Path) + 32;
    PWSTR url = WhAlloc(size * sizeof(WCHAR));
    if (!url)
        return nullptr;
    BOOL default_port = request->HostPort == (request->Secure ? 443 : 80);
    if (default_port)
        wsprintfW(url, L"%s://%s%s", request->Secure ? L"https" : L"http", request->Host, request->Path);
    else
        wsprintfW(url, L"%s://%s:%u%s", request->Secure ? L"https" : L"http", request->Host, request->HostPort,
                  request->Path);
    return url;
}

static DWORD QueryOption(WH_OBJECT *object, DWORD option, LPVOID buffer, LPDWORD length)
{
    DWORD type = object ? object->Type : 0;
    BOOL session = type == WH_SESSION, request = type == WH_REQUEST;
    DWORD value;
    switch (option) {
    case WINHTTP_OPTION_CALLBACK:
        if (!object)
            return ERROR_INVALID_HANDLE;
        return CopyOut(&object->Callback, sizeof(object->Callback), buffer, length);
    case WINHTTP_OPTION_RESOLVE_TIMEOUT:
    case WINHTTP_OPTION_CONNECT_TIMEOUT:
    case WINHTTP_OPTION_SEND_TIMEOUT:
    case WINHTTP_OPTION_RECEIVE_TIMEOUT:
    case WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT:
        if (!object)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        value = *TimeoutSlot(object, option);
        return CopyOut(&value, 4, buffer, length);
    case WINHTTP_OPTION_CONNECT_RETRIES:
        return CopyOut(&ConnectRetries, 4, buffer, length);
    case WINHTTP_OPTION_HANDLE_TYPE:
        value = type;
        return CopyOut(&value, 4, buffer, length);
    case WINHTTP_OPTION_READ_BUFFER_SIZE:
    case WINHTTP_OPTION_WRITE_BUFFER_SIZE:
        if (!request)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        value = option == WINHTTP_OPTION_READ_BUFFER_SIZE ? object->ReadBufferSize : object->WriteBufferSize;
        return CopyOut(&value, 4, buffer, length);
    case WINHTTP_OPTION_PARENT_HANDLE: {
        HINTERNET parent = object && object->Parent ? object->Parent->Handle : nullptr;
        return CopyOut(&parent, sizeof(parent), buffer, length);
    }
    case WINHTTP_OPTION_EXTENDED_ERROR:
        value = 0;
        return CopyOut(&value, 4, buffer, length);
    case WINHTTP_OPTION_SECURITY_FLAGS:
        if (!request)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        value = object->SecurityFlags;
        if (object->Secure && object->Conn && object->Conn->Tls)
            value |= SECURITY_FLAG_SECURE | SECURITY_FLAG_STRENGTH_STRONG;
        return CopyOut(&value, 4, buffer, length);
    case WINHTTP_OPTION_URL: {
        if (!request)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        PWSTR url = RequestUrl(object);
        if (!url)
            return ERROR_NOT_ENOUGH_MEMORY;
        DWORD error = CopyString(url, buffer, length);
        WhFree(url);
        return error;
    }
    case WINHTTP_OPTION_SECURITY_KEY_BITNESS:
        if (!request)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        value = object->Conn ? object->Conn->CipherStrength : 0;
        return CopyOut(&value, 4, buffer, length);
    case WINHTTP_OPTION_PROXY: {
        if (*length < sizeof(WINHTTP_PROXY_INFO)) {
            *length = sizeof(WINHTTP_PROXY_INFO);
            return ERROR_INSUFFICIENT_BUFFER;
        }
        WINHTTP_PROXY_INFO *info = buffer;
        memset(info, 0, sizeof(*info));
        *length = sizeof(*info);
        if (!object)
            return WhReadDefaultProxy(info) ? ERROR_SUCCESS : GetLastError();
        if (!session && !request)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        if (object->AccessType == WINHTTP_ACCESS_TYPE_DEFAULT_PROXY)
            return WhReadDefaultProxy(info) ? ERROR_SUCCESS : GetLastError();
        info->dwAccessType = object->AccessType == WINHTTP_ACCESS_TYPE_NAMED_PROXY ? WINHTTP_ACCESS_TYPE_NAMED_PROXY
                                                                                   : WINHTTP_ACCESS_TYPE_NO_PROXY;
        if (object->Proxy) {
            SIZE_T size = (wcslen(object->Proxy) + 1) * sizeof(WCHAR);
            if ((info->lpszProxy = GlobalAlloc(GMEM_FIXED, size)))
                memcpy(info->lpszProxy, object->Proxy, size);
        }
        if (object->ProxyBypass) {
            SIZE_T size = (wcslen(object->ProxyBypass) + 1) * sizeof(WCHAR);
            if ((info->lpszProxyBypass = GlobalAlloc(GMEM_FIXED, size)))
                memcpy(info->lpszProxyBypass, object->ProxyBypass, size);
        }
        return ERROR_SUCCESS;
    }
    case WINHTTP_OPTION_USER_AGENT:
        if (!session)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        return CopyString(object->Agent ? object->Agent : L"", buffer, length);
    case WINHTTP_OPTION_CONTEXT_VALUE:
        if (!object)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        return CopyOut(&object->Context, sizeof(object->Context), buffer, length);
    case WINHTTP_OPTION_HTTP_VERSION: {
        DWORD version[2] = {DefaultMajor, DefaultMinor};
        return CopyOut(version, sizeof(version), buffer, length);
    }
    case WINHTTP_OPTION_MAX_CONNS_PER_SERVER:
    case WINHTTP_OPTION_MAX_CONNS_PER_1_0_SERVER:
        if (!session)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        value = option == WINHTTP_OPTION_MAX_CONNS_PER_SERVER ? object->MaxConnsPerServer : object->MaxConnsPer10Server;
        return CopyOut(&value, 4, buffer, length);
    case WINHTTP_OPTION_SERVER_CERT_CONTEXT:
        if (!request)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        if (*length < sizeof(void *)) {
            *length = sizeof(void *);
            return ERROR_INSUFFICIENT_BUFFER;
        }
        return ERROR_WINHTTP_INCORRECT_HANDLE_STATE;
    case WINHTTP_OPTION_WORKER_THREAD_COUNT:
        return CopyOut(&WorkerThreads, 4, buffer, length);
    case WINHTTP_OPTION_AUTOLOGON_POLICY:
    case WINHTTP_OPTION_REDIRECT_POLICY:
    case WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS:
    case WINHTTP_OPTION_MAX_HTTP_STATUS_CONTINUE:
    case WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE:
    case WINHTTP_OPTION_MAX_RESPONSE_DRAIN_SIZE:
        if (!session && !request)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        value = option == WINHTTP_OPTION_REDIRECT_POLICY               ? object->RedirectPolicy
                : option == WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS ? object->MaxRedirects
                : option == WINHTTP_OPTION_MAX_HTTP_STATUS_CONTINUE     ? 10
                : option == WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE     ? object->MaxResponseHeaderSize
                : option == WINHTTP_OPTION_MAX_RESPONSE_DRAIN_SIZE      ? object->MaxResponseDrainSize
                                                                        : 0;
        return CopyOut(&value, 4, buffer, length);
    case WINHTTP_OPTION_SECURE_PROTOCOLS:
        if (!session)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        return CopyOut(&object->SecureProtocols, 4, buffer, length);
    case WINHTTP_OPTION_CONNECTION_INFO: {
        if (!request)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        struct {
            DWORD cbSize;
            SOCKADDR_STORAGE LocalAddress;
            SOCKADDR_STORAGE RemoteAddress;
        } info = {sizeof(info)};
        if (*length < sizeof(info)) {
            *length = sizeof(info);
            return ERROR_INSUFFICIENT_BUFFER;
        }
        if (!object->Conn)
            return ERROR_WINHTTP_INCORRECT_HANDLE_STATE;
        int size = sizeof(info.LocalAddress);
        getsockname(object->Conn->Socket, (struct sockaddr *)&info.LocalAddress, &size);
        size = sizeof(info.RemoteAddress);
        getpeername(object->Conn->Socket, (struct sockaddr *)&info.RemoteAddress, &size);
        return CopyOut(&info, sizeof(info), buffer, length);
    }
    case WINHTTP_OPTION_REJECT_USERPWD_IN_URL:
        if (!session && !request)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        value = object->RejectUserPassword;
        return CopyOut(&value, 4, buffer, length);
    case WINHTTP_OPTION_DECOMPRESSION:
        if (!session && !request && type != WH_CONNECT)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        return CopyOut(&object->Decompression, 4, buffer, length);
    case WINHTTP_OPTION_HTTP_PROTOCOL_USED:
        if (!request)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        value = 0;
        return CopyOut(&value, 4, buffer, length);
    case WINHTTP_OPTION_USERNAME:
    case WINHTTP_OPTION_PASSWORD:
    case WINHTTP_OPTION_PROXY_USERNAME:
    case WINHTTP_OPTION_PROXY_PASSWORD: {
        if (!request)
            return ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        PWSTR text = option == WINHTTP_OPTION_USERNAME   ? object->User
                     : option == WINHTTP_OPTION_PASSWORD ? object->Password
                     : option == WINHTTP_OPTION_PROXY_USERNAME ? object->ProxyUser
                                                               : object->ProxyPassword;
        return CopyString(text ? text : L"", buffer, length);
    }
    }
    if (option == WINHTTP_OPTION_CALLBACK || option == WINHTTP_OPTION_HANDLE_TYPE)
        return ERROR_INVALID_PARAMETER;
    return ERROR_WINHTTP_INVALID_OPTION;
}

BOOL WINAPI WinHttpQueryOption(HINTERNET handle, DWORD option, LPVOID buffer, LPDWORD length)
{
    if (!length)
        return WhFail(ERROR_INVALID_PARAMETER);
    if (!buffer)
        *length = 0;
    WH_OBJECT *object = nullptr;
    if (handle) {
        object = WhReference(handle);
        if (!object)
            return FALSE;
    } else if (!GlobalOption(option)) {
        return WhFail(option - 1 <= 212 ? ERROR_WINHTTP_INCORRECT_HANDLE_TYPE : ERROR_WINHTTP_INVALID_OPTION);
    }
    DWORD error = QueryOption(object, option, buffer, length);
    WhRelease(object);
    SetLastError(error);
    return !error;
}

/* ------------------------------------------------------------------ com and service entry points */

HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}

HRESULT WINAPI DllGetClassObject(const GUID *clsid, const GUID *iid, LPVOID *object)
{
    MIME_STUB("winhttp");
    if (object)
        *object = nullptr;
    return CLASS_E_CLASSNOTAVAILABLE;
}

void WINAPI SvchostPushServiceGlobals(LPVOID globals)
{
}

void WINAPI WinHttpAutoProxySvcMain(DWORD argc, LPWSTR *argv)
{
    MIME_STUB("winhttp");
}

DWORD WINAPI WinHttpPacJsWorkerMain(DWORD_PTR a, DWORD_PTR b)
{
    MIME_STUB("winhttp");
    return ERROR_NOT_SUPPORTED;
}
